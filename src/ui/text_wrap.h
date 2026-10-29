//
// Word wrap measured with the font: libcross2d wraps on spaces only and cuts long
// words (file names) at the box edge.
//

#ifndef PPLAY_TEXT_WRAP_H
#define PPLAY_TEXT_WRAP_H

#include <string>
#include "cross2d/c2d.h"

namespace pplay::TextWrap {

    // text width on one line, in pixels
    float width(c2d::Font *font, unsigned int size, const std::string &text);

    // spaces made non breaking for wrap(): kept on one line
    std::string noBreak(const std::string &text);

    // lines no wider than maxWidth: broken at spaces, long words after "." "_" "-"...
    std::string wrap(c2d::Font *font, unsigned int size, const std::string &text, float maxWidth);
}

#endif //PPLAY_TEXT_WRAP_H
