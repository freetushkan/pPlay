//
// Player menus: tracks, playlist, playback mode, normalization.
//

#include <algorithm>

#include "main.h"
#include "player.h"
#include "menu_video_submenu.h"
#include "player_osd.h"
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
        std::vector<MenuItem> items;
        for (auto &stream: file.mediaInfo.videos) {
            items.emplace_back(trackItem(stream), "", MenuItem::Position::Top, stream.id);
        }
        menuVideoStreams = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Video", items,
                                                MENU_VIDEO_TYPE_VID);
        menuVideoStreams->setVisibility(Visibility::Hidden, false);
        menuVideoStreams->setLayer(3);
        main->add(menuVideoStreams);
    }

    buildTrackMenus();
    buildPlaylistMenu();

    if (menuNormalize == nullptr) {
        menuNormalize = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Volume normalization",
                                             {MenuItem("Off", "", MenuItem::Position::Top, 0),
                                              MenuItem("Normalize", "", MenuItem::Position::Top, 1),
                                              MenuItem("Night mode", "", MenuItem::Position::Top, 2)},
                                             MENU_VIDEO_TYPE_NORMALIZE);
        menuNormalize->setLayer(3);
        menuNormalize->setVisibility(Visibility::Hidden, false);
        main->add(menuNormalize);
    }

    if (menuVisualizer == nullptr) {
        menuVisualizer = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Visualization",
                                              {MenuItem("Cover > visualizer", "", MenuItem::Position::Top, 0),
                                               MenuItem("Visualizer", "", MenuItem::Position::Top, 1),
                                               MenuItem("Icon only", "", MenuItem::Position::Top, 2)},
                                              MENU_VIDEO_TYPE_VISUALIZER);
        menuVisualizer->setLayer(3);
        menuVisualizer->setVisibility(Visibility::Hidden, false);
        main->add(menuVisualizer);
    }

    if (menuSpeed == nullptr) {
        menuSpeed = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Speed", adjustItems(),
                                         MENU_VIDEO_TYPE_SPEED);
        menuSpeed->setLayer(3);
        menuSpeed->setVisibility(Visibility::Hidden, false);
        main->add(menuSpeed);
    }
    if (menuBrightness == nullptr) {
        menuBrightness = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Brightness",
                                              adjustItems(), MENU_VIDEO_TYPE_BRIGHTNESS);
        menuBrightness->setLayer(3);
        menuBrightness->setVisibility(Visibility::Hidden, false);
        main->add(menuBrightness);
    }

    if (menuPlaybackMode == nullptr) {
        menuPlaybackMode = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Playback mode",
                                                {MenuItem("Pause at end", "pl_pause.png", MenuItem::Position::Top, 0),
                                                 MenuItem("Sequential", "pl_next.png", MenuItem::Position::Top, 1),
                                                 MenuItem("Loop file", "pl_loop_file.png", MenuItem::Position::Top, 2),
                                                 MenuItem("Loop queue", "pl_loop_directory.png", MenuItem::Position::Top, 3),
                                                 MenuItem("Shuffle", "pl_shuffle.png", MenuItem::Position::Top, 4)},
                                                MENU_VIDEO_TYPE_PL_MODE);
        menuPlaybackMode->setLayer(3);
        menuPlaybackMode->setVisibility(Visibility::Hidden, false);
        main->add(menuPlaybackMode);
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
        items.emplace_back("Add file", "", MenuItem::Position::Top, MENU_TRACK_ADD);
        menuAudioStreams = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Audio", items,
                                                MENU_VIDEO_TYPE_AUD);
        menuAudioStreams->setVisibility(audioVisible ? Visibility::Visible : Visibility::Hidden, false);
        menuAudioStreams->setLayer(3);
        main->add(menuAudioStreams);
    }

    // subtitles: none, the tracks, then a file to add
    if (!file.mediaInfo.subtitles.empty() || hasVideo()) {
        std::vector<MenuItem> items;
        items.emplace_back("None", "", MenuItem::Position::Top, -1);
        for (auto &stream: file.mediaInfo.subtitles) {
            items.emplace_back(trackItem(stream), "", MenuItem::Position::Top, stream.id);
        }
        items.emplace_back("Add file", "", MenuItem::Position::Top, MENU_TRACK_ADD);
        menuSubtitlesStreams = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Subtitles", items,
                                                    MENU_VIDEO_TYPE_SUB);
        menuSubtitlesStreams->setVisibility(subtitlesVisible ? Visibility::Visible : Visibility::Hidden, false);
        menuSubtitlesStreams->setLayer(3);
        main->add(menuSubtitlesStreams);
    }
}

void Player::buildPlaylistMenu() {
    std::vector<MenuItem> playlistItems;
    std::string key;
    if (isPlaylistFile()) {
        key = "m3u:" + file.path;
        for (auto &entry: mpv->getPlaylistItems()) {
            playlistItems.emplace_back(entry.second, "", MenuItem::Position::Top, entry.first);
            playlistItems.back().badge = std::to_string(entry.first + 1);
            key += "\n" + entry.second;
        }
    } else {
        // shuffled: in its order
        std::vector<size_t> order;
        for (auto &path: shuffleOrder) {
            for (size_t i = 0; i < playbackQueue.size(); i++) {
                if (playbackQueue[i].path == path) {
                    order.push_back(i);
                    break;
                }
            }
        }
        if (order.empty()) {
            for (size_t i = 0; i < playbackQueue.size(); i++) {
                order.push_back(i);
            }
        }
        for (size_t n = 0; n < order.size(); n++) {
            auto &item = playbackQueue[order[n]];
            if (!pplay::Utility::isMedia(item)) {
                continue;
            }
            size_t number = shuffleOrder.empty() ? order[n] + 1 : n + 1;
            std::string name = encoding::fix(item.label.empty() ? item.name : item.label);
            playlistItems.emplace_back(name, "", MenuItem::Position::Top, (int) order[n], true, item.path);
            playlistItems.back().badge = std::to_string(number);
            key += "\n" + std::to_string(number) + " " + item.path;
        }
    }
    // the numbers in one column: as wide as the widest
    size_t chars = 1;
    for (auto &item: playlistItems) {
        chars = std::max(chars, item.badge.size());
    }
    for (auto &item: playlistItems) {
        item.badgeChars = (int) chars;
    }
    // the same list: kept (shown, its focus), the playing one marked
    if (menuPlaylist != nullptr && key == playlistKey) {
        menuPlaylist->updateSelectionHighlight();
        return;
    }
    // another list (shuffled meanwhile): shown again if it was
    bool shown = menuPlaylist != nullptr && menuPlaylist->isVisible();
    delete (menuPlaylist);
    menuPlaylist = nullptr;
    playlistKey = key;
    if (!playlistItems.empty()) {
        menuPlaylist = new MenuVideoSubmenu(main, main->getMenuVideo()->getGlobalBounds(), "Playback queue",
                                            playlistItems, MENU_VIDEO_TYPE_PLAYLIST);
        menuPlaylist->setVisibility(Visibility::Hidden, false);
        if (shown) {
            menuPlaylist->setVisibility(Visibility::Visible, false);
        }
        menuPlaylist->setLayer(3);
        main->add(menuPlaylist);
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
    if (main->getMenuVideo()->isVisible() || main->getMenuMain()->isMenuVisible()) {
        return true;
    }
    for (auto *menu: menus()) {
        if (menu->isVisible()) {
            return true;
        }
    }
    return false;
}

void Player::showOsd() {
    osd->expand(true);
}

void Player::hideMenus() {
    main->getMenuVideo()->setVisibility(Visibility::Hidden, true);
    main->getMenuMain()->closePlayerSettings();
    for (auto *menu: menus()) {
        menu->setVisibility(Visibility::Hidden, true);
    }
}

void Player::deleteMenus(bool all) {
    for (auto **menu: {&menuVideoStreams, &menuAudioStreams, &menuSubtitlesStreams, &menuPlaylist, &menuPlaybackMode,
                       &menuNormalize, &menuVisualizer, &menuSpeed, &menuBrightness}) {
        // the file independent ones are kept between files
        bool kept = menu == &menuPlaybackMode || menu == &menuNormalize || menu == &menuVisualizer
                    || menu == &menuSpeed || menu == &menuBrightness
                    // its list may stay (another file of it)
                    || menu == &menuPlaylist;
        if (*menu != nullptr && (all || !kept)) {
            delete (*menu);
            *menu = nullptr;
        }
    }
    if (all) {
        playlistKey.clear();
    }
}

void Player::processDeferredMenus() {
    const bool deletePending = menusDeletePending;
    const bool deleteAll = menusDeleteAllPending;
    const bool buildPending = menusBuildPending;
    const bool trackBuildPending = menusTrackBuildPending;
    const bool playlistBuildPending = menusPlaylistBuildPending;

    menusDeletePending = false;
    menusDeleteAllPending = false;
    menusBuildPending = false;
    menusTrackBuildPending = false;
    menusPlaylistBuildPending = false;

    // These menus are children of Main, not Player. Mutating Main's child vector
    // from Player::onUpdate() invalidates C2DObject::onUpdate()'s active iterators.
    if (deletePending) {
        deleteMenus(deleteAll);
    }
    if (buildPending && !main->isExiting()) {
        buildMenus();
    } else {
        if (trackBuildPending && !main->isExiting() && !deleteAll && !mpv->isStopped()) {
            buildTrackMenus();
        }
        // another shuffled order
        if (playlistBuildPending && !main->isExiting() && !deleteAll && menuPlaylist != nullptr) {
            buildPlaylistMenu();
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
