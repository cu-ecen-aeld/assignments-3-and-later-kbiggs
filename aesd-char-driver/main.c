/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/slab.h>
#include "aesdchar.h"
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Katie Biggs");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    filp->private_data = container_of(inode->i_cdev, struct aesd_dev, cdev);
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t ret_offset = 0;
    ssize_t bytes_to_read_out = 0;
    struct aesd_buffer_entry *ret_entry = NULL;
    struct aesd_dev *aesd_dev = NULL;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    
    // check for filp and buf being valid
    if (!filp || !buf)
    {
        return -EINVAL;
    }

    // use filp private_data to get aesd_dev
    aesd_dev = filp->private_data;

    if (!aesd_dev)
    {
        PDEBUG("Unable to use private data");
        return -EPERM;
    }

    // lock mutex before reading from circular buffer
    if (mutex_lock_interruptible(&aesd_dev->buf_mutex))
    {
        PDEBUG("Unable to lock mutex for read");
        return -ERESTARTSYS;
    }

    // start read at fpos
    // ret_offset gets the location within a single entry (the returned entry) corresponding to f_pos
    ret_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&aesd_dev->buffer, *f_pos, &ret_offset);

    // if entry is still null, then we weren't able to read anything at f_pos
    // so we must be at end of file
    if (!ret_entry)
    {
        PDEBUG("Nothing left to read");
        mutex_unlock(&aesd_dev->buf_mutex);
        return bytes_to_read_out;
    }
    
    // determine how many bytes are left to read in this individual entry
    ssize_t bytes_left_in_entry = ret_entry->size - ret_offset;

    // ensure we aren't writing out more bytes than allowed by count param
    if (bytes_left_in_entry > count)
    {
        bytes_to_read_out = count;
    }
    else
    {
        bytes_to_read_out = bytes_left_in_entry;
    }
    
    // use copy_to_user to fill buffer with what we have read so far and return back to user
    if (copy_to_user(buf, ret_entry->buffptr+ret_offset, bytes_to_read_out))
    {
        PDEBUG("Unable to copy buffer contents back to user");
        mutex_unlock(&aesd_dev->buf_mutex);
        return -EFAULT;
    }

    // move f_pos forward according to the number of bytes we've read
    *f_pos = *f_pos + bytes_to_read_out;

    // unlock mutex & return the number of bytes read out
    mutex_unlock(&aesd_dev->buf_mutex);
    return bytes_to_read_out;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    ssize_t bytes_to_write = 0;
    struct aesd_dev *aesd_dev = NULL;
    char *write_buf = NULL;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    // check for filp and buff being valid
    if (!filp || !buf)
    {
        return -EINVAL;
    }

    // allocate memory as each write command is received
    // use kmalloc and check for errors with alloc being too big
    write_buf = kmalloc(count, GFP_KERNEL);
    if (!write_buf)
    {
        PDEBUG("Unable to allocate memory for write");
        return -ENOMEM;
    }

    // use filp private_data to get aesd_dev
    aesd_dev = filp->private_data;

    if (!aesd_dev)
    {
        PDEBUG("Unable to use private data");
        return -EPERM;
    }

    // copy buffer from user space into kernel buffer
    if (copy_from_user(write_buf, buf, count))
    {
        PDEBUG("Unable to copy buffer to kernel for writing");
        kfree(write_buf);
        return -EFAULT;
    }
    
    // check to see if there's a newline in the input
    char * new_line_found = memchr(write_buf, '\n', count);

    if (new_line_found)
    {
        bytes_to_write = 1 + (new_line_found - write_buf);
    }
    else
    {
        bytes_to_write = count;
    }    

    // lock mutex before writing to circular buffer
    if (mutex_lock_interruptible(&aesd_dev->buf_mutex))
    {
        PDEBUG("Unable to lock mutex for read");
        kfree(write_buf);
        return -ERESTARTSYS;
    }

    // realloc working entry so that we can store the new contents to write
    aesd_dev->working_entry.buffptr = krealloc(aesd_dev->working_entry.buffptr,
                                               aesd_dev->working_entry.size+bytes_to_write,
                                               GFP_KERNEL);
    if (!aesd_dev->working_entry.buffptr)
    {
        PDEBUG("Unable to reallocate for the new write command addition");
        mutex_unlock(&aesd_dev->buf_mutex);
        kfree(write_buf);
        return -ENOMEM;
    }

    // copy the most recent write buffer into working entry
    // use the working_entry.size so that we start copying at the end of the entry
    memcpy(aesd_dev->working_entry.buffptr+aesd_dev->working_entry.size,
           write_buf, bytes_to_write);

    aesd_dev->working_entry.size += bytes_to_write;

    // add into circular buffer once full packet is received    
    if (new_line_found)
    {
        struct aesd_buffer_entry new_entry = {0};
        const char *ret_buf = NULL;
        new_entry.buffptr = aesd_dev->working_entry.buffptr;
        new_entry.size    = aesd_dev->working_entry.size;

        // more than 10 writes should free the oldest
        // if the add entry has returned non-null, free
        ret_buf = aesd_circular_buffer_add_entry(&aesd_dev->buffer, &new_entry);
        if (ret_buf)
        {
            kfree(ret_buf);
        }

        aesd_dev->working_entry.size = 0;
        aesd_dev->working_entry.buffptr = NULL;
    }    

    // unlock mutex
    mutex_unlock(&aesd_dev->buf_mutex);
    kfree(write_buf);

    // update fpos
    *f_pos = *f_pos + count;

    // return number of bytes written
    // if nothing written, return 0
    retval = count;

    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    loff_t retval = 0;
    struct aesd_dev *aesd_dev = NULL;
    
    // check for filp being valid
    if (!filp)
    {
        return -EINVAL;
    }

    // use filp private_data to get aesd_dev
    aesd_dev = filp->private_data;

    if (!aesd_dev)
    {
        PDEBUG("Unable to use private data");
        return -EPERM;
    }

    // lock mutex before reading from circular buffer
    if (mutex_lock_interruptible(&aesd_dev->buf_mutex))
    {
        PDEBUG("Unable to lock mutex for read");
        return -ERESTARTSYS;
    }

    // calculate the size of all the buffer contents
    loff_t buf_size = 0;

    int idx = 0;
    struct aesd_buffer_entry *entry = NULL;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_dev->buffer, idx)
    {
       buf_size += entry->size;
    }

    retval = fixed_size_llseek(filp, off, whence, buf_size);

    if (retval < 0)
    {
        retval = -EINVAL;
    }
    else
    {
        filp->f_pos = retval;
    }

    // unlock mutex
    mutex_unlock(&aesd_dev->buf_mutex);

    return retval;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *aesd_dev = NULL;
    struct aesd_seekto seek_to;
    
    // check for filp being valid
    if (!filp)
    {
        return -EINVAL;
    }

    // use filp private_data to get aesd_dev
    aesd_dev = filp->private_data;

    if (!aesd_dev)
    {
        PDEBUG("Unable to use private data");
        return -EPERM;
    }

    // check for cmd being valid
    if (cmd != AESDCHAR_IOCSEEKTO)
    {
        return -ENOTTY;
    }

    // copy from userspace
    if (copy_from_user(&seek_to, (const void __user *)arg, sizeof(seek_to)))
    {
        return -EFAULT;
    }

    if (mutex_lock_interruptible(&aesd_dev->buf_mutex))
    {
        PDEBUG("Unable to lock mutex for read");
        return -ERESTARTSYS;
    }

    // bounds check the number of commands and command length
    if ((seek_to.write_cmd > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) ||
        (seek_to.write_cmd_offset > aesd_dev->buffer.entry[seek_to.write_cmd].size))
    {
        return -EINVAL;
    }

    // update fpos (starting offset of the command + write cmd offset)
    long offset = 0;
    size_t buf_idx = 0;
    for (buf_idx = 0; buf_idx < seek_to.write_cmd; buf_idx++)
    {
        offset += aesd_dev->buffer.entry[buf_idx].size;
    }

    mutex_unlock(&aesd_dev->buf_mutex);

    filp->f_pos = offset + seek_to.write_cmd_offset;

    return 0;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .llseek =   aesd_llseek,
    .read =     aesd_read,
    .write =    aesd_write,
    .unlocked_ioctl = aesd_ioctl,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /* initialize the AESD specific portion of the device */

    // init mutex and circular buffer so they are ready/available when driver is loaded
    mutex_init(&aesd_device.buf_mutex);

    aesd_circular_buffer_init(&aesd_device.buffer);

    result = aesd_setup_cdev(&aesd_device);

    if (result)
    {
        unregister_chrdev_region(dev, 1);
    }

    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /* cleanup AESD specific poritions here as necessary */
    aesd_device.working_entry.buffptr = NULL;

    int idx = 0;
    struct aesd_buffer_entry *entry = NULL;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, idx)
    {
       kfree(entry->buffptr);
    }

    mutex_destroy(&aesd_device.buf_mutex);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
