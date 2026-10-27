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

    // fade multiplier, keeps the base colors
    void setAlpha(uint8_t alpha, bool recursive = false) override;

    uint8_t getAlpha() override;

    void setFillColor(const c2d::Color &color) override;

    // base fill color with an explicit base alpha
    void setFillColor(const c2d::Color &color, uint8_t alpha);

    void setCursorColor(const c2d::Color &color);

    void tweenTo(const c2d::Vector2f &position);

    void setWidth(float width);

private:

    void applyColors();

    c2d::GradientRectangle *gradientRectangle;
    c2d::RectangleShape *cursor;
    c2d::TweenPosition *tween;
    c2d::Color hlFillColor;
    c2d::Color hlCursorColor;
    uint8_t hlAlpha = 255;
    c2d::GradientRectangle::Direction hlDirection;
    CursorPosition cursorPosition;

};

#endif //PPLAY_HIGHLIGHT_H
