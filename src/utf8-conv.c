#include <Windows.h>
#include <stdlib.h>
#include <strsafe.h>

#include "utf8-conv.h"

DWORD ConvertUTF8ToUTF16(IN const char *inputUtf8, OUT WCHAR **outputUtf16, OUT size_t *cchOutput OPTIONAL)
{
    int result;
    size_t cbUtf8;
    int cchUtf16;
    WCHAR *bufferUtf16;

    if (FAILED(StringCchLengthA(inputUtf8, STRSAFE_MAX_CCH, &cbUtf8)))
        return GetLastError();

    if (cbUtf8 - 1 >= INT_MAX)
        return ERROR_BAD_ARGUMENTS;

    cchUtf16 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, inputUtf8, (int) cbUtf8 + 1, NULL, 0);
    if (!cchUtf16)
    {
        return GetLastError();
    }

    bufferUtf16 = (WCHAR*) malloc(cchUtf16 * sizeof(WCHAR));
    if (!bufferUtf16)
        return ERROR_NOT_ENOUGH_MEMORY;

    result = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, inputUtf8, (int) cbUtf8 + 1, bufferUtf16, cchUtf16);
    if (!result)
    {
        free(bufferUtf16);
        return GetLastError();
    }

    bufferUtf16[cchUtf16 - 1] = L'\0';
    *outputUtf16 = bufferUtf16;
    if (cchOutput)
        *cchOutput = cchUtf16 - 1; /* without terminating NULL character */

    return ERROR_SUCCESS;
}

DWORD ConvertUTF16ToUTF8(IN const WCHAR *inputUtf16, OUT char **outputUtf8, OUT size_t *cchOutput OPTIONAL)
{
    char *bufferUtf8;
    int cbUtf8;
    DWORD conversionFlags = 0;

    // WC_ERR_INVALID_CHARS is defined for Vista and later only
#if (WINVER >= 0x0600)
    conversionFlags = WC_ERR_INVALID_CHARS;
#endif

    /* convert filename from UTF-16 to UTF-8 */
    /* calculate required size */
    cbUtf8 = WideCharToMultiByte(CP_UTF8, conversionFlags, inputUtf16, -1, NULL, 0, NULL, NULL);

    if (!cbUtf8)
    {
        return GetLastError();
    }

    bufferUtf8 = (char*) malloc(cbUtf8);
    if (!bufferUtf8)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    if (!WideCharToMultiByte(CP_UTF8, conversionFlags, inputUtf16, -1, bufferUtf8, cbUtf8, NULL, NULL))
    {
        free(bufferUtf8);
        return GetLastError();
    }

    if (cchOutput)
        *cchOutput = cbUtf8 - 1; /* without terminating NULL character */
    *outputUtf8 = bufferUtf8;
    return ERROR_SUCCESS;
}