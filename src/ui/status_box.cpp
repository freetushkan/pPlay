//
// Created by cpasjuste on 08/12/18.
//

#include <algorithm>
#include <cmath>
#include "cross2d/c2d.h"
#include "main.h"
#include "status_box.h"
#include "round_rect.h"
#include "i18n.h"
#include "player_osd.h"
#include "utility.h"

using namespace c2d;

StatusBox::StatusBox(Main *m) : Rectangle(m->getScaled({0, 0, 64, 48})) {

    main = m;
    const float sc = m->getScaling().y;
    StatusBox::setSize(m->getSize().x, 32 * sc);
    StatusBox::setPosition(0, 18 * sc);

    chip = new RoundRect({10, std::round(26 * sc)});
    chip->setOrigin(Origin::Left);
    chip->setFillColor(Color(10, 12, 16, 170));
    StatusBox::add(chip);

    icon = new C2DTexture(m->getIo()->getDataPath() + "skin/wait.png");
    float iconSize = 22 * m->getScaling().y;
    float iconScale = iconSize / (float) std::max(1, icon->getTextureSize().y);
    icon->setOrigin(Origin::Center);
    icon->setPosition(iconSize / 2 + (22 * m->getScaling().x), StatusBox::getSize().y / 2);
    icon->setScale(iconScale, iconScale);
    icon->setFillColor(COLOR_FONT);
    icon->setAlpha(200);
    icon->add(new TweenRotation(360, 0, 2, TweenLoop::Loop, TweenState::Playing));
    StatusBox::add(icon);

    titleText = new Text("Wait", m->getFontSize(Main::FontSize::Medium), m->getFont());
    titleText->setFillColor(COLOR_ACCENT);
    titleText->setPosition(iconSize + (38 * m->getScaling().x), (4 * m->getScaling().y));
    titleText->setVisibility(Visibility::Hidden);
    StatusBox::add(titleText);

    messageText = new Text("Wait", m->getFontSize(Main::FontSize::Small), m->getFont());
    messageText->setFillColor(COLOR_FONT);
    messageText->setOrigin(Origin::Left);
    messageText->setPosition(iconSize + (38 * m->getScaling().x), StatusBox::getSize().y / 2 - (1 * m->getScaling().y));
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
    messageText->setFillColor(title == "Error" ? Color(240, 120, 110, 255) : COLOR_FONT);
    bool spinning = inf || title == "Wait";
    icon->setVisibility(spinning ? Visibility::Visible : Visibility::Hidden);
    float iconSize = 22 * main->getScaling().y;
    messageText->setPosition(spinning ? icon->getPosition().x + (iconSize / 2) + 16 * main->getScaling().x
                                      : 22 * main->getScaling().x,
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
    }
    setPosition(32 * main->getScaling().x, 20 * main->getScaling().y + getSize().y);
    if (isVisible() && titleText->getAlpha() > 0) {
        titleText->setFillColor(COLOR_ACCENT);
    }

    SDL_LockMutex(mutex);
    if (isVisible() && chip != nullptr) {
        const Vector2f sc = main->getScaling();
        FloatRect textBounds = messageText->getLocalBounds();
        float chipLeft = 0;
        float chipRight = messageText->getPosition().x + textBounds.width + 22 * sc.x;
        float chipWidth = std::round(chipRight - chipLeft);
        float chipHeight = std::round(textBounds.height + 20 * sc.y);
        if (chip->getSize().x != chipWidth || chip->getSize().y != chipHeight) {
            chip->setSize({chipWidth, chipHeight});
        }
        chip->setPosition(chipLeft, std::round((StatusBox::getSize().y - chipHeight) / 2));
    }
    C2DObject::onDraw(transform, draw);
    SDL_UnlockMutex(mutex);
}

void StatusBox::setAlpha(uint8_t alpha, bool recursive) {
    C2DObject::setAlpha(alpha, recursive);
}
