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

#include <linux/cdev.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h> // kmalloc()
#include <linux/types.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Juan Ignacio Giorgetti");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t total_size = 0;
    int i, retval = 0;

    if (mutex_lock_interruptible(&dev->lock))
    {
        retval = -ERESTARTSYS;
        goto no_mutex_error;
    }

    // Validate the write_cmd index
    if (write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        retval = -EINVAL;
        goto end;
    }

    // Traverse the circular buffer to locate the specified write_cmd
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->bufferP, i)
    {
        if (i == write_cmd)
        {
            // Validate the write_cmd_offset
            if (write_cmd_offset >= entry->size)
            {
                retval = -EINVAL;
                goto end;
            }

            // Adjust the file offset
            filp->f_pos = total_size + write_cmd_offset;
            goto end;
        }
        total_size += entry->size;
    }

    goto end; // If the write_cmd was not found

end:
    mutex_unlock(&dev->lock);
    return retval;

no_mutex_error:
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t newpos;
    loff_t total_size = 0;

    // Calculate total bytes stored in buffer
    struct aesd_buffer_entry *entry;
    uint8_t index;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->bufferP, index)
    {
        total_size += entry->size;
    }

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    switch (whence)
    {
    case SEEK_SET:
        newpos = offset;
        break;
    case SEEK_CUR:
        newpos = filp->f_pos + offset;
        break;
    case SEEK_END:
        newpos = dev->bufferP.out_offs + offset;
        break;
    default:
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    if (newpos < 0 || newpos > total_size)
    {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }
    filp->f_pos = newpos;

    mutex_unlock(&dev->lock);

    PDEBUG("llseek: offset=%lld whence=%d -> new pos=%lld", offset, whence, newpos);

    return newpos;
}

void clean_aesd(void)
{
    uint8_t index;
    struct aesd_buffer_entry *entry;
    PDEBUG("Erase device");
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.bufferP, index)
    {
        kfree(entry->buffptr);
    }
}

// Return the position of the first EOL char in char array
int checkEOLChar(const char *buff, const int size)
{
    int i;
    for (i = 0; i < size; i++)
    {
        if (buff[i] == '\n')
        {
            return i;
        }
    }
    return -1;
}

// Check if single entry should be written into circular buffer
void write_entry_into_buffer(struct aesd_dev *dev)
{
    // Now check if current write has EOL character
    int EOLPos = checkEOLChar(dev->entry.buffptr, dev->entry.size);
    PDEBUG("Found EOL char at pos %d ", EOLPos);
    if (EOLPos == dev->entry.size - 1)
    {
        const char *entryToRemove = aesd_circular_buffer_add_entry(&(dev->bufferP), &(dev->entry));
        if (entryToRemove)
        {
            PDEBUG("Removing entry: %s", entryToRemove);
            kfree(entryToRemove);
        }
        // Remove the content from the entry buffer since moved to circular buffer
        dev->entry.buffptr = NULL;
        dev->entry.size = 0;
    }
    else if (EOLPos < 0)
    {
        // No EOL char, meaning we only store in entry buffer
        PDEBUG("Written in entry buffer");
    }
    else
    {
        // EOL char found inside the char array
        PDEBUG("Written in entry buffer");
    }
}

// System call implementation
int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");

    struct aesd_dev *dev; /* device information */
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev; /* for other methods */

    if (!dev->bufferP.full)
    {
        dev->bufferP.out_offs = 0;
    }
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
    PDEBUG("Request %zu bytes read with offset %lld", count, *f_pos);
    bool lastNonZeroReadCurrentEntry = false;
    struct aesd_dev *dev = filp->private_data;
    size_t entrySize = dev->bufferP.entry[dev->bufferP.out_offs].size;
    PDEBUG("Currently handling entry %u containing %zu bytes", dev->bufferP.out_offs, entrySize);

    if (mutex_lock_interruptible(&dev->lock))
    {
        return -ERESTARTSYS;
    }
    if (0 == entrySize)
    {
        PDEBUG("No entrie was written yet, so do nothing");
        goto out;
    }
    // Once the 10 last written buffer entries were read, stop the loop
    if (dev->readCounter == 10)
    {
        PDEBUG("Last 10 entries were read, return 0 bytes to stop reading");
        dev->readCounter = 0;
        goto out;
    }
    if (*f_pos + count > entrySize)
    {
        count = entrySize - *f_pos;
        lastNonZeroReadCurrentEntry = true;
        PDEBUG("Don t read outside the buffer entry, last read is only %zu", count);
    }

    if (copy_to_user(buf, dev->bufferP.entry[dev->bufferP.out_offs].buffptr, count))
    {
        retval = -EFAULT;
        goto out;
    }
    PDEBUG("Read %s, %zu bytes from entry %u of the circular buffer",
           dev->bufferP.entry[dev->bufferP.out_offs].buffptr,
           count,
           dev->bufferP.out_offs);
    *f_pos += count;
    retval = count;
    dev->readCounter += 1;
    // Next call, read next buffer entry
    if (lastNonZeroReadCurrentEntry)
    {
        dev->bufferP.out_offs = (dev->bufferP.out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        // Reinitialize position
        *f_pos = 0;
    }
out:
    PDEBUG("Returns %zd bytes with new offset %lld, new read pointer set to %u", retval, *f_pos, dev->bufferP.out_offs);
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("Request write %zu bytes with offset %lld", count, *f_pos);
    struct aesd_dev *dev = filp->private_data;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    // Partial write ongoing
    if (dev->entry.buffptr)
    {
        PDEBUG("Proceeding a partial write");
        int newSize = count + dev->entry.size;
        char *newString = kmalloc(newSize, GFP_KERNEL);
        memcpy(newString, dev->entry.buffptr, dev->entry.size);
        // Here we do pointer arithmetic to concatenate previous and new content
        if (copy_from_user(newString + dev->entry.size, buf, count))
        {
            retval = -EFAULT;
            goto out;
        }
        // Replace the entry buffer with the new content
        kfree(dev->entry.buffptr);
        dev->entry.buffptr = newString;
        dev->entry.size = newSize;

        write_entry_into_buffer(dev);
    }
    else
    {
        PDEBUG("Starting a new write session");
        // Since entry buffer was not used, we can directly allocate it
        dev->entry.buffptr = kmalloc(count, GFP_KERNEL);
        dev->entry.size = count;
        if (copy_from_user(dev->entry.buffptr, buf, dev->entry.size))
        {
            retval = -EFAULT;
            goto out;
        }
        write_entry_into_buffer(dev);
    }

    *f_pos += count;
    retval = count;
out:
    mutex_unlock(&dev->lock);
    PDEBUG("%zd bytes were written", retval);
    return retval;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
    .unlocked_ioctl = aesd_ioctl,
    .llseek = aesd_llseek,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    {
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
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    // Here, the device, the circular buffer and the circular buffer entry are initialized
    memset(&aesd_device, 0, sizeof(struct aesd_dev));
    aesd_circular_buffer_init(&aesd_device.bufferP);
    aesd_device.entry.buffptr = NULL;
    result = aesd_setup_cdev(&aesd_device);
    mutex_init(&aesd_device.lock);

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
    clean_aesd();
    unregister_chrdev_region(devno, 1);
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int err = 0;
    int retval = 0;
    /*
     * extract the type and number bitfields, and don't decode
     * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
     */
    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR)
        return -ENOTTY;

    switch (cmd)
    {
    case AESDCHAR_IOCSEEKTO:
        struct aesd_seekto seekto;
        PDEBUG("AESDCHAR_IOCSEEKTO");
        if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto) != 0))
        {
            retval = -EFAULT;
        }
        else
        {
            retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
        }
        break;
    default:
        retval = -ENOTTY;
        break;
    }
    return retval;
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
