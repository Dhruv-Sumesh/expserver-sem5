#include "../xps.h"
#include "xps_core.h"
xps_core_t *xps_core_create() {
  xps_core_t *core=malloc(sizeof(xps_core_t));
  if(core==NULL){
    logger(LOG_ERROR,"()","memory allocation failed");
    return NULL;
  }
  xps_loop_t *loop=xps_loop_create(core);
  if(loop==NULL){
    free(core);
    logger(LOG_ERROR,"xps_loop_create()","event allocation failed");
    return NULL;
  }
  core->loop=loop;
  vec_init(&(core->listeners));
  vec_init(&(core->connections));
  vec_init(&(core->pipes));
  vec_init(&(core->sessions));
  core->n_null_connections=0;
  core->n_null_listeners=0;
  core->n_null_pipes=0;
  core->n_null_sessions=0;
  logger(LOG_DEBUG,"xps_core_create()","core created");
  return core;
}
void xps_core_destroy(xps_core_t *core) {
  assert(core!=NULL);
  for(int i=0;i<core->connections.length;i++){
    xps_connection_t *connection=core->connections.data[i];
    if(connection!=NULL)
      xps_connection_destroy(connection);
  }
  vec_deinit(&(core->connections));
  for(int i=0;i<core->listeners.length;i++){
    xps_listener_t *listener=core->listeners.data[i];
    if(listener!=NULL){
        xps_listener_destroy(listener);
    }
  }
  vec_deinit(&(core->listeners));
  for(int i=0;i<core->sessions.length;i++){
    xps_session_t *session=core->sessions.data[i];
    if(session!=NULL)
      xps_session_destroy(session);
  }
  vec_deinit(&(core->sessions));
    for(int i=0;i<core->pipes.length;i++){
        xps_pipe_t *pipe=core->pipes.data[i];
        if(pipe!=NULL){
            if(pipe->source!=NULL){
                xps_pipe_source_destroy(pipe->source);
            }
            if(pipe->sink!=NULL){
                xps_pipe_sink_destroy(pipe->sink);
            }
            xps_pipe_destroy(pipe);
        }
    }
    vec_deinit(&(core->pipes));
  if(core->loop!=NULL){
    xps_loop_destroy(core->loop);
  }
  free(core);
  core=NULL;
  logger(LOG_DEBUG,"xps_core_destroy()","core destroyed");
}
void xps_core_start(xps_core_t *core) {
  assert(core!=NULL);
  logger(LOG_DEBUG,"xps_start()","core starting");
    for(int port=8001;port<=8004;port++){
        xps_listener_create(core,"0.0.0.0" ,port);
        logger(LOG_INFO,"main()","server listening on port %u",port);
    }
    xps_loop_run(core->loop);
}
