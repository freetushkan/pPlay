//
// Created by cpasjuste on 07/12/18.
//

#ifndef PPLAY_MENU_VIDEO_H
#define PPLAY_MENU_VIDEO_H

#include "menu.h"

class MenuVideo : public Menu {

public:

    MenuVideo(Main *main, const c2d::FloatRect &rect, const std::vector<MenuItem> &items);

    // the current values on the right
    void refreshValues();

    void setVisibility(c2d::Visibility visibility, bool tweenPlay = true) override;

private:

    bool onInput(c2d::Input::Player *players) override;

    void onOptionSelection(MenuItem *item) override;

    bool onOptionChange(MenuItem *item, int direction) override;

    void onUpdate() override;

    c2d::C2DClock valuesClock;
};

#endif //PPLAY_MENU_VIDEO_H
