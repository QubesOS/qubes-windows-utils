/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (c) Invisible Things Lab
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include "buffer.h"
#include "log.h"

#include <stdlib.h>

// internal data structure
struct _CMQ_BUFFER
{
    UINT64 Size;       // buffer size
    BYTE *BufferStart; // start of storage memory
    BYTE *DataStart;   // pointer to the first byte of data. null = buffer empty
    BYTE *DataEnd;     // pointer to the last data byte
};

// allocate memory and set variables
CMQ_BUFFER *CmqCreate(IN UINT64 bufferSize)
{
    CMQ_BUFFER *buffer = NULL;
    LogDebug("size: 0x%llx bytes", bufferSize);

    if (bufferSize == 0)
    {
        LogWarning("size == 0");
        return NULL;
    }

    buffer = (CMQ_BUFFER *) malloc(sizeof(CMQ_BUFFER));
    if (!buffer)
    {
        LogError("out of memory");
        return NULL;
    }

    buffer->Size = bufferSize;
    buffer->BufferStart = (BYTE *) malloc(buffer->Size);
    buffer->DataStart = buffer->DataEnd = 0;
    if (buffer->BufferStart == 0)
    {
        free(buffer);
        LogError("out of memory");
        return NULL;
    }

    LogDebug("created %p", buffer);
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
void CmqClear(IN CMQ_BUFFER *buffer)
{
    LogDebug("%p", buffer);

    buffer->DataStart = buffer->DataEnd = 0;
    ZeroMemory(buffer->BufferStart, buffer->Size);
}

// queue data to the buffer
BOOL CmqAddData(IN CMQ_BUFFER *buffer, IN const void *inputData, IN UINT64 inputDataSize)
{
    BYTE *data = (BYTE *) inputData, *destination;
    UINT64 freeSize, startToEnd, endToEnd;

    LogVerbose("(%p, %p, %llx)", buffer, data, inputDataSize);

    if (buffer->DataEnd == NULL && buffer->DataStart != NULL)  // buffer full
    {
        LogDebug("(%p, %p, %llx): buffer full", buffer, data, inputDataSize);
        return FALSE;
    }

    if (inputDataSize == 0)
        return TRUE;

    freeSize = CmqGetFreeSize(buffer);

    if (inputDataSize > freeSize)
    {
        LogDebug("(%p, %p, %llx): buffer too small (free: %llx)", buffer, data, inputDataSize, freeSize);
        return FALSE;
    }

    if (buffer->DataStart == NULL) // empty, initialize pointers
    {
        buffer->DataStart = buffer->BufferStart;
        buffer->DataEnd = buffer->BufferStart + inputDataSize - 1;
        memcpy(buffer->BufferStart, data, inputDataSize);
        return TRUE;
    }

    startToEnd = buffer->BufferStart + buffer->Size - 1 - buffer->DataStart;
    endToEnd = buffer->BufferStart + buffer->Size - 1 - buffer->DataEnd;

    destination = buffer->DataEnd + 1;
    if (destination > buffer->BufferStart + buffer->Size - 1)
        destination -= buffer->Size;

    if (buffer->DataStart == buffer->BufferStart ||
        buffer->DataEnd == buffer->BufferStart + buffer->Size - 1 ||
        endToEnd > startToEnd)
    {
        // free space is contiguous
        memcpy(destination, data, inputDataSize);
    }
    else
    {
        // possibly two writes
        if (inputDataSize <= endToEnd)
        {
            // just one, we fit in the tail free region
            memcpy(destination, data, inputDataSize);
        }
        else
        {
            memcpy(destination, data, endToEnd);
            memcpy(buffer->BufferStart, data + endToEnd, inputDataSize - endToEnd);
        }
    }

    buffer->DataEnd += inputDataSize;
    if (buffer->DataEnd > buffer->BufferStart + buffer->Size - 1)
        buffer->DataEnd -= buffer->Size;

    LogVerbose("%p: data start %llx, data end %llx", buffer, buffer->DataStart - buffer->BufferStart, buffer->DataEnd - buffer->BufferStart);
    return TRUE;
}

// dequeue data from the buffer
// underflow = true: don't treat underflow as errors (reading empty buffer is ok etc)
// dataSize = 0: read all (use carefully in case of buffer overruns)
BOOL CmqGetData(IN CMQ_BUFFER *buffer, OUT void *outputData, IN OUT UINT64 *dataSize, CMQ_UNDERFLOW_MODE underflowMode)
{
    BYTE *data = (BYTE *) outputData;
    UINT64 usedSize, usedAtEnd;

    LogVerbose("(%p, %p, %llx, %d)", buffer, data, *dataSize, underflowMode);
    if (buffer->DataStart == NULL)  // buffer empty
    {
        if ((underflowMode == CMQ_ALLOW_UNDERFLOW) || (*dataSize == 0))
        {
            *dataSize = 0;
            return TRUE;
        }
        else
        {
            *dataSize = 0;
            return FALSE;
        }
    }

    usedSize = CmqGetUsedSize(buffer);

    if (*dataSize == 0)   // "read all"
    {
        *dataSize = usedSize;
    }
    else
    {
        if (*dataSize > usedSize) // underflow
        {
            if (underflowMode == CMQ_ALLOW_UNDERFLOW)
            {
                *dataSize = usedSize;
            }
            else
            {
                LogDebug("%p: underflow (requested %llx, got %llx)", buffer, *dataSize, usedSize);
                *dataSize = 0;
                return FALSE;
            }
        }
    }

    if (buffer->DataStart <= buffer->DataEnd)
    {
        // contiguous data region
        memcpy(data, buffer->DataStart, *dataSize);
    }
    else
    {
        usedAtEnd = buffer->BufferStart + buffer->Size - buffer->DataStart;
        if (*dataSize <= usedAtEnd)
        {
            memcpy(data, buffer->DataStart, *dataSize);
        }
        else
        {
            memcpy(data, buffer->DataStart, usedAtEnd);
            memcpy((data + usedAtEnd), buffer->BufferStart, *dataSize - usedAtEnd);
        }
    }

    buffer->DataStart += *dataSize;
    if (buffer->DataStart > buffer->BufferStart + buffer->Size - 1)
        buffer->DataStart -= buffer->Size;

    if (usedSize - *dataSize == 0) // buffer empty
    {
        buffer->DataStart = NULL;
    }

    LogVerbose("%p: data start %llx, data end %llx", buffer, buffer->DataStart - buffer->BufferStart, buffer->DataEnd - buffer->BufferStart);
    return TRUE;
}

// get used data size
UINT64 CmqGetUsedSize(IN const CMQ_BUFFER *buffer)
{
    if (buffer->DataStart == NULL || buffer->BufferStart == NULL || buffer->Size == 0)
        return 0;

    if (buffer->DataStart <= buffer->DataEnd) // data in one consecutive block
        return (buffer->DataEnd - buffer->DataStart + 1);
    else
        return (buffer->DataEnd + buffer->Size - buffer->DataStart + 1);
}

UINT64 CmqGetFreeSize(IN const CMQ_BUFFER *buffer)
{
    return buffer->Size - CmqGetUsedSize(buffer);
}
