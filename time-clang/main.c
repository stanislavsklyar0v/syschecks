#include <asm-generic/socket.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

#define PORT 12345
#define BUFFER_SIZE 1024

struct stm_t {
    int step;
    int conn_fd;
    char buf[BUFFER_SIZE];
};

void stm_init(struct stm_t* stm, int conn_fd) {
    stm->conn_fd = conn_fd;
    stm->step = 1;
}

void stm_process(struct stm_t* stm) {
    time_t now;
    int n;
    switch (stm->step) {
        case 1:
            // read request
            if (read(stm->conn_fd, stm->buf, sizeof(stm->buf)) < 0) {
                perror("read failed");
                exit(EXIT_FAILURE);
            }
            stm->step = 2;
            break;
        case 2:
            // write response
            now = time(NULL);
            n = snprintf(stm->buf, sizeof(stm->buf), "HTTP/1.1 200 OK\r\n\r\n%.24s\r\n", ctime(&now));
            if (write(stm->conn_fd, stm->buf, n) < 0) {
                perror("write failed");
                exit(EXIT_FAILURE);
            }
            stm->step = 3;
            break;
        case 3:
            // close connection
            if (close(stm->conn_fd) < 0) {
                perror("close failed");
                exit(EXIT_FAILURE);        
            }
            bzero(stm, sizeof(struct stm_t));
            break;
        default: 
            printf("invalid state %d", stm->step);
            exit(EXIT_FAILURE);    
    }
}

int start_server(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
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

int main(void) {
    int server_fd = start_server();

    struct stm_t stm;
    while (true) {
        int conn_fd = accept(server_fd, (struct sockaddr *)NULL, NULL);
        if (conn_fd < 0) {
            perror("accept failed");
            exit(EXIT_FAILURE);
        }

        stm_init(&stm, conn_fd);

        // read request
        stm_process(&stm);

        // write response
        stm_process(&stm);

         // close connection
         stm_process(&stm);
    }
}
