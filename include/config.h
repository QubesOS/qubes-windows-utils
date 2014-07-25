#include <Windows.h>

// Main registry configuration key.
#define REG_CONFIG_KEY L"Software\\Invisible Things Lab\\Qubes Tools"

// Log diractory.
#define REG_CONFIG_LOG_VALUE L"LogDir"

// Read a string value from registry config.
DWORD CfgReadString(IN PWCHAR valueName, OUT PWCHAR value, IN DWORD valueLength);

// Read a DWORD value from registry config.
DWORD CfgReadDword(IN PWCHAR valueName, OUT PDWORD value);

// Read a 64-bit value from registry config.
DWORD CfgReadQword(IN PWCHAR valueName, OUT PLARGE_INTEGER value);
