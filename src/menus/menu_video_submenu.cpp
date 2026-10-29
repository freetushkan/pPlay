//
// Created by cpasjuste on 07/12/18.
//

#include "cross2d/c2d.h"
#include "main.h"
#include "menu_video_submenu.h"
#include "pplay_config.h"
#include "utility.h"

using namespace c2d;

MenuVideoSubmenu::MenuVideoSubmenu(Main *main, const c2d::FloatRect &rect, const std::string &_title,
                                   const std::vector<MenuItem> &items, int type)
    : Menu(main, rect, _title, items, false) {

    this->type = type;
    updateSelectionHighlight();
}

void MenuVideoSubmenu::setSelection(int streamId) {
    for (auto btn: buttons) {
        if (btn->item.id == streamId) {
            showCurrent(btn);
        }
    }
}

void MenuVideoSubmenu::updateSelectionHighlight() {
    current->setVisibility(Visibility::Visible);
    if (type == MENU_VIDEO_TYPE_VID) {
        setSelection(main->getPlayer()->getVideoStream());
    } else if (type == MENU_VIDEO_TYPE_AUD) {
        setSelection(main->getPlayer()->getAudioStream());
    } else if (type == MENU_VIDEO_TYPE_SUB) {
        setSelection(main->getPlayer()->getSubtitleStream());
    } else if (type == MENU_VIDEO_TYPE_PL_MODE) {
        setSelection(main->getConfig()->getOption(OPT_AUTOPLAY_MODE)->getInteger());
    } else if (type == MENU_VIDEO_TYPE_NORMALIZE) {
        setSelection(main->getConfig()->getOption(OPT_AUDIO_NORMALIZE)->getInteger());
    } else if (type == MENU_VIDEO_TYPE_PLAYLIST) {
        if (main->getPlayer()->isPlaylistFile()) {
            setSelection(main->getPlayer()->getMpv()->getPlaylistPos());
            return;
        }
        for (auto btn: buttons) {
            auto item = btn->item;
            if (item.id < 0) {
                continue;
            }
            if (item.data_str == main->getPlayer()->getTitle()) {
                setSelection(item.id);
                break;
            }
        }
    }
}

void MenuVideoSubmenu::onOptionSelection(MenuItem *item) {
    if (item->id == MENU_TRACK_ADD && (type == MENU_VIDEO_TYPE_AUD || type == MENU_VIDEO_TYPE_SUB)) {
        setVisibility(Visibility::Hidden, true);
        main->getPlayer()->pickExternalTrack(type == MENU_VIDEO_TYPE_AUD);
        return;
    }
    if (item->position == MenuItem::Position::Top) {
        std::string name = item->name;
        std::replace(name.begin(), name.end(), '\n', ' ');
        if (type == MENU_VIDEO_TYPE_VID && main->getPlayer()->getVideoStream() != item->id) {
            main->getStatus()->show("Please Wait...",
                                    "Loading video stream: " + name + ".\nThis can take a few seconds...");
            main->getPlayer()->setVideoStream(item->id);
        } else if (type == MENU_VIDEO_TYPE_AUD && main->getPlayer()->getAudioStream() != item->id) {
            main->getStatus()->show("Please Wait...",
                                    "Loading audio stream: " + name + ".\nThis can take a few seconds...");
            main->getPlayer()->setAudioStream(item->id);
        } else if (type == MENU_VIDEO_TYPE_SUB && main->getPlayer()->getSubtitleStream() != item->id) {
            main->getStatus()->show("Please Wait...",
                                    "Loading subtitle stream: " + name + ".\nThis can take a few seconds...");
            main->getPlayer()->setSubtitleStream(item->id);
        } else if (type == MENU_VIDEO_TYPE_PL_MODE) {
            auto *option = main->getConfig()->getOption(OPT_AUTOPLAY_MODE);
            const std::string oldValue = option->getString();
            option->setType(c2d::config::Option::Type::Integer);
            option->setInteger(item->id);
            main->getConfig()->save();
            pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Options: " + std::string(OPT_AUTOPLAY_MODE)
                                                                         + " changed from " + oldValue + " to "
                                                                         + option->getString());
        } else if (type == MENU_VIDEO_TYPE_NORMALIZE) {
            main->getPlayer()->setAudioNormalize(item->id);
        } else if (type == MENU_VIDEO_TYPE_PLAYLIST) {
            main->getStatus()->show("Please Wait...", "Switching playlist item...");
            if (main->getPlayer()->isPlaylistFile()) {
                main->getPlayer()->getMpv()->setPlaylistPos(item->id);
            } else {
                auto files = main->getPlayer()->getAutoplayFiles();
                if (item->id >= 0 && item->id < (int) files.size()) {
                    main->getPlayer()->load(files[item->id]);
                }
            }
        }
        updateSelectionHighlight();
    }
}

void MenuVideoSubmenu::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    Menu::setVisibility(visibility, tweenPlay);
    updateSelectionHighlight();
}

bool MenuVideoSubmenu::onInput(c2d::Input::Player *players) {
    if (players[0].buttons & Input::Left || players[0].buttons & Input::B) {
        setVisibility(Visibility::Hidden, true);
        main->getMenuVideo()->setVisibility(Visibility::Visible, true);
        return true;
    }

    bool result = Menu::onInput(players);
    updateSelectionHighlight();
    return result;
}
