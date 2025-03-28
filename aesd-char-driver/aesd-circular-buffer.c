/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset. Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
                                                                          size_t char_offset, size_t *entry_offset_byte_rtn)
{
    // if circular buffer does not exist, exit. C-array cannot be empty
    if (!buffer)
    {
        return NULL;
    }
    // Beware of pointer arithmetic and overflows, use int values.
    int16_t accumulated_length = 0;
    int8_t entry_id = buffer->out_offs;
    int16_t offset = char_offset;

    // Iterating through circular buffer elements
    while (char_offset > accumulated_length)
    {
        // For each iteration, we accumulate the char number of each entry in accumulated_length, and we point to
        // the next entry. We keep the remaining char_offset in offset
        accumulated_length += buffer->entry[entry_id].size;
        entry_id = (entry_id + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        offset = char_offset - accumulated_length;

        // If offset is negative, it means we went too far, and the remaining offset is targeting a character within the
        // current entry_id. So we add back the string size to the offset. Entering this if means we will exit the loop
        // in the next iteration.
        if (offset < 0)
        {
            PDEBUG("entry_id %d, buffer->entry[entry_id].size %ld, and offset is %d\n", entry_id, buffer->entry[entry_id].size, offset);
            offset += buffer->entry[entry_id].size;
        }

        // The char_offset is higher than the total number of written elements in the circular buffer.
        // We detect this if we went back to the initial element (out_offs), and char offset is higher than the total
        // number of chars we have accumulated. Both conditions together means char_offset is bigger than the total number
        // of char in the buffer
        int16_t max_position = accumulated_length - 1;
        if ((entry_id == buffer->out_offs) && (char_offset > max_position))
        {
            return NULL;
        }
    }
    PDEBUG("offset is %d, accumulated_length is %d, and string is %s\n", offset, accumulated_length, buffer->entry[entry_id].buffptr);
    *entry_offset_byte_rtn = offset;
    return &(buffer->entry[entry_id]);
}

/**
 * Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
 * If the buffer was already full, replace the oldest entry and advances buffer->out_offs to the
 * new start location. Returns pointer to the oldest entry.
 * Any necessary locking must be handled by the caller
 * Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
 */
const char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    const char *resultP = NULL;
    // Printing %s leads to a strange behavior, because it only stops at the first \0 char, which we are not writing. So the print
    // overflows and print until a \0 is found in memory. Adding this \0 during write is not that trivial, hence not done yet.
    PDEBUG("Writing element %s at write pointer %d, read pointer is %d\n", add_entry->buffptr, buffer->in_offs, buffer->out_offs);
    // Check if the current circular buffer pointer is valid
    if (buffer->in_offs >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        PDEBUG("Error, the input circular buffer is invalid");
        return resultP;
    }

    // If buffer full, make room for a new element
    if (buffer->full)
    {
        PDEBUG("Buffer full, Replacing entries!");
        resultP = buffer->entry[buffer->in_offs].buffptr;
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // If write buffer pointer about to catch read buffer, it means the buffer will be full
    // after adding the current element
    if ((buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED == buffer->out_offs)
    {
        buffer->full = true;
    }

    // Replace entry in the circular buffer
    buffer->entry[buffer->in_offs].buffptr = add_entry->buffptr;
    buffer->entry[buffer->in_offs].size = add_entry->size;
    PDEBUG("Writen %s at write pointer %d", buffer->entry[buffer->in_offs].buffptr, buffer->in_offs);
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    return resultP;
}

/**
 * Initializes the circular buffer described by @param buffer to an empty struct
 */
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}
