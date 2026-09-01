#include "../xps.h"
bool handle_connections(xps_loop_t* loop) {

    for (u_int i = 0; i < loop->core->connections.length; i++) {
        xps_connection_t* connection = loop->core->connections.data[i];

        if(connection==NULL){
            continue;
        }
        if (connection->read_ready == true){
            connection->recv_handler(connection);
            //check if connection still exists
            connection = loop->core->connections.data[i];
            if (connection == NULL){
                continue;
            }
        }

        if (connection->write_ready == true && connection->write_buff_list->len > 0){
            connection->send_handler(connection);
            //check if connection still exists
            connection = loop->core->connections.data[i];
            if (connection == NULL){
                continue;
            }
        }
    }

    for (u_int i = 0; i < loop->core->connections.length; i++) {
        xps_connection_t* connection = loop->core->connections.data[i];
        /*check if connection is NULL and continue if it is*/

        if(connection==NULL){
            continue;
        }

        if (connection->read_ready == true)
            return true;

        if (connection->write_ready == true && connection->write_buff_list->len > 0)
            return true;
    }

    return false;
}
loop_event_t *loop_event_create(u_int fd, void *ptr, xps_handler_t read_cb,xps_handler_t write_cb,xps_handler_t close_cb) {
  assert(ptr != NULL);

  // Alloc memory for 'event' instance
  loop_event_t *event = malloc(sizeof(loop_event_t));
  if (event == NULL) {
    logger(LOG_ERROR, "event_create()", "malloc() failed for 'event'");
    return NULL;
  }

  event->fd = fd;
  event->ptr = ptr;

  event->read_cb = read_cb;
  event->write_cb = write_cb;
  event->close_cb = close_cb;

  logger(LOG_DEBUG, "event_create()", "created event");

  return event;
}

void loop_event_destroy(loop_event_t *event) {
  assert(event != NULL);

  free(event);

  logger(LOG_DEBUG, "event_destroy()", "destroyed event");
}

/**
 * Creates a new event loop instance associated with the given core.
 *
 * This function creates an epoll file descriptor, allocates memory for the xps_loop instance,
 * and initializes its values.
 *
 * @param core : The core instance to which the loop belongs
 * @return A pointer to the newly created loop instance, or NULL on failure.
 */
xps_loop_t *xps_loop_create(xps_core_t *core)
{
    assert(core != NULL);

    /* Create epoll file descriptor */
    int epoll_fd = epoll_create1(0);

    if (epoll_fd == -1) {
        logger(LOG_ERROR, "loop_create()", "epoll_create1() failed");
        return NULL;
    }

    /* Allocate memory for 'loop' instance */
    xps_loop_t *loop = malloc(sizeof(xps_loop_t));

    if (loop == NULL) {
        logger(LOG_ERROR, "loop_create()", "malloc() failed for 'loop'");
        close(epoll_fd);
        return NULL;
    }

    /* Initialize loop fields */
    loop->core = core;
    loop->epoll_fd = epoll_fd;
    loop->n_null_events = 0;

    /* Initialize events vector */
    vec_init(&loop->events);

    logger(LOG_DEBUG, "loop_create()", "created loop");

    return loop;
}


/**
 * Destroys the given loop instance and releases associated resources.
 *
 * This function destroys all loop_event_t instances present in loop->events list,
 * closes the epoll file descriptor and releases memory allocated for the loop instance,
 *
 * @param loop The loop instance to be destroyed.
 */
void xps_loop_destroy(xps_loop_t *loop)
{
    assert(loop != NULL);

    /* Destroy all loop events */
    for (u_int i = 0; i < loop->events.length; i++) {

        loop_event_t *event = loop->events.data[i];

        if (event != NULL) {
            loop_event_destroy(event);
        }
    }

    /* Free events vector */
    vec_deinit(&loop->events);

    /* Close epoll file descriptor */
    close(loop->epoll_fd);

    /* Free loop */
    free(loop);

    logger(LOG_DEBUG, "loop_destroy()", "destroyed loop");
}


/**
 * Attaches a FD to be monitored using epoll
 *
 * The function creates an intance of loop_event_t and attaches it to epoll.
 * Add the pointer to loop_event_t to the events list in loop
 *
 * @param loop : loop to which FD should be attached
 * @param fd : FD to be attached to epoll
 * @param event_flags : epoll event flags
 * @param ptr : Pointer to instance of xps_listener_t or xps_connection_t
 * @param read_cb : Callback function to be called on a read event
 * @return : OK on success and E_FAIL on error
 */
int xps_loop_attach(xps_loop_t *loop,u_int fd,int event_flags,void *ptr,xps_handler_t read_cb,xps_handler_t write_cb,xps_handler_t close_cb)
{
    assert(loop != NULL);
    assert(ptr != NULL);

    /* Create loop event */
    loop_event_t *event = loop_event_create(fd, ptr, read_cb,write_cb,close_cb);

    if (event == NULL) {
        logger(LOG_ERROR, "loop_attach()",
               "loop_event_create() failed");
        return E_FAIL;
    }

    /* Setup epoll event */
    struct epoll_event epoll_event;

    epoll_event.events = event_flags;
    epoll_event.data.ptr = event;

    /* Attach FD to epoll */
    if (epoll_ctl(loop->epoll_fd,EPOLL_CTL_ADD,fd,&epoll_event) == -1) {

        logger(LOG_ERROR, "loop_attach()",
               "epoll_ctl() failed");

        loop_event_destroy(event);

        return E_FAIL;
    }

    /* Add event to vector */
    vec_push(&loop->events, event);

    logger(LOG_DEBUG, "loop_attach()",
           "attached fd to epoll");

    return OK;
}


/**
 * Remove FD from epoll
 *
 * Find the instance of loop_event_t from loop->events that matches fd param
 * and detach FD from epoll. Destroy the loop_event_t instance and set the pointer
 * to NULL in loop->events list. Increment loop->n_null_events.
 *
 * @param loop : loop instnace from which to detach fd
 * @param fd : FD to be detached
 * @return : OK on success and E_FAIL on error
 */
int xps_loop_detach(xps_loop_t *loop, u_int fd)
{
    assert(loop != NULL);

    /* Find matching event */
    for (u_int i = 0; i < loop->events.length; i++) {

        loop_event_t *event = loop->events.data[i];

        /* Skip NULL entries */
        if (event == NULL) {
            continue;
        }

        /* Check for matching FD */
        if (event->fd == fd) {

            /* Remove FD from epoll */
            if (epoll_ctl(loop->epoll_fd,EPOLL_CTL_DEL,fd,NULL) == -1) {

                logger(LOG_ERROR, "loop_detach()",
                       "epoll_ctl() failed");

                return E_FAIL;
            }

            /* Destroy event */
            loop_event_destroy(event);

            /* Set vector entry to NULL */
            loop->events.data[i] = NULL;

            /* Increment NULL event count */
            loop->n_null_events++;

            logger(LOG_DEBUG, "loop_detach()",
                   "detached fd from epoll");

            return OK;
        }
    }

    logger(LOG_ERROR, "loop_detach()",
           "event not found");

    return E_FAIL;
}
void xps_loop_run(xps_loop_t *loop) {
  /* Validate params */

  while (1) {
    logger(LOG_DEBUG, "xps_loop_run()", "epoll wait");
    bool has_ready_connections=handle_connections(loop);
    int timeout=has_ready_connections ? 0 : -1;
    int n_events=epoll_wait(loop->epoll_fd, loop->epoll_events, MAX_EPOLL_EVENTS, timeout);

    logger(LOG_DEBUG, "xps_loop_run()", "epoll wait over");

    if (n_events<0){
        if (errno==EINTR){
            continue;
        }
        logger(LOG_ERROR, "xps_loop_run()", "epoll_wait() failed");
        return;
    }

    logger(LOG_DEBUG, "xps_loop_run()", "handling %d events", n_events);

    // Handle events
    for (int i = 0; i < n_events; i++) {
      logger(LOG_DEBUG, "xps_loop_run()", "handling event no. %d", i + 1);

      struct epoll_event curr_epoll_event = loop->epoll_events[i];
      loop_event_t *curr_event = curr_epoll_event.data.ptr;

      // Check if event still exists. Could have been destroyed due to prev event
      int curr_event_idx = -1;/* search through loop->events and get index of curr_event, set it to -1 if not found */
      for (u_int j = 0; j < loop->events.length; j++) {
        if (loop->events.data[j] == curr_event) {
            curr_event_idx = j;
            break;
        }
      }
      // 🟡 Above can be optimized using an RB tree
      if (curr_event_idx == -1) {
        logger(LOG_DEBUG, "handle_epoll_events()", "event not found. skipping");
        continue;
      }

      //Close / Error event
      if (curr_epoll_event.events &(EPOLLERR | EPOLLHUP)){
            logger(LOG_DEBUG,"xps_loop_run()","EVENT / close");
            if (curr_event->close_cb != NULL) {
                curr_event->close_cb(curr_event->ptr);
            }
      }

      for (u_int j=0;j<loop->events.length;j++) {
            if (loop->events.data[j] == curr_event) {
                curr_event_idx = j;
                break;
            }
        }
        if(curr_event_idx == -1){
            logger(LOG_DEBUG,"xps_loop_run()","event destroyed after close callback");
            continue;
        }

      // Read event
      if (curr_epoll_event.events & EPOLLIN) {
        logger(LOG_DEBUG, "handle_epoll_events()", "EVENT / read");
        if (curr_event->read_cb != NULL)
          // Pass the ptr from loop_event_t as a parameter to the callback
          curr_event->read_cb(curr_event->ptr);
      }

      for (u_int j=0;j<loop->events.length;j++) {
            if (loop->events.data[j] == curr_event) {
                curr_event_idx = j;
                break;
            }
        }
        if(curr_event_idx == -1){
            logger(LOG_DEBUG,"xps_loop_run()","event destroyed after read callback");
            continue;
        }

        // Write event
        if (curr_epoll_event.events & EPOLLOUT) {

            logger(
                LOG_DEBUG,
                "xps_loop_run()",
                "EVENT / write"
            );

            if (curr_event->write_cb != NULL) {
                curr_event->write_cb(curr_event->ptr);
            }
        }
    }
  }
}