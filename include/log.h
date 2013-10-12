#pragma once

// used if DEBUG is defined and client requests default log location in log_init
#define DEBUG_LOG_DIR "c:\\qubes\\logs"

// formats unique log file path and calls log_start
// if log_dir is NULL, use default log location (%APPDATA%\Qubes if !DEBUG)
void log_init(TCHAR *log_dir, TCHAR *base_name);

// if logfile_path is NULL, use stderr
void log_start(TCHAR *logfile_path);

void _logf(BOOL echo_to_stderr, TCHAR *format, ...);

// helper to not need to stick TEXT everywhere
#ifdef LOG_TO_STDERR
#define logf(format, ...) _logf(TRUE, TEXT(format), ##__VA_ARGS__)
#else
#define logf(format, ...) _logf(FALSE, TEXT(format), ##__VA_ARGS__)
#endif

// debugf never echoes to stderr (if logging to file)
#if defined(DEBUG) || defined(_DEBUG)
#define debugf(format, ...) _logf(FALSE, TEXT(format), ##__VA_ARGS__) 
#else
#define debugf(format, ...)
#endif

// errorf always echoes to stderr
#define errorf(format, ...) _logf(TRUE, TEXT(format), ##__VA_ARGS__) 

void _perror(TCHAR *prefix);
#define perror(prefix) _perror(TEXT(prefix))

void hex_dump (TCHAR *desc, void *addr, int len);
