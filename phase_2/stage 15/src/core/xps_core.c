#include "../xps.h"
#include "xps_core.h"

xps_core_t *xps_core_create() {

  xps_core_t *core = malloc(sizeof(xps_core_t));
  if(core==NULL){
    logger(LOG_ERROR, "()", "xps_core_create() malloc() failed for 'xps_core_create'");
    return NULL;
  }


  xps_loop_t *loop = xps_loop_create(core);
  if(loop==NULL){
    free(core);
    logger(LOG_ERROR, "xps_loop_create()", "malloc() failed for 'event'");
    return NULL;
  }
  

  // Init values
  core->loop = loop;
  vec_init(&(core->listeners));
  vec_init(&(core->connections));
  vec_init(&(core->pipes));
  vec_init(&(core->sessions));
  core->n_null_connections = 0;
  core->n_null_listeners = 0;
  core->n_null_pipes = 0;
  core->n_null_sessions = 0;

  logger(LOG_DEBUG, "xps_core_create()", "created core");

  return core;
}

void xps_core_destroy(xps_core_t *core) {
  assert(core != NULL);

  // Destroy connections
  for (int i = 0; i < core->connections.length; i++) {
    xps_connection_t *connection = core->connections.data[i];
    if (connection != NULL)
      xps_connection_destroy(connection); // modification of xps_connection_destroy() will be look at later
  }
  vec_deinit(&(core->connections));

  /* destory all the listeners and de-initialize core->listeners */
  for(int i=0; i<core->listeners.length; i++){
    xps_listener_t * listener = core->listeners.data[i];
    if(listener != NULL){
        xps_listener_destroy(listener);
    }
  }
  vec_deinit(&(core->listeners));


  // Destroy sessions
  for (int i = 0; i < core->sessions.length; i++) {
    xps_session_t *session = core->sessions.data[i];
    if (session != NULL)
      xps_session_destroy(session); // modification of xps_connection_destroy() will be look at later
  }
  vec_deinit(&(core->sessions));

    // destory all pipes source and sink and deinit
    for(int i = 0; i < core->pipes.length; i++) {
        xps_pipe_t *pipe = core->pipes.data[i];
        if(pipe != NULL) {
            if(pipe->source != NULL) {
                xps_pipe_source_destroy(pipe->source);
            }
            if(pipe->sink != NULL) {
                xps_pipe_sink_destroy(pipe->sink);
            }
            xps_pipe_destroy(pipe);
        }
    }
    vec_deinit(&(core->pipes));

  /* destory loop attached to core */
  if(core->loop != NULL){
    xps_loop_destroy(core->loop);
  }

  /* free core instance */
  free(core);
  core=NULL;

  logger(LOG_DEBUG, "xps_core_destroy()", "destroyed core");
}

void xps_core_start(xps_core_t *core) {

  assert(core!=NULL);

  logger(LOG_DEBUG, "xps_start()", "starting core");

  /* create listeners from port 8001 to 8004 */
    for(int port=8001; port<=8004; port++){
        xps_listener_create(core, "0.0.0.0" ,port);
        logger(LOG_INFO, "main()", "Server listening on port %u", port);
    }
  /* run loop instance using xps_loop_run() */
    xps_loop_run(core->loop);
}