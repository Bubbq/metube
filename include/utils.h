#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdbool.h>

int bound_index_to_array (const int pos, const int array_size);
bool file_exists(const char* filename);
const long get_file_length(FILE* fp);
char* get_file_content(const char* filepath);
void write_string_to_file(const char* filename, const char* buffer);
size_t trim_whitespace(char* string);
int filter_non_numeric_chars(char* string, const size_t string_size);
bool valid_string(const char* string);
bool connected_to_internet();
char** text_split(const char* text, const char delim, int* count, char** copy_output);
void format_view_count(char* dest, const size_t dest_size);

#endif