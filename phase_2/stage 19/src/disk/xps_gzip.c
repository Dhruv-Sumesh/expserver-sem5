#include "xps_gzip.h"
static void gzip_compress(xps_gzip_t *gzip,bool flush);
static void gzip_check_destroy(xps_gzip_t *gzip);
xps_gzip_t *xps_gzip_create(int level){
    if(level<-1||level>9){
        logger(LOG_ERROR,"xps_gzip_create()","invalid compression level %d",level);
        return NULL;
    }
    xps_gzip_t *gzip=calloc(1,sizeof(xps_gzip_t));
    if(gzip==NULL){
        logger(LOG_ERROR,"xps_gzip_create()","calloc() failed for gzip");
        return NULL;
    }
    gzip->source=xps_pipe_source_create((void *)gzip,gzip_source_handler,gzip_source_close_handler);
    if(gzip->source==NULL){
        free(gzip);
        return NULL;
    }
    gzip->sink=xps_pipe_sink_create((void *)gzip,gzip_sink_handler,gzip_sink_close_handler);
    if(gzip->sink==NULL){
        xps_pipe_source_destroy(gzip->source);
        free(gzip);
        return NULL;
    }
    if(deflateInit2(&(gzip->stream),level,Z_DEFLATED,16+MAX_WBITS,8,Z_DEFAULT_STRATEGY)!=Z_OK){
        logger(LOG_ERROR,"xps_gzip_create()","deflateinit2() failed");
        xps_pipe_source_destroy(gzip->source);
        xps_pipe_sink_destroy(gzip->sink);
        free(gzip);
        return NULL;
    }
    gzip->sink->ready=true;
    gzip->transfer_buff=NULL;
    gzip->total_in_len=0;
    gzip->total_out_len=0;
    gzip->flushed=false;
    return gzip;
}
void xps_gzip_destroy(xps_gzip_t *gzip){
    assert(gzip!=NULL);
    deflateEnd(&(gzip->stream));
    xps_pipe_source_destroy(gzip->source);
    xps_pipe_sink_destroy(gzip->sink);
    if(gzip->transfer_buff)xps_buffer_destroy(gzip->transfer_buff);
    free(gzip);
}
static void gzip_compress(xps_gzip_t *gzip,bool flush){
    assert(gzip!=NULL);
    xps_buffer_t *in_buff=NULL;
    size_t in_len=0;
    if(!flush){
        in_len=gzip->sink->pipe->buff_list->len;
        if(in_len==0)return;
        in_buff=xps_pipe_sink_read(gzip->sink,in_len);
    }else{
        in_buff=xps_buffer_create(1,0,NULL);
    }
    if(in_buff==NULL){
        logger(LOG_ERROR,"gzip_compress()","failed to get input buffer");
        return;
    }
    xps_buffer_list_t *out_buff_list=xps_buffer_list_create();
    if(out_buff_list==NULL){
        xps_buffer_destroy(in_buff);
        return;
    }
    gzip->stream.avail_in=in_buff->len;
    gzip->stream.next_in=in_buff->data;
    int flush_stream=flush?Z_FINISH:Z_NO_FLUSH;
    do{
        xps_buffer_t *out_buff=xps_buffer_create(DEFAULT_BUFFER_SIZE,0,NULL);
        if(out_buff==NULL){
            xps_buffer_list_destroy(out_buff_list);
            xps_buffer_destroy(in_buff);
            return;
        }
        gzip->stream.avail_out=out_buff->size;
        gzip->stream.next_out=out_buff->data;
        int error=deflate(&(gzip->stream),flush_stream);
        assert(error!=Z_STREAM_ERROR);
        (void)error;
        out_buff->len=out_buff->size-gzip->stream.avail_out;
        if(out_buff->len>0)xps_buffer_list_append(out_buff_list,out_buff);
        else xps_buffer_destroy(out_buff);
    }while(gzip->stream.avail_out==0);
    assert(gzip->stream.avail_in==0);
    xps_buffer_t *compressed_buff=NULL;
    if(out_buff_list->len>0)compressed_buff=xps_buffer_list_read(out_buff_list,out_buff_list->len);
    xps_buffer_list_destroy(out_buff_list);
    xps_buffer_destroy(in_buff);
    if(!flush){
        xps_pipe_sink_clear(gzip->sink,in_len);
        gzip->total_in_len+=in_len;
    }else{
        gzip->flushed=true;
    }
    gzip->transfer_buff=compressed_buff;
    gzip->source->ready=(compressed_buff!=NULL);
    gzip->sink->ready=(compressed_buff==NULL);
}
void gzip_sink_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_sink_t *sink=ptr;
    xps_gzip_t *gzip=sink->ptr;
    if(gzip->transfer_buff!=NULL)return;
    gzip_compress(gzip,false);
}
void gzip_sink_close_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_sink_t *sink=ptr;
    xps_gzip_t *gzip=sink->ptr;
    if(!gzip->flushed&&gzip->transfer_buff==NULL)gzip_compress(gzip,true);
    gzip_check_destroy(gzip);
}
void gzip_source_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_source_t *source=ptr;
    xps_gzip_t *gzip=source->ptr;
    if(gzip->transfer_buff==NULL){
        source->ready=false;
        return;
    }
    if(xps_pipe_source_write(source,gzip->transfer_buff)!=OK){
        logger(LOG_ERROR,"gzip_source_handler()","xps_pipe_source_write() failed");
        return;
    }
    gzip->total_out_len+=gzip->transfer_buff->len;
    xps_buffer_destroy(gzip->transfer_buff);
    gzip->transfer_buff=NULL;
    gzip->source->ready=false;
    gzip->sink->ready=true;
    if(!gzip->sink->active&&!gzip->flushed)gzip_compress(gzip,true);
    gzip_check_destroy(gzip);
}
void gzip_source_close_handler(void *ptr){
    assert(ptr!=NULL);
    xps_pipe_source_t *source=ptr;
    xps_gzip_t *gzip=source->ptr;
    gzip_check_destroy(gzip);
}
static void gzip_check_destroy(xps_gzip_t *gzip){
    assert(gzip!=NULL);
    if(!gzip->source->active||(!gzip->sink->active&&gzip->flushed&&gzip->transfer_buff==NULL)){
        xps_gzip_destroy(gzip);
    }
}
