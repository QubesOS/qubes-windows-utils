#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

// To override CRT's perror definition.
#define _CRT_PERROR_DEFINED

// If LOG_SAFE_FLUSH is defined, the log file is flushed after every log call. (TODO: registry config)

// Underscore functions are meant for internal use.

// Registry config value: Log directory.
#define LOG_CONFIG_PATH_VALUE L"LogDir"

// Registry config value: Log level.
#define LOG_CONFIG_LEVEL_VALUE L"LogLevel"

// Registry config value: Log retention time (seconds).
#define LOG_CONFIG_RETENTION_VALUE L"LogRetention"

// Size of internal buffer in WCHARs.
#define LOG_MAX_MESSAGE_LENGTH 65536

// Logs older than this (seconds) will be deleted (default value - 7 days).
#define LOG_DEFAULT_RETENTION_TIME (7*24*60*60ULL)

// Default log directory (prepend "%SYSTEMDRIVE%\")
#define LOG_DEFAULT_DIR L"QubesLogs"

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
WINDOWSUTILS_API
DWORD LogInitDefault(IN const WCHAR *logName OPTIONAL);

// Formats unique log file path and calls LogStart.
// If logDir is NULL, use default log location.
WINDOWSUTILS_API
void LogInit(IN const WCHAR *logDir OPTIONAL, IN const WCHAR *logName);

// If logfilePath is NULL, use stderr.
WINDOWSUTILS_API
void LogStart(IN const WCHAR *logfilePath OPTIONAL);

// Explicitly set verbosity level.
WINDOWSUTILS_API
void LogSetLevel(IN int level);

// Get current verbosity level.
WINDOWSUTILS_API
int LogGetLevel(void);

WINDOWSUTILS_API
void _LogFormat(IN int level, IN BOOL raw, IN const char *functionName, IN const WCHAR *format, ...);
// *_raw functions omit the timestamp, function name prefix and don't append newlines automatically.

// Microsoft compilers define __FUNCTION__ as a string literal.
// This is better than the C standard which defines __func__ as a char[] variable,
// but we need this to compile with GCC...

// Helpers to not need to stick TEXT everywhere...
#define LogVerbose(format, ...)     _LogFormat(LOG_LEVEL_VERBOSE, FALSE, __FUNCTION__, L##format, ##__VA_ARGS__)
#define LogVerboseRaw(format, ...)  _LogFormat(LOG_LEVEL_VERBOSE,  TRUE,         NULL, L##format, ##__VA_ARGS__)

#define LogDebug(format, ...)       _LogFormat(LOG_LEVEL_DEBUG,   FALSE, __FUNCTION__, L##format, ##__VA_ARGS__)
#define LogDebugRaw(format, ...)    _LogFormat(LOG_LEVEL_DEBUG,    TRUE,         NULL, L##format, ##__VA_ARGS__)

#define LogInfo(format, ...)        _LogFormat(LOG_LEVEL_INFO,    FALSE, __FUNCTION__, L##format, ##__VA_ARGS__)
#define LogInfoRaw(format, ...)     _LogFormat(LOG_LEVEL_INFO,     TRUE,         NULL, L##format, ##__VA_ARGS__)

#define LogWarning(format, ...)     _LogFormat(LOG_LEVEL_WARNING, FALSE, __FUNCTION__, L##format, ##__VA_ARGS__)
#define LogWarningRaw(format, ...)  _LogFormat(LOG_LEVEL_WARNING,  TRUE,         NULL, L##format, ##__VA_ARGS__)

#define LogError(format, ...)       _LogFormat(LOG_LEVEL_ERROR,   FALSE, __FUNCTION__, L##format, ##__VA_ARGS__)
#define LogErrorRaw(format, ...)    _LogFormat(LOG_LEVEL_ERROR,    TRUE,         NULL, L##format, ##__VA_ARGS__)

// Returns last error code.
WINDOWSUTILS_API
DWORD _perror(IN const char *functionName, IN const WCHAR *prefix);
#define perror(prefix) _perror(__FUNCTION__, L##prefix)

WINDOWSUTILS_API
DWORD _perror2(IN const char *functionName, IN DWORD errorCode, IN const WCHAR *prefix);
#define perror2(error, prefix) _perror2(__FUNCTION__, error, L##prefix)

// hex_dump only logs if DEBUG is defined.
// You can define LOG_NO_HEX_DUMP to disable it even in DEBUG build (it can generate massive log files).
WINDOWSUTILS_API
void _hex_dump(IN const WCHAR *desc, IN const void *addr, IN int len);
#if (defined(DEBUG) || defined(_DEBUG)) && !defined(LOG_NO_HEX_DUMP)
#define hex_dump(desc, addr, len) _hex_dump(L##desc, addr, len)
#else
#define hex_dump(desc, addr, len)
#endif

// Flush pending data to the log file.
WINDOWSUTILS_API
void LogFlush(void);

#ifdef __cplusplus
}
#endif
