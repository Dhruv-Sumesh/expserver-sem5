#include "xps.h"
#include "config/xps_config.h"
#include "utils/xps_cliargs.h"
xps_core_t *core=NULL;
xps_config_t *config=NULL;
xps_cliargs_t *cliargs=NULL;
void sigint_handler(int signum);
int core_create(xps_config_t *config);
void core_destroy(xps_core_t *core);
static void destroy_unattached_listener(xps_listener_t *listener){
    if(listener!=NULL){
        close(listener->sock_fd);
        free(listener);
    }
}
int main(int argc,char *argv[]) {
    signal(SIGINT,sigint_handler);
    cliargs=xps_cliargs_create(argc,argv);
    if(cliargs==NULL)return EXIT_FAILURE;
    config=xps_config_create(cliargs->config_path);
    if(config==NULL){
        xps_cliargs_destroy(cliargs);
        return EXIT_FAILURE;
    }
    if(core_create(config)!=OK){
        xps_config_destroy(config);
        xps_cliargs_destroy(cliargs);
        return EXIT_FAILURE;
    }
    xps_core_start(core);
    core_destroy(core);
    return EXIT_SUCCESS;
}
int core_create(xps_config_t *config) {
    assert(config!=NULL);
    core=xps_core_create(config);
    if(core==NULL)return E_FAIL;
    for(int i=0;i<config->_all_listeners.length;i++){
        xps_config_listener_t *config_listener=config->_all_listeners.data[i];
        xps_listener_t *listener=xps_listener_create(config_listener->host,config_listener->port);
        if(listener==NULL){
            xps_core_destroy(core);
            core=NULL;
            return E_FAIL;
        }
        xps_listener_t *dup_listener=malloc(sizeof(xps_listener_t));
        if(dup_listener==NULL){
            destroy_unattached_listener(listener);
            xps_core_destroy(core);
            core=NULL;
            return E_FAIL;
        }
        dup_listener->core=core;
        dup_listener->host=listener->host;
        dup_listener->port=listener->port;
        dup_listener->sock_fd=dup(listener->sock_fd);
        if(dup_listener->sock_fd<0){
            logger(LOG_ERROR,"core_create()","dup() failed");
            free(dup_listener);
            destroy_unattached_listener(listener);
            xps_core_destroy(core);
            core=NULL;
            return E_FAIL;
        }
        if(xps_loop_attach(core->loop,dup_listener->sock_fd,EPOLLIN|EPOLLET,dup_listener,listener_connection_handler,NULL,NULL)!=OK){
            close(dup_listener->sock_fd);
            free(dup_listener);
            destroy_unattached_listener(listener);
            xps_core_destroy(core);
            core=NULL;
            return E_FAIL;
        }
        vec_push(&core->listeners,dup_listener);
        destroy_unattached_listener(listener);
    }
    return OK;
}
void core_destroy(xps_core_t *core) {
    if(core!=NULL)
        xps_core_destroy(core);
    if(config!=NULL){
        xps_config_destroy(config);
        config=NULL;
    }
    if(cliargs!=NULL){
        xps_cliargs_destroy(cliargs);
        cliargs=NULL;
    }
}
void sigint_handler(int signum) {
    (void)signum;
    logger(LOG_WARNING,"sigint_handler()","SIGINT received");
    core_destroy(core);
    core=NULL;
    exit(EXIT_SUCCESS);
}
