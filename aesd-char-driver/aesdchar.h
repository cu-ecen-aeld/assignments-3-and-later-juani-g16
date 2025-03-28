/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#include "aesd-circular-buffer.h"

struct aesd_dev
{
    /**
     * TODO: Add structure(s) and locks needed to complete assignment requirements
     */
    struct aesd_circular_buffer *buffer; /* Circular buffer to store data */
    struct aesd_buffer_entry entry;      /* Buffer entry to store data */
    unsigned long size;                  /* amount of data stored here */
    struct mutex lock;                   /* Mutex to protect read and write operations */
    struct cdev cdev;                    /* Char device structure      */
};

/*Function prototypes*/
void write_entry_to_buffer(struct aesd_dev *dev);
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos);
int aesd_setup_cdev(struct aesd_dev *dev);
int aesd_init_module(void);
void aesd_cleanup_module(void);

#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
