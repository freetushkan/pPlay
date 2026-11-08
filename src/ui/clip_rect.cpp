//
// Container clipping its children (see clip_rect.h).
//

#include <algorithm>
#include <cmath>

#include "clip_rect.h"

using namespace c2d;

void pplay::ClipRect::onDraw(Transform &transform, bool draw) {
    // drawn while a fade out runs too (already hidden then): clipped all the same
    if (!draw) {
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

pplay::FadeEdge::FadeEdge(const FloatRect &rect) : Rectangle(rect) {
    top = new GradientRectangle({0, 0, rect.width, rect.height});
    bottom = new GradientRectangle({0, rect.height, rect.width, 0});
    FadeEdge::add(top);
    FadeEdge::add(bottom);
    upperColor = lowerColor = Color::Transparent;
    setBackground(Color::Black);
}

void pplay::FadeEdge::setBackground(const Color &color) {
    setBackgrounds(color, color, 1);
}

void pplay::FadeEdge::setBackgrounds(const Color &upper, const Color &lower, float split) {
    split = std::min(1.0f, std::max(0.0f, split));
    if (upper == lower) {
        split = 1;
    }
    if (upper != upperColor) {
        upperColor = upper;
        apply(top, upper);
    }
    if (lower != lowerColor) {
        lowerColor = lower;
        apply(bottom, lower);
    }
    if (split != splitAt) {
        splitAt = split;
        // whole pixels: no seam between the parts
        float height = getSize().y, at = std::round(height * split);
        top->setSize(getSize().x, at);
        bottom->setPosition(0, at);
        bottom->setSize(getSize().x, height - at);
    }
}

void pplay::FadeEdge::apply(GradientRectangle *part, const Color &color) {
    Color transparent = color;
    transparent.a = 0;
    part->setColor(transparent, color, GradientRectangle::Direction::Right);
}
