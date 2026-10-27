//
// System clock check: compared with the "Date" of plain HTTP servers (no TLS,
// which needs a correct clock), in a thread at startup.
//

#ifndef PPLAY_NET_TIME_H
#define PPLAY_NET_TIME_H

#include <ctime>
#include <string>

namespace pplay::NetTime {

    // allowed difference (seconds)
#define CLOCK_MAX_DRIFT 3600

    void start();

    // waits for the check (at exit)
    void stop();

    // from the main thread, true once if the clock is off: the message to show
    bool takeWarning(std::string *message);
}

#endif //PPLAY_NET_TIME_H
