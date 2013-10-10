#pragma once

// if logfile_path is NULL, use stderr
void log_init(TCHAR *logfile_path);

void _logf(TCHAR *format, ...);
// helper to not need to stick TEXT everywhere
#define logf(format, ...) _logf(TEXT(format), ##__VA_ARGS__)

#if defined(DEBUG) || defined(_DEBUG)
#define debugf(format, ...) _logf(TEXT(format), ##__VA_ARGS__) 
#else
#define debugf(format, ...)
#endif

void _perror(TCHAR *prefix);
#define perror(prefix) _perror(TEXT(prefix))

void hex_dump (TCHAR *desc, void *addr, int len);
