#include "../xps.h"

void connection_loop_read_handler(void *ptr);
void connection_loop_write_handler(void *ptr);
void connection_loop_close_handler(void *ptr);

void connection_read_handler(void *ptr);
void connection_write_handler(void *ptr);
/*
 * Create connection
 */

void connection_loop_read_handler(void *ptr) {

    assert(ptr != NULL);

    xps_connection_t *connection = ptr;

    assert(connection != NULL);

    connection->read_ready = true;
}

void connection_loop_write_handler(void *ptr) {

    assert(ptr != NULL);

    xps_connection_t *connection = ptr;

    assert(connection != NULL);

    connection->write_ready = true;
}

xps_connection_t *xps_connection_create(xps_core_t *core, u_int sock_fd) {

    assert(core != NULL);

    xps_connection_t *connection = malloc(sizeof(xps_connection_t));

    if (connection == NULL) {
        logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'connection'");
        return NULL;
    }

    /*
     * Initialize connection BEFORE attaching to epoll.
     */

    connection->core = core;
    connection->sock_fd = sock_fd;
    connection->listener = NULL;
    connection->remote_ip = get_remote_ip(sock_fd);

    connection->write_buff_list = xps_buffer_list_create();

    if (connection->write_buff_list == NULL) {
        logger(LOG_ERROR, "xps_connection_create()", "xps_buffer_list_create() failed");

        free(connection->remote_ip);
        free(connection);

        return NULL;
    }

    /*
     * Stage 9 readiness flags.
     */
    connection->read_ready = false;
    connection->write_ready = false;

    /*
     * Actual I/O handlers.
     */
    connection->recv_handler = connection_read_handler;
    connection->send_handler = connection_write_handler;

    /*
     * Attach connection to epoll.
     *
     * EPOLLIN  -> incoming data
     * EPOLLOUT -> socket ready for writing
     */

    int ret = xps_loop_attach(core->loop, sock_fd, EPOLLIN | EPOLLOUT | EPOLLET, connection, connection_loop_read_handler, connection_loop_write_handler, connection_loop_close_handler);

    if (ret != OK) {
        logger(LOG_ERROR, "xps_connection_create()", "xps_loop_attach() failed");

        xps_buffer_list_destroy(connection->write_buff_list);

        free(connection->remote_ip);
        free(connection);

        return NULL;
    }

    /*
     * Add connection to core connection list.
     */

    vec_push(&core->connections, connection);

    logger(LOG_DEBUG, "xps_connection_create()", "created connection");

    return connection;
}


/*
 * Destroy connection
 */
void xps_connection_destroy(xps_connection_t *connection) {

    assert(connection != NULL);

    /*
     * Remove connection from core connection list.
     */

    for (int i = 0; i < connection->core->connections.length; i++) {

        xps_connection_t *curr = connection->core->connections.data[i];

        if (curr == connection) {
            connection->core->connections.data[i] = NULL;
            connection->core->n_null_connections++;
            break;
        }
    }

    /*
     * Remove from epoll.
     */

    xps_loop_detach(connection->core->loop, connection->sock_fd);

    /*
     * Close socket.
     */

    close(connection->sock_fd);

    /*
     * Destroy pending write buffers.
     */

    if (connection->write_buff_list != NULL) {
        xps_buffer_list_destroy(connection->write_buff_list);
    }

    /*
     * Free remote IP.
     */

    if (connection->remote_ip != NULL) {
        free(connection->remote_ip);
    }

    /*
     * Free connection.
     */

    free(connection);

    logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}


/*
 * Read handler
 */
void connection_read_handler(void *ptr) {
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;
    assert(connection != NULL);

    connection->read_ready = true;

    /*
     * With EPOLLET, keep reading until recv()
     * returns EAGAIN or EWOULDBLOCK.
     */
    while (1) {
        char buff[10000];

        long read_n = recv(connection->sock_fd, buff, sizeof(buff) - 1, 0);

        /*
         * recv() error
         */
        if (read_n < 0) {

            /*
             * Normal for non-blocking sockets.
             * Socket receive buffer is drained.
             */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                connection->read_ready = false;
                return;
            }

            logger(LOG_ERROR, "connection_loop_read_handler()", "recv() failed");
            perror("recv");

            xps_connection_destroy(connection);

            return;
        }

        /*
         * Client disconnected.
         */
        if (read_n == 0) {
            logger(LOG_INFO, "connection_loop_read_handler()", "peer closed connection");

            xps_connection_destroy(connection);

            return;
        }

        /*
         * Null terminate for logging.
         */
        buff[read_n] = '\0';

        logger(LOG_INFO, "connection_loop_read_handler()", "client message: %s", buff);

        /*
         * Reverse client message.
         */
        int end = read_n;

        /*
         * Preserve trailing newline.
         */
        if (end > 0 && buff[end - 1] == '\n') {
            end--;
        }

        for (int i = 0, j = end - 1; i < j; i++, j--) {
            char temp = buff[i];

            buff[i] = buff[j];
            buff[j] = temp;
        }

        /*
         * Allocate outgoing data.
         */
        u_char *data = malloc(read_n);

        if (data == NULL) {
            logger(LOG_ERROR, "connection_loop_read_handler()", "malloc() failed");

            xps_connection_destroy(connection);

            return;
        }

        /*
         * Copy reversed message.
         */
        memcpy(data, buff, read_n);

        /*
         * Create write buffer.
         */
        xps_buffer_t *write_buff = xps_buffer_create(read_n, read_n, data);

        if (write_buff == NULL) {
            logger(LOG_ERROR, "connection_loop_read_handler()", "xps_buffer_create() failed");

            free(data);

            xps_connection_destroy(connection);

            return;
        }

        /*
         * Add reversed data to write queue.
         */
        xps_buffer_list_append(connection->write_buff_list, write_buff);

        /*
         * Try writing immediately.
         */
        connection_loop_write_handler(connection);

        logger(LOG_DEBUG, "connection_loop_read_handler()", "reversed data queued for writing");

        /*
         * Do not return here.
         *
         * Continue reading until EAGAIN/EWOULDBLOCK.
         */
    }
}


/*
 * Write handler
 */
void connection_write_handler(void *ptr) {
    assert(ptr != NULL);

    xps_connection_t *connection = ptr;

    assert(connection != NULL);

    connection->write_ready = true;

    xps_buffer_list_t *write_buff_list = connection->write_buff_list;

    assert(write_buff_list != NULL);

    /*
     * Nothing to write.
     */

    if (write_buff_list->len == 0) {
        return;
    }

    logger(LOG_DEBUG, "connection_loop_write_handler()", "write handler called, buffer length: %d", write_buff_list->len);

    /*
     * Read pending data into a temporary buffer.
     */

    xps_buffer_t *buff = xps_buffer_list_read(write_buff_list, write_buff_list->len);

    if (buff == NULL) {
        logger(LOG_ERROR, "connection_loop_write_handler()", "xps_buffer_list_read() failed");
        return;
    }

    /*
     * Send data.
     */

    long write_n = send(connection->sock_fd, buff->data, buff->len, 0);

    /*
     * send() error.
     */

    if (write_n < 0) {

        xps_buffer_destroy(buff);

        /*
         * Socket cannot currently accept more data.
         */

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            connection->write_ready = false;
            return;
        }

        /*
         * Fatal socket error.
         */

        logger(LOG_ERROR, "connection_loop_write_handler()", "send() failed");

        perror("send");

        xps_connection_destroy(connection);

        return;
    }

    logger(LOG_DEBUG, "connection_loop_write_handler()", "send() wrote %ld bytes", write_n);

    /*
     * Remove only successfully sent bytes.
     */

    if (write_n > 0) {

        int ret = xps_buffer_list_clear(write_buff_list, (size_t)write_n);

        if (ret != OK) {

            logger(LOG_ERROR, "connection_loop_write_handler()", "xps_buffer_list_clear() failed");

            xps_buffer_destroy(buff);

            xps_connection_destroy(connection);

            return;
        }
    }

    /*
     * Free temporary buffer.
     */

    xps_buffer_destroy(buff);
}

/*
 * Close handler
 */
void connection_loop_close_handler(void *ptr) {

    assert(ptr != NULL);

    xps_connection_t *connection = ptr;

    assert(connection != NULL);

    xps_connection_destroy(connection);
}