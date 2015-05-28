#pragma once

#include <windows.h>
#include "libvchan.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

// libvchan isn't reentrant so remember to synchronize access if using multiple threads.
// Read/write functions try to read/write the whole size requested, blocking if needed.
// Check read/write buffer sizes before initiating transfer to avoid blocking.

WINDOWSUTILS_API
int VchanGetReadBufferSize(IN libvchan_t *vchan);

WINDOWSUTILS_API
int VchanGetWriteBufferSize(IN libvchan_t *vchan);

WINDOWSUTILS_API
BOOL VchanSendBuffer(IN libvchan_t *vchan, IN const void *data, IN size_t size, IN const WCHAR *what);

WINDOWSUTILS_API
BOOL VchanReceiveBuffer(IN libvchan_t *vchan, OUT void *data, IN size_t size, IN const WCHAR *what);

#ifdef __cplusplus
}
#endif
