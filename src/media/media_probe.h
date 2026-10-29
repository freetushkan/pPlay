//
// Media information of the selected file, read without playing it (libavformat, in a
// thread), cached like the player one.
//

#ifndef PPLAY_MEDIA_PROBE_H
#define PPLAY_MEDIA_PROBE_H

#include <atomic>
#include <set>
#include <string>
#include "cross2d/c2d.h"
#include "media_file.h"

class MediaProbe {

public:

    MediaProbe() = default;

    ~MediaProbe();

    // probed once the selection settles; known files and the others skipped
    void request(const MediaFile &file);

    // each frame: true for a finished probe, its file with the information
    bool poll(MediaFile *file);

    // the cache was cleared
    void reset();

private:

    static int run(void *data);

    MediaFile wanted;
    bool pending = false;
    c2d::C2DClock requestClock;
    std::set<std::string> tried;

    c2d::C2DThread *thread = nullptr;
    MediaFile job;
    std::atomic<bool> done{false};
};

#endif //PPLAY_MEDIA_PROBE_H
