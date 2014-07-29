#include "buffer.h"
#include "log.h"

struct _buffer
{
    size_t size;       // buffer size
    char  *start;      // start of storage memory
    char  *data_start; // pointer to the first byte of data. null = buffer empty
    char  *data_end;   // pointer to the first free byte. null = buffer full
};

// allocate memory and set variables
buffer_t *buffer_create(size_t buffer_size)
{
    buffer_t *buffer = NULL;
    LogDebug("buffer_create: %d bytes\n", buffer_size);

    if (buffer_size == 0)
    {
        LogWarning("buffer_create: size == 0\n");
        return NULL;
    }

    buffer = (buffer_t*) malloc(sizeof(buffer_t));
    if (!buffer)
    {
        LogWarning("buffer_create: out of memory\n");
        return NULL;
    }

    buffer->size = buffer_size;
    buffer->start = (char*) malloc(buffer->size);
    buffer->data_start = buffer->data_end = 0;
    if (buffer->start == 0)
    {
        free(buffer);
        LogWarning("buffer_create: no memory\n");
        return NULL;
    }

    return buffer;
}

// free memory and deinitialize
int buffer_destroy(buffer_t *buffer)
{
    LogDebug("buffer_destroy(0x%0x)\n", buffer);

    free(buffer->start);
    free(buffer);
    
    return 0;
}

// zero-fill buffer, rewind internal pointer
int buffer_clear(buffer_t *buffer)
{
    LogDebug("buffer_clear(0x%x)\n", buffer);

    buffer->data_start = buffer->data_end = 0;
    memset(buffer->start, 0, buffer->size);

    return 0;
}

// queue data to the buffer
int buffer_add_data(buffer_t *buffer, void *pdata, size_t data_size)
{
    char *data = (char*) pdata;
    size_t free_size, free_at_end;

    LogVerbose("buffer_add_data(0x%x, 0x%x, %d)\n", buffer, data, data_size);

    if (buffer->data_end == 0 && buffer->data_start != 0)  // buffer full
    {
        LogWarning("buffer_add_data(0x%x, 0x%x, %d): buffer full\n", buffer, data, data_size);
        return -1;
    }

    if (data_size == 0)
        return 0;

    if (buffer->data_start == 0) // empty, initialize pointers
    {
        buffer->data_start = buffer->data_end = buffer->start;
    }

    free_size = buffer_free_size(buffer);
    //  =       <       <       <!      >
    // [.....] [oo...] [.oo..] [...oo] [o...o]
    if (data_size > free_size)
    {
        LogWarning("buffer_add_data(0x%x, 0x%x, %d): buffer too small (free: %d)\n", buffer, data, data_size, free_size);
        return -1;
    }

    if (buffer->data_start > buffer->start && buffer->data_end < buffer->start+buffer->size) // [.oo..]
    {
        free_at_end = (buffer->start + buffer->size - buffer->data_end);
        if (data_size <= free_at_end)
            memcpy(buffer->data_end, data, data_size);
        else
        {
            memcpy(buffer->data_end, data, data_size-free_at_end);
            memcpy(buffer->start, (data+data_size-free_at_end), free_at_end);
        }
    }
    else
    {
        if (buffer->data_end == buffer->start+buffer->size) // end at the limit, wrap to 0
            buffer->data_end = buffer->start;
        // now we have one consecutive block of free memory so just copy
        memcpy(buffer->data_end, data, data_size);
    }

    buffer->data_end += data_size;
    if (buffer->data_end > buffer->start+buffer->size) // wrap only if end > limit, if end==limit pointer stays at the end
        buffer->data_end -= buffer->size;

    if (free_size - data_size == 0) // buffer full
        buffer->data_end = 0;

    return 0;
}

// "pop" data from the buffer
// underflow = true: don't treat underflow as errors (reading empty buffer is ok etc)
// data_size = 0: read all (use carefully in case of buffer overruns)
int buffer_get_data(buffer_t *buffer, void *pdata, size_t *data_size, BUFFER_UNDERFLOW_MODE underflow_mode)
{
    char *data = (char*) pdata;
    size_t used_size, used_at_end;

    LogVerbose("buffer_get_data(0x%x, 0x%x, 0x%x, %d)\n", buffer, data, data_size, underflow_mode);
    if (buffer->data_start == 0)  // buffer empty
    {
        *data_size = 0;
        if ((underflow_mode==BUFFER_ALLOW_UNDERFLOW) || (*data_size == 0))
            return 0;
        else
        {
            LogWarning("buffer_get_data: buffer empty\n");
            return -1;
        }
    }

    used_size = buffer_used_size(buffer);

    if (used_size == buffer->size) // buffer full, only _pDataStart is valid
        buffer->data_end = buffer->data_start;

    //  full    <       >       <       <
    // [ooooo] [..ooo] [o..oo] [ooo..] [.ooo.]

    if (*data_size == 0)   // "read all"
        *data_size = used_size;
    else
    {
        if (*data_size > used_size) // underflow
        {
            if (underflow_mode==BUFFER_ALLOW_UNDERFLOW)
                *data_size = used_size;
            else
            {
                *data_size = 0;
                LogWarning("buffer_get_data: underflow (requested %d, got %d)\n", *data_size, used_size);
                return -1;
            }
        }
    }

    if (buffer->data_start >= buffer->data_end) // [o..oo] should be safe for full buffer too (_pDataEnd == _pDataStart)
    {
        used_at_end = (buffer->start + buffer->size - buffer->data_start);
        if (*data_size <= used_at_end)
            memcpy(data, buffer->data_start, *data_size);
        else
        {
            memcpy(data, buffer->data_start, used_at_end);
            memcpy((data+used_at_end), buffer->start, *data_size-used_at_end);
        }
    }
    else
    {
        // now we have one consecutive block of data memory so just copy
        memcpy(data, buffer->data_start, *data_size);
    }

    buffer->data_start += *data_size;
    if (buffer->data_start >= buffer->start+buffer->size)
        buffer->data_start -= buffer->size;

    if (used_size - *data_size == 0) // buffer empty
    {
        buffer->data_start = 0;
    }

    return 0;
}

// get used data size
size_t buffer_used_size(buffer_t *buffer)
{
    if (buffer->size == 0 || buffer->start == 0 || buffer->data_start == 0)
        return 0;

    if (buffer->data_end == 0)  // full
        return buffer->size;

    if (buffer->data_start < buffer->data_end) // data in one consecutive block
        return (buffer->data_end - buffer->data_start);
    else
        return (buffer->data_end + buffer->size - buffer->data_start);
}

size_t buffer_free_size(buffer_t *buffer)
{
    return buffer->size - buffer_used_size(buffer);
}
