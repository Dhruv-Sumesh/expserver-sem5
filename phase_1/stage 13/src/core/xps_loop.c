#include "../xps.h"

/**
 * Iterates through core->pipes, invoking source/sink handler callbacks
 * for pipes that are ready, and close callbacks for pipes whose source
 * or sink has been detached.
 *
 * A second pass (done separately, since the first pass's callbacks can
 * affect the readiness of previously-visited pipes) checks whether any
 * ready pipes remain.
 *
 * @param loop : loop whose core's pipes should be handled
 * @return : true if any pipe still needs immediate processing
 */
bool handle_pipes(xps_loop_t *loop) {
    assert(loop != NULL);

    for (int i = 0; i < loop->core->pipes.length; i++) {
        xps_pipe_t *pipe = loop->core->pipes.data[i];

        if (pipe == NULL)
            continue;

        // No source and no sink left -> pipe is dead, destroy it
        if (pipe->source == NULL && pipe->sink == NULL) {
            xps_pipe_destroy(pipe);
            continue;
        }

        if (pipe->source != NULL && pipe->source->ready && xps_pipe_is_writable(pipe))
            pipe->source->handler_cb(pipe->source);

        if (pipe->sink != NULL && pipe->sink->ready && xps_pipe_is_readable(pipe))
            pipe->sink->handler_cb(pipe->sink);

        if (pipe->source != NULL && pipe->sink == NULL) {
            pipe->source->active = false;
            pipe->source->close_cb(pipe->source);
        }

        if (pipe->sink != NULL && pipe->source == NULL && !xps_pipe_is_readable(pipe)) {
            pipe->sink->active = false;
            pipe->sink->close_cb(pipe->sink);
        }
    }

    for (int i = 0; i < loop->core->pipes.length; i++) {
        xps_pipe_t *pipe = loop->core->pipes.data[i];

        if (pipe == NULL) {
            logger(LOG_DEBUG, "handle_pipes", "pipe is null");
            continue;
        }

        if (pipe->source != NULL && pipe->source->ready && xps_pipe_is_writable(pipe))
            return true;

        if (pipe->sink != NULL && pipe->sink->ready && xps_pipe_is_readable(pipe))
            return true;

        if (pipe->source != NULL && pipe->sink == NULL)
            return true;

        if (pipe->sink != NULL && pipe->source == NULL && !xps_pipe_is_readable(pipe))
            return true;
    }

    return false;
}

/**
 * Filters out NULL entries from core's listeners/connections/pipes
 * lists once the number of NULLs accumulated in each crosses
 * DEFAULT_NULLS_THRESH.
 *
 * @param core : core whose lists should be filtered
 */
void filter_nulls(xps_core_t *core) {
    assert(core != NULL);

    if (core->n_null_listeners > DEFAULT_NULLS_THRESH) {
        vec_filter_null(&core->listeners);
        core->n_null_listeners = 0;
    }

    if (core->n_null_connections > DEFAULT_NULLS_THRESH) {
        vec_filter_null(&core->connections);
        core->n_null_connections = 0;
    }

    if (core->n_null_pipes > DEFAULT_NULLS_THRESH) {
        vec_filter_null(&core->pipes);
        core->n_null_pipes = 0;
    }

    if(core->n_null_sessions>DEFAULT_NULLS_THRESH){
        vec_filter_null(&core->sessions);
        core->n_null_sessions=0;
    }
    
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
/**
 * Iterates through the epoll events returned by epoll_wait() and
 * invokes the corresponding read/write/close callbacks. Re-checks that
 * the loop_event_t still exists before each callback, since a previous
 * callback (on the same or an earlier event) may have destroyed it.
 *
 * @param loop : loop whose events should be handled
 * @param n_events : number of epoll events returned by epoll_wait()
 */
void handle_epoll_events(xps_loop_t *loop, int n_events) {
    logger(LOG_DEBUG, "handle_epoll_events()", "handling %d events", n_events);

    for (int i = 0; i < n_events; i++) {
      logger(LOG_DEBUG, "handle_epoll_events()", "handling event no. %d", i + 1);

      struct epoll_event curr_epoll_event = loop->epoll_events[i];
      loop_event_t *curr_event = curr_epoll_event.data.ptr;

      // Check if event still exists. Could have been destroyed due to prev event
      int curr_event_idx = -1;
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
            logger(LOG_DEBUG,"handle_epoll_events()","EVENT / close");
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
            logger(LOG_DEBUG,"handle_epoll_events()","event destroyed after close callback");
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
            logger(LOG_DEBUG,"handle_epoll_events()","event destroyed after read callback");
            continue;
        }

        // Write event
        if (curr_epoll_event.events & EPOLLOUT) {

            logger(
                LOG_DEBUG,
                "handle_epoll_events()",
                "EVENT / write"
            );

            if (curr_event->write_cb != NULL) {
                curr_event->write_cb(curr_event->ptr);
            }
        }
    }
}

void xps_loop_run(xps_loop_t *loop) {
  assert(loop != NULL);

  logger(LOG_DEBUG, "xps_loop_run()", "starting to run loop");

  while (1) {
    logger(LOG_DEBUG, "xps_loop_run()", "loop top");

    // Handle pipes
    bool has_ready_pipes = handle_pipes(loop);

    // Non-blocking wait if pipes are ready, else block indefinitely
    int timeout = has_ready_pipes ? 0 : -1;

    logger(LOG_DEBUG, "xps_loop_run()", "epoll waiting");
    int n_events = epoll_wait(loop->epoll_fd, loop->epoll_events, MAX_EPOLL_EVENTS, timeout);
    logger(LOG_DEBUG, "xps_loop_run()", "epoll wait over");

    if (n_events<0){
        if (errno==EINTR){
            continue;
        }
        logger(LOG_ERROR, "xps_loop_run()", "epoll_wait() failed");
        return;
    }

    // Handle epoll events
    if (n_events > 0)
        handle_epoll_events(loop, n_events);

    // Filter NULLs from vec lists
    filter_nulls(loop->core);
  }
}