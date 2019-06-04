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

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

/**
* @brief Get size, in bytes, of a multi-string (including the final null terminator).
* @param MultiStr The input multi-string.
* @param Count Number of non-empty strings contained in the @a MultiStr.
* @return Size, in bytes, of @a MultiStr.
*/
WINDOWSUTILS_API
ULONG
MultiStrSize(
    IN  PCHAR MultiStr,
    OUT PULONG Count OPTIONAL
    );

/**
* @brief Get size, in bytes, of a multi-wstring (including the final null terminator).
* @param MultiStr The input multi-wstring.
* @param Count Number of non-empty wstrings contained in the @a MultiStr.
* @return Size, in bytes, of @a MultiStr.
*/
WINDOWSUTILS_API
ULONG
MultiWStrSize(
    IN  PWCHAR MultiStr,
    OUT PULONG Count OPTIONAL
    );

/**
* @brief Add string to a multi-string.
* @param MultiStr The input multi-string, modified in-place.
* @param BufferSize Size, in bytes, of @a MultiStr buffer.
* @param Str String to add.
* @return TRUE if successful, FALSE otherwise.
*/
WINDOWSUTILS_API
BOOL
MultiStrAdd(
    IN OUT PCHAR MultiStr,
    IN     ULONG BufferSize,
    IN     PCHAR Str
    );

/**
* @brief Add wstring to a multi-wstring.
* @param MultiStr The input multi-wstring, modified in-place.
* @param BufferSize Size, in bytes, of @a MultiStr buffer.
* @param Str Wstring to add.
* @return TRUE if successful, FALSE otherwise.
*/
WINDOWSUTILS_API
BOOL
MultiWStrAdd(
    IN OUT PWCHAR MultiStr,
    IN     ULONG  BufferSize,
    IN     PWCHAR Str
    );


#ifdef __cplusplus
}
#endif
