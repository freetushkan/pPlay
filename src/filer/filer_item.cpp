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
    if (file.type == Io::Type::Directory) {
        textTitle->setFillColor(COLOR_ACCENT);
    } else {
        bool wlExists = pplay::Utility::isWatchLaterExist(pplay::TorrServe::toStreamUrl(file.path));
        bool tsViewed = pplay::TorrServe::isFileViewed(file.path, main->getPlayer());
        textTitle->setFillColor((wlExists || tsViewed) ? COLOR_VIEWED : COLOR_FONT);
    }
    textTitle->setAlpha(alpha);
}

void FilerItem::setTitle(const std::string &title) {

    pplay::Utility::log(pplay::Utility::LogLevel::Info,
        "FilerItem::setTitle title=" + title);
    textTitle->setString(title);
}

void FilerItem::onUpdate() {

    if (updateClock.getElapsedTime().asSeconds() > 30.0f) {
        if (file.type != Io::Type::Directory) {
            bool wlExists = pplay::Utility::isWatchLaterExist(pplay::TorrServe::toStreamUrl(file.path));
            bool tsViewed = pplay::TorrServe::isFileViewed(file.path, main->getPlayer());
            Color targetColor = (wlExists || tsViewed) ? COLOR_VIEWED : COLOR_FONT;
            if (textTitle->getFillColor() != targetColor) {
                textTitle->setFillColor(targetColor);
            }
        }
        updateClock.restart();
    }
    Rectangle::onUpdate();
}