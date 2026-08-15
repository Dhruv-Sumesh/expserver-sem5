#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#define PORT 8080
#define BUFF_SIZE 10000

typedef struct{
	int sockfd;
	char message[BUFF_SIZE];
	struct sockaddr_in server_addr, client_addr;
	pthread_t thread_id;
	socklen_t addr_len;

}client_data_t;

void strrev(char *str){
	for(int start=0,end=strlen(str)-1;start<end;start++,end--){
		char temp=str[start];
		str[start]=str[end];
		str[end]=temp;
	}
}

void* handle_client(void* arg) {
	client_data_t* data=(client_data_t*) arg;
	strrev(data->message);
	sendto(data->sockfd, data->message, strlen(data->message),0,(struct sockaddr*)&(data->client_addr),data->addr_len);
	free(data);
	pthread_exit(NULL);
}

int main(){
	int sockfd;
	char buff[BUFF_SIZE];
	struct sockaddr_in server_addr, client_addr;
	pthread_t thread_id;

	sockfd=socket(AF_INET,SOCK_DGRAM,0);

	server_addr.sin_family=AF_INET;
	server_addr.sin_addr.s_addr=htonl(INADDR_ANY);
	server_addr.sin_port=htons(PORT);

	bind(sockfd,(struct sockaddr*)&server_addr,sizeof(server_addr));

    printf("[INFO] server listening on port %d\n",PORT);

    while(1){
    	socklen_t len=sizeof(client_addr);
    	ssize_t n=recvfrom(sockfd,buff,sizeof(buff)-1,0,(struct sockaddr*)&client_addr,&len);
    	buff[n]='\0';

    	client_data_t* data = (client_data_t*)malloc(sizeof(client_data_t));

    	strcpy(data->message,buff);
    	data->client_addr=client_addr;
    	data->sockfd=sockfd;
    	data->addr_len=len;

    	if(pthread_create(&thread_id,NULL,handle_client,(void*)data)!=0){
    		perror("Failed to create thread");
    		free(data);
    	}

    	pthread_detach(thread_id);
    }
    close(sockfd);
    return 0;
}