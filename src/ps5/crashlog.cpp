//
// PS5 fatal-signal diagnostics.
//

#if defined(__PS5__)

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>

#include "crashlog.h"

namespace {

    constexpr int MaxFrames = 64;
    constexpr size_t MaxPath = 512;

    int crashFd = -1;
    volatile sig_atomic_t handlingCrash = 0;
    char crashPath[MaxPath] = {};
    char executableName[256] = {};
    uintptr_t executableBase = 0;

    void writeAll(int fd, const char *data, size_t size) {
        if (fd < 0 || data == nullptr) {
            return;
        }
        while (size > 0) {
            ssize_t written = write(fd, data, size);
            if (written <= 0) {
                return;
            }
            data += written;
            size -= (size_t) written;
        }
    }

    void logLine(const char *line) {
        if (line == nullptr) {
            return;
        }
        const size_t size = strlen(line);
        writeAll(crashFd, line, size);
        writeAll(STDERR_FILENO, line, size);
    }

    const char *signalName(int sig) {
        switch (sig) {
            case SIGSEGV: return "SIGSEGV";
            case SIGBUS: return "SIGBUS";
            case SIGILL: return "SIGILL";
            case SIGFPE: return "SIGFPE";
            case SIGABRT: return "SIGABRT";
            case SIGTRAP: return "SIGTRAP";
#ifdef SIGSYS
            case SIGSYS: return "SIGSYS";
#endif
            default: return "UNKNOWN";
        }
    }

    void fatalSignal(int sig, siginfo_t *info, void *) {
        if (handlingCrash) {
            _exit(128 + sig);
        }
        handlingCrash = 1;

        char line[256];
        snprintf(line, sizeof(line),
                 "\n========== pPlay PS5 fatal signal ==========\n"
                 "Signal: %s (%d)\nPID: %d\nFault address: %p\n",
                 signalName(sig), sig, (int) getpid(),
                 info != nullptr ? info->si_addr : nullptr);
        logLine(line);
        snprintf(line, sizeof(line), "Executable: %s\nExecutable load base: 0x%llx\n",
                 executableName[0] ? executableName : "(unknown)",
                 (unsigned long long) executableBase);
        logLine(line);

        void *frames[MaxFrames] = {};
        int count = (int) backtrace(frames, MaxFrames);
        snprintf(line, sizeof(line), "Backtrace (%d frames):\n", count);
        logLine(line);
        for (int i = 0; i < count; i++) {
            const uintptr_t address = (uintptr_t) frames[i];
            if (executableBase != 0 && address >= executableBase) {
                const uintptr_t relative = address - executableBase;
                snprintf(line, sizeof(line), "  #%02d %s+0x%llx [0x%llx]\n", i,
                         executableName, (unsigned long long) relative,
                         (unsigned long long) address);
            } else {
                snprintf(line, sizeof(line), "  #%02d 0x%llx\n", i,
                         (unsigned long long) address);
            }
            logLine(line);
        }

        snprintf(line, sizeof(line), "Crash report: %s\n========== end crash report ==========\n",
                 crashPath[0] ? crashPath : "(unavailable)");
        logLine(line);
        if (crashFd >= 0) {
            fsync(crashFd);
        }

        struct sigaction action {};
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        sigaction(sig, &action, nullptr);

        sigset_t unblock;
        sigemptyset(&unblock);
        sigaddset(&unblock, sig);
        sigprocmask(SIG_UNBLOCK, &unblock, nullptr);
        raise(sig);
        _exit(128 + sig);
    }

} // namespace

void pplay::CrashLog::install(const std::string &logPath) {
    if (crashFd >= 0) {
        return;
    }

    const size_t copySize = std::min(logPath.size(), sizeof(crashPath) - 1);
    memcpy(crashPath, logPath.data(), copySize);
    crashPath[copySize] = '\0';

    Dl_info executableInfo {};
    if (dladdr((void *) &fatalSignal, &executableInfo) != 0) {
        executableBase = (uintptr_t) executableInfo.dli_fbase;
        if (executableInfo.dli_fname != nullptr) {
            const size_t nameSize = std::min(strlen(executableInfo.dli_fname), sizeof(executableName) - 1);
            memcpy(executableName, executableInfo.dli_fname, nameSize);
            executableName[nameSize] = '\0';
        }
    }

    crashFd = open(crashPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (crashFd < 0) {
        char line[640];
        snprintf(line, sizeof(line), "pPlay CrashLog: cannot open %s\n", crashPath);
        writeAll(STDERR_FILENO, line, strlen(line));
    } else {
        char line[768];
#ifdef APP_VERSION_FULL
        snprintf(line, sizeof(line),
                 "\n========== pPlay PS5 session ==========\n"
                 "Build: %s\nPID: %d\nCrash report: %s\n======================================\n",
                 APP_VERSION_FULL, (int) getpid(), crashPath);
#else
        snprintf(line, sizeof(line),
                 "\n========== pPlay PS5 session ==========\n"
                 "PID: %d\nCrash report: %s\n======================================\n",
                 (int) getpid(), crashPath);
#endif
        logLine(line);
    }

    struct sigaction action {};
    action.sa_sigaction = fatalSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP,
#ifdef SIGSYS
                           SIGSYS
#endif
    };
    for (int sig: signals) {
        sigaction(sig, &action, nullptr);
    }
}

#else

#include "crashlog.h"
void pplay::CrashLog::install(const std::string &) {}

#endif
