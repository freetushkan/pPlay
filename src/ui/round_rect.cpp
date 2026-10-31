//
// Rounded rectangle with smooth edges.
//

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "round_rect.h"

using namespace c2d;

RoundRect::RoundRect(const Vector2f &size, float r) : RectangleShape(size), radius(r) {
    build();
}

// textures shared by size and kept (the menus are made again for each file)
RoundRect::~RoundRect() = default;

void RoundRect::setSize(const Vector2f &size) {
    RectangleShape::setSize(size);
    build();
}

void RoundRect::setSize(float width, float height) {
    setSize({width, height});
}

void RoundRect::build() {
    static std::map<std::tuple<int, int, int>, Texture *> cache;
    Vector2i size = {std::max(1, (int) std::lround(getSize().x)), std::max(1, (int) std::lround(getSize().y))};
    if (size == built && texture != nullptr) {
        return;
    }
    float r = radius < 0 ? (float) size.y / 2 : std::min(radius, (float) std::min(size.x, size.y) / 2);
    auto key = std::make_tuple(size.x, size.y, (int) std::lround(r * 4));
    auto found = cache.find(key);
    if (found != cache.end()) {
        setTexture(found->second, true);
        texture = found->second;
        built = size;
        return;
    }
    auto *tex = new C2DTexture(size, Texture::Format::RGBA8);
    uint8_t *pixels = nullptr;
    int pitch = 0;
    if (!tex->available || tex->lock(&pixels, &pitch) != 0 || pixels == nullptr) {
        delete (tex);
        return;
    }
    for (int y = 0; y < size.y; y++) {
        uint8_t *row = pixels + (size_t) y * pitch;
        for (int x = 0; x < size.x; x++) {
            // distance out of the rounded border, from the pixel center
            float px = (float) x + 0.5f, py = (float) y + 0.5f;
            float dx = std::max(std::max(r - px, px - ((float) size.x - r)), 0.0f);
            float dy = std::max(std::max(r - py, py - ((float) size.y - r)), 0.0f);
            float out = std::sqrt(dx * dx + dy * dy) - r;
            float a = std::min(1.0f, std::max(0.0f, 0.5f - out));
            row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
            row[x * 4 + 3] = (uint8_t) std::lround(a * 255);
        }
    }
    tex->unlock();
    cache[key] = tex;
    setTexture(tex, true);
    texture = tex;
    built = size;
}
