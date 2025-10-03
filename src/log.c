/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (c) Invisible Things Lab
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <windows.h>
#include <PathCch.h>
#include <stdlib.h>
#include <strsafe.h>

#include "utf8-conv.h"
#include "log.h"
#include "config.h"
#include "error.h"
#include "exec.h"
#include "qubes-io.h"

static BOOL g_LoggerInitialized = FALSE;
static BOOL g_SafeFlush = FALSE;
static HANDLE g_LogfileHandle = INVALID_HANDLE_VALUE;
static WCHAR g_LogName[CFG_MODULE_MAX] = { 0 };
static int g_LogLevel = -1; // uninitialized
static CRITICAL_SECTION g_Lock = { 0 };

#define BUFFER_SIZE (LOG_MAX_MESSAGE_LENGTH * sizeof(WCHAR))

#if (UNLEN > LOG_MAX_MESSAGE_LENGTH)
#error "UNLEN > LOG_MAX_MESSAGE_LENGTH"
#endif

static WCHAR g_Buffer[BUFFER_SIZE];
static char g_BufferUtf8[CONVERT_MAX_BUFFER_LENGTH];
static char g_PrefixBufferUtf8[CONVERT_MAX_BUFFER_LENGTH];

static WCHAR g_LogLevelChar[] = {
    L'?',
    L'E',
    L'W',
    L'I',
    L'D',
    L'V'
};

void LogLock()
{
    EnterCriticalSection(&g_Lock);
}

void LogUnlock()
{
    LeaveCriticalSection(&g_Lock);
}

static DWORD GetCurrentModuleVersion(OUT DWORD *versionMajor, OUT DWORD *versionMinor)
{
    BYTE *versionBuffer = NULL;

    DWORD status = ERROR_OUTOFMEMORY;
    WCHAR* currentModulePath = malloc(MAX_PATH_LONG_WSIZE);
    if (!currentModulePath)
        goto cleanup;

    if (GetModuleFileName(NULL, currentModulePath, MAX_PATH_LONG) == 0)
    {
        status = GetLastError();
        goto cleanup;
    }

    DWORD cbVersion = GetFileVersionInfoSize(currentModulePath, NULL);
    if (cbVersion == 0)
        goto cleanup;

    versionBuffer = malloc(cbVersion);
    if (!versionBuffer)
    {
        status = ERROR_OUTOFMEMORY;
        goto cleanup;
    }

    if (!GetFileVersionInfo(currentModulePath, 0, cbVersion, versionBuffer))
    {
        status = GetLastError();
        goto cleanup;
    }

    VS_FIXEDFILEINFO* fileInfo = NULL;
    UINT cbFileInfo = 0;
    if (!VerQueryValue(versionBuffer, L"\\", &fileInfo, &cbFileInfo))
    {
        status = GetLastError();
        goto cleanup;
    }

    *versionMajor = fileInfo->dwFileVersionMS;
    *versionMinor = fileInfo->dwFileVersionLS;
    status = ERROR_SUCCESS;

cleanup:
    free(currentModulePath);
    free(versionBuffer);
    return status;
}

static void PurgeOldLogs(IN const WCHAR *logDir)
{
    // Read log retention time from registry (in seconds).
    LARGE_INTEGER logRetentionTime = { 0 };
    if (ERROR_SUCCESS != CfgReadDword(g_LogName, LOG_CONFIG_RETENTION_VALUE, &logRetentionTime.LowPart, NULL))
        logRetentionTime.QuadPart = LOG_DEFAULT_RETENTION_TIME;

    logRetentionTime.QuadPart *= 10000000ULL; // convert to 100ns units

    FILETIME ft;
    ULARGE_INTEGER* thresholdTime = (ULARGE_INTEGER*)&ft;

    GetSystemTimeAsFileTime(&ft);
    (*thresholdTime).QuadPart -= logRetentionTime.QuadPart;

    HANDLE findHandle = INVALID_HANDLE_VALUE;
    WCHAR* filePath = NULL;
    WCHAR* searchMask = malloc(MAX_PATH_LONG_WSIZE);
    if (!searchMask)
        goto end;

    filePath = malloc(MAX_PATH_LONG_WSIZE);
    if (!filePath)
        goto end;

    if (FAILED(StringCchPrintf(searchMask, MAX_PATH_LONG, L"%s\\%s*.*", logDir, g_LogName)))
        goto end;

    WIN32_FIND_DATA findData;
    findHandle = FindFirstFile(searchMask, &findData);
    if (findHandle == INVALID_HANDLE_VALUE)
        goto end;

    do
    {
        if ((*(ULARGE_INTEGER *) &findData.ftCreationTime).QuadPart < thresholdTime->QuadPart)
        {
            // File is too old, delete.
            if (FAILED(PathCchCombineEx(filePath, MAX_PATH_LONG, logDir, findData.cFileName, PATHCCH_ALLOW_LONG_PATHS)))
                goto end;
            DeleteFile(filePath);
        }
    } while (FindNextFile(findHandle, &findData));

end:
    free(filePath);
    free(searchMask);
    if (findHandle != INVALID_HANDLE_VALUE)
        FindClose(findHandle);
}

static WCHAR *LogGetName(void)
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
    DWORD logLevel;
    status = CfgReadDword(LogGetName(), LOG_CONFIG_LEVEL_VALUE, &logLevel, NULL);

    if (status != ERROR_SUCCESS)
        g_LogLevel = LOG_LEVEL_DEFAULT;
    else
        g_LogLevel = logLevel;
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
    if (g_LogLevel < 0)
        LogReadLevel();

    return g_LogLevel;
}

void LogInit(IN const WCHAR *logDir OPTIONAL, IN const WCHAR *logName)
{
    SYSTEMTIME st;
    DWORD len = 0;
    WCHAR *format = L"%s\\%s-%04d%02d%02d-%02d%02d%02d-%d.log";
    WCHAR systemPath[MAX_PATH]; // this should be fine unless for some reason Windows dir is in a weird location
    DWORD versionMajor = 0, versionMinor = 0;
    HANDLE token;
    DWORD sessionId;
    WCHAR* logPath = NULL;

    if (g_LogLevel < 0)
        g_LogLevel = LOG_LEVEL_DEFAULT;

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

        if (FAILED(StringCchCopy(systemPath + 3, RTL_NUMBER_OF(systemPath) - 3, LOG_DEFAULT_DIR L"\0")))
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
            win_perror("CreateDirectory");
            LogWarning("failed to create %s", logDir);
            goto fallback;
        }
    }

    PurgeOldLogs(logDir);

    logPath = malloc(MAX_PATH_LONG_WSIZE);
    if (!logPath)
    {
        LogStart(NULL);
        LogWarning("Out of memory");
        goto fallback;
    }

    if (FAILED(StringCchPrintf(logPath, MAX_PATH_LONG, format,
        logDir, g_LogName, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        GetCurrentProcessId()
        )))
    {
        LogStart(NULL);
        LogWarning("StringCchPrintf(full path) failed");
        goto fallback;
    }

    LogStart(logPath);

fallback:
    free(logPath);
    LogInfo("Log started, module name: %s", g_LogName);

    // system uptime (useful for performance testing)
    UINT64 uptime_ms = GetTickCount64();
    LogInfo("System uptime: %.3f seconds", uptime_ms / 1000.0f);

    // reuse systemPath
    len = ARRAYSIZE(systemPath); //UNLEN
    if (!GetUserName(systemPath, &len))
    {
        win_perror("GetUserName");
        LogInfo("Running as user: <UNKNOWN>, process ID: %d", GetCurrentProcessId());
    }
    else
    {
        LogInfo("Running as user: %s, process ID: %d", systemPath, GetCurrentProcessId());
    }

    // version
    if (ERROR_SUCCESS == GetCurrentModuleVersion(&versionMajor, &versionMinor))
    {
        LogInfo("Module version: %d.%d.%d.%d",
                (versionMajor >> 0x10) & 0xffff,
                (versionMajor >> 0x00) & 0xffff,
                (versionMinor >> 0x10) & 0xffff,
                (versionMinor >> 0x00) & 0xffff);
    }

    // session
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    GetTokenInformation(token, TokenSessionId, &sessionId, sizeof(sessionId), &len);
    CloseHandle(token);
    LogInfo("Session: %lu", sessionId);
    LogInfo("Command line: %s", GetOriginalCommandLine());
}

// Use the log directory from registry config.
// If logName is NULL, use current executable as name.
DWORD LogInitDefault(IN const WCHAR *logName OPTIONAL)
{
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

    DWORD status;
    WCHAR* logPath = malloc(MAX_PATH_LONG_WSIZE);
    if (!logPath)
    {
        // failed, use default location
        LogInit(NULL, logName);
        status = ERROR_OUTOFMEMORY;
        goto end;
    }

    status = CfgReadString(logName, LOG_CONFIG_PATH_VALUE, logPath, MAX_PATH_LONG, NULL);
    if (ERROR_SUCCESS != status)
    {
        LogInit(NULL, logName);
    }
    else
    {
        LogInit(logPath, logName);
    }

end:
    LogDebug("Verbosity level set to %d, safe flush: %d", g_LogLevel, g_SafeFlush);
    return status;
}

// create the log file
// if logfile_path is NULL, use stderr
void LogStart(IN const WCHAR *logfilePath OPTIONAL)
{
    BYTE utf8Bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD len, status = ERROR_SUCCESS;

    if (!g_LoggerInitialized)
    {
        InitializeCriticalSection(&g_Lock);

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
                fwprintf(stderr, L"LogStart: CreateFile(%s) failed: error %d\n", logfilePath, GetLastError());
                goto fallback;
            }

            // seek to end
            len = SetFilePointer(g_LogfileHandle, 0, 0, SEEK_END);
            if (INVALID_SET_FILE_POINTER == len)
            {
                status = GetLastError();
                fwprintf(stderr, L"LogStart: SetFilePointer(%s) failed: error %d\n", logfilePath, GetLastError());
                goto fallback;
            }

            if (len == 0) // fresh file - write BOM
            {
                if (!WriteFile(g_LogfileHandle, utf8Bom, 3, &len, NULL))
                {
                    status = GetLastError();
                    fwprintf(stderr, L"LogStart: WriteFile(%s) failed: error %d\n", logfilePath, GetLastError());
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

    if (g_LogfileHandle != INVALID_HANDLE_VALUE)
    {
        DWORD safeFlush;
        if (CfgReadDword(g_LogName, LOG_CONFIG_FLUSH_VALUE, &safeFlush, NULL) == ERROR_SUCCESS)
        {
            g_SafeFlush = (safeFlush != 0);
        }
        else
        {
            g_SafeFlush = FALSE;
        }
    }

    g_LoggerInitialized = TRUE;
}

void LogFlush(void)
{
    if (g_LoggerInitialized && g_LogfileHandle != INVALID_HANDLE_VALUE)
        FlushFileBuffers(g_LogfileHandle);
}

static void _LogFormatUnlocked(IN int level, IN BOOL raw, IN const char *functionName, IN const WCHAR *format, va_list args)
{
    size_t bufferSize = 0;
    DWORD written = 0;
    SYSTEMTIME st;
    WCHAR prefixBuffer[256];
    int prefixLen = 0;
    BOOL addNewline = FALSE;
    size_t prefixBufferSize = 0;
    BOOL echoToStderr = level <= LOG_LEVEL_WARNING;

#define PREFIX_FORMAT TEXT("[%04d%02d%02d.%02d%02d%02d.%03d-%d-%c] ")
#define PREFIX_FORMAT_FUNCNAME TEXT("%S: ")
    if (!raw)
    {
        ZeroMemory(prefixBuffer, sizeof(prefixBuffer));
        GetLocalTime(&st); // or system time (UTC)?
        prefixLen = swprintf_s(prefixBuffer, RTL_NUMBER_OF(prefixBuffer),
            functionName ? PREFIX_FORMAT PREFIX_FORMAT_FUNCNAME : PREFIX_FORMAT,
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            GetCurrentThreadId(), g_LogLevelChar[level], functionName);
    }

    // format buffer
    bufferSize = vswprintf_s(g_Buffer, LOG_MAX_MESSAGE_LENGTH, format, args) * sizeof(WCHAR);

    if (!raw)
    {
        if (ERROR_SUCCESS != ConvertUTF16ToUTF8(prefixBuffer, g_PrefixBufferUtf8, &prefixBufferSize))
        {
            fwprintf(stderr, L"_LogFormat: ConvertUTF16ToUTF8(prefix) failed: error %d\n", GetLastError());
            goto cleanup;
        }
    }

    if (ERROR_SUCCESS != ConvertUTF16ToUTF8(g_Buffer, g_BufferUtf8, &bufferSize))
    {
        fwprintf(stderr, L"_LogFormat: ConvertUTF16ToUTF8(buffer) failed: error %d\n", GetLastError());
        goto cleanup;
    }

    if (g_BufferUtf8[bufferSize - 1] != '\n')
        addNewline = TRUE;

    if (g_LogfileHandle != INVALID_HANDLE_VALUE)
    {
        if (!raw)
        {
            if (!WriteFile(g_LogfileHandle, g_PrefixBufferUtf8, (DWORD) prefixBufferSize, &written, NULL) || written != (DWORD) prefixBufferSize)
            {
                fwprintf(stderr, L"_LogFormat: WriteFile(prefix) failed: error %d\n", GetLastError());
                goto cleanup;
            }
        }

        // bufferSize is at most INT_MAX*2
        if (!WriteFile(g_LogfileHandle, g_BufferUtf8, (DWORD) bufferSize, &written, NULL) || written != (DWORD) bufferSize)
        {
            fwprintf(stderr, L"_LogFormat: WriteFile failed: error %d\n", GetLastError());
            goto cleanup;
        }

        if (addNewline && !raw)
        {
            if (!WriteFile(g_LogfileHandle, "\n", 1, &written, NULL) || written != 1)
            {
                fwprintf(stderr, L"_LogFormat: WriteFile failed: error %d\n", GetLastError());
                goto cleanup;
            }
        }

        if (echoToStderr)
        {
            if (!raw)
                fwprintf(stderr, prefixBuffer);

            fwprintf(stderr, g_Buffer);
            if (addNewline && !raw)
            {
                fwprintf(stderr, L"\n");
            }

#if defined(DEBUG) || defined(_DEBUG)
            OutputDebugString(g_Buffer);
            if (addNewline && !raw)
            {
                OutputDebugStringA("\n");
            }
#endif
        }
    }
    else // use stderr
    {
        if (!raw)
            fwprintf(stderr, L"%s", prefixBuffer);
        fwprintf(stderr, L"%s", g_Buffer);
        if (addNewline && !raw)
        {
            fwprintf(stderr, L"\n");
        }
    }

cleanup:

#ifdef LOG_SAFE_FLUSH
    LogFlush();
#endif
    return;
}

void _LogFormat(IN int level, IN BOOL raw, IN const char* functionName, IN const WCHAR* format, ...)
{
    DWORD lastError = GetLastError(); // preserve last error

    ErrRegisterUEF();

    // If we're initializing ad-hoc on a first log call, first read the log level
    // so we can tell if the log file should be created now.
    if (g_LogLevel < 0)
        LogReadLevel();

    if (level > g_LogLevel)
        goto end;

    if (!g_LoggerInitialized)
        LogInitDefault(NULL);

    if (!raw)
        EnterCriticalSection(&g_Lock);

    va_list args;
    va_start(args, format);
    _LogFormatUnlocked(level, raw, functionName, format, args);
    va_end(args);

    if (!raw)
        LeaveCriticalSection(&g_Lock);

end:
    SetLastError(lastError);
}

// Like _win_perror, but takes explicit error code. For cases when previous call doesn't set LastError.
DWORD _win_perror2(IN const char *functionName, IN DWORD errorCode, IN const WCHAR *prefix)
{
    SetLastError(errorCode);
    return _win_perror(functionName, prefix);
}

// Helper function to report errors. Similar to win_perror, but uses GetLastError() instead of errno.
DWORD _win_perror(IN const char *functionName, IN const WCHAR *prefix)
{
    size_t charCount;
    WCHAR *message = NULL;
    WCHAR buffer[2048];
    HRESULT ret;
    DWORD errorCode;

    errorCode = GetLastError();

    ZeroMemory(buffer, sizeof(buffer));
    charCount = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (WCHAR *) &message,
        0,
        NULL);

    if (!charCount)
    {
        if (FAILED(StringCchPrintf(buffer, RTL_NUMBER_OF(buffer), L" failed with error 0x%x\n", errorCode)))
            goto cleanup;
    }
    else
    {
        ret = StringCchPrintf(
            buffer,
            RTL_NUMBER_OF(buffer),
            L" failed with error 0x%x: %s%s",
            errorCode,
            message,
            ((charCount >= 1) && (0x0a == message[charCount - 1])) ? L"" : L"\n");
        LocalFree(message);

        if (FAILED(ret))
            goto cleanup;
    }

    _LogFormat(LOG_LEVEL_ERROR, FALSE, functionName, L"%s%s", prefix, buffer);
cleanup:
    SetLastError(errorCode); // preserve
    return errorCode;
}

// disabled if LOG_NO_HEX_DUMP is defined
void _hex_dump(IN const WCHAR *desc, IN const void *addr, IN int len)
{
    int i;
    WCHAR buff[17];
    BYTE *pc = (BYTE *) addr;

    if (len == 0)
        return;

    LogLock();
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
    LogUnlock();
}
