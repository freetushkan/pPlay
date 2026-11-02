//
// Created by cpasjuste on 08/12/18.
//

#ifndef PPLAY_STATUS_BOX_H
#define PPLAY_STATUS_BOX_H

#include "cross2d/skeleton/sfml/RectangleShape.hpp"

class Main;

class RoundRect;

class StatusBox : public c2d::Rectangle {

public:

    explicit StatusBox(Main *main);

    ~StatusBox() override;

    void onUpdate() override;

    // faded in / out
    void setVisibility(c2d::Visibility visibility, bool tweenPlay = false) override;

    void show(const std::string &type, const std::string &message, bool infinite = false, bool drawNow = false);

    void hide();

    void setAlpha(uint8_t alpha, bool recursive = false) override;

private:

    virtual void onDraw(c2d::Transform &transform, bool draw = true) override;

    void applyAlpha();

    Main *main;
    RoundRect *chip = nullptr;
    c2d::Texture *icon = nullptr;
    c2d::Text *messageText = nullptr;
    c2d::Color textColor;
    c2d::Clock *clock = nullptr;
    SDL_mutex *mutex = nullptr;
    float alpha = 1, target = 1;
    float textMaxWidth = -1;
    c2d::C2DClock fadeClock;
    bool infinite = false;
};

#endif //PPLAY_STATUS_BOX_H
