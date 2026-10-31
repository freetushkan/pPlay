//
// Created by cpasjuste on 09/01/19.
//

#include <sstream>
#include <iomanip>
#include <ctime>
#include <cmath>
#include <algorithm>

#include "cross2d/c2d.h"
#include "main.h"
#include "status_bar.h"
#include "utility.h"
#include "pplay_config.h"
#include "round_rect.h"

#ifdef __SWITCH__

#include <switch.h>

#endif

using namespace c2d;

#ifdef __SWITCH__
class Battery : public RectangleShape {

public:

    explicit Battery(const FloatRect &rect, const Vector2f &scaling) : RectangleShape({0, 0}) {

        float outline = std::ceil(scaling.x);
        Battery::setFillColor(Color::Transparent);
        Battery::setOutlineColor(COLOR_FONT);
        Battery::setOutlineThickness(outline);
        Battery::setOrigin(Origin::Right);
        Battery::setPosition((rect.width - (8 * scaling.x)) + outline, (rect.height / 2) + outline);
        Battery::setSize(32 * scaling.x, rect.height - (scaling.y * 16));

        maxWidth = Battery::getSize().x - (4 * scaling.x);
        Vector2f size = {maxWidth, Battery::getSize().y - (4 * scaling.y)};
        batteryRect = new RectangleShape(size);
        batteryRect->setFillColor(COLOR_FONT);
        batteryRect->setOrigin(Origin::Right);
        batteryRect->setPosition(Battery::getSize().x - (2 * scaling.x), (Battery::getSize().y + (outline / 2)) / 2);
        Battery::add(batteryRect);

        leftRect = new RectangleShape({scaling.x * 6, rect.height / 3});
        leftRect->setFillColor(COLOR_FONT);
        leftRect->setOrigin(Origin::Right);
        leftRect->setPosition(0, batteryRect->getPosition().y);
        Battery::add(leftRect);
    }

    void onUpdate() override {

        if (!isVisible()) {
            return;
        }

        psmGetBatteryChargePercentage(&percent);
        // ps4: not std::clamp
        if (percent < 1) {
            percent = 1;
        } else if (percent > 100) {
            percent = 100;
        }

        float width = ((float) percent / 100) * maxWidth;
        if (width != batteryRect->getSize().x) {
            batteryRect->setSize(std::max(width, 2.0f), batteryRect->getSize().y);
            if (percent < 15) {
                batteryRect->setFillColor(Color::Red);
            } else if (percent < 30) {
                batteryRect->setFillColor(Color::Orange);
            } else {
                batteryRect->setFillColor(COLOR_FONT);
            }
        }

        RectangleShape::onUpdate();
    }

    // its fill stays transparent
    void fade(uint8_t alpha) {
        setOutlineColor({COLOR_FONT.r, COLOR_FONT.g, COLOR_FONT.b, alpha});
        batteryRect->setAlpha(alpha);
        leftRect->setAlpha(alpha);
    }

    RectangleShape *batteryRect = nullptr;
    RectangleShape *leftRect = nullptr;
    unsigned int percent = 100;
    float maxWidth;
};
#endif

StatusBar::StatusBar(Main *m) : Rectangle({0, 0, 64, 64}) {
#ifdef __SWITCH__
    psmInitialize();
#endif
    main = m;
    const float sc = m->getScaling().y;
    StatusBar::setSize(m->getSize().x, 32 * sc);
    // off the screen edge, as the player panel at the bottom
    StatusBar::setPosition(0, 18 * sc);

    // a chip on the right: battery (Switch) and clock
    chip = new RoundRect({10, std::round(26 * sc)});
    chip->setOrigin(Origin::Right);
    StatusBar::add(chip);

#ifdef __SWITCH__
    battery = new Battery(StatusBar::getGlobalBounds(), m->getScaling());
    battery->setPosition(battery->getPosition().x - 20 * m->getScaling().x, battery->getPosition().y);
    StatusBar::add(battery);
#endif

    timeText = new Text("12:00", m->getFontSize(Main::FontSize::Big), m->getFont());
    timeText->setOrigin(Origin::Right);
#ifdef __SWITCH__
    timeText->setPosition(battery->getPosition().x - battery->getSize().x - (m->getScaling().x * 16),
                          StatusBar::getSize().y / 2);
#else
    timeText->setPosition(StatusBar::getGlobalBounds().width - (m->getScaling().x * 38), StatusBar::getSize().y / 2);
#endif
    StatusBar::add(timeText);
    applyAlpha();
}

void StatusBar::setVisibility(Visibility visibility, bool tweenPlay) {
    target = visibility == Visibility::Visible ? 1.0f : 0.0f;
    if (!tweenPlay) {
        alpha = target;
    }
    if (visibility == Visibility::Visible || alpha <= 0) {
        Rectangle::setVisibility(visibility, false);
    }
    applyAlpha();
}

void StatusBar::applyAlpha() {
    uint8_t a = (uint8_t) std::lround(255 * alpha);
    timeText->setFillColor({COLOR_FONT.r, COLOR_FONT.g, COLOR_FONT.b, a});
    chip->setFillColor({10, 12, 16, (uint8_t) std::lround(170 * alpha)});
#ifdef __SWITCH__
    battery->fade(a);
#endif
}

void StatusBar::onUpdate() {
    if (!isVisible()) {
        return;
    }
    float dt = std::min(0.1f, fadeClock.restart().asSeconds());
    if (alpha != target) {
        alpha += (target > alpha ? 1 : -1) * dt * 4;
        alpha = std::min(1.0f, std::max(0.0f, alpha));
        if (std::fabs(alpha - target) < 0.02f) {
            alpha = target;
        }
        applyAlpha();
        if (alpha <= 0) {
            Rectangle::setVisibility(Visibility::Hidden, false);
            return;
        }
    }

#ifdef __PS4__
    // time zone changes
    if (tzClock.getElapsedTime().asSeconds() >= 30) {
        ps4_update_timezone();
        tzClock.restart();
    }
#endif

    time_t time_raw;
    time(&time_raw);
    struct tm time_struct {};
    localtime_r(&time_raw, &time_struct);

    int hour = time_struct.tm_hour;
    int min = time_struct.tm_min;

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hour << ":";
    oss << std::setfill('0') << std::setw(2) << min;

    timeText->setString(oss.str());

    // the player: off the edge as its panel; the filer: on its path line
    bool player = main->getPlayer() != nullptr && main->getPlayer()->isFullscreen();
    setPosition(0, std::round((player ? 18 : 6) * main->getScaling().y));

    // the chip around the drawn digits (not the font line), even paddings
    const Vector2f sc = main->getScaling();
    FloatRect text = timeText->getLocalBounds();
    float originY = std::max(text.height, (float) timeText->getCharacterSize()) / 2;
    float centerY = timeText->getPosition().y - originY + text.top + text.height / 2;
    float right = getSize().x - 22 * sc.x;
    float left = timeText->getPosition().x - text.width - 16 * sc.x;
    Vector2f chipSize = {std::round(right - left), std::round(text.height + 20 * sc.y)};
    if (chip->getSize().x != chipSize.x || chip->getSize().y != chipSize.y) {
        chip->setSize(chipSize);
    }
    chip->setPosition(right, std::round(centerY));
    Rectangle::onUpdate();
}

StatusBar::~StatusBar() {
#ifdef __SWITCH__
    psmExit();
#endif
}
