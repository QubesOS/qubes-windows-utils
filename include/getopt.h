#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#   define WINDOWSUTILS_API __declspec(dllexport)
#else
#   define WINDOWSUTILS_API __declspec(dllimport)
#   ifdef _UNICODE
#       define getopt GetOptionW
#       define optarg optargW
#   else
#       define getopt GetOptionA
#       define optarg optargA
#   endif
#endif

WINDOWSUTILS_API
WCHAR GetOptionW(int argc, WCHAR** argv, WCHAR* optionString);

WINDOWSUTILS_API
CHAR GetOptionA(int argc, CHAR** argv, CHAR* optionString);

WINDOWSUTILS_API
extern int optind;

WINDOWSUTILS_API
extern char *optargA;

WINDOWSUTILS_API
extern WCHAR *optargW;

#ifdef __cplusplus
}
#endif
