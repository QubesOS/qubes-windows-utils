#include "buffer.h"
#include "log.h"

// internal data structure
struct _CMQ_BUFFER
{
    UINT64 Size;      // buffer size
    BYTE *BufferStart;      // start of storage memory
    BYTE *DataStart; // pointer to the first byte of data. null = buffer empty
    BYTE *DataEnd;   // pointer to the first free byte. null = buffer full
};

// allocate memory and set variables
CMQ_BUFFER *CmqCreate(IN UINT64 bufferSize)
{
    CMQ_BUFFER *buffer = NULL;
    LogDebug("size: %lu bytes", bufferSize);

    if (bufferSize == 0)
    {
        LogWarning("size == 0");
        return NULL;
    }

    buffer = (CMQ_BUFFER *) malloc(sizeof(CMQ_BUFFER));
    if (!buffer)
    {
        LogWarning("out of memory");
        return NULL;
    }

    buffer->Size = bufferSize;
    buffer->BufferStart = (BYTE *) malloc(buffer->Size);
    buffer->DataStart = buffer->DataEnd = 0;
    if (buffer->BufferStart == 0)
    {
        free(buffer);
        LogWarning("no memory");
        return NULL;
    }

    return buffer;
}

// free memory and deinitialize
void CmqDestroy(IN CMQ_BUFFER *buffer)
{
    LogDebug("%p", buffer);

    free(buffer->BufferStart);
    free(buffer);
}

// zero-fill buffer, rewind internal pointer
void CmqClear(CMQ_BUFFER *buffer)
{
    LogDebug("%p", buffer);

    buffer->DataStart = buffer->DataEnd = 0;
    ZeroMemory(buffer->BufferStart, buffer->Size);
}

// queue data to the buffer
BOOL CmqAddData(IN CMQ_BUFFER *buffer, IN const void *inputData, IN UINT64 inputDataSize)
{
    BYTE *data = (BYTE *) inputData;
    UINT64 freeSize, freeAtEnd;

    LogVerbose("(%p, %p, %lu)", buffer, data, inputDataSize);

    if (buffer->DataEnd == NULL && buffer->DataStart != NULL)  // buffer full
    {
        LogWarning("(%p, %p, %lu): buffer full", buffer, data, inputDataSize);
        return FALSE;
    }

    if (inputDataSize == 0)
        return TRUE;

    if (buffer->DataStart == NULL) // empty, initialize pointers
    {
        buffer->DataStart = buffer->DataEnd = buffer->BufferStart;
    }

    freeSize = CmqGetFreeSize(buffer);
    //  =       <       <       <!      >
    // [.....] [oo...] [.oo..] [...oo] [o...o]
    if (inputDataSize > freeSize)
    {
        LogWarning("(%p, %p, %lu): buffer too small (free: %lu)", buffer, data, inputDataSize, freeSize);
        return FALSE;
    }

    if (buffer->DataStart > buffer->BufferStart && buffer->DataEnd < buffer->BufferStart + buffer->Size) // [.oo..]
    {
        freeAtEnd = (buffer->BufferStart + buffer->Size - buffer->DataEnd);
        if (inputDataSize <= freeAtEnd)
        {
            memcpy(buffer->DataEnd, data, inputDataSize);
        }
        else
        {
            memcpy(buffer->DataEnd, data, inputDataSize - freeAtEnd);
            memcpy(buffer->BufferStart, (data + inputDataSize - freeAtEnd), freeAtEnd);
        }
    }
    else
    {
        if (buffer->DataEnd == buffer->BufferStart + buffer->Size) // end at the limit, wrap to 0
            buffer->DataEnd = buffer->BufferStart;
        // now we have one consecutive block of free memory so just copy
        memcpy(buffer->DataEnd, data, inputDataSize);
    }

    buffer->DataEnd += inputDataSize;
    if (buffer->DataEnd > buffer->BufferStart + buffer->Size) // wrap only if end > limit, if end==limit pointer stays at the end
        buffer->DataEnd -= buffer->Size;

    if (freeSize - inputDataSize == 0) // buffer full
        buffer->DataEnd = NULL;

    return TRUE;
}

// "pop" data from the buffer
// underflow = true: don't treat underflow as errors (reading empty buffer is ok etc)
// dataSize = 0: read all (use carefully in case of buffer overruns)
BOOL CmqGetData(IN CMQ_BUFFER *buffer, OUT void *outputData, IN OUT UINT64 *dataSize, CMQ_UNDERFLOW_MODE underflowMode)
{
    BYTE *data = (BYTE *) outputData;
    UINT64 usedSize, usedAtEnd;

    LogVerbose("(%p, %p, %lu, %d)", buffer, data, dataSize, underflowMode);
    if (buffer->DataStart == NULL)  // buffer empty
    {
        *dataSize = 0;
        if ((underflowMode == CMQ_ALLOW_UNDERFLOW) || (*dataSize == 0))
            return TRUE;
        else
        {
            LogWarning("buffer empty");
            return FALSE;
        }
    }

    usedSize = CmqGetUsedSize(buffer);

    if (usedSize == buffer->Size) // buffer full, only dataStart is valid
        buffer->DataEnd = buffer->DataStart;

    //  full    <       >       <       <
    // [ooooo] [..ooo] [o..oo] [ooo..] [.ooo.]

    if (*dataSize == 0)   // "read all"
        *dataSize = usedSize;
    else
    {
        if (*dataSize > usedSize) // underflow
        {
            if (underflowMode == CMQ_ALLOW_UNDERFLOW)
                *dataSize = usedSize;
            else
            {
                LogWarning("underflow (requested %lu, got %lu)", *dataSize, usedSize);
                *dataSize = 0;
                return FALSE;
            }
        }
    }

    if (buffer->DataStart >= buffer->DataEnd) // [o..oo] should be safe for full buffer too (_pDataEnd == _pDataStart)
    {
        usedAtEnd = (buffer->BufferStart + buffer->Size - buffer->DataStart);
        if (*dataSize <= usedAtEnd)
            memcpy(data, buffer->DataStart, *dataSize);
        else
        {
            memcpy(data, buffer->DataStart, usedAtEnd);
            memcpy((data + usedAtEnd), buffer->BufferStart, *dataSize - usedAtEnd);
        }
    }
    else
    {
        // now we have one consecutive block of data memory so just copy
        memcpy(data, buffer->DataStart, *dataSize);
    }

    buffer->DataStart += *dataSize;
    if (buffer->DataStart >= buffer->BufferStart + buffer->Size)
        buffer->DataStart -= buffer->Size;

    if (usedSize - *dataSize == 0) // buffer empty
    {
        buffer->DataStart = NULL;
    }

    return TRUE;
}

// get used data size
UINT64 CmqGetUsedSize(IN const CMQ_BUFFER *buffer)
{
    if (buffer->Size == 0 || buffer->BufferStart == NULL || buffer->DataStart == NULL)
        return 0;

    if (buffer->DataEnd == NULL)  // full
        return buffer->Size;

    if (buffer->DataStart < buffer->DataEnd) // data in one consecutive block
        return (buffer->DataEnd - buffer->DataStart);
    else
        return (buffer->DataEnd + buffer->Size - buffer->DataStart);
}

UINT64 CmqGetFreeSize(IN const CMQ_BUFFER *buffer)
{
    return buffer->Size - CmqGetUsedSize(buffer);
}
