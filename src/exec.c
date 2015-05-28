#include "exec.h"
#include "log.h"

DWORD GetAccountSid(
    IN const WCHAR *accountName,
    IN const WCHAR *systemName,
    OUT SID **sid
    )
{
    SID_NAME_USE sidUsage;
    DWORD cbSid;
    DWORD cchReferencedDomainName = MAX_PATH;
    WCHAR referencedDomainName[MAX_PATH];
    DWORD status;

    if (!accountName || !sid)
        return ERROR_INVALID_PARAMETER;

    cbSid = 0;
    *sid = NULL;

    if (!LookupAccountName(
        systemName,
        accountName,
        NULL,
        &cbSid,
        referencedDomainName,
        &cchReferencedDomainName,
        &sidUsage))
    {
        status = GetLastError();
        if (ERROR_INSUFFICIENT_BUFFER != status)
        {
            return perror("LookupAccountName");
        }
    }

    *sid = LocalAlloc(LPTR, cbSid);
    if (*sid == NULL)
    {
        return perror("LocalAlloc");
    }

    if (!LookupAccountName(
        systemName,
        accountName,
        *sid,
        &cbSid,
        referencedDomainName,
        &cchReferencedDomainName,
        &sidUsage))
    {
        status = GetLastError();
        LocalFree(*sid);
        return perror2(status, "LookupAccountName");
    }

    return ERROR_SUCCESS;
}

DWORD GetObjectSecurityDescriptorDacl(
    IN HANDLE object,
    OUT SECURITY_DESCRIPTOR **sd,
    OUT BOOL *daclPresent,
    OUT ACL **dacl
    )
{
    DWORD status;
    SECURITY_INFORMATION si;
    DWORD sizeNeeded;
    BOOL daclDefaulted;

    if (!sd || !daclPresent || !dacl)
        return ERROR_INVALID_PARAMETER;

    si = DACL_SECURITY_INFORMATION;

    if (!GetUserObjectSecurity(
        object,
        &si,
        NULL,
        0,
        &sizeNeeded))
    {
        status = GetLastError();
        if (ERROR_INSUFFICIENT_BUFFER != status)
        {
            return perror("GetUserObjectSecurity");
        }
    }

    *sd = LocalAlloc(LPTR, sizeNeeded);
    if (*sd == NULL)
    {
        return perror("LocalAlloc");
    }

    if (!GetUserObjectSecurity(
        object,
        &si,
        *sd,
        sizeNeeded,
        &sizeNeeded))
    {
        return perror("GetUserObjectSecurity");
    }

    if (!GetSecurityDescriptorDacl(*sd, daclPresent, dacl, &daclDefaulted))
    {
        status = GetLastError();
        LocalFree(*sd);
        return perror2(status, "GetSecurityDescriptorDacl");
    }

    return ERROR_SUCCESS;
}

DWORD MergeWithExistingDacl(
    IN HANDLE object,
    IN ULONG countOfExplicitEntries,
    IN EXPLICIT_ACCESS *listOfExplicitEntries
    )
{
    DWORD status = ERROR_UNIDENTIFIED_ERROR;
    SECURITY_DESCRIPTOR	*sd = NULL;
    BOOL daclPresent = FALSE;
    ACL *dacl = NULL;
    ACL *newAcl = NULL;
    SECURITY_INFORMATION siRequested = DACL_SECURITY_INFORMATION;

    if (countOfExplicitEntries == 0 || listOfExplicitEntries == NULL)
        return ERROR_INVALID_PARAMETER;

    status = GetObjectSecurityDescriptorDacl(object, &sd, &daclPresent, &dacl);
    if (ERROR_SUCCESS != status)
    {
        perror("GetObjectSecurityDescriptorDacl");
        goto cleanup;
    }

    status = SetEntriesInAcl(countOfExplicitEntries, listOfExplicitEntries, dacl, &newAcl);

    if (ERROR_SUCCESS != status)
    {
        perror("SetEntriesInAcl");
        goto cleanup;
    }

    sd = LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (!sd)
    {
        status = perror("LocalAlloc");
        goto cleanup;
    }

    if (!InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION))
    {
        status = perror("InitializeSecurityDescriptor");
        goto cleanup;
    }

    if (!SetSecurityDescriptorDacl(sd, TRUE, newAcl, FALSE))
    {
        status = perror("SetSecurityDescriptorDacl");
        goto cleanup;
    }

    if (!SetUserObjectSecurity(object, &siRequested, sd))
    {
        status = perror("SetUserObjectSecurity");
        goto cleanup;
    }

    status = ERROR_SUCCESS;

cleanup:
    if (newAcl)
        LocalFree(newAcl);
    if (sd)
        LocalFree(sd);

    return status;
}

DWORD GrantDesktopAccess(
    IN const WCHAR *accountName,
    IN const WCHAR *systemName
    )
{
    HWINSTA originalWindowStation;
    HWINSTA windowStation = NULL;
    HDESK desktop = NULL;
    DWORD status = ERROR_UNIDENTIFIED_ERROR;
    SID *sid = NULL;
    EXPLICIT_ACCESS newEa[2];

    if (!accountName)
        return ERROR_INVALID_PARAMETER;

    originalWindowStation = GetProcessWindowStation();
    if (!originalWindowStation)
    {
        return perror("GetProcessWindowStation");
    }

    windowStation = OpenWindowStation(
        L"WinSta0",
        FALSE,
        READ_CONTROL | WRITE_DAC);

    if (!windowStation)
    {
        return perror("OpenWindowStation");
    }

    if (!SetProcessWindowStation(windowStation))
    {
        status = perror("SetProcessWindowStation");
        goto cleanup;
    }

    desktop = OpenDesktop(
        TEXT("Default"),
        0,
        FALSE,
        READ_CONTROL | WRITE_DAC | DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS);

    if (!desktop)
    {
        status = perror("OpenDesktop");
        goto cleanup;
    }

    if (!SetProcessWindowStation(originalWindowStation))
    {
        status = perror("SetProcessWindowStation(Original)");
        goto cleanup;
    }

    status = GetAccountSid(accountName, systemName, &sid);
    if (ERROR_SUCCESS != status)
    {
        perror2(status, "GetAccountSid");
        goto cleanup;
    }

    newEa[0].grfAccessPermissions = GENERIC_ACCESS;
    newEa[0].grfAccessMode = GRANT_ACCESS;
    newEa[0].grfInheritance = CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE | OBJECT_INHERIT_ACE;
    newEa[0].Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    newEa[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    newEa[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    newEa[0].Trustee.ptstrName = (WCHAR *) sid;

    newEa[1] = newEa[0];

    newEa[1].grfAccessPermissions = WINSTA_ALL;
    newEa[1].grfInheritance = NO_PROPAGATE_INHERIT_ACE;

    status = MergeWithExistingDacl(windowStation, 2, newEa);
    if (ERROR_SUCCESS != status)
    {
        perror2(status, "MergeWithExistingDacl(WindowStation)");
        goto cleanup;
    }

    newEa[0].grfAccessPermissions = DESKTOP_ALL;
    newEa[0].grfAccessMode = GRANT_ACCESS;
    newEa[0].grfInheritance = 0;

    status = MergeWithExistingDacl(desktop, 1, newEa);
    if (ERROR_SUCCESS != status)
    {
        perror2(status, "MergeWithExistingDacl(Desktop)");
        goto cleanup;
    }

cleanup:
    if (desktop)
        CloseDesktop(desktop);
    if (windowStation)
        CloseWindowStation(windowStation);
    if (sid)
        LocalFree(sid);
    return ERROR_SUCCESS;
}

// Open a window station and a desktop in another session, grant access to those handles
DWORD GrantRemoteSessionDesktopAccess(
    IN DWORD sessionId,
    IN const WCHAR *accountName,
    IN WCHAR *systemName
    )
{
    DWORD status = ERROR_UNIDENTIFIED_ERROR;
    HRESULT hresult;
    HANDLE token = NULL;
    HANDLE tokenDuplicate = NULL;
    WCHAR fullPath[MAX_PATH + 1] = { 0 };
    WCHAR arguments[UNLEN + 1];
    PROCESS_INFORMATION pi = { 0 };
    STARTUPINFO si = { 0 };
    DWORD currentSessionId;

    if (!accountName)
        return ERROR_INVALID_PARAMETER;

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSessionId))
    {
        return perror("ProcessIdToSessionId");
    }

    if (currentSessionId == sessionId)
    {
        // We're in the same session, no need to run an additional process.
        LogInfo("Already running in the specified session");
        status = GrantDesktopAccess(accountName, systemName);
        if (ERROR_SUCCESS != status)
            perror2(status, "GrantDesktopAccess");

        return status;
    }

    if (!OpenThreadToken(GetCurrentThread(), TOKEN_ALL_ACCESS, TRUE, &token))
    {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &token))
        {
            return perror("OpenProcessToken");
        }
    }

    if (!DuplicateTokenEx(
        token,
        MAXIMUM_ALLOWED,
        NULL,
        SecurityIdentification,
        TokenPrimary,
        &tokenDuplicate))
    {
        status = perror("DuplicateTokenEx");
        goto cleanup;
    }

    CloseHandle(token);
    token = tokenDuplicate;

    if (!SetTokenInformation(token, TokenSessionId, &sessionId, sizeof(sessionId)))
    {
        status = perror("SetTokenInformation");
        goto cleanup;
    }

    if (!GetModuleFileName(NULL, fullPath, RTL_NUMBER_OF(fullPath) - 1))
    {
        status = perror("GetModuleFileName");
        goto cleanup;
    }

    hresult = StringCchPrintf(arguments, RTL_NUMBER_OF(arguments), L"\"%s\" -a %s", fullPath, accountName);
    if (FAILED(hresult))
    {
        LogError("StringCchPrintf failed");
        goto cleanup;
    }

    si.cb = sizeof(si);

    LogDebug("CreateProcessAsUser(%s)", arguments);

    if (!CreateProcessAsUser(
        token,
        fullPath,
        arguments,
        NULL,
        NULL,
        TRUE, // handles are inherited
        0,
        NULL,
        NULL,
        &si,
        &pi))
    {
        status = perror("CreateProcessAsUser");
        goto cleanup;
    }

    status = WaitForSingleObject(pi.hProcess, 1000);

    if (WAIT_OBJECT_0 != status)
    {
        if (WAIT_TIMEOUT == status)
        {
            status = ERROR_ACCESS_DENIED;
            LogInfo("WaitForSingleObject timed out");
        }
        else
        {
            status = perror("WaitForSingleObject");
        }
    }

cleanup:
    if (pi.hThread)
        CloseHandle(pi.hThread);
    if (pi.hProcess)
        CloseHandle(pi.hProcess);
    if (token)
        CloseHandle(token);
    return status;
}

DWORD CreatePipedProcessAsCurrentUser(
    IN WCHAR *commandLine, // non-const, CreateProcess can modify it
    IN HANDLE pipeStdin,
    IN HANDLE pipeStdout,
    IN HANDLE pipeStderr,
    OUT HANDLE *process
    )
{
    PROCESS_INFORMATION pi = { 0 };
    STARTUPINFO si = { 0 };
    BOOL inheritHandles;

    if (!commandLine || !process)
        return ERROR_INVALID_PARAMETER;

    *process = INVALID_HANDLE_VALUE;

    LogDebug("%s", commandLine);

    si.cb = sizeof(si);

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

    if (!CreateProcess(
        NULL,
        commandLine,
        NULL,
        NULL,
        inheritHandles, // inherit handles if IO is piped
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi))
    {
        return perror("CreateProcess");
    }

    LogDebug("pid %lu", pi.dwProcessId);

    *process = pi.hProcess;
    CloseHandle(pi.hThread);

    return ERROR_SUCCESS;
}

DWORD CreatePipedProcessAsUser(
    IN const WCHAR *userName,
    IN const WCHAR *userPassword,
    IN WCHAR *commandLine, // non-const, CreateProcess can modify it
    IN BOOL runInteractively,
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
    HANDLE userTokenDuplicate = NULL;
    PROCESS_INFORMATION pi = { 0 };
    STARTUPINFO si = { 0 };
    void *environment = NULL;
    WCHAR loggedUserName[UNLEN + 1];
    DWORD nSize;
    BOOL inheritHandles;
    BOOL userIsLoggedOn;
    BOOL impersonating = FALSE;

    if (!userName || !commandLine || !process)
        return ERROR_INVALID_PARAMETER;

    *process = INVALID_HANDLE_VALUE;
    LogDebug("%s, %s", userName, commandLine);

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSessionId))
    {
        return perror("ProcessIdToSessionId");
    }

    consoleSessionId = WTSGetActiveConsoleSessionId();
    if (0xFFFFFFFF == consoleSessionId)
    {
        LogWarning("No active console session");
        return ERROR_NOT_SUPPORTED;
    }

    if (!WTSQueryUserToken(consoleSessionId, &userToken))
    {
        return perror("WTSQueryUserToken");
    }

    if (!DuplicateTokenEx(
        userToken,
        MAXIMUM_ALLOWED,
        NULL,
        SecurityIdentification,
        TokenPrimary,
        &userTokenDuplicate))
    {
        status = perror("DuplicateTokenEx");
        goto cleanup;
    }

    CloseHandle(userToken);
    userToken = userTokenDuplicate;

    // Check if the logged on user is the same as the user specified by pwszUserName -
    // in that case we won't need to do LogonUser()
    if (!ImpersonateLoggedOnUser(userToken))
    {
        status = perror("ImpersonateLoggedOnUser");
        goto cleanup;
    }

    impersonating = TRUE;

    nSize = RTL_NUMBER_OF(loggedUserName);
    if (!GetUserName(loggedUserName, &nSize))
    {
        status = perror("GetUserName");
        goto cleanup;
    }

    RevertToSelf();
    impersonating = FALSE;
    userIsLoggedOn = FALSE;

    if (wcscmp(loggedUserName, userName))
    {
        // Current user is not the one specified by userName. Log on the required user.

        CloseHandle(userToken);
        userToken = NULL;

        if (!LogonUser(
            userName,
            L".",
            userPassword,
            LOGON32_LOGON_INTERACTIVE,
            LOGON32_PROVIDER_DEFAULT,
            &userToken))
        {
            status = perror("LogonUser");
            goto cleanup;
        }
    }
    else
        userIsLoggedOn = TRUE;

    if (!runInteractively)
        consoleSessionId = currentSessionId;

    if (!(userIsLoggedOn && runInteractively))
    {
        // Do not do this if the specified user is currently logged on and the process is run interactively
        // because the user already has all the access to the window station and desktop, and
        // we don't have to change the session.
        if (!SetTokenInformation(userToken, TokenSessionId, &consoleSessionId, sizeof(consoleSessionId)))
        {
            status = perror("SetTokenInformation");
            goto cleanup;
        }

        status = GrantRemoteSessionDesktopAccess(consoleSessionId, userName, NULL);
        if (ERROR_SUCCESS != status)
            perror2(status, "GrantRemoteSessionDesktopAccess");
    }

    if (!CreateEnvironmentBlock(&environment, userToken, TRUE))
    {
        status = perror("CreateEnvironmentBlock");
        goto cleanup;
    }

    if (!ImpersonateLoggedOnUser(userToken))
    {
        status = perror("ImpersonateLoggedOnUser");
        goto cleanup;
    }

    impersonating = TRUE;
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
        status = perror("CreateProcessAsUser");
        goto cleanup;
    }

    LogDebug("pid: %lu", pi.dwProcessId);

    *process = pi.hProcess;
    status = ERROR_SUCCESS;

cleanup:
    if (impersonating)
        RevertToSelf();
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
        perror2(status, "CreatePipedProcessAsUser");

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
        INVALID_HANDLE_VALUE,
        INVALID_HANDLE_VALUE,
        INVALID_HANDLE_VALUE,
        process);

    if (ERROR_SUCCESS != status)
        perror2(status, "CreatePipedProcessAsCurrentUser");

    return status;
}
