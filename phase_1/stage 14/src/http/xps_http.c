#include "../xps.h"

static int method_from_range(const char *start,const char *end){
    size_t len=(size_t)(end-start);
    if(len==3&&!strncmp(start,"GET",3))return HTTP_GET;
    if(len==4&&!strncmp(start,"POST",4))return HTTP_POST;
    if(len==3&&!strncmp(start,"PUT",3))return HTTP_PUT;
    if(len==6&&!strncmp(start,"DELETE",6))return HTTP_DELETE;
    if(len==4&&!strncmp(start,"HEAD",4))return HTTP_HEAD;
    if(len==7&&!strncmp(start,"OPTIONS",7))return HTTP_OPTIONS;
    if(len==5&&!strncmp(start,"PATCH",5))return HTTP_PATCH;
    return HTTP_UNKNOWN;
}

static bool is_method_char(char c){
    return((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
           c=='-'||c=='_'||c=='!');
}

static bool is_uri_char(char c){
    return c!=' '&&c!='\r'&&c!='\n'&&c!='\0';
}

int xps_http_parse_request_line(xps_http_req_t *http_req,xps_buffer_t *buff){
    assert(http_req!=NULL);
    assert(buff!=NULL);
    const char *p=(const char *)buff->pos;
    const char *end=(const char *)buff->data+buff->len;
    while(p<end){
        char c=*p;
        switch(http_req->parser_state){
            case RL_START:
                if(c=='\r'||c=='\n'){
                    p++;
                    break;
                }
                if(!is_method_char(c))return E_FAIL;
                http_req->request_line_start=p;
                http_req->method_start=p;
                http_req->parser_state=RL_METHOD;
                p++;
                break;
            case RL_METHOD:
                if(is_method_char(c)){
                    p++;
                    break;
                }
                if(c!=' ')return E_FAIL;
                http_req->method_end=p;
                http_req->method_n=method_from_range(http_req->method_start,http_req->method_end);
                if(http_req->method_n==HTTP_UNKNOWN)return E_FAIL;
                http_req->parser_state=RL_SP_AFTER_METHOD;
                p++;
                break;
            case RL_SP_AFTER_METHOD:
                if(c==' ')return E_FAIL;
                if(c=='/'){
                    http_req->uri_start=p;
                    http_req->path_start=p;
                    http_req->pathname_start=p;
                    http_req->parser_state=RL_PATH;
                    p++;
                    break;
                }
                if((c>='A'&&c<='Z')||(c>='a'&&c<='z')){
                    http_req->uri_start=p;
                    http_req->schema_start=p;
                    http_req->parser_state=RL_SCHEMA;
                    p++;
                    break;
                }
                return E_FAIL;
            case RL_SCHEMA:
                if(c==':'){
                    http_req->schema_end=p;
                    http_req->parser_state=RL_SCHEMA_SLASH;
                    p++;
                    break;
                }
                if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
                   (c>='0'&&c<='9')||c=='+'||c=='-'||c=='.'){
                    p++;
                    break;
                }
                return E_FAIL;
            case RL_SCHEMA_SLASH:
                if(c!='/')return E_FAIL;
                http_req->parser_state=RL_SCHEMA_SLASH_SLASH;
                p++;
                break;
            case RL_SCHEMA_SLASH_SLASH:
                if(c!='/')return E_FAIL;
                http_req->host_start=p+1;
                http_req->parser_state=RL_HOST;
                p++;
                break;
            case RL_HOST:
                if(c=='/'){
                    http_req->host_end=p;
                    http_req->path_start=p;
                    http_req->pathname_start=p;
                    http_req->parser_state=RL_PATH;
                    p++;
                    break;
                }
                if(c==':'){
                    http_req->host_end=p;
                    http_req->port_start=p+1;
                    http_req->parser_state=RL_PORT;
                    p++;
                    break;
                }
                if(c==' '){
                    http_req->host_end=p;
                    http_req->path_start=NULL;
                    http_req->pathname_start=NULL;
                    http_req->uri_end=p;
                    http_req->parser_state=RL_VERSION_START;
                    p++;
                    break;
                }
                if(is_uri_char(c)){
                    p++;
                    break;
                }
                return E_FAIL;
            case RL_PORT:
                if(c>='0'&&c<='9'){
                    p++;
                    break;
                }
                if(c=='/'){
                    http_req->port_end=p;
                    if(http_req->port_start==p)return E_FAIL;
                    http_req->path_start=p;
                    http_req->pathname_start=p;
                    http_req->parser_state=RL_PATH;
                    p++;
                    break;
                }
                if(c==' '){
                    http_req->port_end=p;
                    if(http_req->port_start==p)return E_FAIL;
                    http_req->path_start=NULL;
                    http_req->pathname_start=NULL;
                    http_req->uri_end=p;
                    http_req->parser_state=RL_VERSION_START;
                    p++;
                    break;
                }
                return E_FAIL;
            case RL_PATH:
                if(c==' '){
                    http_req->path_end=p;
                    http_req->pathname_end=p;
                    http_req->uri_end=p;
                    http_req->parser_state=RL_VERSION_START;
                    p++;
                    break;
                }
                if(c=='?'){
                    http_req->pathname_end=p;
                    http_req->parser_state=RL_PATHNAME;
                    p++;
                    break;
                }
                if(is_uri_char(c)){
                    p++;
                    break;
                }
                return E_FAIL;
            case RL_PATHNAME:
                if(c==' '){
                    http_req->path_end=p;
                    http_req->uri_end=p;
                    http_req->parser_state=RL_VERSION_START;
                    p++;
                    break;
                }
                if(is_uri_char(c)){
                    p++;
                    break;
                }
                return E_FAIL;
            case RL_VERSION_START:
                if(c!='H')return E_FAIL;
                http_req->parser_state=RL_VERSION_H;
                p++;
                break;
            case RL_VERSION_H:
                if(c!='T')return E_FAIL;
                http_req->parser_state=RL_VERSION_HT;
                p++;
                break;
            case RL_VERSION_HT:
                if(c!='T')return E_FAIL;
                http_req->parser_state=RL_VERSION_HTT;
                p++;
                break;
            case RL_VERSION_HTT:
                if(c!='P')return E_FAIL;
                http_req->parser_state=RL_VERSION_HTTP;
                p++;
                break;
            case RL_VERSION_HTTP:
                if(c!='/')return E_FAIL;
                http_req->parser_state=RL_VERSION_HTTP_SLASH;
                p++;
                break;
            case RL_VERSION_HTTP_SLASH:
                if(c<'0'||c>'9')return E_FAIL;
                http_req->http_major=p;
                http_req->parser_state=RL_VERSION_MAJOR;
                p++;
                break;
            case RL_VERSION_MAJOR:
                if(c=='.'){
                    http_req->parser_state=RL_VERSION_DOT;
                    p++;
                    break;
                }
                if(c<'0'||c>'9')return E_FAIL;
                p++;
                break;
            case RL_VERSION_DOT:
                if(c<'0'||c>'9')return E_FAIL;
                http_req->http_minor=p;
                http_req->parser_state=RL_VERSION_MINOR;
                p++;
                break;
            case RL_VERSION_MINOR:
                if(c>='0'&&c<='9'){
                    p++;
                    break;
                }
                if(c=='\r'){
                    http_req->request_line_end=p;
                    http_req->parser_state=RL_CR;
                    p++;
                    break;
                }
                if(c=='\n'){
                    http_req->request_line_end=p;
                    http_req->parser_state=RL_LF;
                    p++;
                    buff->pos=(u_char *)p;
                    http_req->parser_state=H_START;
                    return OK;
                }
                return E_FAIL;
            case RL_CR:
                if(c!='\n')return E_FAIL;
                buff->pos=(u_char *)(p+1);
                http_req->parser_state=H_START;
                return OK;
            default:
                return E_FAIL;
        }
    }
    buff->pos=(u_char *)p;
    return E_AGAIN;
}

int xps_http_parse_header_line(xps_http_req_t *http_req,xps_buffer_t *buff){
    assert(http_req!=NULL);
    assert(buff!=NULL);
    const char *p=(const char *)buff->pos;
    const char *end=(const char *)buff->data+buff->len;
    while(p<end){
        char c=*p;
        switch(http_req->parser_state){
            case H_START:
                if(c=='\r'){
                    http_req->header_key_start=NULL;
                    http_req->parser_state=H_CR;
                    p++;
                    break;
                }
                if(c=='\n'){
                    http_req->header_key_start=NULL;
                    http_req->parser_state=H_LF_LF;
                    buff->pos=(u_char *)(p+1);
                    return OK;
                }
                if((c>='A'&&c<='Z')||(c>='a'&&c<='z')){
                    http_req->header_key_start=p;
                    http_req->parser_state=H_NAME;
                    p++;
                    break;
                }
                return E_FAIL;
            case H_NAME:
                if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='-'||
                   (c>='0'&&c<='9')){
                    p++;
                    break;
                }
                if(c==':'){
                    http_req->header_key_end=p;
                    http_req->parser_state=H_COLON;
                    p++;
                    break;
                }
                return E_FAIL;
            case H_COLON:
                if(c==' '||c=='\t'){
                    http_req->parser_state=H_SP_AFTER_COLON;
                    p++;
                    break;
                }
                if(c=='\r'){
                    http_req->header_val_start=p;
                    http_req->header_val_end=p;
                    http_req->parser_state=H_CR;
                    p++;
                    break;
                }
                if(c=='\n'){
                    http_req->header_val_start=p;
                    http_req->header_val_end=p;
                    http_req->parser_state=H_LF;
                    p++;
                    break;
                }
                http_req->header_val_start=p;
                http_req->parser_state=H_VAL;
                p++;
                break;
            case H_SP_AFTER_COLON:
                if(c==' '||c=='\t'){
                    p++;
                    break;
                }
                if(c=='\r'){
                    http_req->header_val_start=p;
                    http_req->header_val_end=p;
                    http_req->parser_state=H_CR;
                    p++;
                    break;
                }
                if(c=='\n'){
                    http_req->header_val_start=p;
                    http_req->header_val_end=p;
                    http_req->parser_state=H_LF;
                    p++;
                    break;
                }
                http_req->header_val_start=p;
                http_req->parser_state=H_VAL;
                p++;
                break;
            case H_VAL:
                if(c=='\r'){
                    http_req->header_val_end=p;
                    http_req->parser_state=H_CR;
                    p++;
                    break;
                }
                if(c=='\n'){
                    http_req->header_val_end=p;
                    http_req->parser_state=H_LF;
                    p++;
                    break;
                }
                p++;
                break;
            case H_CR:
                if(c!='\n')return E_FAIL;
                p++;
                if(http_req->header_key_start==NULL){
                    http_req->parser_state=H_LF_CR;
                    buff->pos=(u_char *)p;
                    return OK;
                }
                http_req->parser_state=H_START;
                buff->pos=(u_char *)p;
                return E_NEXT;
            case H_LF:
                if(http_req->header_key_start==NULL){
                    http_req->parser_state=H_LF_LF;
                    buff->pos=(u_char *)p;
                    return OK;
                }
                http_req->parser_state=H_START;
                buff->pos=(u_char *)p;
                return E_NEXT;
            case H_LF_LF:
            case H_LF_CR:
                return OK;
            default:
                return E_FAIL;
        }
    }
    buff->pos=(u_char *)p;
    return E_AGAIN;
}

const char *xps_http_get_header(vec_void_t *headers,const char *key){
    assert(headers!=NULL);
    assert(key!=NULL);
    for(int i=0;i<headers->length;i++){
        xps_keyval_t *header=headers->data[i];
        if(header!=NULL&&header->key!=NULL&&
           strcasecmp(header->key,key)==0)
            return header->val;
    }
    return NULL;
}

xps_buffer_t *xps_http_serialize_headers(vec_void_t *headers){
    assert(headers!=NULL);
    size_t size=1;
    for(int i=0;i<headers->length;i++){
        xps_keyval_t *header=headers->data[i];
        if(header==NULL||header->key==NULL||header->val==NULL)continue;
        size+=strlen(header->key)+strlen(header->val)+4;
    }
    xps_buffer_t *buff=xps_buffer_create(size,0,NULL);
    if(buff==NULL)return NULL;
    buff->data[0]='\0';
    for(int i=0;i<headers->length;i++){
        xps_keyval_t *header=headers->data[i];
        if(header==NULL||header->key==NULL||header->val==NULL)continue;
        size_t needed=strlen(header->key)+strlen(header->val)+4;
        if(buff->size-buff->len<needed){
            size_t new_size=buff->size*2;
            while(new_size-buff->len<needed)new_size*=2;
            u_char *new_data=realloc(buff->data,new_size);
            if(new_data==NULL){
                xps_buffer_destroy(buff);
                return NULL;
            }
            buff->data=new_data;
            buff->size=new_size;
            buff->pos=buff->data+buff->len;
        }
        int written=sprintf((char *)buff->data+buff->len,
                            "%s: %s\r\n",header->key,header->val);
        if(written<0){
            xps_buffer_destroy(buff);
            return NULL;
        }
        buff->len+=(size_t)written;
    }
    buff->pos=buff->data+buff->len;
    return buff;
}