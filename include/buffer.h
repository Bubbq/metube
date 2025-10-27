#ifndef BUFFER_H
#define BUFFER_H

#include <stdlib.h>
#include <stdbool.h>

typedef struct
{
    size_t size ;
    void * data ;
} Buffer ;

Buffer buffer_init   () ;
void buffer_free     (Buffer * buffer) ;
bool buffer_is_ready (const Buffer * buffer) ;
bool write_to_buffer (Buffer * buffer, const char * data, const size_t data_size) ;

#endif