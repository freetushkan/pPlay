//
// A skin icon made at the screen pixels: sharp at any interface scale.
//

#include <cmath>
#include "icon_sprite.h"
#include "skin.h"

namespace {
    // The scale used to rasterize icons, in screen pixels per interface unit.
    float screenScale() {
        float scale = c2d_renderer != nullptr ? c2d_renderer->getScale().y : 1.0f;
        return scale > 0 ? scale : 1.0f;
    }
}

bool pplay::IconSprite::setIcon(const std::string &n, float h, bool s) {
    if (name == n && height == h && smooth == s && !textureDirty) return true;
    name = n;
    height = h;
    if (name.empty() || height <= 0) {
        setSize(0, 0);
        textureDirty = true;
        scalePending = false;
        return false;
    }
    smooth = s;
    textureDirty = true;
    scalePending = false;
    return make();
}

bool pplay::IconSprite::make() {
    const float scale = screenScale();
    c2d::Texture *texture = name.empty() ? nullptr
                                         : Skin::icon(name, (int) std::lround(height * scale), smooth);
    // Do not mark this scale as rendered on failure. The update loop will retry
    // after the resize has settled instead of leaving the old icon indefinitely.
    if (texture == nullptr) {
        textureDirty = true;
        return false;
    }

    if (getTexture() != texture) {
        setTexture(texture, true);
    }

    // The same cached pixel texture can be used at two nearby scales. Its logical
    // size must still follow the current scale even when the texture pointer matches.
    // Set a pixel-space rect explicitly: do not inherit a stale rect or a
    // texture's logical rectangle (which may differ after scaling).
    const auto pixels = texture->getTextureSize();
    setTextureRect({0, 0, pixels.x, pixels.y});
    const c2d::IntRect rect = getTextureRect();
    if (rect.width <= 0 || rect.height <= 0) {
        textureDirty = true;
        return false;
    }
    setSize((float) rect.width / scale, (float) rect.height / scale);
    madeScale = scale;
    textureDirty = false;
    return true;
}

void pplay::IconSprite::onUpdate() {
    c2d::Sprite::onUpdate();
    if (name.empty()) {
        scalePending = false;
        return;
    }

    const float scale = screenScale();
    if (!textureDirty && std::fabs(scale - madeScale) < 0.001f) {
        scalePending = false;
        return;
    }

    // Each size change restarts the debounce. Texture creation happens only on the
    // update thread, after the scale has stayed unchanged for one second.
    if (!scalePending || std::fabs(scale - pendingScale) >= 0.001f) {
        pendingScale = scale;
        scalePending = true;
        scaleClock.restart();
        return;
    }

    if (scaleClock.getElapsedTime().asMilliseconds() >= 1000) {
        if (make()) {
            scalePending = false;
        } else {
            // Retry failures at a bounded rate; never spin or touch textures while drawing.
            scaleClock.restart();
        }
    }
}
