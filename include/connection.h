#ifndef CONNECTION_H
#define CONNECTION_H

#include <stdbool.h>
#include <openssl/ssl.h>

#define HTTPS_PORT "443"

typedef struct {
    char host[64] ;
    pthread_mutex_t mutex ;
    char port[8] ;
    struct addrinfo * address_information ;
    SSL * ssl ;
    int sockfd ;
    bool connected ;
} Connection ;

void connection_init      (Connection * connection, const char * host, const char * port) ;
bool connection_establish (Connection * connection, SSL_CTX * ssl_ctx) ;
void connection_free      (Connection * connection) ;
void disconnect           (Connection * connection) ;

typedef struct
{
    pthread_mutex_t mutex ; 
    size_t nconn ;
    size_t current_conn ;
    Connection * connections ;
} ConnectionPool ;

bool connection_pool_init (ConnectionPool * pool, const char * host, const char * port, const size_t n_conn) ;
void connection_pool_free (ConnectionPool * pool) ;
Connection * connection_pool_get_current_conn (ConnectionPool * pool) ;

typedef struct
{
    pthread_mutex_t token_mutex ; 
    ConnectionPool conn_pool ;
    char * continuation_token ;
    const char * api_key ;
} ClientContext ;

bool client_context_init (ClientContext * client_context, const size_t nconns, const char * host, const char * api_key) ;
void client_context_free (ClientContext * client) ;

#endif