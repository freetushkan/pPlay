//
// Container clipping its children (see clip_rect.h).
//

#include <algorithm>
#include <cmath>

#include "clip_rect.h"

using namespace c2d;

void pplay::ClipRect::onDraw(Transform &transform, bool draw) {
    if (!draw || !isVisible()) {
        Rectangle::onDraw(transform, draw);
        return;
    }

    // the renderer projection maps its coordinates to drawable pixels, y down
    FloatRect rect = (transform * getTransform()).transformRect({0, 0, getSize().x, getSize().y});
    int width = 0, height = 0;
    SDL_GL_GetDrawableSize(((SDL2Renderer *) c2d_renderer)->getWindow(), &width, &height);
    auto left = (GLint) std::floor(rect.left);
    auto bottom = (GLint) std::floor((float) height - (rect.top + rect.height));
    auto w = (GLsizei) std::ceil(rect.width);
    auto h = (GLsizei) std::ceil(rect.height);

    // inside another clip: both
    GLboolean outer = glIsEnabled(GL_SCISSOR_TEST);
    GLint box[4] = {0, 0, 0, 0};
    if (outer) {
        glGetIntegerv(GL_SCISSOR_BOX, box);
        GLint right = std::min(left + w, box[0] + box[2]), top = std::min(bottom + h, box[1] + box[3]);
        left = std::max(left, box[0]);
        bottom = std::max(bottom, box[1]);
        w = right - left;
        h = top - bottom;
    }

    glEnable(GL_SCISSOR_TEST);
    glScissor(left, bottom, std::max(w, 0), std::max(h, 0));
    Rectangle::onDraw(transform, draw);
    if (outer) {
        glScissor(box[0], box[1], box[2], box[3]);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

pplay::FadeEdge::FadeEdge(const FloatRect &rect) : GradientRectangle(rect) {
    setBackground(Color::Black);
}

void pplay::FadeEdge::setBackground(const Color &color) {
    if (color == background) {
        return;
    }
    background = color;
    apply();
}

void pplay::FadeEdge::setAlpha(uint8_t value, bool recursive) {
    if (value != alpha) {
        alpha = value;
        apply();
    }
    if (recursive) {
        C2DObject::setAlpha(value, recursive);
    }
}

void pplay::FadeEdge::apply() {
    Color transparent = background, opaque = background;
    transparent.a = 0;
    opaque.a = (uint8_t) (background.a * alpha / 255);
    setColor(transparent, opaque, Direction::Right);
}
