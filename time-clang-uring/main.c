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
#define MAX_THREADS 2
#define QUEUE_SIZE 1
#define BUFFER_SIZE 1024

//===============================================
// io_uring
//===============================================

void io_uring_enqueue_accept(struct io_uring *ring, int server_fd, void *user_data) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_accept(sqe, server_fd, (struct sockaddr *)NULL, NULL, 0);
    io_uring_sqe_set_data(sqe, user_data);
    
    if (io_uring_submit(ring) < 0) {
        perror("io_uring_submit failed");
        exit(EXIT_FAILURE);
    }
}

void io_uring_enqueue_read(struct io_uring *ring, int fd, struct iovec *io, void *user_data) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_readv(sqe, fd, io, 1, 0);
    io_uring_sqe_set_data(sqe, user_data);

    if (io_uring_submit(ring) < 0) {
        perror("io_uring_submit failed");
        exit(EXIT_FAILURE);
    }
}

void io_uring_enqueue_write(struct io_uring *ring, int fd, struct iovec *io, void *user_data) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_writev(sqe, fd, io, 1, 0);
    io_uring_sqe_set_data(sqe, user_data);

    if (io_uring_submit(ring) < 0) {
        perror("io_uring_submit failed");
        exit(EXIT_FAILURE);
    }
}

void io_uring_enqueue_close(struct io_uring *ring, int fd, void *user_data) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_close(sqe, fd);
    io_uring_sqe_set_data(sqe, user_data);
    
    if (io_uring_submit(ring) < 0) {
        perror("io_uring_submit failed");
        exit(EXIT_FAILURE);
    }
}

//===============================================
// stm
//===============================================

#define STM_ACCEPT_CONNECTION 0
#define STM_READ_HTTP_REQUEST 10
#define STM_WRITE_HTTP_RESPONSE 20
#define STM_CLOSE_CONNECTION 30

struct stm_t {
    int step;
    int conn_fd;
    char buf[BUFFER_SIZE];
    struct iovec io;
};

// processing loop
// 1: accept connection
// 2: read http request
// 3: write http response
// 4: close connection

void stm_process(struct io_uring *ring, int server_fd, struct stm_t *stm, int ring_result) {
    if (stm->step == STM_ACCEPT_CONNECTION) {
        bzero(stm, sizeof(struct stm_t)); // reset stm
        //printf("accept connection\n");
        io_uring_enqueue_accept(ring, server_fd, stm);
        stm->step = STM_READ_HTTP_REQUEST;
        return;
    }
    if (stm->step == STM_READ_HTTP_REQUEST) {
        stm->conn_fd = ring_result;
        //printf("read http request\n");
        stm->io.iov_base = stm->buf;
        stm->io.iov_len = sizeof(stm->buf);
        io_uring_enqueue_read(ring, stm->conn_fd, &stm->io, stm);
        stm->step = STM_WRITE_HTTP_RESPONSE;
        return;
    }
    if (stm->step == STM_WRITE_HTTP_RESPONSE) {
        //printf("%.*s\n", ring_result, stm->buf); // print request body
        //printf("write http response\n");
        time_t now = time(NULL);
        stm->io.iov_base = stm->buf;
        stm->io.iov_len = snprintf(stm->buf, sizeof(stm->buf), "HTTP/1.1 200 OK\r\n\r\n%.24s\r\n", ctime(&now));
        io_uring_enqueue_write(ring, stm->conn_fd, &stm->io, stm);
        stm->step = STM_CLOSE_CONNECTION;
        return;
    }
    if (stm->step == STM_CLOSE_CONNECTION) {
        //printf("close connection");
        io_uring_enqueue_close(ring, stm->conn_fd, stm);
        stm->step = STM_ACCEPT_CONNECTION;
        return;
    }
    printf("invalid stm step %d\n", stm->step);
    exit(EXIT_FAILURE);    
}

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

void server_loop(struct io_uring *ring, int server_fd, struct stm_t *stm_vec, unsigned stm_count) {
    // enqueue initial accept operation
    struct stm_t *iter = stm_vec;
    for (int i = 0; i < stm_count; ++i, ++iter)
        stm_process(ring, server_fd, iter, 0);

    struct io_uring_cqe *cqe;
    while (true) {
        if (io_uring_wait_cqe(ring, &cqe) < 0) {
            perror("io_uring_wait_cqe failed");
            exit(EXIT_FAILURE);
        }

        //printf("ring operation finished with result %d\n", cqe->res);
        if (cqe->res < 0) {
            perror("ring operation failed");
            exit(EXIT_FAILURE);
        }

        //printf("%llu\n", cqe->user_data);

        stm_process(ring, server_fd, (struct stm_t*)cqe->user_data, cqe->res);

        io_uring_cqe_seen(ring, cqe);
    }
}

//===============================================
// main
//===============================================

struct io_uring ring;

void sigint_handler(int signo) {
    io_uring_queue_exit(&ring);
    exit(EXIT_SUCCESS);
}

void run(void) {
    signal(SIGINT, sigint_handler);
    
    if (io_uring_queue_init(QUEUE_SIZE, &ring, 0) < 0) {
        perror("io_uring_queue_init failed");
        exit(EXIT_FAILURE);
    }

    struct stm_t stm[QUEUE_SIZE];
    bzero(stm, QUEUE_SIZE * sizeof(struct stm_t));

    int server_fd = server_start();

    server_loop(&ring, server_fd, stm, QUEUE_SIZE);
}

int main(void) {
    printf("server started\n");

    // spawn worker threads
    for (int i = 1; i < MAX_THREADS; ++i) {
        if (fork() == 0)
            break;
    }
    run();
}
