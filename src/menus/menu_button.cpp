//
// Created by cpasjuste on 20/10/18.
//

#include <algorithm>
#include <cmath>

#include "main.h"
#include "clip_rect.h"
#include "menu_button.h"
#include "round_rect.h"
#include "text_wrap.h"

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
        icon->setPosition(12 * main->getScaling().x, MenuButton::getSize().y / 2);
        // any resolution: 36 px at 720p, smaller in compact menus
        iconSize = std::min(30.0f * main->getScaling().y, MenuButton::getSize().y * 0.65f);
        float scale = iconSize / (float) icon->getTextureSize().x;
        icon->setScale(scale, scale);
        icon->setFillColor(COLOR_FONT);
        MenuButton::add(icon);
    }

    name = new Text(item.text(), fontSize, main->getFont());
    name->setOrigin(Origin::Left);
    name->setFillColor(pplay::Utility::isValidHexColor(item.data_str) ? pplay::Utility::hexToColor(item.data_str)
                                                                      : COLOR_FONT);

    float textX = 12.0f * main->getScaling().x + (item.icon.empty() ? 0 : iconSize + 12.0f * main->getScaling().x);
    // up to the button edge (the highlight cursor), or the value part
    const float sc = main->getScaling().x;
    float right = MenuButton::getSize().x - 18 * sc;
    bool valued = !item.value.empty() || item.slider >= 0 || item.opens;
    // the name gets what the value leaves (refreshValue), at least half the row
    float valueWidth = item.slider >= 0 ? MenuButton::getSize().x * 0.36f
                                        : (item.opens && item.value.empty() ? 24 * sc : MenuButton::getSize().x * 0.5f);
    maxTextWidth = std::max(0.0f, (valued ? right - valueWidth - 12 * sc : MenuButton::getSize().x - 6.0f) - textX);
    if (valued) {
        valueText = new Text("", main->getFontSize(Main::FontSize::Small), main->getFont());
        valueText->setOrigin(Origin::Right);
        valueText->setPosition(right, MenuButton::getSize().y / 2);
        MenuButton::add(valueText);
    }
    scaling = main->getScaling();
    if (item.slider >= 0) {
        createSlider();
    }
    scrollSpeed = SCROLL_SPEED * main->getScaling().x;
    clip = new pplay::ClipRect({textX, 0, maxTextWidth, MenuButton::getSize().y});
    clip->add(name);
    MenuButton::add(clip);
    refreshText();
    refreshValue();
}

void MenuButton::createSlider() {
    // between the name and the value: 60 px of value text at 720p
    float right = getSize().x - 18 * scaling.x, valueWidth = getSize().x * 0.36f;
    float line = std::max(2.0f, std::round(4 * scaling.y));
    float left = right - valueWidth, end = right - 60 * scaling.x;
    sliderTrack = new RectangleShape({left, getSize().y / 2 - line / 2, end - left, line});
    sliderTrack->setFillColor(Color(255, 255, 255, 40));
    add(sliderTrack);
    sliderFill = new RectangleShape({0, 0, 0, line});
    sliderTrack->add(sliderFill);
    float knob = std::round(12 * scaling.y);
    sliderKnob = new RoundRect({knob, knob});
    sliderKnob->setOrigin(Origin::Center);
    sliderKnob->setFillColor(COLOR_WHITE);
    sliderTrack->add(sliderKnob);
    if (clip != nullptr) {
        // the name before the slider
        float width = std::max(0.0f, left - 12 * scaling.x - clip->getPosition().x);
        maxTextWidth = width;
        clip->setSize(width, clip->getSize().y);
        refreshText();
    }
}

void MenuButton::refreshValue() {
    if (valueText == nullptr) {
        return;
    }
    // a slider known later (settings groups)
    if (item.slider >= 0 && sliderTrack == nullptr) {
        createSlider();
    }
    if (sliderTrack != nullptr) {
        sliderTrack->setVisibility(item.slider >= 0 ? Visibility::Visible : Visibility::Hidden);
    }
    std::string text = item.value.empty() && item.opens ? "›" : oneLine(item.value);
    if (!item.value.empty() && item.opens) {
        text += "  ›";
    }
    valueText->setString(text);
    if (sliderTrack == nullptr) {
        // the name up to the value
        float gap = 28 * (getSize().y / BUTTON_HEIGHT);
        float textX = clip->getPosition().x;
        float fullWidth = std::max(0.0f, getSize().x - 6.0f - textX);
        // a long value cut, the name keeps half the row at least
        name->setSizeMax(0, 0);
        float nameWidth = std::min(name->getLocalBounds().width, fullWidth / 2);
        valueText->setSizeMax(std::max(0.0f, fullWidth - nameWidth - gap), 0);
        float width = std::max(fullWidth / 2, valueText->getPosition().x - valueText->getLocalBounds().width - gap - textX);
        width = std::min(width, fullWidth);
        if (width != maxTextWidth) {
            maxTextWidth = width;
            clip->setSize(width, clip->getSize().y);
                refreshText();
        }
    }
    valueText->setFillColor(item.valueAccent ? COLOR_ACCENT : Color(160, 168, 178, 255));
    if (sliderTrack != nullptr) {
        float part = std::min(1.0f, std::max(0.0f, item.slider));
        float width = sliderTrack->getSize().x;
        sliderFill->setSize(std::round(part * width), sliderFill->getSize().y);
        sliderFill->setFillColor(COLOR_ACCENT);
        sliderKnob->setPosition(std::round(part * width), sliderTrack->getSize().y / 2);
    }
}

void MenuButton::refreshText() {
    name->setSizeMax(0, 0);
    name->setString(oneLine(item.text()));
    scrollable = name->getLocalBounds().width > maxTextWidth;
    scrolling = false;
    scrollSelected = false;
    showStatic();
}

void MenuButton::showStatic() {
    // too long: cut with "…"
    name->setString(pplay::TextWrap::ellipsize(name->getFont(), name->getCharacterSize(), oneLine(item.text()),
                                               clip->getSize().x));
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
