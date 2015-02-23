#include "vchan-common.h"
#include "log.h"

int VchanGetReadBufferSize(IN libvchan_t *vchan)
{
    return libvchan_data_ready(vchan);
}

int VchanGetWriteBufferSize(IN libvchan_t *vchan)
{
    return libvchan_buffer_space(vchan);
}

BOOL VchanSendBuffer(IN libvchan_t *vchan, IN const void *data, IN size_t size, IN const WCHAR *what)
{
    int writeSize;

    LogVerbose("(%p, %s): %d bytes (free: %d)", vchan, what, size, VchanGetWriteBufferSize(vchan));
    //hex_dump(what, data, size);

    if (VchanGetWriteBufferSize(vchan) < size)
    {
        // wait for space
        LogWarning("(%p, %s): vchan buffer full, blocking write", vchan, what);
        while (VchanGetWriteBufferSize(vchan) < size)
            Sleep(1);
    }

    if ((writeSize = libvchan_send(vchan, data, size)) < 0)
    {
        LogError("(%p, %s): failed to send (buffer %p, size %d)", vchan, what, data, size);
        return FALSE;
    }

    if (writeSize != size)
    {
        // shouldn't happen
        LogError("(%p, %s): size mismatch or error: requested %d, sent %d", vchan, what, size, writeSize);
        return FALSE;
    }

    return TRUE;
}

BOOL VchanReceiveBuffer(IN libvchan_t *vchan, OUT void *data, IN size_t size, IN const WCHAR *what)
{
    int readSize;

    LogVerbose("(%p, %s): %d bytes (available: %d)", vchan, what, size, VchanGetReadBufferSize(vchan));
    if (VchanGetReadBufferSize(vchan) < size)
    {
        // wait for data
        LogWarning("(%p, %s): no data, blocking read", vchan, what);
    }

    if ((readSize = libvchan_recv(vchan, data, size)) < 0)
    {
        LogError("(%p, %s): failed to read (size %d)", vchan, what, size);
        return FALSE;
    }

    if (readSize != size)
    {
        // shouldn't happen
        LogError("(%p, %s): size mismatch or error: requested %d, got %d", vchan, what, size, readSize);
        return FALSE;
    }

    //hex_dump(what, data, size);
    return TRUE;
}
