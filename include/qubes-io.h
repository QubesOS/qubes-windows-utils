#pragma once
#include <Windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

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
