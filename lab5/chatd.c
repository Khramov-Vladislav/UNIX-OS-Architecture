#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

enum { MAX_CLIENTS = 64, NICK_LIMIT = 20, LINE_LIMIT = 1024, OUTPUT_LIMIT = 32768 };

struct client {
    int fd;
    bool finishing;
    bool dropped;
    char nick[NICK_LIMIT + 1];
    char input[LINE_LIMIT + 2];
    size_t input_size;
    char output[OUTPUT_LIMIT];
    size_t output_start;
    size_t output_end;
};

static struct client clients[MAX_CLIENTS];
static volatile sig_atomic_t stopping;

static void stop_server(int signal_number) {
    (void)signal_number;
    stopping = 1;
}

static int install_signals(void) {
    struct sigaction action = {.sa_handler = SIG_IGN};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPIPE, &action, NULL) < 0) {
        return -1;
    }
    action.sa_handler = stop_server;
    if (sigaction(SIGTERM, &action, NULL) < 0) {
        return -1;
    }
    return sigaction(SIGINT, &action, NULL);
}

/* The launcher waits until the session leader has completed daemon setup. */
static void detach(void) {
    pid_t child = fork();
    if (child < 0) {
        syslog(LOG_ERR, "fork: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (child > 0) {
        int status;
        pid_t result;
        do {
            result = waitpid(child, &status, 0);
        } while (result < 0 && errno == EINTR);
        exit(result == child && WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE);
    }

    umask(027);
    if (setsid() < 0 || chdir("/") < 0) {
        syslog(LOG_ERR, "daemon setup: %s", strerror(errno));
        _exit(EXIT_FAILURE);
    }

    int output = fcntl(STDOUT_FILENO, F_DUPFD, STDERR_FILENO + 1);
    if (output < 0) {
        syslog(LOG_ERR, "save standard output: %s", strerror(errno));
        _exit(EXIT_FAILURE);
    }
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0 || dup2(null_fd, STDIN_FILENO) < 0 ||
        dup2(null_fd, STDOUT_FILENO) < 0 || dup2(null_fd, STDERR_FILENO) < 0) {
        syslog(LOG_ERR, "redirect standard streams: %s", strerror(errno));
        _exit(EXIT_FAILURE);
    }
    if (null_fd > STDERR_FILENO) {
        close(null_fd);
    }

    child = fork();
    if (child < 0) {
        syslog(LOG_ERR, "detach: fork: %s", strerror(errno));
        _exit(EXIT_FAILURE);
    }
    if (child > 0) {
        if (dprintf(output, "%jd\n", (intmax_t)child) < 0) {
            syslog(LOG_ERR, "write daemon PID: %s", strerror(errno));
            kill(child, SIGTERM);
            _exit(EXIT_FAILURE);
        }
        _exit(EXIT_SUCCESS);
    }
    close(output);
    openlog("chatd", LOG_PID, LOG_DAEMON);
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static bool would_block(void) {
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

static bool queue_text(struct client *client, const char *text) {
    if (client->dropped) {
        return false;
    }

    size_t length = strlen(text);
    size_t pending = client->output_end - client->output_start;
    if (length > sizeof(client->output) - pending) {
        syslog(LOG_WARNING, "client fd %d: output queue full", client->fd);
        client->dropped = true;
        return false;
    }
    if (length > sizeof(client->output) - client->output_end) {
        memmove(client->output, client->output + client->output_start, pending);
        client->output_start = 0;
        client->output_end = pending;
    }
    memcpy(client->output + client->output_end, text, length);
    client->output_end += length;
    return true;
}

static bool online(const struct client *client) {
    return client->fd >= 0 && !client->finishing && !client->dropped && client->nick[0];
}

static struct client *find_nick(const char *nick) {
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (online(&clients[i]) && strcmp(nick, clients[i].nick) == 0) {
            return &clients[i];
        }
    }
    return NULL;
}

static bool valid_nick(const char *nick) {
    const char allowed[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
    size_t length = strlen(nick);
    return length > 0 && length <= NICK_LIMIT && strspn(nick, allowed) == length;
}

static void register_nick(struct client *client, const char *nick) {
    if (client->nick[0]) {
        queue_text(client, "ERR nickname already set\n");
    } else if (!valid_nick(nick)) {
        queue_text(client, "ERR use 1-20 characters from A-Z, a-z, 0-9, _ or -\n");
    } else if (find_nick(nick)) {
        queue_text(client, "ERR nickname in use\n");
    } else {
        strcpy(client->nick, nick);
        char reply[NICK_LIMIT + 16];
        snprintf(reply, sizeof(reply), "OK welcome %s\n", nick);
        queue_text(client, reply);
    }
}

static void list_clients(struct client *client) {
    char reply[sizeof("WHO\n") + MAX_CLIENTS * (NICK_LIMIT + 1)] = "WHO";
    size_t used = strlen(reply);
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (online(&clients[i])) {
            const char *nick = clients[i].nick;
            reply[used++] = ' ';
            size_t length = strlen(nick);
            memcpy(reply + used, nick, length);
            used += length;
        }
    }
    reply[used++] = '\n';
    reply[used] = '\0';
    queue_text(client, reply);
}

static void send_private(struct client *sender, char *args) {
    char *text = strchr(args, ' ');
    if (text == NULL || text == args || text[strspn(text, " \t")] == '\0') {
        queue_text(sender, "ERR usage: /msg NAME TEXT\n");
        return;
    }
    *text++ = '\0';
    struct client *recipient = find_nick(args);
    if (recipient == NULL) {
        queue_text(sender, "ERR no such participant\n");
        return;
    }

    char message[LINE_LIMIT + NICK_LIMIT + 16];
    snprintf(message, sizeof(message), "FROM %s: %s\n", sender->nick, text);
    if (queue_text(recipient, message)) {
        queue_text(sender, "OK private message queued\n");
    } else {
        queue_text(sender, "ERR participant unavailable\n");
    }
}

static void broadcast(const struct client *sender, const char *text) {
    char message[LINE_LIMIT + NICK_LIMIT + 16];
    snprintf(message, sizeof(message), "ALL %s: %s\n", sender->nick, text);
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (online(&clients[i])) {
            queue_text(&clients[i], message);
        }
    }
}

static void handle_line(struct client *client) {
    char *line = client->input;
    for (size_t i = 0; i < client->input_size; i++) {
        unsigned char byte = (unsigned char)line[i];
        if ((byte < 32 && byte != '\t') || byte == 127) {
            queue_text(client, "ERR control characters in input\n");
            return;
        }
    }
    if (line[strspn(line, " \t")] == '\0') {
        return;
    }

    if (strcmp(line, "/quit") == 0) {
        queue_text(client, "BYE\n");
        client->finishing = true;
    } else if (strncmp(line, "/nick ", 6) == 0) {
        register_nick(client, line + 6);
    } else if (!client->nick[0]) {
        queue_text(client, "ERR register with /nick NAME\n");
    } else if (strcmp(line, "/who") == 0) {
        list_clients(client);
    } else if (strncmp(line, "/msg ", 5) == 0) {
        send_private(client, line + 5);
    } else if (line[0] == '/') {
        queue_text(client, "ERR unknown command (use /who, /msg NAME TEXT or /quit)\n");
    } else {
        broadcast(client, line);
    }
}

static void long_line(struct client *client) {
    syslog(LOG_WARNING, "client fd %d: input line too long", client->fd);
    queue_text(client, "ERR line exceeds 1024 bytes\n");
    client->finishing = true;
}

static void read_client(struct client *client) {
    char buffer[4096];
    ssize_t count = recv(client->fd, buffer, sizeof(buffer), 0);
    if (count < 0) {
        if (errno != EINTR && !would_block()) {
            syslog(LOG_WARNING, "client fd %d: recv: %s", client->fd, strerror(errno));
            client->dropped = true;
        }
        return;
    }
    if (count == 0) {
        client->finishing = true;
        return;
    }

    /* Each TCP read may contain several lines or only part of a line. */
    for (ssize_t i = 0; i < count && !client->finishing && !client->dropped; i++) {
        char byte = buffer[i];
        if (byte == '\n') {
            if (client->input_size > 0 && client->input[client->input_size - 1] == '\r') {
                client->input_size--;
            }
            if (client->input_size > LINE_LIMIT) {
                long_line(client);
                return;
            }
            client->input[client->input_size] = '\0';
            handle_line(client);
            client->input_size = 0;
        } else if (client->input_size == LINE_LIMIT + 1) {
            long_line(client);
        } else {
            client->input[client->input_size++] = byte;
        }
    }
}

static void write_client(struct client *client) {
    ssize_t count = send(client->fd, client->output + client->output_start,
                         client->output_end - client->output_start, 0);
    if (count > 0) {
        client->output_start += (size_t)count;
        if (client->output_start == client->output_end) {
            client->output_start = 0;
            client->output_end = 0;
        }
    } else if (count == 0 || (errno != EINTR && !would_block())) {
        syslog(LOG_WARNING, "client fd %d: send: %s", client->fd,
               count == 0 ? "no progress" : strerror(errno));
        client->dropped = true;
    }
}

static void accept_client(int listener) {
    int fd = accept(listener, NULL, NULL);
    if (fd < 0) {
        if (errno != EINTR && !would_block()) {
            syslog(LOG_WARNING, "accept: %s", strerror(errno));
        }
        return;
    }
    if (fd >= FD_SETSIZE || set_nonblocking(fd) < 0) {
        syslog(LOG_WARNING, "reject client fd %d: %s", fd,
               fd >= FD_SETSIZE ? "descriptor exceeds select limit" : strerror(errno));
        close(fd);
        return;
    }

    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        struct client *client = &clients[i];
        if (client->fd < 0) {
            client->fd = fd;
            client->finishing = false;
            client->dropped = false;
            client->nick[0] = '\0';
            client->input_size = 0;
            client->output_start = 0;
            client->output_end = 0;
            queue_text(client, "CHAT register with /nick NAME\n");
            return;
        }
    }
    syslog(LOG_WARNING, "reject client: limit of %d connections reached", MAX_CLIENTS);
    close(fd);
}

static int serve(int listener) {
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
    }

    int status = EXIT_SUCCESS;
    while (!stopping) {
        fd_set readers, writers;
        FD_ZERO(&readers);
        FD_ZERO(&writers);
        FD_SET(listener, &readers);
        int max_fd = listener;

        for (size_t i = 0; i < MAX_CLIENTS; i++) {
            const struct client *client = &clients[i];
            if (client->fd < 0) {
                continue;
            }
            if (!client->finishing) {
                FD_SET(client->fd, &readers);
            }
            if (client->output_start < client->output_end) {
                FD_SET(client->fd, &writers);
            }
            if (client->fd > max_fd) {
                max_fd = client->fd;
            }
        }

        /* Bound the wait if SIGTERM arrives just before select(). */
        struct timeval timeout = {.tv_sec = 1};
        int ready = select(max_fd + 1, &readers, &writers, NULL, &timeout);
        if (ready < 0 && errno != EINTR) {
            syslog(LOG_ERR, "select: %s", strerror(errno));
            status = EXIT_FAILURE;
            break;
        }
        if (ready <= 0 || stopping) {
            continue;
        }

        for (size_t i = 0; i < MAX_CLIENTS; i++) {
            struct client *client = &clients[i];
            if (client->fd < 0 || client->dropped) {
                continue;
            }
            if (FD_ISSET(client->fd, &writers)) {
                write_client(client);
            }
            if (!client->dropped && FD_ISSET(client->fd, &readers)) {
                read_client(client);
            }
        }

        /* Broadcast can mark any client as dropped during the pass above. */
        for (size_t i = 0; i < MAX_CLIENTS; i++) {
            struct client *client = &clients[i];
            bool drained = client->output_start == client->output_end;
            if (client->fd >= 0 && (client->dropped || (client->finishing && drained))) {
                close(client->fd);
                client->fd = -1;
            }
        }
        if (FD_ISSET(listener, &readers)) {
            accept_client(listener);
        }
    }

    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) {
            close(clients[i].fd);
        }
    }
    return status;
}

int main(int argc, char **argv) {
    const char *address = "127.0.0.1";
    const char *port_text = "3425";
    bool foreground = false;
    int option;
    while ((option = getopt(argc, argv, "fa:p:")) != -1) {
        switch (option) {
            case 'f': foreground = true; break;
            case 'a': address = optarg; break;
            case 'p': port_text = optarg; break;
            default: return EXIT_FAILURE;
        }
    }

    char *end;
    long port = strtol(port_text, &end, 10);
    struct sockaddr_in local = {.sin_family = AF_INET};
    if (optind != argc || *end != '\0' || port < 1 || port > 65535 ||
        inet_pton(AF_INET, address, &local.sin_addr) != 1) {
        fprintf(stderr, "Usage: %s [-f] [-a IPv4] [-p PORT]\n", argv[0]);
        return EXIT_FAILURE;
    }
    local.sin_port = htons((uint16_t)port);
    openlog("chatd", LOG_PID | LOG_PERROR, LOG_DAEMON);

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (!foreground && listener >= 0 && listener <= STDERR_FILENO) {
        int original = listener;
        listener = fcntl(original, F_DUPFD, STDERR_FILENO + 1);
        if (listener < 0) {
            syslog(LOG_ERR, "move listener away from standard streams: %s", strerror(errno));
            return EXIT_FAILURE;
        }
        close(original);
    }
    int reuse_address = 1;
    if (listener < 0 || listener >= FD_SETSIZE || set_nonblocking(listener) < 0 ||
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address)) < 0 ||
        bind(listener, (struct sockaddr *)&local, sizeof(local)) < 0 ||
        listen(listener, MAX_CLIENTS) < 0) {
        syslog(LOG_ERR, "listen on %s:%ld: %s", address, port,
               listener >= FD_SETSIZE ? "descriptor exceeds select limit" : strerror(errno));
        return EXIT_FAILURE;
    }
    if (install_signals() < 0) {
        syslog(LOG_ERR, "sigaction: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    if (!foreground) {
        detach();
    }

    syslog(LOG_INFO, "listening on %s:%ld", address, port);
    int status = serve(listener);
    close(listener);
    syslog(LOG_INFO, "stopped");
    closelog();
    return status;
}
