#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#ifndef __linux__
#error "filemond is intended to build and run on Linux"
#endif

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char *path;
    off_t size;
    time_t mtime_sec;
    long mtime_nsec;
    ino_t inode;
    mode_t mode;
} FileInfo;

typedef struct {
    char directory[PATH_MAX];
    unsigned int interval;
} Config;

typedef struct {
    FileInfo *items;
    size_t count;
    size_t capacity;
} Snapshot;

static volatile sig_atomic_t reload_requested = 0;
static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int signal_number) {
    if (signal_number == SIGHUP) {
        reload_requested = 1;
    } else if (signal_number == SIGTERM) {
        stop_requested = 1;
    }
}

static void log_message(int priority, const char *format, ...) {
    char message[PATH_MAX + 512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    syslog(priority, "%s", message);
}

static char *trim(char *text) {
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }

    size_t length = strlen(text);
    while (length > 0 &&
           (text[length - 1] == ' ' || text[length - 1] == '\t' ||
            text[length - 1] == '\r' || text[length - 1] == '\n')) {
        text[--length] = '\0';
    }
    return text;
}

static int load_config(const char *config_path, Config *config, char *error, size_t error_size) {
    FILE *file = fopen(config_path, "r");
    if (file == NULL) {
        snprintf(error, error_size, "cannot open config '%s': %s",
                 config_path, strerror(errno));
        return -1;
    }

    Config parsed = {0};
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    unsigned int line_number = 0;
    bool have_directory = false;
    bool have_interval = false;

    while ((length = getline(&line, &capacity, file)) != -1) {
        (void)length;
        line_number++;
        char *content = trim(line);

        if (*content == '\0' || *content == '#') {
            continue;
        }

        char *equals = strchr(content, '=');
        if (equals == NULL) {
            snprintf(error, error_size, "invalid line %u: expected key=value", line_number);
            free(line);
            fclose(file);
            return -1;
        }

        *equals = '\0';
        char *key = trim(content);
        char *value = trim(equals + 1);

        if (strcmp(key, "directory") == 0) {
            if (*value == '\0' || strlen(value) >= sizeof(parsed.directory)) {
                snprintf(error, error_size, "invalid directory on line %u", line_number);
                free(line);
                fclose(file);
                return -1;
            }
            strcpy(parsed.directory, value);
            have_directory = true;
        } else if (strcmp(key, "interval") == 0) {
            char *end = NULL;
            errno = 0;
            unsigned long number = strtoul(value, &end, 10);
            if (errno != 0 || end == value || *trim(end) != '\0' ||
                number == 0 || number > 86400) {
                snprintf(error, error_size, "interval must be between 1 and 86400 seconds (line %u)",
                         line_number);
                free(line);
                fclose(file);
                return -1;
            }
            parsed.interval = (unsigned int)number;
            have_interval = true;
        } else {
            snprintf(error, error_size, "unknown parameter '%s' on line %u", key, line_number);
            free(line);
            fclose(file);
            return -1;
        }
    }

    free(line);
    fclose(file);

    if (!have_directory || !have_interval) {
        snprintf(error, error_size, "config must contain directory and interval");
        return -1;
    }

    struct stat directory_stat;
    if (stat(parsed.directory, &directory_stat) != 0 ||
        !S_ISDIR(directory_stat.st_mode)) {
        snprintf(error, error_size, "directory '%.400s' does not exist or is not a directory",
                 parsed.directory);
        return -1;
    }

    if (parsed.directory[0] != '/') {
        snprintf(error, error_size, "directory must be an absolute path");
        return -1;
    }

    *config = parsed;
    return 0;
}

static void free_snapshot(Snapshot *snapshot) {
    for (size_t i = 0; i < snapshot->count; i++) {
        free(snapshot->items[i].path);
    }
    free(snapshot->items);
    snapshot->items = NULL;
    snapshot->count = 0;
    snapshot->capacity = 0;
}

static int add_file(Snapshot *snapshot, const char *path, const struct stat *st) {
    if (snapshot->count == snapshot->capacity) {
        size_t new_capacity = snapshot->capacity == 0 ? 32 : snapshot->capacity * 2;
        FileInfo *new_items = realloc(snapshot->items, new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            return -1;
        }
        snapshot->items = new_items;
        snapshot->capacity = new_capacity;
    }

    FileInfo *item = &snapshot->items[snapshot->count];
    item->path = strdup(path);
    if (item->path == NULL) {
        return -1;
    }
    item->size = st->st_size;
    item->mtime_sec = st->st_mtim.tv_sec;
    item->mtime_nsec = st->st_mtim.tv_nsec;
    item->inode = st->st_ino;
    item->mode = st->st_mode;
    snapshot->count++;
    return 0;
}

static int scan_directory(const char *path, Snapshot *snapshot) {
    DIR *directory = opendir(path);
    if (directory == NULL) {
        return -1;
    }

    struct dirent *entry;
    int scan_error = 0;

    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0) {
                scan_error = errno;
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child_path[PATH_MAX];
        int written = snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(child_path)) {
            if (scan_error == 0) {
                scan_error = ENAMETOOLONG;
            }
            continue;
        }

        struct stat st;
        if (lstat(child_path, &st) != 0) {
            if (scan_error == 0) {
                scan_error = errno;
            }
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (add_file(snapshot, child_path, &st) != 0) {
                if (scan_error == 0) {
                    scan_error = errno != 0 ? errno : ENOMEM;
                }
                break;
            }
            if (scan_directory(child_path, snapshot) != 0) {
                if (scan_error == 0) {
                    scan_error = errno != 0 ? errno : EIO;
                }
            }
        } else {
            if (add_file(snapshot, child_path, &st) != 0) {
                if (scan_error == 0) {
                    scan_error = errno != 0 ? errno : ENOMEM;
                }
                break;
            }
        }
    }

    if (closedir(directory) != 0) {
        if (scan_error == 0) {
            scan_error = errno;
        }
    }
    if (scan_error != 0) {
        errno = scan_error;
        return -1;
    }
    return 0;
}

static const FileInfo *find_file(const Snapshot *snapshot, const char *path) {
    for (size_t i = 0; i < snapshot->count; i++) {
        if (strcmp(snapshot->items[i].path, path) == 0) {
            return &snapshot->items[i];
        }
    }
    return NULL;
}

static bool file_changed(const FileInfo *old_file, const FileInfo *new_file) {
    return old_file->size != new_file->size ||
           old_file->mtime_sec != new_file->mtime_sec ||
           old_file->mtime_nsec != new_file->mtime_nsec ||
           old_file->inode != new_file->inode ||
           old_file->mode != new_file->mode;
}

static int take_snapshot(const char *directory, Snapshot *snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
    if (scan_directory(directory, snapshot) != 0) {
        int saved_errno = errno;
        free_snapshot(snapshot);
        errno = saved_errno;
        return -1;
    }
    return 0;
}

static void compare_snapshots(const Snapshot *old_snapshot,
                              const Snapshot *new_snapshot) {
    for (size_t i = 0; i < new_snapshot->count; i++) {
        const FileInfo *current = &new_snapshot->items[i];
        const FileInfo *previous = find_file(old_snapshot, current->path);

        if (previous == NULL) {
            if (S_ISDIR(current->mode)) {
                log_message(LOG_INFO, "DIRECTORY CREATED: %s", current->path);
            } else {
                log_message(LOG_INFO, "CREATED: %s", current->path);
            }
        } else if (S_ISDIR(previous->mode) != S_ISDIR(current->mode)) {
            log_message(LOG_INFO, "TYPE CHANGED: %s", current->path);
        } else if (!S_ISDIR(current->mode) && file_changed(previous, current)) {
            log_message(LOG_INFO, "MODIFIED: %s", current->path);
        }
    }

    for (size_t i = 0; i < old_snapshot->count; i++) {
        const FileInfo *previous = &old_snapshot->items[i];
        if (find_file(new_snapshot, previous->path) == NULL) {
            if (S_ISDIR(previous->mode)) {
                log_message(LOG_INFO, "DIRECTORY DELETED: %s", previous->path);
            } else {
                log_message(LOG_INFO, "DELETED: %s", previous->path);
            }
        }
    }
}

static int daemonize_process(void) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    umask(0);
    if (chdir("/") != 0) {
        return -1;
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    return 0;
}

static int make_absolute_config_path(const char *argument, char *result, size_t size) {
    if (realpath(argument, result) == NULL) {
        return -1;
    }
    if (strlen(result) >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /absolute/path/to/daemon.conf\n", argv[0]);
        return EXIT_FAILURE;
    }

    openlog("filemond", LOG_PID | LOG_NDELAY, LOG_DAEMON);

    char config_path[PATH_MAX];
    if (make_absolute_config_path(argv[1], config_path, sizeof(config_path)) != 0) {
        syslog(LOG_ERR, "cannot resolve config path: %s", strerror(errno));
        closelog();
        return EXIT_FAILURE;
    }

    Config config;
    char error[512];
    if (load_config(config_path, &config, error, sizeof(error)) != 0) {
        syslog(LOG_ERR, "configuration error: %s", error);
        closelog();
        return EXIT_FAILURE;
    }

    if (daemonize_process() != 0) {
        syslog(LOG_ERR, "cannot daemonize process: %s", strerror(errno));
        closelog();
        return EXIT_FAILURE;
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGHUP, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0) {
        log_message(LOG_ERR, "cannot install signal handlers: %s",
                    strerror(errno));
        closelog();
        return EXIT_FAILURE;
    }

    Snapshot previous;
    if (take_snapshot(config.directory, &previous) != 0) {
        log_message(LOG_ERR, "cannot scan directory '%s': %s",
                    config.directory, strerror(errno));
        closelog();
        return EXIT_FAILURE;
    }

    log_message(LOG_INFO,
                "Daemon started. Directory: %s; interval: %u sec",
                config.directory, config.interval);

    while (!stop_requested) {
        unsigned int remaining = config.interval;
        while (remaining > 0 && !stop_requested && !reload_requested) {
            remaining = sleep(remaining);
        }

        if (stop_requested) {
            break;
        }

        if (reload_requested) {
            reload_requested = 0;
            Config new_config;
            if (load_config(config_path, &new_config, error, sizeof(error)) == 0) {
                Snapshot new_previous;
                if (take_snapshot(new_config.directory, &new_previous) == 0) {
                    free_snapshot(&previous);
                    config = new_config;
                    previous = new_previous;
                    log_message(LOG_INFO,
                                "Configuration reloaded. Directory: %s; interval: %u sec",
                                config.directory, config.interval);
                } else {
                    log_message(LOG_ERR,
                                "config reload scan failed; keeping old configuration: %s",
                                strerror(errno));
                }
            } else {
                log_message(LOG_ERR, "config reload failed: %s", error);
            }
            continue;
        }

        Snapshot current;
        if (take_snapshot(config.directory, &current) != 0) {
            log_message(LOG_ERR, "directory scan failed: %s",
                        strerror(errno));
            continue;
        }

        compare_snapshots(&previous, &current);
        free_snapshot(&previous);
        previous = current;
    }

    free_snapshot(&previous);
    log_message(LOG_INFO, "Daemon stopped by SIGTERM.");
    closelog();
    return EXIT_SUCCESS;
}
