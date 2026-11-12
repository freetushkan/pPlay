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

StatusBox::StatusBox(Main *m) : Rectangle({0, 0, 64, 64}) {

    main = m;
    const Vector2f sc = m->getScaling();
    StatusBox::setSize(m->getSize().x, std::round(32 * sc.y));
    // Main sets BottomLeft as the origin, so position the bottom edge below the top margin.
    StatusBox::setPosition(0, std::round(18 * sc.y + StatusBox::getSize().y));

    // a soft shadow (the filer: over the path line)
    for (int i = 0; i < 3; i++) {
        shadows[i] = new RoundRect({10, std::round(26 * sc.y)});
        shadows[i]->setOrigin(Origin::Left);
        StatusBox::add(shadows[i]);
    }
    chip = new RoundRect({10, std::round(26 * sc.y)});
    chip->setOrigin(Origin::Left);
    chip->setFillColor(Color(10, 12, 16, 170));
    StatusBox::add(chip);

    icon = new C2DTexture(m->getIo()->getDataPath() + "skin/wait.png");
    icon->setOrigin(Origin::Center);
    icon->setFillColor(COLOR_FONT);
    icon->setAlpha(255);
    icon->add(new TweenRotation(360, 0, 2, TweenLoop::Loop, TweenState::Playing));
    StatusBox::add(icon);

    messageText = new Text("Wait", m->getFontSize(Main::FontSize::Medium), m->getFont());
    messageText->setFillColor(COLOR_FONT);
    messageText->setOrigin(Origin::Left);
    messageText->setOverflow(Text::Clamp);
    messageText->setPosition(0, StatusBox::getSize().y / 2);
    StatusBox::add(messageText);

    clock = new C2DClock();
    mutex = SDL_CreateMutex();

    StatusBox::setVisibility(Visibility::Hidden);
}

StatusBox::~StatusBox() {
    delete clock;
    SDL_DestroyMutex(mutex);
}

void StatusBox::setVisibility(Visibility visibility, bool tweenPlay) {
    target = visibility == Visibility::Visible ? 1.0f : 0.0f;
    if (!tweenPlay) {
        alpha = target;
    }
    if (visibility == Visibility::Visible || alpha <= 0) {
        Rectangle::setVisibility(visibility, false);
    }
    applyAlpha();
}

void StatusBox::applyAlpha() {
    uint8_t a = (uint8_t) std::lround(255 * alpha);
    // over the player: translucent dark; over the filer: opaque gray with a shadow; an error: red
    bool player = main->getPlayer() != nullptr && main->getPlayer()->isFullscreen();
    Color base = error ? Color(170, 48, 44, 255) : player ? Color(10, 12, 16, 170) : Color(44, 48, 54, 255);
    chip->setFillColor({base.r, base.g, base.b, (uint8_t) std::lround(base.a * alpha)});
    for (int i = 0; i < 3; i++) {
        uint8_t shade = player ? 0 : (uint8_t) std::lround((70 - 20 * i) * alpha);
        shadows[i]->setFillColor({0, 0, 0, shade});
    }
    icon->setAlpha(a);
    messageText->setFillColor({COLOR_FONT.r, COLOR_FONT.g, COLOR_FONT.b, a});
}

void StatusBox::show(const std::string &type, const std::string &message, bool inf, bool drawNow) {

    SDL_LockMutex(mutex);
    messageText->setString(pplay::tr(message));
    error = type == "Error";
    bool spinning = inf || type == "Wait";
    icon->setVisibility(spinning ? Visibility::Visible : Visibility::Hidden);
    applyAlpha();
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

void StatusBox::onUpdate() {
    if (!isVisible()) {
        return;
    }

    float dt = std::min(0.1f, fadeClock.restart().asSeconds());
    if (alpha != target) {
        alpha += (target > alpha ? 1 : -1) * dt * 4;
        alpha = std::min(1.0f, std::max(0.0f, alpha));
        if (std::fabs(alpha - target) < 0.02f) {
            alpha = target;
        }
        applyAlpha();
        if (alpha <= 0) {
            Rectangle::setVisibility(Visibility::Hidden, false);
            return;
        }
    }

    const Vector2f sc = main->getScaling();
    bool player = main->getPlayer() != nullptr && main->getPlayer()->isFullscreen();

    // Match StatusBar's top edge; this object's origin is BottomLeft.
    float top = std::round((player ? 18 : 6) * sc.y);
    setPosition(0, top + getSize().y);

    float centerY = getSize().y / 2;
    float iconSize = std::round(22 * sc.y);
    float left = std::round(22 * sc.x);
    float padding = std::round(14 * sc.x);
    float currentX = left + padding;

    if (icon->isVisible()) {
        float iconScale = iconSize / (float) std::max(1, icon->getTextureSize().y);
        icon->setScale(iconScale, iconScale);
        currentX += iconSize + std::round(10 * sc.x);
    }

    // Keep a gap before the StatusBar clock chip.
    float maxRight = getSize().x;
    StatusBar *bar = main->getStatusBar();
    if (bar != nullptr && bar->timeText != nullptr) {
        FloatRect barText = bar->timeText->getLocalBounds();
        float barLeft = bar->timeText->getPosition().x - (barText.width * bar->timeText->getScale().x)
                        - (16 * sc.x);
        maxRight = std::min(maxRight, barLeft - (8 * sc.x));
    }

    float maxText = std::max(1.0f, maxRight - currentX - padding);
    if (std::fabs(maxText - textMaxWidth) > 0.5f) {
        textMaxWidth = maxText;
        messageText->setSizeMax(textMaxWidth, 0);
    }

    FloatRect text = messageText->getLocalBounds();
    float textWidth = std::round(text.width * messageText->getScale().x);
    float right = std::min(maxRight, currentX + textWidth + padding);

    if (icon->isVisible()) {
        icon->setPosition(std::round(left + padding + iconSize / 2), centerY);
    }

    messageText->setPosition(currentX, centerY);
    unsigned int fontSize = main->getFontSize(Main::FontSize::Medium);
    Vector2f chipSize = {std::round(std::max(1.0f, right - left)), std::round(fontSize + 20 * sc.y)};
    if (chip->getSize().x != chipSize.x || chip->getSize().y != chipSize.y) {
        chip->setSize(chipSize);
        for (int i = 0; i < 3; i++) {
            float grow = std::round((2 + 2 * i) * sc.y);
            shadows[i]->setSize(chipSize.x + 2 * grow, chipSize.y + 2 * grow);
        }
    }
    chip->setPosition(left, std::round(centerY));
    for (int i = 0; i < 3; i++) {
        float grow = std::round((2 + 2 * i) * sc.y);
        shadows[i]->setPosition(left - grow, std::round(centerY + sc.y));
    }
    // the player switched meanwhile: its colors
    if (player != wasPlayer) {
        wasPlayer = player;
        applyAlpha();
    }
    Rectangle::onUpdate();
}

void StatusBox::onDraw(c2d::Transform &transform, bool draw) {
    if (isVisible() && !infinite && clock->getElapsedTime().asSeconds() > 2) {
        setVisibility(Visibility::Hidden, true);
    }
    SDL_LockMutex(mutex);
    C2DObject::onDraw(transform, draw);
    SDL_UnlockMutex(mutex);
}

void StatusBox::setAlpha(uint8_t alpha, bool recursive) {
    C2DObject::setAlpha(alpha, recursive);
}
