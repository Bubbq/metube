#include "include/buffer.h"

#include <string.h>

Buffer buffer_init ()
{
    return (Buffer) {
        .data = NULL ,
        .size = 0 ,
    } ;
}

void buffer_free (Buffer * buffer)
{
    if ( !buffer) 
        return ;

    if (buffer->data) {
        free(buffer->data) ; buffer->data = NULL ;
    }
}

bool buffer_is_ready (const Buffer * buffer)
{
    return (buffer) && (buffer->data) && (buffer->size > 0) ;
}

bool write_to_buffer (Buffer * buffer, const char * data, const size_t data_size)
{
    if ( !buffer || !data) 
        return false ;

    const size_t new_size = buffer->size + data_size + 1 ;
    
    char * new_data = realloc(buffer->data, new_size) ;
    if ( !new_data) 
        return false ;

    buffer->data = new_data ;
    void * data_dest = buffer->data + buffer->size ;

    memcpy(data_dest, data, data_size) ;

    buffer->size += data_size ;
    
    (*((char*)(buffer->data + buffer->size))) = '\0' ;

    return true ;
}