//
// Created by cpasjuste on 07/12/18.
//

#include "cross2d/c2d.h"
#include "main.h"
#include "menu_video.h"
#include "player_osd.h"
#include "pplay_config.h"
#include "utility.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace c2d;

namespace {
    // what each item shows on its right
    std::vector<MenuItem> withValues(std::vector<MenuItem> items) {
        for (auto &item: items) {
            const std::string &n = item.name;
            if (n == "Playlist" || n == "Audio" || n == "Video" || n == "Subtitles") {
                item.opens = true;
            }
            if (n == "Speed" || n == "Brightness") {
                item.slider = 0;
            }
            if (n != "Stop" && n != "Playlist") {
                item.value = "-";
            }
        }
        return items;
    }

    const char *modeNames[] = {"Stop", "Play next", "Loop file", "Loop directory"};
    const char *normalizeNames[] = {"Off", "Normalize", "Night mode"};
    const char *screenNames[] = {"Cover > visualizer", "Visualizer", "Icon only"};

    int cycle(int value, int direction, int count) {
        return (value + direction + count) % count;
    }
}

MenuVideo::MenuVideo(Main *main, const c2d::FloatRect &rect, const std::vector<MenuItem> &items)
    : Menu(main, rect, "Playback options", withValues(items), false) {}

void MenuVideo::refreshValues() {
    Player *player = main->getPlayer();
    if (player == nullptr || player->getMpv() == nullptr) {
        return;
    }
    Mpv *mpv = player->getMpv();
    PPLAYConfig *config = main->getConfig();
    auto track = [mpv](const std::string &type) {
        const std::string prefix = "current-tracks/" + type + "/";
        long id = mpv->getInt((prefix + "id").c_str(), -1);
        if (id < 0) {
            return pplay::tr("Off");
        }
        std::string lang = c2d::Utility::toUpper(mpv->getString((prefix + "lang").c_str()));
        std::string codec = c2d::Utility::toUpper(mpv->getString((prefix + "codec").c_str()));
        std::string out = lang.empty() ? "#" + std::to_string(id) : lang;
        if (type == "video") {
            long height = mpv->getInt("video-params/h", 0);
            out = height > 0 ? std::to_string(height) + "p" : out;
        }
        return codec.empty() ? out : out + " · " + codec;
    };
    for (auto *button: buttons) {
        MenuItem &item = button->item;
        const std::string &n = item.name;
        if (n == "Playback mode") {
            int mode = std::max(0, std::min(3, config->getOption(OPT_AUTOPLAY_MODE)->getInteger()));
            item.value = pplay::tr(modeNames[mode]);
            item.valueAccent = mode >= 2;
        } else if (n == "Audio" || n == "Video") {
            item.value = track(n == "Audio" ? "audio" : "video");
        } else if (n == "Subtitles") {
            item.value = track("sub");
        } else if (n == "Volume normalization") {
            int mode = std::max(0, std::min(2, config->getOption(OPT_AUDIO_NORMALIZE)->getInteger()));
            item.value = pplay::tr(normalizeNames[mode]);
            item.valueAccent = mode != 0;
        } else if (n == "Visualization") {
            int mode = std::max(0, std::min(2, config->getOption(OPT_AUDIO_VISUALIZER)->getInteger()));
            item.value = pplay::tr(screenNames[mode]);
        } else if (n == "Speed") {
            double speed = mpv->getSpeed();
            char text[16];
            snprintf(text, sizeof(text), "%.2fx", speed);
            item.value = text;
            item.valueAccent = std::fabs(speed - 1.0) > 0.001;
            item.slider = (float) ((speed - 0.25) / (3.0 - 0.25));
        } else if (n == "Brightness") {
            int brightness = (int) mpv->getDouble("brightness", 0);
            item.value = std::to_string(brightness);
            item.valueAccent = brightness != 0;
            item.slider = (float) (brightness + 100) / 200.0f;
        } else if (n == "Information") {
            item.value = pplay::tr(player->isInfoVisible() ? "On" : "Off");
            item.valueAccent = player->isInfoVisible();
        }
        button->refreshValue();
    }
}

void MenuVideo::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    Menu::setVisibility(visibility, tweenPlay);
    if (visibility == Visibility::Visible) {
        refreshValues();
    }
}

void MenuVideo::onUpdate() {
    if (isVisible() && valuesClock.getElapsedTime().asSeconds() >= 0.5f) {
        valuesClock.restart();
        refreshValues();
    }
    Menu::onUpdate();
}

bool MenuVideo::onOptionChange(MenuItem *item, int direction) {
    Player *player = main->getPlayer();
    PPLAYConfig *config = main->getConfig();
    const std::string &n = item->name;
    if (n == "Playback mode") {
        auto *option = config->getOption(OPT_AUTOPLAY_MODE);
        option->setType(c2d::config::Option::Type::Integer);
        option->setInteger(cycle(option->getInteger(), direction, 4));
        config->save();
    } else if (n == "Volume normalization") {
        player->setAudioNormalize(cycle(config->getOption(OPT_AUDIO_NORMALIZE)->getInteger(), direction, 3));
    } else if (n == "Visualization") {
        player->setAudioScreenMode(cycle(config->getOption(OPT_AUDIO_VISUALIZER)->getInteger(), direction, 3));
    } else if (n == "Speed") {
        double speed = std::round((player->getMpv()->getSpeed() + 0.05 * direction) * 100.0) / 100.0;
        player->setSpeed(std::max(0.25, std::min(3.0, speed)));
    } else if (n == "Brightness") {
        player->changeBrightness(direction);
    } else if (n == "Information") {
        player->setInfoVisible(!player->isInfoVisible());
    } else {
        return false;
    }
    refreshValues();
    return true;
}

void MenuVideo::onOptionSelection(MenuItem *item) {

    if (item->name == "Video") {
        if (main->getPlayer()->getMenuVideoStreams()) {
            main->getPlayer()->getMenuVideoStreams()->setVisibility(Visibility::Visible, true);
            setVisibility(Visibility::Hidden, true);
        } else {
            main->getStatus()->show("Information...", "No video streams found in media", false, false);
        }
    } else if (item->name == "Audio") {
        if (main->getPlayer()->getMenuAudioStreams()) {
            main->getPlayer()->getMenuAudioStreams()->setVisibility(Visibility::Visible, true);
            setVisibility(Visibility::Hidden, true);
        } else {
            main->getStatus()->show("Information...", "No audio streams found in media", false, false);
        }
    } else if (item->name == "Subtitles") {
        if (main->getPlayer()->getMenuSubtitlesStreams()) {
            main->getPlayer()->getMenuSubtitlesStreams()->setVisibility(Visibility::Visible, true);
            setVisibility(Visibility::Hidden, true);
        } else {
            main->getStatus()->show("Information...", "No subtitles streams found in media", false, false);
        }
    } else if (item->name == "Playlist") {
        if (main->getPlayer()->getMenuPlaylist()) {
            main->getPlayer()->getMenuPlaylist()->setVisibility(Visibility::Visible, true);
            setVisibility(Visibility::Hidden, true);
        } else {
            main->getStatus()->show("Information...", "Playlist is empty", false, false);
        }
    } else if (item->name == "Playback mode") {
        if (main->getPlayer()->getMenuPlaybackMode()) {
            main->getPlayer()->getMenuPlaybackMode()->setVisibility(Visibility::Visible, true);
            setVisibility(Visibility::Hidden, true);
        }
    } else if (item->name == "Volume normalization") {
        if (main->getPlayer()->getMenuNormalize()) {
            main->getPlayer()->getMenuNormalize()->setVisibility(Visibility::Visible, true);
            setVisibility(Visibility::Hidden, true);
        }
    } else if (item->name == "Visualization") {
        if (main->getPlayer()->getMenuVisualizer()) {
            main->getPlayer()->getMenuVisualizer()->setVisibility(Visibility::Visible, true);
            setVisibility(Visibility::Hidden, true);
        }
    } else if (item->name == "Speed") {
        // A: back to the default
        main->getPlayer()->setSpeed(1);
        refreshValues();
    } else if (item->name == "Brightness") {
        main->getPlayer()->resetBrightness();
        refreshValues();
    } else if (item->name == "Information") {
        main->getPlayer()->setInfoVisible(!main->getPlayer()->isInfoVisible());
        refreshValues();
    } else if (item->name == "Stop") {
        main->getPlayer()->stop();
        setVisibility(Visibility::Hidden, true);
    }
}

bool MenuVideo::onInput(c2d::Input::Player *players) {
    unsigned int keys = players[0].buttons;
    if (keys & Input::B) {
        setVisibility(Visibility::Hidden, true);
        return true;
    }
    // right on a list item: its submenu (left / right change the values otherwise)
    MenuItem *item = getSelection();
    if ((keys & Input::Right) && item->opens) {
        onOptionSelection(item);
        return true;
    }
    return Menu::onInput(players);
}
