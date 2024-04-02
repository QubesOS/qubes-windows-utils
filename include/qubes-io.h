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

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

// Extended path length limit for Windows FS APIs
#define MAX_PATH_LONG         32768
#define MAX_PATH_LONG_WSIZE   (MAX_PATH_LONG * sizeof(WCHAR))
#define UNC_PATH_PREFIX       L"\\\\?\\"
#define UNC_PATH_PREFIX_LEN   4
// without null terminator
#define UNC_PATH_PREFIX_SIZE  8

// Read/WriteFile can succeed but not read/write the whole buffer in some cases
// (mostly pipes with small internal buffers and PIPE_NOWAIT mode for writing).
// The functions below make sure that the whole buffer is transferred
// (blocking if needed).

WINDOWSUTILS_API
BOOL QioWriteBuffer(IN HANDLE file, IN const void *buffer, IN DWORD bufferSize);

WINDOWSUTILS_API
BOOL QioReadBuffer(IN HANDLE file, OUT void *buffer, IN DWORD bufferSize);

// Read from the file until the bufferSize bytes are read or reading fails/returns 0.
// Returns number of bytes read.
WINDOWSUTILS_API
DWORD QioReadUntilEof(IN HANDLE file, OUT void *buffer, IN DWORD bufferSize);

// Reads from input and writes to output until input read fails or returns 0.
WINDOWSUTILS_API
BOOL QioCopyUntilEof(IN HANDLE output, IN HANDLE input);


#ifdef __cplusplus
}
#endif
