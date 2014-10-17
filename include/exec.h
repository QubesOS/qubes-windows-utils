#pragma once
#include <windows.h>
#include <lmcons.h>
#include <aclapi.h>
#include <userenv.h>
#include <strsafe.h>
#include <Wtsapi32.h>

#define DESKTOP_ALL (DESKTOP_READOBJECTS | DESKTOP_CREATEWINDOW | \
    DESKTOP_CREATEMENU | DESKTOP_HOOKCONTROL | DESKTOP_JOURNALRECORD | \
    DESKTOP_JOURNALPLAYBACK | DESKTOP_ENUMERATE | DESKTOP_WRITEOBJECTS | \
    DESKTOP_SWITCHDESKTOP | STANDARD_RIGHTS_REQUIRED)

#define WINSTA_ALL (WINSTA_ENUMDESKTOPS | WINSTA_READATTRIBUTES | \
    WINSTA_ACCESSCLIPBOARD | WINSTA_CREATEDESKTOP | \
    WINSTA_WRITEATTRIBUTES | WINSTA_ACCESSGLOBALATOMS | \
    WINSTA_EXITWINDOWS | WINSTA_ENUMERATE | WINSTA_READSCREEN | \
    STANDARD_RIGHTS_REQUIRED)

#define GENERIC_ACCESS (GENERIC_READ | GENERIC_WRITE | \
    GENERIC_EXECUTE | GENERIC_ALL)

DWORD GrantDesktopAccess(
    IN const WCHAR *accountName,
    IN const WCHAR *systemName
    );

// Open a window station and a desktop in another session, grant access to those handles
DWORD GrantRemoteSessionDesktopAccess(
    IN DWORD sessionId,
    IN const WCHAR *accountName,
    IN WCHAR *systemName
    );

DWORD CreatePipedProcessAsCurrentUser(
    IN WCHAR *commandLine, // non-const, CreateProcess can modify it
    IN HANDLE pipeStdin,
    IN HANDLE pipeStdout,
    IN HANDLE pipeStderr,
    OUT HANDLE *process
    );

DWORD CreatePipedProcessAsUser(
    IN const WCHAR *userName,
    IN const WCHAR *userPassword,
    IN WCHAR *commandLine, // non-const, CreateProcess can modify it
    IN BOOL runInteractively,
    IN HANDLE pipeStdin,
    IN HANDLE pipeStdout,
    IN HANDLE pipeStderr,
    OUT HANDLE *process
    );

DWORD CreateNormalProcessAsUser(
    IN const WCHAR *userName,
    IN const WCHAR *userPassword,
    IN WCHAR *commandLine, // non-const, CreateProcess can modify it
    IN BOOL runInteractively,
    OUT HANDLE *process
    );

DWORD CreateNormalProcessAsCurrentUser(
    IN WCHAR *commandLine, // non-const, CreateProcess can modify it
    OUT HANDLE *process
    );
