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

#include <windows.h>
#include <stdlib.h>

#include "utf8-conv.h"

static char* g_Buffer = NULL;
static WCHAR* g_BufferW = NULL;

DWORD ConvertUTF8ToUTF16(IN const char* inputUtf8, OUT WCHAR* outputUtf16, OUT size_t* cchOutput OPTIONAL)
{
    DWORD status = ERROR_SUCCESS;

    int utf16_count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, inputUtf8, -1, outputUtf16,
        CONVERT_MAX_BUFFER_LENGTH);
    if (utf16_count == 0)
    {
        status = GetLastError();
        if (cchOutput)
            *cchOutput = 0;
        goto end;
    }

    if (cchOutput)
        *cchOutput = utf16_count - 1; // without terminating NULL character

end:
    return status;
}

DWORD ConvertUTF16ToUTF8(IN const WCHAR* inputUtf16, OUT char* outputUtf8, OUT size_t* cchOutput OPTIONAL)
{
    DWORD status = ERROR_SUCCESS;

    int utf8_count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, inputUtf16, -1, outputUtf8,
        CONVERT_MAX_BUFFER_LENGTH, NULL, NULL);
    if (utf8_count == 0)
    {
        status = GetLastError();
        if (cchOutput)
            *cchOutput = 0;
        goto end;
    }

    if (cchOutput)
        *cchOutput = utf8_count - 1; // without terminating NULL character

end:
    return status;
}

DWORD ConvertUTF8ToUTF16Static(IN const char *inputUtf8, OUT WCHAR **outputUtf16, OUT size_t *cchOutput OPTIONAL)
{
    DWORD status = ERROR_SUCCESS;

    if (!g_BufferW)
    {
        g_BufferW = (WCHAR*) malloc(CONVERT_MAX_BUFFER_SIZE_UTF16);
        if (!g_BufferW)
        {
            status = ERROR_OUTOFMEMORY;
            goto end;
        }
    }

    status = ConvertUTF8ToUTF16(inputUtf8, g_BufferW, cchOutput);
    if (status == ERROR_SUCCESS)
        *outputUtf16 = g_BufferW;
    else
        *outputUtf16 = NULL;

end:
    return status;
}

DWORD ConvertUTF16ToUTF8Static(IN const WCHAR *inputUtf16, OUT char **outputUtf8, OUT size_t *cchOutput OPTIONAL)
{
    DWORD status = ERROR_SUCCESS;

    if (!g_Buffer)
    {
        g_Buffer = (char*) malloc(CONVERT_MAX_BUFFER_SIZE_UTF8);
        if (!g_Buffer)
        {
            status = ERROR_OUTOFMEMORY;
            goto end;
        }
    }

    status = ConvertUTF16ToUTF8(inputUtf16, g_Buffer, cchOutput);
    if (status == ERROR_SUCCESS)
        *outputUtf8 = g_Buffer;
    else
        *outputUtf8 = NULL;

end:
    return status;
}
