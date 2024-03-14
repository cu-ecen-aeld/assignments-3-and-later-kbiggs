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
    ssize_t retval = 0;
    ssize_t ret_offset = 0;
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

    // lock mutex
    if (mutex_lock_interruptible(&aesd_dev->dev_mutex))
    {
        PDEBUG("Unable to lock mutex for read");
        return -ERESTARTSYS;
    }

    // start read at fpos
    // ret_offset gets the location within the returned entry
    ret_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&aesd_dev->buffer, *f_pos, &ret_offset);

    // if entry is still null, then we weren't able to read anything at f_pos
    // so we must be at end of file
    if (!ret_entry)
    {
        PDEBUG("Nothing left to read");
        mutex_unlock(&aesd_dev->dev_mutex);
        return 0;
    }
    
    // can return a single write command/circular buffer entry
    // determine how many bytes are left to read in this individual entry
    ssize_t bytes_left_in_entry = ret_entry->size - ret_offset;

    // ensure we aren't writing out more bytes than allowed by count param
    if (bytes_left_in_entry > count)
    {
        bytes_left_in_entry = count;
    }
    
    // use copy_to_user to fill buffer with what we have read so far and return back to user
    if (copy_to_user(buf, ret_entry->buffptr+ret_offset, bytes_left_in_entry))
    {
        PDEBUG("Unable to copy buffer contents back to user");
        mutex_unlock(&aesd_dev->dev_mutex);
        return -EFAULT;
    }

    // move f_pos forward according to the number of bytes we've read
    *f_pos = *f_pos + bytes_left_in_entry;

    // return number of bytes read
    retval =  bytes_left_in_entry;   

    // unlock mutex
    mutex_unlock(&aesd_dev->dev_mutex);
    return retval;
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

    // allocate (and realloc) memory as each write command is receive
    // use kmalloc
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

    // lock mutex
    if (mutex_lock_interruptible(&aesd_dev->dev_mutex))
    {
        PDEBUG("Unable to lock mutex for read");
        kfree(write_buf);
        return -ERESTARTSYS;
    }

    // copy buffer from user space
    if (copy_from_user(write_buf, buf, count))
    {
        PDEBUG("Unable to copy buffer to kernel for writing");
        mutex_unlock(&aesd_dev->dev_mutex);
        kfree(write_buf);
        return -EFAULT;
    }
    
    // check to see if we've found a newline yet
    char * new_line_found = memchr(write_buf, '\n', count);

    if (new_line_found)
    {
        bytes_to_write = 1 + (new_line_found - write_buf);
    }
    else
    {
        bytes_to_write = count;
    }

    // append to command being written until there's a newline
    aesd_dev->working_entry.buffptr = krealloc(aesd_dev->working_entry.buffptr,
                                               aesd_dev->working_entry.size+bytes_to_write,
                                               GFP_KERNEL);
    if (!aesd_dev->working_entry.buffptr)
    {
        PDEBUG("Unable to reallocate for the new write command addition");
        mutex_unlock(&aesd_dev->dev_mutex);
        kfree(write_buf);
        return -ENOMEM;
    }

    // copy the most recent write buffer into working entry
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
    mutex_unlock(&aesd_dev->dev_mutex);
    kfree(write_buf);

    // return number of bytes written
    // if nothing written, return 0
    retval = count;

    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
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
    mutex_init(&aesd_device.dev_mutex);

    aesd_circular_buffer_init(&aesd_device.buffer);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
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

    mutex_destroy(&aesd_device.dev_mutex);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
