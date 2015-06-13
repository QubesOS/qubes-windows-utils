#include <windows.h>
#include <stdlib.h>
#include <strsafe.h>

#include "buffer.h"
#include "log.h"
#include "qubes-io.h"

#include "pipe-server.h"

typedef struct _PIPE_CLIENT
{
    HANDLE WritePipe;
    HANDLE ReadPipe;
    CMQ_BUFFER *ReadBuffer;
    CMQ_BUFFER *WriteBuffer;
    BOOL Disconnecting;
    CRITICAL_SECTION Lock;
    HANDLE ReaderThread;
    HANDLE WriterThread;
} PIPE_CLIENT, *PPIPE_CLIENT;

typedef struct _PIPE_SERVER
{
    WCHAR PipeName[256];
    DWORD PipeBufferSize;
    DWORD InternalBufferSize;
    DWORD WriteTimeout;
    PSECURITY_ATTRIBUTES SecurityAttributes;
    DWORD NumberClients;
    PIPE_CLIENT Clients[PS_MAX_CLIENTS];
    CRITICAL_SECTION Lock;

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
};

// Initialize data for a newly connected client.
DWORD QpsConnectClient(
    IN  PIPE_SERVER Server,
    IN  HANDLE WritePipe,
    IN  HANDLE ReadPipe
    );

// Disconnect can be called from inside worker threads, we don't want to
// wait for them in that case.
void QpsDisconnectClientInternal(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientIndex,
    IN  BOOL WriterExiting,
    IN  BOOL ReaderExiting
    );

// Create the server.
DWORD QpsCreate(
    IN  PWCHAR PipeName, // This is a client->server pipe name (clients write, server reads). server->client pipes have "-%PID%" appended.
    IN  DWORD PipeBufferSize, // Pipe read/write buffer size. Shouldn't be too big.
    IN  DWORD InternalBufferSize, // Internal read/write buffer (per client).
    IN  DWORD WriteTimeout, // If a client doesn't read written data in this amount of milliseconds, it's disconnected.
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
    (*Server)->InternalBufferSize = InternalBufferSize;
    (*Server)->WriteTimeout = WriteTimeout;
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
        if (Server->Clients[i].WritePipe == NULL)
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
    PPIPE_CLIENT client = &(param->Server->Clients[param->ClientIndex]);
    HANDLE pipe = client->ReadPipe;
    PVOID buffer = malloc(param->Server->PipeBufferSize);
    DWORD transferred;

    if (!buffer)
    {
        LogError("no memory");
        QpsDisconnectClientInternal(param->Server, param->ClientIndex, FALSE, TRUE);
        free(param);
        return 1;
    }

    // This thread endlessly tries to read from the client's read pipe.
    // Read data is enqueued into the internal buffer.
    while (TRUE)
    {
        LogVerbose("[%lu] reading...", param->ClientIndex);
        // reading will fail even if blocked when we call CancelIo() from QpsDisconnectClient
        if (!ReadFile(pipe, buffer, param->Server->PipeBufferSize, &transferred, NULL)) // this can block
        {
            perror("ReadFile");
            LogWarning("[%lu] read failed", param->ClientIndex);
            // disconnect the client if the read failed because of other errors (broken pipe etc), it's harmless if we're already disconnecting
            QpsDisconnectClientInternal(param->Server, param->ClientIndex, FALSE, TRUE);
            free(param);
            free(buffer);
            return 1;
        }

        // disconnect could happen after a successful read but before entering the client lock below
        if (client->Disconnecting)
        {
            LogWarning("[%lu] client is disconnecting, exiting", param->ClientIndex);
            free(param);
            free(buffer);
            return 1;
        }

        // we have some data from the pipe, add it to the read buffer
        EnterCriticalSection(&client->Lock);
        LogVerbose("[%lu] read %lu 0x%lx", param->ClientIndex, transferred, transferred);

        if (!CmqAddData(client->ReadBuffer, buffer, transferred))
        {
            // FIXME: block here until there's space
            LeaveCriticalSection(&client->Lock);
            LogError("[%lu] read buffer full", param->ClientIndex);
            QpsDisconnectClientInternal(param->Server, param->ClientIndex, FALSE, TRUE);
            free(param);
            free(buffer);
            return 1;
        }
        LeaveCriticalSection(&client->Lock);

        if (param->Server->ReadCallback)
            param->Server->ReadCallback(param->Server, param->ClientIndex, buffer, transferred, param->Server->UserContext);
    }
}

static DWORD WINAPI QpsWriterThread(
    PVOID Param
    )
{
    struct THREAD_PARAM *param = Param;
    PPIPE_CLIENT client = &(param->Server->Clients[param->ClientIndex]);
    HANDLE pipe = client->WritePipe;
    PVOID data = malloc(param->Server->InternalBufferSize);
    UINT64 size;

    if (!data)
    {
        LogError("no memory");
        QpsDisconnectClientInternal(param->Server, param->ClientIndex, TRUE, FALSE);
        free(param);
        return 1;
    }

    // This thread endlessly tries to flush the client's write buffer to the client's write pipe.
    while (TRUE)
    {
        // if the client is disconnecting, QpsWrite() calls will fail so no new data may be added to the write buffer
        if (client->Disconnecting)
        {
            LogWarning("[%lu] client is disconnecting, exiting", param->ClientIndex);
            free(param);
            free(data);
            return 1;
        }

        EnterCriticalSection(&client->Lock);
        size = CmqGetUsedSize(client->WriteBuffer);
        if (size > 0)
        {
            // there's data to write
            if (!CmqGetData(client->WriteBuffer, data, &size, CMQ_NO_UNDERFLOW))
            { // shouldn't happen
                LeaveCriticalSection(&client->Lock);
                LogError("CmqGetData(client->WriteBuffer) failed");
                QpsDisconnectClientInternal(param->Server, param->ClientIndex, TRUE, FALSE);
                free(param);
                free(data);
                return 1;
            }
            LeaveCriticalSection(&client->Lock);

            LogVerbose("[%lu] writing %lu 0x%lx", param->ClientIndex, size, size);
            // writing will fail even if blocked when we call CancelIo() from QpsDisconnectClient
            if (!QioWriteBuffer(pipe, data, (DWORD)size)) // this can block
            {
                perror("QioWriteBuffer");
                LogWarning("[%lu] write failed", param->ClientIndex);
                QpsDisconnectClientInternal(param->Server, param->ClientIndex, TRUE, FALSE);
                free(param);
                free(data);
                return 1;
            }
        }
        else
            LeaveCriticalSection(&client->Lock);

        if (size == 0)
            Sleep(1);
    }
}

static DWORD QpsConnectClient(
    IN  PIPE_SERVER Server,
    IN  HANDLE WritePipe,
    IN  HANDLE ReadPipe
    )
{
    DWORD index;
    struct THREAD_PARAM *param;
    PPIPE_CLIENT client;

    EnterCriticalSection(&Server->Lock);
    index = QpsAllocateIndex(Server);
    client = &(Server->Clients[index]);

    client->ReadBuffer = CmqCreate(Server->InternalBufferSize);
    if (client->ReadBuffer == NULL)
    {
        LeaveCriticalSection(&Server->Lock);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    client->WriteBuffer = CmqCreate(Server->InternalBufferSize);
    if (client->WriteBuffer == NULL)
    {
        LeaveCriticalSection(&Server->Lock);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    client->WritePipe = WritePipe;
    client->ReadPipe = ReadPipe;
    client->Disconnecting = FALSE;
    InitializeCriticalSection(&client->Lock);
    Server->NumberClients++;
    LeaveCriticalSection(&Server->Lock);

    // start the reader thread
    param = malloc(sizeof(struct THREAD_PARAM));
    if (!param)
        return ERROR_NOT_ENOUGH_MEMORY;
    param->Server = Server;
    param->ClientIndex = index;
    client->ReaderThread = CreateThread(NULL, 0, QpsReaderThread, param, 0, NULL);
    if (!client->ReaderThread)
        return ERROR_NO_SYSTEM_RESOURCES;

    // start the writer thread
    param = malloc(sizeof(struct THREAD_PARAM));
    if (!param)
        return ERROR_NOT_ENOUGH_MEMORY;
    param->Server = Server;
    param->ClientIndex = index;
    client->WriterThread = CreateThread(NULL, 0, QpsWriterThread, param, 0, NULL);
    if (!client->WriterThread)
        return ERROR_NO_SYSTEM_RESOURCES;

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
    QpsDisconnectClientInternal(Server, ClientIndex, FALSE, FALSE);
}

static void QpsDisconnectClientInternal(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientIndex,
    IN  BOOL WriterExiting,
    IN  BOOL ReaderExiting
    )
{
    PPIPE_CLIENT client = &(Server->Clients[ClientIndex]);

    if (client->Disconnecting)
        return;

    if (client->WritePipe == NULL)
        return;

    LogInfo("[%lu] disconnecting", ClientIndex);

    EnterCriticalSection(&Server->Lock);
    client->Disconnecting = TRUE;

    EnterCriticalSection(&client->Lock);

    if (!WriterExiting)
    {
        // wait for the writer thread to exit
        if (WaitForSingleObject(client->WriterThread, Server->WriteTimeout) != WAIT_OBJECT_0)
        {
            LogWarning("[%lu] writer thread didn't terminate in time, canceling write", ClientIndex);
            if (!CancelIo(client->WritePipe)) // this will abort a blocking operation
                perror("CancelIo(write)");
        }

        // wait for the writer thread cleanup
        if (WaitForSingleObject(client->WriterThread, 100) != WAIT_OBJECT_0)
        {
            LogWarning("[%lu] writer thread didn't terminate in time, killing it", ClientIndex);
            // this may leak memory or do other nasty things but should never happen
            TerminateThread(client->WriterThread, 0);
        }
    }
    CloseHandle(client->WriterThread);
    CloseHandle(client->WritePipe);

    if (!ReaderExiting)
    {
        // wait for the reader thread to exit
        if (!CancelIo(client->ReadPipe)) // this will abort a blocking operation
            perror("CancelIo(read)");

        if (WaitForSingleObject(client->ReaderThread, 100) != WAIT_OBJECT_0)
        {
            LogWarning("[%lu] reader thread didn't terminate in time, killing it", ClientIndex);
            // this may leak memory or do other nasty things but should never happen
            TerminateThread(client->ReaderThread, 0);
        }
    }
    CloseHandle(client->ReaderThread);
    CloseHandle(client->ReadPipe);

    // destroy rest of the client's data
    CmqDestroy(client->ReadBuffer);
    CmqDestroy(client->WriteBuffer);

    LeaveCriticalSection(&client->Lock);
    DeleteCriticalSection(&client->Lock);

    ZeroMemory(client, sizeof(PIPE_CLIENT));
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
    PPIPE_CLIENT client = &(Server->Clients[ClientIndex]);

    // get data from the read buffer until the requested amount is read
    do
    {
        if (!client->ReadPipe || client->Disconnecting)
        {
            LogWarning("[%lu] client is disconnected", ClientIndex);
            return ERROR_BROKEN_PIPE;
        }

        // get data from the read buffer if available
        size = DataSize;
        EnterCriticalSection(&client->Lock);
        ret = CmqGetData(client->ReadBuffer, Data, &size, CMQ_NO_UNDERFLOW);
        LeaveCriticalSection(&client->Lock);
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
    PPIPE_CLIENT client = &(Server->Clients[ClientIndex]);
    BOOL ret;

    if (!client->WritePipe || client->Disconnecting)
    {
        LogWarning("[%lu] client is disconnected", ClientIndex);
        return ERROR_BROKEN_PIPE;
    }

    // add data to the write queue
    // it will be flushed to the client pipe by the background writer thread
    EnterCriticalSection(&client->Lock);
    ret = CmqAddData(client->WriteBuffer, Data, DataSize);
    LeaveCriticalSection(&client->Lock);

    if (!ret)
    {
        LogError("[%lu] write buffer full", ClientIndex);
        return ERROR_BUFFER_OVERFLOW;
    }

    return ERROR_SUCCESS;
}

DWORD QpsGetReadBufferSize(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientIndex
    )
{
    DWORD queuedData;
    PPIPE_CLIENT client = &(Server->Clients[ClientIndex]);

    if (!client->ReadPipe)
        return ERROR_NOT_CONNECTED;

    EnterCriticalSection(&client->Lock);
    queuedData = (DWORD)CmqGetUsedSize(client->ReadBuffer);
    LeaveCriticalSection(&client->Lock);
    return queuedData;
}
