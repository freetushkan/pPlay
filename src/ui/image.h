//
// Image files to textures: stb_image, then ffmpeg (webp, avif...).
//

#ifndef PPLAY_IMAGE_H
#define PPLAY_IMAGE_H

#include <string>
#include "cross2d/c2d.h"

namespace pplay::Image {

    // nullptr on errors; ffmpeg decoded images are scaled down to fit in maxSize
    c2d::Texture *load(const std::string &path, const c2d::Vector2i &maxSize, std::string *error = nullptr);

    // the average color of a loaded texture (RGBA8)
    c2d::Color average(c2d::Texture *texture);
}

#endif //PPLAY_IMAGE_H
