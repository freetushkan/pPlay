//
// Created by cpasjuste on 20/10/18.
//

#ifndef PPLAY_MENU_BUTTON_H
#define PPLAY_MENU_BUTTON_H

#include <string>
#include "cross2d/c2d.h"
#include "clip_rect.h"
#include "icon_sprite.h"

class RoundRect;

class Main;

class MenuItem {

public:

    enum class Position {
        Top,
        Bottom
    };

    MenuItem() = default;

    MenuItem(const std::string &name, const std::string &icon, const Position &position, int id = 0,
             bool selectable = true, const std::string &data_str = "", bool enabled = true)
        : name(name), icon(icon), position(position), id(id), selectable(selectable), data_str(data_str),
          enabled(enabled) {}

    // label, or the translated name (the menu key)
    std::string text() const;

    std::string name;
    std::string label;
    std::string icon;
    Position position = Position::Top;
    int id = 0;
    bool selectable = true;
    std::string data_str;
    bool enabled = true;
    // on the right: a value (changed with left / right), a slider (0..1, < 0: none);
    // the value in the accent color when not the default one
    std::string value;
    bool valueAccent = false;
    // a thin line above it (a group apart)
    bool line = false;
    float slider = -1;
    // opens a submenu ("›" without a value)
    bool opens = false;
    // a column before the name, as wide as badgeChars digits (the same in all the items):
    // the badge (a number), or the icon when there is one
    std::string badge;
    int badgeChars = 0;
};

class MenuButton : public c2d::Rectangle {

public:

    MenuButton(Main *main, const MenuItem &item, const c2d::FloatRect &rect, unsigned int fontSize);

    // after item.name / item.label changes
    void refreshText();

    // after item.value / valueAccent / slider changes
    void refreshValue();

    // another skin icon (a network type changed)
    void setIcon(const std::string &name);

    // the line above it shown or not
    void setLine(bool shown);

    void onUpdate() override;

    // Pointer positions are in Main's logical coordinates. Convert through the
    // menu/button transforms (not the transformed renderer bounds) for hit testing.
    bool containsPointer(const c2d::Vector2f &position, const c2d::Transform &parentTransform);
    // The normalized slider value at a pointer position, or -1 outside its track.
    float sliderPartAt(const c2d::Vector2f &position, const c2d::Transform &parentTransform,
                       bool dragging = false);
    void setSliderPreview(float part);
    void clearSliderPreview();

    MenuItem item;
    pplay::IconSprite *icon = nullptr;
    c2d::RectangleShape *line = nullptr;

    c2d::Text *name = nullptr;
    bool selected = false;

private:

    void showStatic();

    void createSlider();

    void startScroll();

    c2d::Vector2f pointerToLocal(const c2d::Vector2f &position,
                                 const c2d::Transform &parentTransform) const;

    c2d::Text *valueText = nullptr;
    c2d::Text *badgeText = nullptr;
    c2d::RectangleShape *sliderTrack = nullptr;
    c2d::RectangleShape *sliderFill = nullptr;
    RoundRect *sliderKnob = nullptr;
    bool sliderPreview = false;
    float sliderPreviewPart = 0.0f;
    Main *mainRef = nullptr;
    c2d::Vector2f scaling;
    float iconSize = 0;
    float maxTextWidth = 0;
    // the text, clipped at maxTextWidth; scrolled when selected and too long
    c2d::Rectangle *clip = nullptr;
    bool scrollable = false;
    bool scrolling = false;
    bool scrollSelected = false;
    long long scrollStartMs = 0;
    float scrollCycle = 0;
    float scrollSpeed = 0;
};

#endif //PPLAY_MENU_BUTTON_H
