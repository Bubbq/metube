#ifndef CONNECTION_H
#define CONNECTION_H

#include <netdb.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <openssl/ssl.h>

#define HTTPS_PORT "443"

typedef struct {
    char host[64];
    pthread_mutex_t mutex;
    char port[16];
    struct addrinfo* address_information;
    SSL *ssl;
    int sockfd;
    bool connected;
} Connection;

void connection_init(Connection* connection, const char* host, const char* port);
void connection_free(Connection* connection);
bool connection_establish(Connection* connection, SSL_CTX* ssl_ctx);
void disconnect(Connection* connection);

typedef struct
{
    size_t nconn;
    size_t current_conn;
    Connection* connections;
} ConnectionPool;

ConnectionPool connection_pool_init(const char* host, const char* port, const size_t n_conn);
void connection_pool_free(ConnectionPool* pool);
void cycle_connection(ConnectionPool* pool);

#endif