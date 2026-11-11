//
// Media information of the selected file, read without playing it (libavformat, in a
// thread), cached like the player one.
//

#ifndef PPLAY_MEDIA_PROBE_H
#define PPLAY_MEDIA_PROBE_H

#include <atomic>
#include <deque>
#include <map>
#include <set>
#include <string>
#include "cross2d/c2d.h"
#include "worker.h"
#include "media_file.h"

class MediaProbe {

public:

    MediaProbe() = default;

    ~MediaProbe();

    // probed once the selection settles; known files and the others skipped
    void request(const MediaFile &file);

    // each frame: true for a finished probe, its file with the information (again once
    // its TorrServer viewed state is restored)
    bool poll(MediaFile *file);

    // the cache was cleared
    void reset();

    // probed when nothing else is (the rows' progress needs their durations)
    void requestBackground(const MediaFile &file);

    void clearBackground();

    // a probe failed (its first time): "<name>: <error>", once; "" if none
    std::string takeError();

private:

    static int run(void *data);

    MediaFile wanted;
    std::deque<MediaFile> background;
    bool pending = false;
    c2d::C2DClock requestClock;
    std::set<std::string> tried;
    std::map<std::string, int> failures;

    pplay::Worker *thread = nullptr;
    MediaFile job;
    std::atomic<bool> probed{false};
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    bool delivered = false;
    std::string error;
    std::string failure;
};

#endif //PPLAY_MEDIA_PROBE_H
