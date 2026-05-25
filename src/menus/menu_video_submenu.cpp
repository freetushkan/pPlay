//
// Created by cpasjuste on 07/12/18.
//

#include "cross2d/c2d.h"
#include "main.h"
#include "menu_video_submenu.h"

using namespace c2d;

MenuVideoSubmenu::MenuVideoSubmenu(
        Main *main, const c2d::FloatRect &rect, const std::string &_title,
        const std::vector<MenuItem> &items, int type) : Menu(main, rect, _title, items, false) {

    this->type = type;

    // highlight
    highlight_selection = new Highlight({MenuVideoSubmenu::getSize().x, BUTTON_HEIGHT * main->getScaling().y},
                                        Highlight::CursorPosition::Left);
    highlight_selection->setFillColor(COLOR_ACCENT);
    highlight_selection->setAlpha(60);
    highlight_selection->setCursorColor(COLOR_ACCENT);
    highlight_selection->setOrigin(Origin::Left);
    highlight_selection->setPosition(0, 200 * main->getScaling().y);
    highlight_selection->setLayer(-1);
    MenuVideoSubmenu::add(highlight_selection);
    updateSelectionHighlight();
}

void MenuVideoSubmenu::setSelection(int streamId)  {
    for (auto btn: buttons) {
        if (btn->item.id == streamId) {
            highlight_selection->setVisibility(btn->isVisible() ? Visibility::Visible : Visibility::Hidden);
            highlight_selection->tweenTo(btn->getPosition());
        }
    }
}

void MenuVideoSubmenu::updateSelectionHighlight() {
    highlight_selection->setVisibility(Visibility::Visible);
    if (type == MENU_VIDEO_TYPE_VID) {
        setSelection(main->getPlayer()->getVideoStream());
    } else if (type == MENU_VIDEO_TYPE_AUD) {
        setSelection(main->getPlayer()->getAudioStream());
    } else if (type == MENU_VIDEO_TYPE_SUB) {
        setSelection(main->getPlayer()->getSubtitleStream());
    } else if (type == MENU_VIDEO_TYPE_PL) {
        if (main->getPlayer()->isPlaylistFile()) {
            setSelection(main->getPlayer()->getMpv()->getPlaylistPos());
            return;
        }
        for (auto btn: buttons) {
            auto item = btn->item;
            if (item.id < 0) continue; 
            if (item.data_str == main->getPlayer()->getTitle()) {
                setSelection(item.id);
                break;
            }
        }
    }
}

void MenuVideoSubmenu::onOptionSelection(MenuItem *item) {
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
        } else if (type == MENU_VIDEO_TYPE_PL) {
            main->getPlayer()->getMpv()->showText("Switching playlist item...");
            if (main->getPlayer()->isPlaylistFile()) {
                main->getPlayer()->getMpv()->setPlaylistPos(item->id);
            } else {
                auto files = main->getPlayer()->getAutoplayFiles();
                if (item->id >= 0 && item->id < (int)files.size()) {
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

void MenuVideoSubmenu::onUpdate() {
    Menu::onUpdate();
    highlight_selection->setFillColor(COLOR_ACCENT);
    highlight_selection->setAlpha(60);
    highlight_selection->setCursorColor(COLOR_ACCENT);
}
