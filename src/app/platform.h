//
// Platform specific startup and shutdown.
//

#ifndef PPLAY_PLATFORM_H
#define PPLAY_PLATFORM_H

#include "cross2d/c2d.h"

class Main;

namespace pplay::Platform {

    // first call; false: exit now (already running...)
    bool init();

    c2d::Vector2f resolution();

    // ui created
    void started(Main *main);

    // after each frame
    void frameDone(Main *main);

    // after the main loop (does not return on PS4)
    void exit();
}

#endif //PPLAY_PLATFORM_H
