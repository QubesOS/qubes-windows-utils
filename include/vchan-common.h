#pragma once

#include <windows.h>
#include <tchar.h>
#include "libvchan.h"

BOOL _send_to_vchan(libvchan_t *vchan, void *data, size_t size, TCHAR *what);
BOOL _recv_from_vchan(libvchan_t *vchan, void *data, size_t size, TCHAR *what);

#define send_to_vchan(vchan, data, size, what) _send_to_vchan(vchan, data, size, TEXT(what))
#define recv_from_vchan(vchan, data, size, what) _recv_from_vchan(vchan, data, size, TEXT(what))
