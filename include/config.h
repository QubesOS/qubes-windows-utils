#pragma once
#include <Windows.h>

// Main registry configuration key.
#define REG_CONFIG_KEY L"Software\\Invisible Things Lab\\Qubes Tools"

// Maximum characters for a registry key path.
// Tests show that the real limit is higher, but let's stick to what MSDN says.
#define CFG_PATH_MAX 256

// Maximum characters for a module name.
#define CFG_MODULE_MAX (CFG_PATH_MAX - RTL_NUMBER_OF(REG_CONFIG_KEY) - 1)

// Get current executable's module name (base file name without extension).
DWORD CfgGetModuleName(OUT WCHAR *moduleName, IN DWORD cchModuleName);

// Read a string value from registry config.
DWORD CfgReadString(IN const WCHAR *moduleName OPTIONAL, IN const WCHAR *valueName, OUT WCHAR *value, IN DWORD valueLength, OUT BOOL *rootFallback OPTIONAL);

// Read a DWORD value from registry config.
DWORD CfgReadDword(IN const WCHAR *moduleName OPTIONAL, IN const WCHAR *valueName, OUT DWORD *value, OUT BOOL *rootFallback OPTIONAL);

// Read a 64-bit value from registry config.
DWORD CfgReadQword(IN const WCHAR *moduleName OPTIONAL, IN const WCHAR *valueName, OUT LARGE_INTEGER *value, OUT BOOL *rootFallback OPTIONAL);

// Write a DWORD value to registry config.
DWORD CfgWriteDword(IN const WCHAR *moduleName OPTIONAL, IN const WCHAR *valueName, IN DWORD value, OUT BOOL *rootFallback OPTIONAL);

// Creates the registry config key if not present.
// NOTE: this will fail for non-administrators if the key doesn't exist.
DWORD CfgEnsureKeyExists(IN const WCHAR *moduleName OPTIONAL);
