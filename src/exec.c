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

#include "exec.h"
#include "log.h"

#include <aclapi.h>
#include <lmcons.h>
#include <strsafe.h>
#include <userenv.h>
#include <wtsapi32.h>


static WCHAR* g_OriginalCommandLine = NULL;

HANDLE GetLoggedOnUserToken(
    OUT PWCHAR userName,
    IN  DWORD cchUserName // at least UNLEN WCHARs
    )
{
    DWORD consoleSessionId;
    HANDLE userToken, duplicateToken;

    consoleSessionId = WTSGetActiveConsoleSessionId();
    if (0xFFFFFFFF == consoleSessionId)
    {
        LogWarning("no active console session");
        return NULL;
    }

    if (!WTSQueryUserToken(consoleSessionId, &userToken))
    {
        LogDebug("no user is logged on");
        return NULL;
    }

    // create a primary token (needed for logon)
    if (!DuplicateTokenEx(
        userToken,
        MAXIMUM_ALLOWED,
        NULL,
        SecurityIdentification,
        TokenPrimary,
        &duplicateToken))
    {
        win_perror("DuplicateTokenEx");
        CloseHandle(userToken);
        return NULL;
    }

    CloseHandle(userToken);

    if (!ImpersonateLoggedOnUser(duplicateToken))
    {
        win_perror("ImpersonateLoggedOnUser");
        CloseHandle(duplicateToken);
        return NULL;
    }

    if (!GetUserName(userName, &cchUserName))
    {
        win_perror("GetUserName");
        userName[0] = 0;
    }

    RevertToSelf();

    return duplicateToken;
}

DWORD CreatePipedProcessAsCurrentUser(
    IN  WCHAR *commandLine, // non-const, CreateProcess can modify it
    IN  BOOL interactive,
    IN  HANDLE pipeStdin,
    IN  HANDLE pipeStdout,
    IN  HANDLE pipeStderr,
    OUT HANDLE *process
    )
{
    LogVerbose("cmd '%s', interactive %d", commandLine, interactive);
    return CreatePipedProcessAsUser(NULL, NULL, commandLine, interactive, pipeStdin, pipeStdout, pipeStderr, process);
}

DWORD CreatePipedProcessAsUser(
    IN const WCHAR *userName OPTIONAL, // use current if null
    IN const WCHAR *userPassword,
    IN WCHAR *commandLine, // non-const, CreateProcess can modify it
    IN BOOL interactive,
    IN HANDLE pipeStdin,
    IN HANDLE pipeStdout,
    IN HANDLE pipeStderr,
    OUT HANDLE *process
    )
{
    DWORD consoleSessionId;
    DWORD currentSessionId;
    DWORD status = ERROR_UNIDENTIFIED_ERROR;
    HANDLE userToken = NULL;
    PROCESS_INFORMATION pi = { 0 };
    STARTUPINFO si = { 0 };
    void *environment = NULL;
    WCHAR loggedUserName[UNLEN];
    BOOL inheritHandles;
    BOOL userIsLoggedOn = FALSE;

    if (!commandLine || !process)
        return ERROR_INVALID_PARAMETER;

    *process = INVALID_HANDLE_VALUE;
    LogDebug("user '%s', cmd '%s', interactive %d", userName, commandLine, interactive);

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSessionId))
        return win_perror("get current session id");

    consoleSessionId = WTSGetActiveConsoleSessionId();
    if (0xFFFFFFFF == consoleSessionId)
    {
        LogWarning("No active console session");
        return ERROR_NOT_SUPPORTED;
    }

    LogDebug("current session: %d, console session: %d", currentSessionId, consoleSessionId);

    if (!userName) // run as current user
    {
        HANDLE duplicateToken;

        if (!OpenProcessToken(GetCurrentProcess(), MAXIMUM_ALLOWED, &userToken))
            return win_perror("open current process token");

        // create a new primary token
        if (!DuplicateTokenEx(
            userToken,
            MAXIMUM_ALLOWED,
            NULL,
            SecurityIdentification,
            TokenPrimary,
            &duplicateToken))
        {
            status = win_perror("create new primary token for current user");
            CloseHandle(userToken);
            return status;
        }

        CloseHandle(userToken);
        userToken = duplicateToken;
        userIsLoggedOn = TRUE;
    }
    else
    {
        userToken = GetLoggedOnUserToken(loggedUserName, RTL_NUMBER_OF(loggedUserName));

        // Check if the logged on user is the same as the user specified by userName -
        // in that case we won't need to do LogonUser()
        if (userToken)
            LogDebug("logged on user name: '%s'", loggedUserName);

        if (!userToken || wcscmp(loggedUserName, userName))
        {
            if (userToken)
            {
                CloseHandle(userToken);
                userToken = NULL;
            }

            if (!LogonUser(
                userName,
                L".",
                userPassword,
                LOGON32_LOGON_INTERACTIVE,
                LOGON32_PROVIDER_DEFAULT,
                &userToken))
            {
                status = win_perror("LogonUser");
                goto cleanup;
            }
        }
        else
            userIsLoggedOn = TRUE;
    }
    if (!interactive)
        consoleSessionId = currentSessionId;

    if (!SetTokenInformation(userToken, TokenSessionId, &consoleSessionId, sizeof(consoleSessionId)))
    {
        status = win_perror("set token session id");
        goto cleanup;
    }

    if (!userIsLoggedOn)
    {
        // If the process is started using a newly created logon session,
        // this user must be granted access to the interactive desktop and window station.
        LogWarning("need to grant access to console session %d", consoleSessionId);
        return ERROR_NOT_SUPPORTED;
        /*
        status = GrantRemoteSessionDesktopAccess(consoleSessionId, userName, NULL);
        if (ERROR_SUCCESS != status)
        win_perror2(status, "GrantRemoteSessionDesktopAccess");*/
    }

    if (!CreateEnvironmentBlock(&environment, userToken, TRUE))
    {
        status = win_perror("CreateEnvironmentBlock");
        goto cleanup;
    }

    si.cb = sizeof(si);
    si.lpDesktop = TEXT("Winsta0\\Default");

    inheritHandles = FALSE;

    if (INVALID_HANDLE_VALUE != pipeStdin &&
        INVALID_HANDLE_VALUE != pipeStdout &&
        INVALID_HANDLE_VALUE != pipeStderr)
    {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = pipeStdin;
        si.hStdOutput = pipeStdout;
        si.hStdError = pipeStderr;

        inheritHandles = TRUE;
    }

    if (!CreateProcessAsUser(
        userToken,
        NULL,
        commandLine,
        NULL,
        NULL,
        inheritHandles, // inherit handles if IO is piped
        NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        environment,
        NULL,
        &si,
        &pi))
    {
        status = win_perror("CreateProcessAsUser");
        goto cleanup;
    }

    LogDebug("pid: %lu", pi.dwProcessId);

    *process = pi.hProcess;
    status = ERROR_SUCCESS;

cleanup:
    if (pi.hThread)
        CloseHandle(pi.hThread);
    if (userToken)
        CloseHandle(userToken);
    if (environment)
        DestroyEnvironmentBlock(environment);

    return status;
}

DWORD CreateNormalProcessAsUser(
    IN const WCHAR *userName,
    IN const WCHAR *userPassword,
    IN WCHAR *commandLine, // non-const, CreateProcess can modify it
    IN BOOL runInteractively,
    OUT HANDLE *process
    )
{
    DWORD status;

    status = CreatePipedProcessAsUser(
        userName,
        userPassword,
        commandLine,
        runInteractively,
        INVALID_HANDLE_VALUE,
        INVALID_HANDLE_VALUE,
        INVALID_HANDLE_VALUE,
        process);

    if (ERROR_SUCCESS != status)
        win_perror2(status, "CreatePipedProcessAsUser");

    return status;
}

DWORD CreateNormalProcessAsCurrentUser(
    IN WCHAR *commandLine, // non-const, CreateProcess can modify it
    OUT HANDLE *process
    )
{
    DWORD status;

    status = CreatePipedProcessAsCurrentUser(
        commandLine,
        TRUE,
        INVALID_HANDLE_VALUE,
        INVALID_HANDLE_VALUE,
        INVALID_HANDLE_VALUE,
        process);

    if (ERROR_SUCCESS != status)
        win_perror2(status, "CreatePipedProcessAsCurrentUser");

    return status;
}

PWSTR GetOriginalCommandLine(void)
{
    if (!g_OriginalCommandLine)
        g_OriginalCommandLine = _wcsdup(GetCommandLineW());

    return g_OriginalCommandLine;
}

PWSTR GetArgument(void)
{
    static PWCHAR cmd = NULL;
    static PWCHAR separator;

    if (!g_OriginalCommandLine)
        g_OriginalCommandLine = _wcsdup(GetCommandLineW());

    if (!cmd) // find the start of arguments
    {
        WCHAR searchChar;

        cmd = GetCommandLineW();
        // Executable path at the start may be enclosed in double quotes.
        if (*cmd == L'"')
            searchChar = L'"';
        else
            searchChar = L' ';

        for (cmd++; *cmd != searchChar; cmd++)
            ;

        cmd++;
        while (*cmd == L' ') // skip remaining spaces, sometimes there's more than one
            cmd++;

        separator = cmd;
    }

    if (!separator || *separator == 0) // no arguments left
        return NULL;

    cmd = separator;
    separator = wcschr(cmd, QUBES_ARGUMENT_SEPARATOR);
    if (separator)
    {
        *separator = 0;
        separator++; // move to the next argument
    }

    return cmd;
}

DWORD CreatePublicPipeSecurityDescriptor(
    OUT PSECURITY_DESCRIPTOR *securityDescriptor,
    OUT PACL *acl
    )
{
    DWORD status;
    SID *everyoneSid = NULL;
    EXPLICIT_ACCESS	ea[2] = { 0 };
    SID_IDENTIFIER_AUTHORITY sidAuthWorld = SECURITY_WORLD_SID_AUTHORITY;

    LogVerbose("start");

    if (!securityDescriptor || !acl)
        return ERROR_INVALID_PARAMETER;

    // Create a well-known SID for the Everyone group.
    if (!AllocateAndInitializeSid(
        &sidAuthWorld,
        1,
        SECURITY_WORLD_RID,
        0, 0, 0, 0, 0, 0, 0,
        &everyoneSid))
    {
        return win_perror("AllocateAndInitializeSid");
    }

    *acl = NULL;
    *securityDescriptor = NULL;

    // Initialize an EXPLICIT_ACCESS structure for an ACE.
    // The ACE will allow Everyone read/write access to the pipe.
    ea[0].grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_CREATE_PIPE_INSTANCE | SYNCHRONIZE;
    ea[0].grfAccessMode = SET_ACCESS;
    ea[0].grfInheritance = NO_INHERITANCE;
    ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[0].Trustee.ptstrName = (WCHAR *)everyoneSid;

    // Create a new ACL that contains the new ACE.
    status = SetEntriesInAcl(1, ea, NULL, acl);

    if (ERROR_SUCCESS != status)
    {
        win_perror2(status, "SetEntriesInAcl");
        goto cleanup;
    }

    // Initialize a security descriptor.
    *securityDescriptor = (SECURITY_DESCRIPTOR *)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (*securityDescriptor == NULL)
    {
        win_perror("LocalAlloc");
        goto cleanup;
    }

    if (!InitializeSecurityDescriptor(*securityDescriptor, SECURITY_DESCRIPTOR_REVISION))
    {
        status = win_perror("InitializeSecurityDescriptor");
        goto cleanup;
    }

    // Add the ACL to the security descriptor.
    if (!SetSecurityDescriptorDacl(*securityDescriptor, TRUE, *acl, FALSE))
    {
        status = win_perror("SetSecurityDescriptorDacl");
        goto cleanup;
    }

    status = ERROR_SUCCESS;

    LogVerbose("success");

cleanup:
    if (everyoneSid)
        FreeSid(everyoneSid);
    if (status != ERROR_SUCCESS && (*acl))
        LocalFree(*acl);
    if (status != ERROR_SUCCESS && (*securityDescriptor))
        LocalFree(*securityDescriptor);
    return status;
}

// enable specified privilege for the current process
DWORD EnablePrivilege(const WCHAR* privilegeName)
{
    DWORD status = ERROR_SUCCESS;
    HANDLE token = NULL;

    LogDebug("%s", privilegeName);
    TOKEN_PRIVILEGES* privileges = NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &token))
    {
        status = win_perror("get current process token");
        goto end;
    }

    LUID requestedPriv;
    if (!LookupPrivilegeValue(NULL, privilegeName, &requestedPriv))
    {
        status = win_perror("LookupPrivilegeValue");
        goto end;
    }

    DWORD size;
    GetTokenInformation(token, TokenPrivileges, NULL, 0, &size);
    status = GetLastError();
    if (status != ERROR_INSUFFICIENT_BUFFER)
        goto end;

    status = ERROR_OUTOFMEMORY;
    privileges = (TOKEN_PRIVILEGES*) malloc(size);
    if (!privileges)
        goto end;

    if (!GetTokenInformation(token, TokenPrivileges, privileges, size, &size))
    {
        status = win_perror("GetTokenInformation");
        goto end;
    }

    for (DWORD i = 0; i < privileges->PrivilegeCount; i++)
    {
        LONG high = privileges->Privileges[i].Luid.HighPart;
        DWORD low = privileges->Privileges[i].Luid.LowPart;
        DWORD mask = SE_PRIVILEGE_ENABLED_BY_DEFAULT | SE_PRIVILEGE_ENABLED;

        // check if the privilege is present in the token
        if (high == requestedPriv.HighPart && low == requestedPriv.LowPart)
        {
            if ((privileges->Privileges[i].Attributes & mask) != 0)
            {
                LogDebug("privilege %s is already enabled", privilegeName);
                status = ERROR_SUCCESS;
                goto end;
            }

            // we have the privilege but it's not enabled
            privileges->Privileges[i].Attributes |= SE_PRIVILEGE_ENABLED;
            if (AdjustTokenPrivileges(token, FALSE, privileges, 0, NULL, NULL))
            {
                LogDebug("privilege successfully enabled");
                status = ERROR_SUCCESS;
            }
            else
            {
                status = win_perror("enabling token privilege");
            }
            goto end;
        }
    }

    // we don't have the requested privilege at all
    // TODO: adding privilege to a token seems impossible
    // is it possible to create a fresh token without being a LSA provider?

    status = ERROR_PRIVILEGE_NOT_HELD;
end:
    free(privileges);
    if (token)
        CloseHandle(token);
    return status;
}

DWORD EnableUIAccess()
{
    HANDLE token = NULL;
    DWORD status;
    DWORD ui = 1;

    LogDebug("start");
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &token))
    {
        status = win_perror("get current process token");
        goto end;
    }

    if (SetTokenInformation(token, TokenUIAccess, &ui, sizeof(ui)))
        status = ERROR_SUCCESS;
    else
        status = win_perror("SetTokenInformation(TokenUIAccess");
end:
    if (token)
        CloseHandle(token);
    return status;
}
