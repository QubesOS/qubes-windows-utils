#pragma once
#include <windows.h>
#include "buffer.h"

/*
This is a generic implementation of a named pipe server.
It's multithreaded with blocking pipes (two per client).

Initial pipe created is outbound (server->client). When a client
connects, the inbound pipe is created with the same name and
"-clientpid" appended (clientpid is the decimal PID of the client).

Each connected client has a background reader thread that tries
to read data in a loop and adds it to an internal memory buffer.

When you read from a client, the function just gets the data
from the read buffer. If insufficient data is present, it waits
until more data arrives.

Writing to a client is non-blocking, a background thread is started
to complete the operation in case the client blocks. The client
is expected to read written data in a certain amount of time,
otherwise it's disconnected.

The usual mode of operation is as follows:
- Create the server object, register at least a QPS_CLIENT_CONNECTED
  callback function.
- Call QpsMainLoop() in a separate thread.
- When a client connects in the QPS_CLIENT_CONNECTED callback,
  start a new thread to handle the communication. Callbacks are
  blocking and should return ASAP.
- A single client thread usually flows according to the desired
  communication protocol state machine.
*/

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WINDOWSUTILS_EXPORTS
#    define WINDOWSUTILS_API __declspec(dllexport)
#else
#    define WINDOWSUTILS_API __declspec(dllimport)
#endif

struct _PIPE_SERVER;

// Control structure.
typedef struct _PIPE_SERVER *PIPE_SERVER;

/*
Callback invoked after a client has connected.
This is a good place to start a thread for the usual processing/state machine.
This is a blocking call, hence the need for a separate processing thread.
The server takes care of locking its data.
*/
typedef void(*QPS_CLIENT_CONNECTED)(struct _PIPE_SERVER *Server, DWORD Id, PVOID Context);

/*
Callback invoked after a client has disconnected.
All client's data is deallocated/invalid at this time.
*/
typedef void(*QPS_CLIENT_DISCONNECTED)(struct _PIPE_SERVER *Server, DWORD Id, PVOID Context);

/*
Callback invoked when data has been read from a client.
Probably not very useful since you can't expect the reads having predictable sizes.
Read data is enqueued in FIFO manner in an internal buffer.
Use QpsRead() for structured, buffered reads.
*/
typedef void(*QPS_DATA_RECEIVED)(struct _PIPE_SERVER *Server, DWORD Id, PVOID Data, DWORD DataSize, PVOID Context);

// All functions return an error code unless specified otherwise.

// Create the server.
WINDOWSUTILS_API
DWORD QpsCreate(
    IN  PWCHAR PipeName, // This is a client->server pipe name (clients write, server reads). server->client pipes have "-%PID%" appended.
    IN  DWORD PipeBufferSize, // Pipe read/write buffer size. Shouldn't be too big.
    IN  DWORD ReadBufferSize, // Read buffer (per client). The server enqueues all received data here until it's read by QpsRead().
    IN  DWORD WriteTimeout, // If a client doesn't read written data in this amount of milliseconds, it's disconnected.
    IN  QPS_CLIENT_CONNECTED ConnectCallback, // "Client connected" callback.
    IN  QPS_CLIENT_DISCONNECTED DisconnectCallback OPTIONAL, // "Client disconnected" callback.
    IN  QPS_DATA_RECEIVED ReadCallback OPTIONAL, // "Data received" callback.
    IN  PVOID Context OPTIONAL, // Opaque parameter that will be passed to callbacks.
    IN  PSECURITY_ATTRIBUTES SecurityAttributes OPTIONAL, // Must be valid for the whole lifetime of the server.
    OUT PIPE_SERVER *Server // Server object.
    );

// Destroy the server, disconnect all clients, deallocate memory.
WINDOWSUTILS_API
void QpsDestroy(
    PIPE_SERVER Server // The server to destroy.
    );

// Main loop of the server. Call to start accepting clients.
// Returns only on error. At that point the server state is undefined, call ServerDestroy.
WINDOWSUTILS_API
DWORD QpsMainLoop(
    PIPE_SERVER Server
    );

// Blocking read from a client.
// Returns immediately if the internal read queue has enough data.
WINDOWSUTILS_API
DWORD QpsRead(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientId,
    OUT void *Data,
    IN  DWORD DataSize
    );

// Nonblocking write to a client.
// Uses a background thread to write.
WINDOWSUTILS_API
DWORD QpsWrite(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientId,
    IN  const void *Data,
    IN  DWORD DataSize
    );

// Get the number of bytes available in a client's read buffer.
WINDOWSUTILS_API
DWORD QpsGetReadBufferSize(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientId
    );

// Cancel all IO, disconnect the client, deallocate client's data.
WINDOWSUTILS_API
void QpsDisconnectClient(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientId
    );

// Assign arbitrary data to the client.
WINDOWSUTILS_API
DWORD QpsSetClientData(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientId,
    IN  PVOID UserData
    );

// Get user data assigned to the client.
WINDOWSUTILS_API
PVOID QpsGetClientData(
    IN  PIPE_SERVER Server,
    IN  DWORD ClientId
    );

// Client API: connect to a server.
WINDOWSUTILS_API
DWORD QpsConnect(
    IN  PWCHAR PipeName,
    OUT HANDLE *ReadPipe,
    OUT HANDLE *WritePipe
    );

#ifdef __cplusplus
}
#endif
