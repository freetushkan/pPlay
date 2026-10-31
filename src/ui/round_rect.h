//
// Rounded rectangle (or circle) with smooth edges: a white texture made for its size,
// the color from setFillColor().
//

#ifndef PPLAY_ROUND_RECT_H
#define PPLAY_ROUND_RECT_H

#include "cross2d/c2d.h"

class RoundRect : public c2d::RectangleShape {

public:

    // radius < 0: half the height (pill, circle)
    explicit RoundRect(const c2d::Vector2f &size, float radius = -1);

    ~RoundRect() override;

    void setSize(const c2d::Vector2f &size) override;

    void setSize(float width, float height) override;

private:

    void build();

    c2d::Texture *texture = nullptr;
    c2d::Vector2i built;
    float radius;
};

#endif //PPLAY_ROUND_RECT_H
