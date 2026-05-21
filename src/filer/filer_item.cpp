//
// Created by cpasjuste on 03/12/18.
//

#include <sstream>

#include "cross2d/c2d.h"
#include "main.h"
#include "filer_item.h"
#include "utility.h"
#include "torrserve.h"

using namespace c2d;

FilerItem::FilerItem(Main *m, const c2d::FloatRect &rect, const MediaFile &f) : Rectangle(rect) {

    main = m;
    file = f;

    textTitle = new Text(file.name.empty() ? "AjIilp" : file.name,
            main->getFontSize(Main::FontSize::Bigger), main->getFont());
    textTitle->setOrigin(Origin::Left);
    textTitle->setPosition(16, FilerItem::getSize().y / 2);
    textTitle->setSizeMax(FilerItem::getSize().x - 64, 0);
    FilerItem::add(textTitle);
}

void FilerItem::setFile(const MediaFile &f) {

    this->file = f;
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "FilerItem::setFile name=" + file.name + " path=" + file.path
        + " type=" + std::to_string((int) file.type));

    if (file.name == "..") {
        textTitle->setString("◀ BACK");
    } else {
        textTitle->setString(file.name);
    }
    uint8_t alpha = textTitle->getAlpha();
    if (file.type == Io::Type::File) {
        const std::string streamUrl = pplay::TorrServe::toStreamUrl(file.path);
        const bool isWatched = pplay::Utility::isWatchLaterExist(streamUrl) 
                            || pplay::TorrServe::isFileViewed(file.path);
        textTitle->setFillColor(isWatched ? COLOR_VIEWED : COLOR_FONT);
    } else if (file.type == Io::Type::Directory) {
        textTitle->setFillColor(COLOR_ACCENT);
    }
    textTitle->setAlpha(alpha);
}

void FilerItem::setTitle(const std::string &title) {

    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "FilerItem::setTitle title=" + title);
    textTitle->setString(title);
}

void FilerItem::onUpdate() {
    const bool isFullscreen = main->getPlayer() && main->getPlayer()->isFullscreen();
    const bool fullscreenExited = lastFullscreen && !isFullscreen;
    lastFullscreen = isFullscreen;
    if (updateClock.getElapsedTime().asSeconds() > 30.0f || fullscreenExited) {
        if (file.type == Io::Type::File && !isFullscreen) {
            const std::string streamUrl = pplay::TorrServe::toStreamUrl(file.path);
            const bool isWatched = pplay::Utility::isWatchLaterExist(streamUrl) 
                                || pplay::TorrServe::isFileViewed(file.path);
            Color targetColor = isWatched ? COLOR_VIEWED : COLOR_FONT;
            textTitle->setFillColor(targetColor);
        }
        updateClock.restart();
    }
    if (file.type == Io::Type::Directory) {
        textTitle->setFillColor(COLOR_ACCENT);
    }
    Rectangle::onUpdate();
}