#include "xps_upstream.h"

xps_connection_t *xps_upstream_create(xps_core_t *core, const char *host, u_int port) {
    assert(core!=NULL);
    assert(host!=NULL);

    int upstream_sockfd=socket(AF_INET,SOCK_STREAM,0);
    if(upstream_sockfd<0){
        logger(LOG_ERROR,"xps_upstream_create()","socket() failed");
        return NULL;
    }

    struct addrinfo *info=xps_getaddrinfo(host,port);
    if(info==NULL){
        logger(LOG_ERROR,"xps_upstream_create()","xps_getaddrinfo() failed");
        close(upstream_sockfd);
        return NULL;
    }

    if(connect(upstream_sockfd,info->ai_addr,info->ai_addrlen)<0){
        logger(LOG_ERROR,"xps_upstream_create()","connect() failed");
        perror("Error message");
        freeaddrinfo(info);
        close(upstream_sockfd);
        return NULL;
    }
    freeaddrinfo(info);

    if(make_socket_non_blocking(upstream_sockfd)!=OK){
        close(upstream_sockfd);
        return NULL;
    }

    xps_connection_t *connection=xps_connection_create(core,upstream_sockfd);
    if(connection==NULL){
        close(upstream_sockfd);
        return NULL;
    }
    return connection;
}
