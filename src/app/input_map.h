//
// Input mapping: keyboard, media keys, remotes and other controllers, unknown
// buttons logging, auto repeat.
//

#ifndef PPLAY_INPUT_MAP_H
#define PPLAY_INPUT_MAP_H

#include "cross2d/c2d.h"

namespace pplay::InputMap {

    // media buttons, after the libcross2d ones
    enum Button : unsigned int {
        PlayPause = BIT(24),
        Stop = BIT(25),
        Next = BIT(26),
        Previous = BIT(27),
        FastForward = BIT(28),
        Rewind = BIT(29),
        VolumeUp = BIT(30),
        VolumeDown = BIT(31),
        Media = PlayPause | Stop | Next | Previous | FastForward | Rewind | VolumeUp | VolumeDown
    };

    // after the renderer creation
    void setup(c2d::Input *input);

    // keys pressed since the last call (media keys...)
    unsigned int takePending();

    // every frame: buttons of the other devices to handle now
    unsigned int update();

    // auto repeat (ms): first repeat, then three speeds by hold time
#define REPEAT_DELAY 500
#define REPEAT_LEVEL1 200        // until REPEAT_LEVEL2_AFTER ms of hold
#define REPEAT_LEVEL2_AFTER 1500
#define REPEAT_LEVEL2 100        // until REPEAT_LEVEL3_AFTER ms of hold
#define REPEAT_LEVEL3_AFTER 3000
#define REPEAT_LEVEL3 50

    int repeatDelay(int heldMs);

    std::string toString(unsigned int buttons);
}

#endif //PPLAY_INPUT_MAP_H
