//
// Created by cpasjuste on 04/12/18.
//

#ifndef PPLAY_HIGHLIGHT_H
#define PPLAY_HIGHLIGHT_H

class Highlight : public c2d::Rectangle {

public:

    enum class CursorPosition {
        Left,
        Right
    };

    explicit Highlight(const c2d::Vector2f &size, const CursorPosition &cursor = CursorPosition::Right);

    void setAlpha(uint8_t alpha, bool recursive = false) override;

    void setFillColor(const c2d::Color &color) override;

    void setCursorColor(const c2d::Color &color);

    void tweenTo(const c2d::Vector2f &position);

    void onDraw(c2d::Transform &transform, bool draw = true) override;

private:

    c2d::GradientRectangle *gradientRectangle;
    c2d::RectangleShape *cursor;
    c2d::TweenPosition *tween;
    c2d::Color hlFillColor;
    c2d::Color hlCursorColor;
    c2d::GradientRectangle::Direction hlDirection;

};

#endif //PPLAY_HIGHLIGHT_H
