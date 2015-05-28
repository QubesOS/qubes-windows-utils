#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

// function prototypes
WINDOWSUTILS_API
WCHAR GetOption(int argc, WCHAR** argv, WCHAR* pszValidOpts, WCHAR** ppszParam);

#ifdef __cplusplus
}
#endif
