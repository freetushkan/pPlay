//
// Platform-specific fatal crash diagnostics.
// 

#ifndef PPLAY_CRASHLOG_H
#define PPLAY_CRASHLOG_H

#include <string>

namespace pplay::CrashLog {

    // Linux: installs process-wide fatal signal handlers and writes to crash.log.
    void install();

    // PS5 / Switch: installs the native console crash handler and writes to this path.
    void install(const std::string &logPath);

    // Linux: registers an alternate signal stack for the calling thread.
    void registerCurrentThread();
}

#endif //PPLAY_CRASHLOG_H
