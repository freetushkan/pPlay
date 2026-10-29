//
// Posters of the details panel: downloaded in a thread to data/cache/posters, decoded
// a bit after the selection settles (fast scrolling).
//

#ifndef PPLAY_POSTER_LOADER_H
#define PPLAY_POSTER_LOADER_H

#include <atomic>
#include <string>
#include "cross2d/c2d.h"

class Main;

class PosterLoader {

public:

    explicit PosterLoader(Main *main);

    ~PosterLoader();

    // http(s) url, "embedded:<media url>" for the file cover, "" for none
    void request(const std::string &url);

    // each frame: true once the requested poster is done, with a texture (owned by
    // the caller) or an error
    bool poll(const c2d::Vector2i &maxSize, c2d::Texture **texture, std::string *error);

private:

    static int downloadThread(void *data);

    std::string cachePath(const std::string &url) const;

    Main *main;
    std::string wanted;
    std::string done;
    std::string failed;
    c2d::C2DClock requestClock;

    c2d::C2DThread *thread = nullptr;
    std::string downloadUrl;
    std::string downloadPath;
    std::string downloadError;
    std::atomic<bool> downloadDone{false};
    std::atomic<bool> downloadOk{false};
};

#endif //PPLAY_POSTER_LOADER_H
