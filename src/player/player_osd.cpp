//
// Created by cpasjuste on 12/12/18.
//

#include <algorithm>
#include "main.h"
#include "utility.h"
#include "player_osd.h"
#include "encodings.h"

using namespace c2d;

#define OSD_HEIGHT 80.0f
#define OSD_HIDE_TIME 7.0f

PlayerOSD::PlayerOSD(Main *m) : GradientRectangle({0, 0, 64, 64}) {

    main = m;

    setColor(COLOR_BG, COLOR_BG_ALPHA, Direction::Up);
    PlayerOSD::setSize(main->getSize().x, OSD_HEIGHT * main->getScaling().y);
    PlayerOSD::setPosition(main->getSize().x / 2, main->getSize().y + PlayerOSD::getSize().y);
    PlayerOSD::setOrigin(Origin::Bottom);

    highlight = new Highlight({PlayerOSD::getSize().y, 64 * main->getScaling().y});
    highlight->setOrigin(Origin::Left);
    highlight->setRotation(90);
    highlight->setPosition(64 * main->getScaling().x, 0);
    PlayerOSD::add(highlight);

    progress = new Progress({0, 0, PlayerOSD::getSize().x, 6 * main->getScaling().y});
    progress->setFgColor(COLOR_ACCENT);
    progress->setBgColor(Color(64, 68, 72, 255));
    PlayerOSD::add(progress);

    // buffered (cache) part, under the played one
    buffered = new RectangleShape({1, 1, 0, progress->getSize().y - 2});
    buffered->setFillColor({255, 255, 255, 120});
    progress->add(buffered);
    buffered->setLayer(-1); // sorted once added

    // controls in the middle, the times by the sides, settings and stop on the right
    const Vector2f sc = main->getScaling();
    auto addButton = [this, sc](const char *file, float x) {
        auto *button = new C2DTexture(main->getIo()->getDataPath() + "skin/" + file);
        // 48 px at 720p, whatever the image size
        float scale = 48.0f * sc.y / (float) button->getTextureSize().x;
        button->setScale(scale, scale);
        button->setPosition(x, PlayerOSD::getSize().y / 2);
        button->setOrigin(Origin::Center);
        PlayerOSD::add(button);
        return button;
    };
    const float step = 72 * sc.x;
    const float center = PlayerOSD::getSize().x / 2;
    const float right = PlayerOSD::getSize().x - 48 * sc.x;
    btn_pause = addButton("btn_pause.png", center);
    btn_play = addButton("btn_play.png", center);
    btn_play->setVisibility(Visibility::Hidden);
    buttons = {addButton("btn_prev.png", center - step * 3),
               addButton("btn_seek_backward_long.png", center - step * 2),
               addButton("btn_seek_backward_short.png", center - step),
               btn_pause,
               addButton("btn_seek_forward_short.png", center + step),
               addButton("btn_seek_forward_long.png", center + step * 2),
               addButton("btn_next.png", center + step * 3),
               addButton("btn_settings.png", right - step),
               addButton("btn_stop.png", right)};

    progress_text = new Text("00:00:00 / 00:00:00", main->getFontSize(Main::FontSize::Big), main->getFont());
    progress_text->setOrigin(Origin::Left);
    progress_text->setPosition(24 * sc.x, PlayerOSD::getSize().y / 2);
    PlayerOSD::add(progress_text);

    // remaining, at the playback speed
    duration_text = new Text("-00:00:00", main->getFontSize(Main::FontSize::Big), main->getFont());
    duration_text->setOrigin(Origin::Right);
    duration_text->setPosition(right - step - 48 * sc.x, PlayerOSD::getSize().y / 2);
    PlayerOSD::add(duration_text);

    // TITLE
    title = new Text("Unknown Title", main->getFontSize(Main::FontSize::XL), main->getFont());
    title->setFillColor(COLOR_FONT);
    title->setOrigin(Origin::Left);
    title->setOutlineColor(COLOR_GRAY_DARK);
    title->setOutlineThickness(5.0f);
    title->setPosition((20 * main->getScaling().x), (-static_cast<int>(Main::FontSize::XL) * main->getScaling().y));
    title->setSizeMax(PlayerOSD::getSize().x - (20 * main->getScaling().x), 0);
    PlayerOSD::add(title);
    titleTween = new TweenAlpha(0, 255, 0.5f);
    title->add(titleTween);

    PlayerOSD::add(new TweenPosition({PlayerOSD::getPosition().x, PlayerOSD::getPosition().y},
                                     {PlayerOSD::getPosition().x, PlayerOSD::getPosition().y - PlayerOSD::getSize().y},
                                     0.5f));

    PlayerOSD::setVisibility(Visibility::Hidden);

    clock.restart();
}

void PlayerOSD::setVisibility(c2d::Visibility visibility, bool tweenPlay) {

    if (visibility == Visibility::Visible) {
        index = (int) ButtonID::Pause;
        highlight->tweenTo({buttons.at((size_t) index)->getPosition().x, 0});
        if (main->getPlayer() != nullptr) {
            title->setString(encoding::fix(main->getPlayer()->getTitle()));
        }
        if (tweenPlay && titleTween) {
            titleTween->play(TweenDirection::Forward);
        }
    } else if (visibility == Visibility::Hidden) {
        if (tweenPlay && titleTween) {
            titleTween->play(TweenDirection::Backward);
        }
    }

    clock.restart();
    GradientRectangle::setVisibility(visibility, tweenPlay);
}

void PlayerOSD::onDraw(c2d::Transform &transform, bool draw) {

    Player *player = main->getPlayer();
    Mpv *mpv = player->getMpv();

    if (!player->isFullscreen()) {
        setVisibility(Visibility::Hidden, false);
        return;
    }

    if (mpv->isStopped()) {
        // (re)loading: keep the OSD state (and its auto hide timer), just don't draw it
        clock.restart();
        return;
    }

    bool hasVideo = player->hasVideo();
    if (hasVideo && !mpv->isPaused() && clock.getElapsedTime().asSeconds() >= OSD_HIDE_TIME) {
        setVisibility(Visibility::Hidden, true);
        main->getStatusBar()->setVisibility(Visibility::Hidden, true);
    }

    position = (float) mpv->getPosition();
    duration = (float) mpv->getDuration();
    progress->setProgress(duration > 0 ? position / duration : 0);
    if (duration > 0) {
        double cacheEnd = std::max(0.0, mpv->getTime()) + std::max(0.0, mpv->getCacheDuration());
        float width = (float) std::min(1.0, cacheEnd / duration) * (progress->getSize().x - 2);
        buffered->setSize(width, buffered->getSize().y);
    } else {
        buffered->setSize(0, buffered->getSize().y);
    }
    progress->setFgColor(COLOR_ACCENT);
    progress_text->setString(pplay::Utility::formatTime(position) + " / " + pplay::Utility::formatTime(duration));
    double speed = std::max(0.01, mpv->getSpeed());
    duration_text->setString("-" + pplay::Utility::formatTime(std::max(0.0, (duration - position) / speed)));
    highlight->setFillColor(COLOR_HIGHLIGHT);
    highlight->setCursorColor(COLOR_ACCENT);

    GradientRectangle::onDraw(transform, draw);
}

bool PlayerOSD::onInput(c2d::Input::Player *players) {

    Player *player = main->getPlayer();
    Mpv *mpv = player->getMpv();

    auto hideOSD = [&]() {
        setVisibility(Visibility::Hidden, true);
        main->getStatusBar()->setVisibility(Visibility::Hidden, true);
    };

    unsigned int keys = players[0].buttons;
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                        "PlayerOSD::onInput keys=" + pplay::Utility::getKeysString(keys));
    if (!keys) {
        return true;
    }

    if (keys & Input::B) {
        if (!player->hasVideo()) {
            player->setFullscreen(false);
        } else {
            hideOSD();
        }
    } else if (keys & Input::X) {
        hideOSD();
        main->getMenuVideo()->setVisibility(Visibility::Visible, true);
    }

    else if (keys & (Input::Left | Input::Right)) {
        int dir = (keys & Input::Right) ? 1 : -1;
        index = (index + dir + (int) buttons.size()) % (int) buttons.size();
        highlight->tweenTo({buttons.at((size_t) index)->getPosition().x, 0});
    }

    else if (keys & Input::A) {
        auto cfg = main->getConfig();
        float s_short = cfg->getOption(OPT_SEEK_SHORT_SEC)->getFloat();
        float s_long = cfg->getOption(OPT_SEEK_LONG_SEC)->getFloat();

        switch ((ButtonID) index) {
            case ButtonID::Pause: {
                bool pause = !mpv->isPaused();
                btn_play->setVisibility(pause ? Visibility::Visible : Visibility::Hidden);
                btn_pause->setVisibility(pause ? Visibility::Hidden : Visibility::Visible);
                main->getStatus()->show("Info...", pause ? "Pausing playback..." : "Resuming playback...");
                pause ? player->pause() : player->resume();
                break;
            }
            case ButtonID::SeekForwardShort:
                mpv->seek(s_short);
                break;
            case ButtonID::SeekForwardLong:
                mpv->seek(s_long);
                break;
            case ButtonID::SeekBackwardShort:
                mpv->seek(-s_short);
                break;
            case ButtonID::SeekBackwardLong:
                mpv->seek(-s_long);
                break;
            case ButtonID::Previous:
            case ButtonID::Next:
                hideOSD();
                player->playSibling((ButtonID) index == ButtonID::Next ? 1 : -1);
                break;
            case ButtonID::Settings:
                hideOSD();
                main->getMenuVideo()->setVisibility(Visibility::Visible, true);
                break;
            case ButtonID::Stop:
                hideOSD();
                main->getStatus()->show("Info...", "Stopping playback...");
                player->stop();
                break;
        }

        clock.restart();
    }

    return true;
}

void PlayerOSD::reset() {
    index = (int) ButtonID::Pause;
    highlight->tweenTo({buttons.at((size_t) index)->getPosition().x, 0});
    btn_play->setVisibility(Visibility::Hidden);
    buttons.at((int) ButtonID::Pause)->setVisibility(Visibility::Visible);
    clock.restart();
}
