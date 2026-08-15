#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define SERVER_PORT 8080
#define BUFF_SIZE 10000

int main(){
	int client_sock_fd=socket(AF_INET,SOCK_STREAM,0);

	struct sockaddr_in server_addr;

	server_addr.sin_family=AF_INET;
	server_addr.sin_addr.s_addr=inet_addr("127.0.0.1");
	server_addr.sin_port=htons(SERVER_PORT);

	if(connect(client_sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr))!=0){
		printf("[ERROR] Failed to connect to tcp server\n");
		exit(1);
	}
	else{
		printf("[INFO] Connected to tcp server\n");
	}

	while(1) {

		//send
		char* line;
		size_t line_len=0, read_n;

		read_n=getline(&line, &line_len, stdin);
		if(read_n==-1){
			free(line);
			break;
		}

		send(client_sock_fd, line, read_n, 0);

		//receive
		char buff[BUFF_SIZE];
		memset(buff, 0, BUFF_SIZE);
		read_n=recv(client_sock_fd, buff, BUFF_SIZE -1, 0);

		if(read_n < 0){
			printf("[INFO] Error occured. Closing server\n");
			close(client_sock_fd);
			exit(1);
		}
		else if(read_n==0){
			printf("[INFO] Client Disconnected. Closing server\n");
			close(client_sock_fd);
			exit(1);
		}

		printf("[SERVER MESSAGE] %s\n", buff);
		free(line);
	}
	return 0;
}