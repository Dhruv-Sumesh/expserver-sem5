#include "../xps.h"
#include <time.h>

static const char *status_text(u_int code){
    switch(code){
        case HTTP_OK:return "OK";
        case HTTP_BAD_REQUEST:return "Bad Request";
        case HTTP_FORBIDDEN:return "Forbidden";
        case HTTP_NOT_FOUND:return "Not Found";
        case HTTP_INTERNAL_SERVER_ERROR:return "Internal Server Error";
        case HTTP_SERVICE_UNAVAILABLE:return "Service Unavailable";
        default:return NULL;
    }
}

xps_http_res_t *xps_http_res_create(xps_core_t *core,u_int code){
    assert(core!=NULL);
    const char *text=status_text(code);
    if(text==NULL){
        logger(LOG_ERROR,"xps_http_res_create()","unsupported HTTP status code");
        return NULL;
    }
    xps_http_res_t *http_res=malloc(sizeof(xps_http_res_t));
    if(http_res==NULL){
        logger(LOG_ERROR,"xps_http_res_create()","failed to alloc memory for http_res. malloc() returned NULL");
        return NULL;
    }
    memset(http_res,0,sizeof(xps_http_res_t));
    int len=snprintf(http_res->response_line,sizeof(http_res->response_line),
                     "HTTP/1.1 %u %s",code,text);
    if(len<0||(size_t)len>=sizeof(http_res->response_line)){
        free(http_res);
        return NULL;
    }
    vec_init(&http_res->headers);
    http_res->body=NULL;

    char time_buf[64];
    time_t now=time(NULL);
    struct tm tm_buf;
    if(now==(time_t)-1||gmtime_r(&now,&tm_buf)==NULL||
       strftime(time_buf,sizeof(time_buf),"%a, %d %b %Y %H:%M:%S GMT",&tm_buf)==0){
        xps_http_res_destroy(http_res);
        logger(LOG_ERROR,"xps_http_res_create()","failed to generate Date header");
        return NULL;
    }
    if(xps_http_set_header(&http_res->headers,"Date",time_buf)!=OK||
       xps_http_set_header(&http_res->headers,"Server",SERVER_NAME)!=OK||
       xps_http_set_header(&http_res->headers,"Access-Control-Allow-Origin","*")!=OK){
        xps_http_res_destroy(http_res);
        return NULL;
    }
    return http_res;
}

void xps_http_res_destroy(xps_http_res_t *res){
    assert(res!=NULL);
    if(res->body!=NULL){
        xps_buffer_destroy(res->body);
        res->body=NULL;
    }
    for(int i=0;i<res->headers.length;i++){
        xps_keyval_t *header=res->headers.data[i];
        if(header!=NULL){
            free(header->key);
            free(header->val);
            free(header);
        }
    }
    vec_deinit(&res->headers);
    free(res);
}

xps_buffer_t *xps_http_res_serialize(xps_http_res_t *http_res){
    assert(http_res!=NULL);
    xps_buffer_t *headers_str=xps_http_serialize_headers(&http_res->headers);
    if(headers_str==NULL){
        logger(LOG_ERROR,"xps_http_res_serialize()","failed to serialize headers");
        return NULL;
    }
    size_t body_len=http_res->body!=NULL?http_res->body->len:0;
    size_t response_line_len=strlen(http_res->response_line);
    size_t final_len=response_line_len+2+headers_str->len+2+body_len;
    xps_buffer_t *buff=xps_buffer_create(final_len>0?final_len:1,0,NULL);
    if(buff==NULL){
        logger(LOG_ERROR,"xps_http_res_serialize()","failed to create buffer instance");
        xps_buffer_destroy(headers_str);
        return NULL;
    }
    size_t pos=0;
    memcpy(buff->data+pos,http_res->response_line,response_line_len);
    pos+=response_line_len;
    memcpy(buff->data+pos,"\r\n",2);
    pos+=2;
    memcpy(buff->data+pos,headers_str->data,headers_str->len);
    pos+=headers_str->len;
    memcpy(buff->data+pos,"\r\n",2);
    pos+=2;
    if(http_res->body!=NULL&&body_len>0){
        memcpy(buff->data+pos,http_res->body->data,body_len);
        pos+=body_len;
    }
    buff->len=pos;
    buff->pos=buff->data;
    xps_buffer_destroy(headers_str);
    return buff;
}

void xps_http_res_set_body(xps_http_res_t *http_res,xps_buffer_t *buff){
    assert(http_res!=NULL);
    if(http_res->body!=NULL)xps_buffer_destroy(http_res->body);
    http_res->body=buff;
}
