#ifdef __PS4__
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>

#include <orbis/libkernel.h>

#define MAX_MESSAGE_SIZE    0x1000
#define MAX_STACK_FRAMES    32

/**
 * A callframe captures the stack and program pointer of a
 * frame in the call path.
 **/
typedef struct  {
  void *sp;
  void *pc;
} callframe_t;


/**
 * Log a backtrace to /data/pplay/crash.log
 **/
static void 
ulong_to_hex(unsigned long num, char* out) {
    char buf[16] = "0123456789abcdef";
    for (int i = 15; i >= 0; i--) {
        out[i] = buf[num & 0xF];
        num >>= 4;
    }
    out[16] = '\0';
}

static void
backtrace(int sig) {
  callframe_t frames[MAX_STACK_FRAMES];
  unsigned int nb_frames = 0;
  int fd = open("/data/pplay/crash.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd < 0) {
      _exit(1);
  }
  write(fd, "[Crashlog]: Fatal signal received: ", 35);
  char sig_str[3];
  if (sig >= 10) {
      sig_str[0] = '0' + (sig / 10);
      sig_str[1] = '0' + (sig % 10);
      sig_str[2] = '\n';
      write(fd, sig_str, 3);
  } else {
      sig_str[0] = '0' + sig;
      sig_str[1] = '\n';
      write(fd, sig_str, 2);
  }
  write(fd, "[Crashlog]: Backtrace:\n", 23);
  sceKernelBacktraceSelf(frames, sizeof frames, &nb_frames, 0);
  char hex_buf[17];
  for(unsigned int i = 0; i < nb_frames; i++) {
    ulong_to_hex((unsigned long)frames[i].pc, hex_buf);
    write(fd, "  # ", 4);
    write(fd, hex_buf, 16);
    write(fd, "\n", 1);
  }
  close(fd);
}

/**
 * Log fatal signals to kernel log.
 **/
static void
fatal_signal(int sig) {
  backtrace(sig);
  _exit(1);
}


/**
 * Register handlers for fatal signals.
 **/
static void __attribute__((constructor))
init_signal(void) {
  signal(5, fatal_signal);  // SIGTRAP
  signal(6, fatal_signal);  // SIGABRT
  signal(8, fatal_signal);  // SIGFPE
  signal(10, fatal_signal); // SIGBUS
  signal(11, fatal_signal); // SIGSEGV
}
#endif
