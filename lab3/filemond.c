/*
 * filemond.c — демон слежения за изменениями файлов в каталоге.
 *
 * НАЗНАЧЕНИЕ
 *   Читает конфигурационный файл (каталог и интервал проверки), переходит
 *   в режим демона и через заданные промежутки времени рекурсивно обходит
 *   каталог, сравнивая текущее состояние файлов с предыдущим «снимком».
 *   Результат каждой проверки (созданные, удалённые, изменённые файлы или
 *   отсутствие изменений) записывается в файл журнала.
 *
 * ИСПОЛЬЗОВАНИЕ
 *   filemond [-h] [-f] <конфигурационный_файл>
 *
 *   -h  вывести справку и завершить работу;
 *   -f  (foreground) не уходить в фон — удобно для отладки.
 *
 * КОНФИГУРАЦИОННЫЙ ФАЙЛ
 *   Строки вида «ключ = значение», комментарии начинаются с '#'.
 *     directory = /путь/к/каталогу   (обязательно)
 *     interval  = 10                 (секунды, обязательно, > 0)
 *     logfile   = /tmp/filemond.log  (необязательно)
 *     pidfile   = /tmp/filemond.pid  (необязательно)
 *
 * СИГНАЛЫ
 *   SIGHUP   перечитать конфигурационный файл (и переоткрыть журнал);
 *            если конфигурация некорректна — продолжить работу со старой;
 *   SIGTERM  корректно завершить работу с записью в журнал
 *            (SIGINT обрабатывается так же — для режима -f).
 *
 * КРИТЕРИЙ МОДИФИКАЦИИ
 *   Файл считается изменённым, если изменились время модификации (mtime),
 *   размер, номер inode или тип файла. Символические ссылки не
 *   разыменовываются (обход с FTW_PHYS).
 *
 * СБОРКА
 *   make            (или: cc -Wall -Wextra -O2 -o filemond filemond.c)
 */

#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Наносекундная часть mtime называется по-разному в Linux и macOS. */
#ifdef __APPLE__
#define ST_MTIM(st) ((st)->st_mtimespec)
#else
#define ST_MTIM(st) ((st)->st_mtim)
#endif

#define DEFAULT_LOGFILE "/tmp/filemond.log"
#define DEFAULT_PIDFILE "/tmp/filemond.pid"

/* ---------- Конфигурация ---------- */

struct config {
    char directory[PATH_MAX];
    char logfile[PATH_MAX];
    char pidfile[PATH_MAX];
    unsigned interval;
};

/* ---------- Снимок состояния каталога ---------- */

struct entry {
    char *path;
    mode_t mode;
    off_t size;
    ino_t ino;
    struct timespec mtime;
};

struct snapshot {
    struct entry *items;
    size_t count;
    size_t cap;
};

/* ---------- Глобальное состояние ---------- */

static volatile sig_atomic_t got_sighup = 0;
static volatile sig_atomic_t got_sigterm = 0;

static char config_path[PATH_MAX];
static struct config cfg;
static FILE *logfp = NULL;
static int foreground = 0;

/* nftw() не позволяет передать пользовательский аргумент в callback,
 * поэтому заполняемый снимок передаётся через глобальную переменную. */
static struct snapshot *walk_target = NULL;

/* ---------- Журнал ---------- */

static void log_msg(const char *fmt, ...)
{
    char ts[32];
    time_t now = time(NULL);
    struct tm tm;
    va_list ap;
    FILE *out = logfp ? logfp : stderr;

    localtime_r(&now, &tm);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);

    fprintf(out, "%s [%ld] ", ts, (long)getpid());
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);
    fflush(out);
}

static int open_log(const char *path)
{
    FILE *fp = fopen(path, "a");

    if (fp == NULL)
        return -1;
    if (logfp != NULL)
        fclose(logfp);
    logfp = fp;
    return 0;
}

/* ---------- Разбор конфигурации ---------- */

static char *trim(char *s)
{
    char *end;

    while (isspace((unsigned char)*s))
        s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

/* Читает конфигурацию в *out. При ошибке возвращает -1 и пишет
 * пояснение в errbuf; *out при этом не изменяется. */
static int load_config(const char *path, struct config *out,
                       char *errbuf, size_t errlen)
{
    FILE *fp;
    char line[PATH_MAX + 64];
    struct config c;
    struct stat st;
    unsigned lineno = 0;
    int have_dir = 0, have_interval = 0;

    memset(&c, 0, sizeof c);
    snprintf(c.logfile, sizeof c.logfile, "%s", DEFAULT_LOGFILE);
    snprintf(c.pidfile, sizeof c.pidfile, "%s", DEFAULT_PIDFILE);

    if ((fp = fopen(path, "r")) == NULL) {
        snprintf(errbuf, errlen, "cannot open config %s: %s",
                 path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof line, fp) != NULL) {
        char *key, *val, *eq, *hash;

        lineno++;
        if ((hash = strchr(line, '#')) != NULL)
            *hash = '\0';
        key = trim(line);
        if (*key == '\0')
            continue;

        if ((eq = strchr(key, '=')) == NULL) {
            snprintf(errbuf, errlen, "%s:%u: expected 'key = value'",
                     path, lineno);
            fclose(fp);
            return -1;
        }
        *eq = '\0';
        key = trim(key);
        val = trim(eq + 1);

        if (strcmp(key, "directory") == 0) {
            if (realpath(val, c.directory) == NULL) {
                snprintf(errbuf, errlen, "%s:%u: directory '%s': %s",
                         path, lineno, val, strerror(errno));
                fclose(fp);
                return -1;
            }
            have_dir = 1;
        } else if (strcmp(key, "interval") == 0) {
            char *endp;
            unsigned long n;

            errno = 0;
            n = strtoul(val, &endp, 10);
            if (errno != 0 || *endp != '\0' || endp == val || n == 0 ||
                n > 86400 || val[0] == '-') {
                snprintf(errbuf, errlen,
                         "%s:%u: interval must be an integer in 1..86400",
                         path, lineno);
                fclose(fp);
                return -1;
            }
            c.interval = (unsigned)n;
            have_interval = 1;
        } else if (strcmp(key, "logfile") == 0) {
            snprintf(c.logfile, sizeof c.logfile, "%s", val);
        } else if (strcmp(key, "pidfile") == 0) {
            snprintf(c.pidfile, sizeof c.pidfile, "%s", val);
        } else {
            snprintf(errbuf, errlen, "%s:%u: unknown key '%s'",
                     path, lineno, key);
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);

    if (!have_dir || !have_interval) {
        snprintf(errbuf, errlen, "%s: '%s' is not set", path,
                 !have_dir ? "directory" : "interval");
        return -1;
    }
    if (stat(c.directory, &st) != 0 || !S_ISDIR(st.st_mode)) {
        snprintf(errbuf, errlen, "%s is not a directory", c.directory);
        return -1;
    }
    /* После демонизации текущий каталог — "/", поэтому пути к журналу
     * и pid-файлу должны быть абсолютными. */
    if (c.logfile[0] != '/' || c.pidfile[0] != '/') {
        snprintf(errbuf, errlen, "logfile and pidfile must be absolute paths");
        return -1;
    }

    *out = c;
    return 0;
}

/* ---------- Снимки ---------- */

static void snapshot_free(struct snapshot *s)
{
    size_t i;

    for (i = 0; i < s->count; i++)
        free(s->items[i].path);
    free(s->items);
    s->items = NULL;
    s->count = s->cap = 0;
}

static int walk_cb(const char *path, const struct stat *st,
                   int type, struct FTW *ftw)
{
    struct snapshot *s = walk_target;
    struct entry *e;

    if (ftw->level == 0)        /* сам корневой каталог не учитываем */
        return 0;
    if (type == FTW_DNR || type == FTW_NS) {
        log_msg("WARNING: cannot access %s", path);
        if (type == FTW_NS)
            return 0;
    }

    if (s->count == s->cap) {
        size_t ncap = s->cap ? s->cap * 2 : 256;
        struct entry *n = realloc(s->items, ncap * sizeof *n);

        if (n == NULL)
            return -1;
        s->items = n;
        s->cap = ncap;
    }
    e = &s->items[s->count];
    if ((e->path = strdup(path)) == NULL)
        return -1;
    e->mode = st->st_mode;
    e->size = st->st_size;
    e->ino = st->st_ino;
    e->mtime = ST_MTIM(st);
    s->count++;
    return 0;
}

static int entry_cmp(const void *a, const void *b)
{
    return strcmp(((const struct entry *)a)->path,
                  ((const struct entry *)b)->path);
}

static int snapshot_take(const char *dir, struct snapshot *s)
{
    walk_target = s;
    if (nftw(dir, walk_cb, 32, FTW_PHYS) != 0) {
        log_msg("ERROR: scanning %s failed: %s", dir, strerror(errno));
        snapshot_free(s);
        return -1;
    }
    qsort(s->items, s->count, sizeof *s->items, entry_cmp);
    return 0;
}

static const char *kind(mode_t m)
{
    if (S_ISDIR(m))  return "directory";
    if (S_ISLNK(m))  return "symlink";
    if (S_ISREG(m))  return "file";
    return "special file";
}

static int entry_changed(const struct entry *a, const struct entry *b)
{
    return (a->mode & S_IFMT) != (b->mode & S_IFMT) ||
           a->size != b->size || a->ino != b->ino ||
           a->mtime.tv_sec != b->mtime.tv_sec ||
           a->mtime.tv_nsec != b->mtime.tv_nsec;
}

/* Сравнивает два отсортированных снимка слиянием и пишет различия
 * в журнал. Каталоги в «изменённые» не попадают: их mtime меняется
 * при каждом создании/удалении файла, что уже отражено отдельно. */
static void snapshot_diff(const struct snapshot *old, const struct snapshot *cur)
{
    size_t i = 0, j = 0;
    unsigned added = 0, removed = 0, modified = 0;

    while (i < old->count || j < cur->count) {
        int c;

        if (i == old->count)
            c = 1;
        else if (j == cur->count)
            c = -1;
        else
            c = strcmp(old->items[i].path, cur->items[j].path);

        if (c < 0) {
            log_msg("  DELETED  %s: %s", kind(old->items[i].mode),
                    old->items[i].path);
            removed++;
            i++;
        } else if (c > 0) {
            log_msg("  CREATED  %s: %s", kind(cur->items[j].mode),
                    cur->items[j].path);
            added++;
            j++;
        } else {
            if (entry_changed(&old->items[i], &cur->items[j]) &&
                !(S_ISDIR(old->items[i].mode) && S_ISDIR(cur->items[j].mode))) {
                log_msg("  MODIFIED %s: %s (size %lld -> %lld)",
                        kind(cur->items[j].mode), cur->items[j].path,
                        (long long)old->items[i].size,
                        (long long)cur->items[j].size);
                modified++;
            }
            i++;
            j++;
        }
    }

    if (added + removed + modified == 0)
        log_msg("Check: no changes (%zu entries)", cur->count);
    else
        log_msg("Check: %u created, %u deleted, %u modified (%zu entries)",
                added, removed, modified, cur->count);
}

/* ---------- PID-файл ---------- */

/* Возвращает pid уже работающего экземпляра из pid-файла или 0. */
static long running_pid(const char *path)
{
    FILE *fp;
    long pid = 0;

    if ((fp = fopen(path, "r")) == NULL)
        return 0;
    if (fscanf(fp, "%ld", &pid) != 1 || pid <= 0 || pid == (long)getpid() ||
        kill((pid_t)pid, 0) != 0)
        pid = 0;
    fclose(fp);
    return pid;
}

static int write_pidfile(const char *path)
{
    FILE *fp;

    if ((fp = fopen(path, "w")) == NULL)
        return -1;
    fprintf(fp, "%ld\n", (long)getpid());
    fclose(fp);
    return 0;
}

/* ---------- Сигналы ---------- */

static void on_signal(int sig)
{
    if (sig == SIGHUP)
        got_sighup = 1;
    else
        got_sigterm = sig;
}

static void setup_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;            /* без SA_RESTART: sleep() прерывается */
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

/* ---------- Демонизация ---------- */

static void daemonize(void)
{
    pid_t pid;
    int fd;

    /* 1-й fork: родитель возвращает управление оболочке,
     * потомок гарантированно не лидер группы — можно вызвать setsid(). */
    if ((pid = fork()) < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0)
        exit(EXIT_SUCCESS);

    /* Новая сессия: отрываемся от управляющего терминала. */
    if (setsid() < 0) {
        perror("setsid");
        exit(EXIT_FAILURE);
    }

    /* 2-й fork: процесс перестаёт быть лидером сессии и уже не сможет
     * случайно получить управляющий терминал. */
    signal(SIGHUP, SIG_IGN);
    if ((pid = fork()) < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0)
        exit(EXIT_SUCCESS);

    umask(022);
    if (chdir("/") != 0) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }

    /* Стандартные потоки перенаправляем в /dev/null. */
    if ((fd = open("/dev/null", O_RDWR)) >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO)
            close(fd);
    }
}

/* ---------- Перечитывание конфигурации ---------- */

static void reload_config(struct snapshot *snap)
{
    struct config newcfg;
    char err[PATH_MAX + 128];

    log_msg("SIGHUP received: reloading %s", config_path);
    if (load_config(config_path, &newcfg, err, sizeof err) != 0) {
        log_msg("ERROR: %s; keeping previous configuration", err);
        return;
    }

    /* Журнал переоткрываем всегда — это позволяет его ротировать. */
    if (open_log(newcfg.logfile) != 0) {
        log_msg("ERROR: cannot open log %s: %s; keeping %s",
                newcfg.logfile, strerror(errno), cfg.logfile);
        snprintf(newcfg.logfile, sizeof newcfg.logfile, "%s", cfg.logfile);
    }

    if (strcmp(newcfg.pidfile, cfg.pidfile) != 0) {
        unlink(cfg.pidfile);
        if (write_pidfile(newcfg.pidfile) != 0)
            log_msg("WARNING: cannot write pidfile %s: %s",
                    newcfg.pidfile, strerror(errno));
    }

    if (strcmp(newcfg.directory, cfg.directory) != 0) {
        log_msg("Directory changed: %s -> %s", cfg.directory, newcfg.directory);
        snapshot_free(snap);
        if (snapshot_take(newcfg.directory, snap) == 0)
            log_msg("Initial scan: %zu entries", snap->count);
    }

    cfg = newcfg;
    log_msg("Configuration: directory=%s interval=%us logfile=%s",
            cfg.directory, cfg.interval, cfg.logfile);
}

/* ---------- main ---------- */

static void usage(FILE *out)
{
    fprintf(out, "Usage: filemond [-h] [-f] <config_file>\n"
                 "  -h  show this help\n"
                 "  -f  stay in foreground (do not daemonize)\n");
}

int main(int argc, char *argv[])
{
    struct snapshot snap = {0};
    char err[PATH_MAX + 128];
    long oldpid;
    int opt;

    while ((opt = getopt(argc, argv, "hf")) != -1) {
        switch (opt) {
        case 'h':
            usage(stdout);
            return EXIT_SUCCESS;
        case 'f':
            foreground = 1;
            break;
        default:
            usage(stderr);
            return EXIT_FAILURE;
        }
    }
    if (optind != argc - 1) {
        usage(stderr);
        return EXIT_FAILURE;
    }

    /* Абсолютный путь нужен, чтобы перечитывать конфиг после chdir("/"). */
    if (realpath(argv[optind], config_path) == NULL) {
        fprintf(stderr, "filemond: %s: %s\n", argv[optind], strerror(errno));
        return EXIT_FAILURE;
    }
    if (load_config(config_path, &cfg, err, sizeof err) != 0) {
        fprintf(stderr, "filemond: %s\n", err);
        return EXIT_FAILURE;
    }
    if (open_log(cfg.logfile) != 0) {
        fprintf(stderr, "filemond: cannot open log %s: %s\n",
                cfg.logfile, strerror(errno));
        return EXIT_FAILURE;
    }

    /* Проверяем до ухода в фон, чтобы ошибку увидел пользователь. */
    if ((oldpid = running_pid(cfg.pidfile)) != 0) {
        fprintf(stderr, "filemond: already running (pid %ld)\n", oldpid);
        return EXIT_FAILURE;
    }

    if (!foreground)
        daemonize();
    setup_signals();
    if (write_pidfile(cfg.pidfile) != 0)
        log_msg("WARNING: cannot write pidfile %s: %s",
                cfg.pidfile, strerror(errno));

    log_msg("Daemon started: config=%s directory=%s interval=%us",
            config_path, cfg.directory, cfg.interval);
    if (snapshot_take(cfg.directory, &snap) == 0)
        log_msg("Initial scan: %zu entries", snap.count);

    while (!got_sigterm) {
        unsigned left = cfg.interval;

        /* sleep() прерывается сигналом и возвращает остаток времени. */
        while (left > 0 && !got_sigterm && !got_sighup)
            left = sleep(left);

        if (got_sigterm)
            break;
        if (got_sighup) {
            got_sighup = 0;
            reload_config(&snap);
            continue;           /* отсчёт интервала начинается заново */
        }

        {
            struct snapshot cur = {0};

            if (snapshot_take(cfg.directory, &cur) == 0) {
                snapshot_diff(&snap, &cur);
                snapshot_free(&snap);
                snap = cur;
            }
        }
    }

    log_msg("%s received: daemon stopped",
            got_sigterm == SIGINT ? "SIGINT" : "SIGTERM");
    snapshot_free(&snap);
    unlink(cfg.pidfile);
    if (logfp != NULL)
        fclose(logfp);
    return EXIT_SUCCESS;
}
