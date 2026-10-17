#include "xps_upstream.h"

xps_connection_t *xps_upstream_create(xps_core_t *core, const char *host, u_int port) {
  /* validate parameter */
  assert(core!=NULL);
  assert(host!=NULL);

  /* create a socket and connect to host and port to upstream using xps_getaddrinfo and connect function */
  u_int upstream_sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK , 0);
  struct addrinfo* info = xps_getaddrinfo(host,port);
  if (info == NULL) {
      logger(LOG_ERROR, "xps_upstream_create()", "xps_getaddrinfo() failed");
      close(upstream_sockfd);
      return NULL;
  }
  int connect_error = connect(upstream_sockfd, info->ai_addr, info->ai_addrlen);

  if (!(connect_error == 0 || errno == EINPROGRESS)) {
    logger(LOG_ERROR, "xps_upstream_create()", "connect() failed");
    perror("Error message");
    close(upstream_sockfd);
    return NULL;
  }
    freeaddrinfo(info);
 /* create a connection to upstream with core and sock_fd*/
    xps_connection_t* connection = xps_connection_create(core,upstream_sockfd);
    if(connection == NULL) {
        logger(LOG_ERROR, "xps_upstream_create()", "xps_connection_create() failed");
        close(upstream_sockfd);
        return NULL;
    }
    
    return connection;
}