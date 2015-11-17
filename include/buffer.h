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

#pragma once
#include <windows.h>
#include <stdlib.h>

// Circular memory queue implementation.

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

struct _CMQ_BUFFER;
typedef struct _CMQ_BUFFER CMQ_BUFFER;

// Underflow mode for read operations.
typedef enum _CMQ_UNDERFLOW_MODE
{
    CMQ_ALLOW_UNDERFLOW = 1,
    CMQ_NO_UNDERFLOW
} CMQ_UNDERFLOW_MODE;

// allocate memory and initialize, return 0 = failure
WINDOWSUTILS_API
CMQ_BUFFER *CmqCreate(IN UINT64 bufferSize);

// free memory and deinitialize
WINDOWSUTILS_API
void CmqDestroy(IN CMQ_BUFFER *buffer);

// "push" data to the queue
WINDOWSUTILS_API
BOOL CmqAddData(IN CMQ_BUFFER *buffer, IN const void *data, UINT64 dataSize);

// "pop" data from the queue, dataSize=0: get all data (make sure you have the space for it)
WINDOWSUTILS_API
BOOL CmqGetData(IN CMQ_BUFFER *buffer, OUT void *data, IN OUT UINT64 *dataSize, CMQ_UNDERFLOW_MODE underflowMode);

// returns bytes used by data
WINDOWSUTILS_API
UINT64 CmqGetUsedSize(IN const CMQ_BUFFER *buffer);

// returns free space
WINDOWSUTILS_API
UINT64 CmqGetFreeSize(IN const CMQ_BUFFER *buffer);

// zero-fill buffer, rewind internal pointer
WINDOWSUTILS_API
void CmqClear(IN CMQ_BUFFER *buffer);

#ifdef __cplusplus
}
#endif
