#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static int connect_to_server(const char *host, const char *port) {
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_NUMERICSERV,
    };
    struct addrinfo *addresses;
    int error = getaddrinfo(host, port, &hints, &addresses);
    if (error != 0) {
        fprintf(stderr, "%s: %s\n", host, gai_strerror(error));
        return -1;
    }

    int fd = -1;
    int last_error = EADDRNOTAVAIL;
    for (struct addrinfo *address = addresses; address != NULL; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) {
            last_error = errno;
            continue;
        }
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        last_error = errno;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd < 0) {
        errno = last_error;
        perror("connect");
    }
    return fd;
}

static int write_all(int fd, const char *buffer, size_t length) {
    while (length > 0) {
        ssize_t count = write(fd, buffer, length);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            if (count == 0) {
                errno = EIO;
            }
            return -1;
        }
        buffer += count;
        length -= (size_t)count;
    }
    return 0;
}

static int relay(int fd) {
    char buffer[4096];
    bool input_open = true;
    while (true) {
        fd_set readers;
        FD_ZERO(&readers);
        FD_SET(fd, &readers);
        if (input_open) {
            FD_SET(STDIN_FILENO, &readers);
        }

        if (select(fd + 1, &readers, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            return EXIT_FAILURE;
        }
        if (FD_ISSET(fd, &readers)) {
            ssize_t count = read(fd, buffer, sizeof(buffer));
            if (count == 0) {
                return EXIT_SUCCESS;
            }
            if (count < 0 && errno != EINTR) {
                perror("read server");
                return EXIT_FAILURE;
            }
            if (count > 0 && write_all(STDOUT_FILENO, buffer, (size_t)count) < 0) {
                perror("write output");
                return EXIT_FAILURE;
            }
        }
        if (input_open && FD_ISSET(STDIN_FILENO, &readers)) {
            ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (count < 0 && errno != EINTR) {
                perror("read input");
                return EXIT_FAILURE;
            }
            if (count > 0 && write_all(fd, buffer, (size_t)count) < 0) {
                perror("write server");
                return EXIT_FAILURE;
            }
            if (count == 0) {
                input_open = false;
                if (shutdown(fd, SHUT_WR) < 0 && errno != ENOTCONN) {
                    perror("shutdown");
                    return EXIT_FAILURE;
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    const char *port = argc > 2 ? argv[2] : "3425";
    char *end;
    long port_number = strtol(port, &end, 10);
    if (argc > 3 || *end != '\0' || port_number < 1 || port_number > 65535) {
        fprintf(stderr, "Usage: %s [HOST [PORT]]\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct sigaction action = {.sa_handler = SIG_IGN};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPIPE, &action, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    int fd = connect_to_server(host, port);
    if (fd < 0) {
        return EXIT_FAILURE;
    }
    if (fd >= FD_SETSIZE) {
        fprintf(stderr, "socket descriptor exceeds select limit\n");
        close(fd);
        return EXIT_FAILURE;
    }
    int status = relay(fd);
    close(fd);
    return status;
}
