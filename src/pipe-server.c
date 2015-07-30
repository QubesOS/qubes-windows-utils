#include <windows.h>
#include <stdlib.h>
#include <strsafe.h>

#include "buffer.h"
#include "log.h"
#include "qubes-io.h"
#include "list.h"

#include "pipe-server.h"

typedef struct _PIPE_CLIENT
{
    LIST_ENTRY ListEntry;
    DWORD Id;
    HANDLE WritePipe;
    HANDLE ReadPipe;
    CMQ_BUFFER *ReadBuffer;
    CMQ_BUFFER *WriteBuffer;
    BOOL Disconnecting;
    CRITICAL_SECTION Lock;
    HANDLE ReaderThread;
    HANDLE WriterThread;
    DWORD RefCount;
    PVOID UserData;
} PIPE_CLIENT, *PPIPE_CLIENT;

typedef struct _PIPE_SERVER
{
    WCHAR PipeName[256];
    DWORD PipeBufferSize;
    DWORD InternalBufferSize;
    DWORD WriteTimeout;
    PSECURITY_ATTRIBUTES SecurityAttributes;
    DWORD NumberClients;
    DWORD NextClientId;
    LIST_ENTRY Clients;
    BOOL AcceptConnections;
    CRITICAL_SECTION Lock;

    PVOID UserContext;

    QPS_CLIENT_CONNECTED ConnectCallback;
    QPS_CLIENT_DISCONNECTED DisconnectCallback;
    QPS_DATA_RECEIVED ReadCallback;
} *PIPE_SERVER;

// used for passing data to worker threads
struct THREAD_PARAM
{
    PIPE_SERVER Server;
    DWORD ClientId;
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
    IN  DWORD ClientId,
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

    InitializeListHead(&(*Server)->Clients);

    InitializeCriticalSection(&(*Server)->Lock);

    (*Server)->AcceptConnections = TRUE;

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
    DWORD *clientIds;
    int i;

    if (!Server)
        return;

    // get list of current clients
    EnterCriticalSection(&Server->Lock);
    Server->AcceptConnections = FALSE;
    clientIds = malloc(Server->NumberClients * sizeof(DWORD));

    i = 0;
    while (!IsListEmpty(&Server->Clients))
    {
        PPIPE_CLIENT client = CONTAINING_RECORD(Server->Clients.Flink, PIPE_CLIENT, ListEntry);
        clientIds[i++] = client->Id; // can't call disconnect here, would deadlock on QpsReleaseClient
    }
    LeaveCriticalSection(&Server->Lock);

    // disconnect all clients, need to be not holding the global lock
    while (i > 0)
    {
        --i;
        // if a client is disconnected in the meantime it's ok
        QpsDisconnectClientInternal(Server, clientIds[i], FALSE, FALSE);
    }

    DeleteCriticalSection(&Server->Lock);
    ZeroMemory(Server, sizeof(PIPE_SERVER));
    free(Server);

    LogVerbose("done");
}

static DWORD QpsAllocateClientId(
    IN  PIPE_SERVER Server
    )
{
    return ++Server->NextClientId;
}

static PPIPE_CLIENT QpsGetClient(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientId
    )
{
    PLIST_ENTRY entry;
    PPIPE_CLIENT returnClient = NULL;

    EnterCriticalSection(&Server->Lock);
    entry = Server->Clients.Flink;
    while (entry != &Server->Clients)
    {
        PPIPE_CLIENT client = (PPIPE_CLIENT)CONTAINING_RECORD(entry, PIPE_CLIENT, ListEntry);
        if (client->Id == ClientId)
        {
            returnClient = client;
            break;
        }

        entry = entry->Flink;
    }
    LeaveCriticalSection(&Server->Lock);

    if (returnClient)
    {
        InterlockedIncrement(&returnClient->RefCount);
        LogVerbose("[%lu] (%p) refs: %lu", returnClient->Id, returnClient, returnClient->RefCount);
    }
    else
    {
        LogVerbose("[%lu] not found", ClientId);
    }

    return returnClient;
}

// server or client lock must *NOT* be held
static void QpsReleaseClient(
    IN  PIPE_SERVER Server,
    IN  PPIPE_CLIENT Client
    )
{
    // it's possible that multiple threads hit the refcount==0 comparison simultaneously, locking is needed
    // can't use the client lock obviously since client's data is being freed
    // the global server lock isn't ideal but we would use it anyway to protect access to the client list...
    EnterCriticalSection(&Server->Lock);
    InterlockedDecrement(&Client->RefCount);
    LogVerbose("[%lu] (%p) refs: %lu", Client->Id, Client, Client->RefCount);

    if (Client->RefCount == 0)
    {
        // Free client's data.
        // This should only occur on disconnection as reader/writer threads always have a ref to client's data.
        LogDebug("[%lu] freeing client data %p", Client->Id, Client);
        CmqDestroy(Client->ReadBuffer);
        CmqDestroy(Client->WriteBuffer);

        DeleteCriticalSection(&Client->Lock);

        RemoveEntryList(&Client->ListEntry);

        ZeroMemory(Client, sizeof(PIPE_CLIENT));
        free(Client);

        InterlockedDecrement(&Server->NumberClients);
    }
    LeaveCriticalSection(&Server->Lock);
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
    PIPE_SERVER server = param->Server;
    PPIPE_CLIENT client = QpsGetClient(server, param->ClientId);
    HANDLE pipe = client->ReadPipe;
    PVOID buffer = malloc(server->PipeBufferSize);
    DWORD transferred;

    LogVerbose("[%lu] (%p) start", client->Id, client);
    if (!buffer)
    {
        LogError("no memory");
        QpsReleaseClient(server, client);
        QpsDisconnectClientInternal(server, param->ClientId, FALSE, TRUE);
        free(param);
        return 1;
    }

    // This thread endlessly tries to read from the client's read pipe.
    // Read data is enqueued into the internal buffer.
    while (TRUE)
    {
        LogVerbose("[%lu] reading...", client->Id);
        // reading will fail even if blocked when we call CancelIo() from QpsDisconnectClient
        if (!ReadFile(pipe, buffer, server->PipeBufferSize, &transferred, NULL)) // this can block
        {
            perror("ReadFile");
            LogWarning("[%lu] read failed", client->Id);
            // disconnect the client if the read failed because of other errors (broken pipe etc), it's harmless if we're already disconnecting
            QpsReleaseClient(server, client);
            QpsDisconnectClientInternal(server, param->ClientId, FALSE, TRUE);
            free(param);
            free(buffer);
            return 1;
        }
        // disconnect could happen after a successful read but before entering the client lock below
        if (client->Disconnecting)
        {
            LogWarning("[%lu] client is disconnecting, exiting", client->Id);
            QpsReleaseClient(server, client);
            free(param);
            free(buffer);
            return 1;
        }

        // we have some data from the pipe, add it to the read buffer
        EnterCriticalSection(&client->Lock);
        LogVerbose("[%lu] read %lu 0x%lx", client->Id, transferred, transferred);

        if (!CmqAddData(client->ReadBuffer, buffer, transferred))
        {
            // FIXME: block here until there's space
            LeaveCriticalSection(&client->Lock);
            LogError("[%lu] read buffer full", client->Id);
            QpsReleaseClient(server, client);
            QpsDisconnectClientInternal(server, param->ClientId, FALSE, TRUE);
            free(param);
            free(buffer);
            return 1;
        }
        LeaveCriticalSection(&client->Lock);

        if (param->Server->ReadCallback)
            param->Server->ReadCallback(param->Server, client->Id, buffer, transferred, param->Server->UserContext);
    }
}

static DWORD WINAPI QpsWriterThread(
    PVOID Param
    )
{
    struct THREAD_PARAM *param = Param;
    PIPE_SERVER server = param->Server;
    PPIPE_CLIENT client = QpsGetClient(server, param->ClientId);
    HANDLE pipe = client->WritePipe;
    PVOID data = malloc(server->InternalBufferSize);
    UINT64 size;

    if (!data)
    {
        LogError("no memory");
        QpsReleaseClient(server, client);
        QpsDisconnectClientInternal(server, param->ClientId, TRUE, FALSE);
        free(param);
        return 1;
    }

    // This thread endlessly tries to flush the client's write buffer to the client's write pipe.
    while (TRUE)
    {
        EnterCriticalSection(&client->Lock);
        // if the client is disconnecting, QpsWrite() calls will fail so no new data may be added to the write buffer
        if (client->Disconnecting)
        {
            LeaveCriticalSection(&client->Lock);
            LogWarning("[%lu] client is disconnecting, exiting", client->Id);
            QpsReleaseClient(server, client);
            free(param);
            free(data);
            return 1;
        }

        size = CmqGetUsedSize(client->WriteBuffer);
        if (size > 0)
        {
            // there's data to write
            if (!CmqGetData(client->WriteBuffer, data, &size, CMQ_NO_UNDERFLOW))
            { // shouldn't happen
                LeaveCriticalSection(&client->Lock);
                LogError("CmqGetData(client->WriteBuffer) failed");
                QpsReleaseClient(server, client);
                QpsDisconnectClientInternal(server, param->ClientId, TRUE, FALSE);
                free(param);
                free(data);
                return 1;
            }
            LeaveCriticalSection(&client->Lock);

            LogVerbose("[%lu] writing %lu 0x%lx", client->Id, size, size);
            // writing will fail even if blocked when we call CancelIo() from QpsDisconnectClient
            if (!QioWriteBuffer(pipe, data, (DWORD)size)) // this can block
            {
                perror("QioWriteBuffer");
                LogWarning("[%lu] write failed", client->Id);
                QpsReleaseClient(server, client);
                QpsDisconnectClientInternal(server, param->ClientId, TRUE, FALSE);
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
    struct THREAD_PARAM *param;
    PPIPE_CLIENT client;

    if (!Server->AcceptConnections)
        return ERROR_SHUTDOWN_IN_PROGRESS;

    client = malloc(sizeof(PIPE_CLIENT));
    ZeroMemory(client, sizeof(PIPE_CLIENT));

    EnterCriticalSection(&Server->Lock);
    client->Id = QpsAllocateClientId(Server);

    InitializeCriticalSection(&client->Lock);

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

    // start the reader thread
    param = malloc(sizeof(struct THREAD_PARAM));
    if (!param)
    {
        LeaveCriticalSection(&Server->Lock);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    param->Server = Server;
    param->ClientId = client->Id;
    client->ReaderThread = CreateThread(NULL, 0, QpsReaderThread, param, 0, NULL);
    if (!client->ReaderThread)
    {
        LeaveCriticalSection(&Server->Lock);
        return ERROR_NO_SYSTEM_RESOURCES;
    }

    // start the writer thread
    param = malloc(sizeof(struct THREAD_PARAM));
    if (!param)
    {
        LeaveCriticalSection(&Server->Lock);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    param->Server = Server;
    param->ClientId = client->Id;
    client->WriterThread = CreateThread(NULL, 0, QpsWriterThread, param, 0, NULL);
    if (!client->WriterThread)
    {
        LeaveCriticalSection(&Server->Lock);
        return ERROR_NO_SYSTEM_RESOURCES;
    }

    InsertTailList(&Server->Clients, &client->ListEntry);
    Server->NumberClients++;
    LogInfo("[%lu] (%p) connected (%lu total)", client->Id, client, Server->NumberClients);
    LeaveCriticalSection(&Server->Lock);

    if (Server->ConnectCallback)
        Server->ConnectCallback(Server, client->Id, Server->UserContext);

    return ERROR_SUCCESS;
}

void QpsDisconnectClient(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientId
    )
{
    QpsDisconnectClientInternal(Server, ClientId, FALSE, FALSE);
}

static void QpsDisconnectClientInternal(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientId,
    IN  BOOL WriterExiting,
    IN  BOOL ReaderExiting
    )
{
    PPIPE_CLIENT client = QpsGetClient(Server, ClientId);

    if (!client)
    {
        LogWarning("[%lu] not connected", ClientId);
        return;
    }

    if (client->Disconnecting)
    {
        QpsReleaseClient(Server, client);
        return;
    }

    client->Disconnecting = TRUE;
    LogInfo("[%lu] (%p) disconnecting", ClientId, client);

    if (!WriterExiting)
    {
        // wait for the writer thread to exit
        if (WaitForSingleObject(client->WriterThread, Server->WriteTimeout) != WAIT_OBJECT_0)
        {
            LogWarning("[%lu] writer thread didn't terminate in time, canceling write", ClientId);
            if (!CancelIo(client->WritePipe)) // this will abort a blocking operation
                perror("CancelIo(write)");
        }

        // wait for the writer thread cleanup
        if (WaitForSingleObject(client->WriterThread, 100) != WAIT_OBJECT_0)
        {
            LogWarning("[%lu] writer thread didn't terminate in time, killing it", ClientId);
            // this may leak memory or do other nasty things but should never happen
            TerminateThread(client->WriterThread, 0);
        }
    }
    CloseHandle(client->WriterThread);
    client->WriterThread = NULL;
    CloseHandle(client->WritePipe);
    client->WritePipe = NULL;

    if (!ReaderExiting)
    {
        // wait for the reader thread to exit
        if (!CancelIo(client->ReadPipe)) // this will abort a blocking operation
            perror("CancelIo(read)");

        if (WaitForSingleObject(client->ReaderThread, 100) != WAIT_OBJECT_0)
        {
            LogWarning("[%lu] reader thread didn't terminate in time, killing it", ClientId);
            // this may leak memory or do other nasty things but should never happen
            TerminateThread(client->ReaderThread, 0);
        }
    }
    CloseHandle(client->ReaderThread);
    client->ReaderThread = NULL;
    CloseHandle(client->ReadPipe);
    client->ReadPipe = NULL;

    // rest of the client's data will be destroyed by QpsReleaseClient when refcount drops to 0
    QpsReleaseClient(Server, client);

    LogInfo("[%lu] disconnected (%lu total)", ClientId, Server->NumberClients);

    if (Server->DisconnectCallback)
        Server->DisconnectCallback(Server, ClientId, Server->UserContext);
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
    IN  DWORD ClientId,
    OUT void *Data,
    IN  DWORD DataSize
    )
{
    UINT64 size;
    BOOL ret;
    PPIPE_CLIENT client = QpsGetClient(Server, ClientId);

    LogVerbose("[%lu] size %lu", ClientId, DataSize);
    if (!client)
        return ERROR_NOT_CONNECTED;

    // get data from the read buffer until the requested amount is read
    do
    {
        if (client->Disconnecting)
        {
            QpsReleaseClient(Server, client);
            LogWarning("[%lu] client is disconnected", ClientId);
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

    QpsReleaseClient(Server, client);
    return ERROR_SUCCESS;
}

DWORD QpsWrite(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientId,
    IN  const void *Data,
    IN  DWORD DataSize
    )
{
    PPIPE_CLIENT client = QpsGetClient(Server, ClientId);
    BOOL ret;

    LogVerbose("[%lu] size %lu", ClientId, DataSize);
    if (!client)
        return ERROR_NOT_CONNECTED;

    if (client->Disconnecting)
    {
        // pipe(s) broken, we won't be able to send anything
        QpsReleaseClient(Server, client);
        LogWarning("[%lu] client is disconnected", ClientId);
        return ERROR_BROKEN_PIPE;
    }

    // add data to the write queue
    // it will be flushed to the client pipe by the background writer thread
    EnterCriticalSection(&client->Lock);
    ret = CmqAddData(client->WriteBuffer, Data, DataSize);
    LeaveCriticalSection(&client->Lock);

    if (!ret)
    {
        QpsReleaseClient(Server, client);
        LogError("[%lu] write buffer full", ClientId);
        return ERROR_BUFFER_OVERFLOW;
    }

    QpsReleaseClient(Server, client);
    return ERROR_SUCCESS;
}

DWORD QpsGetReadBufferSize(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientId
    )
{
    DWORD queuedData;
    PPIPE_CLIENT client = QpsGetClient(Server, ClientId);

    LogVerbose("[%lu]", ClientId);
    if (!client)
        return ERROR_NOT_CONNECTED;

    EnterCriticalSection(&client->Lock);
    queuedData = (DWORD)CmqGetUsedSize(client->ReadBuffer);
    LeaveCriticalSection(&client->Lock);
    QpsReleaseClient(Server, client);
    return queuedData;
}

DWORD QpsSetClientData(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientId,
    IN  PVOID UserData
    )
{
    PPIPE_CLIENT client = QpsGetClient(Server, ClientId);

    LogVerbose("[%lu]", ClientId);
    if (!client)
        return ERROR_NOT_CONNECTED;

    EnterCriticalSection(&client->Lock);
    client->UserData = UserData;
    LeaveCriticalSection(&client->Lock);
    QpsReleaseClient(Server, client);
    return ERROR_SUCCESS;
}

PVOID QpsGetClientData(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientId
    )
{
    PPIPE_CLIENT client = QpsGetClient(Server, ClientId);
    PVOID data;

    LogVerbose("[%lu]", ClientId);
    if (!client)
        return NULL;

    EnterCriticalSection(&client->Lock);
    data = client->UserData;
    LeaveCriticalSection(&client->Lock);
    QpsReleaseClient(Server, client);
    return data;
}
