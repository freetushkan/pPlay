//
// Created by cpasjuste on 08/12/18.
//

#include <algorithm>
#include "cross2d/c2d.h"
#include "main.h"
#include "status_box.h"
#include "i18n.h"
#include "player_osd.h"
#include "utility.h"

using namespace c2d;

StatusBox::StatusBox(Main *m, const c2d::Vector2f &position) : Rectangle(m->getScaled({64, 48})) {

    main = m;
    pos = m->getScaled(position);
    StatusBox::setPosition(pos);

    // as high as the two text lines
    icon = new C2DTexture(main->getIo()->getDataPath() + "skin/wait.png");
    float iconSize = 22 * m->getScaling().y;
    float iconScale = iconSize / (float) std::max(1, icon->getTextureSize().y);
    icon->setOrigin(Origin::Center);
    icon->setPosition(iconSize / 2 + (4 * m->getScaling().x), StatusBox::getSize().y / 2);
    icon->setScale(iconScale, iconScale);
    icon->setFillColor(COLOR_FONT);
    icon->setAlpha(200);
    icon->add(new TweenRotation(360, 0, 2, TweenLoop::Loop, TweenState::Playing));
    StatusBox::add(icon);

    titleText = new Text("Please Wait...", main->getFontSize(Main::FontSize::Medium), main->getFont());
    titleText->setFillColor(COLOR_ACCENT);
    titleText->setPosition(iconSize + (14 * m->getScaling().x), (4 * m->getScaling().y));
    // the titles are generic ("Info...", "Please Wait..."): the spinner / the color say it
    titleText->setVisibility(Visibility::Hidden);
    StatusBox::add(titleText);

    messageText = new Text("Doing something in background, please wait", main->getFontSize(Main::FontSize::Small),
                           main->getFont());
    messageText->setFillColor(COLOR_FONT);
    messageText->setOrigin(Origin::Left);
    messageText->setOutlineThickness(1);
    messageText->setOutlineColor(Color(0, 0, 0, 160));
    messageText->setPosition(iconSize + (12 * m->getScaling().x), StatusBox::getSize().y / 2);
    StatusBox::add(messageText);

    clock = new C2DClock();
    mutex = SDL_CreateMutex();

    tween = new TweenAlpha(0, 255, 0.5f);
    StatusBox::add(tween);
    StatusBox::setVisibility(Visibility::Hidden);
}

StatusBox::~StatusBox() {
    delete (clock);
    SDL_DestroyMutex(mutex);
}

void StatusBox::show(const std::string &title, const std::string &message, bool inf, bool drawNow) {

    SDL_LockMutex(mutex);
    titleText->setString(pplay::tr(title));
    messageText->setString(pplay::tr(message));
    messageText->setFillColor(title == "Error..." ? Color(240, 120, 110, 255) : COLOR_FONT);
    // the spinner while waiting only
    bool spinning = inf || title == "Please Wait...";
    icon->setVisibility(spinning ? Visibility::Visible : Visibility::Hidden);
    messageText->setPosition(spinning ? icon->getPosition().x * 2 + 10 * main->getScaling().x
                                      : 4 * main->getScaling().x,
                             messageText->getPosition().y);
    SDL_UnlockMutex(mutex);

    infinite = inf;
    clock->restart();
    setVisibility(Visibility::Visible, true);
    if (drawNow) {
        for (int i = 0; i < 30; i++) {
            main->flip();
        }
    }
}

void StatusBox::hide() {
    clock->restart();
    infinite = false;
}

void StatusBox::onDraw(c2d::Transform &transform, bool draw) {

    if (isVisible() && !infinite && clock->getElapsedTime().asSeconds() > 2) {
        setVisibility(Visibility::Hidden, true);
    } else {
        PlayerOSD *osd = main->getPlayer()->getOSD();
        if (osd && main->getPlayer()->isFullscreen()) {
            // top left, the clock on the other side
            setPosition(32 * main->getScaling().x, 20 * main->getScaling().y + getSize().y);
        } else {
            FloatRect bounds = main->getMenuMain()->getGlobalBounds();
            setPosition(bounds.left + bounds.width + pos.x, main->getSize().y - (16 * main->getScaling().x));
        }
    }
    if (isVisible() && titleText->getAlpha() > 0) {
        titleText->setFillColor(COLOR_ACCENT);
    }

    SDL_LockMutex(mutex);
    C2DObject::onDraw(transform, draw);
    SDL_UnlockMutex(mutex);
}

void StatusBox::setAlpha(uint8_t alpha, bool recursive) {
    C2DObject::setAlpha(alpha, recursive);
}
