#ifndef XPS_HTTP_H
#define XPS_HTTP_H

#include "../xps.h"

typedef enum{
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_HEAD,
    HTTP_OPTIONS,
    HTTP_PATCH,
    HTTP_UNKNOWN
}xps_http_method_t;

typedef enum{
    HTTP_200_OK=200,
    HTTP_400_BAD_REQUEST=400,
    HTTP_403_FORBIDDEN=403,
    HTTP_404_NOT_FOUND=404,
    HTTP_500_INTERNAL_SERVER_ERROR=500
}xps_http_status_code_t;

typedef enum{
    RL_START,
    RL_METHOD,
    RL_SP_AFTER_METHOD,
    RL_SCHEMA,
    RL_SCHEMA_SLASH,
    RL_SCHEMA_SLASH_SLASH,
    RL_HOST,
    RL_PORT,
    RL_PATH,
    RL_PATHNAME,
    RL_VERSION_START,
    RL_VERSION_H,
    RL_VERSION_HT,
    RL_VERSION_HTT,
    RL_VERSION_HTTP,
    RL_VERSION_HTTP_SLASH,
    RL_VERSION_MAJOR,
    RL_VERSION_DOT,
    RL_VERSION_MINOR,
    RL_CR,
    RL_LF,
    H_START,
    H_NAME,
    H_COLON,
    H_SP_AFTER_COLON,
    H_VAL,
    H_CR,
    H_LF,
    H_LF_LF,
    H_LF_CR
}xps_http_parser_state_t;

int xps_http_parse_request_line(xps_http_req_t *http_req,xps_buffer_t *buff);
int xps_http_parse_header_line(xps_http_req_t *http_req,xps_buffer_t *buff);
const char *xps_http_get_header(vec_void_t *headers,const char *key);
xps_buffer_t *xps_http_serialize_headers(vec_void_t *headers);

#endif