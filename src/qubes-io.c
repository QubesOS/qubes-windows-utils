#include "qubes-io.h"
#include "log.h"

BOOL QioWriteBuffer(IN HANDLE file, IN const void *buffer, IN DWORD bufferSize)
{
    DWORD cbWrittenTotal = 0;
    DWORD cbWritten;

    while (cbWrittenTotal < bufferSize)
    {
        if (!WriteFile(file, (BYTE *) buffer + cbWrittenTotal, bufferSize - cbWrittenTotal, &cbWritten, NULL))
        {
            perror("WriteFile");
            return FALSE;
        }
        cbWrittenTotal += cbWritten;
    }
    return TRUE;
}

BOOL QioReadBuffer(IN HANDLE file, OUT void *buffer, IN DWORD bufferSize)
{
    DWORD cbReadTotal = 0;
    DWORD cbRead;

    while (cbReadTotal < bufferSize)
    {
        if (!ReadFile(file, (BYTE *)buffer + cbReadTotal, bufferSize - cbReadTotal, &cbRead, NULL))
        {
            perror("ReadFile");
            return FALSE;
        }

        if (cbRead == 0)
        {
            LogError("EOF");
            return FALSE;
        }

        cbReadTotal += cbRead;
    }
    return TRUE;
}

DWORD QioReadUntilEof(IN HANDLE file, OUT void *buffer, IN DWORD bufferSize)
{
    DWORD cbReadTotal = 0;
    DWORD cbRead;

    while (cbReadTotal < bufferSize)
    {
        if (!ReadFile(file, (BYTE *)buffer + cbReadTotal, bufferSize - cbReadTotal, &cbRead, NULL))
            return cbReadTotal;
        
        if (cbRead == 0)
            return cbReadTotal;
        
        cbReadTotal += cbRead;
    }
    return cbReadTotal;
}

BOOL QioCopyUntilEof(IN HANDLE output, IN HANDLE input)
{
    DWORD cb;
    BYTE buffer[4096];

    while (TRUE)
    {
        cb = QioReadUntilEof(input, buffer, sizeof(buffer));
        if (cb == 0)
            return TRUE;

        if (!QioWriteBuffer(output, buffer, cb))
            return FALSE;
    }
    return TRUE;
}
