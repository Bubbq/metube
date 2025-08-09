#include "utils.h"
#include "connection.h"

#include <stdio.h>
#include <unistd.h>

void connection_init(Connection* connection, const char* host, const char* port)
{
    if ((connection == NULL) || (valid_string(host) == false) || (valid_string(port) == false)) return;
    
    connection->ssl = NULL;
    connection->sockfd = -1; 
    connection->connected = false;
    connection->address_information = NULL;
    pthread_mutex_init(&connection->mutex, NULL);
    snprintf(connection->host, sizeof(connection->host), "%s", host);
    snprintf(connection->port, sizeof(connection->port), "%s", port);
}

void connection_free(Connection* connection)
{
    if (connection == NULL) return;

    disconnect(connection);
    pthread_mutex_destroy(&connection->mutex);
}

bool connection_establish(Connection* connection, SSL_CTX* ssl_ctx)
{
    if ((connection == NULL) || (ssl_ctx == NULL)) return false;

    if (connection->connected) disconnect(connection);

    struct addrinfo addr = {0};
    addr.ai_family = AF_INET;
    addr.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(connection->host, connection->port, &addr, &connection->address_information) != 0) {
        fprintf(stderr, "establish_persistent_connection: getaddrinfo failed");
        return false;
    }

    connection->sockfd = socket(connection->address_information->ai_family, connection->address_information->ai_socktype, connection->address_information->ai_protocol);
    if (connection->sockfd < 0) {
        fprintf(stderr, "establish_persistent_connection: socket() failed\n");
        disconnect(connection);
        return false;
    }

    if (connect(connection->sockfd, connection->address_information->ai_addr, connection->address_information->ai_addrlen) != 0) {
        fprintf(stderr, "establish_persistent_connection: connect() failed\n");
        disconnect(connection);
        return false;
    }

    connection->ssl = SSL_new(ssl_ctx);
    if (connection->ssl == NULL) {
        fprintf(stderr, "establish_persistent_connection: SSL_new() returned null\n");
        disconnect(connection);
        return false;
    }

    SSL_set_fd(connection->ssl, connection->sockfd);
    if (SSL_connect(connection->ssl) != 1) {
        fprintf(stderr, "establish_persistent_connection: SSL_connect failed for host %s\n", connection->host);
        disconnect(connection);
        return false;
    }

    return true;
}

void disconnect(Connection* connection)
{
    if (connection == NULL) return;

    if (connection->address_information) {
        freeaddrinfo(connection->address_information); connection->address_information = NULL;
    }

    if (connection->ssl) {
        SSL_shutdown(connection->ssl);
        SSL_free(connection->ssl); connection->ssl = NULL;
    }

    if (connection->sockfd >= 0) {
        close(connection->sockfd); connection->sockfd = -1;
    }

    connection->connected = false;
}

ConnectionPool connection_pool_init(const char* host, const char* port, const size_t n_conn)
{
    if ((valid_string(host) == false) || (valid_string(port) == false)) return (ConnectionPool){0};

    ConnectionPool pool = {
        .nconn = n_conn,
        .current_conn = 0,
        .connections = malloc(n_conn * sizeof(Connection)),
    };

    if (pool.connections == NULL) {
        fprintf(stderr, "connection_pool_init: malloc returned null\n");
        exit(EXIT_FAILURE);
    }

    for (size_t c = 0; c < n_conn; c++) {
        connection_init(&pool.connections[c], host, port);
    } 
    
    return pool;
}

void connection_pool_free(ConnectionPool* pool)
{
    if (pool == NULL) return;

    for(size_t c = 0; c < pool->nconn; c++) {
        connection_free(&pool->connections[c]);
    }
    
    free(pool->connections); pool->connections = NULL;
}

void cycle_connection(ConnectionPool* pool)
{
    if (pool == NULL) return;

    pool->current_conn = bound_index_to_array((pool->current_conn + 1), pool->nconn);
}