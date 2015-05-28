#include <windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reasonForCall, void *reserved)
{
    switch (reasonForCall)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        break;

    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
