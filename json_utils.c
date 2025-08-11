#include "utils.h"
#include "raylib.h"
#include "json_utils.h"

#include <stdio.h>
#include <stdbool.h>

cJSON* parse_json_file(const char* filename)
{
    if (valid_string(filename) == false) return NULL;

    char* buffer = get_file_content(filename);
    if (buffer == NULL) {
        fprintf(stderr, "parse_json_file: \"%s\" is empty\n", filename);
        return NULL;
    }

    cJSON* json = cJSON_Parse(buffer);

    free(buffer); buffer = NULL;

    return json;
}

void write_json_to_file(const cJSON* json, const char* filename)
{
    if ((json == NULL) || (valid_string(filename) == false)) return;

    char* buffer = cJSON_Print(json);
    if (buffer == NULL) {
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
    if (root == NULL) return NULL;
    else if (valid_string(path) == false) return root;

    const int last_element_index = -1;

    int n = 0;
    const char** elements = TextSplit(path, '.', &n); 

    cJSON* ret = root;

    for(int i = 0; (i < n); i++) {
        if (elements[i][0] == '\0') {
            continue;
        }

        const char* opening_brace_ptr = strchr(elements[i], '[');
        if (opening_brace_ptr) {
            const size_t name_len = opening_brace_ptr - elements[i] + 1; 

            char array_name[name_len];
            strncpy(array_name, elements[i], name_len - 1);
            array_name[name_len - 1] = '\0';

            ret = cJSON_GetObjectItem(ret, array_name);
            if (ret == NULL) {
                // printf("cjson_pointer_get: failed to add array object \"%s\"\n", elements[i]);
                return NULL;
            }

            if (cJSON_IsArray(ret) == false) {
                printf("cjson_pointer_get: accessing element in non-array object (%s)\n", elements[i]);
                return NULL;
            }

            const char* closing_brace_ptr = strchr(opening_brace_ptr, ']');
            if (closing_brace_ptr == NULL) {
                printf("cjson_pointer_get: \"%s\" is an unbalanced array\n", elements[i]);
                return NULL;
            }

            const size_t len = closing_brace_ptr - opening_brace_ptr; 

            char index_buffer[len];
            strncpy(index_buffer, opening_brace_ptr + 1, len - 1);
            index_buffer[len - 1] = '\0';

            const size_t arr_size = cJSON_GetArraySize(ret);
            
            const int index_buffer_val = atoi(index_buffer);
            const size_t index = (index_buffer_val == last_element_index) ? (arr_size - 1) : index_buffer_val;
            if (index >= arr_size) {
                printf("cjson_pointer_get: accessing element %zu in %zu size array (%s)\n", index, arr_size, elements[i]);
                return NULL;
            }

            ret = cJSON_GetArrayItem(ret, index);
        } 

        else ret = cJSON_GetObjectItem(ret, elements[i]);
    }

    return ret;
}

bool assign_string_from_path(cJSON* root, const char* path, char* dest, const size_t dest_size)
{
    if ((root == NULL) || (valid_string(path) == false) || (dest == NULL)) return false;

    const cJSON* json_str = cjson_pointer_get(root, path);
    if (json_string_is_valid(json_str)) {
        const int written = snprintf(dest, dest_size, "%s", json_str->valuestring);
        return (written > 0) && (written < dest_size);
    }

    return false;
}

cJSON* create_empty_array_object(const char* array_name)
{
    if (valid_string(array_name) == false) return NULL;

    cJSON* root = cJSON_CreateObject();
    if (root == NULL) {
        fprintf(stderr, "create_empty_array_object: failed to create root\n");
        return NULL;
    }

    cJSON* array = cJSON_CreateArray();
    if (array == NULL) {
        fprintf(stderr, "create_empty_array_object: failed to create array\n");
        cJSON_Delete(root); root = NULL;
        return NULL;
    }

    if (cJSON_AddItemToObject(root, array_name, array) == false) {
        fprintf(stderr, "create_empty_array_object: failed to add array to root\n");
        cJSON_Delete(root); root = NULL;
    }

    return root;
}