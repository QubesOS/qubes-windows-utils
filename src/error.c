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

#ifdef _DEBUG
    DebugBreak();
#endif

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
