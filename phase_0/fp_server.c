#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <string.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5
#define MAX_EPOLL_EVENTS 10

void write_to_file(int conn_sock_fd) {
    char buffer[BUFF_SIZE];
    ssize_t bytes_received;
    FILE *fp;
    const char *filename = "t2.txt";
    fp = fopen(filename, "w");
    if (fp == NULL) {
        perror("[-]Error in creating file");
        exit(EXIT_FAILURE);
    }
    printf("[INFO] Receiving data from client...\n");
    while ((bytes_received = recv(conn_sock_fd, buffer, BUFF_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("[FILE DATA] %s", buffer);
        fprintf(fp, "%s", buffer);
        memset(buffer, 0, BUFF_SIZE);
    }
    if (bytes_received < 0) {
        perror("[-]Error in receiving data");
    }
    fclose(fp);
    printf("[INFO] Data written to file successfully.\n");
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
	socklen_t client_addr_len = sizeof(client_addr);

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
			if (curr_fd==listen_sock_fd) {
                struct sockaddr_in client_addr;
                socklen_t client_addr_len=sizeof(client_addr);
                int conn_sock_fd=accept(listen_sock_fd,(struct sockaddr *)&client_addr,&client_addr_len);
                if (conn_sock_fd < 0) {
                    perror("accept");
                    continue;
                }
                printf("[INFO] Client connected to server\n");
                event.events = EPOLLIN;
                event.data.fd = conn_sock_fd;
                epoll_ctl(epoll_fd,EPOLL_CTL_ADD,conn_sock_fd,&event);
            }
            else {
                printf("[DEBUG] Client socket is ready\n");
                write_to_file(curr_fd);
                epoll_ctl(epoll_fd,EPOLL_CTL_DEL,curr_fd,NULL);
                close(curr_fd);
                printf("[INFO] Client connection closed\n");
            }
		}
	}
}
