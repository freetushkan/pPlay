#ifdef __PS4__

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <signal.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <orbis/libkernel.h>
#include <orbis/SystemService.h>

#include "ps4_compat.h"

// Clock ids: musl headers have the Linux ids, libkernel expects the FreeBSD ones
// (Linux CLOCK_MONOTONIC is FreeBSD CLOCK_VIRTUAL, the process cpu time).
// Enabled with -Wl,--wrap (CMakeLists.txt).

#define FBSD_CLOCK_REALTIME 0
#define FBSD_CLOCK_MONOTONIC 4
#define FBSD_CLOCK_THREAD_CPUTIME_ID 14
#define FBSD_CLOCK_PROCESS_CPUTIME_ID 15

#define LINUX_CLOCK_REALTIME 0
#define LINUX_CLOCK_MONOTONIC 1
#define LINUX_CLOCK_PROCESS_CPUTIME_ID 2
#define LINUX_CLOCK_THREAD_CPUTIME_ID 3
#define LINUX_CLOCK_MONOTONIC_RAW 4
#define LINUX_CLOCK_REALTIME_COARSE 5
#define LINUX_CLOCK_MONOTONIC_COARSE 6
#define LINUX_CLOCK_BOOTTIME 7

static clockid_t ps4_clock_id(clockid_t id) {
    switch ((int) id) {
        case LINUX_CLOCK_REALTIME:
        case LINUX_CLOCK_REALTIME_COARSE:
            return FBSD_CLOCK_REALTIME;
        case LINUX_CLOCK_MONOTONIC:
        case LINUX_CLOCK_MONOTONIC_RAW:
        case LINUX_CLOCK_MONOTONIC_COARSE:
        case LINUX_CLOCK_BOOTTIME:
            return FBSD_CLOCK_MONOTONIC;
        case LINUX_CLOCK_PROCESS_CPUTIME_ID:
            return FBSD_CLOCK_PROCESS_CPUTIME_ID;
        case LINUX_CLOCK_THREAD_CPUTIME_ID:
            return FBSD_CLOCK_THREAD_CPUTIME_ID;
        default:
            return id;
    }
}

int __real_clock_gettime(clockid_t id, struct timespec *ts);
int __real_clock_getres(clockid_t id, struct timespec *ts);
int __real_pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t id);

int __wrap_clock_gettime(clockid_t id, struct timespec *ts);
int __wrap_clock_getres(clockid_t id, struct timespec *ts);
int __wrap_pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t id);

int __wrap_clock_gettime(clockid_t id, struct timespec *ts) {
    return __real_clock_gettime(ps4_clock_id(id), ts);
}

int __wrap_clock_getres(clockid_t id, struct timespec *ts) {
    return __real_clock_getres(ps4_clock_id(id), ts);
}

int __wrap_pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t id) {
    return __real_pthread_condattr_setclock(attr, ps4_clock_id(id));
}

// Socket flags: Linux values in the headers, FreeBSD ones in libkernel
// (MSG_NOSIGNAL, MSG_DONTWAIT, MSG_WAITALL differ).

#define LINUX_MSG_OOB 0x0001
#define LINUX_MSG_PEEK 0x0002
#define LINUX_MSG_DONTROUTE 0x0004
#define LINUX_MSG_CTRUNC 0x0008
#define LINUX_MSG_TRUNC 0x0020
#define LINUX_MSG_DONTWAIT 0x0040
#define LINUX_MSG_EOR 0x0080
#define LINUX_MSG_WAITALL 0x0100
#define LINUX_MSG_NOSIGNAL 0x4000

#define FBSD_MSG_OOB 0x00001
#define FBSD_MSG_PEEK 0x00002
#define FBSD_MSG_DONTROUTE 0x00004
#define FBSD_MSG_EOR 0x00008
#define FBSD_MSG_TRUNC 0x00010
#define FBSD_MSG_CTRUNC 0x00020
#define FBSD_MSG_WAITALL 0x00040
#define FBSD_MSG_DONTWAIT 0x00080
#define FBSD_MSG_NOSIGNAL 0x20000

static int ps4_msg_flags(int flags) {
    int out = 0;
    if (flags & LINUX_MSG_OOB) {
        out |= FBSD_MSG_OOB;
    }
    if (flags & LINUX_MSG_PEEK) {
        out |= FBSD_MSG_PEEK;
    }
    if (flags & LINUX_MSG_DONTROUTE) {
        out |= FBSD_MSG_DONTROUTE;
    }
    if (flags & LINUX_MSG_CTRUNC) {
        out |= FBSD_MSG_CTRUNC;
    }
    if (flags & LINUX_MSG_TRUNC) {
        out |= FBSD_MSG_TRUNC;
    }
    if (flags & LINUX_MSG_DONTWAIT) {
        out |= FBSD_MSG_DONTWAIT;
    }
    if (flags & LINUX_MSG_EOR) {
        out |= FBSD_MSG_EOR;
    }
    if (flags & LINUX_MSG_WAITALL) {
        out |= FBSD_MSG_WAITALL;
    }
    out |= FBSD_MSG_NOSIGNAL;
    return out;
}

ssize_t __real_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *addr, socklen_t alen);
ssize_t __real_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *addr, socklen_t *alen);
ssize_t __real_sendmsg(int fd, const struct msghdr *msg, int flags);
ssize_t __real_recvmsg(int fd, struct msghdr *msg, int flags);

ssize_t __wrap_send(int fd, const void *buf, size_t len, int flags);
ssize_t __wrap_recv(int fd, void *buf, size_t len, int flags);
ssize_t __wrap_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *addr, socklen_t alen);
ssize_t __wrap_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *addr, socklen_t *alen);
ssize_t __wrap_sendmsg(int fd, const struct msghdr *msg, int flags);
ssize_t __wrap_recvmsg(int fd, struct msghdr *msg, int flags);

ssize_t __wrap_send(int fd, const void *buf, size_t len, int flags) {
    return __real_sendto(fd, buf, len, ps4_msg_flags(flags), NULL, 0);
}

ssize_t __wrap_recv(int fd, void *buf, size_t len, int flags) {
    return __real_recvfrom(fd, buf, len, ps4_msg_flags(flags) & ~FBSD_MSG_NOSIGNAL, NULL, NULL);
}

ssize_t __wrap_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *addr, socklen_t alen) {
    return __real_sendto(fd, buf, len, ps4_msg_flags(flags), addr, alen);
}

ssize_t __wrap_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *addr, socklen_t *alen) {
    return __real_recvfrom(fd, buf, len, ps4_msg_flags(flags) & ~FBSD_MSG_NOSIGNAL, addr, alen);
}

ssize_t __wrap_sendmsg(int fd, const struct msghdr *msg, int flags) {
    return __real_sendmsg(fd, msg, ps4_msg_flags(flags));
}

ssize_t __wrap_recvmsg(int fd, struct msghdr *msg, int flags) {
    return __real_recvmsg(fd, msg, ps4_msg_flags(flags) & ~FBSD_MSG_NOSIGNAL);
}


/*
 * OpenOrbis libc does not export __cxa_thread_atexit_impl, which libc++abi
 * uses to register C++ thread_local destructors. Keep a per-thread LIFO list
 * and execute it from a pthread-key destructor when the thread exits.
 */
typedef struct ps4_thread_dtor {
    void (*function)(void *);
    void *object;
    struct ps4_thread_dtor *next;
} ps4_thread_dtor;

static pthread_key_t g_ps4_thread_dtor_key;
static atomic_flag g_ps4_thread_dtor_key_lock = ATOMIC_FLAG_INIT;
static atomic_int g_ps4_thread_dtor_key_state = ATOMIC_VAR_INIT(0);

static void ps4_run_thread_dtors(void *value) {
    ps4_thread_dtor *entry = (ps4_thread_dtor *) value;
    while (entry != NULL) {
        ps4_thread_dtor *next = entry->next;
        entry->function(entry->object);
        free(entry);
        entry = next;
    }
}

static int ps4_ensure_thread_dtor_key(void) {
    int state = atomic_load_explicit(&g_ps4_thread_dtor_key_state, memory_order_acquire);
    if (state == 1) {
        return 0;
    }
    if (state < 0) {
        return -1;
    }

    while (atomic_flag_test_and_set_explicit(&g_ps4_thread_dtor_key_lock, memory_order_acquire)) {
        /* Key creation happens once; this short spin avoids OpenOrbis pthread_once ABI differences. */
    }

    state = atomic_load_explicit(&g_ps4_thread_dtor_key_state, memory_order_relaxed);
    if (state == 0) {
        state = pthread_key_create(&g_ps4_thread_dtor_key, ps4_run_thread_dtors) == 0 ? 1 : -1;
        atomic_store_explicit(&g_ps4_thread_dtor_key_state, state, memory_order_release);
    }

    atomic_flag_clear_explicit(&g_ps4_thread_dtor_key_lock, memory_order_release);
    return state == 1 ? 0 : -1;
}

/* C++ ABI entry point used by libc++abi for thread_local destructors. */
int __cxa_thread_atexit_impl(void (*function)(void *), void *object, void *dso_symbol) {
    (void) dso_symbol;
    if (function == NULL || ps4_ensure_thread_dtor_key() != 0) {
        return -1;
    }

    ps4_thread_dtor *entry = (ps4_thread_dtor *) malloc(sizeof(*entry));
    if (entry == NULL) {
        return -1;
    }

    entry->function = function;
    entry->object = object;
    entry->next = (ps4_thread_dtor *) pthread_getspecific(g_ps4_thread_dtor_key);
    if (pthread_setspecific(g_ps4_thread_dtor_key, entry) != 0) {
        free(entry);
        return -1;
    }
    return 0;
}

// Timezone: musl has no tz database, TZ is built from the system settings.

static int g_utc_offset = INT_MIN;

void ps4_update_timezone(void) {
    int32_t tz = 0, dst = 0;
    char buf[32];

    if (sceSystemServiceParamGetInt(ORBIS_SYSTEM_SERVICE_PARAM_ID_TIME_ZONE, &tz) < 0) {
        return;
    }
    if (sceSystemServiceParamGetInt(ORBIS_SYSTEM_SERVICE_PARAM_ID_SUMMERTIME, &dst) < 0) {
        dst = 0;
    }

    if (tz < -14 * 60 || tz > 14 * 60) {
        return;
    }

    int offset = tz + (dst ? 60 : 0);
    if (offset == g_utc_offset) {
        return;
    }

    int abs_offset = offset < 0 ? -offset : offset;
    int hours = abs_offset / 60;
    int minutes = abs_offset % 60;
    // POSIX TZ sign is inverted: "<+03>-3" is UTC+3
    char name_sign = offset < 0 ? '-' : '+';
    char posix_sign = offset < 0 ? '+' : '-';
    if (minutes) {
        snprintf(buf, sizeof(buf), "<%c%02d%02d>%c%d:%02d", name_sign, hours, minutes, posix_sign, hours, minutes);
    } else {
        snprintf(buf, sizeof(buf), "<%c%02d>%c%d", name_sign, hours, posix_sign, hours);
    }

    setenv("TZ", buf, 1);
    tzset();
    g_utc_offset = offset;
}

void ps4_compat_init(void) {
    signal(SIGPIPE, SIG_IGN);
    ps4_update_timezone();
}

void ps4_clock_report(char *buf, size_t size) {
    struct timespec rt = {0}, mono = {0};
    clock_gettime(CLOCK_REALTIME, &rt);
    clock_gettime(CLOCK_MONOTONIC, &mono);
    uint64_t proc = sceKernelGetProcessTime();
    snprintf(buf, size, "realtime=%lld.%03ld monotonic=%lld.%03ld process=%llu.%03llu tz=%s", (long long) rt.tv_sec,
             rt.tv_nsec / 1000000, (long long) mono.tv_sec, mono.tv_nsec / 1000000,
             (unsigned long long) (proc / 1000000), (unsigned long long) ((proc / 1000) % 1000),
             getenv("TZ") ? getenv("TZ") : "(unset)");
}

#endif
