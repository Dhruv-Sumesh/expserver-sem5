#ifndef XPS_HTTP_REQ_H
#define XPS_HTTP_REQ_H

#include "../xps.h"

struct xps_http_req_s{
    xps_http_parser_state_t parser_state;
    xps_http_method_t method_n;
    char *request_line;
    char *method;
    char *uri;
    char *schema;
    char *host;
    int port;
    char *path;
    char *pathname;
    char *http_version;
    const char *request_line_start;
    const char *request_line_end;
    const char *method_start;
    const char *method_end;
    const char *uri_start;
    const char *uri_end;
    const char *schema_start;
    const char *schema_end;
    const char *host_start;
    const char *host_end;
    const char *port_start;
    const char *port_end;
    const char *path_start;
    const char *path_end;
    const char *pathname_start;
    const char *pathname_end;
    const char *http_major;
    const char *http_minor;
    const char *header_key_start;
    const char *header_key_end;
    const char *header_val_start;
    const char *header_val_end;
    size_t header_len;
    size_t body_len;
    vec_void_t headers;
};

typedef struct xps_http_req_s xps_http_req_t;

int http_process_request_line(xps_http_req_t *http_req,xps_buffer_t *buff);
int http_process_headers(xps_http_req_t *http_req,xps_buffer_t *buff);
xps_buffer_t *xps_http_req_serialize(xps_http_req_t *http_req);
xps_http_req_t *xps_http_req_create(xps_core_t *core,xps_buffer_t *buff,int *error);
void xps_http_req_destroy(xps_core_t *core,xps_http_req_t *http_req);

#endif