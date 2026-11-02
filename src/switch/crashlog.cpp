//
// Nintendo Switch exception diagnostics (libnx).
//

#if defined(__SWITCH__)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <switch.h>
#include <switch/arm/thread_context.h>
#include <switch/kernel/svc.h>
#include <unistd.h>

#include "crashlog.h"

extern "C" {
    // libnx calls this stack when a fatal user-mode exception occurs.
    alignas(16) uint8_t __nx_exception_stack[0x10000];
    uint64_t __nx_exception_stack_size = sizeof(__nx_exception_stack);
    uint32_t __nx_exception_ignoredebug = 1;
}

namespace {

    constexpr size_t MaxPath = 512;
    constexpr int MaxFrames = 32;
    constexpr uintptr_t MaxFrameGap = 1024 * 1024;

    int crashFd = -1;
    volatile uint32_t exceptionInProgress = 0;
    char crashPath[MaxPath] = {};

    void writeAll(const char *data, size_t size) {
        if (crashFd < 0 || data == nullptr) {
            return;
        }
        while (size > 0) {
            ssize_t written = write(crashFd, data, size);
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
        writeAll(line, size);
        svcOutputDebugString(line, size);
    }

    void logFrame(int index, uintptr_t address, const char *kind) {
        char line[128];
        snprintf(line, sizeof(line), "  #%02d %-3s 0x%016llx\n",
                 index, kind, (unsigned long long) address);
        logLine(line);
    }

    bool readFrame(uintptr_t fp, uintptr_t *previous, uintptr_t *lr) {
        if (fp == 0 || (fp & 0x0f) != 0) {
            return false;
        }

        MemoryInfo info {};
        uint32_t pageInfo = 0;
        if (R_FAILED(svcQueryMemory(&info, &pageInfo, fp))) {
            return false;
        }
        if ((info.perm & Perm_R) == 0 || fp < info.addr || fp > info.addr + info.size) {
            return false;
        }
        if (info.addr + info.size - fp < 2 * sizeof(uintptr_t)) {
            return false;
        }

        uintptr_t record[2] = {};
        memcpy(record, (const void *) fp, sizeof(record));
        *previous = record[0];
        *lr = record[1];
        if (*previous <= fp || *previous - fp > MaxFrameGap) {
            return false;
        }
        return true;
    }

} // namespace

extern "C" void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    if (exceptionInProgress != 0) {
        svcOutputDebugString("pPlay: nested exception while writing crash report\n",
                             sizeof("pPlay: nested exception while writing crash report\n") - 1);
        return;
    }
    exceptionInProgress = 1;

    if (ctx == nullptr) {
        logLine("\n========== pPlay Switch exception (no context) ==========\n");
        return;
    }

    char line[256];
    snprintf(line, sizeof(line),
             "\n========== pPlay Switch fatal exception ==========\n"
             "Error description: 0x%08x\n"
             "PC: 0x%016llx\nLR: 0x%016llx\nFP: 0x%016llx\nSP: 0x%016llx\n"
             "Fault address: 0x%016llx\nESR: 0x%08x\nPSTATE: 0x%08x\n",
             ctx->error_desc,
             (unsigned long long) ctx->pc.x,
             (unsigned long long) ctx->lr.x,
             (unsigned long long) ctx->fp.x,
             (unsigned long long) ctx->sp.x,
             (unsigned long long) ctx->far.x,
             ctx->esr, ctx->pstate);
    logLine(line);

    MemoryInfo pcInfo {};
    uint32_t pcPageInfo = 0;
    if (R_SUCCEEDED(svcQueryMemory(&pcInfo, &pcPageInfo, ctx->pc.x))) {
        const uintptr_t pc = (uintptr_t) ctx->pc.x;
        snprintf(line, sizeof(line),
                 "PC mapping: base=0x%016llx size=0x%llx offset=0x%llx perm=0x%x\n",
                 (unsigned long long) pcInfo.addr,
                 (unsigned long long) pcInfo.size,
                 (unsigned long long) (pc - pcInfo.addr), pcInfo.perm);
        logLine(line);
    }
    for (int reg = 0; reg < 29; reg += 2) {
        const unsigned long long first = (unsigned long long) ctx->cpu_gprs[reg].x;
        const unsigned long long second = reg + 1 < 29
                                                  ? (unsigned long long) ctx->cpu_gprs[reg + 1].x
                                                  : 0;
        snprintf(line, sizeof(line), "x%-2d=0x%016llx x%-2d=0x%016llx\n",
                 reg, first, reg + 1, second);
        logLine(line);
    }

    logFrame(0, (uintptr_t) ctx->pc.x, "PC");
    uintptr_t fp = (uintptr_t) ctx->fp.x;
    uintptr_t previous = 0;
    uintptr_t lr = (uintptr_t) ctx->lr.x;
    logFrame(1, lr, "LR");

    for (int i = 2; i < MaxFrames; i++) {
        if (!readFrame(fp, &previous, &lr)) {
            break;
        }
        logFrame(i, lr, "LR");
        fp = previous;
    }

    snprintf(line, sizeof(line),
             "Crash report: %s\n========== end crash report ==========\n",
             crashPath[0] ? crashPath : "(unavailable)");
    logLine(line);
    if (crashFd >= 0) {
        fsync(crashFd);
    }
    // libnx's exception trampoline issues svcBreak(Panic) after this handler returns.
}

void pplay::CrashLog::install(const std::string &logPath) {
    if (crashFd >= 0) {
        return;
    }
    const size_t copySize = std::min(logPath.size(), sizeof(crashPath) - 1);
    memcpy(crashPath, logPath.data(), copySize);
    crashPath[copySize] = '\0';

    crashFd = open(crashPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    char line[768];
    if (crashFd >= 0) {
#ifdef APP_VERSION_FULL
        snprintf(line, sizeof(line),
                 "\n========== pPlay Switch session ==========\n"
                 "Build: %s\nCrash report: %s\n==========================================\n",
                 APP_VERSION_FULL, crashPath);
#else
        snprintf(line, sizeof(line),
                 "\n========== pPlay Switch session ==========\n"
                 "Crash report: %s\n==========================================\n",
                 crashPath);
#endif
        logLine(line);
    } else {
        snprintf(line, sizeof(line), "pPlay CrashLog: cannot open %s\n", crashPath);
        svcOutputDebugString(line, strlen(line));
    }
}

#else

#include "crashlog.h"
void pplay::CrashLog::install(const std::string &) {}

#endif
