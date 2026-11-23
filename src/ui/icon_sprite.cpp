//
// A skin icon made at the screen pixels: sharp at any interface scale.
//

#include <cmath>
#include "icon_sprite.h"
#include "skin.h"

namespace {
    // the screen pixels of an interface unit (the interface scaled to the window or the drawable)
    float screenScale() {
        float scale = c2d_renderer != nullptr ? c2d_renderer->getScale().y : 1.0f;
        return scale > 0 ? scale : 1.0f;
    }
}

bool pplay::IconSprite::setIcon(const std::string &n, float h, bool s) {
    name = n;
    height = h;
    smooth = s;
    return make();
}

bool pplay::IconSprite::make() {
    madeScale = screenScale();
    c2d::Texture *texture = name.empty() ? nullptr
                                         : Skin::icon(name, (int) std::lround(height * madeScale), smooth);
    if (texture == nullptr) {
        return false;
    }
    if (getTexture() != texture) {
        setTexture(texture, true);
        // drawn at its pixels: its size in interface units
        c2d::IntRect rect = getTextureRect();
        setSize((float) rect.width / madeScale, (float) rect.height / madeScale);
    }
    return true;
}

void pplay::IconSprite::onDraw(c2d::Transform &transform, bool draw) {
    if (draw && !name.empty() && screenScale() != madeScale) {
        make();
    }
    c2d::Sprite::onDraw(transform, draw);
}
