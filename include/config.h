#pragma once
#include <Windows.h>

// Main registry configuration key.
#define REG_CONFIG_KEY L"Software\\Invisible Things Lab\\Qubes Tools"

// Maximum characters for a registry key path.
// Tests show that the real limit is higher, but let's stick to what MSDN says.
#define CFG_PATH_MAX 256

// Maximum characters for a module name.
#define CFG_MODULE_MAX (CFG_PATH_MAX - RTL_NUMBER_OF(REG_CONFIG_KEY) - 1)

// Read a string value from registry config.
DWORD CfgReadString(const IN PWCHAR moduleName, const IN PWCHAR valueName, OUT PWCHAR value, IN DWORD valueLength, OUT OPTIONAL PBOOL rootFallback);

// Read a DWORD value from registry config.
DWORD CfgReadDword(const IN PWCHAR moduleName, const IN PWCHAR valueName, OUT PDWORD value, OUT OPTIONAL PBOOL rootFallback);

// Read a 64-bit value from registry config.
DWORD CfgReadQword(const IN PWCHAR moduleName, const IN PWCHAR valueName, OUT PLARGE_INTEGER value, OUT OPTIONAL PBOOL rootFallback);

// Creates the registry config key if not present.
// NOTE: this will fail for non-administrators if the key doesn't exist.
DWORD CfgEnsureKeyExists(const IN OPTIONAL PWCHAR moduleName);
