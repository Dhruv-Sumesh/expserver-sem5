#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5
#define MAX_EPOLL_EVENTS 10

void strrev(char* str){
	int len = strlen(str);
	int end = len - 1;
	if (len > 0 && str[len - 1] == '\n')
    end--;
	for (int i = 0, j = end; i < j; i++, j--) {
    	char temp = str[i];
    	str[i] = str[j];
   	 	str[j] = temp;

	}
}
int main(){
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
	printf("[INFO] Server listening on port %d\n",PORT);

	//client
	struct sockaddr_in client_addr;
	socklen_t client_addr_len;

	//epoll
	int epoll_fd=epoll_create1(0);
	struct epoll_event event, events[MAX_EPOLL_EVENTS];

	event.events=EPOLLIN;
	event.data.fd=listen_sock_fd;
	epoll_ctl(epoll_fd,EPOLL_CTL_ADD,listen_sock_fd,&event);

	while(1){
		printf("[DEBUG] Epoll wait\n");
		int n_ready_fds=epoll_wait(epoll_fd,events,MAX_EPOLL_EVENTS,-1);

		for(int i=0;i<n_ready_fds;i++){
		    int curr_fd=events[i].data.fd;
			if(curr_fd==listen_sock_fd){
				int conn_sock_fd=accept(listen_sock_fd, (struct sockaddr*)&client_addr, &client_addr_len);
				printf("[INFO] Client connected to server\n");
				event.events=EPOLLIN;
				event.data.fd=conn_sock_fd;
				epoll_ctl(epoll_fd,EPOLL_CTL_ADD,conn_sock_fd,&event);
			}
			else{
				char buff[BUFF_SIZE];
				memset(buff,0,BUFF_SIZE);
				int read_n=recv(curr_fd,buff,BUFF_SIZE-1,0);

				if(read_n<0){
					close(curr_fd);
					epoll_ctl(epoll_fd,EPOLL_CTL_DEL,curr_fd,NULL);
					printf("[INFO] Error occured. Closing server\n");
				}
				else if(read_n==0){
					close(curr_fd);
					epoll_ctl(epoll_fd,EPOLL_CTL_DEL,curr_fd,NULL);
					printf("[INFO] Client Disconnected. Closing server\n");
				}
				else{
					printf("[CLIENT MESSAGE] %s\n", buff);
					strrev(buff);
					send(curr_fd, buff, read_n, 0);
				}
			}
		}
	}
}
