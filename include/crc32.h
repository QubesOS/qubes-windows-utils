#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

WINDOWSUTILS_API
unsigned long Crc32_ComputeBuf(unsigned long inCrc32, const void *buffer, size_t bufferSize);

#ifdef __cplusplus
}
#endif
