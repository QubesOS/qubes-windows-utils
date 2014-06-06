#include <Windows.h>

// Main registry configuration key.
#define REG_CONFIG_KEY L"Software\\Invisible Things Lab\\Qubes Tools"

// Log diractory.
#define REG_CONFIG_LOG_VALUE L"LogDir"

// Read a string value from registry config.
DWORD CfgReadString(IN PWCHAR valueName, OUT PWCHAR value, IN DWORD valueLength);
