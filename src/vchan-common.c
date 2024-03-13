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

#include "vchan-common.h"
#include "log.h"

#include <strsafe.h>

static void VchanLogger(IN int logLevel, IN const char* function, IN const wchar_t* format, IN va_list args)
{
    wchar_t buf[1024];

    StringCbVPrintfW(buf, sizeof(buf), format, args);
    _LogFormat(logLevel, /*raw=*/FALSE, function, buf);
}

libvchan_t *VchanInitServer(IN int domain, IN int port, IN size_t bufferSize, IN DWORD timeout)
{
    libvchan_t *vchan;
    ULONGLONG start = GetTickCount64();
    ULONGLONG ticks = start;

    LogDebug("domain %d, port %d, buffer %lu, timeout %lu", domain, port, bufferSize, timeout);

    libvchan_register_logger(VchanLogger, LogGetLevel());
    vchan = libvchan_server_init(domain, port, bufferSize, bufferSize);

    while (vchan == NULL && GetLastError() == ERROR_NOT_SUPPORTED && (ticks - start) < timeout)
    {
        Sleep(1000);
        LogDebug("server init failed: 0x%x, retrying", GetLastError());
        ticks = GetTickCount64();
        vchan = libvchan_server_init(domain, port, bufferSize, bufferSize);
    }

    return vchan;
}

libvchan_t *VchanInitClient(IN int domain, IN int port, IN DWORD timeout)
{
    libvchan_t *vchan;
    ULONGLONG start = GetTickCount64();
    ULONGLONG ticks = start;

    LogDebug("domain %d, port %d, timeout %lu", domain, port, timeout);

    libvchan_register_logger(VchanLogger, LogGetLevel());
    vchan = libvchan_client_init(domain, port);

    while (vchan == NULL && GetLastError() == ERROR_NOT_SUPPORTED && (ticks - start) < timeout)
    {
        Sleep(1000);
        LogDebug("client init failed: 0x%x, retrying", GetLastError());
        ticks = GetTickCount64();
        vchan = libvchan_client_init(domain, port);
    }

    return vchan;
}

int VchanGetReadBufferSize(IN libvchan_t *vchan)
{
    return libvchan_data_ready(vchan);
}

int VchanGetWriteBufferSize(IN libvchan_t *vchan)
{
    return libvchan_buffer_space(vchan);
}

BOOL VchanSendBuffer(IN libvchan_t *vchan, IN const void *data, IN size_t size, IN const WCHAR *what)
{
    int writeSize;

    LogVerbose("(%p, %s): %d bytes (free: %d)", vchan, what, size, VchanGetWriteBufferSize(vchan));
    //hex_dump(what, data, size);

    if (VchanGetWriteBufferSize(vchan) < size)
    {
        // wait for space
        LogWarning("(%p, %s): vchan buffer full, blocking write", vchan, what);
        while (VchanGetWriteBufferSize(vchan) < size)
            Sleep(1);
    }

    if ((writeSize = libvchan_send(vchan, data, size)) < 0)
    {
        LogError("(%p, %s): failed to send (buffer %p, size %d)", vchan, what, data, size);
        return FALSE;
    }

    if (writeSize != size)
    {
        // shouldn't happen
        LogError("(%p, %s): size mismatch or error: requested %d, sent %d", vchan, what, size, writeSize);
        return FALSE;
    }

    return TRUE;
}

BOOL VchanReceiveBuffer(IN libvchan_t *vchan, OUT void *data, IN size_t size, IN const WCHAR *what)
{
    int readSize;

    LogVerbose("(%p, %s): %d bytes (available: %d)", vchan, what, size, VchanGetReadBufferSize(vchan));
    if (VchanGetReadBufferSize(vchan) < size)
    {
        // wait for data
        LogWarning("(%p, %s): no data, blocking read", vchan, what);
    }

    if ((readSize = libvchan_recv(vchan, data, size)) < 0)
    {
        LogError("(%p, %s): failed to read (size %d)", vchan, what, size);
        return FALSE;
    }

    if (readSize != size)
    {
        // shouldn't happen
        LogError("(%p, %s): size mismatch or error: requested %d, got %d", vchan, what, size, readSize);
        return FALSE;
    }

    //hex_dump(what, data, size);
    return TRUE;
}
