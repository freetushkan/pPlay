//
// System on-screen keyboard (Switch swkbd, PS4/PS5 IME dialog).
//

#ifndef PPLAY_KEYBOARD_H
#define PPLAY_KEYBOARD_H

#include <string>

namespace pplay::Keyboard {

    // longest text the keyboard accepts (utf-16 units)
    size_t maxLength(bool multiline);

    // utf-16 length of an utf-8 string
    size_t length(const std::string &utf8);

    // blocking, false when cancelled or not available
    bool edit(const std::string &title, std::string &text, bool multiline = false);
}

#endif //PPLAY_KEYBOARD_H
