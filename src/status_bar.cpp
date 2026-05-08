//
// Created by cpasjuste on 09/01/19.
//

#include <sstream>
#include <iomanip>
#include <ctime>

#include "cross2d/c2d.h"
#include "main.h"
#include "status_bar.h"
#include "utility.h"
#include "pplay_config.h"

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
        batteryRect->setPosition(Battery::getSize().x - (2 * scaling.x),
                                 (Battery::getSize().y + (outline / 2)) / 2);
        Battery::add(batteryRect);

        auto leftRect = new RectangleShape({scaling.x * 6, rect.height / 3});
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

    RectangleShape *batteryRect = nullptr;
    unsigned int percent = 100;
    float maxWidth;
};
#endif

StatusBar::StatusBar(Main *m) : GradientRectangle({0, 0, 64, 64}) {

#ifdef __SWITCH__
    psmInitialize();
#endif

    main = m;

    setColor(COLOR_BG, Color::Transparent, Direction::Left);
    StatusBar::setSize(m->getSize().x, 32 * m->getScaling().y);
    StatusBar::setPosition(0, -StatusBar::getSize().y);
    StatusBar::add(new TweenPosition(StatusBar::getPosition(), {0, 0}, 0.5f));

#ifdef __SWITCH__
    battery = new Battery(StatusBar::getGlobalBounds(), m->getScaling());
    StatusBar::add(battery);
#endif

    // time
    timeText = new Text("12:00", m->getFontSize(Main::FontSize::Big), m->getFont());
    timeText->setOrigin(Origin::Right);
#ifdef __SWITCH__
    timeText->setPosition(battery->getPosition().x - battery->getSize().x - (m->getScaling().x * 20),
                          StatusBar::getSize().y / 2);
#else
    timeText->setPosition(StatusBar::getGlobalBounds().width - (m->getScaling().x * 20),
                          StatusBar::getSize().y / 2);
#endif
    timeText->setFillColor(COLOR_FONT);
    StatusBar::add(timeText);

    StatusBar::setVisibility(Visibility::Visible, true);
}

void StatusBar::onUpdate() {

    if (!isVisible()) {
        return;
    }

    time_t time_raw;
    struct tm *time_struct;

    time(&time_raw);
    // localtime() cannot handle real offset on PS4.
#ifdef __PS4__
    float offset_hours = main->getConfig()->getOption(OPT_UTC_OFFSET)->getFloat();
    long offset_seconds = static_cast<long>(offset_hours * 3600.0);
    time_raw += offset_seconds;
    time_struct = gmtime(&time_raw);
#else
    time_struct = localtime(&time_raw);
#endif
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << time_struct->tm_hour << ":";
    oss << std::setfill('0') << std::setw(2) << time_struct->tm_min;
    timeText->setString(oss.str());

    GradientRectangle::onUpdate();
}

StatusBar::~StatusBar() {
#ifdef __SWITCH__
    psmExit();
#endif
}
