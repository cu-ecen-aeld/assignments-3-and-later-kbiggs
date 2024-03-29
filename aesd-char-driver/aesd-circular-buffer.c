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
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    if (!buffer)
    {
        return NULL;
    }
    if (!entry_offset_byte_rtn)
    {
        return NULL;
    }

    size_t byte_to_ret = 0;
    size_t offset = 0;
    size_t idx = buffer->out_offs;
    bool   found_match = false;

    // Want to capture the first check of whether offset is found
    do
    {
        // Increment overall offset by the size of this buffer entry
        offset += buffer->entry[idx].size;
        byte_to_ret = buffer->entry[idx].size;
        // If we've passed the desired offset, success
        if (offset > char_offset)
        {
            found_match = true;
            byte_to_ret -= (offset-char_offset);
            break;
        }

        // Increment index, accounting for wraparound
        idx++;
        idx %= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } while (idx != buffer->in_offs);

    // Return null if no offset was found
    if (!found_match)
    {
        return NULL;
    }

    *entry_offset_byte_rtn = byte_to_ret;
    return buffer->entry + idx;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
const char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    const char *buf_to_ret = NULL;
    
    if (!buffer)
    {
        return buf_to_ret;
    }
    if (!add_entry)
    {
        return buf_to_ret;
    }
    
    // If buffer is full, save off the pointer that we're going to return
    if (buffer->full)
    {
        buf_to_ret = buffer->entry[buffer->in_offs].buffptr;
    }

    // Add the entry to buffer and increment offset for the next addition
    buffer->entry[buffer->in_offs] = *add_entry;
    buffer->in_offs++;
    buffer->in_offs = buffer->in_offs % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // If buffer is full, also need to increase the output offset
    if (buffer->full)
    {
        buffer->out_offs++;
        buffer->out_offs = buffer->out_offs % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
    // If the in and out offset are equal then we know the buffer is full
    else if (buffer->in_offs == buffer->out_offs)
    {
        buffer->full = true;
    }

    return buf_to_ret;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
