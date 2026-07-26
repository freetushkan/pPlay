//
// Monotonic clock for the PS4.
//
// The PS4 libc is a musl port, so <time.h> hands out *Linux* clock ids while
// the kernel underneath is FreeBSD and expects the FreeBSD ones:
//
//   id | musl <time.h>            | PS4 kernel (FreeBSD)
//   ---+--------------------------+--------------------------------
//    0 | CLOCK_REALTIME           | CLOCK_REALTIME
//    1 | CLOCK_MONOTONIC          | CLOCK_VIRTUAL  <- process cpu time
//    2 | CLOCK_PROCESS_CPUTIME_ID | CLOCK_PROF
//    3 | CLOCK_THREAD_CPUTIME_ID  | (unassigned)
//    4 | CLOCK_MONOTONIC_RAW      | CLOCK_MONOTONIC
//    5 | CLOCK_REALTIME_COARSE    | CLOCK_UPTIME
//    7 | CLOCK_BOOTTIME           | CLOCK_UPTIME_PRECISE
//   14 | -                        | CLOCK_THREAD_CPUTIME_ID
//   15 | -                        | CLOCK_PROCESS_CPUTIME_ID
//
// Only id 0 lines up, which is why the wall clock (gettimeofday, time(),
// std::chrono::system_clock) is the one thing that behaves. Everything asking
// for CLOCK_MONOTONIC gets CLOCK_VIRTUAL instead: a counter that advances with
// how much cpu the process burns rather than with real time. That is what makes
// the ui animations track the mpv decode rate, freeze while playback is paused
// and speed up along with the playback speed.
//
// It is not only libcross2d that is affected: ffmpeg's av_gettime_relative(),
// std::chrono::steady_clock and mbedtls' timing helpers all read
// clock_gettime(CLOCK_MONOTONIC) too, so every network timeout, retry delay and
// interrupt deadline inside the statically linked libraries is computed against
// the cpu clock. libmpv already works around this on its own (the ps4 portlib
// patch does "#undef _POSIX_TIMERS" in osdep/timer-linux.c so mpv falls back to
// gettimeofday); ffmpeg does not.
//
// Rather than patching each library, this file defines clock_gettime() in the
// executable. Object files are linked before the libraries, so this definition
// resolves the undefined clock_gettime of libc, ffmpeg, mbedtls and libc++ in
// one go (the pplay link passes --allow-multiple-definition, so the
// libkernel stub losing the tie is not an error).
//
// The monotonic source itself is picked at runtime and re-validated against the
// wall clock while the app runs, so a source that turns out to misbehave is
// dropped instead of silently poisoning every timer again.
//

#ifdef __PS4__

// musl only exposes the CLOCK_* ids outside of strict ansi mode.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "ps4_platform.h"

// Declared here instead of via <orbis/libkernel.h>: that header declares
// sceKernelClockGettime() without a prototype, which cannot be reconciled with
// the real signature.
extern int sceKernelGettimeofday(struct timeval *tv);

extern int sceKernelClockGettime(int clock_id, struct timespec *ts);

extern uint64_t sceKernelGetProcessTime(void);

extern int sceKernelUsleep(unsigned int microseconds);

extern int sceKernelDebugOutText(int channel, const char *text, ...);

// FreeBSD clock ids, the ones the kernel actually understands.
#define FBSD_CLOCK_REALTIME 0
#define FBSD_CLOCK_VIRTUAL 1
#define FBSD_CLOCK_PROF 2
#define FBSD_CLOCK_MONOTONIC 4
#define FBSD_CLOCK_UPTIME 5
#define FBSD_CLOCK_UPTIME_PRECISE 7
#define FBSD_CLOCK_THREAD_CPUTIME_ID 14
#define FBSD_CLOCK_PROCESS_CPUTIME_ID 15

#define USEC_PER_SEC 1000000ull

enum {
    MONO_SRC_PROCESS_TIME = 0,
    MONO_SRC_KERNEL_MONOTONIC,
    MONO_SRC_REALTIME,
    MONO_SRC_COUNT
};

static const char *const mono_src_names[MONO_SRC_COUNT] = {
    "sceKernelGetProcessTime()",
    "sceKernelClockGettime(CLOCK_MONOTONIC=4)",
    "sceKernelGettimeofday() [latched]"
};

// Number of pplay_ps4_monotonic_us() calls between two drift checks. The check
// needs a wall clock read, so it is kept off the hot path.
#define DRIFT_CHECK_INTERVAL 128

// Shortest window a drift verdict is allowed to be based on.
#define DRIFT_CHECK_WINDOW_US USEC_PER_SEC

static pthread_once_t mono_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t mono_lock = PTHREAD_MUTEX_INITIALIZER;

static int mono_source = MONO_SRC_PROCESS_TIME;
static uint64_t mono_origin;  // raw source reading when the source was anchored
static uint64_t mono_offset;  // value handed out at that moment
static uint64_t mono_last;    // latch, keeps the result non decreasing
static uint64_t drift_wall;   // wall clock at the last drift checkpoint
static uint64_t drift_mono;   // monotonic value at the last drift checkpoint
static unsigned drift_countdown = DRIFT_CHECK_INTERVAL;

static void debug_out(const char *message) {
    sceKernelDebugOutText(0, message);
}

// Also goes to the captured stdout, so it ends up in /data/pplay/stdio.log next
// to whatever ffmpeg and mbedtls printed. Only safe outside the hot path.
static void log_line(const char *message) {
    sceKernelDebugOutText(0, message);
    fputs(message, stdout);
    fflush(stdout);
}

static uint64_t timespec_to_us(const struct timespec *ts) {
    return (uint64_t) ts->tv_sec * USEC_PER_SEC + (uint64_t) (ts->tv_nsec / 1000);
}

static uint64_t kernel_clock_us(int fbsd_clock_id) {
    struct timespec ts;
    memset(&ts, 0, sizeof(ts));
    if (sceKernelClockGettime(fbsd_clock_id, &ts) != 0) {
        return 0;
    }
    return timespec_to_us(&ts);
}

uint64_t pplay_ps4_realtime_us(void) {
    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    if (sceKernelGettimeofday(&tv) == 0) {
        return (uint64_t) tv.tv_sec * USEC_PER_SEC + (uint64_t) tv.tv_usec;
    }
    return kernel_clock_us(FBSD_CLOCK_REALTIME);
}

static uint64_t raw_source_us(int source) {
    switch (source) {
        case MONO_SRC_PROCESS_TIME:
            return sceKernelGetProcessTime();
        case MONO_SRC_KERNEL_MONOTONIC:
            return kernel_clock_us(FBSD_CLOCK_MONOTONIC);
        default:
            return pplay_ps4_realtime_us();
    }
}

// Switches to `source` while keeping the value handed out continuous.
static void mono_anchor(int source, uint64_t value_now) {
    mono_source = source;
    mono_origin = raw_source_us(source);
    mono_offset = value_now;
    drift_wall = pplay_ps4_realtime_us();
    drift_mono = value_now;
    drift_countdown = DRIFT_CHECK_INTERVAL;
}

static void mono_init(void) {
    mono_last = 0;
    mono_anchor(MONO_SRC_PROCESS_TIME, 0);
}

// Compares the current source against the wall clock and demotes it if the two
// disagree by more than a factor of two. Called with mono_lock held.
static void mono_check_drift(uint64_t value) {
    uint64_t wall, elapsed_wall, elapsed_mono;
    char message[256];

    if (mono_source == MONO_SRC_REALTIME || --drift_countdown != 0) {
        return;
    }
    drift_countdown = DRIFT_CHECK_INTERVAL;

    wall = pplay_ps4_realtime_us();
    elapsed_wall = wall > drift_wall ? wall - drift_wall : 0;
    if (elapsed_wall < DRIFT_CHECK_WINDOW_US) {
        // Not enough of a window to judge, keep the checkpoint and retry later.
        return;
    }

    elapsed_mono = value > drift_mono ? value - drift_mono : 0;
    if (elapsed_mono * 2 < elapsed_wall || elapsed_mono > elapsed_wall * 2) {
        snprintf(message, sizeof(message),
                 "[pPlay][ps4time] %s advanced %llu us over %llu us of wall time, "
                 "falling back to %s\n",
                 mono_src_names[mono_source],
                 (unsigned long long) elapsed_mono,
                 (unsigned long long) elapsed_wall,
                 mono_src_names[mono_source + 1]);
        debug_out(message);
        mono_anchor(mono_source + 1, value);
        return;
    }

    drift_wall = wall;
    drift_mono = value;
}

uint64_t pplay_ps4_monotonic_us(void) {
    uint64_t raw, value;

    pthread_once(&mono_once, mono_init);
    pthread_mutex_lock(&mono_lock);

    raw = raw_source_us(mono_source);
    value = raw > mono_origin ? mono_offset + (raw - mono_origin) : mono_offset;

    mono_check_drift(value);

    if (value < mono_last) {
        value = mono_last;
    } else {
        mono_last = value;
    }

    pthread_mutex_unlock(&mono_lock);

    return value;
}

static void us_to_timespec(uint64_t us, struct timespec *ts) {
    ts->tv_sec = (time_t) (us / USEC_PER_SEC);
    ts->tv_nsec = (long) ((us % USEC_PER_SEC) * 1000);
}

// Replaces the libc/libkernel clock_gettime for the whole executable, see the
// comment at the top of the file.
int clock_gettime(clockid_t clock_id, struct timespec *tp) {
    int kernel_clock_id;

    if (tp == NULL) {
        errno = EFAULT;
        return -1;
    }

    switch ((int) clock_id) {
        case CLOCK_REALTIME:
        case CLOCK_REALTIME_COARSE:
        case CLOCK_REALTIME_ALARM:
            us_to_timespec(pplay_ps4_realtime_us(), tp);
            return 0;
        case CLOCK_MONOTONIC:
        case CLOCK_MONOTONIC_RAW:
        case CLOCK_MONOTONIC_COARSE:
        case CLOCK_BOOTTIME:
        case CLOCK_BOOTTIME_ALARM:
            us_to_timespec(pplay_ps4_monotonic_us(), tp);
            return 0;
        case CLOCK_PROCESS_CPUTIME_ID:
            kernel_clock_id = FBSD_CLOCK_PROCESS_CPUTIME_ID;
            break;
        case CLOCK_THREAD_CPUTIME_ID:
            kernel_clock_id = FBSD_CLOCK_THREAD_CPUTIME_ID;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    memset(tp, 0, sizeof(*tp));
    if (sceKernelClockGettime(kernel_clock_id, tp) != 0) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

// Resolution is not something the FreeBSD ids would report any differently, and
// musl leaves clock_getres() to the platform as well. One microsecond matches
// what every source above actually delivers.
int clock_getres(clockid_t clock_id, struct timespec *res) {
    (void) clock_id;
    if (res != NULL) {
        res->tv_sec = 0;
        res->tv_nsec = 1000;
    }
    return 0;
}

struct probe_source {
    const char *name;
    int is_kernel_clock;
    int clock_id;
};

static const struct probe_source probe_sources[] = {
    {"sceKernelGetProcessTime()               ", 0, 0},
    {"sceKernelClockGettime(0)  REALTIME      ", 1, FBSD_CLOCK_REALTIME},
    {"sceKernelClockGettime(1)  VIRTUAL       ", 1, FBSD_CLOCK_VIRTUAL},
    {"sceKernelClockGettime(2)  PROF          ", 1, FBSD_CLOCK_PROF},
    {"sceKernelClockGettime(4)  MONOTONIC     ", 1, FBSD_CLOCK_MONOTONIC},
    {"sceKernelClockGettime(5)  UPTIME        ", 1, FBSD_CLOCK_UPTIME},
    {"sceKernelClockGettime(7)  UPTIME_PRECISE", 1, FBSD_CLOCK_UPTIME_PRECISE},
};

#define PROBE_SOURCE_COUNT ((int) (sizeof(probe_sources) / sizeof(probe_sources[0])))
#define PROBE_SLEEP_US 250000u

static uint64_t probe_read(const struct probe_source *source) {
    return source->is_kernel_clock ? kernel_clock_us(source->clock_id)
                                   : sceKernelGetProcessTime();
}

void pplay_ps4_time_probe(void) {
    uint64_t before[PROBE_SOURCE_COUNT], after[PROBE_SOURCE_COUNT];
    uint64_t wall_before, wall_after, wall_elapsed;
    char message[256];
    int i;

    wall_before = pplay_ps4_realtime_us();
    for (i = 0; i < PROBE_SOURCE_COUNT; i++) {
        before[i] = probe_read(&probe_sources[i]);
    }

    sceKernelUsleep(PROBE_SLEEP_US);

    for (i = 0; i < PROBE_SOURCE_COUNT; i++) {
        after[i] = probe_read(&probe_sources[i]);
    }
    wall_after = pplay_ps4_realtime_us();

    wall_elapsed = wall_after > wall_before ? wall_after - wall_before : 0;
    snprintf(message, sizeof(message),
             "[pPlay][ps4time] clock probe, %llu us of wall time elapsed:\n",
             (unsigned long long) wall_elapsed);
    log_line(message);

    for (i = 0; i < PROBE_SOURCE_COUNT; i++) {
        uint64_t elapsed = after[i] > before[i] ? after[i] - before[i] : 0;
        // Permille of the wall clock: 1000 means the source keeps real time.
        unsigned long long permille = wall_elapsed > 0
                                          ? (unsigned long long) ((elapsed * 1000) / wall_elapsed)
                                          : 0;
        snprintf(message, sizeof(message),
                 "[pPlay][ps4time]   %s %10llu us (%llu/1000 of real time)\n",
                 probe_sources[i].name, (unsigned long long) elapsed, permille);
        log_line(message);
    }

    pthread_once(&mono_once, mono_init);
    pthread_mutex_lock(&mono_lock);
    snprintf(message, sizeof(message), "[pPlay][ps4time] monotonic source in use: %s\n",
             mono_src_names[mono_source]);
    pthread_mutex_unlock(&mono_lock);
    log_line(message);
}

#endif // __PS4__
