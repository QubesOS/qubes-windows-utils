#pragma once
#include <Windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

// Register unhandled exception filter that logs them.
WINDOWSUTILS_API
DWORD ErrRegisterUEF(void);

#ifdef __cplusplus
}
#endif
