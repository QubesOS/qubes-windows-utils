#pragma once
#include <Windows.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

// Returns number of characters in output buffer, without terminating NULL.
WINDOWSUTILS_API
DWORD ConvertUTF8ToUTF16(IN const char *inputUtf8, OUT WCHAR **outputUtf16, OUT size_t *cchOutput OPTIONAL);

// Returns number of characters in output buffer, without terminating NULL.
WINDOWSUTILS_API
DWORD ConvertUTF16ToUTF8(IN const WCHAR *inputUtf16, OUT char **outputUtf8, OUT size_t *cchOutput OPTIONAL);

// Free memory allocated by the conversion functions.
// This is needed for executables that don't use the same CRT as this DLL (can't just use free() in such cases).
WINDOWSUTILS_API
void ConvertFree(void *p);

#ifdef __cplusplus
}
#endif
