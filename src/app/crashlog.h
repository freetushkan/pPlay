//
// Linux fatal-signal diagnostics.
// 

#ifndef PPLAY_CRASHLOG_H
#define PPLAY_CRASHLOG_H

namespace pplay::CrashLog {

    // Installs process-wide fatal signal handlers and writes to <data-dir>/crash.log.
    void install();

    // Registers an alternate signal stack for the calling thread.
    void registerCurrentThread();
}

#endif //PPLAY_CRASHLOG_H
