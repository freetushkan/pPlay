//
// Platform specific startup and shutdown.
//

#ifndef PPLAY_PLATFORM_H
#define PPLAY_PLATFORM_H

#include <cstddef>
#include <string>
#include "cross2d/c2d.h"

class Main;

namespace pplay::Platform {

    // stack size for SDL workers and explicit pthread workers (FFmpeg, covers)
    size_t threadStackSize();

    // first call; false: exit now (already running...)
    bool init();

    c2d::Vector2f resolution();

    // "Switch", "PS4"... with the system version when known
    std::string name();

    // ui created
    void started(Main *main);

    // after each frame
    void frameDone(Main *main);

    // nothing to draw now (PS5: ready for a suspend)
    bool renderPaused();

    // after the main loop (does not return on PS4)
    void exit();
}

#endif //PPLAY_PLATFORM_H
