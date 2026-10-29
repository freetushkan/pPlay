//
// Linux window: resizable (the ui scaled, letterboxed), F11 fullscreen.
//

#ifndef PPLAY_DESKTOP_WINDOW_H
#define PPLAY_DESKTOP_WINDOW_H

class Main;

namespace pplay::DesktopWindow {

    // once the ui is created
    void setup(Main *main);

    // each frame: size changes, fullscreen toggle
    void update(Main *main);
}

#endif //PPLAY_DESKTOP_WINDOW_H
