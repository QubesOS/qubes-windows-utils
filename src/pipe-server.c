#include <windows.h>
#include <stdlib.h>
#include <strsafe.h>

#include "buffer.h"
#include "log.h"
#include "qubes-io.h"

#include "pipe-server.h"

typedef struct _PIPE_SERVER
{
    WCHAR PipeName[256];
    DWORD PipeBufferSize;
    DWORD ReadBufferSize;
    PSECURITY_ATTRIBUTES SecurityAttributes;
    DWORD NumberClients;
    HANDLE WritePipes[PS_MAX_CLIENTS];
    HANDLE ReadPipes[PS_MAX_CLIENTS];
    CRITICAL_SECTION Lock;
    CMQ_BUFFER *Buffers[PS_MAX_CLIENTS];

    PVOID UserContext;

    QPS_CLIENT_CONNECTED ConnectCallback;
    QPS_CLIENT_DISCONNECTED DisconnectCallback;
    QPS_DATA_RECEIVED ReadCallback;
} *PIPE_SERVER;

// used for passing data to worker threads
struct THREAD_PARAM
{
    DWORD ClientIndex;
    PIPE_SERVER Server;
    PVOID Data;
    DWORD DataSize;
};

// Initialize data for a newly connected client.
DWORD QpsConnectClient(
    IN  PIPE_SERVER Server,
    IN  HANDLE WritePipe,
    IN  HANDLE ReadPipe
    );

// Create the server.
DWORD QpsCreate(
    IN  PWCHAR PipeName, // This is a client->server pipe name (clients write, server reads). server->client pipes have "-%PID%" appended.
    IN  DWORD PipeBufferSize, // Pipe read/write buffer size. Shouldn't be too big.
    IN  DWORD ReadBufferSize, // Read buffer (per client). The server enqueues all received data here until it's read by QpsRead().
    IN  QPS_CLIENT_CONNECTED ConnectCallback, // "Client connected" callback.
    IN  QPS_CLIENT_DISCONNECTED DisconnectCallback OPTIONAL, // "Client disconnected" callback.
    IN  QPS_DATA_RECEIVED ReadCallback OPTIONAL, // "Data received" callback.
    IN  PVOID Context OPTIONAL, // Opaque parameter that will be passed to callbacks.
    IN  PSECURITY_ATTRIBUTES SecurityAttributes OPTIONAL, // Must be valid for the whole lifetime of the server.
    OUT PIPE_SERVER *Server // Server object.
    )
{
    DWORD Status = ERROR_NOT_ENOUGH_MEMORY;

    *Server = malloc(sizeof(struct _PIPE_SERVER));
    if (*Server == NULL)
        goto cleanup;

    ZeroMemory(*Server, sizeof(struct _PIPE_SERVER));

    StringCbCopyW((*Server)->PipeName, sizeof((*Server)->PipeName), PipeName);
    (*Server)->PipeBufferSize = PipeBufferSize;
    (*Server)->ReadBufferSize = ReadBufferSize;
    (*Server)->SecurityAttributes = SecurityAttributes;

    (*Server)->ConnectCallback = ConnectCallback;
    (*Server)->DisconnectCallback = DisconnectCallback;
    (*Server)->ReadCallback = ReadCallback;
    (*Server)->UserContext = Context;

    InitializeCriticalSection(&(*Server)->Lock);

    Status = ERROR_SUCCESS;

cleanup:
    if (Status != ERROR_SUCCESS)
    {
        QpsDestroy(*Server);
        *Server = NULL;
    }
    return Status;
}

void QpsDestroy(
    PIPE_SERVER Server
    )
{
    if (Server != NULL)
    {
        for (DWORD i = 0; i < PS_MAX_CLIENTS; i++)
            QpsDisconnectClient(Server, i); // safe if not connected
        DeleteCriticalSection(&Server->Lock);
        free(Server);
    }
}

static DWORD QpsAllocateIndex(
    IN  PIPE_SERVER Server
    )
{
    for (DWORD i = 0; i < PS_MAX_CLIENTS; i++)
    {
        if (Server->WritePipes[i] == NULL)
            return i;
    }
    return ~0;
}

/*
MSDN says:
The ReadFile function returns when one of the following conditions occur:
- The number of bytes requested is read.
- A write operation completes on the write end of the pipe.
- An asynchronous handle is being used and the read is occurring asynchronously.
- An error occurs.

That means we can do ReadFile synchronously with a large buffer and not block until
the whole buffer is read. The read will return once *something* is written to the other end.
*/
static DWORD WINAPI QpsReaderThread(
    PVOID Param
    )
{
    struct THREAD_PARAM *param = Param;
    HANDLE pipe = param->Server->ReadPipes[param->ClientIndex];
    PVOID buffer = malloc(param->Server->PipeBufferSize);
    DWORD transferred;

    if (!buffer)
    {
        QpsDisconnectClient(param->Server, param->ClientIndex);
        free(param);
        return 1;
    }

    // This thread endlessly tries to read from the client's read pipe.
    // Read data is enqueued into the internal buffer.
    while (TRUE)
    {
        LogVerbose("[%lu] reading...", param->ClientIndex);
        if (!ReadFile(pipe, buffer, param->Server->PipeBufferSize, &transferred, NULL))
        {
            LogWarning("[%lu] read failed: %d %x", param->ClientIndex, GetLastError(), GetLastError());
            QpsDisconnectClient(param->Server, param->ClientIndex);
            free(param);
            free(buffer);
            return 1;
        }

        LogVerbose("[%lu] read %lu 0x%lx", param->ClientIndex, transferred, transferred);
        if (param->Server->ReadCallback)
            param->Server->ReadCallback(param->Server, param->ClientIndex, buffer, transferred, param->Server->UserContext);

        EnterCriticalSection(&param->Server->Lock);
        if (!CmqAddData(param->Server->Buffers[param->ClientIndex], buffer, transferred))
        {
            // FIXME: block here until there's space
            LeaveCriticalSection(&param->Server->Lock);
            LogError("[%lu] read buffer full", param->ClientIndex);
            QpsDisconnectClient(param->Server, param->ClientIndex);
            free(param);
            free(buffer);
            return 1;
        }
        LeaveCriticalSection(&param->Server->Lock);
    }
}

static DWORD WINAPI QpsWriterThread(
    PVOID Param
    )
{
    struct THREAD_PARAM *param = Param;
    HANDLE pipe = param->Server->WritePipes[param->ClientIndex];

    LogVerbose("[%lu] writing %lu 0x%lx", param->ClientIndex, param->DataSize, param->DataSize);
    EnterCriticalSection(&param->Server->Lock);
    if (!QioWriteBuffer(pipe, param->Data, param->DataSize))
    {
        LeaveCriticalSection(&param->Server->Lock);
        LogWarning("[%lu] write failed", param->ClientIndex);
        QpsDisconnectClient(param->Server, param->ClientIndex);
    }
    else
        LeaveCriticalSection(&param->Server->Lock);

    free(param->Data);
    free(param);
    return 1;
}

static DWORD QpsConnectClient(
    IN  PIPE_SERVER Server,
    IN  HANDLE WritePipe,
    IN  HANDLE ReadPipe
    )
{
    DWORD index;
    HANDLE thread;
    struct THREAD_PARAM *param;

    EnterCriticalSection(&Server->Lock);
    index = QpsAllocateIndex(Server);

    Server->Buffers[index] = CmqCreate(Server->ReadBufferSize);
    if (Server->Buffers[index] == NULL)
    {
        LeaveCriticalSection(&Server->Lock);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    Server->WritePipes[index] = WritePipe;
    Server->ReadPipes[index] = ReadPipe;
    Server->NumberClients++;
    LeaveCriticalSection(&Server->Lock);

    // start the reader thread
    param = malloc(sizeof(struct THREAD_PARAM));
    if (!param)
        return ERROR_NOT_ENOUGH_MEMORY;
    param->Server = Server;
    param->ClientIndex = index;
    thread = CreateThread(NULL, 0, QpsReaderThread, param, 0, NULL);
    if (!thread)
        return ERROR_NO_SYSTEM_RESOURCES;

    CloseHandle(thread);

    LogInfo("[%lu] connected", index);
    if (Server->ConnectCallback)
        Server->ConnectCallback(Server, index, Server->UserContext);
    return ERROR_SUCCESS;
}

void QpsDisconnectClient(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientIndex
    )
{
    if (Server->WritePipes[ClientIndex] == NULL)
        return;

    LogInfo("[%lu] disconnecting", ClientIndex);
    EnterCriticalSection(&Server->Lock);
    CancelIo(Server->WritePipes[ClientIndex]);
    CancelIo(Server->ReadPipes[ClientIndex]);
    // reader thread will terminate when the pipe's io is canceled/handle closed
    CloseHandle(Server->WritePipes[ClientIndex]);
    CloseHandle(Server->ReadPipes[ClientIndex]);
    Server->WritePipes[ClientIndex] = NULL;
    Server->ReadPipes[ClientIndex] = NULL;
    CmqDestroy(Server->Buffers[ClientIndex]);
    Server->Buffers[ClientIndex] = NULL;
    Server->NumberClients--;
    LeaveCriticalSection(&Server->Lock);

    if (Server->DisconnectCallback)
        Server->DisconnectCallback(Server, ClientIndex, Server->UserContext);
}

// Returns only on error. At that point the server state is undefined, call QpsDestroy().
DWORD QpsMainLoop(
    PIPE_SERVER Server
    )
{
    BOOL connected;
    HANDLE writePipe, readPipe;
    DWORD status;
    WCHAR pipeName[256];
    ULONG pid;

    while (TRUE)
    {
        if (Server->NumberClients == PS_MAX_CLIENTS)
        {
            LogVerbose("Too many clients, waiting for disconnect");
            Sleep(1000);
            continue;
        }

        writePipe = CreateNamedPipe(Server->PipeName,
                                    PIPE_ACCESS_OUTBOUND,
                                    PIPE_TYPE_BYTE | PIPE_WAIT,
                                    PIPE_UNLIMITED_INSTANCES,
                                    Server->PipeBufferSize,
                                    Server->PipeBufferSize,
                                    0,
                                    Server->SecurityAttributes);

        // wait for connection
        do
        {
            connected = ConnectNamedPipe(writePipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (!connected)
                Sleep(10);
        } while (!connected);

        // prepare the read pipe
        if (!GetNamedPipeClientProcessId(writePipe, &pid))
        {
            return perror("GetNamedPipeClientProcessId");
        }

        StringCbPrintf(pipeName, sizeof(pipeName), L"%s-%d", Server->PipeName, pid);
        readPipe = CreateNamedPipe(pipeName,
                                   PIPE_ACCESS_INBOUND,
                                   PIPE_TYPE_BYTE | PIPE_WAIT,
                                   PIPE_UNLIMITED_INSTANCES,
                                   Server->PipeBufferSize,
                                   Server->PipeBufferSize,
                                   0,
                                   Server->SecurityAttributes);

        do
        {
            connected = ConnectNamedPipe(readPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (!connected)
                Sleep(10);
        } while (!connected);

        // initialize the client
        status = QpsConnectClient(Server, writePipe, readPipe);
        if (status != ERROR_SUCCESS)
            return status;
    }
}

DWORD QpsRead(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientIndex,
    OUT void *Data,
    IN  DWORD DataSize
    )
{
    UINT64 size;
    BOOL ret;

    // get data from the read buffer until the requested amount is read
    do
    {
        // we need to check if the client is connected to access its buffer
        // FIXME: this is atomic in practice but should be replaced with some connected flag and an interlocked check
        EnterCriticalSection(&Server->Lock);
        if (!Server->WritePipes[ClientIndex] || !Server->Buffers[ClientIndex])
        {
            LeaveCriticalSection(&Server->Lock);
            LogWarning("[%d] client is disconnected", ClientIndex);
            return ERROR_BROKEN_PIPE;
        }

        // get data from the read buffer if available
        size = DataSize;
        ret = CmqGetData(Server->Buffers[ClientIndex], Data, &size, CMQ_NO_UNDERFLOW);
        LeaveCriticalSection(&Server->Lock);
        if (!ret)
            Sleep(1); // don't congest the lock
    } while (!ret);

    return ERROR_SUCCESS;
}

DWORD QpsWrite(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientIndex,
    IN  const void *Data,
    IN  DWORD DataSize
    )
{
    struct THREAD_PARAM *param;
    HANDLE thread;

    if (!Server->WritePipes[ClientIndex])
        return ERROR_NOT_CONNECTED;

    // create a thread for writing in case it blocks
    param = malloc(sizeof(struct THREAD_PARAM));
    if (!param)
        return ERROR_NOT_ENOUGH_MEMORY;

    param->Server = Server;
    param->ClientIndex = ClientIndex;
    // Ideally we wouldn't copy the data but this call is nonblocking.
    // This way we don't require that the caller keeps this buffer valid
    // for an indeterminate amount of time (no completion notification).
    param->Data = malloc(DataSize);
    if (!param->Data)
    {
        free(param);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    memcpy(param->Data, Data, DataSize);
    param->DataSize = DataSize;

    thread = CreateThread(NULL, 0, QpsWriterThread, param, 0, NULL);
    if (!thread)
    {
        free(param->Data);
        free(param);
        return ERROR_NO_SYSTEM_RESOURCES;
    }
    CloseHandle(thread);

    return ERROR_SUCCESS;
}

DWORD QpsGetReadBufferSize(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientIndex
    )
{
    DWORD queuedData;

    if (!Server->WritePipes[ClientIndex])
        return ERROR_NOT_CONNECTED;

    EnterCriticalSection(&Server->Lock);
    queuedData = (DWORD)CmqGetUsedSize(Server->Buffers[ClientIndex]);
    LeaveCriticalSection(&Server->Lock);
    return queuedData;
}
