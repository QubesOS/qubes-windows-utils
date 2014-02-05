#pragma once
#include <windows.h>

// If LOG_SAFE_FLUSH is defined, the log file is flushed after every log call.

// Underscore functions are meant for internal use.

// Used if DEBUG is defined and client requests default log location in log_init.
#define DEBUG_LOG_DIR "c:\\qubes\\logs"

// Size of internal buffer in TCHARs.
#define LOG_MAX_MESSAGE_LENGTH 65536

// Formats unique log file path and calls log_start.
// If log_dir is NULL, use default log location (%APPDATA%\Qubes if !DEBUG).
void log_init(TCHAR *log_dir, TCHAR *base_name);

// If logfile_path is NULL, use stderr.
void log_start(TCHAR *logfile_path);

void _logf(BOOL echo_to_stderr, BOOL raw, const char *function_name, TCHAR *format, ...);
// *_raw functions omit the timestamp, function name prefix and don't append newlines automatically.

// Microsoft compilers define __FUNCTION__ as a string literal.
// This is better than the C standard which defines __func__ as a char[] variable,
// but we need this to compile with GCC...

// Helper to not need to stick TEXT everywhere...
#ifdef LOG_TO_STDERR
#define logf(format, ...) _logf(TRUE, FALSE, __FUNCTION__, TEXT(format), ##__VA_ARGS__)
#define logf_raw(format, ...) _logf(TRUE, TRUE, NULL, TEXT(format), ##__VA_ARGS__)
#else
#define logf(format, ...) _logf(FALSE, FALSE, __FUNCTION__, TEXT(format), ##__VA_ARGS__)
#define logf_raw(format, ...) _logf(FALSE, TRUE, NULL, TEXT(format), ##__VA_ARGS__)
#endif

// debugf never echoes to stderr (if logging to file).
#if defined(DEBUG) || defined(_DEBUG)
#define debugf(format, ...) _logf(FALSE, FALSE, __FUNCTION__, TEXT(format), ##__VA_ARGS__)
#define debugf_raw(format, ...) _logf(FALSE, TRUE, NULL, TEXT(format), ##__VA_ARGS__)
#else
#define debugf(format, ...)
#define debugf_raw(format, ...)
#endif

// errorf always echoes to stderr.
#define errorf(format, ...) _logf(TRUE, FALSE, __FUNCTION__, TEXT(format), ##__VA_ARGS__)
#define errorf_raw(format, ...) _logf(TRUE, TRUE, NULL, TEXT(format), ##__VA_ARGS__)

// Returns last error code.
DWORD _perror(const char *function_name, TCHAR *prefix);
#define perror(prefix) _perror(__FUNCTION__, TEXT(prefix))

// hex_dump only logs if DEBUG is defined.
// You can define LOG_NO_HEX_DUMP to disable it even in DEBUG build (it can generate massive log files).
void _hex_dump (TCHAR *desc, void *addr, int len);
#if (defined(DEBUG) || defined(_DEBUG)) && !defined(LOG_NO_HEX_DUMP)
#define hex_dump(desc, addr, len) _hex_dump(TEXT(desc), addr, len)
#else
#define hex_dump(desc, addr, len)
#endif

// Flush pending data to the log file.
void log_flush();
