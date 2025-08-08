#pragma once

#include <stdlib.h>
#include <stdbool.h>

typedef struct
{
    size_t size;
    char* data;
} Buffer;

Buffer buffer_init();
void buffer_free(Buffer *buffer);
bool buffer_is_ready(const Buffer *buffer);
void buffer_write_data(Buffer *buffer, const char* data, const size_t data_size);