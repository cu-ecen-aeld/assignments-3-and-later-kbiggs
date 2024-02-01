#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    echo "Building kernel"

    # Deep clean kernel build tree, removing existing configurations
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper

    # Configure for virtual ARM dev board
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig

    # Build kernel image for booting with QEMU
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all

    # Build kernel modules
    # Skipping for assignment 3
    #make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules

    # Build devicetree
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs

    # Move the generated image to outdir
    echo "Adding the Image in outdir"
    cp arch/${ARCH}/boot/Image ${OUTDIR}/Image
fi

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

echo "Building rootfs"
mkdir rootfs
cd rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log home/conf

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    echo "Setting up busybox"
    make distclean
    make defconfig
else
    cd busybox
fi

echo "Install busybox"
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX="${OUTDIR}/rootfs" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
${CROSS_COMPILE}readelf -a busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a busybox | grep "Shared library"

echo "Add library dependencies"
# Create toolchain sysroot
sysroot="$(aarch64-none-linux-gnu-gcc -print-sysroot)"

# Place program interpreter ld-linux-aarch64.so.1 into /lib
# Search by name in the sysroot path for the desired file
ld="$(find $sysroot -name ld-linux-aarch64.so.1)"
cp ${ld} "${OUTDIR}/rootfs/lib"

# Place libm.so.6 libresolv.so.2 libc.so.6 into /lib64
# Search by name in the sysroot path for the desired file
libm="$(find $sysroot -name libm.so.6)"
libresolv="$(find $sysroot -name libresolv.so.2)"
libc="$(find $sysroot -name libc.so.6)"
cp $libm $libresolv $libc "${OUTDIR}/rootfs/lib64"

echo "Making device nodes"
cd "${OUTDIR}/rootfs"
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 666 dev/console c 5 1

echo "Building writer utility"
cd ${FINDER_APP_DIR}
make CROSS_COMPILE=${CROSS_COMPILE}
cp writer "${OUTDIR}/rootfs/home"

echo "Copy finder scripts"
cp finder.sh finder-test.sh autorun-qemu.sh "${OUTDIR}/rootfs/home"
cp conf/username.txt conf/assignment.txt ${OUTDIR}/rootfs/home/conf

echo "Chown the root directory"
sudo chown root "${OUTDIR}/rootfs"

echo "Create initramfs.gz"
cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ../initramfs.cpio
