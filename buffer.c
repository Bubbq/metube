#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct
{
    size_t size;
    void* data;
} Buffer;

Buffer buffer_init()
{
    Buffer buffer;
    buffer.size = 0;
    buffer.data = NULL;
    return buffer;
}

void buffer_free(Buffer* buffer)
{
    if (buffer == NULL) return;

    if (buffer) {
        free(buffer->data); 
        buffer->data = NULL;
    }

    buffer->size = 0;
}

bool buffer_ready(Buffer* buffer)
{
    return (buffer->data) && (buffer->size > 0);
}

void write_data_to_buffer(Buffer* buffer, const char* data, const size_t data_size, const bool null_terminate)
{
    if ((buffer == NULL) || (data == NULL)) return;

    size_t new_size = buffer->size + data_size + (null_terminate ? 1 : 0);

    char* new_data = realloc(buffer->data, new_size);
    if (new_data == NULL) {
        printf("write_data_to_buffer: realloc returned NULL\n");
        buffer_free(buffer);
        return;
    }

    buffer->data = new_data;

    memcpy((buffer->data + buffer->size), data, data_size);

    buffer->size += data_size;

    if (null_terminate) {
        ((char*)buffer->data)[buffer->size] = '\0';
    }
}

void buffer_create_file(const char* filename, const Buffer* buffer)
{
    if ((filename == NULL) || (buffer == NULL)) return;

    FILE* fp = fopen(filename, "wb");
    if (fp == NULL) {
        printf("buffer_create_file: fopen returned NULL\n");
        return;
    }

    unsigned long written = fwrite(buffer->data, sizeof(char), buffer->size, fp);
    if (written != buffer->size) {
        printf("buffer_create_file: only %ld out of %zu chars written\n", written, buffer->size);
    }

    fclose(fp);
}