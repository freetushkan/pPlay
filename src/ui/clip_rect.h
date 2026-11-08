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

    // the right end of a clipped text faded to the background: a gradient over it, in two
    // bands (a highlight sliding over the row: what is behind each part)
    class FadeEdge : public c2d::Rectangle {

    public:

        explicit FadeEdge(const c2d::FloatRect &rect);

        void setBackground(const c2d::Color &color);

        // upper: the background of the top part (split: its share of the height), lower: the rest
        void setBackgrounds(const c2d::Color &upper, const c2d::Color &lower, float split);

    private:

        static void apply(c2d::GradientRectangle *part, const c2d::Color &color);

        c2d::GradientRectangle *top;
        c2d::GradientRectangle *bottom;
        c2d::Color upperColor;
        c2d::Color lowerColor;
        float splitAt = -1;
    };
}

#endif //PPLAY_CLIP_RECT_H
