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
#include <shlwapi.h>
#include <strsafe.h>

#include "config.h"

// Get current executable's module name (base file name without extension).
DWORD CfgGetModuleName(OUT WCHAR *moduleName, IN DWORD cchModuleName)
{
    WCHAR exePath[MAX_PATH], *exeName;
    DWORD status = ERROR_SUCCESS;

    if (!GetModuleFileName(NULL, exePath, RTL_NUMBER_OF(exePath)))
    {
        status = GetLastError();
        goto cleanup;
    }
    PathRemoveExtension(exePath);
    exeName = PathFindFileName(exePath);
    if (exeName == exePath) // failed
    {
        status = ERROR_INVALID_NAME;
        goto cleanup;
    }

    if (FAILED(StringCchCopy(moduleName, cchModuleName, exeName)))
    {
        status = ERROR_INVALID_DATA;
    }

cleanup:
    SetLastError(status);
    return status;
}

static DWORD CfgOpenKey(IN const WCHAR *moduleName OPTIONAL, OUT HKEY *key, IN const WCHAR *valueName, OUT BOOL *rootFallback OPTIONAL)
{
    DWORD status;
    WCHAR keyPath[CFG_PATH_MAX];

    if (moduleName)
    {
        if (rootFallback)
            *rootFallback = FALSE;

        // Try to open module-specific key.
        StringCchPrintf(keyPath, RTL_NUMBER_OF(keyPath), L"%s\\%s", REG_CONFIG_KEY, moduleName);

        SetLastError(status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ | KEY_WRITE, key));
        if (status == ERROR_SUCCESS)
        {
            // Check if the requested value exists.
            SetLastError(status = RegQueryValueEx(*key, valueName, NULL, NULL, NULL, NULL));
            if (status == ERROR_SUCCESS)
                return status;
            RegCloseKey(*key); // value not found, try the root key
        }
    }

    if (rootFallback)
        *rootFallback = TRUE;

    // Open root key.
    SetLastError(status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, REG_CONFIG_KEY, 0, KEY_READ | KEY_WRITE, key));
    return status;
}

// Read a string value from registry config.
DWORD CfgReadString(IN const WCHAR *moduleName OPTIONAL, IN const WCHAR *valueName, OUT WCHAR *value, IN DWORD valueLength, OUT BOOL *rootFallback OPTIONAL)
{
    HKEY key = NULL;
    DWORD status;
    DWORD type;
    DWORD size;

    status = CfgOpenKey(moduleName, &key, valueName, rootFallback);
    if (status != ERROR_SUCCESS)
        goto cleanup;

    size = sizeof(WCHAR) * (valueLength - 1);
    ZeroMemory(value, sizeof(WCHAR)*valueLength);

    SetLastError(status = RegQueryValueEx(key, valueName, NULL, &type, (BYTE *) value, &size));
    if (status != ERROR_SUCCESS)
        goto cleanup;

    if (type != REG_SZ)
    {
        status = ERROR_DATATYPE_MISMATCH;
        goto cleanup;
    }

cleanup:
    if (key)
        RegCloseKey(key);

    return status;
}

// Read a DWORD value from registry config.
DWORD CfgReadDword(IN const WCHAR *moduleName OPTIONAL, IN const WCHAR *valueName, OUT DWORD *value, OUT BOOL *rootFallback OPTIONAL)
{
    HKEY key = NULL;
    DWORD status;
    DWORD type;
    DWORD size;

    status = CfgOpenKey(moduleName, &key, valueName, rootFallback);
    if (status != ERROR_SUCCESS)
        goto cleanup;

    size = sizeof(DWORD);

    SetLastError(status = RegQueryValueEx(key, valueName, NULL, &type, (BYTE *) value, &size));
    if (status != ERROR_SUCCESS)
        goto cleanup;

    if (type != REG_DWORD)
    {
        status = ERROR_DATATYPE_MISMATCH;
        goto cleanup;
    }

cleanup:
    if (key)
        RegCloseKey(key);

    return status;
}

// Write a DWORD value to registry config.
DWORD CfgWriteDword(IN const WCHAR *moduleName OPTIONAL, IN const WCHAR *valueName, IN DWORD value, OUT BOOL *rootFallback OPTIONAL)
{
    HKEY key = NULL;
    DWORD status;

    status = CfgOpenKey(moduleName, &key, valueName, rootFallback);
    if (status != ERROR_SUCCESS)
        goto cleanup;

    SetLastError(status = RegSetValueEx(key, valueName, 0, REG_DWORD, (BYTE *) &value, sizeof(DWORD)));
    if (status != ERROR_SUCCESS)
        goto cleanup;

cleanup:
    if (key)
        RegCloseKey(key);

    return status;
}

// Read a 64-bit value from registry config.
DWORD CfgReadQword(IN const WCHAR *moduleName OPTIONAL, IN const WCHAR *valueName, OUT LARGE_INTEGER *value, OUT BOOL *rootFallback OPTIONAL)
{
    HKEY key = NULL;
    DWORD status;
    DWORD type;
    DWORD size;

    status = CfgOpenKey(moduleName, &key, valueName, rootFallback);
    if (status != ERROR_SUCCESS)
        goto cleanup;

    size = sizeof(LARGE_INTEGER);

    SetLastError(status = RegQueryValueEx(key, valueName, NULL, &type, (BYTE *) value, &size));
    if (status != ERROR_SUCCESS)
        goto cleanup;

    if (type != REG_QWORD)
    {
        status = ERROR_DATATYPE_MISMATCH;
        goto cleanup;
    }

cleanup:
    if (key)
        RegCloseKey(key);

    return status;
}

// Creates the registry config key if not present.
// NOTE: this will fail for non-administrators if the key doesn't exist.
DWORD CfgEnsureKeyExists(IN const WCHAR *moduleName OPTIONAL)
{
    HKEY key = NULL;
    DWORD status;
    WCHAR keyPath[CFG_PATH_MAX];

    if (moduleName)
    {
        // Try to open module-specific key.
        StringCchPrintf(keyPath, RTL_NUMBER_OF(keyPath), L"%s\\%s", REG_CONFIG_KEY, moduleName);
    }
    else
    {
        // Open the root key.
        StringCchCopy(keyPath, RTL_NUMBER_OF(keyPath), REG_CONFIG_KEY);
    }

    SetLastError(status = RegCreateKeyEx(HKEY_LOCAL_MACHINE, keyPath, 0, NULL, 0, KEY_READ, NULL, &key, NULL));

    RegCloseKey(key);

    return status;
}
