#pragma once
#include <stdlib.h>

// Circular memory queue implementation. 
// All int-returning functions return 0 on success, <0 on failure.

struct _buffer;
typedef struct _buffer buffer_t;

// Underflow mode for read operations.
typedef enum
{
	BUFFER_ALLOW_UNDERFLOW = 1,
	BUFFER_NO_UNDERFLOW
} BUFFER_UNDERFLOW_MODE;

// allocate memory and initialize, return 0 = failure
buffer_t *buffer_create(size_t buffer_size);

// free memory and deinitialize
int buffer_destroy(buffer_t *buffer);

// "push" data to the buffer
int buffer_add_data(buffer_t *buffer, void *data, size_t data_size);

// "pop" data from the buffer, size=0: get all data (make sure you have the space for it)
int buffer_get_data(buffer_t *buffer, void *data, size_t *data_size, BUFFER_UNDERFLOW_MODE underflow_mode);

// returns bytes used by data
size_t buffer_get_size(buffer_t *buffer);

// zero-fill buffer, rewind internal pointer
int buffer_clear(buffer_t *buffer);
