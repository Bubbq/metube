#include "include/utils.h"

#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

void timer_start(Timer* timer, const size_t lifetime) 
{
    if (!timer) 
        return;

    timer->start_time = (size_t) time(NULL);
	timer->lifetime = lifetime;
}

bool timer_is_done(Timer timer)
{ 
    const size_t seconds_elapsed = (size_t) (time(NULL) - timer.start_time);
    
    return (seconds_elapsed >= timer.lifetime);
} 

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

int hms_to_seconds (const char * hms)
{
    if ( !hms)
        return -1 ;

    int colon_count = 0 ;
    for (const char * c_ptr = hms ; (*c_ptr) != '\0' ; c_ptr++) 
        if ((*c_ptr) == ':')
            colon_count++ ;
    
    int hours = 0, minutes = 0, seconds = 0 ;

    if (colon_count == 2) 
        sscanf(hms, "%d:%d:%d", &hours, &minutes, &seconds) ;

    else if (colon_count == 1) 
        sscanf(hms, "%d:%d", &minutes, &seconds) ;

    else {
        fprintf(stderr, "extract_video_duration: %s is not in HH:MM:SS format\n", hms) ;
        return -1 ;
    }

    return (hours * 3600) + (minutes * 60) + seconds ;
}

bool array_contains_object (const void * array, const size_t nmemb, const size_t element_size, const void * object, const size_t object_size)
{
    if ( !array || !object || (element_size != object_size))
        return false ;

    size_t i  = 0 ;

    for (char * ptr = ((char*) array); (i < nmemb); i++, ptr += object_size) 
        if (memcmp(object, ptr, object_size) == 0)
            return true ;
    
    return false ;
}

bool enum_is_valid (const int enumeration, const size_t ne_memb)
{
    return (0 <= enumeration) && (enumeration < ne_memb) ;
}