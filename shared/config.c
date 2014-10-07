#include <Windows.h>
#include <Shlwapi.h>
#include <tchar.h>
#include <strsafe.h>

#include "config.h"

// Get current executable's module name (base file name without extension).
DWORD CfgGetModuleName(OUT TCHAR *moduleName, IN DWORD moduleNameLength)
{
    TCHAR exePath[MAX_PATH], *exeName;
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

    if (FAILED(StringCchCopy(moduleName, moduleNameLength, exeName)))
    {
        status = ERROR_INVALID_DATA;
    }

cleanup:
    SetLastError(status);
    return status;
}

DWORD CfgOpenKey(const IN OPTIONAL TCHAR *moduleName, OUT HKEY *key, const IN TCHAR *valueName, OUT OPTIONAL BOOL *rootFallback)
{
    DWORD status;
    TCHAR keyPath[CFG_PATH_MAX];

    if (moduleName)
    {
        if (rootFallback)
            *rootFallback = FALSE;

        // Try to open module-specific key.
        StringCchPrintf(keyPath, RTL_NUMBER_OF(keyPath), TEXT("%s\\%s"), REG_CONFIG_KEY, moduleName);

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
DWORD CfgReadString(const IN OPTIONAL TCHAR *moduleName, const IN TCHAR *valueName, OUT TCHAR *value, IN DWORD valueLength, OUT OPTIONAL BOOL *rootFallback)
{
    HKEY key = NULL;
    DWORD status;
    DWORD type;
    DWORD size;

    status = CfgOpenKey(moduleName, &key, valueName, rootFallback);
    if (status != ERROR_SUCCESS)
        goto cleanup;

    size = sizeof(TCHAR) * (valueLength - 1);
    ZeroMemory(value, sizeof(TCHAR)*valueLength);

    SetLastError(status = RegQueryValueEx(key, valueName, NULL, &type, (PBYTE) value, &size));
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
DWORD CfgReadDword(const IN OPTIONAL TCHAR *moduleName, const IN TCHAR *valueName, OUT DWORD *value, OUT OPTIONAL BOOL *rootFallback)
{
    HKEY key = NULL;
    DWORD status;
    DWORD type;
    DWORD size;

    status = CfgOpenKey(moduleName, &key, valueName, rootFallback);
    if (status != ERROR_SUCCESS)
        goto cleanup;

    size = sizeof(DWORD);

    SetLastError(status = RegQueryValueEx(key, valueName, NULL, &type, (PBYTE) value, &size));
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
DWORD CfgWriteDword(const IN OPTIONAL TCHAR *moduleName, const IN TCHAR *valueName, IN DWORD value, OUT OPTIONAL BOOL *rootFallback)
{
    HKEY key = NULL;
    DWORD status;

    status = CfgOpenKey(moduleName, &key, valueName, rootFallback);
    if (status != ERROR_SUCCESS)
        goto cleanup;

    SetLastError(status = RegSetValueExW(key, valueName, 0, REG_DWORD, (PBYTE) &value, sizeof(DWORD)));
    if (status != ERROR_SUCCESS)
        goto cleanup;

cleanup:
    if (key)
        RegCloseKey(key);

    return status;
}

// Read a 64-bit value from registry config.
DWORD CfgReadQword(const IN OPTIONAL TCHAR *moduleName, const IN TCHAR *valueName, OUT LARGE_INTEGER *value, OUT OPTIONAL BOOL *rootFallback)
{
    HKEY key = NULL;
    DWORD status;
    DWORD type;
    DWORD size;

    status = CfgOpenKey(moduleName, &key, valueName, rootFallback);
    if (status != ERROR_SUCCESS)
        goto cleanup;

    size = sizeof(LARGE_INTEGER);

    SetLastError(status = RegQueryValueEx(key, valueName, NULL, &type, (PBYTE) value, &size));
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
DWORD CfgEnsureKeyExists(const IN OPTIONAL TCHAR *moduleName)
{
    HKEY key = NULL;
    DWORD status;
    TCHAR keyPath[CFG_PATH_MAX];

    if (moduleName)
    {
        // Try to open module-specific key.
        StringCchPrintf(keyPath, RTL_NUMBER_OF(keyPath), TEXT("%s\\%s"), REG_CONFIG_KEY, moduleName);
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
