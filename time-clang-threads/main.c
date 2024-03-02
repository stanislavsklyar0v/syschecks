#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PORT 12345
#define MAX_THREADS 1
#define BUFFER_SIZE 1024

//===============================================
// server
//===============================================

int server_start(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 128) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    return server_fd;
}

void server_run(void) {
    int server_fd = server_start();

    char buf[BUFFER_SIZE];
    while(1) {
        int conn_fd = accept(server_fd, (struct sockaddr*)NULL, NULL);
        if (conn_fd < 0) {
            perror("accept failed");
            exit(EXIT_FAILURE);
        }

        if (read(conn_fd, buf, sizeof(buf)) < 0) {
            perror("read failed");
            exit(EXIT_FAILURE);
        }

        time_t now = time(NULL);
        int n = snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\r\n\r\n%.24s\r\n", ctime(&now));
        if (write(conn_fd, buf, n) < 0) {
            perror("write failed");
            exit(EXIT_FAILURE);
        }

        if (close(conn_fd) < 0) {
            perror("close failed");
            exit(EXIT_FAILURE);
        }
    }    
}

//===============================================
// main
//===============================================

int main(void) {
    printf("server started\n");

    // spawn worker threads
    for (int i = 1; i < MAX_THREADS; ++i) {
        if (fork() == 0)
            break;
    }
    server_run();
}
