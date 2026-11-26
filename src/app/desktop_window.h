//
// Linux window: resizable (the ui scaled, letterboxed), F11 fullscreen.
//

#ifndef PPLAY_DESKTOP_WINDOW_H
#define PPLAY_DESKTOP_WINDOW_H

#include "cross2d/c2d.h"

class Main;

namespace pplay::DesktopWindow {

    // once the ui is created
    void setup(Main *main);

    // each frame: size changes, fullscreen toggle
    void update(Main *main);

    // Convert SDL drawable coordinates to the renderer's logical UI coordinates.
    c2d::Vector2f mapPointer(const c2d::Vector2f &position);
}

#endif //PPLAY_DESKTOP_WINDOW_H
