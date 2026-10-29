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
}

#endif //PPLAY_SKIN_H
