//
// Created by cpasjuste on 12/12/18.
//

#include "main.h"
#include "utility.h"
#include "player_osd.h"

using namespace c2d;

#define OSD_HEIGHT      80.0f
#define OSD_HIDE_TIME   4.0f

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
    progress->setBgColor(COLOR_GRAY);
    PlayerOSD::add(progress);

    // left buttons
    btn_pause = new C2DTexture(main->getIo()->getRomFsPath() + "skin/btn_pause.png");
    btn_pause->setScale(main->getScaling());
    btn_pause->setPosition(64 * 1 * main->getScaling().x, PlayerOSD::getSize().y / 2);
    btn_pause->setOrigin(Origin::Center);
    PlayerOSD::add(btn_pause);
    buttons.push_back(btn_pause);

    btn_play = new C2DTexture(main->getIo()->getRomFsPath() + "skin/btn_play.png");
    btn_play->setScale(main->getScaling());
    btn_play->setPosition(buttons.at((size_t) ButtonID::Pause)->getPosition().x, PlayerOSD::getSize().y / 2);
    btn_play->setOrigin(Origin::Center);
    btn_play->setVisibility(Visibility::Hidden);
    PlayerOSD::add(btn_play);

    auto btn = new C2DTexture(main->getIo()->getRomFsPath() + "skin/btn_seek_backward_long.png");
    btn->setScale(main->getScaling());
    btn->setPosition(64 * 2 * main->getScaling().x, PlayerOSD::getSize().y / 2);
    btn->setOrigin(Origin::Center);
    PlayerOSD::add(btn);
    buttons.push_back(btn);

    btn = new C2DTexture(main->getIo()->getRomFsPath() + "skin/btn_seek_backward_short.png");
    btn->setScale(main->getScaling());
    btn->setPosition(64 * 3 * main->getScaling().x, PlayerOSD::getSize().y / 2);
    btn->setOrigin(Origin::Center);
    PlayerOSD::add(btn);
    buttons.push_back(btn);

    progress_text = new Text("00:00:00", main->getFontSize(Main::FontSize::Big), main->getFont());
    progress_text->setOrigin(Origin::Left);
    progress_text->setPosition(64 * 4 * main->getScaling().x, PlayerOSD::getSize().y / 2);
    PlayerOSD::add(progress_text);

    // right buttons
    duration_text = new Text("00:00:00", main->getFontSize(Main::FontSize::Big), main->getFont());
    duration_text->setOrigin(Origin::Right);
    duration_text->setPosition(PlayerOSD::getSize().x - (64 * 4 * main->getScaling().x),
            PlayerOSD::getSize().y / 2);
    PlayerOSD::add(duration_text);

    btn = new C2DTexture(main->getIo()->getRomFsPath() + "skin/btn_seek_forward_short.png");
    btn->setScale(main->getScaling());
    btn->setPosition(PlayerOSD::getSize().x - (64 * 3 * main->getScaling().x), PlayerOSD::getSize().y / 2);
    btn->setOrigin(Origin::Center);
    PlayerOSD::add(btn);
    buttons.push_back(btn);

    btn = new C2DTexture(main->getIo()->getRomFsPath() + "skin/btn_seek_forward_long.png");
    btn->setScale(main->getScaling());
    btn->setPosition(PlayerOSD::getSize().x - (64 * 2 * main->getScaling().x), PlayerOSD::getSize().y / 2);
    btn->setOrigin(Origin::Center);
    PlayerOSD::add(btn);
    buttons.push_back(btn);

    btn = new C2DTexture(main->getIo()->getRomFsPath() + "skin/btn_stop.png");
    btn->setScale(main->getScaling());
    btn->setPosition(PlayerOSD::getSize().x - (64 * 1 * main->getScaling().x), PlayerOSD::getSize().y / 2);
    btn->setOrigin(Origin::Center);
    PlayerOSD::add(btn);
    buttons.push_back(btn);

    // TITLE
    title = new Text("Unknown Title", main->getFontSize(Main::FontSize::XL), main->getFont());
    title->setFillColor(COLOR_FONT);
    title->setOrigin(Origin::Left);
    title->setOutlineColor(COLOR_GRAY_DARK);
    title->setOutlineThickness(5.0f);
    title->setPosition((20 * main->getScaling().x), (-static_cast<int>(Main::FontSize::XL) * main->getScaling().y));
    title->setSizeMax(PlayerOSD::getSize().x - (20 * main->getScaling().x), 0);
    PlayerOSD::add(title);

    PlayerOSD::add(new TweenPosition({PlayerOSD::getPosition().x, PlayerOSD::getPosition().y},
                                     {PlayerOSD::getPosition().x, PlayerOSD::getPosition().y - PlayerOSD::getSize().y},
                                     0.5f));

    PlayerOSD::setVisibility(Visibility::Hidden);

    clock.restart();
}

void PlayerOSD::setVisibility(c2d::Visibility visibility, bool tweenPlay) {

    if (visibility == Visibility::Visible) {
        index = 0;
        highlight->tweenTo({buttons.at((size_t) index)->getPosition().x, 0});
        if (main->getPlayer() != nullptr) {
            title->setString(main->getPlayer()->getTitle());
        }
    }

    clock.restart();
    GradientRectangle::setVisibility(visibility, tweenPlay);
}

void PlayerOSD::onDraw(c2d::Transform &transform, bool draw) {

    Player *player = main->getPlayer();
    Mpv *mpv = player->getMpv();

    if (mpv->isStopped() || !player->isFullscreen()) {
        setVisibility(Visibility::Hidden, false);
        return;
    }

    if (mpv->getSpeed() != 1) {
        clock.restart();
    }

    if (!mpv->isPaused() && clock.getElapsedTime().asSeconds() >= OSD_HIDE_TIME) {
        setVisibility(Visibility::Hidden, true);
        main->getStatusBar()->setVisibility(Visibility::Hidden, true);
    }

    position = (float) mpv->getPosition();
    duration = (float) mpv->getDuration();
    progress->setProgress(position / duration);
    progress_text->setString(pplay::Utility::formatTime(position));
    duration_text->setString(pplay::Utility::formatTime(duration));

    GradientRectangle::onDraw(transform, draw);
}

bool PlayerOSD::onInput(c2d::Input::Player *players) {

    Player *player = main->getPlayer();
    Mpv *mpv = player->getMpv();

    auto hideOSD = [&]() {
        setVisibility(Visibility::Hidden, true);
        main->getStatusBar()->setVisibility(Visibility::Hidden, true);
    };

    if (mpv->isStopped() || !player->isFullscreen()) {
        hideOSD();
        return true;
    }

    // unsigned int keys = players[0].buttons;
    unsigned int keys = main->getInput()->getButtons(0); 
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
        "Player::onInput keys=" + pplay::Utility::getKeysString(keys));
    if (!keys) return true;

    if (keys & (Input::Up | Input::Down)) {
        int delta = (keys & Input::Up) ? 1 : -1;
        if (keys & Input::Y) {
            mpv->changeBrightness(delta);
        } else {
            mpv->changeVolume(delta);
        }
    } else if (keys & Input::B) {
        hideOSD();
    } else if (keys & Input::X) {
        hideOSD();
        main->getMenuVideo()->setVisibility(Visibility::Visible, true);
    }

    else if (keys & (Input::Left | Input::Right)) {
        int dir = (keys & Input::Right) ? 1 : -1;
        index = (index + dir + (int)buttons.size()) % (int)buttons.size();
        highlight->tweenTo({buttons.at((size_t)index)->getPosition().x, 0});
    }

    else if (keys & Input::A) {
        auto cfg = main->getConfig();
        float s_short = cfg->getOption(OPT_SEEK_SHORT)->getFloat() * 60.0f;
        float s_long = cfg->getOption(OPT_SEEK_LONG)->getFloat() * 60.0f;

        switch ((ButtonID)index) {
            case ButtonID::Pause: {
                bool pause = !mpv->isPaused();
                btn_play->setVisibility(pause ? Visibility::Visible : Visibility::Hidden);
                btn_pause->setVisibility(pause ? Visibility::Hidden : Visibility::Visible);
                mpv->showText(pause ? "Pausing playback..." : "Resuming playback...");
                pause ? player->pause() : player->resume();
                break;
            }
            case ButtonID::SeekForwardShort:  mpv->seek(s_short); break;
            case ButtonID::SeekForwardLong:   mpv->seek(s_long); break;
            case ButtonID::SeekBackwardShort: mpv->seek(-s_short); break;
            case ButtonID::SeekBackwardLong:  mpv->seek(-s_long); break;
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
    index = 0;
    highlight->tweenTo({buttons.at((size_t) index)->getPosition().x, 0});
    btn_play->setVisibility(Visibility::Hidden);
    buttons.at((int) ButtonID::Pause)->setVisibility(Visibility::Visible);
    clock.restart();
}
