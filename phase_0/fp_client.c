#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <string.h>

#define SERVER_PORT 8080
#define BUFF_SIZE 10000

void send_file(int sockfd) {
    FILE *fp;
    const char *filename = "t1.txt";
    fp = fopen(filename, "r");
    if (fp == NULL) {
        perror("[-]Error in opening file.");
        exit(1);
    }
    char data[BUFF_SIZE] = {0};
    printf("[INFO] Sending data to server...\n");
    while (fgets(data, BUFF_SIZE, fp) != NULL) {
        if (send(sockfd, data, strlen(data), 0) == -1) {
            perror("[-]Error in sending data.");
            fclose(fp);
            exit(1);
        }
        printf("[FILE DATA] %s", data);
        bzero(data, BUFF_SIZE);
    }
    printf("[INFO] File data sent successfully.\n");
    fclose(fp);
}

int main() {
    int client_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(SERVER_PORT);
    if (connect(client_sock_fd,(struct sockaddr*)&server_addr,sizeof(server_addr)) != 0) {
        printf("[ERROR] Failed to connect to tcp server\n");
        exit(1);
    }
    printf("[INFO] Connected to tcp server\n");
    send_file(client_sock_fd);
    close(client_sock_fd);
    return 0;
}