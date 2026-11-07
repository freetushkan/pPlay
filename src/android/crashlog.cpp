//
// Android fatal-signal diagnostics: the signal, the frames (library + offset, the symbol if
// exported) to crash.log and logcat, then the system handler (the tombstone). Only writes in
// the handler. Also: a stuck main loop (its frames, by a signal to it), and the logcat of the
// previous run (the system crash report, the abort message) in logcat.txt.
//

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <unwind.h>
#include <android/log.h>

#include "crashlog.h"

namespace {

    constexpr int Signals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP};
    constexpr int MaxFrames = 64;

    // the main loop: a report after this, then every HangRepeat (the same stall)
    constexpr int HangSecs = 4;
    constexpr int HangRepeat = 10;
    constexpr int HangReports = 4;

    int crashFd = -1;
    struct sigaction previous[NSIG];
    char altStack[64 * 1024];

    pid_t mainTid = 0;
    std::atomic<unsigned int> beats{0};
    std::atomic<bool> idle{false};

    void writeLine(const char *line) {
        if (crashFd >= 0) {
            ssize_t ignored = write(crashFd, line, strlen(line));
            (void) ignored;
        }
        __android_log_write(ANDROID_LOG_FATAL, "pPlay", line);
    }

    void writeFrame(int index, uintptr_t pc) {
        char line[512];
        Dl_info info{};
        if (dladdr((void *) pc, &info) != 0 && info.dli_fname != nullptr) {
            const char *name = strrchr(info.dli_fname, '/');
            name = name != nullptr ? name + 1 : info.dli_fname;
            if (info.dli_sname != nullptr) {
                snprintf(line, sizeof(line), "#%02d pc %p %s+0x%lx (%s+0x%lx)\n", index, (void *) pc, name,
                         (unsigned long) (pc - (uintptr_t) info.dli_fbase), info.dli_sname,
                         (unsigned long) (pc - (uintptr_t) info.dli_saddr));
            } else {
                snprintf(line, sizeof(line), "#%02d pc %p %s+0x%lx\n", index, (void *) pc, name,
                         (unsigned long) (pc - (uintptr_t) info.dli_fbase));
            }
        } else {
            snprintf(line, sizeof(line), "#%02d pc %p\n", index, (void *) pc);
        }
        writeLine(line);
    }

    struct Trace {
        uintptr_t frames[MaxFrames];
        int count;
    };

    _Unwind_Reason_Code onFrame(_Unwind_Context *context, void *arg) {
        auto *trace = (Trace *) arg;
        uintptr_t pc = _Unwind_GetIP(context);
        if (pc != 0) {
            if (trace->count >= MaxFrames) {
                return _URC_END_OF_STACK;
            }
            trace->frames[trace->count++] = pc;
        }
        return _URC_NO_REASON;
    }

    uintptr_t faultPc(void *context) {
        auto *uc = (ucontext_t *) context;
#if defined(__aarch64__)
        return (uintptr_t) uc->uc_mcontext.pc;
#elif defined(__arm__)
        return (uintptr_t) uc->uc_mcontext.arm_pc;
#elif defined(__x86_64__)
        return (uintptr_t) uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__i386__)
        return (uintptr_t) uc->uc_mcontext.gregs[REG_EIP];
#else
        (void) uc;
        return 0;
#endif
    }

    // the caller of the interrupted function (the unwinder may stop in a library without tables)
    uintptr_t linkRegister(void *context) {
        auto *uc = (ucontext_t *) context;
#if defined(__aarch64__)
        return (uintptr_t) uc->uc_mcontext.regs[30];
#elif defined(__arm__)
        return (uintptr_t) uc->uc_mcontext.arm_lr;
#else
        (void) uc;
        return 0;
#endif
    }

    void writeHeader(const char *what) {
        char line[256];
        time_t now = time(nullptr);
        struct tm tm{};
        gmtime_r(&now, &tm);
        snprintf(line, sizeof(line),
                 "\n========================================\n"
                 "%04d.%02d.%02d %02d:%02d:%02d UTC pPlay %s, process %d, thread %d\n",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, what,
                 (int) getpid(), (int) gettid());
        writeLine(line);
    }

    // the interrupted instruction, its caller, then the frames the unwinder finds
    void writeFrames(void *context) {
        uintptr_t pc = faultPc(context);
        int index = 0;
        if (pc != 0) {
            writeFrame(index++, pc);
        }
        uintptr_t lr = linkRegister(context);
        if (lr != 0) {
            writeLine("lr:\n");
            writeFrame(index++, lr);
        }
        Trace trace{};
        _Unwind_Backtrace(onFrame, &trace);
        for (int i = 0; i < trace.count; i++) {
            writeFrame(index++, trace.frames[i]);
        }
        writeLine("========================================\n");
        if (crashFd >= 0) {
            fsync(crashFd);
        }
    }

    void onSignal(int signal, siginfo_t *info, void *context) {
        char what[160];
        snprintf(what, sizeof(what), "crash: signal %d (%s), code %d, address %p", signal, strsignal(signal),
                 info != nullptr ? info->si_code : 0, info != nullptr ? info->si_addr : nullptr);
        writeHeader(what);
        writeFrames(context);

        // the system handler (debuggerd: the tombstone), the process ends
        sigaction(signal, &previous[signal], nullptr);
        raise(signal);
    }

    // asked by the watchdog: where the main loop is
    void onHang(int, siginfo_t *, void *context) {
        writeHeader("main loop stuck (still running)");
        writeFrames(context);
    }

    void watchdog() {
        unsigned int last = 0;
        int stuck = 0;
        int reports = 0;
        while (true) {
            sleep(1);
            unsigned int now = beats;
            // not before the loop runs, not in the background
            if (now == 0 || now != last || idle) {
                last = now;
                stuck = 0;
                reports = 0;
                continue;
            }
            stuck++;
            if (reports < HangReports && stuck >= HangSecs && (stuck - HangSecs) % HangRepeat == 0) {
                reports++;
                syscall(SYS_tgkill, getpid(), mainTid, SIGUSR2);
            }
        }
    }

    // the previous run's logcat (this app's lines only), warnings and up
    void saveLogcat(const std::string &path) {
        FILE *in = popen("logcat -d -v threadtime -t 3000 '*:W' 2>&1", "r");
        if (in == nullptr) {
            return;
        }
        FILE *out = fopen(path.c_str(), "w");
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            if (out != nullptr) {
                fwrite(buf, 1, n, out);
            }
        }
        pclose(in);
        if (out != nullptr) {
            fclose(out);
        }
    }
}

void pplay::CrashLog::heartbeat() {
    beats++;
}

void pplay::CrashLog::setIdle(bool value) {
    idle = value;
}

void pplay::CrashLog::install(const std::string &logPath) {
    crashFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

    // a stack overflow handled too (this thread)
    stack_t stack{};
    stack.ss_sp = altStack;
    stack.ss_size = sizeof(altStack);
    sigaltstack(&stack, nullptr);

    struct sigaction action{};
    action.sa_sigaction = onSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int signal: Signals) {
        sigaction(signal, &action, &previous[signal]);
    }

    // the main loop watched (this thread)
    mainTid = gettid();
    struct sigaction hang{};
    hang.sa_sigaction = onHang;
    hang.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
    sigemptyset(&hang.sa_mask);
    sigaction(SIGUSR2, &hang, nullptr);

    std::string logcatPath = logPath.substr(0, logPath.find_last_of('/') + 1) + "logcat.txt";
    std::thread([logcatPath] {
        saveLogcat(logcatPath);
        watchdog();
    }).detach();
}
