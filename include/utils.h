#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdbool.h>

typedef struct
{
    size_t start_time;
	size_t lifetime; // in seconds
} Timer;

void timer_start(Timer* timer, const size_t lifetime) ;
bool timer_is_done(Timer timer) ;

int bound_index_to_array (const int pos, const int array_size);
bool file_exists(const char* filename);
const long get_file_length(FILE* fp);
char* get_file_content(const char* filepath);
void write_string_to_file(const char* filename, const char* buffer);
size_t trim_whitespace(char* string);
int filter_non_numeric_chars(char* string, const size_t string_size);
bool valid_string(const char* string);
bool connected_to_internet();
int hms_to_seconds (const char * hms) ;
bool array_contains_object (const void * array, const size_t nmemb, const size_t element_size, const void * object, const size_t object_size) ;
bool enum_is_valid (const int enumeration, const size_t ne_memb) ;

#endif