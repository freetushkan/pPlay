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
}

#endif //PPLAY_CLIP_RECT_H
