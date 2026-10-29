//
// Created by cpasjuste on 20/10/18.
//

#ifndef PPLAY_MENU_BUTTON_H
#define PPLAY_MENU_BUTTON_H

#include <string>
#include "cross2d/c2d.h"

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
};

class MenuButton : public c2d::Rectangle {

public:

    MenuButton(Main *main, const MenuItem &item, const c2d::FloatRect &rect, unsigned int fontSize);

    // after item.name / item.label changes
    void refreshText();

    void onUpdate() override;

    MenuItem item;
    c2d::Texture *icon = nullptr;
    c2d::Text *name = nullptr;
    bool selected = false;

private:

    void showStatic();

    void startScroll();

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
