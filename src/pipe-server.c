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
    LONGLONG Id;
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
    LONGLONG NumberClients;
    LONGLONG NextClientId;
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
    LONGLONG ClientId;
};

// Initialize data for a newly connected client.
static DWORD QpsConnectClient(
    IN  PIPE_SERVER Server,
    IN  LONGLONG ClientId,
    IN  HANDLE WritePipe,
    IN  HANDLE ReadPipe
    );

// Disconnect can be called from inside worker threads, we don't want to
// wait for them in that case.
static void QpsDisconnectClientInternal(
    IN  PIPE_SERVER Server,
    IN  LONGLONG ClientId,
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

    LogVerbose("pipe name '%s'", PipeName);
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
    LONGLONG *clientIds;
    LONGLONG i;

    if (!Server)
        return;

    // get list of current clients
    EnterCriticalSection(&Server->Lock);
    Server->AcceptConnections = FALSE;
    clientIds = malloc(Server->NumberClients * sizeof(clientIds[0]));

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
    free(clientIds);

    LogVerbose("done");
}

static LONGLONG QpsAllocateClientId(
    IN  PIPE_SERVER Server
    )
{
    return InterlockedIncrement64(&Server->NextClientId);
}

// Get client's data by ID and optionally increase the client's refcount.
// DO NOT USE unless you know EXACTLY why you want to NOT increase the refcount.
static PPIPE_CLIENT QpsGetClientRaw(
    IN  PIPE_SERVER Server,
    IN  LONGLONG ClientId,
    IN  BOOL IncreaseRefcount
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
        if (IncreaseRefcount)
            InterlockedIncrement(&returnClient->RefCount);
        LogVerbose("[%lld] (%p) refs: %lu", returnClient->Id, returnClient, returnClient->RefCount);
    }
    else
    {
        LogVerbose("[%lld] not found", ClientId);
    }

    return returnClient;
}

// Use this function to access client's data by ID. It increases the client's refcount.
// Call QpsReleaseClient after you're done touching the client's data.
static PPIPE_CLIENT QpsGetClient(
    IN  PIPE_SERVER Server,
    IN  LONGLONG ClientId
    )
{
    return QpsGetClientRaw(Server, ClientId, TRUE);
}

// Release the client (decreases the client's refcount).
// Server or client lock must *NOT* be held.
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
    LogVerbose("[%lld] (%p) refs: %lu", Client->Id, Client, Client->RefCount);

    if (Client->RefCount == 0)
    {
        // Free client's data.
        // This should only occur on disconnection as reader/writer threads always have a ref to client's data.
        LogDebug("[%lld] freeing client data %p", Client->Id, Client);
        CmqDestroy(Client->ReadBuffer);
        CmqDestroy(Client->WriteBuffer);

        DeleteCriticalSection(&Client->Lock);

        RemoveEntryList(&Client->ListEntry);

        ZeroMemory(Client, sizeof(PIPE_CLIENT));
        free(Client);

        InterlockedDecrement64(&Server->NumberClients);
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

    LogVerbose("[%lld] (%p) start", client->Id, client);
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
        LogVerbose("[%lld] reading...", client->Id);
        // reading will fail even if blocked when we call CancelIo() from QpsDisconnectClient
        if (!ReadFile(pipe, buffer, server->PipeBufferSize, &transferred, NULL)) // this can block
        {
            // win_perror("ReadFile");
            LogWarning("[%lld] read failed", client->Id);
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
            LogWarning("[%lld] client is disconnecting, exiting", client->Id);
            QpsReleaseClient(server, client);
            free(param);
            free(buffer);
            return 1;
        }

        // we have some data from the pipe, add it to the read buffer
        EnterCriticalSection(&client->Lock);
        LogVerbose("[%lld] read %lu 0x%lx", client->Id, transferred, transferred);

        if (!CmqAddData(client->ReadBuffer, buffer, transferred))
        {
            // FIXME: block here until there's space
            LeaveCriticalSection(&client->Lock);
            LogError("[%lld] read buffer full", client->Id);
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
            LogWarning("[%lld] client is disconnecting, exiting", client->Id);
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

            LogVerbose("[%lld] writing %lu 0x%lx", client->Id, size, size);
            // writing will fail even if blocked when we call CancelIo() from QpsDisconnectClient
            if (!QioWriteBuffer(pipe, data, (DWORD)size)) // this can block
            {
                // win_perror("QioWriteBuffer");
                LogWarning("[%lld] write failed", client->Id);
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
    IN  LONGLONG ClientId,
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
    client->Id = ClientId;

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
    LogInfo("[%lld] (%p) connected (%lld total)", client->Id, client, Server->NumberClients);
    LeaveCriticalSection(&Server->Lock);

    if (Server->ConnectCallback)
    {
        // Increase the refcount if the connect callback is registered (pretty much always).
        // Our host (application that registered the callback) will need to call
        // QpsDisconnectClient after it's done interacting with the client
        // (this will decrease the refcount and allow for client data cleanup).
        InterlockedIncrement(&client->RefCount);
        Server->ConnectCallback(Server, client->Id, Server->UserContext);
    }

    return ERROR_SUCCESS;
}

// Public API. Decreases the client's refcount.
// DO NOT USE HERE. It will mess up the refcount. Use QpsDisconnectClientInternal directly.
void QpsDisconnectClient(
    IN  PIPE_SERVER Server,
    IN  LONGLONG ClientId
    )
{
    PPIPE_CLIENT client;

    LogDebug("[%lld]", ClientId);
    QpsDisconnectClientInternal(Server, ClientId, FALSE, FALSE);
    // We need to decrease the refcount without increasing it.
    client = QpsGetClientRaw(Server, ClientId, FALSE);
    QpsReleaseClient(Server, client);
}

static void QpsDisconnectClientInternal(
    IN  PIPE_SERVER Server,
    IN  LONGLONG ClientId,
    IN  BOOL WriterExiting,
    IN  BOOL ReaderExiting
    )
{
    PPIPE_CLIENT client = QpsGetClient(Server, ClientId);

    if (!client)
    {
        LogWarning("[%lld] not connected", ClientId);
        return;
    }

    if (client->Disconnecting)
    {
        QpsReleaseClient(Server, client);
        return;
    }

    client->Disconnecting = TRUE;
    LogInfo("[%lld] (%p) disconnecting, WriterExiting %d, ReaderExiting %d", ClientId, client, WriterExiting, ReaderExiting);

    if (!WriterExiting)
    {
        // wait for the writer thread to exit
        if (WaitForSingleObject(client->WriterThread, Server->WriteTimeout) != WAIT_OBJECT_0)
        {
            LogWarning("[%lld] writer thread didn't terminate in time, canceling write", ClientId);
            if (!CancelIo(client->WritePipe)) // this will abort a blocking operation
                win_perror("CancelIo(write)");
        }

        // wait for the writer thread cleanup
        if (WaitForSingleObject(client->WriterThread, 100) != WAIT_OBJECT_0)
        {
            LogWarning("[%lld] writer thread didn't terminate in time, killing it", ClientId);
            // this may leak memory or do other nasty things but should never happen
            TerminateThread(client->WriterThread, 0);
        }
    }
    LogDebug("[%lld] (%p) closing write handles", ClientId, client);
    CloseHandle(client->WriterThread);
    client->WriterThread = NULL;
    CloseHandle(client->WritePipe);
    client->WritePipe = NULL;

    if (!ReaderExiting)
    {
        // wait for the reader thread to exit
        LogVerbose("[%lld] (%p) canceling read", ClientId, client);
        if (!CancelIo(client->ReadPipe)) // this will abort a blocking operation
            win_perror("CancelIo(read)");

        if (WaitForSingleObject(client->ReaderThread, 100) != WAIT_OBJECT_0)
        {
            LogWarning("[%lld] reader thread didn't terminate in time, killing it", ClientId);
            // this may leak memory or do other nasty things but should never happen
            TerminateThread(client->ReaderThread, 0);
        }
    }
    LogDebug("[%lld] (%p) closing read handles", ClientId, client);
    CloseHandle(client->ReaderThread);
    client->ReaderThread = NULL;
    CloseHandle(client->ReadPipe);
    client->ReadPipe = NULL;

    // rest of the client's data will be destroyed by QpsReleaseClient when refcount drops to 0
    QpsReleaseClient(Server, client);

    LogInfo("[%lld] disconnected (%lld total)", ClientId, Server->NumberClients);

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
    LONGLONG clientId;
    DWORD cbPipeName;
    DWORD written;

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
            LogVerbose("waiting for outbound connection, pipe %s", Server->PipeName);
            connected = ConnectNamedPipe(writePipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (!connected)
                Sleep(10);
        } while (!connected);

        LogVerbose("outbound pipe connected");
        // prepare the read pipe
        if (!GetNamedPipeClientProcessId(writePipe, &pid))
        {
            return win_perror("GetNamedPipeClientProcessId");
        }

        clientId = QpsAllocateClientId(Server);
        // create the second pipe with unique name
        StringCbPrintf(pipeName, sizeof(pipeName), L"%s-%lu-%lld", Server->PipeName, pid, clientId);
        readPipe = CreateNamedPipe(pipeName,
                                   PIPE_ACCESS_INBOUND,
                                   PIPE_TYPE_BYTE | PIPE_WAIT,
                                   PIPE_UNLIMITED_INSTANCES,
                                   Server->PipeBufferSize,
                                   Server->PipeBufferSize,
                                   0,
                                   Server->SecurityAttributes);

        // send the name to the client
        cbPipeName = (DWORD) (wcslen(pipeName) + 1) * sizeof(WCHAR);
        if (!WriteFile(writePipe, &cbPipeName, sizeof(cbPipeName), &written, NULL))
        {
            return win_perror("writing size of inbound pipe name");
        }

        if (!WriteFile(writePipe, pipeName, cbPipeName, &written, NULL))
        {
            return win_perror("writing name of inbound pipe");
        }

        do
        {
            LogVerbose("waiting for inbound connection, pipe %s", pipeName);
            connected = ConnectNamedPipe(readPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (!connected)
                Sleep(10);
        } while (!connected);
        LogVerbose("inbound pipe connected");

        // initialize the client
        status = QpsConnectClient(Server, clientId, writePipe, readPipe);
        if (status != ERROR_SUCCESS)
            return status;
    }
}

DWORD QpsRead(
    IN  PIPE_SERVER Server,
    IN  LONGLONG ClientId,
    OUT void *Data,
    IN  DWORD DataSize
    )
{
    UINT64 size;
    BOOL ret;
    PPIPE_CLIENT client = QpsGetClient(Server, ClientId);

    LogVerbose("[%lld] size %lu", ClientId, DataSize);
    if (!client)
        return ERROR_NOT_CONNECTED;

    // get data from the read buffer until the requested amount is read
    do
    {
        // get data from the read buffer if available
        size = DataSize;
        EnterCriticalSection(&client->Lock);
        ret = CmqGetData(client->ReadBuffer, Data, &size, CMQ_NO_UNDERFLOW);
        LeaveCriticalSection(&client->Lock);

        if (!ret)
        {
            // If there's not enough data and the client is disconnecting,
            // that means we'll never have enough data: abort.
            if (client->Disconnecting)
            {
                QpsReleaseClient(Server, client);
                LogWarning("[%lld] client is disconnected", ClientId);
                return ERROR_BROKEN_PIPE;
            }

            Sleep(1); // don't congest the lock
        }
    } while (!ret);

    QpsReleaseClient(Server, client);
    return ERROR_SUCCESS;
}

DWORD QpsWrite(
    IN  PIPE_SERVER Server,
    IN  LONGLONG ClientId,
    IN  const void *Data,
    IN  DWORD DataSize
    )
{
    PPIPE_CLIENT client = QpsGetClient(Server, ClientId);
    BOOL ret;

    LogVerbose("[%lld] size %lu", ClientId, DataSize);
    if (!client)
        return ERROR_NOT_CONNECTED;

    if (client->Disconnecting)
    {
        // pipe(s) broken, we won't be able to send anything
        QpsReleaseClient(Server, client);
        LogWarning("[%lld] client is disconnected", ClientId);
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
        LogError("[%lld] write buffer full", ClientId);
        return ERROR_BUFFER_OVERFLOW;
    }

    QpsReleaseClient(Server, client);
    return ERROR_SUCCESS;
}

DWORD QpsGetReadBufferSize(
    IN  PIPE_SERVER Server,
    IN  LONGLONG ClientId
    )
{
    DWORD queuedData;
    PPIPE_CLIENT client = QpsGetClient(Server, ClientId);

    LogVerbose("[%lld]", ClientId);
    if (!client)
        return 0;

    EnterCriticalSection(&client->Lock);
    queuedData = (DWORD)CmqGetUsedSize(client->ReadBuffer);
    LeaveCriticalSection(&client->Lock);
    QpsReleaseClient(Server, client);
    return queuedData;
}

DWORD QpsSetClientData(
    IN  PIPE_SERVER Server,
    IN  LONGLONG ClientId,
    IN  PVOID UserData
    )
{
    PPIPE_CLIENT client = QpsGetClient(Server, ClientId);

    LogVerbose("[%lld]", ClientId);
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
    IN  LONGLONG ClientId
    )
{
    PPIPE_CLIENT client = QpsGetClient(Server, ClientId);
    PVOID data;

    LogVerbose("[%lld]", ClientId);
    if (!client)
        return NULL;

    EnterCriticalSection(&client->Lock);
    data = client->UserData;
    LeaveCriticalSection(&client->Lock);
    QpsReleaseClient(Server, client);
    return data;
}

DWORD QpsConnect(
    IN  PWCHAR PipeName,
    OUT HANDLE *ReadPipe,
    OUT HANDLE *WritePipe
    )
{
    DWORD status;
    WCHAR writePipeName[256];
    DWORD cbWritePipeName;
    DWORD read;

    // Try to open the read pipe; wait for it, if necessary.
    do
    {
        LogDebug("opening read pipe: %s", PipeName);
        *ReadPipe = CreateFile(PipeName,
                              GENERIC_READ, // this is an inbound pipe
                              0,
                              NULL,
                              OPEN_EXISTING,
                              0,
                              NULL);

        // Exit if an error other than ERROR_PIPE_BUSY occurs.
        status = GetLastError();
        if (*ReadPipe == INVALID_HANDLE_VALUE)
        {
            if (ERROR_PIPE_BUSY != status)
            {
                LogDebug("open read pipe failed, code: %d", (int)status);
                return status; // win_perror("open read pipe");
            }

            // Wait until the pipe is available.
            if (!WaitNamedPipe(PipeName, NMPWAIT_WAIT_FOREVER))
            {
                status = GetLastError();
                LogDebug("WaitNamedPipe(read) failed, status: %s", (int)status);
                return status; // win_perror("WaitNamedPipe(read)");
            }
        }
    } while (*ReadPipe == INVALID_HANDLE_VALUE);

    // Read the second pipe name from the server.
    if (!ReadFile(*ReadPipe, &cbWritePipeName, sizeof(cbWritePipeName), &read, NULL))
    {
        CloseHandle(*ReadPipe);
        return win_perror("reading size of write pipe name");
    }

    if (!ReadFile(*ReadPipe, writePipeName, cbWritePipeName, &read, NULL))
    {
        CloseHandle(*ReadPipe);
        return win_perror("reading write pipe name");
    }

    // Try to open the write pipe; wait for it, if necessary.
    do
    {
        LogDebug("opening write pipe: %s", writePipeName);
        *WritePipe = CreateFile(writePipeName,
                                GENERIC_WRITE, // this is an outbound pipe
                                0,
                                NULL,
                                OPEN_EXISTING,
                                0,
                                NULL);

        // This pipe may be not created yet
        status = GetLastError();
        if ((*WritePipe == INVALID_HANDLE_VALUE) && (ERROR_FILE_NOT_FOUND != status))
            return win_perror("open write pipe");

        Sleep(10);
    } while (*WritePipe == INVALID_HANDLE_VALUE);

    return ERROR_SUCCESS;
}
