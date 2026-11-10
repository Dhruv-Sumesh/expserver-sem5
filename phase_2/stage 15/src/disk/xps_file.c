#include "xps_file.h"
xps_file_t *xps_file_create(xps_core_t *core,const char *file_path,int *error) {
  assert(core!=NULL&&file_path!=NULL);
  *error=E_FAIL;
    char *resolved_path=realpath(file_path,NULL);
    if(resolved_path==NULL){
        if(errno==ENOENT){
            *error=E_NOTFOUND;
        }
        else if(errno==EACCES) {
            *error=E_PERMISSION;
        }
        logger(LOG_ERROR,"xps_file_create()","realpath() failed for requested file");
        return NULL;
    }
    char *resolved_public=realpath("../public",NULL);
    if(resolved_public==NULL){
        logger(LOG_ERROR,"xps_file_create()","realpath() failed for public directory");
        free(resolved_path);
        return NULL;
    }
  size_t public_len=strlen(resolved_public);
  if(strncmp(resolved_path,resolved_public,public_len)!=0){
    logger(LOG_WARNING,"xps_file_create()","file requested is outside of public directory");
    *error=E_PERMISSION;
    free(resolved_path);
    free(resolved_public);
    return NULL;
  }
    free(resolved_path);
    free(resolved_public);
  struct stat file_stat;
  if(stat(file_path,&file_stat)!=0){
    logger(LOG_ERROR,"xps_file_create()","stat() failed");
    perror("Error message");
    return NULL;
  }
  if(!(file_stat.st_mode & S_IROTH)){
    logger(LOG_WARNING,"xps_file_create()","others do not have read permission");
    *error=E_PERMISSION;
    return NULL;
  }
  long temp_size=file_stat.st_size;
  FILE *file_struct=fopen(file_path,"rb");
  if(file_struct==NULL){
    logger(LOG_ERROR,"xps_file_create()","fopen() failed");
    return NULL;
  }
  const char *mime_type=xps_get_mime(file_path);
  xps_file_t *file=malloc(sizeof(xps_file_t));
  if(file==NULL){
    logger(LOG_ERROR,"xps_file_create()","malloc() failed");
    fclose(file_struct);
    return NULL;
  }
  xps_pipe_source_t *source=
    xps_pipe_source_create((void *)file,file_source_handler,file_source_close_handler);
  if(source==NULL){
    logger(LOG_ERROR,"xps_file_create()","malloc() failed");
    fclose(file_struct);
    free(file);
    return NULL;
  }
    source->ready=true;
    file->core=core;
    file->file_path=file_path;
    file->source=source;
    file->file_struct=file_struct;
    file->size=temp_size;
    file->mime_type=mime_type;
    *error=OK;
  logger(LOG_DEBUG,"xps_file_create()","created file");
  return file;
}
void xps_file_destroy(xps_file_t *file) {
  assert(file!=NULL);
  fclose(file->file_struct);
  if(file->source!=NULL){
      xps_pipe_source_destroy(file->source);
      file->source=NULL;
  }
  free(file);
  logger(LOG_DEBUG,"xps_file_destroy()","destroyed file struct");
}
void file_source_handler(void *ptr) {
  assert(ptr!=NULL);
  xps_pipe_source_t *source=ptr;
  xps_file_t* file=(xps_file_t *)source->ptr;
  xps_buffer_t *buff=xps_buffer_create(DEFAULT_BUFFER_SIZE,0,NULL);
    if(buff==NULL){
        logger(LOG_ERROR,"file_source_handler()","xps_buffer_create() failed");
        return;
    }
  size_t read_n=fread(buff->data,1,buff->size,file->file_struct);
  buff->len=read_n;
  if(ferror(file->file_struct)){
      xps_buffer_destroy(buff);
      xps_file_destroy(file);
      return;
  }
  if(read_n==0&&feof(file->file_struct)){
    xps_buffer_destroy(buff);
    xps_file_destroy(file);
    return;
  }
  int wr=xps_pipe_source_write(source,buff);
  if(wr!=OK){
    logger(LOG_ERROR,"file_source_handler()","xps_pipe_source_write() failed");
    xps_buffer_destroy(buff);
    xps_file_destroy(file);
    return;
  }
  xps_buffer_destroy(buff);
}
void file_source_close_handler(void *ptr) {
    assert(ptr!=NULL);
    xps_pipe_source_t *source=ptr;
    xps_file_t *file=(xps_file_t *)source->ptr;
    if(file!=NULL) xps_file_destroy(file);
}
