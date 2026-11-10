#include "../xps.h"
#include "xps_loop.h"


bool valid_event(xps_loop_t *loop, loop_event_t *event) {
    for (int j = 0; j < loop->events.length; j++) {
        if (loop->events.data[j] == event)
            return 1;
    }
    return 0;
}

loop_event_t *loop_event_create(u_int fd, void *ptr, xps_handler_t read_cb, xps_handler_t write_cb, xps_handler_t close_cb) {
  assert(ptr != NULL);

  loop_event_t *event = malloc(sizeof(loop_event_t));
  if (event == NULL) {
    logger(LOG_ERROR, "event_create()", "malloc() failed for 'event'");
    return NULL;
  }

  /* set fd, ptr, read_cb fields of event */
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

xps_loop_t *xps_loop_create(xps_core_t *core) {
  assert(core != NULL);

  /* fill this */
  int epoll_fd = epoll_create1(0);
    if(epoll_fd==-1){
        logger(LOG_ERROR, "xps_loop_create()", "epoll_create1() failed");
        return NULL;
    }


  xps_loop_t* loop = malloc(sizeof(xps_loop_t));
  if(loop==NULL){
    logger(LOG_ERROR, "xps_loop_create()", "malloc() failed");
    close(epoll_fd);
    return NULL;
  }

  loop->epoll_fd = epoll_fd;
  loop->core = core;
  loop->n_null_events = 0;  
  vec_init(&loop->events);

  logger(LOG_DEBUG, "xps_loop_create()", "created event loop instance");

  return loop;
}

void xps_loop_destroy(xps_loop_t *loop) {
  assert(loop != NULL);

  close(loop->epoll_fd);
  for(int i=0; i<loop->events.length; i++){
    loop_event_t *ev= loop->events.data[i];
    if(ev != NULL) loop_event_destroy(ev);
  }
  vec_deinit(&loop->events);

  free(loop);
  loop=NULL;
  return;
}

int xps_loop_attach(xps_loop_t *loop, u_int fd, int event_flags, void *ptr, xps_handler_t read_cb, xps_handler_t write_cb, xps_handler_t close_cb){
  assert(loop != NULL);
  assert(ptr != NULL);

  loop_event_t * loop_event = loop_event_create(fd,ptr,read_cb,write_cb,close_cb);
  if(loop_event == NULL){
    return E_FAIL;
  }

  struct epoll_event event;
  event.data.ptr=loop_event;
  event.events=event_flags;

  if(epoll_ctl(loop->epoll_fd,EPOLL_CTL_ADD,fd, &event) ==-1){
    logger(LOG_ERROR, "xps_loop_attach()", "epoll_ctl() failed");
    loop_event_destroy(loop_event);
    return E_FAIL;
  }

  vec_push(&loop->events, loop_event);
  return OK;

}

int xps_loop_detach(xps_loop_t *loop, u_int fd) {
  assert(loop != NULL);

    for(int i=0; i<loop->events.length ; i++){
        loop_event_t * ev = loop->events.data[i];
        if(ev != NULL && ev->fd==fd){ 
            if(epoll_ctl(loop->epoll_fd,EPOLL_CTL_DEL,fd,NULL)==-1){
                logger(LOG_ERROR, "xps_loop_detach()", "epoll_ctl() failed");
                return E_FAIL;
            }
            loop_event_destroy(ev);
            loop->events.data[i]=NULL;
            loop->n_null_events++;
            return OK;
        }
    }
    logger(LOG_ERROR, "xps_loop_detach()", "FD not found in events vector");
    return E_FAIL;
}



void xps_loop_run(xps_loop_t *loop) {
  
  assert(loop!=NULL);
  logger(LOG_DEBUG, "xps_loop_run()", "starting to run loop");

    while (1) {
        int timeout= handle_pipes(loop) ? 0 : -1;

        logger(LOG_DEBUG, "xps_loop_run()", "epoll wait");
        int n_events = epoll_wait(loop->epoll_fd, loop->epoll_events, MAX_EPOLL_EVENTS, timeout);
        logger(LOG_DEBUG, "xps_loop_run()", "epoll wait over");

        logger(LOG_DEBUG, "xps_loop_run()", "handling %d events", n_events);

        if(n_events > 0){
          handle_epoll_events(loop, n_events);
        }
        filter_nulls(loop->core);
    }
}

bool handle_pipes(xps_loop_t *loop) {
    assert(loop != NULL);
    for (int i = 0; i < loop->core->pipes.length; i++) {
    xps_pipe_t *pipe = loop->core->pipes.data[i];
        if (pipe == NULL)
            continue;
        /*Destroy the pipe if it has no source and sink and continue*/
        if(pipe->source == NULL && pipe->sink == NULL){
            xps_pipe_destroy(pipe);
            continue;
        }

        if (pipe->source!= NULL && pipe->source->ready && xps_pipe_is_writable(pipe)){
          //call connection_source_handler to write into pipe
          pipe->source->handler_cb(pipe->source);
        }

        if (pipe->sink!= NULL && pipe->sink->ready && xps_pipe_is_readable(pipe)) {
            //call connection_sink_handler to read from pipe
            pipe->sink->handler_cb(pipe->sink);
        }

        if (pipe->source && pipe->sink==NULL) {
            pipe->source->active = false;
            pipe->source->close_cb(pipe->source);
        }

        if (pipe->sink && pipe->source == NULL && !xps_pipe_is_readable(pipe)) {
            pipe->sink->active = false;
            if (pipe->sink->close_cb != NULL) {
                pipe->sink->close_cb(pipe->sink);
            }
            continue;
        }

    }

    for (int i = 0; i < loop->core->pipes.length; i++) {
    xps_pipe_t *pipe = loop->core->pipes.data[i];
        if (pipe == NULL){
            logger(LOG_DEBUG, "handle_pipes", "pipe is null");
            continue;
        }
        if (pipe->source!= NULL && pipe->source->ready && xps_pipe_is_writable(pipe)){
                return true;
        }
        if (pipe->sink!= NULL && pipe->sink->ready && xps_pipe_is_readable(pipe)) {
                    return true;
        }
        if (pipe->source && pipe->sink==NULL) {
            return true;
        }
        if (pipe->sink && pipe->source==NULL && xps_pipe_is_readable(pipe)) {
            return true;
        }
    }
    return false;
}


void filter_nulls(xps_core_t *core) {
  /*check whether number of nulls in each of events, listeners, connections, pipes list
      exceeds DEFAULT_NULLS_THRESH and filter nulls using vec_filter_null() and set
      number of nulls in each list to 0*/
    if(core->loop->n_null_events > DEFAULT_NULLS_THRESH) {
        vec_filter_null(&core->loop->events);
        core->loop->n_null_events = 0;
    }
    if(core->n_null_listeners > DEFAULT_NULLS_THRESH) {
        vec_filter_null(&core->listeners);
        core->n_null_listeners = 0;
    }
    if(core->n_null_connections > DEFAULT_NULLS_THRESH) {
        vec_filter_null(&core->connections);
        core->n_null_connections = 0;
    }
    if(core->n_null_pipes > DEFAULT_NULLS_THRESH) {
        vec_filter_null(&core->pipes);
        core->n_null_pipes = 0;
    }
    if(core->n_null_sessions > DEFAULT_NULLS_THRESH) {
        vec_filter_null(&core->sessions);
        core->n_null_sessions = 0;
    }
}

void handle_epoll_events(xps_loop_t *loop, int n_events) {
    logger(LOG_DEBUG, "handle_epoll_events()", "handling %d events", n_events);

    for (int i = 0; i < n_events; i++) {
        logger(LOG_DEBUG, "handle_epoll_events()", "handling event no. %d", i + 1);
        struct epoll_event curr_epoll_event = loop->epoll_events[i];
        loop_event_t *curr_event = curr_epoll_event.data.ptr;

        // Check if event still exists. Could have been destroyed due to prev event
        if (!valid_event(loop, curr_event)) {
            logger(LOG_DEBUG, "handle_epoll_events()", "event not found. skipping");
            continue;
        }

        // Close event
        if (curr_epoll_event.events & (EPOLLHUP | EPOLLERR)) {
            logger(LOG_DEBUG, "handle_epoll_events()", "EVENT / close");
            if(curr_event->close_cb != NULL) {
                // Pass the ptr from loop_event_t as a parameter to the callback
                curr_event->close_cb(curr_event->ptr);
            }
            else {
                logger(LOG_WARNING, "handle_epoll_events()", "close_cb is NULL");
            }
        }

        if(!valid_event(loop, curr_event)) {
            logger(LOG_DEBUG, "handle_epoll_events()", "event not found after close_cb. skipping");
            continue;
        }

        // Read event
        if (curr_epoll_event.events & EPOLLIN) {
            logger(LOG_DEBUG, "handle_epoll_events()", "EVENT / read");
            if (curr_event->read_cb != NULL) {
                // Pass the ptr from loop_event_t as a parameter to the callback
                curr_event->read_cb(curr_event->ptr);
            }
            else{
                logger(LOG_WARNING, "handle_epoll_events()", "read_cb is NULL");
            }
        }

        if(!valid_event(loop, curr_event)) {
            logger(LOG_DEBUG, "handle_epoll_events()", "event not found after read_cb. skipping");
            continue;
        }

        // Write event
        if (curr_epoll_event.events & EPOLLOUT) {
            logger(LOG_DEBUG, "handle_epoll_events()", "EVENT / write");
            if (curr_event->write_cb != NULL) {
                // Pass the ptr from loop_event_t as a parameter to the callback
                curr_event->write_cb(curr_event->ptr);
            }
            else{
                logger(LOG_WARNING, "handle_epoll_events()", "write_cb is NULL");
            }
        }
    }
}