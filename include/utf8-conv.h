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
#include <Windows.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

// Returns number of characters in output buffer, without terminating NULL.
WINDOWSUTILS_API
DWORD ConvertUTF8ToUTF16(IN const char *inputUtf8, OUT WCHAR **outputUtf16, OUT size_t *cchOutput OPTIONAL);

// Returns number of characters in output buffer, without terminating NULL.
WINDOWSUTILS_API
DWORD ConvertUTF16ToUTF8(IN const WCHAR *inputUtf16, OUT char **outputUtf8, OUT size_t *cchOutput OPTIONAL);

// Free memory allocated by the conversion functions.
// This is needed for executables that don't use the same CRT as this DLL (can't just use free() in such cases).
WINDOWSUTILS_API
void ConvertFree(void *p);

#ifdef __cplusplus
}
#endif
