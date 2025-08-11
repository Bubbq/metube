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
    if (buffer == NULL) return;

    buffer->size = 0;

    if (buffer->data) {
        free(buffer->data); buffer->data = NULL;
    }
}

bool buffer_is_ready(const Buffer* buffer)
{
    return (buffer) && (buffer->data) && (buffer->size > 0);
}

void buffer_write_data(Buffer* buffer, const char* data, const size_t data_size)
{
    if ((buffer == NULL) || (data == NULL) || (data_size == 0)) return;

    const size_t new_size = buffer->size + data_size + 1;
    
    char* new_data = realloc(buffer->data, new_size);
    if (new_data == NULL) {
        fprintf(stderr,"buffer_write_data: failed to reallocate %zu bytes\n", new_size);
        exit(EXIT_FAILURE);
        return;
    }

    buffer->data = new_data;

    void* dest = buffer->data + buffer->size;
    memcpy(dest, data, data_size);

    buffer->size += data_size;
    
    buffer->data[buffer->size] = '\0';
}