# Assignment 7 Faulty Driver Analysis

This analysis will utilize the kernel oops message to identify where the faulty line in the kernel driver "faulty" is

## System Setup

- The faulty driver source code can be found at [kbiggs assignment 7 repo](https://github.com/cu-ecen-aeld/assignment-7-kbiggs), and was cloned from the [original ldd3 repo](https://github.com/cu-ecen-aeld/ldd3).
- The buildroot environment was setup based on [kbiggs assignment 5 repo](https://github.com/cu-ecen-aeld/assignment-5-kbiggs)

## Commands Run

1. `./build.sh` was run to build and create the necessary buildroot files
2. `./runqemu.sh` was run to start the QEMU instance for our test
3. Once in the active QEMU instance, the command `echo "hello_world" > /dev/faulty` was run to trigger the creation of the kernel oops

## Kernel Oops Message

Below is the oops message that was output when we tried to write to the faulty driver.
```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x96000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=00000000420fc000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 96000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 160 Comm: sh Tainted: G           O      5.15.18 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x14/0x20 [faulty]
lr : vfs_write+0xa8/0x2b0
sp : ffffffc008d1bd80
x29: ffffffc008d1bd80 x28: ffffff80020e3300 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000040001000 x22: 000000000000000c x21: 0000005583f32a50
x20: 0000005583f32a50 x19: ffffff8002094600 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc0006f7000 x3 : ffffffc008d1bdf0
x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x14/0x20 [faulty]
 ksys_write+0x68/0x100
 __arm64_sys_write+0x20/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x40/0xa0
 el0_svc+0x20/0x60
 el0t_64_sync_handler+0xe8/0xf0
 el0t_64_sync+0x1a0/0x1a4
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace b363ae8815ff77b4 ]---
```

## Oops Message Analysis

We can see that there is an error message in the oops, pointing us to the fact that the kernel tried to dereference a NULL address.
```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
```

Looking at the call trace and program counter we can see that the culprit looks to be the faulty_write function, which is being called from the ksys_write function.
```
pc : faulty_write+0x14/0x20 [faulty]

Call trace:
 faulty_write+0x14/0x20 [faulty]
 ksys_write+0x68/0x100
 __arm64_sys_write+0x20/0x30
 ```

 Looking at the code in [faulty.c](https://github.com/cu-ecen-aeld/assignment-7-kbiggs/blob/master/misc-modules/faulty.c), it is easy to see that faulty_write is trying to dereference a NULL pointer.
 ```
 ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
		loff_t *pos)
{
	/* make a simple fault by dereferencing a NULL pointer */
	*(int *)0 = 0;
	return 0;
}
```

## Optional objdump Analysis

Another potential method of figuring out where there is a problem in the faulty driver code is to use objdump.

The command `buildroot/output/host/bin/aarch64-linux-objdump -S buildroot/output/target/lib/modules/5.15.18/extra/faulty.ko` can be run to examine the assembly output.
Looking at the below output, we can narrow our inspection down to faulty_write since we identified that from the call trace. Looking at the instructions we can see that we are trying to move null addresses into registers.

```
buildroot/output/target/lib/modules/5.15.18/extra/faulty.ko:     file format elf64-littleaarch64

Disassembly of section .text:

0000000000000000 <faulty_write>:
   0:   d503245f        bti     c
   4:   d2800001        mov     x1, #0x0                        // #0
   8:   d2800000        mov     x0, #0x0                        // #0
   c:   d503233f        paciasp
  10:   d50323bf        autiasp
  14:   b900003f        str     wzr, [x1]
  18:   d65f03c0        ret
  1c:   d503201f        nop
  ```