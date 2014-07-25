#include <Windows.h>

// Main registry configuration key.
#define REG_CONFIG_KEY L"Software\\Invisible Things Lab\\Qubes Tools"

// Log diractory.
#define REG_CONFIG_LOG_VALUE L"LogDir"

// Read a string value from registry config.
DWORD CfgReadString(const IN PWCHAR valueName, OUT PWCHAR value, IN DWORD valueLength);

// Read a DWORD value from registry config.
DWORD CfgReadDword(const IN PWCHAR valueName, OUT PDWORD value);

// Read a 64-bit value from registry config.
DWORD CfgReadQword(const IN PWCHAR valueName, OUT PLARGE_INTEGER value);
