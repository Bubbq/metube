#include "include/buffer.h"

#include <stdio.h>
#include <string.h>

Buffer buffer_init()
{
    Buffer buffer = {
        .size = 0,
        .data = NULL
    };

    return buffer;
}

void buffer_free(Buffer* buffer)
{
    if (!buffer) 
        return;

    if (buffer->data) {
        free(buffer->data); buffer->data = NULL;
    }
}

bool buffer_is_ready(const Buffer* buffer)
{
    return (buffer) && (buffer->data) && (buffer->size > 0);
}

bool buffer_write_data(Buffer* buffer, const char* data, const size_t data_size)
{
    if (!buffer || !data) 
        return false;

    const size_t new_size = buffer->size + data_size + 1;
    
    char* new_data = realloc(buffer->data, new_size);
    if (!new_data) {
        fprintf(stderr,"buffer_write_data: failed to reallocate %zu bytes\n", new_size);
        return false;
    }

    buffer->data = new_data;

    void* dest = buffer->data + buffer->size;

    memcpy(dest, data, data_size);

    buffer->size += data_size;
    
    buffer->data[buffer->size] = '\0';

    return true;
}