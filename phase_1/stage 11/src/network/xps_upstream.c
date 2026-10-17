#include "../xps.h"

xps_connection_t *xps_upstream_create(xps_core_t *core, const char *host, u_int port) {
    assert(core != NULL);
    assert(host != NULL);
    assert(port > 0);

    struct addrinfo *addr_info = xps_getaddrinfo(host, port);

    if (addr_info == NULL) {
        logger(LOG_ERROR, "xps_upstream_create()", "xps_getaddrinfo() failed");
        return NULL;
    }

    int sock_fd = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);

    if (sock_fd < 0) {
        logger(LOG_ERROR, "xps_upstream_create()", "socket() failed");
        perror("socket");
        freeaddrinfo(addr_info);
        return NULL;
    }

    int ret = connect(sock_fd, addr_info->ai_addr, addr_info->ai_addrlen);

    if (ret != 0 && errno != EINPROGRESS) {
        logger(LOG_ERROR, "xps_upstream_create()", "connect() failed");
        perror("connect");
        close(sock_fd);
        freeaddrinfo(addr_info);
        return NULL;
    }

    freeaddrinfo(addr_info);

    return xps_connection_create(core, sock_fd);
}