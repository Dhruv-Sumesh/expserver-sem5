#include "../xps.h"
xps_core_t *xps_core_create() {

  xps_core_t *core = malloc(sizeof(xps_core_t));/* allocate memory using malloc() */
  /* handle error where core == NULL */
    if (core == NULL) {
        logger(LOG_ERROR,"xps_core_create()","malloc() failed for 'core'");
        return NULL;
    }
  xps_loop_t *loop = xps_loop_create(core);/* create xps_loop instance */
  /* handle error where loop == NULL */

  // Init values
  core->loop = loop;/* fill this */
  vec_init(&(core->listeners));
  /* initialize core->connections */
  vec_init(&(core->connections));
  core->n_null_listeners = 0;
  /* initialize core->n_null_connections */
  core->n_null_connections = 0;

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
  for (int i = 0; i < core->listeners.length; i++) {

      xps_listener_t *listener =
          core->listeners.data[i];

      if (listener != NULL) {
          xps_listener_destroy(listener);
      }
  }
  /* destory loop attached to core */
  vec_deinit(&(core->listeners));
  xps_loop_destroy(core->loop);
  /* free core instance */
  free(core);

  logger(LOG_DEBUG, "xps_core_destroy()", "destroyed core");
}
void xps_core_start(xps_core_t *core) {
   assert(core != NULL);
    logger(LOG_DEBUG, "xps_start()", "starting core");
    for (u_int port = 8001; port <= 8004; port++) {
        xps_listener_t *listener =
            xps_listener_create(core, "0.0.0.0", port);
        if (listener == NULL) {
            logger(LOG_ERROR,"xps_start()","failed to create listener on port %u",port);
            continue;
        }

        logger(LOG_INFO,"xps_start()","Server listening on http://0.0.0.0:%u",port);
    }

    xps_loop_run(core->loop);
}