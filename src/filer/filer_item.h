//
// Created by cpasjuste on 03/12/18.
//

#ifndef PPLAY_FILERITEM_H
#define PPLAY_FILERITEM_H

#include "cross2d/skeleton/sfml/RectangleShape.hpp"
#include "media_file.h"
#include "clip_rect.h"
#include "highlight.h"

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
    // duration
    c2d::Text *info;
    // watched: ✓ here (watch later) or on TorrServer, ✓✓ both
    c2d::Text *check;
    c2d::Text *check2;
    float textX = 0, textWidth = 0;
    c2d::Sprite *icon;
    // saved copies inside / being saved (network rows): a floppy / a progress ring
    c2d::Sprite *savedMark;
    // shown left of x: its width with the gap, 0 if none
    float updateSavedMark(float right);
    c2d::Rectangle *clip;
    pplay::FadeEdge *fade;
    void updateWatched();

    // the watched part (watch later / TorrServer position), up to watchedWidth
    c2d::RectangleShape *watchedTrack;
    float watchedWidth = 0;
    float iconSize = 0;

    bool lastFullscreen = false;
    // the TorrServer marks shown: from this answer
    unsigned int viewedGeneration = 0;
    unsigned int savedGeneration = 0;
    // a directory: its watched mark
    unsigned int folderGeneration = 0;
    c2d::C2DClock folderClock;

public:

    // the selection highlight over this row (its fade matches it), nullptr otherwise
    const Highlight *highlight = nullptr;
};

#endif //PPLAY_FILERITEM_H
