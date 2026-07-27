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

#elif __PS4__
typedef struct OrbisDateTime {
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;
    uint32_t microsecond;
} OrbisDateTime;
extern "C" {
    int sceKernelDlsym(int handle, const char *symbol, void **address);
}
static int (*local_sceRtcGetCurrentClockLocalTime)(OrbisDateTime *time) = nullptr;
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
    time(&time_raw);
    struct tm *time_struct;
    time_struct = localtime(&time_raw);

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << time_struct->tm_hour << ":";
    oss << std::setfill('0') << std::setw(2) << time_struct->tm_min;
#ifdef __PS4__
    if (local_sceRtcGetCurrentClockLocalTime == nullptr) {
        int handle = sceKernelLoadStartModule("/system/common/lib/libSceRtc.sprx", 0, NULL, 0, NULL, NULL);
        if (handle > 0) {
            sceKernelDlsym(handle, "sceRtcGetCurrentClockLocalTime", (void **)&local_sceRtcGetCurrentClockLocalTime);
        }
    }
    OrbisDateTime psTime;
    if (local_sceRtcGetCurrentClockLocalTime != nullptr && local_sceRtcGetCurrentClockLocalTime(&psTime) == 0) {
        oss << std::setfill('0') << std::setw(2) << psTime.hour << ":";
        oss << std::setfill('0') << std::setw(2) << psTime.minute;
    }
#endif
    timeText->setString(oss.str());
    GradientRectangle::onUpdate();
}

StatusBar::~StatusBar() {
#ifdef __SWITCH__
    psmExit();
#endif
}
