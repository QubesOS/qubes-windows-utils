#include <tchar.h>
#include <windows.h>
#include <stdlib.h>
#include <strsafe.h>
#include <Lmcons.h>
#include <Shlwapi.h>

#include "utf8-conv.h"
#include "log.h"
#include "config.h"
#include "error.h"

static BOOL g_LoggerInitialized = FALSE;
static HANDLE g_LogfileHandle = INVALID_HANDLE_VALUE;
static TCHAR g_LogName[CFG_MODULE_MAX] = { 0 };
static int g_LogLevel = -1; // uninitialized

#define BUFFER_SIZE (LOG_MAX_MESSAGE_LENGTH * sizeof(TCHAR))

#if (UNLEN > LOG_MAX_MESSAGE_LENGTH)
#error "UNLEN > LOG_MAX_MESSAGE_LENGTH"
#endif

static TCHAR g_LogLevelChar[] = {
    TEXT('E'),
    TEXT('W'),
    TEXT('I'),
    TEXT('D'),
    TEXT('V')
};

static void PurgeOldLogs(const IN TCHAR *logDir)
{
    FILETIME ft;
    PULARGE_INTEGER thresholdTime = (PULARGE_INTEGER) &ft;
    WIN32_FIND_DATA findData;
    HANDLE findHandle;
    TCHAR searchMask[MAX_PATH];
    TCHAR filePath[MAX_PATH];
    LARGE_INTEGER logRetentionTime = { 0 };

    // Read log retention time from registry (in seconds).
    if (ERROR_SUCCESS != CfgReadDword(g_LogName, LOG_CONFIG_RETENTION_VALUE, &logRetentionTime.LowPart, NULL))
        logRetentionTime.QuadPart = LOG_DEFAULT_RETENTION_TIME;

    logRetentionTime.QuadPart *= 10000000ULL; // convert to 100ns units

    GetSystemTimeAsFileTime(&ft);
    (*thresholdTime).QuadPart -= logRetentionTime.QuadPart;

    StringCchPrintf(searchMask, RTL_NUMBER_OF(searchMask), TEXT("%s\\%s*.*"), logDir, g_LogName);

    findHandle = FindFirstFile(searchMask, &findData);
    if (findHandle == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if ((*(PULARGE_INTEGER) &findData.ftCreationTime).QuadPart < thresholdTime->QuadPart)
        {
            // File is too old, delete.
            StringCchPrintf(filePath, RTL_NUMBER_OF(filePath), TEXT("%s\\%s"), logDir, findData.cFileName);
            DeleteFile(filePath);
        }
    } while (FindNextFile(findHandle, &findData));

    FindClose(findHandle);
}

static TCHAR *LogGetName(void)
{
    DWORD status;

    if (g_LogName[0] == 0)
    {
        status = CfgGetModuleName(g_LogName, RTL_NUMBER_OF(g_LogName));
        if (ERROR_SUCCESS != status)
            return NULL;
    }
    return g_LogName;
}

// Read verbosity level from registry config.
// This should not call LogXXX to avoid infinite loop.
static void LogReadLevel(void)
{
    DWORD status;
    status = CfgReadDword(LogGetName(), LOG_CONFIG_LEVEL_VALUE, &g_LogLevel, NULL);

    if (status != ERROR_SUCCESS)
        g_LogLevel = LOG_LEVEL_INFO; // default
}

// Explicitly set verbosity level.
void LogSetLevel(IN int level)
{
    if (level < LOG_LEVEL_MIN || level > LOG_LEVEL_MAX)
    {
        LogWarning("Ignoring invalid log level %d", level);
        return;
    }
    g_LogLevel = level;
    LogInfo("Verbosity level set to %d (%c)", g_LogLevel, g_LogLevelChar[g_LogLevel]);
}

int LogGetLevel(void)
{
    return g_LogLevel;
}

void LogInit(const IN OPTIONAL TCHAR *logDir, const IN TCHAR *logName)
{
    SYSTEMTIME st;
    DWORD len = 0;
    TCHAR *format = TEXT("%s\\%s-%04d%02d%02d-%02d%02d%02d-%d.log");
    TCHAR systemPath[MAX_PATH];
    TCHAR buffer[MAX_PATH];

    if (g_LogLevel < 0)
        g_LogLevel = LOG_LEVEL_INFO; // default

    StringCchCopy(g_LogName, RTL_NUMBER_OF(g_LogName), logName);
    GetLocalTime(&st);

    // if logDir is NULL, use default log location
    if (!logDir)
    {
        // Prepare default log path (%SYSTEMDRIVE%\QubesLogs)

        if (!GetSystemDirectory(systemPath, RTL_NUMBER_OF(systemPath)))
        {
            LogStart(NULL);
            LogWarning("GetSystemDirectory"); // this will just write to stderr before logfile is initialized
            goto fallback;
        }
        if (FAILED(StringCchCopy(systemPath + 3, RTL_NUMBER_OF(systemPath) - 3, LOG_DEFAULT_DIR TEXT("\0"))))
        {
            LogStart(NULL);
            LogWarning("StringCchCopy failed");
            goto fallback;
        }
        logDir = systemPath;
    }

    if (!CreateDirectory(logDir, NULL))
    {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            LogStart(NULL);
            perror("CreateDirectory");
            LogWarning("failed to create %s\n", logDir);
            goto fallback;
        }
    }
    PurgeOldLogs(logDir);

    memset(buffer, 0, sizeof(buffer));
    if (FAILED(StringCchPrintf(buffer, RTL_NUMBER_OF(buffer),
        format,
        logDir, g_LogName, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        GetCurrentProcessId()
        )))
    {
        LogStart(NULL);
        LogWarning("StringCchPrintf(full path) failed");
        goto fallback;
    }

    LogStart(buffer);

fallback:
    LogInfo("Log started, module name: %s\n", g_LogName);
    memset(buffer, 0, sizeof(buffer));

    // if we pass too large buffer it returns ERROR_BUFFER_TOO_SMALL... go figure
    len = UNLEN;
    if (!GetUserName(buffer, &len))
    {
        perror("GetUserName");
        LogInfo("Running as user: <UNKNOWN>, process ID: %d\n", GetCurrentProcessId());
    }
    else
    {
        LogInfo("Running as user: %s, process ID: %d\n", buffer, GetCurrentProcessId());
    }
}

// Use the log directory from registry config.
// If logName is NULL, use current executable as name.
DWORD LogInitDefault(const IN OPTIONAL TCHAR *logName)
{
    DWORD status;
    TCHAR logPath[MAX_PATH];

    if (!logName)
    {
        logName = LogGetName();
        if (logName == NULL)
        {
            // log to stderr only
            LogStart(NULL);
            g_LogLevel = LOG_LEVEL_DEFAULT;
            LogInfo("Verbosity level set to %d", g_LogLevel);
            return ERROR_INVALID_NAME;
        }
    }
    else
        StringCchCopy(g_LogName, RTL_NUMBER_OF(g_LogName), logName);

    LogReadLevel(); // needs log name

    status = CfgReadString(logName, LOG_CONFIG_PATH_VALUE, logPath, RTL_NUMBER_OF(logPath), NULL);
    if (ERROR_SUCCESS != status)
    {
        // failed, use default location
        LogInit(NULL, logName);
        SetLastError(status);
    }
    else
    {
        LogInit(logPath, logName);
    }

    LogInfo("Verbosity level set to %d", g_LogLevel);

    return status;
}

// create the log file
// if logfile_path is NULL, use stderr
void LogStart(const IN OPTIONAL TCHAR *logfilePath)
{
    BYTE utf8Bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD len, status = ERROR_SUCCESS;

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
                status = GetLastError();
                _ftprintf(stderr, TEXT("LogStart: CreateFile(%s) failed: error %d\n"), logfilePath, GetLastError());
                goto fallback;
            }

            // seek to end
            len = SetFilePointer(g_LogfileHandle, 0, 0, SEEK_END);
            if (INVALID_SET_FILE_POINTER == len)
            {
                status = GetLastError();
                _ftprintf(stderr, TEXT("LogStart: SetFilePointer(%s) failed: error %d\n"), logfilePath, GetLastError());
                goto fallback;
            }

            if (len == 0) // fresh file - write BOM
            {
                if (!WriteFile(g_LogfileHandle, utf8Bom, 3, &len, NULL))
                {
                    status = GetLastError();
                    _ftprintf(stderr, TEXT("LogStart: WriteFile(%s) failed: error %d\n"), logfilePath, GetLastError());
                    goto fallback;
                }
            }
        }
    }
fallback:
    if (status != ERROR_SUCCESS && g_LogfileHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_LogfileHandle);
        g_LogfileHandle = INVALID_HANDLE_VALUE;
    }

    g_LoggerInitialized = TRUE;
}

void LogFlush(void)
{
    if (g_LoggerInitialized && g_LogfileHandle != INVALID_HANDLE_VALUE)
        FlushFileBuffers(g_LogfileHandle);
}

void _LogFormat(IN int level, IN BOOL raw, const IN char *functionName, const IN TCHAR *format, ...)
{
    va_list args;
    size_t bufferSize = 0;
    DWORD written = 0;
    SYSTEMTIME st;
    TCHAR prefixBuffer[256];
    int prefixLen = 0;
    TCHAR *buffer = NULL;
    char *newline = "\n";
#define NEWLINE_LEN 1
    BOOL addNewline = FALSE;
#ifdef UNICODE
    char *bufferUtf8 = NULL;
    char *prefixBufferUtf8 = NULL;
#endif
    size_t prefixBufferSize = 0;
    BOOL echoToStderr = level <= LOG_LEVEL_WARNING;
    DWORD lastError = GetLastError(); // preserve last error

    ErrRegisterUEF();

    // If we're initializing ad-hoc on a first log call, first read the log level
    // so we can tell if the log file should be created now.
    if (g_LogLevel < 0)
        LogReadLevel();

    if (level > g_LogLevel)
        return;

    if (!g_LoggerInitialized)
        LogInitDefault(NULL);

    buffer = (TCHAR*) malloc(BUFFER_SIZE);

#define PREFIX_FORMAT TEXT("[%04d%02d%02d.%02d%02d%02d.%03d-%d-%c] ")
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
            GetCurrentThreadId(), g_LogLevelChar[level], functionName);
    }

    va_start(args, format);

    memset(buffer, 0, bufferSize);
    // format buffer
    bufferSize = _vstprintf_s(buffer, LOG_MAX_MESSAGE_LENGTH, format, args) * sizeof(TCHAR);
    va_end(args);

#ifdef UNICODE
    if (!raw)
    {
        if (ERROR_SUCCESS != ConvertUTF16ToUTF8(prefixBuffer, &prefixBufferUtf8, &prefixBufferSize))
        {
            _ftprintf(stderr, TEXT("_LogFormat: ConvertUTF16ToUTF8 failed: error %d\n"), GetLastError());
            goto cleanup;
        }
    }

    if (ERROR_SUCCESS != ConvertUTF16ToUTF8(buffer, &bufferUtf8, &bufferSize))
    {
        _ftprintf(stderr, TEXT("_LogFormat: ConvertUTF16ToUTF8 failed: error %d\n"), GetLastError());
        goto cleanup;
    }
    if (bufferUtf8[bufferSize - 1] != '\n')
        addNewline = TRUE;
#else
    if (buffer[bufferSize - 1] != '\n')
        addNewline = TRUE;
    if (!raw)
        prefixBufferSize = prefixLen;
#endif

    if (g_LogfileHandle != INVALID_HANDLE_VALUE)
    {
        if (!raw)
        {
#ifdef UNICODE
            if (!WriteFile(g_LogfileHandle, prefixBufferUtf8, (DWORD) prefixBufferSize, &written, NULL) || written != (DWORD) prefixBufferSize)
#else
            if (!WriteFile(g_LogfileHandle, prefixBuffer, (DWORD) prefixBufferSize, &written, NULL) || written != (DWORD) prefixBufferSize)
#endif
            {
                _ftprintf(stderr, TEXT("_LogFormat: WriteFile failed: error %d\n"), GetLastError());
                goto cleanup;
            }
        }

        // buffer_size is at most INT_MAX*2
#ifdef UNICODE
        if (!WriteFile(g_LogfileHandle, bufferUtf8, (DWORD) bufferSize, &written, NULL) || written != (DWORD) bufferSize)
#else
        if (!WriteFile(g_LogfileHandle, buffer, (DWORD) bufferSize, &written, NULL) || written != (DWORD) bufferSize)
#endif
        {
            _ftprintf(stderr, TEXT("_LogFormat: WriteFile failed: error %d\n"), GetLastError());
            goto cleanup;
        }

        if (addNewline && !raw)
        {
            if (!WriteFile(g_LogfileHandle, newline, NEWLINE_LEN, &written, NULL) || written != NEWLINE_LEN)
            {
                _ftprintf(stderr, TEXT("_LogFormat: WriteFile failed: error %d\n"), GetLastError());
                goto cleanup;
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

cleanup:

    free(buffer);

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

// Like _perror, but takes explicit error code. For cases when previous call doesn't set LastError.
DWORD _perror2(const IN char *functionName, IN DWORD errorCode, const IN TCHAR *prefix)
{
    SetLastError(errorCode);
    return _perror(functionName, prefix);
}

// Helper function to report errors. Similar to perror, but uses GetLastError() instead of errno.
DWORD _perror(const IN char *functionName, const IN TCHAR *prefix)
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
        (TCHAR*) &message,
        0,
        NULL);

    if (!charCount)
    {
        if (FAILED(StringCchPrintf(buffer, RTL_NUMBER_OF(buffer), TEXT(" failed with error 0x%08x\n"), errorCode)))
            goto cleanup;
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
            goto cleanup;
    }

    _LogFormat(LOG_LEVEL_ERROR, FALSE, functionName, TEXT("%s%s"), prefix, buffer);
cleanup:
    SetLastError(errorCode); // preserve
    return errorCode;
}

// disabled if LOG_NO_HEX_DUMP is defined
void _hex_dump(const IN TCHAR *desc, const IN void *addr, IN int len)
{
    int i;
    TCHAR buff[17];
    BYTE *pc = (BYTE*) addr;

    if (len == 0)
        return;

    // Output description if given.
    if (desc != NULL)
        LogDebugRaw("%s:\n", desc);

    // Process every byte in the data.
    for (i = 0; i < len; i++)
    {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0)
        {
            // Just don't print ASCII for the zeroth line.
            if (i != 0)
                LogDebugRaw("  %s\n", buff);

            // Output the offset.
            LogDebugRaw("%04x:", i);
        }

        // Now the hex code for the specific character.
        if (i % 8 == 0 && i % 16 != 0)
            LogDebugRaw("  %02x", pc[i]);
        else
            LogDebugRaw(" %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = TEXT('.');
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = TEXT('\0');
    }

    // Pad out last line if not exactly 16 characters.
    if (i % 16 <= 8 && i % 16 != 0)
        LogDebugRaw(" ");
    while ((i % 16) != 0)
    {
        LogDebugRaw("   ");
        i++;
    }

    // And print the final ASCII bit.
    LogDebugRaw("  %s\n", buff);
}
