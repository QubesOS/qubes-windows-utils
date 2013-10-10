#include <tchar.h>
#include <windows.h>
#include <strsafe.h>
#include <stdlib.h>

#include <utf8-conv.h>
#include "log.h"

BOOL logger_initialized = FALSE;
static HANDLE logfile_handle = INVALID_HANDLE_VALUE;

// if logfile_path is NULL, use stderr
void log_init(TCHAR *logfile_path)
{
	BYTE utf8_bom[3] = {0xEF, 0xBB, 0xBF};
	DWORD len;

	if (!logger_initialized)
	{
		if (logfile_path)
		{
			logfile_handle = CreateFile(
				logfile_path,
				GENERIC_WRITE,
				FILE_SHARE_READ,
				NULL, // fixme: security attrs
				OPEN_ALWAYS,
				FILE_ATTRIBUTE_NORMAL,
				NULL);

			if (logfile_handle == INVALID_HANDLE_VALUE)
			{
				_ftprintf(stderr, TEXT("_log_init: CreateFile(%s) failed: error %d\n"), 
					logfile_path, GetLastError());
				exit(1);
			}

			// seek to end
			len = SetFilePointer(logfile_handle, 0, 0, SEEK_END);
			if (INVALID_SET_FILE_POINTER == len)
			{
				_ftprintf(stderr, TEXT("_log_init: SetFilePointer(%s) failed: error %d\n"), 
					logfile_path, GetLastError());
				exit(1);
			}

			if (len == 0) // fresh file - write BOM
			{
				if (!WriteFile(logfile_handle, utf8_bom, 3, &len, NULL))
				{
					_ftprintf(stderr, TEXT("_log_init: WriteFile(%s) failed: error %d\n"), 
						logfile_path, GetLastError());
					exit(1);
				}
			}
		}
	}
	logger_initialized = TRUE;
}

// writes to console, or file if LOGFILE_PATH is defined
void _logf(TCHAR *format, ...)
{
	va_list args;
	TCHAR *buffer = NULL;
	int char_count = 0;
	size_t buffer_size = 0;
	DWORD written;
#ifdef UNICODE
	char *buffer_utf8 = 0;
#endif

	va_start(args, format);
	// format buffer
	char_count = _vsctprintf(format, args);
	if (char_count == INT_MAX)
	{
		_ftprintf(stderr, TEXT("_logf: message too long\n"));
		exit(1);
	}
	char_count++;
	buffer_size = char_count * sizeof(TCHAR);
	buffer = (TCHAR*) malloc(buffer_size);
	_vstprintf_s(buffer, char_count, format, args);
	va_end(args);

#ifdef UNICODE
	if (ERROR_SUCCESS != ConvertUTF16ToUTF8(buffer, &buffer_utf8, &buffer_size))
	{
		_ftprintf(stderr, TEXT("_logf: ConvertUTF16ToUTF8 failed: error %d\n"), GetLastError());
		exit(1);
	}
#endif

	if (logfile_handle != INVALID_HANDLE_VALUE)
	{
		// buffer_size is at most INT_MAX*2
#ifdef UNICODE
		if (!WriteFile(logfile_handle, buffer_utf8, (DWORD)buffer_size, &written, NULL) || written != (DWORD)buffer_size)
#else
		if (!WriteFile(logfile_handle, buffer, (DWORD)buffer_size-sizeof(TCHAR), &written, NULL) || written != (DWORD)buffer_size-sizeof(TCHAR))
#endif
		{
			_ftprintf(stderr, TEXT("_logf: WriteFile failed: error %d\n"), GetLastError());
			exit(1);
		}
	}
	else // use stderr
	{
		_ftprintf(stderr, TEXT("%s"), buffer);
	}

#ifdef UNICODE
	free(buffer_utf8);
#endif
	free(buffer);
}

/** Helper function to report errors. Similar to perror, but uses GetLastError() instead of errno
 * @param prefix Error message prefix
 */
void _perror(TCHAR *prefix)
{
	size_t  cchErrorTextSize;
	LPTSTR  pMessage = NULL;
	TCHAR   szMessage[2048];
	HRESULT ret;
	ULONG   uErrorCode;

	uErrorCode = GetLastError();

	memset(szMessage, 0, sizeof(szMessage));
	cchErrorTextSize = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		uErrorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&pMessage,
		0,
		NULL);
	if (!cchErrorTextSize)
	{
		if (FAILED(StringCchPrintf(szMessage, RTL_NUMBER_OF(szMessage), TEXT(" failed with error 0x%08x\n"), uErrorCode)))
			return;
	}
	else
	{
		ret = StringCchPrintf(
			szMessage,
			RTL_NUMBER_OF(szMessage),
			TEXT(" failed with error %d: %s%s"),
			uErrorCode,
			pMessage,
			((cchErrorTextSize >= 1) &&
			(0x0a == pMessage[cchErrorTextSize - 1])) ? TEXT("") : TEXT("\n"));
		LocalFree(pMessage);

		if (FAILED(ret))
			return;
	}

	logf("%s%s", prefix, szMessage);
}

void hex_dump (TCHAR *desc, void *addr, int len)
{
	int i;
	TCHAR buff[17];
	BYTE *pc = (BYTE*)addr;

#if !defined(DEBUG) && !defined(_DEBUG)
	return;
#endif
	if (len == 0)
		return;

// Output description if given.
	if (desc != NULL)
		logf("%s:\n", desc);

	// Process every byte in the data.
	for (i = 0; i < len; i++) {
		// Multiple of 16 means new line (with line offset).

		if ((i % 16) == 0) {
			// Just don't print ASCII for the zeroth line.
			if (i != 0)
				logf("  %s\n", buff);

			// Output the offset.
			logf("%04x:", i);
		}

		// Now the hex code for the specific character.
		if (i % 8 == 0 && i % 16 != 0)
			logf("  %02x", pc[i]);
		else
			logf(" %02x", pc[i]);

		// And store a printable ASCII character for later.
		if ((pc[i] < 0x20) || (pc[i] > 0x7e))
			buff[i % 16] = TEXT('.');
		else
			buff[i % 16] = pc[i];
		buff[(i % 16) + 1] = TEXT('\0');
	}

	// Pad out last line if not exactly 16 characters.
	if (i%16 <= 8 && i%16 != 0)
		logf(" ");
	while ((i % 16) != 0) {
		logf("   ");
		i++;
	}

	// And print the final ASCII bit.
	logf("  %s\n", buff);
}
