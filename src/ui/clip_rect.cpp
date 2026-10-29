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

    glEnable(GL_SCISSOR_TEST);
    glScissor(left, bottom, std::max(w, 0), std::max(h, 0));
    Rectangle::onDraw(transform, draw);
    glDisable(GL_SCISSOR_TEST);
}

pplay::FadeEdge::FadeEdge(const FloatRect &rect) : GradientRectangle(rect) {
    setBackground(Color::Black);
}

void pplay::FadeEdge::setBackground(const Color &color) {
    if (color == background) {
        return;
    }
    background = color;
    Color transparent = color;
    transparent.a = 0;
    setColor(transparent, color, Direction::Right);
}
