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
#include "aesdchar.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Juan I. Giorgetti"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

void write_entry_to_buffer(struct aesd_dev *dev)
{
    if (dev->entry.buffptr[dev->entry.size - 1] == '\n')
    {
        const char *removed_entry = aesd_circular_buffer_add_entry(dev->buffer, &(dev->entry));
        if (removed_entry)
        {
            PDEBUG("Removed entry: %s", removed_entry);
            kfree(removed_entry);
        }
        dev->entry.buffptr = NULL;
        dev->entry.size = 0;
    }
}

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("aesd open");

    struct aesd_dev *dev; /* device information */
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev; /* for other methods */

    PDEBUG("End of aesd open");

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_buffer_entry *entry;
    size_t entry_offset = 0;
    size_t bytes_read = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    struct aesd_dev *dev = filp->private_data;

    if (mutex_lock_interruptible(&dev->lock))
    {
        retval = -ERESTARTSYS;
        PDEBUG("failed to lock mutex");
        goto no_mutex_out;
    }

    // Read is past the end of the file
    if (*f_pos >= dev->size)
    {
        PDEBUG("Read past end of file");
        goto mutex_out;
    }

    // Read too much data
    if (*f_pos + count > dev->size)
    {
        count = dev->size - *f_pos;
    }

    // Find the entry in the circular buffer
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(dev->buffer, *f_pos, &entry_offset);

    // Check if the entry is found in the circular buffer
    if (entry == NULL)
    {
        PDEBUG("No entry found in circular buffer");
        retval = -EFAULT;
        goto mutex_out;
    }

    bytes_read = entry->size - entry_offset;
    if (bytes_read < 0)
    {
        PDEBUG("Invalid bytes read");
        retval = -EFAULT;
        goto mutex_out;
    }

    if (count > bytes_read)
    {
        count = bytes_read;
    }
    // Copy the data from the entry to userspace
    if (copy_to_user(buf, entry->buffptr + entry_offset, count))
    {
        PDEBUG("Failed to copy data to user");
        retval = -EFAULT;
        goto mutex_out;
    }
    // Update the file position
    *f_pos += count;
    retval = count;
    PDEBUG("Read %zu bytes from offset %zu", count, *f_pos);

mutex_out:
    PDEBUG("Unlocking mutex");
    mutex_unlock(&dev->lock);
    return retval;

no_mutex_out:
    PDEBUG("read failed, returning %zd", retval);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    struct aesd_dev *dev = filp->private_data;

    // Lock the mutex
    if (mutex_lock_interruptible(&dev->lock))
    {
        retval = -ERESTARTSYS;
        PDEBUG("failed to lock mutex");
        goto no_mutex_out;
    }

    // Partial write
    if (dev->entry.buffptr)
    {
        PDEBUG("Partial write going on");
        size_t remaining = dev->entry.size + count;
        char *new_buffptr = kmalloc(remaining, GFP_KERNEL);
        if (!new_buffptr)
        {
            PDEBUG("Failed to allocate memory for new_buffptr");
            goto mutex_out;
        }
        memcpy(new_buffptr, dev->entry.buffptr, dev->entry.size);

        // Copy the data from userspace to the new buffer in kernel space
        if (copy_from_user(new_buffptr + dev->entry.size, buf, count))
        {
            PDEBUG("Failed to copy data from user");
            retval = -EFAULT;
            goto mutex_out;
        }
        kfree(dev->entry.buffptr);
        dev->entry.buffptr = new_buffptr;
        dev->entry.size = remaining;

        write_entry_to_buffer(dev);
    }
    else
    {
        PDEBUG("New write");
        dev->entry.buffptr = kmalloc(count, GFP_KERNEL);
        dev->entry.size = count;
        if (!dev->entry.buffptr)
        {
            PDEBUG("Failed to allocate memory for buffptr");
            goto mutex_out;
        }

        if (copy_from_user(dev->entry.buffptr, buf, dev->entry.size))
        {
            PDEBUG("Failed to copy data from user");
            retval = -EFAULT;
            goto mutex_out;
        }
        write_entry_to_buffer(dev);
    }

    *f_pos += count;
    retval = count;

mutex_out:
    PDEBUG("Unlocking mutex");
    mutex_unlock(&dev->lock);
    return retval;

no_mutex_out:
    PDEBUG("write failed, returning %zd", retval);
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

    PDEBUG("Initializing aesdchar module\n");

    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * initialize the AESD specific portion of the device
     */

    aesd_device.entry.buffptr = NULL;
    aesd_device.entry.size = 0;
    aesd_device.size = 0;

    mutex_init(&aesd_device.lock);

    aesd_device.buffer = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    if (!aesd_device.buffer)
    {
        result = -ENOMEM;
        goto fail;
    }
    aesd_circular_buffer_init(aesd_device.buffer);

    /* end initialize the AESD specific portion of the device */

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }

    PDEBUG("Initialization complete with result: %d\n", result);
    return result;

fail:
    aesd_cleanup_module();
    return result;
}

void aesd_cleanup_module(void)
{
    PDEBUG("Cleaning up aesdchar module\n");
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    cdev_del(&aesd_device.cdev);

    /**
     * cleanup AESD specific poritions here as necessary
     */
    kfree(aesd_device.buffer);
    aesd_device.buffer = NULL;
    mutex_destroy(&aesd_device.lock);

    /**
     * end cleanup AESD specific poritions here as necessary
     */
    unregister_chrdev_region(devno, 1);

    PDEBUG("Cleanup complete\n");
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
