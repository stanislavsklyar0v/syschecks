#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>

#define PORT 12345
#define MAX_EVENTS 32
#define BUFFER_SIZE 1024

//===============================================
// epoll
//===============================================

void epoll_add(int epoll_fd, int fd, uint32_t events) {
	struct epoll_event ev;
	ev.events = events;
	ev.data.fd = fd;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
		perror("epoll_ctl failed");
		exit(EXIT_FAILURE);
	}
}

//===============================================
// server
//===============================================

void server_setnonblock(int sockfd)
{
    if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
        perror("fcntl failed");
        exit(EXIT_FAILURE);
    }
}

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

    server_setnonblock(server_fd);

    if (listen(server_fd, 128) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    return server_fd;
}

int server_accept(int server_fd) {
    int conn_fd = accept(server_fd, (struct sockaddr*)NULL, NULL);
    if (conn_fd < 0) {
        perror("accept failed");
        exit(EXIT_FAILURE);
    }
    return conn_fd;
}

void server_handle_event(int epoll_fd, int server_fd, struct epoll_event *event) {
    if (event->data.fd == server_fd) {
        // new connection
        //printf("new connection\n");
        int conn_fd = server_accept(server_fd);
        server_setnonblock(conn_fd);
        epoll_add(epoll_fd, conn_fd, EPOLLIN|EPOLLET|EPOLLHUP|EPOLLRDHUP);
    } 
    else if (event->events & EPOLLIN) {
        // handle request
        //printf("handle request\n");
        char buf[BUFFER_SIZE];
        // read incoming request
        if (read(event->data.fd, buf, sizeof(buf)) < 0) {
            perror("read failed");
            exit(EXIT_FAILURE);
        }
        // write response
        time_t now = time(NULL);
        int n = snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\r\n\r\n%.24s\r\n", ctime(&now));
        if (write(event->data.fd, buf, n) < 0) {
            perror("write failed");
            exit(EXIT_FAILURE);
        }
        // close connection and remove it from epoll queue
        //printf("close connection\n");
        if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->data.fd, NULL) < 0) {
            perror("epoll_ctl failed");
            exit(EXIT_FAILURE);
        }
        if (close(event->data.fd) < 0) {
            perror("close failed");
            exit(EXIT_FAILURE);
        }
    }
    else if (event->events & (EPOLLHUP|EPOLLRDHUP)) {
        // connection closed by another side
        //printf("connection closed by another side\n");
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event->data.fd, NULL);
        close(event->data.fd);
    }
    else {
        printf("unexpected");
        exit(EXIT_FAILURE);
    }
}

void server_loop(int epoll_fd, int server_fd) {
    // add initial accept to the queue
    epoll_add(epoll_fd, server_fd, EPOLLIN|EPOLLOUT|EPOLLET);

    // handle events    
    struct epoll_event event_queue[MAX_EVENTS];
    while (1) {
        int event_count = epoll_wait(epoll_fd, event_queue, MAX_EVENTS, -1);
        for (int i = 0; i < event_count; ++i) {
            struct epoll_event *event = &event_queue[i];
            server_handle_event(epoll_fd, server_fd, event);
        }
    }
}

//===============================================
// main
//===============================================

void sigint_handler(int signo) {
    printf("server stopped\n");
    exit(EXIT_SUCCESS);
}

int main(void) {
    signal(SIGINT, sigint_handler);
    
    int server_fd = server_start();
    printf("server started\n");

    int epoll_fd = epoll_create(1);

    server_loop(epoll_fd, server_fd);
}
