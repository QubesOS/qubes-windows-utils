#pragma once

#include <windows.h>
#include "libvchan.h"

// libvchan isn't reentrant so remember to synchronize access if using multiple threads.
// Read/write functions try to read/write the whole size requested, blocking if needed.
// Check read/write buffer sizes before initiating transfer to avoid blocking.

int VchanGetReadBufferSize(IN libvchan_t *vchan);
int VchanGetWriteBufferSize(IN libvchan_t *vchan);
BOOL VchanSendBuffer(IN libvchan_t *vchan, IN const void *data, IN size_t size, IN const WCHAR *what);
BOOL VchanReceiveBuffer(IN libvchan_t *vchan, OUT void *data, IN size_t size, IN const WCHAR *what);
