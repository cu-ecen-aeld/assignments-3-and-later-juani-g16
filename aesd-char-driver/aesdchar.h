/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#include "aesd-circular-buffer.h"
#include "aesd_ioctl.h"

#define AESD_DEBUG 1 // Remove comment on this line to enable debug

#undef PDEBUG /* undef it, just in case */
#ifdef AESD_DEBUG
#ifdef __KERNEL__
/* This one if debugging is on, and kernel space */
#define PDEBUG(fmt, args...) printk(KERN_DEBUG "aesdchar: " fmt, ##args)
#else
/* This one for user space */
#define PDEBUG(fmt, args...) fprintf(stderr, fmt, ##args)
#endif
#else
#define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

// Functions prototypes
void clean_aesd(void);
int checkEOLChar(const char *buff, const int size);
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
int aesd_init_module(void);
void aesd_cleanup_module(void);
long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
loff_t aesd_llseek(struct file *filp, loff_t offset, int whence);
static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset);

struct aesd_dev
{
    struct aesd_buffer_entry entry;      /* buffer for partial data     */
    struct aesd_circular_buffer bufferP; /* data buffer                 */
    int readCounter;                     /* End condition for read loop */
    unsigned long size;                  /* amount of data stored here */
    struct mutex lock; /* mutual exclusion semaphore  */
    struct cdev cdev;  /* Char device structure       */
};

// This prototype has to happen after its input definition
void write_entry_into_buffer(struct aesd_dev *dev);

#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
