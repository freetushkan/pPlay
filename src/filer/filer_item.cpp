//
// Created by cpasjuste on 03/12/18.
//

#include <sstream>

#include "cross2d/c2d.h"
#include "main.h"
#include "filer_item.h"
#include "utility.h"

using namespace c2d;

FilerItem::FilerItem(Main *m, const c2d::FloatRect &rect, const MediaFile &f) : Rectangle(rect) {

    main = m;
    file = f;

    textTitle = new Text(file.name.empty() ? "AjIilp" : file.name,
                         main->getFontSize(Main::FontSize::Medium), main->getFont());
    textTitle->setOrigin(Origin::Left);
    textTitle->setPosition(16, (FilerItem::getSize().y / 4) + 3);
    textTitle->setSizeMax(FilerItem::getSize().x - 64, 0);
    FilerItem::add(textTitle);

    textInfo = new Text("INFO", main->getFontSize(Main::FontSize::Small), main->getFont());
    textInfo->setOrigin(Origin::Left);
    textInfo->setPosition(16, ((FilerItem::getSize().y / 4) * 3) - (2 * main->getScaling().y));
    textInfo->setSizeMax(FilerItem::getSize().x - 64, 0);
    textInfo->setFillColor(COLOR_FONT);
    FilerItem::add(textInfo);
}

void FilerItem::setFile(const MediaFile &f) {

    this->file = f;
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "FilerItem::setFile name=" + file.name
                                                         + " path=" + file.path
                                                         + " type=" + std::to_string((int) file.type));

    textTitle->setString(file.name);
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "FilerItem::setFile titleUtf8=" + textTitle->getString());
    uint8_t alpha = textTitle->getAlpha();
    if (file.type == Io::Type::Directory) {
        textTitle->setFillColor(COLOR_BLUE);
    } else {
        textTitle->setFillColor(COLOR_RED);
    }
    textTitle->setAlpha(alpha);
    if (file.type == Io::Type::File) {
        textInfo->setString(file.name);
    } else {
        textInfo->setString("");
    }
}

void FilerItem::setTitle(const std::string &title) {
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "FilerItem::setTitle title=" + title);
    textTitle->setString(title);
}
