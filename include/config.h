#pragma once
#include <Windows.h>

// Main registry configuration key.
#define REG_CONFIG_KEY TEXT("Software\\Invisible Things Lab\\Qubes Tools")

// Maximum characters for a registry key path.
// Tests show that the real limit is higher, but let's stick to what MSDN says.
#define CFG_PATH_MAX 256

// Maximum characters for a module name.
#define CFG_MODULE_MAX (CFG_PATH_MAX - RTL_NUMBER_OF(REG_CONFIG_KEY) - 1)

// Get current executable's module name (base file name without extension).
DWORD CfgGetModuleName(OUT TCHAR *moduleName, IN DWORD moduleNameLength);

// Read a string value from registry config.
DWORD CfgReadString(const IN TCHAR *moduleName, const IN TCHAR *valueName, OUT TCHAR *value, IN DWORD valueLength, OUT OPTIONAL BOOL *rootFallback);

// Read a DWORD value from registry config.
DWORD CfgReadDword(const IN TCHAR *moduleName, const IN TCHAR *valueName, OUT DWORD *value, OUT OPTIONAL BOOL *rootFallback);

// Read a 64-bit value from registry config.
DWORD CfgReadQword(const IN TCHAR *moduleName, const IN TCHAR *valueName, OUT LARGE_INTEGER *value, OUT OPTIONAL BOOL *rootFallback);

// Creates the registry config key if not present.
// NOTE: this will fail for non-administrators if the key doesn't exist.
DWORD CfgEnsureKeyExists(const IN OPTIONAL TCHAR *moduleName);
