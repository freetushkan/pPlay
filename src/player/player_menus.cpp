//
// Player menus: tracks, playlist, playback mode, normalization.
//

#include "main.h"
#include "player.h"
#include "menu_video_submenu.h"
#include "encodings.h"

using namespace c2d;

static std::string trackItem(const MediaInfo::Track &track) {
    return "#" + std::to_string(track.id) + " " + encoding::fix(MediaInfo::describe(track));
}

// value, -, +, reset
static std::vector<MenuItem> adjustItems() {
    return {MenuItem("Value", "", MenuItem::Position::Top, MENU_ADJUST_VALUE, false),
            MenuItem("-", "", MenuItem::Position::Top, -1), MenuItem("+", "", MenuItem::Position::Top, 1),
            MenuItem("Reset", "", MenuItem::Position::Top, 0)};
}

void Player::buildMenus() {
    // build video track selection menu
    if (!file.mediaInfo.videos.empty()) {
        // videos menu options
        std::vector<MenuItem> items;
        for (auto &stream: file.mediaInfo.videos) {
            items.emplace_back(trackItem(stream), "", MenuItem::Position::Top, stream.id);
        }
        menuVideoStreams = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Video", items,
                                                MENU_VIDEO_TYPE_VID);
        menuVideoStreams->setVisibility(Visibility::Hidden, false);
        menuVideoStreams->setLayer(3);
        add(menuVideoStreams);
    }

    buildTrackMenus();

    std::vector<MenuItem> playlistItems;
    if (isPlaylistFile()) {
        for (auto &entry: mpv->getPlaylistItems()) {
            std::string name = std::to_string(entry.first + 1) + ". " + entry.second;
            playlistItems.emplace_back(name, "", MenuItem::Position::Top, entry.first);
        }
    } else {
        for (size_t i = 0; i < autoplayFiles.size(); i++) {
            auto &item = autoplayFiles[i];
            if (!pplay::Utility::isMedia(item)) {
                continue;
            }
            std::string name = std::to_string(i + 1) + ". " + encoding::fix(item.name);
            playlistItems.emplace_back(name, "", MenuItem::Position::Top, (int) i, true, item.name);
        }
    }
    if (!playlistItems.empty()) {
        menuPlaylist = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Playlist", playlistItems,
                                            MENU_VIDEO_TYPE_PLAYLIST);
        menuPlaylist->setVisibility(Visibility::Hidden, false);
        menuPlaylist->setLayer(3);
        add(menuPlaylist);
    }

    if (menuNormalize == nullptr) {
        menuNormalize = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Volume normalization",
                                             {MenuItem("Off", "", MenuItem::Position::Top, 0),
                                              MenuItem("Normalize", "", MenuItem::Position::Top, 1),
                                              MenuItem("Night mode", "", MenuItem::Position::Top, 2)},
                                             MENU_VIDEO_TYPE_NORMALIZE);
        menuNormalize->setLayer(3);
        menuNormalize->setVisibility(Visibility::Hidden, false);
        add(menuNormalize);
    }

    if (menuVisualizer == nullptr) {
        menuVisualizer = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Visualization",
                                              {MenuItem("Cover > visualizer", "", MenuItem::Position::Top, 0),
                                               MenuItem("Visualizer", "", MenuItem::Position::Top, 1),
                                               MenuItem("Icon only", "", MenuItem::Position::Top, 2)},
                                              MENU_VIDEO_TYPE_VISUALIZER);
        menuVisualizer->setLayer(3);
        menuVisualizer->setVisibility(Visibility::Hidden, false);
        add(menuVisualizer);
    }

    if (menuSpeed == nullptr) {
        menuSpeed = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Speed", adjustItems(),
                                         MENU_VIDEO_TYPE_SPEED);
        menuSpeed->setLayer(3);
        menuSpeed->setVisibility(Visibility::Hidden, false);
        add(menuSpeed);
    }
    if (menuBrightness == nullptr) {
        menuBrightness = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Brightness",
                                              adjustItems(), MENU_VIDEO_TYPE_BRIGHTNESS);
        menuBrightness->setLayer(3);
        menuBrightness->setVisibility(Visibility::Hidden, false);
        add(menuBrightness);
    }

    if (menuPlaybackMode == nullptr) {
        menuPlaybackMode = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "At playback end..",
                                                {MenuItem("Stop", "", MenuItem::Position::Top, 0),
                                                 MenuItem("Play next", "", MenuItem::Position::Top, 1),
                                                 MenuItem("Loop file", "", MenuItem::Position::Top, 2),
                                                 MenuItem("Loop directory", "", MenuItem::Position::Top, 3)},
                                                MENU_VIDEO_TYPE_PL_MODE);
        menuPlaybackMode->setLayer(3);
        menuPlaybackMode->setVisibility(Visibility::Hidden, false);
        add(menuPlaybackMode);
    }
}

void Player::buildTrackMenus() {
    bool audioVisible = menuAudioStreams != nullptr && menuAudioStreams->isVisible();
    bool subtitlesVisible = menuSubtitlesStreams != nullptr && menuSubtitlesStreams->isVisible();
    if (menuAudioStreams != nullptr) {
        delete (menuAudioStreams);
        menuAudioStreams = nullptr;
    }
    if (menuSubtitlesStreams != nullptr) {
        delete (menuSubtitlesStreams);
        menuSubtitlesStreams = nullptr;
    }

    // audio: the tracks, then a file to add
    if (!file.mediaInfo.audios.empty() || hasVideo()) {
        std::vector<MenuItem> items;
        for (auto &stream: file.mediaInfo.audios) {
            items.emplace_back(trackItem(stream), "", MenuItem::Position::Top, stream.id);
        }
        items.emplace_back("Add file...", "", MenuItem::Position::Top, MENU_TRACK_ADD);
        menuAudioStreams = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Audio", items,
                                                MENU_VIDEO_TYPE_AUD);
        menuAudioStreams->setVisibility(audioVisible ? Visibility::Visible : Visibility::Hidden, false);
        menuAudioStreams->setLayer(3);
        add(menuAudioStreams);
    }

    // subtitles: none, the tracks, then a file to add
    if (!file.mediaInfo.subtitles.empty() || hasVideo()) {
        std::vector<MenuItem> items;
        items.emplace_back("None", "", MenuItem::Position::Top, -1);
        for (auto &stream: file.mediaInfo.subtitles) {
            items.emplace_back(trackItem(stream), "", MenuItem::Position::Top, stream.id);
        }
        items.emplace_back("Add file...", "", MenuItem::Position::Top, MENU_TRACK_ADD);
        menuSubtitlesStreams = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Subtitles", items,
                                                    MENU_VIDEO_TYPE_SUB);
        menuSubtitlesStreams->setVisibility(subtitlesVisible ? Visibility::Visible : Visibility::Hidden, false);
        menuSubtitlesStreams->setLayer(3);
        add(menuSubtitlesStreams);
    }
}

std::vector<MenuVideoSubmenu *> Player::menus() const {
    std::vector<MenuVideoSubmenu *> list;
    for (auto *menu: {menuVideoStreams, menuAudioStreams, menuSubtitlesStreams, menuPlaylist, menuPlaybackMode,
                      menuNormalize, menuVisualizer, menuSpeed, menuBrightness}) {
        if (menu != nullptr) {
            list.push_back(menu);
        }
    }
    return list;
}

bool Player::isMenuVisible() const {
    if (main->getMenuVideo()->isVisible()) {
        return true;
    }
    for (auto *menu: menus()) {
        if (menu->isVisible()) {
            return true;
        }
    }
    return false;
}

void Player::hideMenus() {
    main->getMenuVideo()->setVisibility(Visibility::Hidden, true);
    for (auto *menu: menus()) {
        menu->setVisibility(Visibility::Hidden, true);
    }
}

void Player::deleteMenus(bool all) {
    for (auto **menu: {&menuVideoStreams, &menuAudioStreams, &menuSubtitlesStreams, &menuPlaylist, &menuPlaybackMode,
                       &menuNormalize, &menuVisualizer, &menuSpeed, &menuBrightness}) {
        // the file independent ones are kept between files
        bool kept = menu == &menuPlaybackMode || menu == &menuNormalize || menu == &menuVisualizer
                    || menu == &menuSpeed || menu == &menuBrightness;
        if (*menu != nullptr && (all || !kept)) {
            delete (*menu);
            *menu = nullptr;
        }
    }
}

MenuVideoSubmenu *Player::getMenuVideoStreams() {
    return menuVideoStreams;
}

MenuVideoSubmenu *Player::getMenuAudioStreams() {
    return menuAudioStreams;
}

MenuVideoSubmenu *Player::getMenuSubtitlesStreams() {
    return menuSubtitlesStreams;
}

MenuVideoSubmenu *Player::getMenuPlaylist() {
    return menuPlaylist;
}

MenuVideoSubmenu *Player::getMenuPlaybackMode() {
    return menuPlaybackMode;
}

MenuVideoSubmenu *Player::getMenuNormalize() {
    return menuNormalize;
}

MenuVideoSubmenu *Player::getMenuVisualizer() {
    return menuVisualizer;
}

MenuVideoSubmenu *Player::getMenuSpeed() {
    return menuSpeed;
}

MenuVideoSubmenu *Player::getMenuBrightness() {
    return menuBrightness;
}
