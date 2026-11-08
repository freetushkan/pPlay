//
// Created by cpasjuste on 07/12/18.
//

#include <vector>
#include "cross2d/c2d.h"
#include "main.h"
#include "menu_video.h"
#include "player_osd.h"
#include "platform.h"
#include "pplay_config.h"
#include "utility.h"
#include "input_map.h"
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
            if (n == "Speed" || n == "Brightness" || n == "Screen brightness") {
                item.slider = 0;
            }
            if (n != "Stop" && n != "Playlist") {
                item.value = "-";
            }
        }
        return items;
    }

    const char *modeNames[] = {"Stop playback", "Play next", "Loop file", "Loop directory"};
    const char *normalizeNames[] = {"Off", "Normalize", "Night mode"};
    const char *screenNames[] = {"Cover > visualizer", "Visualizer", "Icon only"};
    const char *qualityNames[] = {"Best", "Balanced", "Fast"};

    // "Auto · ..." while the playback may change it
    std::string autoValue(bool isAuto, const char *name) {
        return isAuto ? pplay::tr("Auto") + " · " + pplay::tr(name) : pplay::tr(name);
    }

    void toggleProgressLine(PPLAYConfig *config) {
        auto *option = config->getOption(OPT_PROGRESS_LINE);
        option->setType(c2d::config::Option::Type::Integer);
        option->setInteger(option->getInteger() ? 0 : 1);
        config->save();
    }

    // the values cycled in the menu (minutes), the first one: the default
    struct Choices {
        const char *option;
        std::vector<int> values;
        std::vector<const char *> names;
    };

#ifdef __PS5__
    // rest mode after that long without a key, 0 off
    const Choices g_sleep = {OPT_PS5_SLEEP_TIMER, {0, 15, 30, 45, 60, 90, 120, 180},
                             {"Off", "15 min", "30 min", "45 min", "1 h", "1.5 h", "2 h", "3 h"}};
#else
    const Choices g_sleep = {OPT_STAY_AWAKE, {0, 5, 10, 15, 30, 60, 90, 120, 180, 240},
                             {"Always while playing", "5 min", "10 min", "15 min", "30 min", "1 h", "1.5 h", "2 h",
                              "3 h", "4 h"}};
#endif

    int choiceIndex(PPLAYConfig *config, const Choices &choices) {
        int value = config->getOption(choices.option)->getInteger();
        for (size_t i = 0; i < choices.values.size(); i++) {
            if (choices.values[i] == value) {
                return (int) i;
            }
        }
        return 0;
    }

    int cycle(int value, int direction, int count);

    void changeChoice(PPLAYConfig *config, const Choices &choices, int direction) {
        auto *option = config->getOption(choices.option);
        option->setType(c2d::config::Option::Type::Integer);
        option->setInteger(choices.values[cycle(choiceIndex(config, choices), direction, (int) choices.values.size())]);
        config->save();
    }

    int cycle(int value, int direction, int count) {
        // a held key stops at the last value
        if (pplay::InputMap::isRepeat() && (value + direction < 0 || value + direction >= count)) {
            return value;
        }
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
        } else if (n == "Decoder") {
            item.value = autoValue(player->isDecoderAuto(), player->isHardwareDecoding() ? "Hardware" : "Software");
            item.valueAccent = !player->isHardwareDecoding();
        } else if (n == "Video quality") {
            item.value = autoValue(player->isQualityAuto(), qualityNames[player->getVideoQuality()]);
            item.valueAccent = player->getVideoQuality() != 0;
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
        } else if (n == "Screen brightness") {
            // docked: no backlight
            float light = pplay::Platform::backlight();
            item.value = light >= 0 ? std::to_string((int) std::lround(light * 100)) + "%" : "-";
            item.slider = std::max(0.0f, light);
        } else if (n == "Information") {
            item.value = pplay::tr(player->isInfoVisible() ? "On" : "Off");
            item.valueAccent = player->isInfoVisible();
        } else if (n == "Progress line") {
            item.value = pplay::tr(config->getOption(OPT_PROGRESS_LINE)->getInteger() ? "On" : "Off");
        } else if (n == "Prevent sleep" || n == "Sleep timer") {
            int index = choiceIndex(config, g_sleep);
            item.value = pplay::tr(g_sleep.names[index]);
            item.valueAccent = index != 0;
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
    } else if (n == "Decoder") {
        player->setHardwareDecoding(!player->isHardwareDecoding());
    } else if (n == "Video quality") {
        player->setVideoQuality(cycle(player->getVideoQuality(), direction, 3));
    } else if (n == "Speed") {
        double speed = std::round((player->getMpv()->getSpeed() + 0.05 * direction) * 100.0) / 100.0;
        player->setSpeed(std::max(0.25, std::min(3.0, speed)));
    } else if (n == "Brightness") {
        player->changeBrightness(direction);
    } else if (n == "Screen brightness") {
        player->changeBacklight(direction);
    } else if (n == "Information") {
        player->setInfoVisible(!player->isInfoVisible());
    } else if (n == "Progress line") {
        toggleProgressLine(config);
    } else if (n == "Prevent sleep" || n == "Sleep timer") {
        changeChoice(config, g_sleep, direction);
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
            main->getStatus()->show("Info", "No video streams found in media", false, false);
        }
    } else if (item->name == "Audio") {
        if (main->getPlayer()->getMenuAudioStreams()) {
            main->getPlayer()->getMenuAudioStreams()->setVisibility(Visibility::Visible, true);
            setVisibility(Visibility::Hidden, true);
        } else {
            main->getStatus()->show("Info", "No audio streams found in media", false, false);
        }
    } else if (item->name == "Subtitles") {
        if (main->getPlayer()->getMenuSubtitlesStreams()) {
            main->getPlayer()->getMenuSubtitlesStreams()->setVisibility(Visibility::Visible, true);
            setVisibility(Visibility::Hidden, true);
        } else {
            main->getStatus()->show("Info", "No subtitles streams found in media", false, false);
        }
    } else if (item->name == "Playlist") {
        if (main->getPlayer()->getMenuPlaylist()) {
            main->getPlayer()->getMenuPlaylist()->setVisibility(Visibility::Visible, true);
            setVisibility(Visibility::Hidden, true);
        } else {
            main->getStatus()->show("Info", "Playlist is empty", false, false);
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
    } else if (item->name == "Decoder") {
        main->getPlayer()->setHardwareDecoding(!main->getPlayer()->isHardwareDecoding());
        refreshValues();
    } else if (item->name == "Video quality") {
        main->getPlayer()->setVideoQuality(cycle(main->getPlayer()->getVideoQuality(), 1, 3));
        refreshValues();
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
    } else if (item->name == "Progress line") {
        toggleProgressLine(main->getConfig());
        refreshValues();
    } else if (item->name == "Prevent sleep" || item->name == "Sleep timer") {
        changeChoice(main->getConfig(), g_sleep, 1);
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
    return Menu::onInput(players);
}
