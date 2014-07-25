#include <Windows.h>
#include <strsafe.h>

#include "config.h"
#include "log.h"

DWORD CfgOpenKey(const IN OPTIONAL PWCHAR moduleName, OUT PHKEY key, OUT OPTIONAL PBOOL rootFallback)
{
    DWORD status;
    WCHAR keyPath[CFG_PATH_MAX];

    if (moduleName)
    {
        if (rootFallback)
            *rootFallback = FALSE;

        // Try to open module-specific key.
        StringCchPrintf(keyPath, RTL_NUMBER_OF(keyPath), L"%s\\%s", REG_CONFIG_KEY, moduleName);

        SetLastError(status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, key));
        if (status == ERROR_SUCCESS)
            return status;
    }

    if (rootFallback)
        *rootFallback = TRUE;

    // Open root key.
    SetLastError(status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, REG_CONFIG_KEY, 0, KEY_READ, key));
    return status;
}

// Read a string value from registry config.
DWORD CfgReadString(const IN OPTIONAL PWCHAR moduleName, const IN PWCHAR valueName, OUT PWCHAR value, IN DWORD valueLength, OUT OPTIONAL PBOOL rootFallback)
{
    HKEY key = NULL;
    DWORD status;
    DWORD type;
    DWORD size;

    status = CfgOpenKey(moduleName, &key, rootFallback);
    if (status != ERROR_SUCCESS)
        goto cleanup;

    size = sizeof(WCHAR) * (valueLength - 1);
    ZeroMemory(value, sizeof(WCHAR)*valueLength);

    SetLastError(status = RegQueryValueEx(key, valueName, NULL, &type, (PBYTE)value, &size));
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
DWORD CfgReadDword(const IN OPTIONAL PWCHAR moduleName, const IN PWCHAR valueName, OUT PDWORD value, OUT OPTIONAL PBOOL rootFallback)
{
    HKEY key = NULL;
    DWORD status;
    DWORD type;
    DWORD size;

    status = CfgOpenKey(moduleName, &key, rootFallback);
    if (status != ERROR_SUCCESS)
        goto cleanup;

    size = sizeof(DWORD);

    SetLastError(status = RegQueryValueEx(key, valueName, NULL, &type, (PBYTE)value, &size));
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

// Read a 64-bit value from registry config.
DWORD CfgReadQword(const IN OPTIONAL PWCHAR moduleName, const IN PWCHAR valueName, OUT PLARGE_INTEGER value, OUT OPTIONAL PBOOL rootFallback)
{
    HKEY key = NULL;
    DWORD status;
    DWORD type;
    DWORD size;

    status = CfgOpenKey(moduleName, &key, rootFallback);
    if (status != ERROR_SUCCESS)
        goto cleanup;

    size = sizeof(LARGE_INTEGER);

    SetLastError(status = RegQueryValueEx(key, valueName, NULL, &type, (PBYTE)value, &size));
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
DWORD CfgEnsureKeyExists(const IN OPTIONAL PWCHAR moduleName)
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