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
libvchan_t *VchanInitServer(IN int domain, IN int port, IN size_t bufferSize, IN DWORD timeout);

WINDOWSUTILS_API
libvchan_t *VchanInitClient(IN int domain, IN int port, IN DWORD timeout);

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
