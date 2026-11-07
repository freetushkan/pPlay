//
// Platform-specific fatal crash diagnostics.
// 

#ifndef PPLAY_CRASHLOG_H
#define PPLAY_CRASHLOG_H

#include <string>

namespace pplay::CrashLog {

    // Linux: installs process-wide fatal signal handlers and writes to crash.log.
    void install();

    // PS5 / Switch / Android: installs the native crash handler and writes to this path.
    void install(const std::string &logPath);

    // Linux: registers an alternate signal stack for the calling thread.
    void registerCurrentThread();

    // Android: the main loop alive (a stuck one: its place written), not watched while idle
    // (in the background)
    void heartbeat();

    void setIdle(bool idle);
}

#endif //PPLAY_CRASHLOG_H
