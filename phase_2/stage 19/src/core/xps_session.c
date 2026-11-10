#include "xps_session.h"
#include "../config/xps_config.h"
static void session_send_error_response(xps_session_t *session,u_int status){
    xps_http_res_t *res=xps_http_res_create(session->core,status);
    if(res==NULL)return;
    xps_http_set_header(&res->headers,"Content-Length","0");
    xps_buffer_t *buff=xps_http_res_serialize(res);
    xps_http_res_destroy(res);
    if(buff!=NULL)set_to_client_buff(session,buff);
}
static void session_clear_request_buff(xps_session_t *session){
    if(session->from_client_buff!=NULL){
        xps_buffer_destroy(session->from_client_buff);
        set_from_client_buff(session,NULL);
    }
}
xps_session_t *xps_session_create(xps_core_t *core,xps_connection_t *client){
    assert(core!=NULL&&client!=NULL);
    xps_session_t *session=malloc(sizeof(xps_session_t));
    if(session==NULL){
        logger(LOG_ERROR,"xps_session_create()","malloc() failed for 'session'");
        return NULL;
    }
    session->client_source=xps_pipe_source_create(session,client_source_handler,client_source_close_handler);
    session->client_sink=xps_pipe_sink_create(session,client_sink_handler,client_sink_close_handler);
    session->upstream_source=xps_pipe_source_create(session,upstream_source_handler,upstream_source_close_handler);
    session->upstream_sink=xps_pipe_sink_create(session,upstream_sink_handler,upstream_sink_close_handler);
    session->file_sink=xps_pipe_sink_create(session,file_sink_handler,file_sink_close_handler);
    if(!(session->client_source&&session->client_sink&&session->upstream_source&&
         session->upstream_sink&&session->file_sink)){
        logger(LOG_ERROR,"xps_session_create()","failed to create some sources/sinks");
        if(session->client_source)xps_pipe_source_destroy(session->client_source);
        if(session->client_sink)xps_pipe_sink_destroy(session->client_sink);
        if(session->upstream_source)xps_pipe_source_destroy(session->upstream_source);
        if(session->upstream_sink)xps_pipe_sink_destroy(session->upstream_sink);
        if(session->file_sink)xps_pipe_sink_destroy(session->file_sink);
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
    session->http_req=NULL;
    session->lookup=NULL;
    session->gzip=NULL;
    session->client_sink->ready=true;
    session->upstream_sink->ready=true;
    session->file_sink->ready=true;
    vec_push(&core->sessions,session);
    if(xps_pipe_create(core,DEFAULT_PIPE_BUFF_THRESH,client->source,session->client_sink)==NULL||
       xps_pipe_create(core,DEFAULT_PIPE_BUFF_THRESH,session->client_source,client->sink)==NULL){
        logger(LOG_ERROR,"xps_session_create()","failed to create client pipes");
        if(session->client_source)xps_pipe_source_destroy(session->client_source);
        if(session->client_sink)xps_pipe_sink_destroy(session->client_sink);
        if(session->upstream_source)xps_pipe_source_destroy(session->upstream_source);
        if(session->upstream_sink)xps_pipe_sink_destroy(session->upstream_sink);
        if(session->file_sink)xps_pipe_sink_destroy(session->file_sink);
        free(session);
        return NULL;
    }
    logger(LOG_DEBUG,"xps_session_create()","created session");
    return session;
}
void client_source_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_source_t *source=ptr;
    xps_session_t *session=source->ptr;
    if(session->to_client_buff==NULL){
        logger(LOG_ERROR,"client_source_handler()","no data to write to client");
        return;
    }
    if(xps_pipe_source_write(source,session->to_client_buff)!=OK){
        logger(LOG_ERROR,"client_source_handler()","xps_pipe_source_write() failed");
        return;
    }
    xps_buffer_destroy(session->to_client_buff);
    set_to_client_buff(session,NULL);
    session_check_destroy(session);
}
void client_source_close_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_source_t *source=ptr;
    xps_session_t *session=source->ptr;
    session_check_destroy(session);
}
void client_sink_handler(void *ptr){
    assert(ptr);
    xps_pipe_sink_t *sink=ptr;
    xps_session_t *session=sink->ptr;
    size_t len=sink->pipe->buff_list->len;
    if(len==0){
        logger(LOG_ERROR,"client_sink_handler()","xps_pipe_sink_read() failed");
        return;
    }
    xps_buffer_t *buff=xps_pipe_sink_read(sink,len);
    if(buff==NULL){
        logger(LOG_ERROR,"client_sink_handler()","xps_pipe_sink_read() failed");
        return;
    }
    if(session->http_req==NULL){
        int error=E_FAIL;
        xps_http_req_t *http_req=xps_http_req_create(session->core,buff,&error);
        if(error==E_AGAIN){
            logger(LOG_DEBUG,"client_sink_handler()","waiting for more request data");
            xps_buffer_destroy(buff);
            return;
        }
        if(error!=OK){
            logger(LOG_ERROR,"client_sink_handler()","xps_http_req_create() failed");
            xps_buffer_destroy(buff);
            xps_pipe_sink_clear(sink,len);
            session_send_error_response(session,HTTP_BAD_REQUEST);
            return;
        }
        session->http_req=http_req;
        logger(LOG_DEBUG,"client_sink_handler()","http_req created successfully");
        xps_buffer_t *http_req_buff=xps_http_req_serialize(http_req);
        set_from_client_buff(session,http_req_buff);
        xps_buffer_destroy(buff);
        xps_pipe_sink_clear(sink,len);
        session_process_request(session);
    }
    else{
        set_from_client_buff(session,buff);
        xps_pipe_sink_clear(sink,buff->len);
    }
}
void client_sink_close_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_sink_t *sink=ptr;
    xps_session_t *session=sink->ptr;
    session_check_destroy(session);
}
void upstream_source_handler(void *ptr){
    assert(ptr);
    xps_pipe_source_t *source=ptr;
    xps_session_t *session=source->ptr;
    if(session->from_client_buff==NULL){
        logger(LOG_ERROR,"upstream_source_handler()","xps_pipe_source_write() failed");
        source->ready=false;
        return;
    }
    if(xps_pipe_source_write(source,session->from_client_buff)!=OK){
        logger(LOG_ERROR,"upstream_source_handler()","xps_pipe_source_write() failed");
        return;
    }
    if(session->upstream_connected==false){
        session->upstream_write_bytes+=session->from_client_buff->len;
        if(session->upstream_write_bytes>session->upstream_source->pipe->buff_list->len)
            session->upstream_connected=true;
    }
    xps_buffer_destroy(session->from_client_buff);
    source->ready=false;
    set_from_client_buff(session,NULL);
    session_check_destroy(session);
}
void upstream_source_close_handler(void *ptr){
    assert(ptr);
    xps_pipe_source_t *source=ptr;
    xps_session_t *session=source->ptr;
    if(!session->upstream_connected&&!session->upstream_error_res_set)
        upstream_error_res(session);
    session_check_destroy(session);
}
void upstream_sink_handler(void *ptr){
    assert(ptr);
    xps_pipe_sink_t *sink=ptr;
    xps_session_t *session=sink->ptr;
    session->upstream_connected=true;
    size_t len=sink->pipe->buff_list->len;
    if(len==0){
        logger(LOG_ERROR,"upstream_sink_handler()","xps_pipe_sink_read() failed");
        return;
    }
    xps_buffer_t *buff=xps_pipe_sink_read(sink,len);
    if(buff==NULL){
        logger(LOG_ERROR,"upstream_sink_handler()","xps_pipe_sink_read() failed");
        return;
    }
    set_to_client_buff(session,buff);
    xps_pipe_sink_clear(sink,len);
}
void upstream_sink_close_handler(void *ptr){
    assert(ptr);
    xps_pipe_sink_t *sink=ptr;
    xps_session_t *session=sink->ptr;
    if(!session->upstream_connected&&!session->upstream_error_res_set)
        upstream_error_res(session);
    session_check_destroy(session);
}
void upstream_error_res(xps_session_t *session){
    assert(session!=NULL);
    session->upstream_error_res_set=true;
}
void file_sink_handler(void *ptr){
    assert(ptr);
    xps_pipe_sink_t *sink=ptr;
    xps_session_t *session=sink->ptr;
    assert(session!=NULL);
    size_t len=sink->pipe->buff_list->len;
    xps_buffer_t *buff=xps_pipe_sink_read(sink,len);
    if(buff==NULL){
        logger(LOG_ERROR,"file_sink_handler()","xps_pipe_sink_read() failed");
        return;
    }
    set_to_client_buff(session,buff);
    xps_pipe_sink_clear(sink,len);
}
void file_sink_close_handler(void *ptr){
    assert(ptr);
    xps_pipe_sink_t *sink=ptr;
    xps_session_t *session=sink->ptr;
    session_check_destroy(session);
}
void set_to_client_buff(xps_session_t *session,xps_buffer_t *buff){
    assert(session);
    session->to_client_buff=buff;
    if(buff==NULL){
        session->client_source->ready=false;
        session->upstream_sink->ready=true;
        session->file_sink->ready=true;
    }
    else{
        session->client_source->ready=true;
        session->upstream_sink->ready=false;
        session->file_sink->ready=false;
    }
}
void set_from_client_buff(xps_session_t *session,xps_buffer_t *buff){
    assert(session);
    session->from_client_buff=buff;
    if(buff==NULL){
        session->client_sink->ready=true;
        session->upstream_source->ready=false;
    }
    else{
        session->client_sink->ready=false;
        session->upstream_source->ready=true;
    }
}
void session_check_destroy(xps_session_t *session){
    assert(session);
    bool c2u_flow=session->upstream_source->active&&
                  (session->client_sink->active||session->from_client_buff);
    bool u2c_flow=session->client_source->active&&
                  (session->upstream_sink->active||session->to_client_buff);
    bool f2c_flow=session->client_source->active&&
                  (session->file_sink->active||session->to_client_buff);
    bool flowing=c2u_flow||u2c_flow||f2c_flow;
    if(!flowing)
        xps_session_destroy(session);
}
void xps_session_destroy(xps_session_t *session){
    assert(session);
    if(session->client_source)xps_pipe_source_destroy(session->client_source);
    if(session->client_sink)xps_pipe_sink_destroy(session->client_sink);
    if(session->upstream_source)xps_pipe_source_destroy(session->upstream_source);
    if(session->upstream_sink)xps_pipe_sink_destroy(session->upstream_sink);
    if(session->file_sink)xps_pipe_sink_destroy(session->file_sink);
    if(session->to_client_buff)xps_buffer_destroy(session->to_client_buff);
    if(session->from_client_buff)xps_buffer_destroy(session->from_client_buff);
    for(int i=0;i<session->core->sessions.length;i++){
        if(session->core->sessions.data[i]==session){
            session->core->sessions.data[i]=NULL;
            session->core->n_null_sessions++;
            break;
        }
    }
    if(session->lookup!=NULL)
        xps_config_lookup_destroy(session->lookup,session->core);
    if(session->http_req!=NULL)
        xps_http_req_destroy(session->core,session->http_req);
    free(session);
    logger(LOG_DEBUG,"xps_session_destroy()","destroyed session");
}
void session_process_request(xps_session_t *session){
    assert(session!=NULL);
    if(session->http_req==NULL){
        session_send_error_response(session,HTTP_BAD_REQUEST);
        return;
    }
    int error=E_FAIL;
    session->lookup=xps_config_lookup(session->core->config,session->http_req,session->client,&error);
    if(session->lookup==NULL){
        u_int status=(error==E_NOTFOUND)?HTTP_NOT_FOUND:HTTP_INTERNAL_SERVER_ERROR;
        session_send_error_response(session,status);
        return;
    }
    if(session->lookup->ip_whitelist.length>0){
        const char *client_ip=session->client->remote_ip;
        bool is_allowed=false;
        for(int i=0;i<session->lookup->ip_whitelist.length;i++){
            const char *ip_w=session->lookup->ip_whitelist.data[i];
            if(strcmp(client_ip,ip_w)==0){is_allowed=true;break;}
        }
        if(!is_allowed){
            logger(LOG_DEBUG,"session_process_request()","client ip %s is not whitelisted",client_ip);
            session_clear_request_buff(session);
            session_send_error_response(session,HTTP_FORBIDDEN);
            return;
        }
    }
    if(session->lookup->ip_blacklist.length>0){
        const char *client_ip=session->client->remote_ip;
        for(int i=0;i<session->lookup->ip_blacklist.length;i++){
            const char *ip_b=session->lookup->ip_blacklist.data[i];
            if(strcmp(client_ip,ip_b)==0){
                logger(LOG_DEBUG,"session_process_request()","client ip %s is blacklisted",client_ip);
                session_clear_request_buff(session);
                session_send_error_response(session,HTTP_FORBIDDEN);
                return;
            }
        }
    }
    if(session->lookup->type==REQ_FILE_SERVE){
        session_clear_request_buff(session);
        u_int status=HTTP_OK;
	xps_file_t *file=NULL;
	xps_buffer_t *dir_html=NULL;
	if(session->lookup->file_path==NULL&&session->lookup->dir_path!=NULL){
	    dir_html=xps_directory_browsing(session->lookup->dir_path,session->http_req->pathname);
	    if(dir_html==NULL)status=HTTP_INTERNAL_SERVER_ERROR;
	}
	else if(session->lookup->file_path==NULL){
	    status=HTTP_NOT_FOUND;
	}
	else{
	    error=E_FAIL;
	    logger(LOG_DEBUG,"session_process_request()","file_path=%s",session->lookup->file_path);
	    file=xps_file_create(session->core,session->lookup->file_path,&error);
	    if(file==NULL){
		if(error==E_PERMISSION)status=HTTP_FORBIDDEN;
		else if(error==E_NOTFOUND)status=HTTP_NOT_FOUND;
		else status=HTTP_INTERNAL_SERVER_ERROR;
	    }
	}
	const char *accept_enc=xps_http_get_header(&session->http_req->headers,"Accept-Encoding");
	bool use_gzip=file!=NULL&&session->lookup->gzip_enable&&accept_enc!=NULL&&strstr(accept_enc,"gzip")!=NULL;
	if(use_gzip){
	    session->gzip=xps_gzip_create(session->lookup->gzip_level);
	    if(session->gzip==NULL)use_gzip=false;
	}
	xps_http_res_t *res=xps_http_res_create(session->core,status);
	if(res==NULL){
	    if(file!=NULL)xps_file_destroy(file);
	    if(session->gzip!=NULL){xps_gzip_destroy(session->gzip);session->gzip=NULL;}
	    if(dir_html!=NULL)xps_buffer_destroy(dir_html);
	    return;
	}
	char length[32];
	snprintf(length,sizeof(length),"%zu",file!=NULL?file->size:(dir_html!=NULL?dir_html->len:0));
	if((!use_gzip&&xps_http_set_header(&res->headers,"Content-Length",length)!=OK)||
	   (use_gzip&&(xps_http_set_header(&res->headers,"Content-Encoding","gzip")!=OK||
	   xps_http_set_header(&res->headers,"Vary","Accept-Encoding")!=OK||
	   xps_http_set_header(&res->headers,"Connection","close")!=OK))||
	   (file!=NULL&&xps_http_set_header(&res->headers,"Content-Type",
	   file->mime_type!=NULL?file->mime_type:"application/octet-stream")!=OK)||
           (dir_html!=NULL&&xps_http_set_header(&res->headers,"Content-Type","text/html")!=OK)){
	    xps_http_res_destroy(res);
	    if(file!=NULL)xps_file_destroy(file);
	    if(session->gzip!=NULL){xps_gzip_destroy(session->gzip);session->gzip=NULL;}
	    if(dir_html!=NULL)xps_buffer_destroy(dir_html);
	    return;
	}
	if(dir_html!=NULL)xps_http_res_set_body(res,dir_html);
        xps_buffer_t *buff=xps_http_res_serialize(res);
        xps_http_res_destroy(res);
        if(buff==NULL){
            if(file!=NULL)xps_file_destroy(file);
            if(session->gzip!=NULL){xps_gzip_destroy(session->gzip);session->gzip=NULL;}
            return;
        }
        if(file!=NULL){
            session->file=file;
            bool pipes_failed;
            if(use_gzip){
                pipes_failed=xps_pipe_create(session->core,DEFAULT_PIPE_BUFF_THRESH,session->file->source,session->gzip->sink)==NULL||xps_pipe_create(session->core,DEFAULT_PIPE_BUFF_THRESH,session->gzip->source,session->file_sink)==NULL;
            }else{
                pipes_failed=xps_pipe_create(session->core,DEFAULT_PIPE_BUFF_THRESH,session->file->source,session->file_sink)==NULL;
            }
            if(pipes_failed){
                if(session->gzip!=NULL){xps_gzip_destroy(session->gzip);session->gzip=NULL;}
                logger(LOG_ERROR,"session_process_request()","xps_pipe_create() failed for file source and file sink");
                xps_buffer_destroy(buff);
                xps_file_destroy(session->file);
                session->file=NULL;
                session_send_error_response(session,HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
        }
        set_to_client_buff(session,buff);
    }
    else if(session->lookup->type==REQ_REVERSE_PROXY){
        char host[256];
        u_int port;
        if(session->lookup->upstream==NULL||
           sscanf(session->lookup->upstream,"%255[^:]:%u",host,&port)!=2){
            logger(LOG_ERROR,"session_process_request()","invalid upstream address");
            session_clear_request_buff(session);
            session_send_error_response(session,HTTP_BAD_GATEWAY);
            return;
        }
        session->upstream=xps_upstream_create(session->core,host,port);
        if(session->upstream==NULL){
            logger(LOG_ERROR,"session_process_request()","xps_upstream_create() failed");
            session_clear_request_buff(session);
            session_send_error_response(session,HTTP_BAD_GATEWAY);
            return;
        }
        xps_pipe_t *to_upstream=xps_pipe_create(session->core,DEFAULT_PIPE_BUFF_THRESH,session->upstream_source,session->upstream->sink);
        if(to_upstream==NULL){
            logger(LOG_ERROR,"session_process_request()","failed to create client-to-upstream pipe");
            xps_connection_destroy(session->upstream);
            session->upstream=NULL;
            session_clear_request_buff(session);
            session_send_error_response(session,HTTP_BAD_GATEWAY);
            return;
        }
        if(xps_pipe_create(session->core,DEFAULT_PIPE_BUFF_THRESH,session->upstream->source,session->upstream_sink)==NULL){
            logger(LOG_ERROR,"session_process_request()","failed to create upstream-to-client pipe");
            xps_pipe_detach_source(to_upstream);
            xps_pipe_detach_sink(to_upstream);
            xps_pipe_destroy(to_upstream);
            xps_connection_destroy(session->upstream);
            session->upstream=NULL;
            session_clear_request_buff(session);
            session_send_error_response(session,HTTP_BAD_GATEWAY);
            return;
        }
        set_from_client_buff(session,session->from_client_buff);
    }
    else if(session->lookup->type==REQ_REDIRECT){
        session_clear_request_buff(session);
        xps_http_res_t *http_res=xps_http_res_create(session->core,session->lookup->http_status_code);
        if(http_res==NULL)return;
        xps_http_set_header(&http_res->headers,"Location",session->lookup->redirect_url);
        xps_http_set_header(&http_res->headers,"Content-Length","0");
        xps_buffer_t *http_res_buff=xps_http_res_serialize(http_res);
        xps_http_res_destroy(http_res);
        if(http_res_buff!=NULL)
            set_to_client_buff(session,http_res_buff);
        return;
    }
    else{
        logger(LOG_ERROR,"session_process_request()","invalid lookup type");
        xps_session_destroy(session);
        return;
    }
}
