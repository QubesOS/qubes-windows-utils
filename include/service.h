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

// Parameter passed to the service worker thread.
typedef struct _SERVICE_WORKER_CONTEXT
{
    HANDLE StopEvent;  // The thread should terminate when this event is signaled.
    PVOID UserContext; // User context data passed to ServiceMainLoop as workerContext.
} SERVICE_WORKER_CONTEXT, *PSERVICE_WORKER_CONTEXT;

// Starts the service processing loop.
// Doesn't return until the service is stopped.
WINDOWSUTILS_API
DWORD SvcMainLoop(
    IN const WCHAR *serviceName, // service name
    IN DWORD acceptedControlCodes, // SERVICE_ACCEPT_*
    IN LPTHREAD_START_ROUTINE workerFunction, // function started as a worker thread
    IN void *workerContext, // parameter for the worker thread
    IN LPHANDLER_FUNCTION_EX handlerFunction OPTIONAL, // handler for service notifications
    IN void *handlerContext OPTIONAL // parameter for the notification handler
    );

// Creates a service as autostart, own process, run as LocalSystem.
WINDOWSUTILS_API
DWORD SvcCreate(
    IN const WCHAR *serviceName,
    IN const WCHAR *displayName OPTIONAL,
    IN const WCHAR *executablePath
    );

// Deletes a service.
WINDOWSUTILS_API
DWORD SvcDelete(
    IN const WCHAR *serviceName
    );

#ifdef __cplusplus
}
#endif
