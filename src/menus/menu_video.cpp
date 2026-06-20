//
// Created by cpasjuste on 07/12/18.
//

#include "cross2d/c2d.h"
#include "main.h"
#include "menu_video.h"
#include "player_osd.h"

using namespace c2d;

MenuVideo::MenuVideo(Main *main, const c2d::FloatRect &rect, const std::vector<MenuItem> &items)
        : Menu(main, rect, "Video options", items, false) {
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
    } else if (item->name == "Stop") {
        main->getPlayer()->stop();
        setVisibility(Visibility::Hidden, true);
    }
}

bool MenuVideo::onInput(c2d::Input::Player *players) {

    if (players[0].buttons & Input::Left || players[0].buttons & Input::B) {
        setVisibility(Visibility::Hidden, true);
        if (!main->getPlayer()->hasVideo()) {
            main->getPlayer()->getOSD()->setVisibility(c2d::Visibility::Visible);
        }
        return true;
    }

    if (players[0].buttons & Input::Right) {
        MenuItem *item = getSelection();
        if (item->name != "Stop") {
            onOptionSelection(item);
        }
        return true;
    }

    return Menu::onInput(players);
}
