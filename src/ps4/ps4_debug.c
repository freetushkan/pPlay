//
// stdout/stderr capture for the PS4.
//
// A packaged title has nothing attached to fd 1 and 2, so everything the
// statically linked libraries print is written to descriptors that are not
// open. ffmpeg and mbedtls both log to stderr, which musl keeps unbuffered:
// every av_log() call turns straight into a write() on a dead descriptor,
// and none of it is ever visible.
//
// Pointing both descriptors at a real file makes the writes well defined and,
// more usefully, keeps the last thing the libraries said before a crash.
//

#ifdef __PS4__

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps4_platform.h"

extern int sceKernelDebugOutText(int channel, const char *text, ...);

#define PPLAY_PS4_DATA_DIR "/data/pplay"
#define PPLAY_PS4_STDIO_LOG PPLAY_PS4_DATA_DIR "/stdio.log"

static int stdio_fd = -1;

void pplay_ps4_debug_init(void) {
    if (stdio_fd >= 0) {
        return;
    }

    // Runs before Main creates the data directory.
    mkdir(PPLAY_PS4_DATA_DIR, 0777);

    stdio_fd = open(PPLAY_PS4_STDIO_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (stdio_fd < 0) {
        sceKernelDebugOutText(0, "[pPlay] stdio capture: cannot open " PPLAY_PS4_STDIO_LOG "\n");
        return;
    }

    if (stdio_fd != STDOUT_FILENO) {
        dup2(stdio_fd, STDOUT_FILENO);
    }
    if (stdio_fd != STDERR_FILENO) {
        dup2(stdio_fd, STDERR_FILENO);
    }

    // Against a file musl buffers stdout in full blocks, which throws away
    // everything written since the last flush when the process dies.
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    fputs("=== pPlay stdout/stderr capture ===\n", stdout);
    fflush(stdout);

    sceKernelDebugOutText(0, "[pPlay] stdio capture -> " PPLAY_PS4_STDIO_LOG "\n");
}

void pplay_ps4_debug_flush(void) {
    fflush(NULL);
    if (stdio_fd >= 0) {
        fsync(stdio_fd);
    }
}

#endif // __PS4__
