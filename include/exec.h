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

#pragma once
#include <windows.h>
#include <lmcons.h>
#include <aclapi.h>
#include <userenv.h>
#include <strsafe.h>
#include <Wtsapi32.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

// To insulate themselves from Windows' ambiguous path quoting rules,
// all Qubes Tools components expect command line arguments to be separated by this character.
#define QUBES_ARGUMENT_SEPARATOR L'|'

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

WINDOWSUTILS_API
DWORD GrantDesktopAccess(
    IN const WCHAR *accountName,
    IN const WCHAR *systemName
    );

// Open a window station and a desktop in another session, grant access to those handles
WINDOWSUTILS_API
DWORD GrantRemoteSessionDesktopAccess(
    IN DWORD sessionId,
    IN const WCHAR *accountName,
    IN WCHAR *systemName
    );

WINDOWSUTILS_API
DWORD CreatePipedProcessAsCurrentUser(
    IN WCHAR *commandLine, // non-const, CreateProcess can modify it
    IN  BOOL interactive,
    IN HANDLE pipeStdin,
    IN HANDLE pipeStdout,
    IN HANDLE pipeStderr,
    OUT HANDLE *process
    );

WINDOWSUTILS_API
DWORD CreatePipedProcessAsUser(
    IN const WCHAR *userName,
    IN const WCHAR *userPassword,
    IN WCHAR *commandLine, // non-const, CreateProcess can modify it
    IN  BOOL interactive,
    IN HANDLE pipeStdin,
    IN HANDLE pipeStdout,
    IN HANDLE pipeStderr,
    OUT HANDLE *process
    );

WINDOWSUTILS_API
DWORD CreateNormalProcessAsUser(
    IN const WCHAR *userName,
    IN const WCHAR *userPassword,
    IN WCHAR *commandLine, // non-const, CreateProcess can modify it
    IN BOOL runInteractively,
    OUT HANDLE *process
    );

WINDOWSUTILS_API
DWORD CreateNormalProcessAsCurrentUser(
    IN WCHAR *commandLine, // non-const, CreateProcess can modify it
    OUT HANDLE *process
    );

// Call repeatedly to get command line arguments sequentially.
WINDOWSUTILS_API
PWSTR GetArgument(void);

// Create a security descriptor that grants Everyone read/write access to a pipe.
// Caller must LocalFree both pointers.
WINDOWSUTILS_API
DWORD CreatePublicPipeSecurityDescriptor(
    OUT PSECURITY_DESCRIPTOR *securityDescriptor,
    OUT PACL *acl
    );

#ifdef __cplusplus
}
#endif
