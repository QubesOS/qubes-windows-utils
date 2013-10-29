#include "vchan-common.h"
#include "log.h"

BOOL _send_to_vchan(libvchan_t *vchan, void *data, size_t size, TCHAR *what)
{
    int write_size;

    debugf("send_to_vchan(0x%x, %s): %d bytes (free: %d)\n", vchan, what, size, libvchan_buffer_space(vchan));
    hex_dump(0, data, size);

    if (libvchan_buffer_space(vchan) < size)
    {
        // shouldn't happen - send will block
        debugf("send_to_vchan(0x%x, %s): vchan buffer full, blocking write\n", vchan, what);
        while (libvchan_buffer_space(vchan) < size)
            Sleep(1);
    }

    if ((write_size = libvchan_send(vchan, data, size)) < 0)
    {
        errorf("send_to_vchan(0x%x, %s): failed to send (size %d)\n", vchan, what, size);
        return FALSE;
    }

    if (write_size != size)
    {
        // shouldn't happen
        logf("send_to_vchan(0x%x, %s): size mismatch or error: requested %d, sent %d\n", vchan, what, size, write_size);
        return FALSE;
    }

    return TRUE;
}

BOOL _recv_from_vchan(libvchan_t *vchan, void *data, size_t size, TCHAR *what)
{
    int read_size;

    debugf("recv_from_vchan(0x%x, %s): %d bytes (available: %d)\n", vchan, what, size, libvchan_data_ready(vchan));
    if (libvchan_data_ready(vchan) < size)
    {
        // shouldn't happen - recv will block
        debugf("recv_from_vchan(0x%x, %s): no data, blocking read\n", vchan, what);
    }

    if ((read_size = libvchan_recv(vchan, data, size)) < 0)
    {
        errorf("recv_from_vchan(0x%x, %s): failed to read (size %d)\n", vchan, what, size);
        return FALSE;
    }

    if (read_size != size)
    {
        // shouldn't happen
        logf("recv_from_vchan(0x%x, %s): size mismatch or error: requested %d, got %d\n", vchan, what, size, read_size);
        return FALSE;
    }

    hex_dump(0, data, size);
    return TRUE;
}
