#include "xps_http.h"
bool http_strcmp(u_char *str,const char *method,size_t length){
    for(size_t i=0;i<length;i++) {
        u_char c1=str[i];
        u_char c2=(u_char)method[i];
        if(c1>='A'&&c1<='Z') c1 |=0x20;
        if(c2>='A'&&c2<='Z') c2 |=0x20;
        if(c1!=c2) return false;
    }
    if(method[length]!='\0') return false;
    return true;
}
int xps_http_parse_request_line(xps_http_req_t *http_req,xps_buffer_t *buff) {
    xps_http_parser_state_t parser_state=http_req->parser_state;
    u_char *p_ch=buff->pos;
    for(;p_ch<buff->data+buff->len;p_ch++) {
        char ch=*p_ch;
        switch(parser_state) {
            case RL_START:
                http_req->request_line_start=p_ch;
                if(ch==CR||ch==LF) break;
                if(ch<'A'||ch>'Z') return E_FAIL;
                http_req->method_start=p_ch;
                parser_state=RL_METHOD;
                break;
            case RL_METHOD:
                if(ch==' ') {
                    size_t method_len=p_ch - http_req->method_start;
                    if(method_len==3&&http_strcmp(http_req->method_start,"GET",3))
                        http_req->method_n=HTTP_GET;
                    else if(method_len==4&&http_strcmp(http_req->method_start,"POST",4))
                        http_req->method_n=HTTP_POST;
                    else if(method_len==3&&http_strcmp(http_req->method_start,"PUT",3))
                            http_req->method_n=HTTP_PUT;
                    else if(method_len==4&&http_strcmp(http_req->method_start,"HEAD",4))
                        http_req->method_n=HTTP_HEAD;
                    else if(method_len==6&&http_strcmp(http_req->method_start,"DELETE",6))
                        http_req->method_n=HTTP_DELETE;
                    else if(method_len==7&&http_strcmp(http_req->method_start,"OPTIONS",7))
                        http_req->method_n=HTTP_OPTIONS;
                    else if(method_len==5&&http_strcmp(http_req->method_start,"TRACE",5))
                        http_req->method_n=HTTP_TRACE;
                    else if(method_len==7&&http_strcmp(http_req->method_start,"CONNECT",7))
                        http_req->method_n=HTTP_CONNECT;
                    else return E_FAIL;
                    http_req->method_end=p_ch;
                    parser_state=RL_SP_AFTER_METHOD;
                }
                break;
            case RL_SP_AFTER_METHOD:
                if(ch=='/') {
                    http_req->schema_start=p_ch;
					http_req->schema_end=p_ch;
					http_req->host_start=p_ch;
					http_req->host_end=p_ch;
					http_req->port_start=p_ch;
					http_req->port_end=p_ch;
					http_req->uri_start=p_ch;
					http_req->path_start=p_ch;
					http_req->pathname_start=p_ch;
					parser_state=RL_PATH;
                }
                else {
                char c=ch | 0x20;
                if(c<'a'||c>'z') return E_FAIL;
					http_req->schema_start=p_ch;
					http_req->uri_start=p_ch;
					parser_state=RL_SCHEMA;
                }
                break;
            case RL_SCHEMA:
				if(ch==':'){
					http_req->schema_end=p_ch;
					parser_state=RL_SCHEMA_SLASH;
				}
				else {
					char c=ch|0x20;
					if(c<'a'||c>'z') return E_FAIL;
				}
				break;
            case RL_SCHEMA_SLASH:
                if(ch=='/') parser_state=RL_SCHEMA_SLASH_SLASH;
				else return E_FAIL;
                break;
            case RL_SCHEMA_SLASH_SLASH:
                if(ch=='/') {
					http_req->host_start=p_ch + 1;
					parser_state=RL_HOST_START;
				}
				else return E_FAIL;
				break;
            case RL_HOST:
                if(ch==':') {
					http_req->host_end=p_ch;
					http_req->port_start=p_ch + 1;
					parser_state=RL_PORT;
				}
				else if(ch=='/') {
					http_req->host_end=p_ch;
					http_req->port_end=p_ch;
					http_req->path_start=p_ch;
					http_req->pathname_start=p_ch;
					parser_state=RL_PATH;
				}
				else if(ch==' ') {
					http_req->host_end=p_ch;
					http_req->port_end=p_ch;
					http_req->path_start=p_ch;
					http_req->pathname_start=p_ch;
					parser_state=RL_VERSION_START;
				}
				else {
					char c=ch | 0x20;
					if((c<'a'||c>'z')&&(c<'0'||c>'9')&&c!='-'&&c!='.') return E_FAIL;
				}
				break;
            case RL_PORT:
                if(ch=='/') {
					http_req->port_end=p_ch;
					http_req->path_start=p_ch + 1;
					http_req->pathname_start=p_ch + 1;
					parser_state=RL_PATH;
				}
				else if(ch==' ') {
					http_req->port_end=p_ch;
					http_req->uri_end=p_ch;
					http_req->path_start=p_ch;
					http_req->pathname_start=p_ch;
					http_req->path_end=p_ch;
					http_req->pathname_end=p_ch;
					parser_state=RL_VERSION_START;
				}
				else if(ch<'0'||ch>'9') return E_FAIL;
				break;
            case RL_PATH:
                if(ch==' ') {
					http_req->path_end=p_ch;
					http_req->pathname_end=p_ch;
					http_req->uri_end=p_ch;
					parser_state=RL_VERSION_START;
				}
				else if(ch=='?'||ch=='&'||ch=='='||ch=='#') {
					http_req->path_end=p_ch;
					parser_state=RL_PATHNAME;
				}
				else if(ch==CR||ch==LF) return E_FAIL;
				break;
            case RL_PATHNAME:
                if(ch==' ') {
					http_req->uri_end=p_ch;
					http_req->pathname_end=p_ch;
					parser_state=RL_VERSION_START;
				}
				else if(ch==CR||ch==LF) return E_FAIL;
				break;
            case RL_VERSION_START:
                if(ch=='H') parser_state=RL_VERSION_H;
				else if(ch!=' ') return E_FAIL;
				break;
            case RL_VERSION_H:
                if(ch=='T') parser_state=RL_VERSION_HT;
				else return E_FAIL;
				break;
			case RL_VERSION_HT:
				if(ch=='T') parser_state=RL_VERSION_HTT;
				else return E_FAIL;
				break;
			case RL_VERSION_HTT:
				if(ch=='P') parser_state=RL_VERSION_HTTP;
				else return E_FAIL;
				break;
			case RL_VERSION_HTTP:
				if(ch=='/') parser_state=RL_VERSION_HTTP_SLASH;
				else return E_FAIL;
				break;
            case RL_VERSION_HTTP_SLASH:
                if(ch=='1') {
					http_req->http_major=p_ch;
					parser_state=RL_VERSION_MAJOR;
				}
                else return E_FAIL;
				break;
            case RL_VERSION_MAJOR:
                if(ch=='.') parser_state=RL_VERSION_DOT;
				else return E_FAIL;
				break;
			case RL_VERSION_DOT:
				if(ch=='0'||ch=='1') {
					parser_state=RL_VERSION_MINOR;
				} else {
					return E_FAIL;
				}
				break;
			case RL_VERSION_MINOR:
				if(ch==CR) {
					http_req->http_minor=p_ch;
					parser_state=RL_CR;
				} else if(ch==LF) {
					http_req->http_minor=p_ch;
					parser_state=RL_LF;
				} else {
					return E_FAIL;
				}
				break;
				case RL_CR:
					if(ch==LF) {
						parser_state=RL_LF;
					}
					else return E_FAIL;
					break;
				case RL_LF:
					if(http_req->request_line_end==NULL) {
					http_req->request_line_end=p_ch;
					}
					http_req->parser_state=H_START;
					buff->pos=p_ch;
					return OK;
				default:
					return E_FAIL;
        }
    }
    return E_AGAIN;
}
int xps_http_parse_header_line(xps_http_req_t *http_req,xps_buffer_t *buff) {
    assert(http_req!=NULL);
    assert(buff!=NULL);
    u_char *p_ch=buff->pos;
    xps_http_parser_state_t parser_state=http_req->parser_state;
    for(;p_ch<buff->data+buff->len;p_ch++) {
        char ch=*p_ch;
		switch(parser_state) {
			case H_START:
				if((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')) {
					http_req->header_key_start=p_ch;
					parser_state=H_NAME;
				}
				else return E_FAIL;
				break;
			case H_NAME:
				if(ch==':') {
					http_req->header_key_end=p_ch;
					parser_state=H_COLON;
				}
				else if((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_')
					continue;
				else return E_FAIL;
				break;
			case H_COLON:
				if(ch==' ') {
					parser_state=H_SP_AFTER_COLON;
				}
                else {
					http_req->header_val_start=p_ch;
					parser_state=H_VAL;
				}
				break;
			case H_SP_AFTER_COLON:
				if(ch==' ') {
					continue;
				}
                else {
					http_req->header_val_start=p_ch;
					parser_state=H_VAL;
				}
				break;
			case H_VAL:
				if(ch==CR||ch==LF) {
					http_req->header_val_end=p_ch;
					parser_state=(ch==CR) ? H_CR : H_LF;
				}
				break;
			case H_CR:
				if(ch==LF) {
					parser_state=H_LF;
				}
                else {
					return E_FAIL;
				}
				break;
			case H_LF:
				if(ch==LF) {
					parser_state=H_LF_LF;
				}
                else if(ch==CR) {
					parser_state=H_LF_CR;
				}
                else {
					parser_state=H_START;
					buff->pos=p_ch;
					http_req->parser_state=parser_state;
					return E_NEXT;
				}
				break;
			case H_LF_LF:
				buff->pos=p_ch;
				http_req->parser_state=H_START;
				return OK;
			case H_LF_CR:
				if(ch==LF) {
					buff->pos=p_ch;
					http_req->parser_state=H_START;
					return OK;
				} else {
					return E_FAIL;
				}
				break;
			default:
				return E_FAIL;
		}
	}
    return E_AGAIN;
}
const char *xps_http_get_header(vec_void_t *headers,const char *key) {
	assert(headers&&key);
	for(int i=0;i<headers->length;i++) {
		xps_keyval_t *header=headers->data[i];
		if(strcmp(header->key,key)==0) return header->val;
	}
	return NULL;
}
xps_buffer_t *xps_http_serialize_headers(vec_void_t *headers) {
	assert(headers);
	xps_buffer_t *buff=malloc(sizeof(xps_buffer_t));
	if(buff==NULL) return NULL;
	buff->size=DEFAULT_BUFFER_SIZE;
	buff->len=0;
	buff->data=malloc(buff->size);
	buff->pos=buff->data;
	if(buff->data==NULL) {
		free(buff);
		return NULL;
	}
	buff->data[0]='\0';
	for(int i=0;i<headers->length;i++) {
		xps_keyval_t *header=(xps_keyval_t *)headers->data[i];
		size_t h_len=strlen(header->key)+strlen(header->val)+6;
		char header_str[h_len];
		sprintf(header_str,"%s: %s\r\n",header->key,header->val);
		if((buff->size-buff->len)<h_len) {
			u_char *new_data=realloc(buff->data,buff->size*2);
			if(new_data==NULL) {
				free(buff->data);
				free(buff);
				return NULL;
			}
			buff->data=new_data;
			buff->size*=2;
		}
		strcat(buff->data,header_str);
		buff->len=strlen(buff->data);
	}
	return buff;
}
int xps_http_set_header(vec_void_t *headers,const char *key,const char *val){
    if(headers==NULL||key==NULL||val==NULL||key[0]=='\0')return E_FAIL;
    xps_keyval_t *header=malloc(sizeof(*header));
    if(header==NULL)return E_FAIL;
    header->key=strdup(key);
    header->val=strdup(val);
    if(header->key==NULL||header->val==NULL){free(header->key);free(header->val);free(header);return E_FAIL;}
    vec_push(headers,header);
    return OK;
}
