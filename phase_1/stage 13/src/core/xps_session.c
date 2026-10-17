#include "../xps.h"
void client_source_handler(void *ptr);
void client_source_close_handler(void *ptr);
void client_sink_handler(void *ptr);
void client_sink_close_handler(void *ptr);
void upstream_source_handler(void *ptr);
void upstream_source_close_handler(void *ptr);
void upstream_sink_handler(void *ptr);
void upstream_sink_close_handler(void *ptr);
void file_sink_handler(void *ptr);
void file_sink_close_handler(void *ptr);
void upstream_error_res(xps_session_t *session);
void set_to_client_buff(xps_session_t *session);
void set_from_client_buff(xps_session_t *session);
void session_check_destroy(xps_session_t *session);

xps_session_t *xps_session_create(xps_core_t *core,xps_connection_t *client){
    assert(core!=NULL);
    assert(client!=NULL);
    xps_session_t *session=calloc(1,sizeof(xps_session_t));
    if(session==NULL){
        logger(LOG_ERROR,"xps_session_create()","malloc() failed for 'session'");
        return NULL;
    }
    session->client_source=xps_pipe_source_create(session,client_source_handler,client_source_close_handler);
    session->client_sink=xps_pipe_sink_create(session,client_sink_handler,client_sink_close_handler);
    session->upstream_source=xps_pipe_source_create(session,upstream_source_handler,upstream_source_close_handler);
    session->upstream_sink=xps_pipe_sink_create(session,upstream_sink_handler,upstream_sink_close_handler);
    session->file_sink=xps_pipe_sink_create(session,file_sink_handler,file_sink_close_handler);

    
    if(session->client_source==NULL||session->client_sink==NULL||session->upstream_source==NULL||session->upstream_sink==NULL||session->file_sink==NULL){
        logger(LOG_ERROR,"xps_session_create()","failed to create some sources/sinks");
        if(session->client_source!=NULL)xps_pipe_source_destroy(session->client_source);
        if(session->client_sink!=NULL)xps_pipe_sink_destroy(session->client_sink);
        if(session->upstream_source!=NULL)xps_pipe_source_destroy(session->upstream_source);
        if(session->upstream_sink!=NULL)xps_pipe_sink_destroy(session->upstream_sink);
        if(session->file_sink!=NULL)xps_pipe_sink_destroy(session->file_sink);
        free(session);
        return NULL;
    }
    session->core=core;
    session->client=client;
    session->upstream=NULL;
    session->upstream_connected=false;
    session->upstream_error_res_set=false;
    session->upstream_write_bytes=0;
    session->file=NULL;
    session->to_client_buff=NULL;
    session->from_client_buff=NULL;
    session->client_sink->ready=true;
    session->upstream_sink->ready=true;
    session->file_sink->ready=true;
    vec_push(&core->sessions,session);
    if(xps_pipe_create(core,DEFAULT_PIPE_BUFF_THRESH,client->source,session->client_sink)==NULL||xps_pipe_create(core,DEFAULT_PIPE_BUFF_THRESH,session->client_source,client->sink)==NULL){
        logger(LOG_ERROR,"xps_session_create()","failed to create client pipes");
        xps_session_destroy(session);
        return NULL;
    }
    if(client->listener->port==8001){
        xps_connection_t *upstream=xps_upstream_create(core,"127.0.0.1",3000);
        if(upstream==NULL){
            logger(LOG_ERROR,"xps_session_create()","xps_upstream_create() failed");
            xps_session_destroy(session);
            return NULL;
        }
        session->upstream=upstream;
        if(xps_pipe_create(core,DEFAULT_PIPE_BUFF_THRESH,session->upstream_source,upstream->sink)==NULL||xps_pipe_create(core,DEFAULT_PIPE_BUFF_THRESH,upstream->source,session->upstream_sink)==NULL){
            logger(LOG_ERROR,"xps_session_create()","failed to create upstream pipes");
            session->upstream=NULL;
            xps_connection_destroy(upstream);
            xps_session_destroy(session);
            return NULL;
        }
    }else if(client->listener->port==8002){
        int error;
        xps_file_t *file=xps_file_create(core,"../public/sample.txt",&error);
        if(file==NULL){
            logger(LOG_ERROR,"xps_session_create()","xps_file_create() failed");
            xps_session_destroy(session);
            return NULL;
        }
        session->file=file;
        if(xps_pipe_create(core,DEFAULT_PIPE_BUFF_THRESH,file->source,session->file_sink)==NULL){
            logger(LOG_ERROR,"xps_session_create()","failed to create file pipe");
            xps_file_destroy(file);
            session->file=NULL;
            xps_session_destroy(session);
            return NULL;
        }
    }
    logger(LOG_DEBUG,"xps_session_create()","created session");
    return session;
}

void xps_session_destroy(xps_session_t *session){
    assert(session!=NULL);
    xps_core_t *core=session->core;
    if(session->to_client_buff!=NULL){
        xps_buffer_destroy(session->to_client_buff);
        session->to_client_buff=NULL;
    }
    if(session->from_client_buff!=NULL){
        xps_buffer_destroy(session->from_client_buff);
        session->from_client_buff=NULL;
    }
    if(session->client_source!=NULL){
        xps_pipe_source_destroy(session->client_source);
        session->client_source=NULL;
    }
    if(session->client_sink!=NULL){
        xps_pipe_sink_destroy(session->client_sink);
        session->client_sink=NULL;
    }
    if(session->upstream_source!=NULL){
        xps_pipe_source_destroy(session->upstream_source);
        session->upstream_source=NULL;
    }
    if(session->upstream_sink!=NULL){
        xps_pipe_sink_destroy(session->upstream_sink);
        session->upstream_sink=NULL;
    }
    if(session->file_sink!=NULL){
        xps_pipe_sink_destroy(session->file_sink);
        session->file_sink=NULL;
    }
    if(session->upstream!=NULL){
        xps_connection_t *upstream=session->upstream;
        session->upstream=NULL;
        xps_connection_destroy(upstream);
    }
    if(session->file!=NULL){
        xps_file_t *file=session->file;
        session->file=NULL;
        xps_file_destroy(file);
    }
    if(core!=NULL){
        for(int i=0;i<core->sessions.length;i++){
            if(core->sessions.data[i]==session){
                core->sessions.data[i]=NULL;
                core->n_null_sessions++;
                break;
            }
        }
    }
    free(session);
    logger(LOG_DEBUG,"xps_session_destroy()","destroyed session");
}

void client_source_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_source_t *source=ptr;
    xps_session_t *session=source->ptr;
    if(session->to_client_buff==NULL){
        source->ready=false;
        return;
    }
    if(xps_pipe_source_write(source,session->to_client_buff)!=OK)return;
    xps_buffer_destroy(session->to_client_buff);
    session->to_client_buff=NULL;
    set_to_client_buff(session);
}

void client_source_close_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_source_t *source=ptr;
    xps_session_t *session=source->ptr;
    session_check_destroy(session);
}

void client_sink_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_sink_t *sink=ptr;
    xps_session_t *session=sink->ptr;
    if(session->from_client_buff!=NULL)return;
    if(!xps_pipe_is_readable(sink->pipe))return;
    session->from_client_buff=xps_pipe_sink_read(sink,sink->pipe->buff_list->len);
    if(session->from_client_buff==NULL)return;
    xps_pipe_sink_clear(sink,session->from_client_buff->len);
    set_from_client_buff(session);
}

void client_sink_close_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_sink_t *sink=ptr;
    xps_session_t *session=sink->ptr;
    session_check_destroy(session);
}

void upstream_source_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_source_t *source=ptr;
    xps_session_t *session=source->ptr;
    if(session->from_client_buff==NULL){
        source->ready=false;
        return;
    }
    if(xps_pipe_source_write(source,session->from_client_buff)!=OK)return;
    session->upstream_write_bytes+=session->from_client_buff->len;
    session->upstream_connected=true;
    xps_buffer_destroy(session->from_client_buff);
    session->from_client_buff=NULL;
    set_from_client_buff(session);
}

void upstream_source_close_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_source_t *source=ptr;
    xps_session_t *session=source->ptr;
    session->upstream=NULL;
    if(!session->upstream_connected&&!session->upstream_error_res_set)upstream_error_res(session);
    session_check_destroy(session);
}

void upstream_sink_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_sink_t *sink=ptr;
    xps_session_t *session=sink->ptr;
    if(session->to_client_buff!=NULL)return;
    if(!xps_pipe_is_readable(sink->pipe))return;
    session->upstream_connected=true;
    session->to_client_buff=xps_pipe_sink_read(sink,sink->pipe->buff_list->len);
    if(session->to_client_buff==NULL)return;
    xps_pipe_sink_clear(sink,session->to_client_buff->len);
    set_to_client_buff(session);
}

void upstream_sink_close_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_sink_t *sink=ptr;
    xps_session_t *session=sink->ptr;
    session->upstream=NULL;
    if(!session->upstream_connected&&!session->upstream_error_res_set)upstream_error_res(session);
    session_check_destroy(session);
}

void file_sink_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_sink_t *sink=ptr;
    xps_session_t *session=sink->ptr;
    if(session->to_client_buff!=NULL)return;
    if(!xps_pipe_is_readable(sink->pipe))return;
    session->to_client_buff=xps_pipe_sink_read(sink,sink->pipe->buff_list->len);
    if(session->to_client_buff==NULL)return;
    xps_pipe_sink_clear(sink,session->to_client_buff->len);
    set_to_client_buff(session);
}

void file_sink_close_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_sink_t *sink=ptr;
    xps_session_t *session=sink->ptr;
    session_check_destroy(session);
}

void upstream_error_res(xps_session_t *session){
    assert(session!=NULL);
    session->upstream_error_res_set=true;
    if(session->to_client_buff==NULL){
        const char *msg="upstream unavailable\n";
        xps_buffer_t *buff=xps_buffer_create(strlen(msg),strlen(msg),(u_char *)strdup(msg));
        if(buff!=NULL)session->to_client_buff=buff;
    }
    set_to_client_buff(session);
}

void set_to_client_buff(xps_session_t *session){
    assert(session!=NULL);
    if(session->to_client_buff==NULL){
        session->client_source->ready=false;
        session->upstream_sink->ready=true;
        session->file_sink->ready=true;
    }else{
        session->client_source->ready=true;
        session->upstream_sink->ready=false;
        session->file_sink->ready=false;
    }
}

void set_from_client_buff(xps_session_t *session){
    assert(session!=NULL);
    if(session->from_client_buff==NULL){
        session->client_sink->ready=true;
        session->upstream_source->ready=false;
    }else{
        session->client_sink->ready=false;
        session->upstream_source->ready=true;
    }
}

void session_check_destroy(xps_session_t *session){
    assert(session!=NULL);
    bool client_to_upstream=session->upstream_source->active&&(session->client_sink->active||session->from_client_buff!=NULL);
    bool upstream_to_client=session->client_source->active&&(session->upstream_sink->active||session->to_client_buff!=NULL);
    bool file_to_client=session->client_source->active&&(session->file_sink->active||session->to_client_buff!=NULL);
    if(!client_to_upstream&&!upstream_to_client&&!file_to_client)xps_session_destroy(session);
}