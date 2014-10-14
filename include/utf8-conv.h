#pragma once
#include <Windows.h>
#include <stdlib.h>

/* Returns number of characters in output buffer, without terminating NULL. */
DWORD ConvertUTF8ToUTF16(IN const char *inputUtf8, OUT WCHAR **outputUtf16, OUT size_t *cchOutput OPTIONAL);

/* Returns number of characters in output buffer, without terminating NULL. */
DWORD ConvertUTF16ToUTF8(IN const WCHAR *inputUtf16, OUT char **outputUtf8, OUT size_t *cchOutput OPTIONAL);
