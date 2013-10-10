#include <Windows.h>
#include <stdlib.h>
#include <strsafe.h>

DWORD ConvertUTF8ToUTF16(char *input_utf8, WCHAR **output_utf16, size_t *output_len)
{
	int result;
	size_t len_utf8;
	int len_utf16;
	WCHAR *buf_utf16;

	if (FAILED(StringCchLengthA(input_utf8, STRSAFE_MAX_CCH, &len_utf8)))
		return GetLastError();

	if (len_utf8-1 >= INT_MAX)
		return ERROR_BAD_ARGUMENTS;

	len_utf16 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input_utf8, (int)len_utf8 + 1, NULL, 0);
	if (!len_utf16) {
		return GetLastError();
	}

	buf_utf16 = (WCHAR*) malloc(len_utf16 * sizeof(WCHAR));
	if (!buf_utf16)
		return ERROR_NOT_ENOUGH_MEMORY;

	result = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input_utf8, (int)len_utf8 + 1, buf_utf16, len_utf16);
	if (!result) {
		free(buf_utf16);
		return GetLastError();
	}

	buf_utf16[len_utf16 - 1] = L'\0';
	*output_utf16 = buf_utf16;
	if (output_len)
		*output_len = len_utf16 - 1; /* without terminating NULL character */

	return ERROR_SUCCESS;
}

DWORD ConvertUTF16ToUTF8(WCHAR *input_utf16, char **output_utf8, size_t *output_len)
{
	char *but_utf8;
	int len_utf8;
	DWORD conversion_flags = 0;

	// WC_ERR_INVALID_CHARS is defined for Vista and later only
#if (WINVER >= 0x0600)
	conversion_flags = WC_ERR_INVALID_CHARS;
#endif

	/* convert filename from UTF-16 to UTF-8 */
	/* calculate required size */
	len_utf8 = WideCharToMultiByte(CP_UTF8, conversion_flags, input_utf16, -1, NULL, 0, NULL, NULL);
	if (!len_utf8) {
		return GetLastError();
	}
	but_utf8 = (char*) malloc(sizeof(PUCHAR)*len_utf8);
	if (!but_utf8) {
		return ERROR_NOT_ENOUGH_MEMORY;
	}
	if (!WideCharToMultiByte(CP_UTF8, conversion_flags, input_utf16, -1, but_utf8, len_utf8, NULL, NULL)) {
		free(but_utf8);
		return GetLastError();
	}
	if (output_len)
		*output_len = len_utf8 - 1; /* without terminating NULL character */
	*output_utf8 = but_utf8;
	return ERROR_SUCCESS;
}
