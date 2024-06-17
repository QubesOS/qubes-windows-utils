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

#include "qubes-io.h"
#include "log.h"

BOOL QioWriteBuffer(IN HANDLE file, IN const void *buffer, IN DWORD bufferSize)
{
    DWORD cbWrittenTotal = 0;
    DWORD cbWritten;

    while (cbWrittenTotal < bufferSize)
    {
        if (!WriteFile(file, (BYTE *) buffer + cbWrittenTotal, bufferSize - cbWrittenTotal, &cbWritten, NULL))
        {
            win_perror("WriteFile");
            return FALSE;
        }
        cbWrittenTotal += cbWritten;
    }
    return TRUE;
}

BOOL QioReadBuffer(IN HANDLE file, OUT void *buffer, IN DWORD bufferSize)
{
    DWORD cbReadTotal = 0;
    DWORD cbRead;

    while (cbReadTotal < bufferSize)
    {
        if (!ReadFile(file, (BYTE *)buffer + cbReadTotal, bufferSize - cbReadTotal, &cbRead, NULL))
        {
            win_perror("ReadFile");
            return FALSE;
        }

        if (cbRead == 0)
        {
            LogError("EOF");
            return FALSE;
        }

        cbReadTotal += cbRead;
    }
    return TRUE;
}

DWORD QioReadUntilEof(IN HANDLE file, OUT void *buffer, IN DWORD bufferSize)
{
    DWORD cbReadTotal = 0;
    DWORD cbRead;

    while (cbReadTotal < bufferSize)
    {
        if (!ReadFile(file, (BYTE *)buffer + cbReadTotal, bufferSize - cbReadTotal, &cbRead, NULL))
            return cbReadTotal;

        if (cbRead == 0)
            return cbReadTotal;

        cbReadTotal += cbRead;
    }
    return cbReadTotal;
}

BOOL QioCopyUntilEof(IN HANDLE output, IN HANDLE input)
{
    DWORD cb;
    BYTE buffer[4096];

    while (TRUE)
    {
        cb = QioReadUntilEof(input, buffer, sizeof(buffer));
        if (cb == 0)
            return TRUE;

        if (!QioWriteBuffer(output, buffer, cb))
            return FALSE;
    }
    return TRUE;
}
