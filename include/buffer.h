#pragma once
#include <stdlib.h>

// Circular memory queue implementation. 
// All int-returning functions return 0 on success, <0 on failure.

// Underflow mode for read operations.
typedef enum
{
	BUFFER_ALLOW_UNDERFLOW = 1,
	BUFFER_NO_UNDERFLOW
} BUFFER_UNDERFLOW_MODE;

// allocate memory and initialize
int buffer_create(size_t buffer_size);

// free memory and deinitialize
int buffer_destroy(void);

// "push" data to the buffer
int buffer_add_data(void *data, size_t data_size);

// "pop" data from the buffer, size=0: get all data (make sure you have the space for it)
int buffer_get_data(void *data, size_t *data_size, BUFFER_UNDERFLOW_MODE underflow_mode);

// returns bytes used by data
size_t buffer_get_size(void);

// zero-fill buffer, rewind internal pointer
int buffer_clear(void);
