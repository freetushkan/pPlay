//
// Shared skin images.
//

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "skin.h"
#include "utility.h"

c2d::Texture *pplay::Skin::texture(const std::string &name) {
    static std::map<std::string, c2d::Texture *> textures;
    if (name.empty()) {
        return nullptr;
    }
    auto it = textures.find(name);
    if (it != textures.end()) {
        return it->second;
    }
    std::string path = Utility::getDataPath() + "skin/" + name;
    c2d::Texture *texture = Utility::fileExists(path) ? new c2d::C2DTexture(path) : nullptr;
    textures[name] = texture;
    return texture;
}

namespace {

    // one axis of an area resize: for each output pixel, its source pixels and their parts
    struct Span {
        int first;
        std::vector<float> weights;
    };

    std::vector<Span> spans(int from, int to) {
        std::vector<Span> out((size_t) to);
        float ratio = (float) from / (float) to;
        for (int i = 0; i < to; i++) {
            float start = (float) i * ratio, end = (float) (i + 1) * ratio;
            if (ratio < 1) {
                // bigger: the nearest two, linear
                float center = ((float) i + 0.5f) * ratio - 0.5f;
                int left = std::max(0, std::min(from - 1, (int) std::floor(center)));
                int right = std::min(from - 1, left + 1);
                float t = std::max(0.0f, std::min(1.0f, center - (float) left));
                out[i].first = left;
                out[i].weights = right == left ? std::vector<float>{1.0f} : std::vector<float>{1 - t, t};
                continue;
            }
            int first = (int) std::floor(start);
            int last = std::min(from - 1, (int) std::ceil(end) - 1);
            out[i].first = first;
            for (int s = first; s <= last; s++) {
                float part = std::min(end, (float) s + 1) - std::max(start, (float) s);
                out[i].weights.push_back(part / ratio);
            }
        }
        return out;
    }
}

c2d::Texture *pplay::Skin::icon(const std::string &name, int height, bool smooth) {
    static std::map<std::string, c2d::Texture *> textures;
    if (name.empty() || height <= 0) {
        return nullptr;
    }
    std::string key = name + "@" + std::to_string(height) + (smooth ? "s" : "");
    auto it = textures.find(key);
    if (it != textures.end()) {
        return it->second;
    }
    std::string path = Utility::getDataPath() + "skin/" + name;
    c2d::Texture *texture = nullptr;
    auto *source = Utility::fileExists(path) ? new c2d::C2DTexture(path) : nullptr;
    uint8_t *in = nullptr;
    int inPitch = 0;
    c2d::Vector2i size = source != nullptr ? source->getTextureSize() : c2d::Vector2i();
    if (source != nullptr && size.x > 0 && size.y > 0 && source->lock(&in, &inPitch) == 0 && in != nullptr) {
        int width = std::max(1, (int) std::lround((double) size.x * height / size.y));
        std::vector<Span> xs = spans(size.x, width), ys = spans(size.y, height);
        // premultiplied (the edges not darkened), the rows first
        std::vector<float> rows((size_t) size.y * width * 4, 0.0f);
        for (int y = 0; y < size.y; y++) {
            const uint8_t *row = in + (size_t) y * inPitch;
            for (int x = 0; x < width; x++) {
                float *o = &rows[((size_t) y * width + x) * 4];
                for (size_t k = 0; k < xs[x].weights.size(); k++) {
                    const uint8_t *p = row + (size_t) (xs[x].first + (int) k) * 4;
                    float w = xs[x].weights[k] * (float) p[3] / 255.0f;
                    o[0] += w * p[0];
                    o[1] += w * p[1];
                    o[2] += w * p[2];
                    o[3] += xs[x].weights[k] * p[3];
                }
            }
        }
        texture = new c2d::C2DTexture(c2d::Vector2i{width, height});
        uint8_t *out = nullptr;
        int outPitch = 0;
        if (texture->available && texture->lock(&out, &outPitch) == 0 && out != nullptr) {
            for (int y = 0; y < height; y++) {
                uint8_t *row = out + (size_t) y * outPitch;
                for (int x = 0; x < width; x++) {
                    float c[4] = {0, 0, 0, 0};
                    for (size_t k = 0; k < ys[y].weights.size(); k++) {
                        const float *p = &rows[((size_t) (ys[y].first + (int) k) * width + x) * 4];
                        for (int j = 0; j < 4; j++) {
                            c[j] += ys[y].weights[k] * p[j];
                        }
                    }
                    float alpha = std::min(255.0f, c[3]);
                    for (int j = 0; j < 3; j++) {
                        row[x * 4 + j] = (uint8_t) std::min(255.0f, alpha > 0 ? c[j] * 255.0f / alpha : 0.0f);
                    }
                    row[x * 4 + 3] = (uint8_t) std::lround(alpha);
                }
            }
            texture->unlock();
            texture->setFilter(smooth ? c2d::Texture::Filter::Linear : c2d::Texture::Filter::Point);
        } else {
            delete (texture);
            texture = nullptr;
        }
    }
    delete (source);
    textures[key] = texture;
    return texture;
}
