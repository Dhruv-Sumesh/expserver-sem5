#include "xps_pipe.h"

xps_pipe_t *xps_pipe_create(xps_core_t *core, size_t buff_thresh, xps_pipe_source_t *source, xps_pipe_sink_t *sink) {
    assert(core != NULL);
    assert(buff_thresh > 0);
    assert(source != NULL);
    assert(sink != NULL);
    xps_pipe_t *pipe = malloc(sizeof(xps_pipe_t));
    if (pipe == NULL) {
        logger(LOG_ERROR, "xps_pipe_create()", "malloc() failed for 'pipe'");
        return NULL;
    }
    xps_buffer_list_t *buff_list = xps_buffer_list_create();
    if (buff_list == NULL) {
        logger(LOG_ERROR, "xps_pipe_create()", "xps_buffer_list_create() failed");
        free(pipe);
        return NULL;
    }
    pipe->core = core;
    pipe->source = NULL;
    pipe->sink = NULL;
    pipe->buff_list = buff_list;
    pipe->buff_thresh = buff_thresh;
    vec_push(&(core->pipes), (void *)pipe);
    if (xps_pipe_attach_source(pipe, source) != OK) {
        logger(LOG_ERROR, "xps_pipe_create()", "xps_pipe_attach_source() failed");
        core->pipes.data[core->pipes.length - 1] = NULL;
        core->n_null_pipes++;
        xps_buffer_list_destroy(pipe->buff_list);
        free(pipe);
        return NULL;
    }
    if (xps_pipe_attach_sink(pipe, sink) != OK) {
        logger(LOG_ERROR, "xps_pipe_create()", "xps_pipe_attach_sink() failed");
        xps_pipe_detach_source(pipe);
        core->pipes.data[core->pipes.length - 1] = NULL;
        core->n_null_pipes++;
        xps_buffer_list_destroy(pipe->buff_list);
        free(pipe);
        return NULL;
    }
    source->ready = true;
    sink->ready = true;
    source->active = true;
    sink->active = true;
    logger(LOG_DEBUG, "xps_pipe_create()", "created pipe");
    return pipe;
}

void xps_pipe_destroy(xps_pipe_t *pipe) {
    assert(pipe != NULL);
    xps_core_t *core = pipe->core;
    if (core != NULL) {
        for (int i = 0; i < core->pipes.length; i++) {
            if (core->pipes.data[i] == pipe) {
                core->pipes.data[i] = NULL;
                core->n_null_pipes++;
                break;
            }
        }
    }
    xps_buffer_list_destroy(pipe->buff_list);
    free(pipe);
    logger(LOG_DEBUG, "xps_pipe_destroy()", "destroyed pipe");
}

bool xps_pipe_is_readable(xps_pipe_t *pipe) {
    assert(pipe != NULL);
    return pipe->buff_list->len > 0;
}

bool xps_pipe_is_writable(xps_pipe_t *pipe) {
    assert(pipe != NULL);
    return pipe->buff_list->len < pipe->buff_thresh;
}

int xps_pipe_attach_source(xps_pipe_t *pipe, xps_pipe_source_t *source) {
    assert(pipe != NULL);
    assert(source != NULL);
    if (pipe->source != NULL) {
        logger(LOG_ERROR, "xps_pipe_attach_source()", "pipe already has a source");
        return E_FAIL;
    }
    pipe->source = source;
    source->pipe = pipe;
    return OK;
}

int xps_pipe_detach_source(xps_pipe_t *pipe) {
    assert(pipe != NULL);
    if (pipe->source == NULL) {
        logger(LOG_ERROR, "xps_pipe_detach_source()", "pipe has no source");
        return E_FAIL;
    }
    pipe->source->pipe = NULL;
    pipe->source = NULL;
    return OK;
}

int xps_pipe_attach_sink(xps_pipe_t *pipe, xps_pipe_sink_t *sink) {
    assert(pipe != NULL);
    assert(sink != NULL);
    if (pipe->sink != NULL) {
        logger(LOG_ERROR, "xps_pipe_attach_sink()", "pipe already has a sink");
        return E_FAIL;
    }
    pipe->sink = sink;
    sink->pipe = pipe;
    return OK;
}

int xps_pipe_detach_sink(xps_pipe_t *pipe) {
    assert(pipe != NULL);
    if (pipe->sink == NULL) {
        logger(LOG_ERROR, "xps_pipe_detach_sink()", "pipe has no sink");
        return E_FAIL;
    }
    pipe->sink->pipe = NULL;
    pipe->sink = NULL;
    return OK;
}

xps_pipe_source_t *xps_pipe_source_create(void *ptr, xps_handler_t handler_cb, xps_handler_t close_cb) {
    assert(ptr != NULL);
    assert(handler_cb != NULL);
    assert(close_cb != NULL);
    xps_pipe_source_t *source = malloc(sizeof(xps_pipe_source_t));
    if (source == NULL) {
        logger(LOG_ERROR, "xps_pipe_source_create()", "malloc() failed for 'source'");
        return NULL;
    }
    source->pipe = NULL;
    source->ready = false;
    source->active = false;
    source->handler_cb = handler_cb;
    source->close_cb = close_cb;
    source->ptr = ptr;
    logger(LOG_DEBUG, "xps_pipe_source_create()", "created pipe_source");
    return source;
}

void xps_pipe_source_destroy(xps_pipe_source_t *source) {
    assert(source != NULL);
    if (source->pipe != NULL)
        xps_pipe_detach_source(source->pipe);
    free(source);
    logger(LOG_DEBUG, "xps_pipe_source_destroy()", "destroyed pipe_source");
}

int xps_pipe_source_write(xps_pipe_source_t *source, xps_buffer_t *buff) {
    assert(source != NULL);
    assert(buff != NULL);
    if (source->pipe == NULL) {
        logger(LOG_ERROR, "xps_pipe_source_write()", "source is not attached to a pipe");
        return E_FAIL;
    }
    xps_pipe_t *pipe = source->pipe;
    if (!xps_pipe_is_writable(pipe)) {
        logger(LOG_ERROR, "xps_pipe_source_write()", "pipe is not writable");
        return E_FAIL;
    }
    if (buff->len > pipe->buff_thresh - pipe->buff_list->len) {
        logger(LOG_ERROR, "xps_pipe_source_write()", "buffer exceeds pipe threshold");
        return E_FAIL;
    }
    xps_buffer_t *dup_buff = xps_buffer_duplicate(buff);
    if (dup_buff == NULL) {
        logger(LOG_ERROR, "xps_pipe_source_write()", "xps_buffer_duplicate() failed");
        return E_FAIL;
    }
    xps_buffer_list_append(pipe->buff_list, dup_buff);
    return OK;
}

xps_pipe_sink_t *xps_pipe_sink_create(void *ptr, xps_handler_t handler_cb, xps_handler_t close_cb) {
    assert(ptr != NULL);
    assert(handler_cb != NULL);
    assert(close_cb != NULL);
    xps_pipe_sink_t *sink = malloc(sizeof(xps_pipe_sink_t));
    if (sink == NULL) {
        logger(LOG_ERROR, "xps_pipe_sink_create()", "malloc() failed for 'sink'");
        return NULL;
    }
    sink->pipe = NULL;
    sink->ready = false;
    sink->active = false;
    sink->handler_cb = handler_cb;
    sink->close_cb = close_cb;
    sink->ptr = ptr;
    logger(LOG_DEBUG, "xps_pipe_sink_create()", "created pipe_sink");
    return sink;
}

void xps_pipe_sink_destroy(xps_pipe_sink_t *sink) {
    assert(sink != NULL);
    if (sink->pipe != NULL)
        xps_pipe_detach_sink(sink->pipe);
    free(sink);
    logger(LOG_DEBUG, "xps_pipe_sink_destroy()", "destroyed pipe_sink");
}

xps_buffer_t *xps_pipe_sink_read(xps_pipe_sink_t *sink, size_t len) {
    assert(sink != NULL);
    assert(len > 0);
    if (sink->pipe == NULL) {
        logger(LOG_ERROR, "xps_pipe_sink_read()", "sink is not attached to a pipe");
        return NULL;
    }
    xps_pipe_t *pipe = sink->pipe;
    if (!xps_pipe_is_readable(pipe)) {
        logger(LOG_ERROR, "xps_pipe_sink_read()", "pipe is not readable");
        return NULL;
    }
    if (pipe->buff_list->len < len) {
        logger(LOG_ERROR, "xps_pipe_sink_read()", "requested length not available");
        return NULL;
    }
    return xps_buffer_list_read(pipe->buff_list, len);
}

int xps_pipe_sink_clear(xps_pipe_sink_t *sink, size_t len) {
    assert(sink != NULL);
    if (sink->pipe == NULL) {
        logger(LOG_ERROR, "xps_pipe_sink_clear()", "sink is not attached to a pipe");
        return E_FAIL;
    }
    xps_pipe_t *pipe = sink->pipe;
    if (pipe->buff_list->len < len) {
        logger(LOG_ERROR, "xps_pipe_sink_clear()", "requested length not available");
        return E_FAIL;
    }
    return xps_buffer_list_clear(pipe->buff_list, len);
}