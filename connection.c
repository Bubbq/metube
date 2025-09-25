#include "include/connection.h"

#include "include/utils.h"

#include <netdb.h>
#include <unistd.h>

void connection_init (Connection * connection, const char * host, const char * port)
{
    if ( !connection || !valid_string(host) || !valid_string(port)) 
        return ;
    
    connection->ssl = NULL ;
    connection->sockfd = -1 ; 
    connection->connected = false ;
    connection->address_information = NULL ;
    pthread_mutex_init(&connection->mutex, NULL) ;
    strncpy(connection->host, host, sizeof(connection->host) - 1) ;
    strncpy(connection->port, port, sizeof(connection->port) - 1) ;
}

void connection_free (Connection * connection)
{
    if ( !connection) 
        return ;

    disconnect(connection) ;
    pthread_mutex_destroy(&connection->mutex) ;
}

bool connection_establish (Connection * connection, SSL_CTX * ssl_ctx)
{
    if ( !connection || !ssl_ctx)
        return false ;

    disconnect(connection) ;

    struct addrinfo addr = {0} ;
    addr.ai_family = AF_INET ;
    addr.ai_socktype = SOCK_STREAM ;

    if (getaddrinfo(connection->host, connection->port, &addr, &connection->address_information) != 0) {
        fprintf(stderr, "establish_persistent_connection: getaddrinfo failed") ;
        return false ;
    }

    connection->sockfd = socket(connection->address_information->ai_family, connection->address_information->ai_socktype, connection->address_information->ai_protocol) ;
    if (connection->sockfd < 0) {
        fprintf(stderr, "establish_persistent_connection: socket() failed\n") ;
        disconnect(connection) ;
        return false ;
    }

    if (connect(connection->sockfd, connection->address_information->ai_addr, connection->address_information->ai_addrlen) != 0) {
        fprintf(stderr, "establish_persistent_connection: connect() failed\n") ;
        disconnect(connection) ;
        return false ;
    }

    connection->ssl = SSL_new(ssl_ctx) ;
    if (connection->ssl == NULL) {
        fprintf(stderr, "establish_persistent_connection: SSL_new() returned null\n") ;
        disconnect(connection) ;
        return false ;
    }

    SSL_set_fd(connection->ssl, connection->sockfd) ;
    if (SSL_connect(connection->ssl) != 1) {
        fprintf(stderr, "establish_persistent_connection: SSL_connect failed for host %s\n", connection->host) ;
        disconnect(connection) ;
        return false ;
    }

    return true ;
}

void disconnect (Connection * connection)
{
    if ( !connection) 
        return ;

    if (connection->address_information) {
        freeaddrinfo(connection->address_information) ; connection->address_information = NULL ;
    }

    if (connection->ssl) {
        SSL_shutdown(connection->ssl) ;
        SSL_free(connection->ssl) ; connection->ssl = NULL ;
    }

    if (connection->sockfd >= 0) {
        close(connection->sockfd) ; connection->sockfd = -1 ;
    }

    connection->connected = false ;
}

bool connection_pool_init (ConnectionPool * pool, const char * host, const char * port, const size_t n_conn)
{
    if ( !pool || !valid_string(host) || !valid_string(port))
        return false ;

    pool->nconn = n_conn ;
    pool->current_conn = 0 ;
    pool->connections = malloc(n_conn * sizeof(Connection)) ;
    if (!pool->connections) {
        fprintf(stderr, "connection_pool_init: malloc returned null\n") ;
        return false ;
    }

    for (size_t c = 0; c < n_conn; c++) 
        connection_init(&pool->connections[c], host, port) ;
    
    pthread_mutex_init(&pool->mutex, NULL) ;
    
    return true ;
}

void connection_pool_free (ConnectionPool * pool)
{
    if ( !pool) 
        return ;

    for(size_t c = 0; c < pool->nconn; c++) 
        connection_free(&pool->connections[c]) ;
    
    free(pool->connections) ; pool->connections = NULL ; 

    pthread_mutex_destroy(&pool->mutex) ;
}

static void cycle_connection (ConnectionPool * pool)
{
    if ( !pool) 
        return ;

    pool->current_conn = bound_index_to_array((pool->current_conn + 1), pool->nconn) ;
}

Connection * connection_pool_get_current_conn (ConnectionPool * pool) 
{
    if ( !pool)
        return NULL ;
    
    pthread_mutex_lock(&pool->mutex) ;

    Connection * conn = &pool->connections[pool->current_conn] ;
    
    cycle_connection(pool) ;
    
    pthread_mutex_unlock(&pool->mutex) ;

    return conn ;
}

bool client_context_init (ClientContext * client_context, const size_t nconns, const char * host, const char * api_key)
{
    if ( !client_context || !api_key)
        return false ;

    client_context->continuation_token = NULL ;
    client_context->api_key = api_key ;

    if ( !connection_pool_init(&client_context->conn_pool, host, HTTPS_PORT, nconns))
        return false ;

    pthread_mutex_init(&client_context->token_mutex, NULL) ;

    return true ;
}

void client_context_free (ClientContext * client)
{
    if ( !client)
        return ;

    connection_pool_free(&client->conn_pool) ;

    if (client->continuation_token) {
        free(client->continuation_token) ; client->continuation_token = NULL ;
    }

    pthread_mutex_destroy(&client->token_mutex) ;
}