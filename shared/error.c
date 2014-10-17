#include "error.h"
#include "log.h"

// Unhandled exception filter that logs the exception.
// Should work for most exceptions except stack overflows.
static LONG WINAPI ErrUEF(EXCEPTION_POINTERS *ep)
{
    DWORD i;

    LogError("Unhandled exception %08x at %p, flags: %x",
        ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress, ep->ExceptionRecord->ExceptionFlags);

    for (i = 0; i < ep->ExceptionRecord->NumberParameters; i++)
    {
        LogError("Parameter %d: %p", i, ep->ExceptionRecord->ExceptionInformation[i]);
    }

    // TODO: stack trace

    return EXCEPTION_EXECUTE_HANDLER; // terminate
}

DWORD ErrRegisterUEF(void)
{
    static BOOL registered = FALSE;

    if (registered)
        return ERROR_SUCCESS;

    registered = TRUE;
    SetUnhandledExceptionFilter(ErrUEF);
    LogDebug("done");
    return ERROR_SUCCESS;
}
