#include <tchar.h>
#include <windows.h>
#include <stdlib.h>
#include <strsafe.h>
#include <Lmcons.h>
#include <Shlwapi.h>

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

static void PurgeOldLogs(TCHAR *logDir)
{
    FILETIME ft;
    PULARGE_INTEGER thresholdTime = (PULARGE_INTEGER)&ft;
    WIN32_FIND_DATA findData;
    HANDLE findHandle;
    TCHAR searchMask[MAX_PATH];
    TCHAR filePath[MAX_PATH];

    GetSystemTimeAsFileTime(&ft);
    (*thresholdTime).QuadPart -= LOG_RETENTION_TIME_100NS;

    StringCchPrintf(searchMask, RTL_NUMBER_OF(searchMask), TEXT("%s\\%s*.*"), logDir, g_LogName);

    findHandle = FindFirstFile(searchMask, &findData);
    if (findHandle == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if ((*(PULARGE_INTEGER)&findData.ftCreationTime).QuadPart < thresholdTime->QuadPart)
        {
            // File is too old, delete.
            StringCchPrintf(filePath, RTL_NUMBER_OF(filePath), TEXT("%s\\%s"), logDir, findData.cFileName);
            DeleteFile(filePath);
        }
    } while (FindNextFile(findHandle, &findData));

    FindClose(findHandle);
}

void LogGetLevel(void)
{
    DWORD status; 
    status = CfgReadDword(g_LogName, LOG_CONFIG_LEVEL_VALUE, &g_LogLevel, NULL);

    if (status != ERROR_SUCCESS)
        g_LogLevel = LOG_LEVEL_ERROR; // default
}

void LogInit(TCHAR *logDir, TCHAR *baseName)
{
    SYSTEMTIME st;
    DWORD len = 0;
    TCHAR *format = TEXT("%s\\%s-%04d%02d%02d-%02d%02d%02d-%d.log");
    TCHAR systemPath[MAX_PATH];
    TCHAR buffer[MAX_PATH];

    StringCchCopy(g_LogName, RTL_NUMBER_OF(g_LogName), baseName);

    // if logDir is NULL, use default log location
    if (!logDir)
    {
        // Prepare default log path (%SYSTEMDRIVE%\QubesLogs)
        GetLocalTime(&st);

        if (!GetSystemDirectory(systemPath, RTL_NUMBER_OF(systemPath)))
        {
            perror("GetSystemDirectory"); // this will just write to stderr before logfile is initialized
            goto fallback;
        }
        if (FAILED(StringCchCopy(systemPath + 3, RTL_NUMBER_OF(systemPath) - 3, TEXT("QubesLogs\0"))))
        {
            errorf("StringCchCopy failed");
            goto fallback;
        }
        if (!CreateDirectory(systemPath, NULL))
        {
            if (GetLastError() != ERROR_ALREADY_EXISTS)
            {
                perror("CreateDirectory");
                errorf("failed to create %s\n", systemPath);
                goto fallback;
            }
        }
        logDir = systemPath;
    }

    PurgeOldLogs(logDir);

    memset(buffer, 0, sizeof(buffer));
    if (FAILED(StringCchPrintf(buffer, RTL_NUMBER_OF(buffer),
        format,
        logDir, baseName, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        GetCurrentProcessId()
        )))
    {
        errorf("StringCchPrintf(full path) failed");
        goto fallback;
    }

    LogStart(buffer);

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
// If log_name is NULL, use current executable as name.
DWORD LogInitDefault(TCHAR *logName)
{
    DWORD status;
    TCHAR logPath[MAX_PATH];
    TCHAR exePath[MAX_PATH];

    if (!logName)
    {
        if (!GetModuleFileName(NULL, exePath, RTL_NUMBER_OF(exePath)))
        {
            status = GetLastError();
            goto fallback;
        }
        PathRemoveExtension(exePath);
        logName = PathFindFileName(exePath);
        if (logName == exePath) // failed
        {
            status = ERROR_INVALID_NAME;

        fallback:
            // log to stderr only
            LogStart(NULL);
            LogGetLevel();
            return status;
        }
    }

    status = CfgReadString(logName, LOG_CONFIG_PATH_VALUE, logPath, RTL_NUMBER_OF(logPath), NULL);
    if (ERROR_SUCCESS != status)
    {
        // failed, use default location
        // todo: use event log
        LogInit(NULL, logName);
        SetLastError(status);
        perror("CfgReadString(log path)");
    }
    else
    {
        LogInit(logPath, logName);
    }

    LogGetLevel();

    return status;
}

// create the log file
// if logfile_path is NULL, use stderr
void LogStart(TCHAR *logfilePath)
{
    BYTE utf8Bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD len;

    if (!g_LoggerInitialized)
    {
        if (logfilePath)
        {
            g_LogfileHandle = CreateFile(
                logfilePath,
                GENERIC_WRITE,
                FILE_SHARE_READ,
                NULL, // fixme: security attrs
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                NULL);

            if (g_LogfileHandle == INVALID_HANDLE_VALUE)
            {
                _ftprintf(stderr, TEXT("log_start: CreateFile(%s) failed: error %d\n"),
                    logfilePath, GetLastError());
                exit(1);
            }

            // seek to end
            len = SetFilePointer(g_LogfileHandle, 0, 0, SEEK_END);
            if (INVALID_SET_FILE_POINTER == len)
            {
                _ftprintf(stderr, TEXT("log_start: SetFilePointer(%s) failed: error %d\n"),
                    logfilePath, GetLastError());
                exit(1);
            }

            if (len == 0) // fresh file - write BOM
            {
                if (!WriteFile(g_LogfileHandle, utf8Bom, 3, &len, NULL))
                {
                    _ftprintf(stderr, TEXT("log_start: WriteFile(%s) failed: error %d\n"),
                        logfilePath, GetLastError());
                    exit(1);
                }
            }
        }
    }
    g_LoggerInitialized = TRUE;
}

void LogFlush()
{
    if (g_LoggerInitialized && g_LogfileHandle != INVALID_HANDLE_VALUE)
        FlushFileBuffers(g_LogfileHandle);
}

void _logf(BOOL echoToStderr, BOOL raw, const char *functionName, TCHAR *format, ...)
{
    va_list args;
    size_t bufferSize = 0;
    DWORD written = 0;
    SYSTEMTIME st;
    TCHAR prefixBuffer[256];
    int prefixLen = 0;
    TCHAR buffer[BUFFER_SIZE];
    char *newline = "\n";
#define NEWLINE_LEN 1
    BOOL addNewline = FALSE;
#ifdef UNICODE
    char *bufferUtf8 = NULL;
    char *prefixBufferUtf8 = NULL;
#endif
    size_t prefixBufferSize = 0;
    DWORD lastError = GetLastError(); // preserve last error

    if (!g_LoggerInitialized)
        LogInitDefault(NULL);

#define PREFIX_FORMAT TEXT("[%04d%02d%02d.%02d%02d%02d.%03d][%d] ")
#ifdef UNICODE
#define PREFIX_FORMAT_FUNCNAME TEXT("%S: ")
#else
#define PREFIX_FORMAT_FUNCNAME TEXT("%s: ")
#endif
    if (!raw)
    {
        memset(prefixBuffer, 0, sizeof(prefixBuffer));
        GetLocalTime(&st); // or system time (UTC)?
        prefixLen = _stprintf_s(prefixBuffer, RTL_NUMBER_OF(prefixBuffer),
            functionName ? PREFIX_FORMAT PREFIX_FORMAT_FUNCNAME : PREFIX_FORMAT,
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            GetCurrentThreadId(), functionName);
    }

    va_start(args, format);

    memset(buffer, 0, sizeof(buffer));
    // format buffer
    bufferSize = _vstprintf_s(buffer, LOG_MAX_MESSAGE_LENGTH, format, args) * sizeof(TCHAR);
    va_end(args);

#ifdef UNICODE
    if (!raw)
    {
        if (ERROR_SUCCESS != ConvertUTF16ToUTF8(prefixBuffer, &prefixBufferUtf8, &prefixBufferSize))
        {
            _ftprintf(stderr, TEXT("_logf: ConvertUTF16ToUTF8 failed: error %d\n"), GetLastError());
            exit(1);
        }
    }

    if (ERROR_SUCCESS != ConvertUTF16ToUTF8(buffer, &bufferUtf8, &bufferSize))
    {
        _ftprintf(stderr, TEXT("_logf: ConvertUTF16ToUTF8 failed: error %d\n"), GetLastError());
        exit(1);
    }
    if (bufferUtf8[bufferSize - 1] != '\n')
        addNewline = TRUE;
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
            if (!WriteFile(g_LogfileHandle, prefixBufferUtf8, (DWORD)prefixBufferSize, &written, NULL) || written != (DWORD)prefixBufferSize)
#else
            if (!WriteFile(g_LogfileHandle, prefixBuffer, (DWORD)prefixBufferSize, &written, NULL) || written != (DWORD)prefixBufferSize)
#endif
            {
                _ftprintf(stderr, TEXT("_logf: WriteFile failed: error %d\n"), GetLastError());
                exit(1);
            }
        }

        // buffer_size is at most INT_MAX*2
#ifdef UNICODE
        if (!WriteFile(g_LogfileHandle, bufferUtf8, (DWORD)bufferSize, &written, NULL) || written != (DWORD)bufferSize)
#else
        if (!WriteFile(g_LogfileHandle, buffer, (DWORD)bufferSize, &written, NULL) || written != (DWORD)bufferSize)
#endif
        {
            _ftprintf(stderr, TEXT("_logf: WriteFile failed: error %d\n"), GetLastError());
            exit(1);
        }

        if (addNewline && !raw)
        {
            if (!WriteFile(g_LogfileHandle, newline, NEWLINE_LEN, &written, NULL) || written != NEWLINE_LEN)
            {
                _ftprintf(stderr, TEXT("_logf: WriteFile failed: error %d\n"), GetLastError());
                exit(1);
            }
        }

        if (echoToStderr)
        {
            if (!raw)
                _ftprintf(stderr, prefixBuffer);
            _ftprintf(stderr, buffer);
            if (addNewline && !raw)
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
            _ftprintf(stderr, TEXT("%s"), prefixBuffer);
        _ftprintf(stderr, TEXT("%s"), buffer);
        if (addNewline && !raw)
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
        free(prefixBufferUtf8);
    free(bufferUtf8);
#endif

#ifdef LOG_SAFE_FLUSH
    LogFlush();
#endif

    SetLastError(lastError);
}

// Helper function to report errors. Similar to perror, but uses GetLastError() instead of errno.
DWORD _perror(const char *functionName, TCHAR *prefix)
{
    size_t  charCount;
    TCHAR  *message = NULL;
    TCHAR   buffer[2048];
    HRESULT ret;
    DWORD   errorCode;

    errorCode = GetLastError();

    memset(buffer, 0, sizeof(buffer));
    charCount = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (TCHAR*)&message,
        0,
        NULL);
    if (!charCount)
    {
        if (FAILED(StringCchPrintf(buffer, RTL_NUMBER_OF(buffer), TEXT(" failed with error 0x%08x\n"), errorCode)))
            exit(1);
    }
    else
    {
        ret = StringCchPrintf(
            buffer,
            RTL_NUMBER_OF(buffer),
            TEXT(" failed with error %d: %s%s"),
            errorCode,
            message,
            ((charCount >= 1) && (0x0a == message[charCount - 1])) ? TEXT("") : TEXT("\n"));
        LocalFree(message);

        if (FAILED(ret))
            exit(1);
    }

    _logf(TRUE, FALSE, functionName, TEXT("%s%s"), prefix, buffer);
    SetLastError(errorCode); // preserve
    return errorCode;
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