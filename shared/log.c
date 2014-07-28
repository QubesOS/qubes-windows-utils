#include <tchar.h>
#include <windows.h>
#include <stdlib.h>
#include <strsafe.h>
#include <Lmcons.h>

#include "utf8-conv.h"
#include "log.h"
#include "config.h"

static BOOL g_LoggerInitialized = FALSE;
static HANDLE g_LogfileHandle = INVALID_HANDLE_VALUE;
static int g_LogLevel;
static TCHAR g_LogName[CFG_MODULE_MAX];

#define BUFFER_SIZE (LOG_MAX_MESSAGE_LENGTH * sizeof(TCHAR))

#define LOG_RETENTION_TIME_100NS (LOG_RETENTION_TIME * 10000000ULL)

#if (UNLEN > LOG_MAX_MESSAGE_LENGTH)
#error "UNLEN > LOG_MAX_MESSAGE_LENGTH"
#endif

void purge_old_logs(TCHAR *log_dir)
{
    FILETIME ft;
    PULARGE_INTEGER thresholdTime = (PULARGE_INTEGER)&ft;
    WIN32_FIND_DATA findData;
    HANDLE findHandle;
    TCHAR searchMask[MAX_PATH];
    TCHAR filePath[MAX_PATH];

    GetSystemTimeAsFileTime(&ft);
    (*thresholdTime).QuadPart -= LOG_RETENTION_TIME_100NS;

    StringCchPrintf(searchMask, RTL_NUMBER_OF(searchMask), TEXT("%s\\%s*.*"), log_dir, g_LogName);

    findHandle = FindFirstFile(searchMask, &findData);
    if (findHandle == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if ((*(PULARGE_INTEGER)&findData.ftCreationTime).QuadPart < thresholdTime->QuadPart)
        {
            // File is too old, delete.
            StringCchPrintf(filePath, RTL_NUMBER_OF(filePath), TEXT("%s\\%s"), log_dir, findData.cFileName);
            DeleteFile(filePath);
        }
    } while (FindNextFile(findHandle, &findData));

    FindClose(findHandle);
}

void get_log_level(void)
{
    DWORD status;
    status = CfgReadDword(g_LogName, LOG_CONFIG_LEVEL_VALUE, &g_LogLevel, NULL);

    if (status != ERROR_SUCCESS)
        g_LogLevel = LOG_LEVEL_ERROR; // default
}

void log_init(TCHAR *log_dir, TCHAR *base_name)
{
    SYSTEMTIME st;
    DWORD len = 0;
    TCHAR *format = TEXT("%s\\%s-%04d%02d%02d-%02d%02d%02d-%d.log");
    TCHAR system_path[MAX_PATH];
    TCHAR buffer[MAX_PATH];

    StringCchCopy(g_LogName, RTL_NUMBER_OF(g_LogName), base_name);

    GetLocalTime(&st);

    // if log_dir is NULL, use default log location
    if (!log_dir)
    {
        // Prepare default log path (%SYSTEMDRIVE%\QubesLogs)
        if (!GetSystemDirectory(system_path, RTL_NUMBER_OF(system_path)))
        {
            perror("GetSystemDirectory"); // this will just write to stderr before logfile is initialized
            goto fallback;
        }
        if (FAILED(StringCchCopy(system_path+3, RTL_NUMBER_OF(system_path)-3, TEXT("QubesLogs\0"))))
        {
            errorf("StringCchCopy failed");
            goto fallback;
        }
        if (!CreateDirectory(system_path, NULL))
        {
            if (GetLastError() != ERROR_ALREADY_EXISTS)
            {
                perror("CreateDirectory");
                errorf("failed to create %s\n", system_path);
                goto fallback;
            }
        }
        log_dir = system_path;
    }

    purge_old_logs(log_dir);

    memset(buffer, 0, sizeof(buffer));
    if (FAILED(StringCchPrintf(buffer, RTL_NUMBER_OF(buffer),
        format,
        log_dir, base_name, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        GetCurrentProcessId()
        )))
    {
        errorf("StringCchPrintf(full path) failed");
        goto fallback;
    }

    log_start(buffer);

fallback:
    logf("\nLog started: %s\n", buffer);
    memset(buffer, 0, sizeof(buffer));

    // if we pass too large buffer it returns ERROR_BUFFER_TOO_SMALL... go figure
    len = UNLEN;
    if (!GetUserName(buffer, &len))
    {
        perror("GetUserName");
        exit(1);
    }
    logf("Running as user: %s, process ID: %d\n", buffer, GetCurrentProcessId());
}

// Use the log directory from registry config.
DWORD log_init_default(PWCHAR log_name)
{
    DWORD status;
    WCHAR log_path[MAX_PATH];

    status = CfgReadString(log_name, LOG_CONFIG_PATH_VALUE, log_path, RTL_NUMBER_OF(log_path), NULL);
    if (ERROR_SUCCESS != status)
    {
        // failed, use default location
        // todo: use event log
        log_init(NULL, log_name);
        perror("CfgReadString(log path)");
    }
    else
    {
        log_init(log_path, log_name);
    }

    get_log_level();

    return status;
}

// create the log file
// if logfile_path is NULL, use stderr
void log_start(TCHAR *logfile_path)
{
    BYTE utf8_bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD len;

    if (!g_LoggerInitialized)
    {
        if (logfile_path)
        {
            g_LogfileHandle = CreateFile(
                logfile_path,
                GENERIC_WRITE,
                FILE_SHARE_READ,
                NULL, // fixme: security attrs
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                NULL);

            if (g_LogfileHandle == INVALID_HANDLE_VALUE)
            {
                _ftprintf(stderr, TEXT("log_start: CreateFile(%s) failed: error %d\n"),
                    logfile_path, GetLastError());
                exit(1);
            }

            // seek to end
            len = SetFilePointer(g_LogfileHandle, 0, 0, SEEK_END);
            if (INVALID_SET_FILE_POINTER == len)
            {
                _ftprintf(stderr, TEXT("log_start: SetFilePointer(%s) failed: error %d\n"),
                    logfile_path, GetLastError());
                exit(1);
            }

            if (len == 0) // fresh file - write BOM
            {
                if (!WriteFile(g_LogfileHandle, utf8_bom, 3, &len, NULL))
                {
                    _ftprintf(stderr, TEXT("log_start: WriteFile(%s) failed: error %d\n"),
                        logfile_path, GetLastError());
                    exit(1);
                }
            }
        }
    }
    g_LoggerInitialized = TRUE;
}

void log_flush()
{
    if (g_LoggerInitialized && g_LogfileHandle != INVALID_HANDLE_VALUE)
        FlushFileBuffers(g_LogfileHandle);
}

void _logf(BOOL echo_to_stderr, BOOL raw, const char *function_name, TCHAR *format, ...)
{
    va_list args;
    size_t buffer_size = 0;
    DWORD written = 0;
    SYSTEMTIME st;
    TCHAR prefix_buffer[256];
    int prefix_len = 0;
    TCHAR buffer[BUFFER_SIZE];
    char *newline = "\n";
#define NEWLINE_LEN 1
    BOOL add_newline = FALSE;
#ifdef UNICODE
    char *buffer_utf8 = NULL;
    char *prefix_buffer_utf8 = NULL;
#endif
    size_t prefix_buffer_size = 0;
    DWORD last_error = GetLastError(); // preserve last error

#define PREFIX_FORMAT TEXT("[%04d%02d%02d.%02d%02d%02d.%03d][%d] ")
#ifdef UNICODE
#define PREFIX_FORMAT_FUNCNAME TEXT("%S: ")
#else
#define PREFIX_FORMAT_FUNCNAME TEXT("%s: ")
#endif
    if (!raw)
    {
        memset(prefix_buffer, 0, sizeof(prefix_buffer));
        GetLocalTime(&st); // or system time (UTC)?
        prefix_len = _stprintf_s(prefix_buffer, RTL_NUMBER_OF(prefix_buffer),
            function_name ? PREFIX_FORMAT PREFIX_FORMAT_FUNCNAME : PREFIX_FORMAT,
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            GetCurrentThreadId(), function_name);
    }

    va_start(args, format);

    memset(buffer, 0, sizeof(buffer));
    // format buffer
    buffer_size = _vstprintf_s(buffer, LOG_MAX_MESSAGE_LENGTH, format, args) * sizeof(TCHAR);
    va_end(args);

#ifdef UNICODE
    if (!raw)
    {
        if (ERROR_SUCCESS != ConvertUTF16ToUTF8(prefix_buffer, &prefix_buffer_utf8, &prefix_buffer_size))
        {
            _ftprintf(stderr, TEXT("_logf: ConvertUTF16ToUTF8 failed: error %d\n"), GetLastError());
            exit(1);
        }
    }

    if (ERROR_SUCCESS != ConvertUTF16ToUTF8(buffer, &buffer_utf8, &buffer_size))
    {
        _ftprintf(stderr, TEXT("_logf: ConvertUTF16ToUTF8 failed: error %d\n"), GetLastError());
        exit(1);
    }
    if (buffer_utf8[buffer_size - 1] != '\n')
        add_newline = TRUE;
#else
    if (buffer[buffer_size-1] != '\n')
        add_newline = TRUE;
    if (!raw)
        prefix_buffer_size = prefix_len;
#endif

    if (g_LogfileHandle != INVALID_HANDLE_VALUE)
    {
        if (!raw)
        {
#ifdef UNICODE
            if (!WriteFile(g_LogfileHandle, prefix_buffer_utf8, (DWORD)prefix_buffer_size, &written, NULL) || written != (DWORD)prefix_buffer_size)
#else
            if (!WriteFile(g_LogfileHandle, prefix_buffer, (DWORD)prefix_buffer_size, &written, NULL) || written != (DWORD)prefix_buffer_size)
#endif
            {
                _ftprintf(stderr, TEXT("_logf: WriteFile failed: error %d\n"), GetLastError());
                exit(1);
            }
        }

        // buffer_size is at most INT_MAX*2
#ifdef UNICODE
        if (!WriteFile(g_LogfileHandle, buffer_utf8, (DWORD)buffer_size, &written, NULL) || written != (DWORD)buffer_size)
#else
        if (!WriteFile(g_LogfileHandle, buffer, (DWORD)buffer_size, &written, NULL) || written != (DWORD)buffer_size)
#endif
        {
            _ftprintf(stderr, TEXT("_logf: WriteFile failed: error %d\n"), GetLastError());
            exit(1);
        }

        if (add_newline && !raw)
        {
            if (!WriteFile(g_LogfileHandle, newline, NEWLINE_LEN, &written, NULL) || written != NEWLINE_LEN)
            {
                _ftprintf(stderr, TEXT("_logf: WriteFile failed: error %d\n"), GetLastError());
                exit(1);
            }
        }

        if (echo_to_stderr)
        {
            if (!raw)
                _ftprintf(stderr, prefix_buffer);
            _ftprintf(stderr, buffer);
            if (add_newline && !raw)
            {
#ifdef UNICODE
                _ftprintf(stderr, TEXT("%S"), newline);
#else
                _ftprintf(stderr, TEXT("%s"), newline);
#endif
            }

#if defined(DEBUG) || defined(_DEBUG)
            OutputDebugString(buffer);
#endif
        }
    }
    else // use stderr
    {
        if (!raw)
            _ftprintf(stderr, TEXT("%s"), prefix_buffer);
        _ftprintf(stderr, TEXT("%s"), buffer);
        if (add_newline && !raw)
        {
#ifdef UNICODE
            _ftprintf(stderr, TEXT("%S"), newline);
#else
            _ftprintf(stderr, TEXT("%s"), newline);
#endif
        }
    }

#ifdef UNICODE
    if (!raw)
        free(prefix_buffer_utf8);
    free(buffer_utf8);
#endif
#ifdef LOG_SAFE_FLUSH
    log_flush();
#endif
    SetLastError(last_error);
}

// Helper function to report errors. Similar to perror, but uses GetLastError() instead of errno.
DWORD _perror(const char *function_name, TCHAR *prefix)
{
    size_t  char_count;
    TCHAR  *message = NULL;
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
        (TCHAR*)&message,
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

    _logf(TRUE, FALSE, function_name, TEXT("%s%s"), prefix, buffer);
    SetLastError(error_code); // preserve
    return error_code;
}

// disabled if LOG_NO_HEX_DUMP is defined
void _hex_dump(TCHAR *desc, void *addr, int len)
{
    int i;
    TCHAR buff[17];
    BYTE *pc = (BYTE*)addr;

    if (len == 0)
        return;

    // Output description if given.
    if (desc != NULL)
        debugf_raw("%s:\n", desc);

    // Process every byte in the data.
    for (i = 0; i < len; i++) {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            if (i != 0)
                debugf_raw("  %s\n", buff);

            // Output the offset.
            debugf_raw("%04x:", i);
        }

        // Now the hex code for the specific character.
        if (i % 8 == 0 && i % 16 != 0)
            debugf_raw("  %02x", pc[i]);
        else
            debugf_raw(" %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = TEXT('.');
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = TEXT('\0');
    }

    // Pad out last line if not exactly 16 characters.
    if (i % 16 <= 8 && i % 16 != 0)
        debugf_raw(" ");
    while ((i % 16) != 0) {
        debugf_raw("   ");
        i++;
    }

    // And print the final ASCII bit.
    debugf_raw("  %s\n", buff);
}