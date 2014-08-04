#pragma once
#include <windows.h>

// To override CRT's perror definition.
#define _CRT_PERROR_DEFINED

// If LOG_SAFE_FLUSH is defined, the log file is flushed after every log call. (TODO: registry config)

// Underscore functions are meant for internal use.

// Registry config value: Log directory.
#define LOG_CONFIG_PATH_VALUE TEXT("LogDir")

// Registry config value: Log level.
#define LOG_CONFIG_LEVEL_VALUE TEXT("LogLevel")

// Registry config value: Log retention time (seconds).
#define LOG_CONFIG_RETENTION_VALUE TEXT("LogRetention")

// Size of internal buffer in TCHARs.
#define LOG_MAX_MESSAGE_LENGTH 65536

// Logs older than this (seconds) will be deleted (default value - 7 days).
#define LOG_DEFAULT_RETENTION_TIME (7*24*60*60ULL)

// Default log directory (prepend "%SYSTEMDRIVE%\")
#define LOG_DEFAULT_DIR TEXT("QubesLogs")

// Verbosity levels.
enum
{
    LOG_LEVEL_ERROR = 0,    // non-continuable error resulting in process termination
    LOG_LEVEL_WARNING = 1,  // exceptional condition, but continuable
    LOG_LEVEL_INFO = 2,     // significant but not erroneous event
    LOG_LEVEL_DEBUG = 3,    // logging internal state
    LOG_LEVEL_VERBOSE = 4   // extremely detailed information: entry/exit of all functions etc.
} LOG_LEVEL;

#define LOG_LEVEL_MIN       LOG_LEVEL_ERROR
#define LOG_LEVEL_MAX       LOG_LEVEL_VERBOSE
#define LOG_LEVEL_DEFAULT   LOG_LEVEL_INFO

// Use the log directory and log level from registry config.
// If logName is NULL, use current executable as name.
DWORD LogInitDefault(const IN OPTIONAL TCHAR *logName);

// Formats unique log file path and calls LogStart.
// If logDir is NULL, use default log location.
void LogInit(const IN OPTIONAL TCHAR *logDir, const IN TCHAR *logName);

// If logfilePath is NULL, use stderr.
void LogStart(const IN OPTIONAL TCHAR *logfilePath);

// Explicitly set verbosity level.
void LogSetLevel(IN int level);

// Get current verbosity level.
int LogGetLevel(void);

void _LogFormat(IN int level, IN BOOL raw, const IN char *functionName, const IN TCHAR *format, ...);
// *_raw functions omit the timestamp, function name prefix and don't append newlines automatically.

// Microsoft compilers define __FUNCTION__ as a string literal.
// This is better than the C standard which defines __func__ as a char[] variable,
// but we need this to compile with GCC...

// Helpers to not need to stick TEXT everywhere...
#define LogVerbose(format, ...)     _LogFormat(LOG_LEVEL_VERBOSE, FALSE, __FUNCTION__, TEXT(format), ##__VA_ARGS__)
#define LogVerboseRaw(format, ...)  _LogFormat(LOG_LEVEL_VERBOSE,  TRUE,         NULL, TEXT(format), ##__VA_ARGS__)

#define LogDebug(format, ...)       _LogFormat(LOG_LEVEL_DEBUG,   FALSE, __FUNCTION__, TEXT(format), ##__VA_ARGS__)
#define LogDebugRaw(format, ...)    _LogFormat(LOG_LEVEL_DEBUG,    TRUE,         NULL, TEXT(format), ##__VA_ARGS__)

#define LogInfo(format, ...)        _LogFormat(LOG_LEVEL_INFO,    FALSE, __FUNCTION__, TEXT(format), ##__VA_ARGS__)
#define LogInfoRaw(format, ...)     _LogFormat(LOG_LEVEL_INFO,     TRUE,         NULL, TEXT(format), ##__VA_ARGS__)

#define LogWarning(format, ...)     _LogFormat(LOG_LEVEL_WARNING, FALSE, __FUNCTION__, TEXT(format), ##__VA_ARGS__)
#define LogWarningRaw(format, ...)  _LogFormat(LOG_LEVEL_WARNING,  TRUE,         NULL, TEXT(format), ##__VA_ARGS__)

#define LogError(format, ...)       _LogFormat(LOG_LEVEL_ERROR,   FALSE, __FUNCTION__, TEXT(format), ##__VA_ARGS__)
#define LogErrorRaw(format, ...)    _LogFormat(LOG_LEVEL_ERROR,    TRUE,         NULL, TEXT(format), ##__VA_ARGS__)

// Returns last error code.
DWORD _perror(const IN char *functionName, const IN TCHAR *prefix);
#define perror(prefix) _perror(__FUNCTION__, TEXT(prefix))

DWORD _perror2(const IN char *functionName, IN DWORD errorCode, const IN TCHAR *prefix);
#define perror2(error, prefix) _perror2(__FUNCTION__, error, TEXT(prefix))

// hex_dump only logs if DEBUG is defined.
// You can define LOG_NO_HEX_DUMP to disable it even in DEBUG build (it can generate massive log files).
void _hex_dump(const IN TCHAR *desc, const IN void *addr, IN int len);
#if (defined(DEBUG) || defined(_DEBUG)) && !defined(LOG_NO_HEX_DUMP)
#define hex_dump(desc, addr, len) _hex_dump(TEXT(desc), addr, len)
#else
#define hex_dump(desc, addr, len)
#endif

// Flush pending data to the log file.
void LogFlush(void);
