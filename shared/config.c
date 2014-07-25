#include "config.h"
#include "log.h"

// Read a string value from registry config.
DWORD CfgReadString(const IN PWCHAR valueName, OUT PWCHAR value, IN DWORD valueLength)
{
    HKEY key = NULL;
    DWORD status;
    DWORD type;
    DWORD size;

    SetLastError(status = RegOpenKey(HKEY_LOCAL_MACHINE, REG_CONFIG_KEY, &key));
    if (status != ERROR_SUCCESS)
        return status;

    size = sizeof(WCHAR) * (valueLength-1);
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
DWORD CfgReadDword(const IN PWCHAR valueName, OUT PDWORD value)
{
    HKEY key = NULL;
    DWORD status;
    DWORD type;
    DWORD size;

    SetLastError(status = RegOpenKey(HKEY_LOCAL_MACHINE, REG_CONFIG_KEY, &key));
    if (status != ERROR_SUCCESS)
        return status;

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
DWORD CfgReadQword(const IN PWCHAR valueName, OUT PLARGE_INTEGER value)
{
    HKEY key = NULL;
    DWORD status;
    DWORD type;
    DWORD size;

    SetLastError(status = RegOpenKey(HKEY_LOCAL_MACHINE, REG_CONFIG_KEY, &key));
    if (status != ERROR_SUCCESS)
        return status;

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
