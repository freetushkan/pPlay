//
// Created by cpasjuste on 09/01/19.
//

#ifndef PPLAY_STATUSBAR_H
#define PPLAY_STATUSBAR_H

class Main;

class Battery;

class RoundRect;

class StatusBar : public c2d::Rectangle {

public:

    explicit StatusBar(Main *main);

    ~StatusBar() override;

    void onUpdate() override;

    // faded in / out
    void setVisibility(c2d::Visibility visibility, bool tweenPlay = false) override;

    Battery *battery = nullptr;
    c2d::Text *timeText = nullptr;

private:

    void applyAlpha();

    Main *main;
    RoundRect *chip = nullptr;
    float alpha = 1, target = 1;
    c2d::C2DClock fadeClock;
#ifdef __PS4__
    c2d::C2DClock tzClock;
#endif
};

#endif //PPLAY_STATUSBAR_H
