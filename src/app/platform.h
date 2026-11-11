//
// Platform specific startup and shutdown.
//

#ifndef PPLAY_PLATFORM_H
#define PPLAY_PLATFORM_H

#include <cstddef>
#include <string>
#include "cross2d/c2d.h"

class Main;

class PPLAYConfig;

namespace pplay::Platform {

    // stack size for SDL workers and explicit pthread workers (FFmpeg, covers)
    size_t threadStackSize();

    // first call; false: exit now (already running...)
    bool init();

    // the interface size asked to the renderer
    c2d::Vector2f resolution(PPLAYConfig *config);

    // "Switch", "PS4"... with the system version when known
    std::string name();

    // ui created
    void started(Main *main);

    // after each frame
    void frameDone(Main *main);

    // nothing to draw now (PS5: ready for a suspend)
    bool renderPaused();

    // each frame: no automatic sleep (playing, saving files)
    void keepAwake(bool on, bool playing);

    // the console put to sleep now (PS5: rest mode); false if it can't
    bool sleepNow();

    // the app can put the console in standby (PS5, Switch)
    bool hasStandby();

    // a key after a sleepNow() without a standby (Android: the screen back)
    void wake();

    // a link / file given by the system to open (Android VIEW): its url, "" if none
    std::string takeViewed(std::string *name);

    // the system media session (Android): the state shown, the commands given ("" if none)
    void mediaState(const std::string &title, int state, long positionMs, long durationMs);

    std::string takeMediaCommand();

    // the screen backlight 0..1 (Switch handheld), -1 if none
    float backlight();

    void setBacklight(float value);

    // after the main loop (does not return on PS4)
    void exit();
}

#endif //PPLAY_PLATFORM_H
