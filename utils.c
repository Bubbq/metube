#include "include/utils.h"

#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

int bound_index_to_array (const int pos, const int array_size)
{
    return (pos + array_size) % array_size;
}

bool file_exists(const char* filename)
{
    if (!valid_string(filename)) 
        return false;

    FILE* fp = fopen(filename, "r");
    if (fp) {
        fclose(fp);
        return true;
    }

    return false;
}

const long get_file_length(FILE* fp)
{
    if (!fp) 
        return 0;

    const long original_position = ftell(fp);

    fseek(fp, 0, SEEK_END);

    const long file_len = ftell(fp) - original_position;    

    fseek(fp, original_position, SEEK_SET);

    return file_len;
}

char* get_file_content(const char* filepath)
{
    if (!valid_string(filepath)) 
        return NULL;

    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "get_file_content: fopen returned null\n");
        return NULL;
    }

    const long len = get_file_length(fp);

    char* buffer = malloc(sizeof(char) * (len + 1));
    if (!buffer) {
        fprintf(stderr, "get_file_length: malloc returned null\n");
        fclose(fp); fp = NULL;
        exit(EXIT_FAILURE);
    }

    const unsigned long read = fread(buffer, sizeof(char), len, fp);
    
    buffer[read] = '\0';
    
    fclose(fp); fp = NULL;

    return buffer;
}

void write_string_to_file(const char* filename, const char* string)
{
    if (!valid_string(filename) || !valid_string(string)) 
        return;

    FILE* fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "write_string_to_file: fopen returned null\n");
        return;
    }

    const unsigned long len = strlen(string);
    
    fwrite(string, sizeof(char), len, fp);

    fclose(fp); fp = NULL;
}

int filter_non_numeric_chars(char* string, const size_t string_size)
{
    if (!valid_string(string)) 
        return -1;

    int i = 0;
    for (int j = 0; j < string_size; j++) {
        const char c = string[j];
        if (isdigit(c)) 
            string[i++] = string[j];
    }

    string[i] = '\0';   

    return i;
}

size_t trim_whitespace(char* string)
{
    if (!valid_string(string)) 
        return 0;
    
    char* start = string;
    while (isspace((unsigned char) *start)) 
        start++;

    char* end = string + strlen(string) - 1;
    while (isspace((unsigned char) *end)) 
        end--;

    size_t len = end < start ? 0 : end - start + 1;
    memmove(string, start, len);
    
    string[len] = '\0';

    return len;
}

bool valid_string(const char* string)
{
    return (string) && (string[0] != '\0');
}

bool connected_to_internet()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) 
        return false;
    
    struct sockaddr_in server = {
        .sin_family = AF_INET,
        .sin_port = htons(53), 
        .sin_addr.s_addr = inet_addr("8.8.8.8") 
    };
    
    bool connected = (connect(sock, (struct sockaddr*)&server, sizeof(server)) == 0);

    close(sock);
    
    return connected;
}

char** text_split(const char* text, const char delim, int* count, char** copy_out)
{
    if (!valid_string(text) || (delim == '\0') || !count) 
        return NULL;

    (*copy_out) = strdup(text);
    if ((*copy_out) == NULL) {
        fprintf(stderr, "text_split: strdup returned null\n");
        *count = 0;
        return NULL;
    }

    char* copy = (*copy_out);

    (*count) = 1;
    for(const char* p = copy; *p; p++) {
        if (*p == delim) 
            (*count)++;
    }
    
    char** tokens = malloc((*count) * sizeof(char*));
    if (tokens == NULL) {
        fprintf(stderr, "text_split: malloc returned null\n");
        *count = 0;
        return NULL;
    }

    memset(tokens, 0, (*count) * sizeof(char*));

    int i = 0;
    tokens[i++] = copy;
    while ((copy = strchr(copy, delim))) {
        *(copy++) = '\0';
        if (i < (*count)) 
            tokens[i++] = copy;
    }

    return tokens;
}

void format_view_count(char* dest, const size_t dest_size)
{
    if (!dest || (filter_non_numeric_chars(dest, dest_size) <= 0)) 
        return;

    const float view_count = strtof(dest, NULL);

    if      (view_count < 1e3)  snprintf(dest, dest_size, "%.0f  views", view_count);         // 0               - 999
    else if (view_count < 1e4)  snprintf(dest, dest_size, "%.2fk views", (view_count / 1e3)); // 1,000           - 9,999
    else if (view_count < 1e5)  snprintf(dest, dest_size, "%.1fk views", (view_count / 1e3)); // 10,000          - 99,999
    else if (view_count < 1e6)  snprintf(dest, dest_size, "%.0fk views", (view_count / 1e3)); // 100,000         - 999,999
    else if (view_count < 1e7)  snprintf(dest, dest_size, "%.2fM views", (view_count / 1e6)); // 1,000,000       - 9,999,999
    else if (view_count < 1e8)  snprintf(dest, dest_size, "%.1fM views", (view_count / 1e6)); // 10,000,000      - 99,999,999
    else if (view_count < 1e9)  snprintf(dest, dest_size, "%.0fM views", (view_count / 1e6)); // 100,000,000     - 999,999,999
    else if (view_count < 1e10) snprintf(dest, dest_size, "%.2fB views", (view_count / 1e9)); // 1,000,000,000   - 9,999,999,999
    else if (view_count < 1e11) snprintf(dest, dest_size, "%.1fB views", (view_count / 1e9)); // 10,000,000,000  - 99,999,999,999
    else if (view_count < 1e12) snprintf(dest, dest_size, "%.0fB views", (view_count / 1e9)); // 100,000,000,000 - 999,999,999,999
}