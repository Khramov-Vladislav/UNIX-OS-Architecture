/*
 * Многопроцессный HTTP-сервер статических файлов.
 * Главный процесс открывает каталог сайта и принимает TCP-соединения.
 * Для каждого соединения он создаёт потомка, который отвечает на один запрос.
 */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

enum { REQUEST_LIMIT = 8192, IO_TIMEOUT = 5, BACKLOG = 64 };

static void reap_children(int signal_number) {
    (void)signal_number;
    /* Один SIGCHLD может прийти после завершения нескольких потомков. */
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
    errno = saved_errno;
}

static int install_signals(void) {
    /* Игнорируем SIGPIPE, чтобы обработать ошибку send() при обрыве связи. */
    struct sigaction action = {.sa_handler = SIG_IGN};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPIPE, &action, NULL) < 0) {
        return -1;
    }
    action.sa_handler = reap_children;
    action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    return sigaction(SIGCHLD, &action, NULL);
}

/* Двойной fork() отделяет демон от терминала и возвращает оболочке его PID. */
static void detach(void) {
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (child > 0) {
        /* Исходный процесс ждёт результата настройки от промежуточного. */
        int status;
        pid_t result;
        do {
            result = waitpid(child, &status, 0);
        } while (result < 0 && errno == EINTR);
        exit(result == child && WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE);
    }

    if (setsid() < 0 || chdir("/") < 0 || install_signals() < 0) {
        perror("daemon setup");
        _exit(EXIT_FAILURE);
    }

    int null_fd = open("/dev/null", O_RDWR);
    /* Сохраняем исходный stdout, чтобы сообщить PID после перенаправления. */
    int output = dup(STDOUT_FILENO);
    if (null_fd < 0 || output < 0 || dup2(null_fd, STDIN_FILENO) < 0 ||
        dup2(null_fd, STDOUT_FILENO) < 0 || dup2(null_fd, STDERR_FILENO) < 0) {
        syslog(LOG_ERR, "redirect standard streams: %s", strerror(errno));
        _exit(EXIT_FAILURE);
    }
    if (null_fd > STDERR_FILENO) {
        close(null_fd);
    }

    /* После второго fork() демон больше не является лидером сессии. */
    child = fork();
    if (child < 0) {
        syslog(LOG_ERR, "detach: fork: %s", strerror(errno));
        _exit(EXIT_FAILURE);
    }
    if (child > 0) {
        dprintf(output, "%jd\n", (intmax_t)child);
        _exit(EXIT_SUCCESS);
    }
    close(output);
}

/* send() может принять часть буфера. Продолжаем с первого неотправленного байта. */
static int send_all(int client, const void *data, size_t length) {
    const char *bytes = data;
    while (length > 0) {
        ssize_t count = send(client, bytes, length, 0);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            syslog(LOG_WARNING, "send: %s", count < 0 ? strerror(errno) : "no progress");
            return -1;
        }
        bytes += count;
        length -= (size_t)count;
    }
    return 0;
}

/* length задаёт длину тела в байтах. Заголовки заканчиваются пустой строкой. */
static int send_headers(int client, int status, const char *reason, const char *type,
                        uintmax_t length) {
    char date[64] = "";
    time_t now = time(NULL);
    struct tm utc;
    if (now != (time_t)-1 && gmtime_r(&now, &utc) != NULL) {
        strftime(date, sizeof(date), "Date: %a, %d %b %Y %H:%M:%S GMT\r\n", &utc);
    }

    char header[512];
    int size =
        snprintf(header, sizeof(header),
                 "HTTP/1.1 %d %s\r\n%sContent-Type: %s\r\n"
                 "Content-Length: %ju\r\nConnection: close\r\n%s\r\n",
                 status, reason, date, type, length, status == 405 ? "Allow: GET, HEAD\r\n" : "");
    if (size < 0 || (size_t)size >= sizeof(header)) {
        syslog(LOG_ERR, "could not format response header");
        return -1;
    }
    return send_all(client, header, (size_t)size);
}

static void send_error(int client, int status, const char *reason, bool head_only) {
    /* Одна ошибка даёт запись в журнале и, если сокет доступен, HTTP-ответ. */
    syslog(LOG_WARNING, "HTTP %d %s", status, reason);
    if (send_headers(client, status, reason, "text/plain; charset=utf-8", strlen(reason) + 1) ==
            0 &&
        !head_only) {
        if (send_all(client, reason, strlen(reason)) == 0) {
            send_all(client, "\n", 1);
        }
    }
}

/* Возвращает 0 при успехе, HTTP-код ошибки или -1, если ответ не отправляем. */
static int read_request(int client, char request[REQUEST_LIMIT + 1]) {
    size_t used = 0;
    while (used < REQUEST_LIMIT) {
        ssize_t count = recv(client, request + used, REQUEST_LIMIT - used, 0);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 408;
            }
            syslog(LOG_WARNING, "recv: %s", strerror(errno));
            return -1;
        }
        if (count == 0) {
            return used == 0 ? -1 : 400;
        }
        /*
         * Проверяем конец заголовков по мере чтения. Последовательность
         * "\r\n\r\n" может оказаться на стыке двух вызовов recv().
         */
        size_t end = used + (size_t)count;
        for (; used < end; ++used) {
            if (request[used] == '\0') {
                return 400;
            }
            if (used >= 3 && memcmp(request + used - 3, "\r\n\r\n", 4) == 0) {
                request[used + 1] = '\0';
                return 0;
            }
        }
    }
    return 431;
}

/* Проверяем поля построчно. Для HTTP/1.1 нужен один непустой Host. */
static bool valid_headers(const char *line, bool host_required) {
    bool has_host = false;
    while (strncmp(line, "\r\n", 2) != 0) {
        const char *end = strstr(line, "\r\n");
        const char *name_end = line;
        while (*name_end && (isalnum((unsigned char)*name_end) ||
                             strchr("!#$%&'*+-.^`_|~", *name_end) != NULL)) {
            ++name_end;
        }
        if (end == NULL || name_end == line || *name_end != ':' || name_end >= end) {
            return false;
        }
        const char *value = name_end + 1;
        for (const char *p = value; p < end; ++p) {
            unsigned char byte = (unsigned char)*p;
            if ((byte < ' ' && byte != '\t') || byte == 127) {
                return false;
            }
        }
        if (name_end - line == 4 && strncasecmp(line, "Host", 4) == 0) {
            value += strspn(value, " \t");
            if (has_host || value == end) {
                return false;
            }
            has_host = true;
        }
        line = end + 2;
    }
    return has_host || !host_required;
}

static int hex_value(unsigned char byte) {
    if (byte >= '0' && byte <= '9') {
        return byte - '0';
    }
    if (byte >= 'A' && byte <= 'F') {
        return byte - 'A' + 10;
    }
    if (byte >= 'a' && byte <= 'f') {
        return byte - 'a' + 10;
    }
    return -1;
}

/* Декодируем %XX в том же буфере и отбрасываем параметры после буквального '?'. */
static bool decode_path(char *target) {
    unsigned char *input = (unsigned char *)target;
    unsigned char *output = input;
    if (*input != '/') {
        return false;
    }
    while (*input && *input != '?') {
        unsigned char byte = *input++;
        if (byte == '%') {
            if (!input[0] || !input[1]) {
                return false;
            }
            int high = hex_value(input[0]);
            int low = hex_value(input[1]);
            if (high < 0 || low < 0) {
                return false;
            }
            byte = (unsigned char)(high * 16 + low);
            input += 2;
        } else if (byte == '#') {
            return false;
        }
        if (byte < ' ' || byte == 127 || byte == '\\') {
            return false;
        }
        *output++ = byte;
    }
    *output = '\0';
    return true;
}

/*
 * Путь разрешается относительно открытого корня сайта. Открываем по одному
 * компоненту, поскольку O_NOFOLLOW действует на последний компонент openat().
 */
static int open_file(int root, char *path) {
    if (!*path) {
        return openat(root, "index.html", O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    }

    int directory = root;
    int fd = -1;
    for (;;) {
        char *slash = strchr(path, '/');
        if (slash != NULL) {
            *slash = '\0';
        }
        if (!*path || strcmp(path, ".") == 0 || strcmp(path, "..") == 0) {
            errno = EACCES;
            break;
        }
        /* O_NONBLOCK позволяет открыть именованный канал без ожидания и проверить тип. */
        int flags = slash == NULL ? O_NONBLOCK : O_DIRECTORY;
        fd = openat(directory, path, O_RDONLY | O_NOFOLLOW | flags);
        if (fd < 0 || slash == NULL) {
            break;
        }
        if (directory != root) {
            close(directory);
        }
        directory = fd;
        fd = -1;
        path = slash + 1;
    }
    if (directory != root) {
        int saved_errno = errno;
        close(directory);
        errno = saved_errno;
    }
    return fd;
}

static const char *content_type(const char *path) {
    static const struct {
        const char *extension;
        const char *type;
    } types[] = {
        {".html", "text/html; charset=utf-8"},
        {".txt", "text/plain; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
    };
    const char *extension = strrchr(path, '.');
    if (extension != NULL) {
        for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
            if (strcasecmp(extension, types[i].extension) == 0) {
                return types[i].type;
            }
        }
    }
    return "application/octet-stream";
}

/* Читаем файл порциями в пределах размера, объявленного в Content-Length. */
static void send_file(int client, int file, uintmax_t remaining) {
    char buffer[16384];
    while (remaining > 0) {
        size_t wanted = remaining < sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
        ssize_t count = read(file, buffer, wanted);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            syslog(LOG_ERR, "read static file: %s",
                   count < 0 ? strerror(errno) : "file shortened during response");
            return;
        }
        if (send_all(client, buffer, (size_t)count) < 0) {
            return;
        }
        remaining -= (uintmax_t)count;
    }
}

static void serve_client(int client, int root) {
    /* Тайм-аут относится к отдельному вызову чтения или записи. */
    struct timeval timeout = {.tv_sec = IO_TIMEOUT};
    if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        syslog(LOG_ERR, "set client timeout: %s", strerror(errno));
        return;
    }

    char request[REQUEST_LIMIT + 1];
    switch (read_request(client, request)) {
    case 0:
        break;
    case -1:
        return;
    case 400:
        send_error(client, 400, "Bad Request", false);
        return;
    case 408:
        send_error(client, 408, "Request Timeout", false);
        return;
    case 431:
        send_error(client, 431, "Request Header Fields Too Large", false);
        return;
    }

    /* Разделяем первую строку на метод, путь и версию внутри буфера запроса. */
    char *line_end = strstr(request, "\r\n");
    *line_end = '\0';
    char *target = strchr(request, ' ');
    if (target == NULL || target == request) {
        send_error(client, 400, "Bad Request", false);
        return;
    }
    *target++ = '\0';
    bool head_only = strcmp(request, "HEAD") == 0;
    char *version = strchr(target, ' ');
    if (version == NULL) {
        send_error(client, 400, "Bad Request", head_only);
        return;
    }
    *version++ = '\0';
    bool http_11 = strcmp(version, "HTTP/1.1") == 0;
    if ((!http_11 && strcmp(version, "HTTP/1.0") != 0) || !valid_headers(line_end + 2, http_11) ||
        !decode_path(target)) {
        send_error(client, 400, "Bad Request", head_only);
        return;
    }
    if (!head_only && strcmp(request, "GET") != 0) {
        send_error(client, 405, "Method Not Allowed", false);
        return;
    }

    /* Тип определяем заранее, потому что open_file() разбивает путь на части. */
    const char *type = content_type(target[1] ? target + 1 : "index.html");
    int file = open_file(root, target + 1);
    if (file < 0) {
        switch (errno) {
        case ENOENT:
        case ENOTDIR:
        case ENAMETOOLONG:
        case ELOOP:
            send_error(client, 404, "Not Found", head_only);
            break;
        case EACCES:
        case EPERM:
            send_error(client, 403, "Forbidden", head_only);
            break;
        default:
            syslog(LOG_ERR, "open static file: %s", strerror(errno));
            send_error(client, 500, "Internal Server Error", head_only);
        }
        return;
    }

    /* Проверяем тип и размер именно открытого файла перед ответом 200 OK. */
    struct stat info;
    if (fstat(file, &info) < 0) {
        syslog(LOG_ERR, "fstat: %s", strerror(errno));
        send_error(client, 500, "Internal Server Error", head_only);
    } else if (!S_ISREG(info.st_mode)) {
        send_error(client, 404, "Not Found", head_only);
    } else if (send_headers(client, 200, "OK", type, (uintmax_t)info.st_size) == 0 && !head_only) {
        send_file(client, file, (uintmax_t)info.st_size);
    }
    close(file);
}

int main(int argc, char **argv) {
    bool foreground = false;
    const char *address = "127.0.0.1";
    long port = 8080;
    int option;
    while ((option = getopt(argc, argv, "fa:p:")) != -1) {
        switch (option) {
        case 'f':
            foreground = true;
            break;
        case 'a':
            address = optarg;
            break;
        case 'p': {
            char *end;
            port = strtol(optarg, &end, 10);
            if (end == optarg || *end || port < 1 || port > 65535) {
                fprintf(stderr, "Invalid port: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;
        }
        default:
            return EXIT_FAILURE;
        }
    }
    if (argc - optind > 1) {
        fprintf(stderr, "Usage: %s [-f] [-a IPv4] [-p PORT] [DOCUMENT_ROOT]\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct sockaddr_in endpoint = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
    if (inet_pton(AF_INET, address, &endpoint.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", address);
        return EXIT_FAILURE;
    }

    /* Открытый корень сайта остаётся доступен после chdir("/") и fork(). */
    const char *root_path = optind < argc ? argv[optind] : "www";
    int root = open(root_path, O_RDONLY | O_DIRECTORY);
    if (root < 0) {
        perror(root_path);
        return EXIT_FAILURE;
    }

    /* Слушающий сокет остаётся у главного процесса на весь срок работы. */
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    if (listener < 0 || setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
        bind(listener, (struct sockaddr *)&endpoint, sizeof(endpoint)) < 0 ||
        listen(listener, BACKLOG) < 0) {
        perror("set up listening socket");
        return EXIT_FAILURE;
    }

    openlog("webd", LOG_PID | (foreground ? LOG_PERROR : 0), LOG_DAEMON);
    if (!foreground) {
        detach();
    } else if (install_signals() < 0) {
        syslog(LOG_ERR, "sigaction: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    syslog(LOG_INFO, "listening on %s:%ld", address, port);

    for (;;) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) {
            if (errno != EINTR && errno != ECONNABORTED) {
                syslog(LOG_ERR, "accept: %s", strerror(errno));
                sleep(1);
            }
            continue;
        }
        pid_t worker = fork();
        if (worker == 0) {
            /* Потомку нужен только клиентский сокет для обмена данными. */
            close(listener);
            serve_client(client, root);
            close(client);
            /* _exit() завершает процесс без сброса унаследованных буферов stdio. */
            _exit(EXIT_SUCCESS);
        }
        if (worker < 0) {
            syslog(LOG_ERR, "fork client: %s", strerror(errno));
        }
        /*
         * Родитель закрывает копию client даже при ошибке fork(), чтобы
         * соединение не оставалось открытым после выхода потомка.
         */
        close(client);
    }
}
