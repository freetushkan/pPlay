//
// Last playback date of the files (data directory "history").
//

#ifndef PPLAY_HISTORY_H
#define PPLAY_HISTORY_H

#include <ctime>
#include <string>

namespace pplay::History {

    // playback started now
    void touch(const std::string &key);

    // 0: never played
    time_t lastPlayed(const std::string &key);
}

#endif //PPLAY_HISTORY_H
