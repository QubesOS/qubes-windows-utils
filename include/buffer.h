#pragma once
#include <windows.h>
#include <stdlib.h>

// Circular memory queue implementation.

struct _CMQ_BUFFER;
typedef struct _CMQ_BUFFER CMQ_BUFFER;

// Underflow mode for read operations.
typedef enum _CMQ_UNDERFLOW_MODE
{
    CMQ_ALLOW_UNDERFLOW = 1,
    CMQ_NO_UNDERFLOW
} CMQ_UNDERFLOW_MODE;

// allocate memory and initialize, return 0 = failure
CMQ_BUFFER *CmqCreate(IN UINT64 bufferSize);

// free memory and deinitialize
void CmqDestroy(IN CMQ_BUFFER *buffer);

// "push" data to the queue
BOOL CmqAddData(IN CMQ_BUFFER *buffer, IN const void *data, UINT64 dataSize);

// "pop" data from the queue, dataSize=0: get all data (make sure you have the space for it)
BOOL CmqGetData(IN CMQ_BUFFER *buffer, OUT void *data, IN OUT UINT64 *dataSize, CMQ_UNDERFLOW_MODE underflowMode);

// returns bytes used by data
UINT64 CmqGetUsedSize(IN const CMQ_BUFFER *buffer);

// returns free space
UINT64 CmqGetFreeSize(IN const CMQ_BUFFER *buffer);

// zero-fill buffer, rewind internal pointer
void CmqClear(IN CMQ_BUFFER *buffer);
