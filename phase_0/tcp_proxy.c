#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>

#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5
#define MAX_EPOLL_EVENTS 10
#define UPSTREAM_PORT 3000
#define MAX_SOCKS 1000
#define PORT 8080

int listen_sock_fd, epoll_fd;
struct epoll_event events[MAX_EPOLL_EVENTS];
int route_table[MAX_SOCKS][2], route_table_size = 0; 

int create_loop() {
	int epoll_fd=epoll_create1(0);
	return epoll_fd;
}

void loop_attach(int epoll_fd, int fd, int events) {
	struct epoll_event event;
	event.events=events;
	event.data.fd=fd;
	epoll_ctl(epoll_fd,EPOLL_CTL_ADD,fd,&event);
}

int create_server() {
	int listen_sock_fd=socket(AF_INET,SOCK_STREAM,0);
	int enable=1;
	setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable,sizeof(int));

	//server
	struct sockaddr_in server_addr;

	server_addr.sin_family=AF_INET;
	server_addr.sin_addr.s_addr=htonl(INADDR_ANY);
	server_addr.sin_port=htons(PORT);

	bind(listen_sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
	listen(listen_sock_fd, MAX_ACCEPT_BACKLOG);
	return listen_sock_fd;
}
int connect_upstream() {

  int upstream_sock_fd = socket(AF_INET,SOCK_STREAM,0);

  struct sockaddr_in upstream_addr;
  upstream_addr.sin_family=AF_INET;
  upstream_addr.sin_port=htons(UPSTREAM_PORT);
  upstream_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  connect(upstream_sock_fd,(struct sockaddr*)&upstream_addr,sizeof(upstream_addr));

  return upstream_sock_fd;

}

void accept_connection() {

  int conn_sock_fd = accept(listen_sock_fd, NULL, NULL);

  loop_attach(epoll_fd, conn_sock_fd, EPOLLIN);

  int upstream_sock_fd = connect_upstream();

  loop_attach(epoll_fd, upstream_sock_fd, EPOLLIN);

  route_table[route_table_size][0] = conn_sock_fd;
  route_table[route_table_size][1] = upstream_sock_fd;
  route_table_size += 1;

}

void handle_client(int conn_sock_fd) {

  char buff[BUFF_SIZE];
  int read_n=recv(conn_sock_fd,buff,BUFF_SIZE,0);

  if (read_n <= 0) {
    close(conn_sock_fd);
    return;
  }

  printf("Client: %.*s\n", read_n, buff);

  int upstream_sock_fd=-1;
  for(int i=0;i<route_table_size;i++){

        if(route_table[i][0]==conn_sock_fd){
            upstream_sock_fd=route_table[i][1];
            break;
        }
    }
  
  int bytes_written = 0;
  int message_len = read_n;
  while (bytes_written < message_len) {
    int n = send(upstream_sock_fd, buff + bytes_written, message_len - bytes_written, 0);
    bytes_written += n;
    if (n <= 0)
    	break;
  }

}

void handle_upstream(int upstream_sock_fd) {

  char buff[BUFF_SIZE];
  int read_n=recv(upstream_sock_fd,buff,BUFF_SIZE,0);

  if (read_n <= 0) {
    close(upstream_sock_fd);
    return;
  }

  int conn_sock_fd=-1;
  for(int i=0;i<route_table_size;i++){
      if(route_table[i][1]==upstream_sock_fd){
        conn_sock_fd = route_table[i][0];
        break;
       }
   }
   send(conn_sock_fd, buff, read_n, 0);
}

void loop_run(int epoll_fd) {
    while (1) {
    printf("[DEBUG] Epoll wait\n");

	int n_ready_fds=epoll_wait(epoll_fd,events,MAX_EPOLL_EVENTS,-1);

    for (int i=0;i<n_ready_fds;i++) {
      int fd=events[i].data.fd;
      if (fd==listen_sock_fd)
        accept_connection();
      else 
      {
      	int flag=0;
      	for(int j=0;j<route_table_size;j++){
      		if(route_table[j][0]==fd){
      			flag=1;
      			break;
      		}
      	}
      	if(flag==1){ //is client
      		handle_client(fd);
      	}
      	else{
      		handle_upstream(fd);
      	}
      }
    }

  }
}


int main(){
  listen_sock_fd = create_server();

  epoll_fd = create_loop();

  loop_attach(epoll_fd,listen_sock_fd,EPOLLIN);

  loop_run(epoll_fd);
}
