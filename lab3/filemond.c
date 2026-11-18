#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
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
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#define STAT_MTIME(st) ((st)->st_mtimespec)
#else
#define STAT_MTIME(st) ((st)->st_mtim)
#endif

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
    char log_file[PATH_MAX];
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

static void log_message(const char *log_path, const char *format, ...) {
    FILE *log = fopen(log_path, "a");
    if (log == NULL) {
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    char timestamp[32];

    if (localtime_r(&now, &tm_now) != NULL) {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_now);
    } else {
        snprintf(timestamp, sizeof(timestamp), "unknown-time");
    }

    fprintf(log, "[%s] ", timestamp);

    va_list args;
    va_start(args, format);
    vfprintf(log, format, args);
    va_end(args);

    fputc('\n', log);
    fflush(log);
    fclose(log);
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
    bool have_log = false;

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
        } else if (strcmp(key, "log_file") == 0) {
            if (*value == '\0' || strlen(value) >= sizeof(parsed.log_file)) {
                snprintf(error, error_size, "invalid log_file on line %u", line_number);
                free(line);
                fclose(file);
                return -1;
            }
            strcpy(parsed.log_file, value);
            have_log = true;
        } else {
            snprintf(error, error_size, "unknown parameter '%s' on line %u", key, line_number);
            free(line);
            fclose(file);
            return -1;
        }
    }

    free(line);
    fclose(file);

    if (!have_directory || !have_interval || !have_log) {
        snprintf(error, error_size,
                 "config must contain directory, interval and log_file");
        return -1;
    }

    struct stat directory_stat;
    if (stat(parsed.directory, &directory_stat) != 0 ||
        !S_ISDIR(directory_stat.st_mode)) {
        snprintf(error, error_size, "directory '%s' does not exist or is not a directory",
                 parsed.directory);
        return -1;
    }

    if (parsed.directory[0] != '/' || parsed.log_file[0] != '/') {
        snprintf(error, error_size, "directory and log_file must be absolute paths");
        return -1;
    }

    FILE *log = fopen(parsed.log_file, "a");
    if (log == NULL) {
        snprintf(error, error_size, "cannot open log file '%s': %s",
                 parsed.log_file, strerror(errno));
        return -1;
    }
    fclose(log);

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
    item->mtime_sec = STAT_MTIME(st).tv_sec;
    item->mtime_nsec = STAT_MTIME(st).tv_nsec;
    item->inode = st->st_ino;
    item->mode = st->st_mode;
    snapshot->count++;
    return 0;
}

/* Рекурсивно обходит каталоги; символические ссылки не разыменовываются. */
static int scan_directory(const char *path, Snapshot *snapshot) {
    DIR *directory = opendir(path);
    if (directory == NULL) {
        return -1;
    }

    struct dirent *entry;
    int result = 0;

    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child_path[PATH_MAX];
        int written = snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(child_path)) {
            result = -1;
            continue;
        }

        struct stat st;
        if (lstat(child_path, &st) != 0) {
            result = -1;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (scan_directory(child_path, snapshot) != 0) {
                result = -1;
            }
        } else if (S_ISREG(st.st_mode)) {
            if (add_file(snapshot, child_path, &st) != 0) {
                result = -1;
                break;
            }
        }
        /* Символические ссылки и специальные файлы пропускаются. */
    }

    if (closedir(directory) != 0) {
        result = -1;
    }
    return result;
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
        free_snapshot(snapshot);
        return -1;
    }
    return 0;
}

static bool compare_snapshots(const Snapshot *old_snapshot,
                              const Snapshot *new_snapshot,
                              const char *log_path) {
    bool changed = false;
    for (size_t i = 0; i < new_snapshot->count; i++) {
        const FileInfo *current = &new_snapshot->items[i];
        const FileInfo *previous = find_file(old_snapshot, current->path);

        if (previous == NULL) {
            log_message(log_path, "CREATED: %s", current->path);
            changed = true;
        } else if (file_changed(previous, current)) {
            log_message(log_path, "MODIFIED: %s", current->path);
            changed = true;
        }
    }

    for (size_t i = 0; i < old_snapshot->count; i++) {
        const FileInfo *previous = &old_snapshot->items[i];
        if (find_file(new_snapshot, previous->path) == NULL) {
            log_message(log_path, "DELETED: %s", previous->path);
            changed = true;
        }
    }
    if (!changed) {
        log_message(log_path, "No file changes detected.");
    }
    return changed;
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

    char config_path[PATH_MAX];
    if (make_absolute_config_path(argv[1], config_path, sizeof(config_path)) != 0) {
        perror("Cannot resolve config path");
        return EXIT_FAILURE;
    }

    Config config;
    char error[512];
    if (load_config(config_path, &config, error, sizeof(error)) != 0) {
        fprintf(stderr, "Configuration error: %s\n", error);
        return EXIT_FAILURE;
    }

    if (daemonize_process() != 0) {
        /* После daemonize stderr уже закрыт, поэтому это сообщение может быть недоступно. */
        return EXIT_FAILURE;
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGHUP, &action, NULL) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0) {
        log_message(config.log_file, "ERROR: cannot install signal handlers: %s",
                    strerror(errno));
        return EXIT_FAILURE;
    }

    Snapshot previous;
    if (take_snapshot(config.directory, &previous) != 0) {
        log_message(config.log_file, "ERROR: cannot scan directory '%s': %s",
                    config.directory, strerror(errno));
        return EXIT_FAILURE;
    }

    log_message(config.log_file, "Daemon started. Directory: %s; interval: %u sec",
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
                    log_message(config.log_file,
                                "Configuration reloaded. Directory: %s; interval: %u sec",
                                config.directory, config.interval);
                } else {
                    log_message(config.log_file,
                                "ERROR: config reload scan failed; keeping old configuration: %s",
                                strerror(errno));
                }
            } else {
                log_message(config.log_file, "ERROR: config reload failed: %s", error);
            }
            continue;
        }

        Snapshot current;
        if (take_snapshot(config.directory, &current) != 0) {
            log_message(config.log_file, "ERROR: directory scan failed: %s", strerror(errno));
            continue;
        }

        compare_snapshots(&previous, &current, config.log_file);
        free_snapshot(&previous);
        previous = current;
    }

    free_snapshot(&previous);
    log_message(config.log_file, "Daemon stopped by SIGTERM.");
    return EXIT_SUCCESS;
}
