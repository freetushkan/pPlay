//
// Created by cpasjuste on 04/12/18.
//

#include "cross2d/c2d.h"

#include "main.h"
#include "highlight.h"

using namespace c2d;

Highlight::Highlight(const c2d::Vector2f &size, const CursorPosition &pos) : Rectangle(size) {
    hlDirection = pos == CursorPosition::Left ?
                        GradientRectangle::Direction::Right :
                        GradientRectangle::Direction::Left;
    gradientRectangle = new GradientRectangle({0, 0, size.x, size.y});
    setFillColor(COLOR_HIGHLIGHT);
    Highlight::add(gradientRectangle);

    cursor = new RectangleShape(Vector2f{6, size.y});
    setCursorColor(COLOR_ACCENT);
    if (pos == CursorPosition::Right) {
        cursor->move(size.x - 4, 0);
    }
    Highlight::add(cursor);

    tween = new TweenPosition(Highlight::getPosition(),
                              Highlight::getPosition(), (float) INPUT_DELAY / 2);
    tween->setState(TweenState::Stopped);
    Highlight::add(tween);
}

void Highlight::onDraw(c2d::Transform &transform, bool draw) {
    // setFillColor(hlFillColor);
    // setCursorColor(hlCursorColor);
    Rectangle::onDraw(transform, draw);
}

void Highlight::setAlpha(uint8_t alpha, bool recursive) {
    // gradientRectangle->setAlpha(alpha);
    c2d::Color _color = hlFillColor;
    _color.a = alpha;
    gradientRectangle->setColor(_color, Color::Transparent, hlDirection);
    if (recursive) cursor->setAlpha(alpha);
}

void Highlight::setFillColor(const c2d::Color &color) {
    // if (color != hlFillColor) {
        gradientRectangle->setColor(color, Color::Transparent, hlDirection);
        hlFillColor = color;
    // }
}

void Highlight::setCursorColor(const c2d::Color &color) {
    if (color != hlCursorColor) {
        cursor->setFillColor(color);
        hlCursorColor = color;
    }
}

void Highlight::tweenTo(const c2d::Vector2f &position) {
    if (tween != nullptr) {
        float seconds = (float) c2d_renderer->getInput()->getRepeatDelay() * 0.001f / 5;
        tween->setFromTo(getPosition(), position, seconds);
        tween->play(TweenDirection::Forward, true);
    } else {
        Transformable::setPosition(position);
    }
}
