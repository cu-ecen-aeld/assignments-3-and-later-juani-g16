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


#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
