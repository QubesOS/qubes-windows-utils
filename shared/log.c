#include <tchar.h>
#include <windows.h>
#include <stdlib.h>
#include <Shlwapi.h>
#include <ShlObj.h>
#include <strsafe.h>
#include <Lmcons.h>
#include "utf8-conv.h"
#include "log.h"

static BOOL logger_initialized = FALSE;
static HANDLE logfile_handle = INVALID_HANDLE_VALUE;

#define BUFFER_SIZE (LOG_MAX_MESSAGE_LENGTH * sizeof(TCHAR))

#if (UNLEN > LOG_MAX_MESSAGE_LENGTH)
#error "UNLEN > LOG_MAX_MESSAGE_LENGTH"
#endif

void log_init(TCHAR *log_dir, TCHAR *base_name)
{
    SYSTEMTIME st;
    DWORD len = 0;
    TCHAR *format = TEXT("%s\\%s-%04d%02d%02d-%02d%02d%02d-%d.log");
#if !defined(DEBUG) && !defined(_DEBUG)
    TCHAR appdata_path[MAX_PATH];
#endif
    TCHAR buffer[MAX_PATH];

    GetLocalTime(&st);

    // if log_dir is NULL, use default log location
    if (!log_dir)
    {
#if defined(DEBUG) || defined(_DEBUG)
        log_dir = TEXT(DEBUG_LOG_DIR);
#else
        memset(appdata_path, 0, sizeof(appdata_path));
        // use current user's profile directory
        if (FAILED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata_path)))
        {
            perror("log_init: SHGetFolderPath"); // this will just write to stderr before logfile is initialized
            exit(1);
        }
        if (!PathAppend(appdata_path, TEXT("Qubes"))) // PathCchAppend requires win 8...
        {
            perror("log_init: PathAppend");
            exit(1);
        }
        if (!CreateDirectory(appdata_path, NULL))
        {
            if (GetLastError() != ERROR_ALREADY_EXISTS)
            {
                perror("log_init: CreateDirectory");
                errorf("failed to create %s\n", appdata_path);
                exit(1);
            }
        }
        log_dir = appdata_path;
#endif
    }

    memset(buffer, 0, sizeof(buffer));
    if (FAILED(StringCchPrintf(buffer, RTL_NUMBER_OF(buffer), 
        format, 
        log_dir, base_name, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        GetCurrentProcessId()
        )))
    {
        perror("log_init: StringCchPrintf");
        exit(1);
    }

    log_start(buffer);

    logf("\nLog started: %s\n", buffer);
    memset(buffer, 0, sizeof(buffer));

    // if we pass too large buffer it returns ERROR_BUFFER_TOO_SMALL... go figure
    len = UNLEN;
    if (!GetUserName(buffer, &len))
    {
        perror("log_init: GetUserName");
        exit(1);
    }
    logf("Running as user: %s\n", buffer);
}

// if logfile_path is NULL, use stderr
void log_start(TCHAR *logfile_path)
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

void log_flush()
{
    if (logger_initialized)
        FlushFileBuffers(logfile_handle);
}

void _logf(BOOL echo_to_stderr, TCHAR *format, ...)
{
    va_list args;
    size_t buffer_size = 0;
    DWORD written = 0;
    SYSTEMTIME st;
    TCHAR time_buffer[64];
    int time_len = 0;
    TCHAR buffer[BUFFER_SIZE];
    char *newline = "\n";
#define NEWLINE_LEN 1
    BOOL add_newline = FALSE;
#ifdef UNICODE
    char *buffer_utf8 = NULL;
    char *time_buffer_utf8 = NULL;
    size_t time_buffer_size = 0;
#endif

    memset(time_buffer, 0, sizeof(time_buffer));
    GetLocalTime(&st); // or system time (UTC)?
    time_len = _stprintf_s(time_buffer, RTL_NUMBER_OF(time_buffer), 
        TEXT("[%04d%02d%02d.%02d%02d%02d.%03d] "), 
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_start(args, format);

    memset(buffer, 0, sizeof(buffer));
    // format buffer
    buffer_size = _vstprintf_s(buffer, LOG_MAX_MESSAGE_LENGTH, format, args) * sizeof(TCHAR);
    va_end(args);

#ifdef UNICODE
    if (ERROR_SUCCESS != ConvertUTF16ToUTF8(time_buffer, &time_buffer_utf8, &time_buffer_size))
    {
        _ftprintf(stderr, TEXT("_logf: ConvertUTF16ToUTF8 failed: error %d\n"), GetLastError());
        exit(1);
    }
    if (ERROR_SUCCESS != ConvertUTF16ToUTF8(buffer, &buffer_utf8, &buffer_size))
    {
        _ftprintf(stderr, TEXT("_logf: ConvertUTF16ToUTF8 failed: error %d\n"), GetLastError());
        exit(1);
    }
    if (buffer_utf8[buffer_size-1] != '\n')
        add_newline = TRUE;
#else
    if (buffer[buffer_size-1] != '\n')
        add_newline = TRUE;
#endif

    if (logfile_handle != INVALID_HANDLE_VALUE)
    {
#ifdef UNICODE
        if (!WriteFile(logfile_handle, time_buffer_utf8, (DWORD)time_buffer_size, &written, NULL) || written != (DWORD)time_buffer_size)
#else
        if (!WriteFile(logfile_handle, time_buffer, (DWORD)time_buffer_size-sizeof(TCHAR), &written, NULL) || written != (DWORD)time_buffer_size-sizeof(TCHAR))
#endif
        {
            _ftprintf(stderr, TEXT("_logf: WriteFile failed: error %d\n"), GetLastError());
            exit(1);
        }

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

        if (add_newline)
        {
            if (!WriteFile(logfile_handle, newline, NEWLINE_LEN, &written, NULL) || written != NEWLINE_LEN)
            {
                _ftprintf(stderr, TEXT("_logf: WriteFile failed: error %d\n"), GetLastError());
                exit(1);
            }
        }

        if (echo_to_stderr)
        {
            _ftprintf(stderr, time_buffer);
            _ftprintf(stderr, buffer);
            if (add_newline)
            {
#ifdef UNICODE
                _ftprintf(stderr, TEXT("%S"), newline);
#else
                _ftprintf(stderr, TEXT("%s"), newline);
#endif
            }
        }
    }
    else // use stderr
    {
        _ftprintf(stderr, TEXT("%s"), time_buffer);
        _ftprintf(stderr, TEXT("%s"), buffer);
        if (add_newline)
        {
#ifdef UNICODE
            _ftprintf(stderr, TEXT("%S"), newline);
#else
            _ftprintf(stderr, TEXT("%s"), newline);
#endif
        }
    }

#ifdef UNICODE
    free(time_buffer_utf8);
    free(buffer_utf8);
#endif
}

/** Helper function to report errors. Similar to perror, but uses GetLastError() instead of errno
 * @param prefix Error message prefix
 */
void _perror(TCHAR *prefix)
{
    size_t  char_count;
    WCHAR  *message = NULL;
    TCHAR   buffer[2048];
    HRESULT ret;
    DWORD   error_code;

    error_code = GetLastError();

    memset(buffer, 0, sizeof(buffer));
    char_count = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (WCHAR*)&message,
        0,
        NULL);
    if (!char_count)
    {
        if (FAILED(StringCchPrintf(buffer, RTL_NUMBER_OF(buffer), TEXT(" failed with error 0x%08x\n"), error_code)))
            exit(1);
    }
    else
    {
        ret = StringCchPrintf(
            buffer,
            RTL_NUMBER_OF(buffer),
            TEXT(" failed with error %d: %s%s"),
            error_code,
            message,
            ((char_count >= 1) && (0x0a == message[char_count - 1])) ? TEXT("") : TEXT("\n"));
        LocalFree(message);

        if (FAILED(ret))
            exit(1);
    }

    errorf("%s%s", prefix, buffer);
}

// disabled if LOG_NO_HEX_DUMP is defined
void _hex_dump (TCHAR *desc, void *addr, int len)
{
    int i;
    TCHAR buff[17];
    BYTE *pc = (BYTE*)addr;

    if (len == 0)
        return;

// Output description if given.
    if (desc != NULL)
        debugf("%s:\n", desc);

    // Process every byte in the data.
    for (i = 0; i < len; i++) {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            if (i != 0)
                debugf("  %s\n", buff);

            // Output the offset.
            debugf("%04x:", i);
        }

        // Now the hex code for the specific character.
        if (i % 8 == 0 && i % 16 != 0)
            debugf("  %02x", pc[i]);
        else
            debugf(" %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = TEXT('.');
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = TEXT('\0');
    }

    // Pad out last line if not exactly 16 characters.
    if (i%16 <= 8 && i%16 != 0)
        debugf(" ");
    while ((i % 16) != 0) {
        debugf("   ");
        i++;
    }

    // And print the final ASCII bit.
    debugf("  %s\n", buff);
}
