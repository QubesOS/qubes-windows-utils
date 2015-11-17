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

#include "service.h"
#include "log.h"

// service data
typedef struct _SERVICE_CONTEXT
{
    WCHAR *Name;
    HANDLE StopEvent;
    HANDLE WorkerThread;
    SERVICE_STATUS Status;
    SERVICE_STATUS_HANDLE StatusHandle;
    LPHANDLER_FUNCTION_EX HandlerFunction;
    LPTHREAD_START_ROUTINE WorkerFunction;
    SERVICE_WORKER_CONTEXT WorkerContext;
    DWORD AcceptedControlCodes;
    void *HandlerContext;
} SERVICE_CONTEXT, *PSERVICE_CONTEXT;

// Global service data. We can have only one if we're starting ServiceMain from here
// since ServiceMain doesn't take any context parameter.
PSERVICE_CONTEXT g_Service = NULL;

void SvcStop(void);
void WINAPI SvcMain(DWORD argc, WCHAR *argv[]);

static void SvcSetState(
    IN DWORD state,
    IN DWORD win32ExitCode
    )
{
    LogVerbose("start");

    LogDebug("state: %lu", state);
    g_Service->Status.dwCurrentState = state;

    if (SERVICE_RUNNING != state)
        g_Service->Status.dwControlsAccepted = 0;
    else
        g_Service->Status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | g_Service->AcceptedControlCodes;

    if (!SetServiceStatus(g_Service->StatusHandle, &g_Service->Status))
        perror("SetServiceStatus");
}

DWORD SvcMainLoop(
    IN const WCHAR *serviceName,
    IN DWORD acceptedControlCodes,
    IN LPTHREAD_START_ROUTINE workerFunction,
    IN void *workerContext,
    IN LPHANDLER_FUNCTION_EX handlerFunction OPTIONAL,
    IN void *handlerContext OPTIONAL
    )
{
    SERVICE_TABLE_ENTRY	serviceTable[2];

    if (g_Service)
        return ERROR_ALREADY_INITIALIZED;

    g_Service = malloc(sizeof(*g_Service));
    if (!g_Service)
        return ERROR_OUTOFMEMORY;

    ZeroMemory(g_Service, sizeof(*g_Service));
    g_Service->Status.dwWaitHint = 0;
    g_Service->Status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_Service->Name = _wcsdup(serviceName);
    g_Service->AcceptedControlCodes = acceptedControlCodes;
    g_Service->HandlerFunction = handlerFunction;
    g_Service->HandlerContext = handlerContext;
    g_Service->WorkerFunction = workerFunction;
    g_Service->WorkerContext.UserContext = workerContext;

    serviceTable[0].lpServiceName = g_Service->Name;
    serviceTable[0].lpServiceProc = SvcMain;
    serviceTable[1].lpServiceName = NULL;
    serviceTable[1].lpServiceProc = NULL;

    LogDebug("entering dispatcher loop");
    if (!StartServiceCtrlDispatcher(serviceTable))
        return perror("StartServiceCtrlDispatcher");

    LogDebug("exiting");
    return ERROR_SUCCESS;
}

static void SvcStop(
    )
{
    LogDebug("stopping");
    SvcSetState(SERVICE_STOP_PENDING, NO_ERROR);

    SetEvent(g_Service->StopEvent);

    // SvcMain waits for the worker thread to exit and completes shutdown
}

// Default notification handler.
// Handles stop/shutdown, all other codes are passed to the user callback.
static DWORD WINAPI SvcCtrlHandlerEx(
    IN DWORD controlCode,
    IN DWORD eventType,
    IN void *eventData,
    IN void *context
    )
{
    DWORD status;

    LogDebug("code: %lu 0x%lx", controlCode, controlCode);

    switch (controlCode)
    {
    case SERVICE_CONTROL_SHUTDOWN:
    case SERVICE_CONTROL_STOP:
        SvcStop();
        break;

    default:
        if (!g_Service->HandlerFunction)
            break;

        status = g_Service->HandlerFunction(controlCode, eventType, eventData, context);
        if (NO_ERROR != status)
        {
            perror2(status, "user notification handler");
            SvcStop();
        }
        break;
    }

    return NO_ERROR;
}

static void WINAPI SvcMain(DWORD argc, WCHAR *argv[])
{
    DWORD status;

    LogVerbose("start");

    g_Service->StatusHandle = RegisterServiceCtrlHandlerEx(g_Service->Name, SvcCtrlHandlerEx, g_Service);
    if (!g_Service->StatusHandle)
    {
        perror("RegisterServiceCtrlHandlerEx");
        return;
    }

    // initialization
    SvcSetState(SERVICE_START_PENDING, NO_ERROR);

    g_Service->StopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_Service->StopEvent)
    {
        status = perror("Create stop event");
        SvcSetState(SERVICE_STOPPED, status);
        return;
    }

    g_Service->WorkerContext.StopEvent = g_Service->StopEvent;
    g_Service->WorkerThread = CreateThread(NULL, 0, g_Service->WorkerFunction, &g_Service->WorkerContext, 0, NULL);
    if (!g_Service->WorkerThread)
    {
        status = perror("Create worker thread");
        CloseHandle(g_Service->StopEvent);
        SvcSetState(SERVICE_STOPPED, status);
        return;
    }

    // ready to receive notifications
    SvcSetState(SERVICE_RUNNING, NO_ERROR);

    LogDebug("Waiting for the worker thread to exit");
    WaitForSingleObject(g_Service->WorkerThread, INFINITE);

    SvcSetState(SERVICE_STOPPED, NO_ERROR);

    LogDebug("cleanup");
    CloseHandle(g_Service->WorkerThread);
    CloseHandle(g_Service->StopEvent);

    free(g_Service);
    g_Service = NULL;

    LogVerbose("success");
}

DWORD SvcCreate(
    IN const WCHAR *serviceName,
    IN const WCHAR *displayName OPTIONAL,
    IN const WCHAR *executablePath
    )
{
    SC_HANDLE scm = NULL;
    SC_HANDLE service = NULL;
    DWORD status;

    status = ERROR_INVALID_PARAMETER;
    if (!executablePath || !serviceName)
        goto cleanup;

    LogInfo("name '%s', path '%s'", serviceName, executablePath);

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
    {
        status = perror("OpenSCManager");
        goto cleanup;
    }

    service = CreateService(
        scm,
        serviceName,
        displayName ? displayName : serviceName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        executablePath,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL);

    if (!service)
    {
        status = perror("CreateService");
        goto cleanup;
    }

    status = ERROR_SUCCESS;
    LogVerbose("success");

cleanup:
    if (service)
        CloseServiceHandle(service);
    if (scm)
        CloseServiceHandle(scm);

    return status;
}

DWORD SvcDelete(
    IN const WCHAR *serviceName
    )
{
    SC_HANDLE service = NULL;
    SC_HANDLE scm = NULL;
    DWORD status;

    if (!serviceName)
        return ERROR_INVALID_PARAMETER;

    LogInfo("name '%s'", serviceName);

    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
    {
        status = perror("OpenSCManager");
        goto cleanup;
    }

    service = OpenService(scm, serviceName, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (!service)
    {
        status = perror("OpenService");
        goto cleanup;
    }

    if (!DeleteService(service))
    {
        status = perror("DeleteService");
        goto cleanup;
    }

    status = ERROR_SUCCESS;
    LogVerbose("success");

cleanup:
    if (service)
        CloseServiceHandle(service);
    if (scm)
        CloseServiceHandle(scm);

    return status;
}
