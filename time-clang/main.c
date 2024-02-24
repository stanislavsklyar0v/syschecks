#include <asm-generic/socket.h>
#include <bits/types/struct_iovec.h>
#include <liburing/io_uring.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <liburing.h>

#define PORT 12345
#define QUEUE_SIZE 1
#define BUFFER_SIZE 1024

#define STM_READ_REQUEST 1
#define STM_WRITE_RESPONSE 2
#define STM_CLOSE_CONNECTION 3

struct io_uring ring;

void io_uring_add_accept_request(int server_fd) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe, server_fd, (struct sockaddr *)NULL, NULL, 0);
    
    if (io_uring_submit(&ring) < 0) {
        perror("io_uring_submit failed");
        exit(EXIT_FAILURE);
    }
}

void io_uring_add_read_request(int fd, const struct iovec *io) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_readv(sqe, fd, io, 1, 0);

    if (io_uring_submit(&ring) < 0) {
        perror("io_uring_submit failed");
        exit(EXIT_FAILURE);
    }
}

struct stm_t {
    int step;
    int conn_fd;
    char buf[BUFFER_SIZE];
};

void stm_reset(struct stm_t *stm) {
    bzero(stm, sizeof(struct stm_t));
}

void stm_bind_connection(struct stm_t *stm, int conn_fd) {
    printf("bind connection\n");
    stm->conn_fd = conn_fd;
    stm->step = STM_READ_REQUEST;
}

void stm_process(struct stm_t *stm) {
    time_t now;
    int n;
    switch (stm->step) {
        case STM_READ_REQUEST:
            printf("read request\n");
            if (read(stm->conn_fd, stm->buf, sizeof(stm->buf)) < 0) {
                perror("read failed");
                exit(EXIT_FAILURE);
            }
            stm->step = STM_WRITE_RESPONSE;
            break;
        case STM_WRITE_RESPONSE:
            printf("write response\n");
            now = time(NULL);
            n = snprintf(stm->buf, sizeof(stm->buf), "HTTP/1.1 200 OK\r\n\r\n%.24s\r\n", ctime(&now));
            if (write(stm->conn_fd, stm->buf, n) < 0) {
                perror("write failed");
                exit(EXIT_FAILURE);
            }
            stm->step = STM_CLOSE_CONNECTION;
            break;
        case STM_CLOSE_CONNECTION:
            printf("close connection\n");
            if (close(stm->conn_fd) < 0) {
                perror("close failed");
                exit(EXIT_FAILURE);        
            }
            stm_reset(stm);
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

void server_loop(int server_fd, struct stm_t *stm) {
    io_uring_add_accept_request(server_fd);

    struct io_uring_cqe *cqe;
    while (true) {
        if (io_uring_wait_cqe(&ring, &cqe) < 0) {
            perror("io_uring_wait_cqe failed");
            exit(EXIT_FAILURE);
        }
        
        if (cqe->user_data) {
            // existing connection
        } else {
            // new connection
            printf("new connection\n");
            stm_bind_connection(stm, cqe->res);
            stm_process(stm); // read request
            stm_process(stm); // write response
            stm_process(stm); // close connection
            io_uring_add_accept_request(server_fd);
        }

        io_uring_cqe_seen(&ring, cqe);
    }
}

void sigint_handler(int signo) {
    printf("server stopped\n");
    io_uring_queue_exit(&ring);
    exit(EXIT_SUCCESS);
}

int main(void) {
    signal(SIGINT, sigint_handler);
    
    if (io_uring_queue_init(QUEUE_SIZE, &ring, 0) < 0) {
        perror("io_uring_queue_init failed");
        exit(EXIT_FAILURE);
    }

    struct stm_t stm;
    stm_reset(&stm);

    int server_fd = start_server();
    printf("server started\n");

    server_loop(server_fd, &stm);
}
