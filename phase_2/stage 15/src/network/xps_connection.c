#include "../xps.h"
#include "xps_listener.h"
#include "xps_connection.h"

xps_connection_t *xps_connection_create(xps_core_t *core, u_int sock_fd) {

  xps_connection_t *connection = malloc(sizeof(xps_connection_t));
  if (connection == NULL) {
    logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'connection'");
    return NULL;
  }

  xps_pipe_source_t* source = xps_pipe_source_create(
    connection,
    connection_source_handler,
    connection_source_close_handler);
    if (source == NULL) {
    logger(LOG_ERROR, "xps_connection_create()", "xps_pipe_source_create() failed");
    free(connection);
    return NULL;
    }
  
  xps_pipe_sink_t * sink = xps_pipe_sink_create(
    connection,
    connection_sink_handler,
    connection_sink_close_handler
  );
  if (sink == NULL) {
    logger(LOG_ERROR, "xps_connection_create()", "xps_pipe_sink_create() failed");
    xps_pipe_source_destroy(source);
    free(connection);
    return NULL;
  }

  /* attach sock_fd to epoll */
  xps_loop_attach(
    core->loop,
    sock_fd,
    EPOLLIN | EPOLLOUT | EPOLLET ,
    connection,
    connection_loop_read_handler,
    connection_loop_write_handler,
    connection_loop_close_handler
  );

  // Init values
  connection->core = core;
  connection->sock_fd = sock_fd;
  connection->listener = NULL;
  connection->remote_ip = get_remote_ip(sock_fd);
  connection->source = source;
  connection->sink = sink;
  /* add connection to 'connections' list */
	vec_push(&core->connections, connection);

  logger(LOG_DEBUG, "xps_connection_create()", "created connection");
  return connection;

}

void xps_connection_destroy(xps_connection_t *connection) {

  /* validate params */
	assert(connection != NULL);

  /* set connection to NULL in 'connections' list */
	for (int i = 0; i < connection->core->connections.length; i++) {
    xps_connection_t *curr = connection->core->connections.data[i];
    if (curr == connection) {
      connection->core->connections.data[i] = NULL;
      connection->core->n_null_connections++;
      break;
    }
  }
  xps_loop_detach(connection->core->loop, connection->sock_fd);
  xps_pipe_source_destroy(connection->source);
  xps_pipe_sink_destroy(connection->sink);

  /* close connection socket FD */
	close(connection->sock_fd);

  /* free connection->remote_ip */
	free(connection->remote_ip);

	/* free connection instance */
	free(connection);

  logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");

}

void connection_loop_read_handler(void *ptr){
  assert(ptr!=NULL);
  xps_connection_t * connection = ptr;
  connection->source->ready=true;
}

void connection_loop_write_handler(void* ptr){
  assert(ptr!=NULL);
  xps_connection_t * connection = ptr;
  connection->sink->ready = true;
}

void connection_loop_close_handler(void* ptr){
    xps_connection_t *connection = (xps_connection_t *)ptr;
    assert(connection != NULL);
    connection_close(connection, true);
    logger(LOG_INFO, "connection_loop_close_handler()", "connection closed");
}

void connection_source_handler(void* ptr) {

  /* validate params */
	assert(ptr != NULL);
  xps_pipe_source_t *source = (xps_pipe_source_t *) ptr;
  xps_connection_t *connection = source->ptr;
	
  xps_buffer_t * buff = xps_buffer_create(DEFAULT_BUFFER_SIZE, 0, NULL);

	/* read data from client using recv() */
  long read_n = recv(connection->sock_fd, buff->data, DEFAULT_BUFFER_SIZE , 0);
  buff->len = read_n;

  if (read_n < 0) {
    //blocking socket
    if(errno == EAGAIN || errno == EWOULDBLOCK){
      xps_buffer_destroy(buff);
      connection->source->ready = false;
      return;
    }
    else{
      //socket error
      xps_buffer_destroy(buff);
      logger(LOG_ERROR, "connection_source_handler()", "recv() failed");
      connection_close(connection, false);
      return;
    }
  }
  //peer closed connection
  if (read_n == 0) {
    xps_buffer_destroy(buff);
    connection_close(connection, false);
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
    xps_pipe_source_t *source = (xps_pipe_source_t *)ptr;
    xps_connection_t *connection = (xps_connection_t *)source->ptr;

    if (!source->active &&
        (source->pipe == NULL || source->pipe->sink == NULL || !source->pipe->sink->active)) {
        connection_close(connection, false);
    }
}

void connection_sink_handler(void* ptr){
    assert(ptr != NULL);
    xps_pipe_sink_t * sink = ptr;
    xps_connection_t *connection = (xps_connection_t *) (sink->ptr);


    xps_buffer_t *buff = xps_pipe_sink_read(sink,sink->pipe->buff_list->len);

    if(buff == NULL) {
        logger(LOG_ERROR, "connection_write_handler()", "xps_buffer_list_read() failed");
        return;
    }

    long write_n = send(connection->sock_fd, buff->data, buff->len, MSG_NOSIGNAL);
    xps_buffer_destroy(buff);

    // Socket would block
    if (write_n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    sink->ready=false;
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
    xps_pipe_sink_t *sink = (xps_pipe_sink_t *)ptr;
    xps_connection_t *connection = (xps_connection_t *)sink->ptr;

    if (!sink->active && (sink->pipe->source == NULL || !sink->pipe->source->active)) {
        connection_close(connection, false);
    }
}

void connection_close(xps_connection_t *connection, bool peer_closed) {
    assert(connection!=NULL);

    logger(LOG_INFO, "connection_close()",
            peer_closed ? "peer closed connection" : "closing connection");
    /*destroy connection*/
    xps_connection_destroy(connection);
}



void strrev(char *str) {
    int len = strlen(str);
    int start = 0;
    int end = len - 1;
    
    // Check if last char is newline
    if (len > 0 && str[end] == '\n') {
        end--;  // Don't reverse the newline
    }

    while (start < end) {
        char temp = str[start];
        str[start++] = str[end];
        str[end--] = temp;
    }
}