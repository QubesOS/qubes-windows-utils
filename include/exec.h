#pragma once
#include <tchar.h>
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


ULONG CreatePipedProcessAsUser(
		TCHAR *pwszUserName,
		TCHAR *pwszUserPassword,
		TCHAR *pwszCommand,
		BOOL bRunInteractively,
		HANDLE hPipeStdin,
		HANDLE hPipeStdout,
		HANDLE hPipeStderr,
		HANDLE *phProcess
);

ULONG CreateNormalProcessAsUser(
		TCHAR *pwszUserName,
		TCHAR *pwszUserPassword,
		TCHAR *pwszCommand,
		BOOL bRunInteractively,
		HANDLE *phProcess
);

ULONG CreatePipedProcessAsCurrentUser(
		TCHAR *pwszCommand,
		HANDLE hPipeStdin,
		HANDLE hPipeStdout,
		HANDLE hPipeStderr,
		HANDLE *phProcess
);

ULONG CreateNormalProcessAsCurrentUser(
		TCHAR *pwszCommand,
		HANDLE *phProcess
);

ULONG GrantDesktopAccess(
	TCHAR *pszAccountName,
	TCHAR *pszSystemName
);
