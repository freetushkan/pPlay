#ifdef __PS4__
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <orbis/libkernel.h>

#include "ps4_platform.h"

#define MAX_MESSAGE_SIZE    0x1000
#define MAX_STACK_FRAMES    32

#define CRASH_LOG_DIR       "/data/pplay"
#define CRASH_LOG_PATH      CRASH_LOG_DIR "/crash.log"

/**
 * A callframe captures the stack and program pointer of a
 * frame in the call path.
 **/
typedef struct  {
  void *sp;
  void *pc;
} callframe_t;

static volatile sig_atomic_t handling_fatal_signal = 0;
static int handlers_installed = 0;


/**
 * Log a backtrace to /dev/klog and /data/pplay/crash.log
 **/

static void append_file_log(const char *text) {
  int fd = open(CRASH_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd < 0) {
    return;
  }
  if (text) {
    write(fd, text, strlen(text));
  }
  close(fd);
}

/**
 * Appends to a fixed buffer, returning the new length. Kept explicit because
 * strncat() bounds what it appends rather than the destination, so chaining it
 * over 32 frames can walk past the end of the buffer.
 **/
static size_t append(char *dst, size_t size, size_t at, const char *src) {
  size_t len;

  if (!src || at + 1 >= size) {
    return at;
  }

  len = strlen(src);
  if (len > size - at - 1) {
    len = size - at - 1;
  }

  memcpy(dst + at, src, len);
  dst[at + len] = '\0';

  return at + len;
}

static void
backtrace(const char* reason) {
  char addr2line[MAX_STACK_FRAMES * 24];
  callframe_t frames[MAX_STACK_FRAMES];
  OrbisKernelVirtualQueryInfo info;
  char buf[MAX_MESSAGE_SIZE];
  unsigned int nb_frames = 0;
  size_t buf_len = 0;
  size_t addr_len = 0;
  char temp[128];

  memset(addr2line, 0, sizeof addr2line);
  memset(frames, 0, sizeof frames);
  memset(buf, 0, sizeof buf);

  snprintf(temp, sizeof temp, "<118>[Crashlog]: %s\n", reason);
  buf_len = append(buf, sizeof buf, buf_len, temp);
  buf_len = append(buf, sizeof buf, buf_len, "<118>[Crashlog]: Backtrace:\n");

  sceKernelBacktraceSelf(frames, sizeof frames, &nb_frames, 0);
  if (nb_frames > MAX_STACK_FRAMES) {
    nb_frames = MAX_STACK_FRAMES;
  }

  for(unsigned int i=0; i<nb_frames; i++) {
    memset(&info, 0, sizeof info);
    sceKernelVirtualQuery(frames[i].pc, 0, &info, sizeof info);

    snprintf(temp, sizeof temp,
	     "<118>[Crashlog]:   #%02d %32s: 0x%lx\n",
	     i + 1, info.name, frames[i].pc - info.unk01 - 1);
    buf_len = append(buf, sizeof buf, buf_len, temp);

    snprintf(temp, sizeof temp,
	     "0x%lx ", frames[i].pc - info.unk01 - 1);
    addr_len = append(addr2line, sizeof addr2line, addr_len, temp);
  }

  buf_len = append(buf, sizeof buf, buf_len, "<118>[Crashlog]: addr2line: ");
  buf_len = append(buf, sizeof buf, buf_len, addr2line);
  append(buf, sizeof buf, buf_len, "\n");

  sceKernelDebugOutText(0, buf);
  append_file_log(buf);
}


/**
 * Log fatal signals to kernel log.
 **/
static void
fatal_signal(int sig) {
  char reason[64];

  /* A fault inside the handler must not loop back into it. */
  if (handling_fatal_signal) {
    _exit(1);
  }
  handling_fatal_signal = 1;
  signal(sig, SIG_DFL);

  /* Whatever the libraries printed just before the fault is the other half of
     the story, so get it on disk before walking the stack. */
  pplay_ps4_debug_flush();

  snprintf(reason, sizeof(reason), "Received the fatal POSIX signal %d", sig);
  backtrace(reason);

  pplay_ps4_debug_flush();
  _exit(1);
}


/**
 * Register handlers for fatal signals.
 **/
void
pplay_ps4_crashlog_init(void) {
  if (handlers_installed) {
    return;
  }
  handlers_installed = 1;

  mkdir(CRASH_LOG_DIR, 0777);

  signal(4, fatal_signal);  // SIGILL
  signal(5, fatal_signal);  // SIGTRAP
  signal(6, fatal_signal);  // SIGABRT
  signal(8, fatal_signal);  // SIGFPE
  signal(10, fatal_signal); // SIGBUS
  signal(11, fatal_signal); // SIGSEGV
  signal(12, fatal_signal); // SIGSYS
}

static void __attribute__((constructor))
init_signal(void) {
  pplay_ps4_crashlog_init();
}
#endif
