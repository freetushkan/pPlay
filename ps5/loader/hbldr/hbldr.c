/*
 * Based on 'src/ps5/hbldr.c' (commit 7748df9) from websrv by John Törnblom.
 * Copyright (C) 2024 John Törnblom.
 * Licensed under the GNU General Public License v3.0 or later (GPL-3.0-or-later).
 * Project URL: https://github.com/ps5-payload-dev/websrv
 */


#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <ps5/kernel.h>

#include "elfldr.h"
#include "hbldr.h"
#include "pt.h"


/**
 * Read a whole file (from websrv fs.c).
 **/
static uint8_t*
fs_readfile(const char* path, size_t* size) {
  uint8_t* buf;
  long len;
  FILE* file;

  if(!(file=fopen(path, "rb"))) {
    return 0;
  }
  if(fseek(file, 0, SEEK_END) || (len=ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) {
    fclose(file);
    return 0;
  }
  if(!(buf=malloc(len))) {
    fclose(file);
    return 0;
  }
  if(fread(buf, 1, len, file) != (size_t)len) {
    free(buf);
    fclose(file);
    return 0;
  }
  fclose(file);
  if(size) {
    *size = len;
  }
  return buf;
}


#define PSNOW_EBOOT "/system_ex/app/NPXS40106/eboot.bin"
#define FAKE_TITLE_ID "PPLY00005"
#define FAKE_PATH "/system_ex/app/" FAKE_TITLE_ID

#define IOVEC_ENTRY(x) {x ? x : 0, \
			x ? strlen(x)+1 : 0}
#define IOVEC_SIZE(x) (sizeof(x) / sizeof(struct iovec))


static const char param_json[] = "{\n"
  "  \"applicationCategoryType\": 0,\n"
  "  \"attribute\": 1,\n"
  "  \"attribute2\": 0,\n"
  "  \"attribute3\": 4,\n"
  "  \"titleId\": \"" FAKE_TITLE_ID "\",\n"
  "  \"contentId\": \"IV9999-" FAKE_TITLE_ID "_00-PPLAYMEDIAPLAYER\",\n"
  "  \"localizedParameters\": {\n"
  "    \"defaultLanguage\": \"en-US\",\n"
  "    \"en-US\": {\n"
  "      \"titleName\": \"pPlay\"\n"
  "    }\n"
  "  }\n"
  "}\n";


typedef struct app_launch_ctx {
  uint32_t structsize;
  uint32_t user_id;
  uint32_t app_opt;
  uint64_t crash_report;
  uint32_t check_flag;
} app_launch_ctx_t;


int sceUserServiceInitialize(void*);
int sceUserServiceGetForegroundUser(uint32_t *user_id);
int sceUserServiceTerminate(void);

int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceKillApp(int app_id, int how, int reason, int core_dump);
int sceSystemServiceLaunchApp(const char* title_id, char** argv,
			      app_launch_ctx_t* ctx);

int sceKernelGetAppState(int app_id, int*, int*);


#define LAUNCH_TIMEOUT 20 /* seconds, for SceSysCore to fork the application */

static char g_error[256];
static volatile int g_launch_ret = 0;

static void
set_error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(g_error, sizeof(g_error), fmt, args);
  va_end(args);
  printf("hbldr: %s\n", g_error);
}


const char*
hbldr_error(void) {
  return g_error;
}


static int
remount_system_ex(void) {
  struct iovec iov[] = {
    IOVEC_ENTRY("from"),      IOVEC_ENTRY("/dev/ssd0.system_ex"),
    IOVEC_ENTRY("fspath"),    IOVEC_ENTRY("/system_ex"),
    IOVEC_ENTRY("fstype"),    IOVEC_ENTRY("exfatfs"),
    IOVEC_ENTRY("large"),     IOVEC_ENTRY("yes"),
    IOVEC_ENTRY("timezone"),  IOVEC_ENTRY("static"),
    IOVEC_ENTRY("async"),     IOVEC_ENTRY(NULL),
    IOVEC_ENTRY("ignoreacl"), IOVEC_ENTRY(NULL),
  };

  if(nmount(iov, IOVEC_SIZE(iov), MNT_UPDATE)) {
    return -1;
  }

  return 0;
}



static int
fakeapp_create_if_missing(void) {
  struct stat info;
  uint8_t* buf;
  size_t size;
  int fd;

  if(stat(FAKE_PATH, &info)) {
    if(mkdir(FAKE_PATH, 0755)) {
      return -1;
    }
  }

  if(stat(FAKE_PATH "/sce_sys", &info)) {
    if(mkdir(FAKE_PATH "/sce_sys", 0755)) {
      return -1;
    }
  }

  if(stat(FAKE_PATH "/sce_sys/param.json", &info)) {
    if((fd=open(FAKE_PATH "/sce_sys/param.json", O_CREAT|O_WRONLY, 0644)) < 0) {
      return -1;
    }
    if(write(fd, param_json, sizeof(param_json)-1) != sizeof(param_json)-1) {
      close(fd);
      return -1;
    }
    close(fd);
  }

  if(stat(FAKE_PATH "/eboot.bin", &info)) {
    if(!(buf=fs_readfile(PSNOW_EBOOT, &size))) {
      return -1;
    }
    if((fd=open(FAKE_PATH "/eboot.bin", O_CREAT|O_WRONLY, 0755)) < 0) {
      free(buf);
      return -1;
    }
    if(write(fd, buf, size) != size) {
      free(buf);
      close(fd);
      return -1;
    }
    free(buf);
    close(fd);
  }
  return 0;
}


/**
 * Get the pid of a process with the given name.
 **/
static pid_t
find_pid(const char* name) {
  int mib[4] = {1, 14, 8, 0};
  pid_t mypid = getpid();
  pid_t pid = -1;
  size_t buf_size;
  uint8_t *buf;

  if(sysctl(mib, 4, 0, &buf_size, 0, 0)) {
    perror("sysctl");
    return -1;
  }

  if(!(buf=malloc(buf_size))) {
    perror("malloc");
    return -1;
  }

  if(sysctl(mib, 4, buf, &buf_size, 0, 0)) {
    perror("sysctl");
    return -1;
  }

  for(uint8_t *ptr=buf; ptr<(buf+buf_size);) {
    int ki_structsize = *(int*)ptr;
    pid_t ki_pid = *(pid_t*)&ptr[72];
    char *ki_tdname = (char*)&ptr[447];

    ptr += ki_structsize;
    if(!strcmp(name, ki_tdname) && mypid != ki_pid) {
      pid = ki_pid;
    }
  }

  free(buf);

  return pid;
}


/**
 *
 **/
static void*
bigapp_launch_thread(void* arg) {
  char** argv = (char**)arg;
  app_launch_ctx_t ctx = {0};

  if((g_launch_ret=sceUserServiceGetForegroundUser(&ctx.user_id))) {
    return 0;
  }

  g_launch_ret = sceSystemServiceLaunchApp(FAKE_TITLE_ID, argv, &ctx);

  return 0;
}


/**
 *
 **/
static pid_t
bigapp_launch(char** argv) {
  struct kevent evt = {0};
  struct timespec timeout = {LAUNCH_TIMEOUT, 0};
  pid_t parent = -1;
  pid_t child = -1;
  pthread_t trd;
  int kq = -1;

  if((parent=find_pid("SceSysCore.elf")) < 0) {
    set_error("SceSysCore.elf not found");
    return -1;
  }

  if((kq=kqueue()) < 0) {
    perror("kqueue");
    return -1;
  }

  EV_SET(&evt, parent, EVFILT_PROC, EV_ADD | EV_ENABLE | EV_CLEAR,
	 NOTE_FORK | NOTE_EXEC | NOTE_TRACK, 0, 0);
  if(kevent(kq, &evt, 1, 0, 0, 0) < 0) {
    perror("kevent");
    close(kq);
    return -1;
  }

  g_launch_ret = 0;
  pthread_create(&trd, 0, &bigapp_launch_thread, (void*)argv);
  pthread_detach(trd);

  // nothing is forked when the launch fails
  int n = kevent(kq, 0, 0, &evt, 1, &timeout);
  close(kq);
  if(n <= 0) {
    set_error("%s was not started (launch: 0x%x)", FAKE_TITLE_ID, (unsigned) g_launch_ret);
    return -1;
  }

  if(!(evt.fflags & NOTE_CHILD)) {
    set_error("unexpected SceSysCore event (0x%x, launch: 0x%x)", (unsigned) evt.fflags,
              (unsigned) g_launch_ret);
    return -1;
  }

  child = evt.ident;

  if(pt_attach(child)) {
    perror("pt_attach");
    return -1;
  }

  if(pt_follow_exec(child) < 0) {
    perror("pt_follow_exec");
    pt_detach(child, SIGKILL);
    return -1;
  }

  if(pt_continue(child, SIGCONT) < 0) {
    perror("pt_continue");
    pt_detach(child, SIGKILL);
    return -1;
  }

  if(pt_await_exec(child)) {
    perror("pt_await_exec");
    pt_detach(child, SIGKILL);
    return -1;
  }

  return child;
}


/**
 *
 **/
static int
bigapp_set_argv0(pid_t pid, const char* argv0) {
  intptr_t pos = pt_getargv(pid);
  intptr_t buf = 0;

  // allocate memory
  if((buf=pt_mmap(pid, 0, PAGE_SIZE, PROT_WRITE | PROT_READ,
		  MAP_ANONYMOUS | MAP_PRIVATE,
		  -1, 0)) == -1) {
    pt_perror(pid, "pt_mmap");
    return -1;
  }

  // copy string
  if(pt_copyin(pid, argv0, buf, strlen(argv0)+1)) {
    perror("pt_copyin");
    pt_munmap(pid, buf, PAGE_SIZE);
    return -1;
  }

  // copy pointer to string
  if(pt_setlong(pid, pos, buf)) {
    perror("pt_setlong");
    pt_munmap(pid, buf, PAGE_SIZE);
    return -1;
  }

  return 0;
}


/**
 *
 **/
static pid_t
bigapp_replace(pid_t pid, uint8_t* elf, const char* progname, int stdio,
	       const char* cwd, char** envp) {
  uint8_t int3instr = 0xcc;
  intptr_t brkpoint;
  uint8_t orginstr;

  // Let the kernel assign process parameters accessed via sceKernelGetProcParam()
  if(pt_syscall(pid, 599)) {
    puts("sys_dynlib_process_needed_and_relocate failed");
    //return -1;
  }

  // Allow libc to allocate arbitrary amount of memory.
  elfldr_set_heap_size(pid, -1);

  if(!(brkpoint=kernel_dynlib_entry_addr(pid, 0))) {
    puts("kernel_dynlib_entry_addr failed");
    return -1;
  }
  brkpoint += 58;// offset to invocation of main()

  if(kernel_mprotect(pid, brkpoint, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC)) {
    puts("kernel_mprotect failed");
    pt_detach(pid, SIGKILL);
    return -1;
  }

  if(pt_copyout(pid, brkpoint, &orginstr, sizeof(orginstr))) {
    perror("pt_copyout");
    return -1;
  }
  if(pt_copyin(pid, &int3instr, brkpoint, sizeof(int3instr))) {
    perror("pt_copyin");
    return -1;
  }

  // Continue execution until we hit the breakpoint, then remove it.
  if(pt_continue(pid, SIGCONT)) {
    perror("pt_continue");
    return -1;
  }
  if(waitpid(pid, 0, 0) == -1) {
    perror("waitpid");
    return -1;
  }
  if(pt_copyin(pid, &orginstr, brkpoint, sizeof(orginstr))) {
    perror("pt_copyin");
    return -1;
  }

  bigapp_set_argv0(pid, progname);
  elfldr_raise_privileges(pid);
  elfldr_set_procname(pid, basename(progname));
  elfldr_set_environ(pid, envp);
  elfldr_set_cwd(pid, cwd);
  elfldr_set_stdio(pid, stdio);

  // invoke sys_set_butget(0) to operate on relative paths using open, mkdir, etc.
  // TODO: on old fw this causes a kernel panic when the bigapp is restarted
  if(kernel_get_fw_version() > 0x10000000) {
    pt_syscall(pid, 0x23b, 0);
  }

  // Execute the ELF
  if(elfldr_exec(pid, elf)) {
    return -1;
  }

  return pid;
}


pid_t
hbldr_launch(const char*cwd, const char* path, int stdio, char** argv,
	     char** envp) {
  static int initialized = 0;
  char buf[PATH_MAX];
  uint32_t user_id = 0;
  uint8_t* elf;
  int app_id;
  int err;
  pid_t pid;

  g_error[0] = 0;

  // as websrv: user service and ShellUI authid (to launch/kill applications)
  if(!initialized) {
    sceUserServiceInitialize(0);
    kernel_set_ucred_authid(-1, 0x4801000000000013L);
    initialized = 1;
  }

  // checks before closing the running application
  if((err=sceUserServiceGetForegroundUser(&user_id))) {
    set_error("no foreground user (0x%x)", (unsigned) err);
    return -1;
  }

  if(path[0] == '/') {
    strncpy(buf, path, sizeof(buf));
  } else {
    snprintf(buf, sizeof(buf), "%s/%s", cwd, path);
  }

  if(!(elf=fs_readfile(buf, 0))) {
    set_error("can't read %s", buf);
    return -1;
  }

  if(fakeapp_create_if_missing()) {
    if(remount_system_ex()) {
      set_error("can't remount /system_ex (%s)", strerror(errno));
      free(elf);
      return -1;
    }
    if(fakeapp_create_if_missing()) {
      set_error("can't create %s (%s)", FAKE_PATH, strerror(errno));
      free(elf);
      return -1;
    }
  }

  if((app_id=sceSystemServiceGetAppIdOfRunningBigApp()) > 0) {
    if((err=sceSystemServiceKillApp(app_id, -1, 0, 0))) {
      set_error("can't close the running application (0x%x)", (unsigned) err);
      free(elf);
      return -1;
    }

    for(int i=0; !sceKernelGetAppState(app_id, 0, 0); i++) {
      if(i >= 30) {
        set_error("the running application does not close");
        free(elf);
        return -1;
      }
      sleep(1);
    }
  }

  if((pid=bigapp_launch(argv)) < 0) {
    free(elf);
    return -1;
  }

  if(bigapp_replace(pid, elf, path, stdio, cwd, envp) < 0) {
    if(!g_error[0]) {
      set_error("can't load %s into %s", path, FAKE_TITLE_ID);
    }
    pt_detach(pid, SIGKILL);
    pid = -1;
  }

  free(elf);

  return pid;
}


pid_t
hbldr_find_pid(const char* name) {
  return find_pid(name);
}
