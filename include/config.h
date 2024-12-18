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

// Main registry configuration key.
#define REG_CONFIG_KEY L"Software\\Invisible Things Lab\\Qubes Tools"

// Maximum characters for a registry key path.
// Tests show that the real limit is higher, but let's stick to what MSDN says.
#define CFG_PATH_MAX 256

// Maximum characters for a module name.
#define CFG_MODULE_MAX (CFG_PATH_MAX - RTL_NUMBER_OF(REG_CONFIG_KEY) - 1)

// Get current executable's module name (base file name without extension).
WINDOWSUTILS_API
DWORD CfgGetModuleName(OUT WCHAR *moduleName, IN DWORD cchModuleName);

// Read a string value from registry config.
WINDOWSUTILS_API
DWORD CfgReadString(IN const WCHAR *moduleName OPTIONAL, IN const WCHAR *valueName, OUT WCHAR *value, IN DWORD valueLength, OUT BOOL *rootFallback OPTIONAL);

// Read a multi-string value from registry config. It's a sequence of C-strings terminated by an additional '\0'.
WINDOWSUTILS_API
DWORD CfgReadMultiString(
    IN const WCHAR* moduleName OPTIONAL,
    IN const WCHAR* valueName,
    OUT WCHAR* value,
    IN DWORD valueLength,
    OUT BOOL* rootFallback OPTIONAL
);

// Read a DWORD value from registry config.
WINDOWSUTILS_API
DWORD CfgReadDword(IN const WCHAR *moduleName OPTIONAL, IN const WCHAR *valueName, OUT DWORD *value, OUT BOOL *rootFallback OPTIONAL);

// Read a 64-bit value from registry config.
WINDOWSUTILS_API
DWORD CfgReadQword(IN const WCHAR *moduleName OPTIONAL, IN const WCHAR *valueName, OUT LARGE_INTEGER *value, OUT BOOL *rootFallback OPTIONAL);

// Write a DWORD value to registry config.
WINDOWSUTILS_API
DWORD CfgWriteDword(IN const WCHAR *moduleName OPTIONAL, IN const WCHAR *valueName, IN DWORD value, OUT BOOL *rootFallback OPTIONAL);

// Creates the registry config key if not present.
// NOTE: this will fail for non-administrators if the key doesn't exist.
WINDOWSUTILS_API
DWORD CfgEnsureKeyExists(IN const WCHAR *moduleName OPTIONAL);

// Returns full path to QWT installation directory.
WINDOWSUTILS_API
const WCHAR* CfgGetToolsDir(void);

#ifdef __cplusplus
}
#endif
