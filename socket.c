#include <cjson/cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

typedef struct {
    int size;
    char* memory;
} MemoryBlock;

MemoryBlock create_memory_block(size_t inital)
{
    MemoryBlock chunk = { 0 };

    chunk.memory = malloc(inital);
    if (!chunk.memory) printf("create_memory_block: not enough memory, malloc returned NULL\n");
    else chunk.size = inital;
    
    return chunk;
}

void unload_memory_block(MemoryBlock* chunk)
{
    if (chunk->memory) free(chunk->memory);
    chunk->memory = NULL;
    chunk->size = 0;
}

// return the number of bytes written from src to a memory block
size_t write_data_to_memory_block(void* src, const size_t nbytes, MemoryBlock* chunk)
{
    // allocate space to hold new data
    char* new_memory = realloc(chunk->memory, (chunk->size + nbytes + 1));
    if (!new_memory) {
        printf("write_data_to_memory_block: not enough memory, realloc returned null\n");
        return 0;
    }

    // update params
    chunk->memory = new_memory;
    memcpy(&chunk->memory[chunk->size], src, nbytes);
    chunk->size += nbytes;
    chunk->memory[chunk->size] = '\0';

    return nbytes;
}

void create_file_from_memory(const char* filename, const char* memory) 
{
    FILE* fp = fopen(filename, "w");
    if (!fp) printf("could not write memory into \"%s\"\n", filename);
    else {
        fprintf(fp, "%s", memory);
        fclose(fp);
    } 
}
bool is_memory_ready(const MemoryBlock chunk)
{
    return ((chunk.size > 0) && (chunk.memory != NULL));
}

MemoryBlock GET_request(const char *host, const char *path, const char *port) 
{
    // specify the type of address info you want and store the result
    struct addrinfo desired_addr_info = {0}, *result;
    desired_addr_info.ai_family = AF_UNSPEC;
    desired_addr_info.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &desired_addr_info, &result) != 0) {
        perror("getaddrinfo");
        return create_memory_block(0);
    }
    
    // initializing socket
    int sockfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (connect(sockfd, result->ai_addr, result->ai_addrlen) != 0) {
        perror("connect");
        freeaddrinfo(result);
        close(sockfd);
        return create_memory_block(0);
    }

    // ssl setup
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sockfd);
    if (SSL_connect(ssl) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(sockfd);
        freeaddrinfo(result);
        return create_memory_block(0);
    }

    // constructing and sending request
    char request[512];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "User-Agent: OpenSSL-Client\r\n"
             "\r\n",
             path, host);
    int write_status;
    if ((write_status = SSL_write(ssl, request, strlen(request))) <= 0) {
        SSL_get_error(ssl, write_status);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(sockfd);
        freeaddrinfo(result);
        return create_memory_block(0);
    } 

    // reading response
    char buffer[4096];
    int bytes;
    MemoryBlock ret = create_memory_block(0);
    // information is read in chunks, not as a whole
    while ((bytes = SSL_read(ssl, buffer, sizeof(buffer)-1)) > 0) {
        buffer[bytes] = '\0';
        const size_t size = (bytes * sizeof(char));
        write_data_to_memory_block(buffer, size, &ret);
    }

    // deinit
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(sockfd);
    freeaddrinfo(result);
    return ret;
}

char *extract_youtube_inital_data (char *html)
{
    // want the portion after var ytintialData
    char* itemSectionRenderer = "itemSectionRenderer";

    // ptr to first char that matches needle
    char* itemSectionRenderer_location = strstr(html, itemSectionRenderer);
    if (!itemSectionRenderer_location) {
        perror("strstr");
        return NULL;
    }

    // the desired data is enclosed in the first '{}' following the yt initalization
    char* start = strchr(itemSectionRenderer_location, '[');
    if (!start) {
        perror("strstr");
        return NULL;
    }

    int depth = 0;
    char *cptr = start;
    for ( ; cptr; cptr++) {
        if (*cptr == '[') depth++;
        else if (*cptr == ']') depth--;
        if (depth == 0) 
            break;
    }

    if (depth != 0) {
        printf("braces in HTML are unbalanced\n");
        return NULL;
    }

    char *end = cptr;

    // return a duplicate of this section of data found in the html
    const int nchars = end - start + 1; 
    char* youtube_search_data = malloc(nchars + 1);
    if (!youtube_search_data) return NULL;

    memcpy(youtube_search_data, start, nchars);
    youtube_search_data[nchars] = '\0'; 
    create_file_from_memory("youtube_search_data.html", youtube_search_data);
    return youtube_search_data;
}

int main()
{
    const char *host = "www.youtube.com";
    const char *port = "443";
    const char *path = "/results?search_query=test";
    MemoryBlock chunk = (GET_request(host, path, port));
    
    char *cjson_data = extract_youtube_inital_data(chunk.memory);
    free(cjson_data);
    unload_memory_block(&chunk);
    
    // cJSON *cjson = cJSON_Parse(cjson_data);
    // free(cjson_data);
    // if (!cjson) {
    //     printf("error w json\n");
    // }
    // cJSON_Delete(cjson);    
    return 0;    
}