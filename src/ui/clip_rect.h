//
// Container drawing its children inside its rectangle only (GL scissor test):
// libcross2d has no overflow clipping.
//

#ifndef PPLAY_CLIP_RECT_H
#define PPLAY_CLIP_RECT_H

#include "cross2d/c2d.h"

namespace pplay {

    class ClipRect : public c2d::Rectangle {

    public:

        explicit ClipRect(const c2d::FloatRect &rect) : c2d::Rectangle(rect) {}

        void onDraw(c2d::Transform &transform, bool draw = true) override;
    };

    // the right end of a clipped text faded to the background: a gradient over it
    class FadeEdge : public c2d::GradientRectangle {

    public:

        explicit FadeEdge(const c2d::FloatRect &rect);

        void setBackground(const c2d::Color &color);

        // the parents fade (TweenAlpha) the opaque end only, the gradient stays
        void setAlpha(uint8_t alpha, bool recursive = false) override;

        uint8_t getAlpha() override { return alpha; }

    private:

        void apply();

        c2d::Color background;
        uint8_t alpha = 255;
    };
}

#endif //PPLAY_CLIP_RECT_H
