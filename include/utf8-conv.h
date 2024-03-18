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
#include <windef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

#define CONVERT_MAX_BUFFER_LENGTH      65536
#define CONVERT_MAX_BUFFER_SIZE_UTF8   CONVERT_MAX_BUFFER_LENGTH
#define CONVERT_MAX_BUFFER_SIZE_UTF16  (CONVERT_MAX_BUFFER_LENGTH * sizeof(WCHAR))

// Uses a static output buffer that's overwritten with each call.
// cchOutput is the number of characters in the output buffer, without terminating NULL.
WINDOWSUTILS_API
DWORD ConvertUTF8ToUTF16Static(IN const char *inputUtf8, OUT WCHAR **outputUtf16, OUT size_t *cchOutput OPTIONAL);

// Uses a static output buffer that's overwritten with each call.
// cchOutput is the number of characters in the output buffer, without terminating NULL.
WINDOWSUTILS_API
DWORD ConvertUTF16ToUTF8Static(IN const WCHAR *inputUtf16, OUT char **outputUtf8, OUT size_t *cchOutput OPTIONAL);

// outputUtf16 must be at least CONVERT_MAX_BUFFER_LENGTH WCHARs.
// cchOutput is the number of characters in the output buffer, without terminating NULL.
WINDOWSUTILS_API
DWORD ConvertUTF8ToUTF16(IN const char* inputUtf8, OUT WCHAR* outputUtf16, OUT size_t* cchOutput OPTIONAL);

// outputUtf8 must be at least CONVERT_MAX_BUFFER_LENGTH chars.
// cchOutput is the number of characters in the output buffer, without terminating NULL.
WINDOWSUTILS_API
DWORD ConvertUTF16ToUTF8(IN const WCHAR* inputUtf16, OUT char* outputUtf8, OUT size_t* cchOutput OPTIONAL);

#ifdef __cplusplus
}
#endif
