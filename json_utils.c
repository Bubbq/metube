#include "include/json_utils.h"

#include "include/utils.h"

#include <string.h>
#include <stdlib.h>

#define LAST_ELEMENT_INDEX -1

cJSON* parse_json_file(const char* filename)
{
    if (!valid_string(filename)) 
        return NULL;

    char* buffer = get_file_content(filename);
    if (!buffer) {
        fprintf(stderr, "parse_json_file: \"%s\" is empty\n", filename);
        return NULL;
    }

    cJSON* json = cJSON_Parse(buffer);

    free(buffer); buffer = NULL;

    return json;
}

void write_json_to_file(const cJSON* json, const char* filename)
{
    if (!json || !valid_string(filename)) 
        return;

    char* buffer = cJSON_Print(json);
    if (!buffer) {
        fprintf(stderr, "write_json_file: json object is empty\n");
        return; 
    }

    write_string_to_file(filename, buffer);

    free(buffer); buffer = NULL;
}

bool json_string_is_valid(const cJSON* json_str)
{
    return cJSON_IsString(json_str) && valid_string(json_str->valuestring);
}

cJSON* cjson_pointer_get(cJSON* root, const char* path)
{
    if (!root || !valid_string(path)) 
        return root;

    int n = 0;
    char* copy_output = NULL;
    char** elements = text_split(path, '.', &n,&copy_output);
    if (!elements) 
        return NULL;

    cJSON* ret = root;

    for(int i = 0; (i < n); i++) {
        if (elements[i][0] == '\0') 
            continue;

        const char* opening_brace = strchr(elements[i], '[');
        if (opening_brace) {
            const size_t name_len = (size_t) (opening_brace - elements[i]); 

            char array_name[name_len + 1]; 
            memcpy(array_name, elements[i], name_len);
            array_name[name_len] = '\0';

            ret = cJSON_GetObjectItem(ret, array_name);
            if (!ret) {
                fprintf(stderr, "cjson_pointer_get: failed to add array object \"%s\"\n", elements[i]);
                free(copy_output); copy_output = NULL;
                free(elements); elements = NULL;
                return NULL;
            }

            if (!cJSON_IsArray(ret)) {
                fprintf(stderr, "cjson_pointer_get: accessing element in non-array object (%s)\n", elements[i]);
                free(copy_output); copy_output = NULL;
                free(elements); elements = NULL;
                return NULL;
            }

            const char* closing_brace = strchr(opening_brace, ']');
            if (!closing_brace) {
                printf("cjson_pointer_get: \"%s\" is an unbalanced array\n", elements[i]);
                free(copy_output); copy_output = NULL;
                free(elements); elements = NULL;
                return NULL;
            }

            const size_t index_len = (size_t) (closing_brace - (opening_brace + 1)); 

            char index_buffer[index_len + 1];  
            memcpy(index_buffer, (opening_brace + 1), index_len); 
            index_buffer[index_len] = '\0';  

            const size_t array_size = cJSON_GetArraySize(ret);
            
            const int index_buffer_val = atoi(index_buffer);
            
            const int index = index_buffer_val == LAST_ELEMENT_INDEX ? 
                              array_size - 1 : 
                              index_buffer_val;

            if (index >= array_size) {
                fprintf(stderr, "cjson_pointer_get: accessing element %d in %zu size array (%s)\n", index, array_size, elements[i]);
                free(copy_output); copy_output = NULL;
                free(elements); elements = NULL;
                return NULL;
            }

            ret = cJSON_GetArrayItem(ret, index);
        } 

        else ret = cJSON_GetObjectItem(ret, elements[i]);
    }

    free(copy_output); copy_output = NULL;
    free(elements); elements = NULL;

    return ret;
}

bool assign_number_from_path(cJSON* root, const char* path, int* dest)
{
    if (!root || !valid_string(path)|| !dest)
        return false;

    const cJSON* json_num = cjson_pointer_get(root, path);
    if (cJSON_IsNumber(json_num)) {
        (*dest) = json_num->valueint;
        return true;
    }

    return false;
}

bool assign_string_from_path(cJSON* root, const char* path, char* dest, const size_t dest_size)
{
    if (!root || !valid_string(path) || !dest) 
        return false;

    const cJSON* json_str = cjson_pointer_get(root, path);
    if (json_string_is_valid(json_str)) {
        const int written = snprintf(dest, dest_size, "%s", json_str->valuestring);
        return (0 < written) && (written < dest_size);
    }

    return false;
}