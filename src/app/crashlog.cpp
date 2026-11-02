//
// Linux fatal-signal diagnostics. Keep the handler limited to low-level writes and
// backtrace_symbols_fd; do not call the application logger from signal context.
//

#if defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <execinfo.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#include "crashlog.h"
#include "utility.h"

namespace {

    constexpr size_t CrashStackSize = 64 * 1024;
    constexpr int MaxFrames = 64;

    int crashFd = -1;
    uintptr_t executableBase = 0;
    volatile sig_atomic_t handlingCrash = 0;

    struct SignalStack {
        alignas(16) unsigned char bytes[CrashStackSize];
    };

    thread_local SignalStack signalStack;

    void writeText(int fd, const char *text) {
        if (fd < 0 || text == nullptr) {
            return;
        }
        size_t length = 0;
        while (text[length] != '\0') {
            length++;
        }
        while (length > 0) {
            ssize_t written = write(fd, text, length);
            if (written <= 0) {
                return;
            }
            text += written;
            length -= (size_t) written;
        }
    }

    void writeUnsigned(int fd, uint64_t value, unsigned int base) {
        char buffer[32];
        size_t length = 0;
        do {
            unsigned int digit = (unsigned int) (value % base);
            buffer[length++] = (char) (digit < 10 ? '0' + digit : 'a' + digit - 10);
            value /= base;
        } while (value != 0 && length < sizeof(buffer));
        while (length > 0) {
            char c = buffer[--length];
            write(fd, &c, 1);
        }
    }

    void writeHex(int fd, uintptr_t value) {
        writeText(fd, "0x");
        writeUnsigned(fd, (uint64_t) value, 16);
    }

    const char *signalName(int signal) {
        switch (signal) {
            case SIGSEGV:
                return "SIGSEGV";
            case SIGBUS:
                return "SIGBUS";
            case SIGILL:
                return "SIGILL";
            case SIGFPE:
                return "SIGFPE";
            case SIGABRT:
                return "SIGABRT";
            case SIGTRAP:
                return "SIGTRAP";
            case SIGSYS:
                return "SIGSYS";
            default:
                return "UNKNOWN";
        }
    }

    void fatalSignal(int signal, siginfo_t *info, void *context) {
        if (handlingCrash) {
            _exit(128 + signal);
        }
        handlingCrash = 1;

        const int fd = crashFd >= 0 ? crashFd : STDERR_FILENO;
        writeText(fd, "\n========== pPlay fatal signal ==========\n");
        writeText(fd, "Signal: ");
        writeText(fd, signalName(signal));
        writeText(fd, " (");
        writeUnsigned(fd, (uint64_t) signal, 10);
        writeText(fd, ")\nPID: ");
        writeUnsigned(fd, (uint64_t) getpid(), 10);
        writeText(fd, "\nTID: ");
        writeUnsigned(fd, (uint64_t) syscall(SYS_gettid), 10);
        writeText(fd, "\nsi_code: ");
        writeUnsigned(fd, info != nullptr ? (uint64_t) (int64_t) info->si_code : 0, 10);
        writeText(fd, "\nFault address: ");
        writeHex(fd, info != nullptr ? (uintptr_t) info->si_addr : 0);
        writeText(fd, "\n");

#if defined(__x86_64__) && defined(REG_RIP)
        if (context != nullptr) {
            const auto *uc = (const ucontext_t *) context;
            writeText(fd, "Instruction pointer: ");
            writeHex(fd, (uintptr_t) uc->uc_mcontext.gregs[REG_RIP]);
            writeText(fd, "\n");
        }
#elif defined(__i386__) && defined(REG_EIP)
        if (context != nullptr) {
            const auto *uc = (const ucontext_t *) context;
            writeText(fd, "Instruction pointer: ");
            writeHex(fd, (uintptr_t) uc->uc_mcontext.gregs[REG_EIP]);
            writeText(fd, "\n");
        }
#endif

        void *frames[MaxFrames] = {};
        int count = backtrace(frames, MaxFrames);
        writeText(fd, "Backtrace addresses:\n");
        for (int i = 0; i < count; i++) {
            writeText(fd, "  #");
            writeUnsigned(fd, (uint64_t) i, 10);
            writeText(fd, " ");
            writeHex(fd, (uintptr_t) frames[i]);
            writeText(fd, "\n");
        }
        writeText(fd, "Backtrace symbols:\n");
        if (count > 0) {
            backtrace_symbols_fd(frames, count, fd);
        }
        writeText(fd, "========== end pPlay crash report ==========\n");

        // Preserve the normal fatal-signal exit (and core-dump behavior configured by the OS).
        struct sigaction defaultAction {};
        defaultAction.sa_handler = SIG_DFL;
        sigemptyset(&defaultAction.sa_mask);
        sigaction(signal, &defaultAction, nullptr);

        sigset_t unblocked;
        sigemptyset(&unblocked);
        sigaddset(&unblocked, signal);
        sigprocmask(SIG_UNBLOCK, &unblocked, nullptr);
        raise(signal);
        _exit(128 + signal);
    }

} // namespace

void pplay::CrashLog::registerCurrentThread() {
    stack_t stack {};
    stack.ss_sp = signalStack.bytes;
    stack.ss_size = sizeof(signalStack.bytes);
    stack.ss_flags = 0;
    sigaltstack(&stack, nullptr);
}

void pplay::CrashLog::install() {
    if (crashFd >= 0) {
        return;
    }

    const std::string path = Utility::getDataPath() + "crash.log";
    crashFd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (crashFd < 0) {
        Utility::log(Utility::LogLevel::Error, "CrashLog: cannot open " + path);
        return;
    }

    // Resolve libgcc/unwinder lazily now, not for the first time from a signal handler.
    void *warmFrames[4] = {};
    int warmCount = backtrace(warmFrames, 4);
    int nullFd = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (nullFd >= 0) {
        if (warmCount > 0) {
            backtrace_symbols_fd(warmFrames, warmCount, nullFd);
        }
        close(nullFd);
    }

    registerCurrentThread();

    struct sigaction action {};
    action.sa_sigaction = fatalSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS};
    for (int signal: signals) {
        sigaction(signal, &action, nullptr);
    }

    writeText(crashFd, "\n========== pPlay Linux session ==========\n");
#ifdef APP_VERSION_FULL
    writeText(crashFd, "Build: " APP_VERSION_FULL "\n");
#endif
    writeText(crashFd, "Executable: ");
    char executable[4096];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length > 0) {
        executable[length] = '\0';
        writeText(crashFd, executable);
    } else {
        writeText(crashFd, "(unknown)");
    }
    writeText(crashFd, "\nAppImage: ");
    const char *appImage = getenv("APPIMAGE");
    writeText(crashFd, appImage != nullptr ? appImage : "(not running as AppImage)");
    writeText(crashFd, "\nCrash report file: ");
    writeText(crashFd, path.c_str());
    writeText(crashFd, "\n========================================\n");
}

#else

#include "crashlog.h"

void pplay::CrashLog::registerCurrentThread() {}
void pplay::CrashLog::install() {}

#endif
