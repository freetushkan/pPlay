//
// Input mapping: keyboard, media keys, remotes and other controllers, unknown
// buttons logging, auto repeat.
//

#ifndef PPLAY_INPUT_MAP_H
#define PPLAY_INPUT_MAP_H

#include "cross2d/c2d.h"

namespace pplay::InputMap {

    // PointerUp is emitted once when a mouse / touch contact is released.
    // It is deliberately separate from Touch, which remains active while dragging.
    enum Button : unsigned int {
        PointerUp = BIT(22),
        PointerDown = BIT(23),
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

    // the keys the app got (onInput); the pressed ones it did not get since the last call (a
    // tap during a long frame)
    void noteSeen(unsigned int buttons);
    unsigned int takeMissed();

    // every frame: buttons of the other devices to handle now
    unsigned int update();

    // mouse / finger pointer state; PointerUp is consumed once by the UI
    bool isPointerDown();
    bool takePointerDown();
    bool isPointerFinger();
    c2d::Vector2f pointerPosition();
    bool takePointerUp(c2d::Vector2f *position);

    // auto repeat (ms): first repeat, then three speeds by hold time
#define REPEAT_DELAY 500
#define REPEAT_LEVEL1 200 // until REPEAT_LEVEL2_AFTER ms of hold
#define REPEAT_LEVEL2_AFTER 1500
#define REPEAT_LEVEL2 100 // until REPEAT_LEVEL3_AFTER ms of hold
#define REPEAT_LEVEL3_AFTER 3000
#define REPEAT_LEVEL3 50

    int repeatDelay(int heldMs);

    // the keys being handled come from a held key (auto repeat), not a new press:
    // lists stop at their ends instead of wrapping
    void setRepeat(bool value);

    bool isRepeat();

    // the last update() buttons were a repeat
    bool updateRepeated();

    std::string toString(unsigned int buttons);

    // the button as printed on the platform pad (PlayStation symbols), for hints
    // Linux: F11 pressed since the last call
    bool takeFullscreenToggle();

    // skin glyph of a button (or of up + down, left + right), "" if none
    std::string buttonIcon(unsigned int buttons);

    std::string buttonName(unsigned int button);

    // a text is typed (Linux text box): no shortcut from the keyboard
    void setTextInput(bool active);
}

#endif //PPLAY_INPUT_MAP_H
