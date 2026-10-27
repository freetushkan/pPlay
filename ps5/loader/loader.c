/*
 * pPlay PS5 loader, sent to elfldr by the launcher package: syncs the package
 * files to /data/homebrew/pplay, the shared libraries to /user/homebrew/lib, then
 * starts pplay.elf as the BigApp (closing the launcher). Errors are reported on
 * stdout ("PPLAY_ERROR ...") and with notifications.
 * argv[1]: optional source directory (default: the launcher package).
 */

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include <zstd.h>
#include "hbldr/hbldr.h"

#ifndef PPLAY_TITLE_ID
#define PPLAY_TITLE_ID "PPLA00005"
#endif

#define LOADER_NAME     "pplay_loader.elf"
#define PKG_SUBDIR      "ps5/pplay"
#define MARKER          PKG_SUBDIR "/pplay.elf"
#define ZST_EXT         ".zst"
#define DATA_DIR        "/data/homebrew/pplay"
#define LIB_DIR         "/user/homebrew/lib"

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void notify(const char *fmt, ...) {
    notify_request_t req;
    va_list args;

    memset(&req, 0, sizeof(req));
    va_start(args, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, args);
    va_end(args);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static int fail(const char *fmt, ...) {
    char msg[512];
    va_list args;

    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    printf("PPLAY_ERROR %s\n", msg);
    fflush(stdout);
    notify("pPlay: %s", msg);
    return EXIT_FAILURE;
}

static int is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int has_marker(const char *app0) {
    char path[PATH_MAX];
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s", app0, MARKER);
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        return 1;
    }
    snprintf(path, sizeof(path), "%s/%s" ZST_EXT, app0, MARKER);
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int is_zst(const char *name) {
    size_t len = strlen(name), ext = strlen(ZST_EXT);
    return len > ext && strcmp(name + len - ext, ZST_EXT) == 0;
}

// unpacked size from the zstd frame header (-1: unknown)
static long long zst_size(const char *path) {
    unsigned char head[18]; // ZSTD_FRAMEHEADERSIZE_MAX
    unsigned long long size;
    ssize_t n;
    int fd;

    if ((fd = open(path, O_RDONLY)) < 0) {
        return -1;
    }
    n = read(fd, head, sizeof(head));
    close(fd);
    if (n <= 0) {
        return -1;
    }
    size = ZSTD_getFrameContentSize(head, (size_t) n);
    if (size == ZSTD_CONTENTSIZE_UNKNOWN || size == ZSTD_CONTENTSIZE_ERROR) {
        return -1;
    }
    return (long long) size;
}

// Package content, mounted while the launcher runs.
static int find_app0(char *out, size_t size) {
    const char *roots[] = {"/mnt/sandbox/pfsmnt", "/mnt/sandbox"};
    char path[PATH_MAX];
    DIR *dir;
    struct dirent *e;

    snprintf(path, sizeof(path), "/mnt/sandbox/pfsmnt/%s-app0", PPLAY_TITLE_ID);
    if (has_marker(path)) {
        snprintf(out, size, "%s", path);
        return 0;
    }
    snprintf(path, sizeof(path), "/mnt/sandbox/%s_000/app0", PPLAY_TITLE_ID);
    if (has_marker(path)) {
        snprintf(out, size, "%s", path);
        return 0;
    }

    // other title id: look for the marker
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        if (!(dir = opendir(roots[i]))) {
            continue;
        }
        while ((e = readdir(dir))) {
            if (e->d_name[0] == '.') {
                continue;
            }
            if (i == 0) {
                size_t len = strlen(e->d_name);
                if (len < 5 || strcmp(e->d_name + len - 5, "-app0") != 0) {
                    continue;
                }
                snprintf(path, sizeof(path), "%s/%s", roots[i], e->d_name);
            } else {
                snprintf(path, sizeof(path), "%s/%s/app0", roots[i], e->d_name);
            }
            if (has_marker(path)) {
                snprintf(out, size, "%s", path);
                closedir(dir);
                return 0;
            }
        }
        closedir(dir);
    }

    return -1;
}

static int mkdirs(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
        return -1;
    }
    return is_dir(tmp) ? 0 : -1;
}

// size: of the destination, -1 to only compare the mtime
static int same_file(const struct stat *src, long long size, const char *dst) {
    struct stat st;
    if (stat(dst, &st) != 0) {
        return 0;
    }
    return (size < 0 || st.st_size == size) && st.st_mtime == src->st_mtime;
}

static void set_mtime(const char *path, const struct stat *st) {
    struct timeval times[2];
    times[0].tv_sec = st->st_atime;
    times[0].tv_usec = 0;
    times[1].tv_sec = st->st_mtime;
    times[1].tv_usec = 0;
    utimes(path, times);
}

static int replace_file(const char *tmp, const char *dst) {
    if (rename(tmp, dst) != 0) {
        unlink(dst);
        if (rename(tmp, dst) != 0) {
            unlink(tmp);
            return -1;
        }
    }
    return 0;
}

static int write_all(int fd, const void *data, size_t size) {
    const char *p = data;
    while (size > 0) {
        ssize_t w = write(fd, p, size);
        if (w <= 0) {
            return -1;
        }
        p += w;
        size -= (size_t) w;
    }
    return 0;
}

static int copy_file(const char *src, const char *dst, const struct stat *st) {
    char tmp[PATH_MAX];
    static char buf[1024 * 1024];
    ssize_t n;
    int in, out;

    snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
    if ((in = open(src, O_RDONLY)) < 0) {
        return -1;
    }
    if ((out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0777)) < 0) {
        close(in);
        return -1;
    }

    while ((n = read(in, buf, sizeof(buf))) > 0) {
        if (write_all(out, buf, (size_t) n) != 0) {
            close(in);
            close(out);
            unlink(tmp);
            return -1;
        }
    }
    close(in);
    if (n < 0 || close(out) != 0) {
        unlink(tmp);
        return -1;
    }

    // keep the package mtime, so unchanged files are skipped next time
    set_mtime(tmp, st);
    return replace_file(tmp, dst);
}

static int unpack_file(const char *src, const char *dst, const struct stat *st) {
    char tmp[PATH_MAX];
    static char in_buf[1024 * 1024];
    static char out_buf[1024 * 1024];
    ZSTD_DCtx *dctx;
    size_t ret = 1;
    ssize_t n = 0;
    int in, out, err = 0;

    snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
    if (!(dctx = ZSTD_createDCtx())) {
        errno = ENOMEM;
        return -1;
    }
    if ((in = open(src, O_RDONLY)) < 0) {
        ZSTD_freeDCtx(dctx);
        return -1;
    }
    if ((out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0777)) < 0) {
        close(in);
        ZSTD_freeDCtx(dctx);
        return -1;
    }

    while (!err && (n = read(in, in_buf, sizeof(in_buf))) > 0) {
        ZSTD_inBuffer input = {in_buf, (size_t) n, 0};
        while (input.pos < input.size) {
            ZSTD_outBuffer output = {out_buf, sizeof(out_buf), 0};
            ret = ZSTD_decompressStream(dctx, &output, &input);
            if (ZSTD_isError(ret) || write_all(out, out_buf, output.pos) != 0) {
                err = 1;
                break;
            }
        }
    }
    close(in);
    ZSTD_freeDCtx(dctx);
    // ret != 0: truncated frame
    if (err || n < 0 || ret != 0) {
        close(out);
        unlink(tmp);
        errno = EIO;
        return -1;
    }
    if (close(out) != 0) {
        unlink(tmp);
        return -1;
    }

    set_mtime(tmp, st);
    return replace_file(tmp, dst);
}

static int sync_tree(const char *src, const char *dst, int *copied) {
    char s[PATH_MAX], d[PATH_MAX];
    struct dirent *e;
    struct stat st;
    DIR *dir;
    int ret = 0;

    if (mkdirs(dst) != 0) {
        return fail("can't create %s (%s)", dst, strerror(errno));
    }
    if (!(dir = opendir(src))) {
        return fail("can't open %s (%s)", src, strerror(errno));
    }

    while (ret == 0 && (e = readdir(dir))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        snprintf(s, sizeof(s), "%s/%s", src, e->d_name);
        snprintf(d, sizeof(d), "%s/%s", dst, e->d_name);
        if (stat(s, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            ret = sync_tree(s, d, copied);
            continue;
        }
        if (!S_ISREG(st.st_mode)) {
            continue;
        }
        int packed = is_zst(e->d_name);
        if (packed) {
            d[strlen(d) - strlen(ZST_EXT)] = 0;
        }
        if (same_file(&st, packed ? zst_size(s) : st.st_size, d)) {
            continue;
        }
        if (*copied == 0) {
            notify("pPlay: updating files, please wait...");
        }
        if ((packed ? unpack_file(s, d, &st) : copy_file(s, d, &st)) != 0) {
            ret = fail("can't %s %s (%s)", packed ? "unpack" : "copy", d, strerror(errno));
        } else {
            (*copied)++;
        }
    }

    closedir(dir);
    return ret;
}

static int is_shared_lib(const char *name) {
    return strstr(name, ".so") != NULL;
}

// shared libraries -> /user/homebrew/lib
static int install_libs(const char *src_dir, int *copied) {
    char s[PATH_MAX], d[PATH_MAX];
    struct dirent *e;
    struct stat st;
    DIR *dir;
    int ret = 0;

    if (!(dir = opendir(src_dir))) {
        return 0;
    }
    while (ret == 0 && (e = readdir(dir))) {
        if (!is_shared_lib(e->d_name)) {
            continue;
        }
        snprintf(s, sizeof(s), "%s/%s", src_dir, e->d_name);
        snprintf(d, sizeof(d), "%s/%s", LIB_DIR, e->d_name);
        if (stat(s, &st) != 0 || !S_ISREG(st.st_mode) || same_file(&st, st.st_size, d)) {
            continue;
        }
        if (mkdirs(LIB_DIR) != 0) {
            ret = fail("can't create %s (%s)", LIB_DIR, strerror(errno));
        } else if (copy_file(s, d, &st) != 0) {
            ret = fail("can't copy %s (%s)", d, strerror(errno));
        } else {
            (*copied)++;
        }
    }
    closedir(dir);
    return ret;
}

static int launch_bigapp(const char *dir, const char *elf) {
    char env[PATH_MAX + 32];
    char *argv[] = {NULL};
    char *envp[] = {env, NULL};

    snprintf(env, sizeof(env), "LD_LIBRARY_PATH=%s", dir);

    printf("PPLAY_INFO starting pPlay\n");
    fflush(stdout);

    if (hbldr_launch(dir, elf, -1, argv, envp) < 0) {
        return fail("can't start pPlay: %s", hbldr_error());
    }
    return 0;
}

int main(int argc, char *argv[]) {
    char app0[PATH_MAX];
    char src[PATH_MAX];
    char elf[PATH_MAX];
    int copied = 0;

    signal(SIGPIPE, SIG_IGN);

    // single instance
    syscall(SYS_thr_set_name, -1, LOADER_NAME);
    pid_t other;
    for (int i = 0; i < 5 && (other = hbldr_find_pid(LOADER_NAME)) > 0; i++) {
        kill(other, SIGKILL);
        sleep(1);
    }
    // hbldr waits for the traced process with waitpid
    signal(SIGCHLD, SIG_IGN);

    if (argc > 1 && is_dir(argv[1])) {
        snprintf(src, sizeof(src), "%s", argv[1]);
    } else {
        if (find_app0(app0, sizeof(app0)) != 0) {
            return fail("package content not found (is the launcher running?)");
        }
        snprintf(src, sizeof(src), "%s/%s", app0, PKG_SUBDIR);
    }

    if (sync_tree(src, DATA_DIR, &copied) != 0) {
        return EXIT_FAILURE;
    }
    if (install_libs(DATA_DIR, &copied) != 0) {
        return EXIT_FAILURE;
    }
    if (copied > 0) {
        printf("PPLAY_INFO updated %d file(s) from %s\n", copied, src);
        fflush(stdout);
    }

    snprintf(elf, sizeof(elf), "%s/pplay.elf", DATA_DIR);

    if (launch_bigapp(DATA_DIR, elf) != 0) {
        return EXIT_FAILURE;
    }

    printf("PPLAY_OK\n");
    fflush(stdout);

    return EXIT_SUCCESS;
}
