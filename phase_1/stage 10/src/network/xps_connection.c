#include "../xps.h"
void connection_loop_read_handler(void *ptr);
void connection_loop_write_handler(void *ptr);
void connection_loop_close_handler(void *ptr);

void connection_source_handler(void *ptr);
void connection_source_close_handler(void *ptr);
void connection_sink_handler(void *ptr);
void connection_sink_close_handler(void *ptr);
void connection_close(xps_connection_t *connection, bool peer_closed);

void connection_loop_read_handler(void *ptr) {
    assert(ptr != NULL);
    xps_connection_t *connection = ptr;
    assert(connection != NULL);
    connection->source->ready = true;
}

void connection_loop_write_handler(void *ptr) {
    assert(ptr != NULL);
    xps_connection_t *connection = ptr;
    assert(connection != NULL);
    connection->sink->ready = true;
}

void connection_loop_close_handler(void *ptr) {
    assert(ptr != NULL);
    xps_connection_t *connection = ptr;
    assert(connection != NULL);
    connection_close(connection, true);
}

/*
 * Create connection
 */
xps_connection_t *xps_connection_create(xps_core_t *core, u_int sock_fd) {
    assert(core != NULL);
    xps_connection_t *connection = malloc(sizeof(xps_connection_t));
    if (connection == NULL) {
        logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'connection'");
        return NULL;
    }
    connection->core = core;
    connection->sock_fd = sock_fd;
    connection->listener = NULL;
    connection->remote_ip = get_remote_ip(sock_fd);
    /*
     * Create source
     */
    xps_pipe_source_t *source = xps_pipe_source_create(connection, connection_source_handler,connection_source_close_handler);

    if (source == NULL) {
        logger(LOG_ERROR, "xps_connection_create()", "xps_pipe_source_create() failed");
        free(connection->remote_ip);
        free(connection);
        return NULL;
    }

    /*
     * Create sink
     */
    xps_pipe_sink_t *sink = xps_pipe_sink_create(connection, connection_sink_handler,connection_sink_close_handler);
    if (sink == NULL) {
        logger(LOG_ERROR, "xps_connection_create()", "xps_pipe_sink_create() failed");
        xps_pipe_source_destroy(source);
        free(connection->remote_ip);
        free(connection);
        return NULL;
    }
    connection->source = source;
    connection->sink = sink;

    int ret = xps_loop_attach(core->loop, sock_fd, EPOLLIN | EPOLLOUT | EPOLLET, connection,connection_loop_read_handler, connection_loop_write_handler,connection_loop_close_handler);
    if (ret != OK) {
        logger(LOG_ERROR, "xps_connection_create()", "xps_loop_attach() failed");
        xps_pipe_source_destroy(source);
        xps_pipe_sink_destroy(sink);
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
     * Destroy source/sink (this also detaches them from any pipe).
     */
    if (connection->source != NULL) {
        xps_pipe_source_destroy(connection->source);
    }
    if (connection->sink != NULL) {
        xps_pipe_sink_destroy(connection->sink);
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
 * Source handler
 *
 * Reads from the socket and writes into the pipe. The amount read is
 * capped by the remaining free space in the pipe's buffer, so we never
 * write past 'buff_thresh'.
 */
void connection_source_handler(void *ptr) {
    assert(ptr != NULL);
    xps_pipe_source_t *source = ptr;
    xps_connection_t *connection = source->ptr;
    u_char *data = malloc(DEFAULT_BUFFER_SIZE);
    if (data == NULL) {
        logger(LOG_ERROR, "connection_source_handler()", "malloc() failed for 'data'");
        return;
    }
    xps_buffer_t *buff = xps_buffer_create(DEFAULT_BUFFER_SIZE, 0, data);
    if (buff == NULL) {
        logger(LOG_DEBUG, "connection_source_handler()", "xps_buffer_create() failed");
        free(data);
        return;
    }
    /*
     * Cap the recv() length to the remaining free space in the pipe.
     */
    xps_pipe_t *pipe = source->pipe;
    size_t avail_pipe_space = pipe->buff_thresh - pipe->buff_list->len;
    size_t recv_len = DEFAULT_BUFFER_SIZE < avail_pipe_space ? DEFAULT_BUFFER_SIZE : avail_pipe_space;
    int read_n = recv(connection->sock_fd, buff->data, recv_len, 0);
    buff->len = read_n;
    // Socket would block
    if (read_n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        xps_buffer_destroy(buff);
        source->ready = false;
        return;
    }
    // Socket error
    if (read_n < 0) {
        xps_buffer_destroy(buff);
        logger(LOG_ERROR, "connection_source_handler()", "recv() failed");
        connection_close(connection, false);
        return;
    }
    // Peer closed connection
    if (read_n == 0) {
        xps_buffer_destroy(buff);
        connection_close(connection, true);
        return;
    }
    if (xps_pipe_source_write(source, buff) != OK) {
        logger(LOG_ERROR, "connection_source_handler()", "xps_pipe_source_write() failed");
        xps_buffer_destroy(buff);
        connection_close(connection, false);
        return;
    }
    xps_buffer_destroy(buff);
}

void connection_source_close_handler(void *ptr) {
    assert(ptr != NULL);
    xps_pipe_source_t *source = ptr;
    xps_connection_t *connection = source->ptr;
    if (!connection->source->active && !connection->sink->active)
        connection_close(connection, false);
}

/*
 * Sink handler
 *
 * Reads all currently available data from the pipe and sends it to the
 * socket. Only the bytes actually accepted by send() are cleared from
 * the pipe's buffer.
 */
void connection_sink_handler(void *ptr) {
    assert(ptr != NULL);
    xps_pipe_sink_t *sink = ptr;
    xps_connection_t *connection = sink->ptr;
    xps_buffer_t *buff = xps_pipe_sink_read(sink, sink->pipe->buff_list->len);
    if (buff == NULL) {
        logger(LOG_ERROR, "connection_sink_handler()", "xps_pipe_sink_read() failed");
        return;
    }

    // Write to socket
    int write_n = send(connection->sock_fd, buff->data, buff->len, MSG_NOSIGNAL);
    xps_buffer_destroy(buff);
    // Socket would block
    if (write_n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        sink->ready = false;
        return;
    }
    // Socket error
    if (write_n < 0) {
        logger(LOG_ERROR, "connection_sink_handler()", "send() failed");
        connection_close(connection, false);
        return;
    }
    if (write_n == 0)
        return;
    if (xps_pipe_sink_clear(sink, write_n) != OK)
        logger(LOG_ERROR, "connection_sink_handler()", "failed to clear %d bytes from sink", write_n);
}

void connection_sink_close_handler(void *ptr) {
    assert(ptr != NULL);
    xps_pipe_sink_t *sink = ptr;
    xps_connection_t *connection = sink->ptr;
    if (!connection->source->active && !connection->sink->active)
        connection_close(connection, false);
}

void connection_close(xps_connection_t *connection, bool peer_closed) {
    assert(connection != NULL);
    logger(LOG_INFO, "connection_close()",
           peer_closed ? "peer closed connection" : "closing connection");
    xps_connection_destroy(connection);
}