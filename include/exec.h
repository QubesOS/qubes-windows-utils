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
    IN const WCHAR *userName OPTIONAL,
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

// Call repeatedly to get command line arguments sequentially. Modifies process' command line.
WINDOWSUTILS_API
PWSTR GetArgument(void);

// Get the original process command line.
WINDOWSUTILS_API
PWSTR GetOriginalCommandLine(void);

// Create a security descriptor that grants Everyone read/write access to a pipe.
// Caller must LocalFree both pointers.
WINDOWSUTILS_API
DWORD CreatePublicPipeSecurityDescriptor(
    OUT PSECURITY_DESCRIPTOR *securityDescriptor,
    OUT PACL *acl
    );

// enable specified privilege for the current process
WINDOWSUTILS_API
DWORD EnablePrivilege(const WCHAR* privilegeName);

// enable UI access pseudo-privilege in for the current process
// requires SeTcbPrivilege (SYSTEM/service)
WINDOWSUTILS_API
DWORD EnableUIAccess();

#ifdef __cplusplus
}
#endif
