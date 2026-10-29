//
// Created by cpasjuste on 03/12/18.
//

#ifndef PPLAY_FILERITEM_H
#define PPLAY_FILERITEM_H

#include "cross2d/skeleton/sfml/RectangleShape.hpp"
#include "media_file.h"

class Main;

class FilerItem : public c2d::Rectangle {

public:

    FilerItem(Main *main, const c2d::FloatRect &rect, const MediaFile &file = {});

    void setFile(const MediaFile &file);

    void onUpdate();

private:

    // skin file, "" for none
    void setIcon(const std::string &name);

    void setColor(const c2d::Color &color);

    // the file state color
    c2d::Color color();

    Main *main;
    MediaFile file;

    c2d::C2DClock updateClock;

    c2d::Text *textTitle;
    c2d::Sprite *icon;
    float iconSize = 0;

    bool lastFullscreen = false;
};

#endif //PPLAY_FILERITEM_H
