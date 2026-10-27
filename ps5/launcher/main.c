/*
 * pPlay PS5 launcher: PS4 package which sends ps5/pplay_loader.elf to elfldr and
 * waits until the loader replaces it with pPlay. Errors are system notifications.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>

#define LOADER_PATH     "/app0/ps5/pplay_loader.elf"
#define ELFLDR_PORT     9021
#define REPLY_TIMEOUT   600 /* seconds, the first launch copies all files */

int sceSystemServiceLoadExec(const char *path, const char *args[]);

static void notify(const char *fmt, ...) {
    OrbisNotificationRequest req;
    va_list args;

    memset(&req, 0, sizeof(req));
    req.type = NotificationRequest;
    req.targetId = -1;
    va_start(args, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, args);
    va_end(args);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

// Static arena instead of musl malloc: its first call maps 2.6 GB of system
// memory, which freezes the PS5 in backward compatibility mode.
#define ARENA_SIZE (512 * 1024)
static uint8_t g_arena[ARENA_SIZE] __attribute__((aligned(64)));
static size_t g_arena_used = 0;
int __malloc_replaced = 1;

void *__memalign(size_t align, size_t len) {
    if (align < 16) {
        align = 16;
    }
    size_t start = (g_arena_used + sizeof(size_t) + align - 1) & ~(align - 1);
    if (start + len > ARENA_SIZE) {
        errno = ENOMEM;
        return NULL;
    }
    *(size_t *) (g_arena + start - sizeof(size_t)) = len;
    g_arena_used = start + len;
    return g_arena + start;
}

void *memalign(size_t align, size_t len) {
    return __memalign(align, len);
}

void *malloc(size_t len) {
    return __memalign(16, len);
}

void *calloc(size_t n, size_t len) {
    if (len && n > (size_t) -1 / len) {
        errno = ENOMEM;
        return NULL;
    }
    return malloc(n * len);
}

void *realloc(void *ptr, size_t len) {
    void *p = malloc(len);
    if (p && ptr) {
        size_t old = *(size_t *) ((uint8_t *) ptr - sizeof(size_t));
        memcpy(p, ptr, old < len ? old : len);
    }
    return p;
}

void free(void *ptr) {
    (void) ptr;
}

int malloc_init(void) {
    return 0;
}

int malloc_finalize(void) {
    return 0;
}

static uint8_t g_loader[4 * 1024 * 1024];

static uint8_t *read_file(const char *path, size_t *size) {
    size_t done = 0;
    int fd;

    if ((fd = open(path, O_RDONLY)) < 0) {
        return NULL;
    }
    for (;;) {
        if (done == sizeof(g_loader)) {
            close(fd);
            return NULL;
        }
        ssize_t n = read(fd, g_loader + done, sizeof(g_loader) - done);
        if (n < 0) {
            close(fd);
            return NULL;
        }
        if (n == 0) {
            break;
        }
        done += (size_t) n;
    }
    close(fd);
    if (done == 0) {
        return NULL;
    }
    *size = done;
    return g_loader;
}

static int send_all(int sock, const uint8_t *buf, size_t size) {
    while (size > 0) {
        ssize_t n = send(sock, buf, size, 0);
        if (n <= 0) {
            return -1;
        }
        buf += n;
        size -= (size_t) n;
    }
    return 0;
}

// Loader output: only returns on errors (on success this application is closed).
static int wait_loader(int sock) {
    char line[512];
    size_t len = 0;
    char c;

    for (;;) {
        ssize_t n = recv(sock, &c, 1, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                notify("pPlay: no answer from the loader (timeout)");
            } else {
                notify("pPlay: the loader stopped unexpectedly");
            }
            return -1;
        }
        if (c == '\r') {
            continue;
        }
        if (c != '\n' && len < sizeof(line) - 1) {
            line[len++] = c;
            continue;
        }
        line[len] = 0;
        len = 0;

        if (!strncmp(line, "PPLAY_ERROR", 11)) {
            return -1;
        }
        if (!strncmp(line, "[elfldr", 7)) {
            notify("pPlay: %s", line);
            return -1;
        }
    }
}

static int launch(void) {
    struct sockaddr_in addr;
    struct timeval tv;
    uint8_t *elf;
    size_t size = 0;
    int sock, ret;

    if (!(elf = read_file(LOADER_PATH, &size))) {
        notify("pPlay: can't read %s", LOADER_PATH);
        return -1;
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        notify("pPlay: socket error (%d)", errno);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ELFLDR_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(sock, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
        notify("pPlay: ELF loader is not running (port %d).\nLoad elfldr / etaHEN first.", ELFLDR_PORT);
        close(sock);
        return -1;
    }

    ret = send_all(sock, elf, size);
    if (ret != 0) {
        notify("pPlay: can't send the loader to elfldr (%d)", errno);
        close(sock);
        return -1;
    }

    tv.tv_sec = REPLY_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ret = wait_loader(sock);
    close(sock);

    return ret;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);

    launch();

    sceSystemServiceLoadExec("exit", NULL);
    for (;;) {
        sceKernelUsleep(1000 * 1000);
    }

    return 0;
}
