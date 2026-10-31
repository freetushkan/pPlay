//
// Created by cpasjuste on 04/12/18.
//

#include "cross2d/c2d.h"

#include "main.h"
#include "highlight.h"

#include <algorithm>
#include <cmath>

using namespace c2d;

// setAlpha() (parents fade) multiplies the base colors alpha instead of replacing it.
static uint8_t scaleAlpha(uint8_t base, uint8_t factor) {
    return (uint8_t) (((unsigned int) base * (unsigned int) factor + 127) / 255);
}

Highlight::Highlight(const c2d::Vector2f &size, const CursorPosition &pos) : Rectangle(size) {
    hlDirection =
            pos == CursorPosition::Left ? GradientRectangle::Direction::Right : GradientRectangle::Direction::Left;
    gradientRectangle = new GradientRectangle({0, 0, size.x, size.y});
    Highlight::add(gradientRectangle);

    cursorPosition = pos;
    float cursorWidth = 6;
    cursor = new RectangleShape(Vector2f{cursorWidth, size.y});
    if (pos == CursorPosition::Right) {
        cursor->move(size.x - cursorWidth, 0);
    }
    Highlight::add(cursor);

    hlFillColor = COLOR_HIGHLIGHT;
    hlCursorColor = COLOR_ACCENT;
    applyColors();

    tween = new TweenPosition(Highlight::getPosition(), Highlight::getPosition(), (float) INPUT_DELAY / 2);
    tween->setState(TweenState::Stopped);
    Highlight::add(tween);
}

void Highlight::applyColors() {
    c2d::Color fill = hlFillColor;
    fill.a = scaleAlpha(hlFillColor.a, hlAlpha);
    // a soft end, not a fade out: the whole row reads as selected
    c2d::Color end = fill;
    end.a = (uint8_t) (fill.a / 4);
    gradientRectangle->setColor(fill, end, hlDirection);

    c2d::Color cur = hlCursorColor;
    cur.a = scaleAlpha(hlCursorColor.a, hlAlpha);
    cursor->setFillColor(cur);
}

void Highlight::setAlpha(uint8_t alpha, bool /*recursive*/) {
    if (alpha != hlAlpha) {
        hlAlpha = alpha;
        applyColors();
    }
}

uint8_t Highlight::getAlpha() {
    return hlAlpha;
}

void Highlight::setFillColor(const c2d::Color &color) {
    if (color != hlFillColor) {
        hlFillColor = color;
        applyColors();
    }
}

void Highlight::setFillColor(const c2d::Color &color, uint8_t alpha) {
    c2d::Color c = color;
    c.a = alpha;
    setFillColor(c);
}

void Highlight::setCursorColor(const c2d::Color &color) {
    if (color != hlCursorColor) {
        hlCursorColor = color;
        applyColors();
    }
}

void Highlight::resize(const c2d::Vector2f &size) {
    setSize(size);
    gradientRectangle->setSize(size);
    cursor->setSize(cursor->getSize().x, size.y);
    if (cursorPosition == CursorPosition::Right) {
        cursor->setPosition(size.x - cursor->getSize().x, 0);
    }
}

c2d::Color Highlight::seenThrough(const c2d::Color &background, float x, const CursorPosition &cursor) {
    // as applyColors(): the full alpha on the cursor side, a quarter on the other
    float strong = COLOR_HIGHLIGHT.a, weak = (float) (COLOR_HIGHLIGHT.a / 4);
    float f = std::min(1.0f, std::max(0.0f, cursor == CursorPosition::Right ? x : 1 - x));
    float a = (weak + (strong - weak) * f) / 255;
    auto mix = [a](uint8_t from, uint8_t to) { return (uint8_t) std::lround(from + (to - from) * a); };
    return {mix(background.r, COLOR_HIGHLIGHT.r), mix(background.g, COLOR_HIGHLIGHT.g),
            mix(background.b, COLOR_HIGHLIGHT.b), 255};
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
