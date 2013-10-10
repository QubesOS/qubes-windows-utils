#pragma once
#include <Windows.h>
#include <stdlib.h>

/* Returns number of characters in output buffer, without terminating NULL. */
DWORD ConvertUTF8ToUTF16(char *input_utf8, WCHAR **output_utf16, size_t *output_len);

/* Returns number of characters in output buffer, without terminating NULL. */
DWORD ConvertUTF16ToUTF8(WCHAR *pwszUtf16, char **ppszUtf8, size_t *pcbUtf8);
