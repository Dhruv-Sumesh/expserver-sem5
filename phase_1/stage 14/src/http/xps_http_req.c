#include "../xps.h"
int http_process_request_line(xps_http_req_t *http_req,xps_buffer_t *buff){
    assert(http_req!=NULL);
    assert(buff!=NULL);
    int error=xps_http_parse_request_line(http_req,buff);
    if(error!=OK)return error;
    http_req->request_line=str_from_ptrs(http_req->request_line_start,http_req->request_line_end);
    http_req->method=str_from_ptrs(http_req->method_start,http_req->method_end);
    http_req->uri=str_from_ptrs(http_req->uri_start,http_req->uri_end);
    if(http_req->schema_start!=NULL)
        http_req->schema=str_from_ptrs(http_req->schema_start,http_req->schema_end);
    if(http_req->host_start!=NULL)
        http_req->host=str_from_ptrs(http_req->host_start,http_req->host_end);
    if(http_req->path_start!=NULL){
        http_req->path=str_from_ptrs(http_req->path_start,http_req->path_end);
        http_req->pathname=str_from_ptrs(http_req->pathname_start,http_req->pathname_end);
    }
    http_req->http_version=str_from_ptrs(http_req->http_major,http_req->request_line_end);
    http_req->port=-1;
    if(http_req->port_start!=NULL){
        char *port_str=str_from_ptrs(http_req->port_start,http_req->port_end);
        if(port_str==NULL)return E_FAIL;
        char *end_ptr=NULL;
        long port=strtol(port_str,&end_ptr,10);
        if(*port_str=='\0'||*end_ptr!='\0'||port<1||port>65535){
            free(port_str);
            return E_FAIL;
        }
        http_req->port=(int)port;
        free(port_str);
    }else if(http_req->schema!=NULL&&strcasecmp(http_req->schema,"http")==0){
        http_req->port=80;
    }else if(http_req->schema!=NULL&&strcasecmp(http_req->schema,"https")==0){
        http_req->port=443;
    }
    if(http_req->request_line==NULL||http_req->method==NULL||
       http_req->uri==NULL||http_req->http_version==NULL)
        return E_FAIL;
    if(strcmp(http_req->http_version,"1.1")!=0)return E_FAIL;
    printf("REQUEST LINE: %s\n",http_req->request_line);
    printf("METHOD: %s\n",http_req->method);
    printf("URI: %s\n",http_req->uri);
    printf("SCHEMA: %s\n",http_req->schema!=NULL?http_req->schema:"NULL");
    printf("HOST: %s\n",http_req->host!=NULL?http_req->host:"NULL");
    printf("PORT: %d\n",http_req->port);
    printf("PATH: %s\n",http_req->path!=NULL?http_req->path:"NULL");
    printf("PATHNAME: %s\n",http_req->pathname!=NULL?http_req->pathname:"NULL");
    printf("HTTP VERSION: %s\n",http_req->http_version);
    return OK;
}
int http_process_headers(xps_http_req_t *http_req,xps_buffer_t *buff){
    assert(http_req!=NULL);
    assert(buff!=NULL);
    vec_init(&http_req->headers);
    int error;
    while(1){
        error=xps_http_parse_header_line(http_req,buff);
        if(error==E_FAIL||error==E_AGAIN)goto fail;
        if(error==OK&&http_req->header_key_start==NULL)
            break;
        if(error==OK||error==E_NEXT){
            xps_keyval_t *header=malloc(sizeof(xps_keyval_t));
            if(header==NULL)goto fail;
            header->key=str_from_ptrs(http_req->header_key_start,http_req->header_key_end);
            header->val=str_from_ptrs(http_req->header_val_start,http_req->header_val_end);
            if(header->key==NULL||header->val==NULL){
                free(header->key);
                free(header->val);
                free(header);
                goto fail;
            }
            if(vec_push(&http_req->headers,header)!=0){
                free(header->key);
                free(header->val);
                free(header);
                goto fail;
            }
            http_req->header_key_start=NULL;
            http_req->header_key_end=NULL;
            if(error==OK)break;
        }
    }
    printf("HEADERS\n");
    for(int i=0;i<http_req->headers.length;i++){
        xps_keyval_t *header=http_req->headers.data[i];
        if(header!=NULL)printf("%s: %s\n",header->key,header->val);
    }
    return OK;
fail:
    for(int i=0;i<http_req->headers.length;i++){
        xps_keyval_t *header=http_req->headers.data[i];
        if(header!=NULL){
            free(header->key);
            free(header->val);
            free(header);
        }
    }
    vec_deinit(&http_req->headers);
    return error;
}
xps_buffer_t *xps_http_req_serialize(xps_http_req_t *http_req){
    assert(http_req!=NULL);
    if(http_req->request_line==NULL)return NULL;
    xps_buffer_t *headers_str=xps_http_serialize_headers(&http_req->headers);
    if(headers_str==NULL)return NULL;
    size_t request_line_len=strlen(http_req->request_line);
    size_t final_len=request_line_len+2+headers_str->len+2+1;
    xps_buffer_t *buff=xps_buffer_create(final_len,0,NULL);
    if(buff==NULL){
        xps_buffer_destroy(headers_str);
        return NULL;
    }
    int written=sprintf((char *)buff->data,"%s\r\n",http_req->request_line);
    if(written<0){
        xps_buffer_destroy(buff);
        xps_buffer_destroy(headers_str);
        return NULL;
    }
    buff->len=(size_t)written;
    memcpy(buff->data+buff->len,headers_str->data,headers_str->len);
    buff->len+=headers_str->len;
    memcpy(buff->data+buff->len,"\r\n",2);
    buff->len+=2;
    buff->data[buff->len]='\0';
    buff->pos=buff->data;
    xps_buffer_destroy(headers_str);
    return buff;
}
xps_http_req_t *xps_http_req_create(xps_core_t *core,xps_buffer_t *buff,int *error){
    assert(core!=NULL);
    assert(buff!=NULL);
    assert(error!=NULL);
    *error=E_FAIL;
    xps_http_req_t *http_req=calloc(1,sizeof(xps_http_req_t));
    if(http_req==NULL){
        logger(LOG_ERROR,"xps_http_req_create()","calloc() failed for 'http_req'");
        return NULL;
    }
    http_req->parser_state=RL_START;
    http_req->port=-1;
    int process_error=http_process_request_line(http_req,buff);
    if(process_error!=OK){
        *error=process_error;
        xps_http_req_destroy(core,http_req);
        return NULL;
    }
    process_error=http_process_headers(http_req,buff);
    if(process_error!=OK){
        *error=process_error;
        xps_http_req_destroy(core,http_req);
        return NULL;
    }
    const char *host_header=xps_http_get_header(&http_req->headers,"Host");
    if(host_header==NULL||*host_header=='\0'){
        xps_http_req_destroy(core,http_req);
        return NULL;
    }
    http_req->header_len=(size_t)(buff->pos-buff->data);
    http_req->body_len=0;
    const char *body_len_str=xps_http_get_header(&http_req->headers,"Content-Length");
    if(body_len_str!=NULL){
        char *end_ptr=NULL;
        unsigned long long body_len=strtoull(body_len_str,&end_ptr,10);
        if(*body_len_str=='\0'||*end_ptr!='\0'||body_len>SIZE_MAX){
            xps_http_req_destroy(core,http_req);
            return NULL;
        }
        http_req->body_len=(size_t)body_len;
    }
    *error=OK;
    return http_req;
}
void xps_http_req_destroy(xps_core_t *core,xps_http_req_t *http_req){
    assert(http_req!=NULL);
    (void)core;
    free(http_req->request_line);
    free(http_req->method);
    free(http_req->uri);
    free(http_req->schema);
    free(http_req->host);
    free(http_req->path);
    free(http_req->pathname);
    free(http_req->http_version);
    for(int i=0;i<http_req->headers.length;i++){
        xps_keyval_t *header=http_req->headers.data[i];
        if(header!=NULL){
            free(header->key);
            free(header->val);
            free(header);
        }
    }
    vec_deinit(&http_req->headers);
    free(http_req);
}