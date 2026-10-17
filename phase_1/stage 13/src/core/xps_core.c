#include "../xps.h"
xps_core_t *xps_core_create(){
    xps_core_t *core=malloc(sizeof(xps_core_t));
    if(core==NULL){
        logger(LOG_ERROR,"xps_core_create()","malloc() failed for 'core'");
        return NULL;
    }
    xps_loop_t *loop=xps_loop_create(core);
    if(loop==NULL){
        free(core);
        return NULL;
    }
    vec_init(&core->listeners);
    vec_init(&core->connections);
    vec_init(&core->pipes);
    vec_init(&core->sessions);
    core->loop=loop;
    core->n_null_listeners=0;
    core->n_null_connections=0;
    core->n_null_pipes=0;
    core->n_null_sessions=0;
    logger(LOG_DEBUG,"xps_core_create()","created core");
    return core;
}
void xps_core_destroy(xps_core_t *core){
    assert(core!=NULL);
    for(int i=0;i<core->sessions.length;i++){
        xps_session_t *session=core->sessions.data[i];
        if(session!=NULL)xps_session_destroy(session);
    }
    vec_deinit(&core->sessions);
    for(int i=0;i<core->connections.length;i++){
        xps_connection_t *connection=core->connections.data[i];
        if(connection!=NULL)xps_connection_destroy(connection);
    }
    vec_deinit(&core->connections);
    for(int i=0;i<core->pipes.length;i++){
        xps_pipe_t *pipe=core->pipes.data[i];
        if(pipe!=NULL)xps_pipe_destroy(pipe);
    }
    vec_deinit(&core->pipes);
    for(int i=0;i<core->listeners.length;i++){
        xps_listener_t *listener=core->listeners.data[i];
        if(listener!=NULL)xps_listener_destroy(listener);
    }
    vec_deinit(&core->listeners);
    xps_loop_destroy(core->loop);
    free(core);
    logger(LOG_DEBUG,"xps_core_destroy()","destroyed core");
}
void xps_core_start(xps_core_t *core){
    assert(core!=NULL);
    logger(LOG_DEBUG,"xps_start()","starting core");
    xps_listener_t *proxy_listener=xps_listener_create(core,"0.0.0.0",8001);
    if(proxy_listener==NULL)
        logger(LOG_ERROR,"xps_start()","failed to create proxy listener");
    else
        logger(LOG_INFO,"xps_start()","Proxy listening on http://0.0.0.0:8001");
    xps_listener_t *file_listener=xps_listener_create(core,"0.0.0.0",8002);
    if(file_listener==NULL)
        logger(LOG_ERROR,"xps_start()","failed to create file listener");
    else
        logger(LOG_INFO,"xps_start()","File server listening on http://0.0.0.0:8002");
    xps_loop_run(core->loop);
}