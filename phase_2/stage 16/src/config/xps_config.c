#include "../xps.h"

xps_config_t *xps_config_create(const char *config_path){
    assert(config_path!=NULL);
    xps_config_t *config=calloc(1,sizeof(xps_config_t));
    if(config==NULL){
        logger(LOG_ERROR,"xps_config_create()","malloc() failed for config");
        return NULL;
    }
    config->config_path=config_path;
    vec_init(&config->servers);
    vec_init(&config->_all_listeners);
    config->_config_json=json_parse_file(config_path);
    if(config->_config_json==NULL){
        logger(LOG_ERROR,"xps_config_create()","json_parse_file() failed");
        xps_config_destroy(config);
        return NULL;
    }
    JSON_Object *root_object=json_value_get_object(config->_config_json);
    if(root_object==NULL){
        logger(LOG_ERROR,"xps_config_create()","invalid JSON root object");
        xps_config_destroy(config);
        return NULL;
    }
    config->server_name=json_object_get_string(root_object,"server_name");
    JSON_Array *servers=json_object_get_array(root_object,"servers");
    if(config->server_name==NULL||servers==NULL){
        logger(LOG_ERROR,"xps_config_create()","missing server_name or servers");
        xps_config_destroy(config);
        return NULL;
    }
    for(size_t i=0;i<json_array_get_count(servers);i++){
        JSON_Object *server_object=json_array_get_object(servers,i);
        if(server_object==NULL){
            logger(LOG_ERROR,"xps_config_create()","invalid server object");
            xps_config_destroy(config);
            return NULL;
        }
        xps_config_server_t *server=calloc(1,sizeof(xps_config_server_t));
        if(server==NULL){
            logger(LOG_ERROR,"xps_config_create()","malloc() failed for server");
            xps_config_destroy(config);
            return NULL;
        }
        vec_init(&server->listeners);
        vec_init(&server->hostnames);
        vec_init(&server->routes);
        JSON_Array *listeners=json_object_get_array(server_object,"listeners");
        JSON_Array *routes=json_object_get_array(server_object,"routes");
        if(listeners==NULL){
            logger(LOG_ERROR,"xps_config_create()","missing listeners");
            vec_deinit(&server->listeners);
            vec_deinit(&server->hostnames);
            vec_deinit(&server->routes);
            free(server);
            xps_config_destroy(config);
            return NULL;
        }
        for(size_t j=0;j<json_array_get_count(listeners);j++){
            JSON_Object *listener_object=json_array_get_object(listeners,j);
            if(listener_object==NULL){
                logger(LOG_ERROR,"xps_config_create()","invalid listener object");
                free(server);
                xps_config_destroy(config);
                return NULL;
            }
            xps_config_listener_t *listener=malloc(sizeof(xps_config_listener_t));
            if(listener==NULL){
                logger(LOG_ERROR,"xps_config_create()","malloc() failed for listener");
                free(server);
                xps_config_destroy(config);
                return NULL;
            }
            listener->host=json_object_get_string(listener_object,"host");
            listener->port=(u_int)json_object_get_number(listener_object,"port");
            if(listener->host==NULL||listener->port==0){
                logger(LOG_ERROR,"xps_config_create()","invalid listener configuration");
                free(listener);
                free(server);
                xps_config_destroy(config);
                return NULL;
            }
            vec_push(&server->listeners,listener);
            vec_push(&config->_all_listeners,listener);
        }
        size_t n_routes=routes!=NULL?json_array_get_count(routes):0;
        for(size_t j=0;j<n_routes;j++){
            JSON_Object *route_object=json_array_get_object(routes,j);
            if(route_object==NULL){
                logger(LOG_ERROR,"xps_config_create()","invalid route object");
                free(server);
                xps_config_destroy(config);
                return NULL;
            }
            xps_config_route_t *route=calloc(1,sizeof(xps_config_route_t));
            if(route==NULL){
                logger(LOG_ERROR,"xps_config_create()","malloc() failed for route");
                free(server);
                xps_config_destroy(config);
                return NULL;
            }
            route->req_path=json_object_get_string(route_object,"req_path");
            route->type=json_object_get_string(route_object,"type");
            route->dir_path=json_object_get_string(route_object,"dir_path");
            route->http_status_code=(u_int)json_object_get_number(route_object,"http_status_code");
            route->redirect_url=json_object_get_string(route_object,"redirect_url");
            vec_init(&route->index);
            vec_init(&route->upstreams);
            if(route->req_path==NULL||route->type==NULL){
                logger(LOG_ERROR,"xps_config_create()","invalid route configuration");
                free(route);
                free(server);
                xps_config_destroy(config);
                return NULL;
            }
            JSON_Array *index=json_object_get_array(route_object,"index");
            if(index!=NULL){
                for(size_t k=0;k<json_array_get_count(index);k++){
                    const char *filename=json_array_get_string(index,k);
                    if(filename==NULL){
                        logger(LOG_ERROR,"xps_config_create()","invalid index entry");
                        free(route);
                        free(server);
                        xps_config_destroy(config);
                        return NULL;
                    }
                    vec_push(&route->index,(void *)filename);
                }
            }
            JSON_Array *upstreams=json_object_get_array(route_object,"upstreams");
            if(upstreams!=NULL){
                for(size_t k=0;k<json_array_get_count(upstreams);k++){
                    const char *upstream=json_array_get_string(upstreams,k);
                    if(upstream==NULL){
                        logger(LOG_ERROR,"xps_config_create()","invalid upstream entry");
                        free(route);
                        free(server);
                        xps_config_destroy(config);
                        return NULL;
                    }
                    vec_push(&route->upstreams,(void *)upstream);
                }
            }
            vec_push(&server->routes,route);
        }
        vec_push(&config->servers,server);
    }
    logger(LOG_DEBUG,"xps_config_create()","created config");
    return config;
}



void xps_config_destroy(xps_config_t *config){
    assert(config!=NULL);

    for(int i=0;i<config->servers.length;i++){
        xps_config_server_t *server=config->servers.data[i];

        for(int j=0;j<server->listeners.length;j++){
            xps_config_listener_t *listener=server->listeners.data[j];
            free(listener);
        }

        vec_deinit(&server->listeners);
        vec_deinit(&server->hostnames);

        for(int j=0;j<server->routes.length;j++){
            xps_config_route_t *route=server->routes.data[j];

            vec_deinit(&route->index);
            vec_deinit(&route->upstreams);
            free(route);
        }

        vec_deinit(&server->routes);
        free(server);
    }

    vec_deinit(&config->servers);
    vec_deinit(&config->_all_listeners);

    json_value_free(config->_config_json);
    free(config);
}

static bool config_path_is_within(const char *root,const char *path){
    size_t len=strlen(root);
    if(strncmp(root,path,len)!=0)return false;
    if(len==1&&root[0]=='/')return true;
    return path[len]=='\0'||path[len]=='/';
}

xps_config_lookup_t *xps_config_lookup(xps_config_t *config,xps_http_req_t *http_req,xps_connection_t *client,int *error){
    assert(config!=NULL);
    assert(http_req!=NULL);
    assert(client!=NULL);
    assert(error!=NULL);
    *error=E_FAIL;

    xps_config_server_t *selected_server=NULL;
    for(int i=0;i<config->servers.length;i++){
        xps_config_server_t *server=config->servers.data[i];
        bool listener_match=false;
        for(int j=0;j<server->listeners.length;j++){
            xps_config_listener_t *listener=server->listeners.data[j];
            if(listener->port==client->listener->port && strcmp(listener->host,client->listener->host)==0){
                listener_match=true;
                break;
            }
        }
        if(!listener_match)continue;
        if(server->hostnames.length==0){selected_server=server;break;}
        for(int j=0;j<server->hostnames.length;j++){
            const char *hostname=server->hostnames.data[j];
            if(http_req->host!=NULL&&strcmp(hostname,http_req->host)==0){selected_server=server;break;}
        }
        if(selected_server!=NULL)break;
    }
    if(selected_server==NULL){*error=E_NOTFOUND;return NULL;}

    xps_config_route_t *selected_route=NULL;
    size_t best_match_len=0;
    for(int i=0;i<selected_server->routes.length;i++){
        xps_config_route_t *route=selected_server->routes.data[i];
        size_t len=strlen(route->req_path);
        if(len>=best_match_len&&str_starts_with(http_req->pathname,route->req_path)){
            selected_route=route;
            best_match_len=len;
        }
    }
    if(selected_route==NULL){*error=E_NOTFOUND;return NULL;}

    xps_config_lookup_t *lookup=calloc(1,sizeof(xps_config_lookup_t));
    if(lookup==NULL)return NULL;
    vec_init(&lookup->ip_whitelist);
    vec_init(&lookup->ip_blacklist);

    if(strcmp(selected_route->type,"file_serve")==0){
        lookup->type=REQ_FILE_SERVE;
        if(selected_route->dir_path!=NULL){
            char *root=realpath(selected_route->dir_path,NULL);
            const char *relative=http_req->pathname+strlen(selected_route->req_path);
            if(strcmp(selected_route->req_path,"/")==0)relative=http_req->pathname;
            if(*relative=='\0')relative="/";
            char *resource=root!=NULL?path_join(root,relative):NULL;
            char *resolved=resource!=NULL?realpath(resource,NULL):NULL;
            if(resolved!=NULL&&config_path_is_within(root,resolved)){
                if(is_file(resolved))lookup->file_path=resolved;
                else if(is_dir(resolved)){
                    for(int i=0;i<selected_route->index.length;i++){
                        char *candidate=path_join(resolved,selected_route->index.data[i]);
                        char *index_path=candidate!=NULL?realpath(candidate,NULL):NULL;
                        free(candidate);
                        if(index_path!=NULL&&config_path_is_within(root,index_path)&&is_file(index_path)){
                            lookup->file_path=index_path;
                            break;
                        }
                        free(index_path);
                    }
                    free(resolved);
                }else free(resolved);
            }else free(resolved);
            free(resource);
            free(root);
        }
    }else if(strcmp(selected_route->type,"reverse_proxy")==0){
        lookup->type=REQ_REVERSE_PROXY;
        if(selected_route->upstreams.length>0)lookup->upstream=selected_route->upstreams.data[0];
    }else if(strcmp(selected_route->type,"redirect")==0){
        lookup->type=REQ_REDIRECT;
        lookup->http_status_code=selected_route->http_status_code;
        lookup->redirect_url=selected_route->redirect_url;
    }else if(strcmp(selected_route->type,"metrics")==0)lookup->type=REQ_METRICS;
    else lookup->type=REQ_INVALID;

    *error=OK;
    return lookup;
}

void xps_config_lookup_destroy(xps_config_lookup_t *lookup,xps_core_t *core){
    (void)core;
    if(lookup==NULL)return;
    free(lookup->file_path);
    vec_deinit(&lookup->ip_whitelist);
    vec_deinit(&lookup->ip_blacklist);
    free(lookup);
}
