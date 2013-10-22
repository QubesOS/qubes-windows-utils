#include "buffer.h"
#include "log.h"

static size_t g_buffer_size = 0;  // buffer size
static char  *g_buffer_start = 0; // start of storage memory
static char  *g_data_start = 0;   // pointer to the first byte of data. null = buffer empty
static char  *g_data_end = 0;     // pointer to the first free byte. null = buffer full

// allocate memory and set variables
int buffer_create(size_t buffer_size)
{
	debugf("buffer_create: %d bytes\n", buffer_size);

	if (g_buffer_start != 0) // already allocated
	{
		errorf("buffer_create: already allocated\n");
		return -1;
	}

	if (buffer_size == 0)
	{
		errorf("buffer_create: size == 0\n");
		return -1;
	}

	g_buffer_size = buffer_size;
	g_buffer_start = (char*) malloc(g_buffer_size);
	g_data_start = g_data_end = 0;
	if (g_buffer_start == 0)
	{
		errorf("buffer_create: no memory\n");
		return -1;
	}

	return 0;
}

// free memory and deinitialize
int buffer_destroy(void)
{
	debugf("buffer_destroy\n");

	if (g_buffer_start == 0) // not allocated
	{
		errorf("buffer_destroy: not allocated\n");
		return -1;
	}

	free(g_buffer_start);
	g_data_start = g_data_end = g_buffer_start = 0;
	g_buffer_size = 0;

	return 0;
}

// zero-fill buffer, rewind internal pointer
int buffer_clear(void)
{
	debugf("buffer_clear\n");

	if (g_buffer_start == 0) // not allocated
	{
		errorf("buffer_clear: not allocated\n");
		return -1;
	}

	g_data_start = g_data_end = 0;
	memset(g_buffer_start, 0, g_buffer_size);

	return 0;
}

// queue data to the buffer
int buffer_add_data(void *pdata, size_t data_size)
{
	char *data = (char*) pdata;
	size_t used_size, free_size, free_at_end;

	debugf("buffer_add_data(0x%x, %d)\n", data, data_size);

	if (g_data_end == 0 && g_data_start != 0)  // buffer full
	{
		errorf("buffer_add_data(0x%x, %d): buffer full\n", data, data_size);
		return -1;
	}

	if (data_size == 0)
		return 0;

	if (g_data_start == 0) // empty, initialize pointers
	{
		g_data_start = g_data_end = g_buffer_start;
	}

	// needs that if DataEnd is at the end of buffer it's not wrapped to 0
	if (g_data_start <= g_data_end) // data in one consecutive block (or empty)
		used_size = (g_data_end - g_data_start);
	else
		used_size = (g_data_end + g_buffer_size - g_data_start);

	free_size = g_buffer_size - used_size;
	//  =       <       <       <!      >
	// [.....] [oo...] [.oo..] [...oo] [o...o]
	if (data_size > free_size)
	{
		errorf("buffer_add_data(0x%x, %d): buffer too small (free: %d)\n", data, data_size, free_size);
		return -1;
	}

	if (g_data_start > g_buffer_start && g_data_end < g_buffer_start+g_buffer_size) // [.oo..]
	{
		free_at_end = (g_buffer_start + g_buffer_size - g_data_end);
		if (data_size <= free_at_end)
			memcpy(g_data_end, data, data_size);
		else
		{
			memcpy(g_data_end, data, data_size-free_at_end);
			memcpy(g_buffer_start, (data+data_size-free_at_end), free_at_end);
		}
	}
	else
	{
		if (g_data_end == g_buffer_start+g_buffer_size) // end at the limit, wrap to 0
			g_data_end = g_buffer_start;
		// now we have one consecutive block of free memory so just copy
		memcpy(g_data_end, data, data_size);
	}

	g_data_end += data_size;
	if (g_data_end > g_buffer_start+g_buffer_size) // wrap only if end > limit, if end==limit pointer stays at the end
		g_data_end -= g_buffer_size;

	if (used_size + data_size == g_buffer_size) // buffer full
		g_data_end = 0;

	return 0;
}

// "pop" data from the buffer
// underflow = true: don't treat underflow as errors (reading empty buffer is ok etc)
// data_size = 0: read all (use carefully in case of buffer overruns)
int buffer_get_data(void *pdata, size_t *data_size, BUFFER_UNDERFLOW_MODE underflow_mode)
{
	char *data = (char*) pdata;
	size_t used_size, used_at_end;

	errorf("buffer_get_data(0x%x, 0x%x, %d)\n", data, data_size, underflow_mode);
	if (g_data_start == 0)  // buffer empty
	{
		*data_size = 0;
		if ((underflow_mode==BUFFER_ALLOW_UNDERFLOW) || (*data_size == 0))
			return 0;
		else
		{
			errorf("buffer_get_data: buffer empty\n");
			return -1;
		}
	}

	if (g_data_end == 0) // buffer full, only _pDataStart is valid
	{
		g_data_end = g_data_start;
		used_size = g_buffer_size;
	}
	else
	{
		// needs that if DataEnd is at the end of buffer it's not wrapped to 0
		if (g_data_start < g_data_end) // data in one consecutive block
			used_size = (g_data_end - g_data_start);
		else
			used_size = (g_data_end + g_buffer_size - g_data_start);
	}

	//  full    <       >       <       <
	// [ooooo] [..ooo] [o..oo] [ooo..] [.ooo.]

	if (*data_size == 0)   // "read all"
		*data_size = used_size;
	else
	{
		if (*data_size > used_size) // underflow
		{
			if (underflow_mode==BUFFER_ALLOW_UNDERFLOW)
				*data_size = used_size;
			else
			{
				*data_size = 0;
				errorf("buffer_get_data: underflow (requested %d, got %d)\n", *data_size, used_size);
				return -1;
			}
		}
	}

	if (g_data_start >= g_data_end) // [o..oo] should be safe for full buffer too (_pDataEnd == _pDataStart)
	{
		used_at_end = (g_buffer_start + g_buffer_size - g_data_start);
		if (*data_size <= used_at_end)
			memcpy(data, g_data_start, *data_size);
		else
		{
			memcpy(data, g_data_start, used_at_end);
			memcpy((data+used_at_end), g_buffer_start, *data_size-used_at_end);
		}
	}
	else
	{
		// now we have one consecutive block of data memory so just copy
		memcpy(data, g_data_start, *data_size);
	}

	g_data_start += *data_size;
	if (g_data_start >= g_buffer_start+g_buffer_size)
		g_data_start -= g_buffer_size;

	if (used_size - *data_size == 0) // buffer empty
	{
		g_data_start = 0;
	}

	return 0;
}

// get used data size
size_t buffer_get_size(void)
{
	if (g_buffer_size == 0 || g_buffer_start == 0 || g_data_start == 0)
		return 0;

	if (g_data_end == 0)  // full
		return g_buffer_size;

	if (g_data_start < g_data_end) // data in one consecutive block
		return (g_data_end - g_data_start);
	else
		return (g_data_end + g_buffer_size - g_data_start);
}
