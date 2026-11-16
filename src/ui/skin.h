//
// Skin images shared by many objects (icons, key glyphs).
//

#ifndef PPLAY_SKIN_H
#define PPLAY_SKIN_H

#include <string>
#include "cross2d/c2d.h"

namespace pplay::Skin {

    // skin/<name>, loaded once and kept until exit; nullptr if missing
    c2d::Texture *texture(const std::string &name);

    // skin/<name> made this many pixels high (its pixels averaged): drawn at its size, sharp at
    // any resolution; smooth: filtered (turned, scaled), else as is. Kept until exit.
    c2d::Texture *icon(const std::string &name, int height, bool smooth = false);
}

#endif //PPLAY_SKIN_H
