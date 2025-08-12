#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <cjson/cJSON.h>

// file I/O
cJSON* parse_json_file(const char* filename);
void write_json_to_file(const cJSON* json, const char* filename);

// walking down and reading json obj
bool json_string_is_valid(const cJSON* json_str);
cJSON* cjson_pointer_get(cJSON* root, const char* path);
bool assign_number_from_path(cJSON* root, const char* path, int* dest);
bool assign_string_from_path(cJSON* root, const char* path, char* dest, const size_t dest_size);

#endif