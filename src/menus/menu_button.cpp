//
// Created by cpasjuste on 20/10/18.
//

#include <algorithm>
#include <cmath>

#include "main.h"
#include "clip_rect.h"
#include "menu_button.h"

using namespace c2d;

// the selected item text scrolls when it doesn't fit: pause, one turn, pause...
#define SCROLL_WAIT_MS 1500
#define SCROLL_SPEED 90.0f // pixels per second at 1080p
#define SCROLL_GAP "     "

namespace {
    long long nowMs() {
        return (long long) c2d_renderer->getElapsedTime().asMilliseconds();
    }

    // one line: new lines, tabs and repeated spaces as one space
    std::string oneLine(const std::string &input) {
        std::string result;
        bool space = false;
        for (char c: input) {
            if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
                if (!space) {
                    result += ' ';
                }
                space = true;
            } else {
                result += c;
                space = false;
            }
        }
        if (!result.empty() && result.front() == ' ') {
            result.erase(0, 1);
        }
        if (!result.empty() && result.back() == ' ') {
            result.pop_back();
        }
        return result;
    }
}

std::string MenuItem::text() const {
    return label.empty() ? pplay::tr(name) : label;
}

MenuButton::MenuButton(Main *main, const MenuItem &it, const FloatRect &rect, unsigned int fontSize)
    : Rectangle(rect), item(it) {
    MenuButton::setOrigin(Origin::Left);

    if (!item.icon.empty()) {
        icon = new C2DTexture(main->getIo()->getDataPath() + "skin/" + item.icon);
        icon->setOrigin(Origin::Left);
        icon->setPosition(16 * main->getScaling().x, MenuButton::getSize().y / 2);
        icon->setScale(main->getScaling());
        icon->setFillColor(COLOR_FONT);
        MenuButton::add(icon);
    }

    name = new Text(item.text(), fontSize, main->getFont());
    name->setOrigin(Origin::Left);
    name->setFillColor(pplay::Utility::isValidHexColor(item.data_str) ? pplay::Utility::hexToColor(item.data_str)
                                                                      : COLOR_FONT);

    float textX = (item.icon.empty() ? 16.0f : ICON_SIZE + 32.0f) * main->getScaling().x;
    maxTextWidth = std::max(0.0f, MenuButton::getSize().x - textX - 16.0f * main->getScaling().x);
    scrollSpeed = SCROLL_SPEED * main->getScaling().x;
    clip = new pplay::ClipRect({textX, 0, maxTextWidth, MenuButton::getSize().y});
    clip->add(name);
    MenuButton::add(clip);
    refreshText();
}

void MenuButton::refreshText() {
    name->setSizeMax(0, 0);
    name->setString(item.text());
    scrollable = name->getLocalBounds().width > maxTextWidth;
    scrolling = false;
    scrollSelected = false;
    showStatic();
}

void MenuButton::showStatic() {
    name->setString(item.text());
    name->setSizeMax(maxTextWidth, MenuButton::getSize().y);
    name->setPosition(0, clip->getSize().y / 2);
}

void MenuButton::startScroll() {
    // text, gap, text: at one turn (text + gap width) it looks like the start
    std::string text = oneLine(item.text());
    name->setSizeMax(0, 0);
    name->setString(text);
    float single = name->getLocalBounds().width;
    name->setString(text + SCROLL_GAP + text);
    scrollCycle = std::max(1.0f, name->getLocalBounds().width - single);
}

void MenuButton::onUpdate() {
    Rectangle::onUpdate();
    if (!scrollable || !isVisible()) {
        return;
    }
    if (!selected) {
        if (scrollSelected) {
            scrolling = false;
            scrollSelected = false;
            showStatic();
        }
        return;
    }

    long long now = nowMs();
    if (!scrollSelected) {
        scrollSelected = true;
        scrollStartMs = now;
        return;
    }
    long long elapsed = now - scrollStartMs - SCROLL_WAIT_MS;
    if (elapsed < 0) {
        return;
    }
    if (!scrolling) {
        startScroll();
        scrolling = true;
    }
    auto turnMs = (long long) (scrollCycle / scrollSpeed * 1000.0f);
    long long phase = elapsed % (turnMs + SCROLL_WAIT_MS);
    float offset = phase < turnMs ? (float) phase * scrollSpeed / 1000.0f : 0;
    name->setPosition(-std::round(offset), clip->getSize().y / 2);
}
