//
// Created by cpasjuste on 07/12/18.
//

#include <utility>

#include "platform.h"
#include "main.h"
#include "menu_main.h"
#include "menu_file_editor.h"
#include "menu_main_items.h"
#include "io.h"
#include "i18n.h"
#include "saved.h"
#include "playlists.h"

using namespace c2d;

using Submenu = MenuMainOptionsSubmenu;
using namespace pplay::MenuMainItems;

MenuMain::MenuMain(Main *main, const c2d::FloatRect &rect, const std::vector<MenuItem> &items)
    : Menu(main, rect, "pPlay v" APP_VERSION, items, true) {
    using Pos = MenuItem::Position;
    // settings by category (menu key, items)
    std::vector<std::pair<std::string, std::vector<MenuItem>>> categories;
    categories.push_back({"Playback",
                          {
                                  MenuItem("Playback mode", "pl_mode.png", Pos::Top),
                                  MenuItem("Match media type", "file_audio.png", Pos::Top),
                                  MenuItem("Playback retry", "pl_retry.png", Pos::Top),
                                  MenuItem("Seek preview", "btn_seek_forward.png", Pos::Top),
                                  MenuItem("Restore last file", "restore.png", Pos::Top),
                                  MenuItem("Browse position after restore", "restore.png", Pos::Top),
                          }});
    categories.push_back({"Progress",
                          {
                                  MenuItem("Keep progress from", "progress_from.png", Pos::Top),
                                  MenuItem("End margin", "progress_from.png", Pos::Top),
                                  MenuItem("Progress save interval", "time.png", Pos::Top),
                                  MenuItem("Progress line", "progress_line.png", Pos::Top),
                                  MenuItem("Folder marks", "progress_end.png", Pos::Top),
                                  MenuItem("Progress source", "net_torrserve.png", Pos::Top),
                          }});
    categories.push_back({"Audio and subtitles",
                          {
                                  MenuItem("Audio language", "audio.png", Pos::Top),
                                  MenuItem("Subtitles language", "subtitles.png", Pos::Top),
                                  MenuItem("External tracks", "external.png", Pos::Top),
                                  MenuItem("Styled subtitles", "sub_style.png", Pos::Top),
                                  MenuItem("Audio channels", "audio_channels.png", Pos::Top),
                                  MenuItem("Volume normalization", "normalize.png", Pos::Top),
                                  MenuItem("Visualization", "visualizer.png", Pos::Top),
                          }});
    categories.push_back({"Video",
                          {
                                  MenuItem("Video quality", "quality.png", Pos::Top),
                                  MenuItem("Drop threshold", "quality.png", Pos::Top),
                                  MenuItem("Drop check period", "time.png", Pos::Top),
                                  MenuItem("Delay after decoder switch", "time.png", Pos::Top),
                                  MenuItem("Pause on switch", "btn_pause.png", Pos::Top),
#ifdef PPLAY_HW_DECODING
                                  MenuItem("Hardware decoding", "gpu.png", Pos::Top),
                                  MenuItem("Hardware retry", "gpu.png", Pos::Top),
                                  MenuItem("Hardware attempts", "gpu.png", Pos::Top),
#endif
                                  MenuItem("Dithering", "dither.png", Pos::Top),
                          }});
    categories.push_back({"Interface",
                          {
                                  MenuItem("Language", "translate.png", Pos::Top),
#ifdef __PS5__
                                  MenuItem("Resolution", "interface.png", Pos::Top),
#endif
                                  MenuItem("Text encoding", "encoding.png", Pos::Top),
                                  MenuItem("Accent color", "color.png", Pos::Top),
                                  MenuItem("Hidden files", "hidden.png", Pos::Top),
                                  MenuItem("Other files", "file_other.png", Pos::Top),
                                  MenuItem("Saved section", "saved.png", Pos::Top),
                                  MenuItem("Playlists section", "playlist.png", Pos::Top),
                                  MenuItem("Media information", "media_info.png", Pos::Top),
                                  MenuItem("Player panel", "time.png", Pos::Top),
#if !defined(__ANDROID__) && !defined(__LINUX__)
                                  MenuItem("Bumpers and triggers", "swap.png", Pos::Top),
#endif
                          }});
    networkSettings.emplace_back("Buffer length", "buffer.png", Pos::Top);
    networkSettings.emplace_back("Buffer memory", "memory.png", Pos::Top);
    categories.push_back({"Networks", networkCategoryItems()});
    categories.push_back({"Saved files",
                          {
                                  MenuItem("Saving threads", "save_threads.png", Pos::Top),
                                  MenuItem("Retention period", "calendar.png", Pos::Top),
                                  MenuItem("Storage limit", "storage.png", Pos::Top),
                          }});
    std::vector<MenuItem> system = {
            MenuItem("Local home path", "home.png", Pos::Top),
            MenuItem("Edit files", "edit.png", Pos::Top),
            MenuItem("Clear cache", "clear_cache.png", Pos::Top),
            MenuItem("Standby", "awake.png", Pos::Top),
#ifdef __ANDROID__
            MenuItem("Background behavior", "exit.png", Pos::Top),
#endif
    };
#ifdef __SWITCH__
    system.emplace_back("CPU boost", "cpu.png", Pos::Top);
#endif
#if defined(__PS5__) && defined(PPLAY_PS5_KEEP_IN_REST)
    system.emplace_back("Keep in rest mode", "rest.png", Pos::Top);
#endif
    system.emplace_back("Logging", "log.png", Pos::Top);
    system.emplace_back("View log", "log.png", Pos::Top);
    system.emplace_back("Clear log", "clear_cache.png", Pos::Top);
    categories.push_back({"System", system});

    std::vector<MenuItem> it = {
            MenuItem("Playback", "pl_mode.png", Pos::Top), MenuItem("Progress", "time.png", Pos::Top),
            MenuItem("Audio and subtitles", "audio.png", Pos::Top), MenuItem("Video", "video.png", Pos::Top),
            MenuItem("Interface", "interface.png", Pos::Top), MenuItem("Networks", "network.png", Pos::Top),
            MenuItem("Saved files", "saved.png", Pos::Top), MenuItem("System", "system.png", Pos::Top),
            MenuItem("About", "info.png", Pos::Top),
    };
    for (auto &item: it) {
        item.opens = true;
    }
    // the settings: wider, the values on the right
    for (auto &category: categories) {
        for (auto &item: category.second) {
            item.value = "-";
        }
    }
    wide = {-420 * main->getScaling().x, rect.top, 420 * main->getScaling().x, rect.height};

    menuMainOptions = new MenuMainOptions(main, wide, it);
    // over the shade under the menus (see Main)
    menuMainOptions->setLayer(3);
    menuMainOptions->setVisibility(Visibility::Hidden, false);
    main->add(menuMainOptions);

    // no submenus behind these items: A does nothing, the selection scrolls long texts
    addSubmenu("About", "About", "", makeAboutItems(main));
    menuMainOptionsSubmenus["About"]->setCompact(true);

    for (auto &category: categories) {
        addSubmenu(category.first, category.first, "", category.second);
        for (auto &item: category.second) {
            parents[item.name] = category.first;
        }
    }

    auto addTextSubmenu = [&](const std::string &name, const std::string &title, const std::string &optionName) {
        addSubmenu(name, title, optionName, {MenuItem("", "", MenuItem::Position::Top)}, Submenu::ValueType::String,
                   Submenu::MenuType::TextInput);
    };

    for (int i = 1; i <= main->getConfig()->getNetworkCount(); i++) {
        addNetworkMenus(i);
    }
    std::vector<MenuItem> languageItems;
    for (const auto &language: pplay::I18n::list()) {
        MenuItem item(language.code, "", MenuItem::Position::Top);
        item.label = language.name;
        languageItems.push_back(item);
    }
    addSubmenu("Language", "Language", OPT_LANGUAGE, languageItems, Submenu::ValueType::String);
    addSubmenu("Text encoding", "Old tags and file names", OPT_TEXT_ENCODING,
               makeIntItems({{"Auto", 0},
                             {"Central European (1250)", 1250},
                             {"Cyrillic (1251)", 1251},
                             {"Western (1252)", 1252},
                             {"Greek (1253)", 1253},
                             {"Turkish (1254)", 1254},
                             {"Hebrew (1255)", 1255},
                             {"Arabic (1256)", 1256},
                             {"Baltic (1257)", 1257}}),
               Submenu::ValueType::Integer);
    addTextSubmenu("Local home path", "Local home path", OPT_HOME_PATH);
#if defined(__PS5__) && defined(PPLAY_PS5_KEEP_IN_REST)
    addSubmenu("Keep in rest mode", "Keep the player running in rest mode (unstable)", OPT_PS5_KEEP_IN_REST,
               makeIntItems({{"Off", 0}, {"On (unstable)", 1}}), Submenu::ValueType::Integer);
#endif

#ifdef __SWITCH__
    addSubmenu("CPU boost", "CPU boost", OPT_CPU_BOOST,
               {MenuItem("Disabled", "", MenuItem::Position::Top), MenuItem("Enabled", "", MenuItem::Position::Top)});
#endif

    addSubmenu("Progress source", "Preferred progress source", OPT_TS_PROGRESS,
               makeIntItems({{"Off", 0}, {"Local first", 1}, {"TorrServer first", 2}}), Submenu::ValueType::Integer);
    addSubmenu("Playback retry", "Playback retry", OPT_NETWORK_RETRIES, makeAdjustItems(), Submenu::ValueType::Integer,
               Submenu::MenuType::Adjust, 0.0f, 10.0f, 1.0f);
    addSubmenu("Playback mode", "At file end..", OPT_AUTOPLAY_MODE,
               makeIntItems({{"Stop playback", 0}, {"Play next", 1}, {"Loop file", 2}, {"Loop directory", 3},
                             {"Shuffle", 4}}),
               Submenu::ValueType::Integer);
    addSubmenu("Match media type", "Only files of the current audio or video type are added to queue",
               OPT_AUTOPLAY_TYPE_FILTER, makeIntItems({{"Off", 0}, {"On", 1}}), Submenu::ValueType::Integer);
#if !defined(__ANDROID__) && !defined(__LINUX__)
    addSubmenu("Bumpers and triggers", "Bumpers and triggers", OPT_SWAP_CONTROLS,
               makeIntItems({{"Normal", 0}, {"Swapped", 1}, {"Off", 2}}), Submenu::ValueType::Integer);
#endif
    addSubmenu("Keep progress from", "Keep progress of files from", OPT_PROGRESS_MIN_LENGTH, makeAdjustItems(),
               Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 0.0f, 60.0f, 1.0f, "min");
    addSubmenu("Progress save interval", "Progress save interval", OPT_PROGRESS_INTERVAL, makeAdjustItems(),
               Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 5.0f, 120.0f, 5.0f, "s");
    addSubmenu("End margin", "Watched with this left", OPT_WATCHED_TOLERANCE,
               makeAdjustItems(), Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 0.0f, 600.0f, 5.0f, "s");
    getMenuMainOptionsSubmenu("End margin")->setZeroLabel("Off");
    addSubmenu("Progress line", "Progress line always on screen", OPT_PROGRESS_LINE,
               makeIntItems({{"Off", 0}, {"On", 1}}), Submenu::ValueType::Integer);
    addSubmenu("Player panel", "Player panel hidden after", OPT_OSD_HIDE_TIME, makeAdjustItems(),
               Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 0.0f, 30.0f, 1.0f, "s");
    getMenuMainOptionsSubmenu("Player panel")->setZeroLabel("Never");
    addSubmenu("Folder marks", "Folders watched in the list", OPT_FOLDER_MARKS,
               makeIntItems({{"Off", 0}, {"By marks", 1}, {"By progress", 2}}), Submenu::ValueType::Integer);
    addSubmenu("Audio language", "Audio language", OPT_AUDIO_LANGUAGE, makeTrackLanguageItems("Auto"),
               Submenu::ValueType::String);
    addSubmenu("Subtitles language", "Subtitles language", OPT_SUB_LANGUAGE, makeTrackLanguageItems("Off"),
               Submenu::ValueType::String);
    addSubmenu("External tracks", "External audio and subtitles", OPT_EXTERNAL_TRACKS,
               makeIntItems({{"Off", 0}, {"On", 1}}), Submenu::ValueType::Integer);
    addSubmenu("Styled subtitles", "ASS subtitles: their own look, or the plain one", OPT_SUB_ASS_PLAIN,
               makeIntItems({{"As made", 0}, {"Plain", 1}}), Submenu::ValueType::Integer);
    addSubmenu("Audio channels", "Audio output channels (more ones: if the device takes them)", OPT_AUDIO_CHANNELS,
               makeIntItems({{"Stereo", 0}, {"Auto", 1}, {"5.1", 2}, {"7.1", 3}}), Submenu::ValueType::Integer);
    addSubmenu("Volume normalization", "Volume normalization", OPT_AUDIO_NORMALIZE,
               makeIntItems({{"Off", 0}, {"Normalize", 1}, {"Night mode", 2}}), Submenu::ValueType::Integer);
    addSubmenu("Visualization", "Audio files screen", OPT_AUDIO_VISUALIZER,
               makeIntItems({{"Cover > visualizer", 0}, {"Visualizer", 1}, {"Icon only", 2}}),
               Submenu::ValueType::Integer);
    addSubmenu("Media information", "Media information read by itself", OPT_MEDIA_PROBE,
               makeIntItems({{"Panel opened only", 0}, {"Selected file", 2}, {"Selected, with progress", 1}}),
               Submenu::ValueType::Integer);
    addSubmenu("Video quality", "Video quality", OPT_VIDEO_QUALITY,
               makeIntItems({{"Auto", 3}, {"Best", 0}, {"Balanced", 1}, {"Fast", 2}}), Submenu::ValueType::Integer);
    addSubmenu("Dithering", "Smoother color gradients, a bit more GPU work", OPT_DITHER,
               makeIntItems({{"Off", 0}, {"Ordered", 1}, {"Fruit", 2}}), Submenu::ValueType::Integer);
    addSubmenu("Drop threshold", "Quality lowered when this share of frames is dropped", OPT_AUTO_DROPS,
               makeAdjustItems(), Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 1.0f, 50.0f, 1.0f, "%");
    addSubmenu("Drop check period", "Dropped frames counted over", OPT_AUTO_DROPS_TIME,
               makeAdjustItems(), Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 5.0f, 60.0f, 5.0f, "s");
    addSubmenu("Pause on switch", "A short pause while the decoder or quality switches",
               OPT_AUTO_SWITCH_PAUSE, makeIntItems({{"Off", 0}, {"On", 1}}), Submenu::ValueType::Integer);
    addSubmenu("Delay after decoder switch", "Quality not checked after a decoder switch for",
               OPT_AUTO_QUALITY_DELAY, makeAdjustItems(), Submenu::ValueType::Integer, Submenu::MenuType::Adjust,
               0.0f, 120.0f, 5.0f, "s");
#ifdef PPLAY_HW_DECODING
    addSubmenu("Hardware decoding", "Hardware video decoding", OPT_HW_DECODING,
               makeIntItems({{"Auto", 2}, {"On", 1}, {"Off", 0}}), Submenu::ValueType::Integer);
    addSubmenu("Hardware retry", "Hardware decoding retried after (doubled each time, 0: never)",
               OPT_AUTO_HW_RETRY, makeAdjustItems(), Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 0.0f,
               30.0f, 1.0f, "min");
    addSubmenu("Hardware attempts", "Hardware decoding attempts per file", OPT_AUTO_HW_RETRIES,
               makeAdjustItems(), Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 1.0f, 10.0f, 1.0f);
#endif
    addSubmenu("Buffer length", "Network buffer length", OPT_CACHE_SECS, makeAdjustItems(), Submenu::ValueType::Integer,
               Submenu::MenuType::Adjust, 10.0f, 600.0f, 10.0f, "s");
    addSubmenu("Buffer memory", "Network buffer memory", OPT_CACHE_MB, makeAdjustItems(), Submenu::ValueType::Integer,
               Submenu::MenuType::Adjust, 50.0f, (float) CACHE_MB_MAX, 50.0f, "MB");
#ifdef __PS5__
    addSubmenu("Resolution", "Preferred resolution", OPT_PS5_UI_4K,
               makeIntItems({{"1080p", 0}, {"4K", 1}}), Submenu::ValueType::Integer);
#endif
#ifdef __ANDROID__
    addSubmenu("Background behavior", "Application behavior when minimized", OPT_ANDROID_BACKGROUND,
               makeIntItems({{"Keep in memory", 0}, {"Save state and close", 1}}), Submenu::ValueType::Integer);
#endif
    addSubmenu("Standby", "Standby when idle", OPT_STANDBY_TIME,
               makeMinuteItems({5, 10, 15, 30, 60, 120, 180, 360, 720}, {{"By system", 0}}, {{"Never", -1}}),
               Submenu::ValueType::Integer);
    addSubmenu("Restore last file", "The file playing at the exit opened at the start", OPT_RESTORE_SESSION,
#ifdef __PS5__
               makeIntItems({{"Off", 0}, {"After rest mode", 1}, {"Always", 2}}),
#else
               makeIntItems({{"Off", 0}, {"Always", 2}}),
#endif
               Submenu::ValueType::Integer);
    addSubmenu("Seek preview", "Play at seek position", OPT_SEEK_PREVIEW,
               makeIntItems({{"Always", 2}, {"Local files only", 1}, {"Off", 0}}), Submenu::ValueType::Integer);
    addSubmenu("Browse position after restore", "Browse position after the last file restored", OPT_RESTORE_LOCATION,
               makeIntItems({{"At the current file", 1}, {"At the last location", 0}}), Submenu::ValueType::Integer);
    addSubmenu("Saved section", "The Saved section in the menu", OPT_SHOW_SAVED,
               makeIntItems({{"If not empty", 1}, {"Not shown", 0}}), Submenu::ValueType::Integer);
    addSubmenu("Playlists section", "The Playlists section in the menu", OPT_SHOW_PLAYLISTS,
               makeIntItems({{"If not empty", 1}, {"Not shown", 0}}), Submenu::ValueType::Integer);
    addSubmenu("Hidden files", "Files and folders starting with a dot", OPT_SHOW_HIDDEN,
               makeIntItems({{"Hidden", 0}, {"Shown", 1}}), Submenu::ValueType::Integer);
    addSubmenu("Other files", "Files that are not media", OPT_SHOW_ALL_FILES,
               makeIntItems({{"Hidden", 0}, {"Shown", 1}}), Submenu::ValueType::Integer);
    addSubmenu("Saving threads", "Files saved at once", OPT_SAVE_THREADS, makeAdjustItems(),
               Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 1.0f, 4.0f, 1.0f);
    addSubmenu("Retention period", "Delete files not opened for this long", OPT_SAVE_KEEP_DAYS, makeAdjustItems(),
               Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 0.0f, 365.0f, 1.0f, "d");
    getMenuMainOptionsSubmenu("Retention period")->setZeroLabel("∞");
    addSubmenu("Storage limit", "Delete oldest files when the storage limit is exceeded", OPT_SAVE_MAX_GB,
               makeAdjustItems(), Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 0.0f, 2000.0f, 1.0f, "GB");
    getMenuMainOptionsSubmenu("Storage limit")->setZeroLabel("∞");
    addSubmenu("Logging", "Log to file", OPT_LOG_LEVEL,
               makeIntItems({{"Off", 0}, {"Error", 1}, {"Info", 2}, {"Debug", 3}, {"Trace", 4}}),
               Submenu::ValueType::Integer);
    addSubmenu("Accent color", "Accent color", OPT_ACCENT_COLOR, makeAccentColorItems(), Submenu::ValueType::String);

    fileEditor = new MenuFileEditor(main, rect);
    fileEditor->setLayer(3);
    fileEditor->setVisibility(Visibility::Hidden, false);
    main->add(fileEditor);

    networkItems = main->getConfig()->getNetworkCount();
    updateNetworkLabels();
}

void MenuMain::addSubmenu(const std::string &name, const std::string &title, const std::string &optionName,
                          const std::vector<MenuItem> &submenuItems, Submenu::ValueType valueType,
                          Submenu::MenuType menuType, float minValue, float maxValue, float stepValue,
                          const std::string &unit) {
    auto *submenu = new MenuMainOptionsSubmenu(main, wide, title, submenuItems, optionName, valueType, menuType,
                                               minValue, maxValue, stepValue, unit);
    submenu->setLayer(3);
    submenu->setVisibility(Visibility::Hidden, false);
    submenu->setKey(name);
    submenu->refresh();
    main->add(submenu);
    menuMainOptionsSubmenus[name] = submenu;
}

void MenuMain::addNetworkMenus(int i) {
    using Pos = MenuItem::Position;
    if (i <= networkMenus) {
        return;
    }
    networkMenus = i;
    const std::string name = networkKey(i);
    parents[name] = "Networks";
    std::vector<MenuItem> items;
    for (const char *suffix: {" type", " name", " url", " user", " password", " token", " timeout", " tls", " sync",
                              " smb", " search", " up", " down", " delete"}) {
#ifndef __SMB2__
        if (std::string(suffix) == " smb") {
            continue;
        }
#endif
        items.emplace_back(name + suffix, "", Pos::Top);
        parents[name + suffix] = name;
    }
    // its settings: a value on the right
    for (auto &item: items) {
        for (const char *suffix: {" timeout", " tls", " sync", " smb"}) {
            if (item.name == name + suffix) {
                item.value = "-";
            }
        }
    }
    addSubmenu(name, name, "", items, Submenu::ValueType::String);
    // the servers found (DLNA, TorrServer, Samba): one picked sets the url (the items made when shown)
    addSubmenu(name + " search", name + " search", PPLAYConfig::networkOption(i), {}, Submenu::ValueType::String);
    auto addText = [&](const std::string &key, const char *option) {
        addSubmenu(key, key, option, {MenuItem("", "", Pos::Top)}, Submenu::ValueType::String,
                   Submenu::MenuType::TextInput);
    };
    addText(name + " name", PPLAYConfig::networkNameOption(i));
    addText(name + " url", PPLAYConfig::networkOption(i));
    addText(name + " user", PPLAYConfig::networkUserOption(i));
    addText(name + " password", PPLAYConfig::networkPasswordOption(i));
    addText(name + " token", PPLAYConfig::networkTokenOption(i));
    std::vector<MenuItem> typeItems;
    for (auto &typeName: pplay::Io::networkTypeNames()) {
        typeItems.emplace_back(typeName, "", Pos::Top);
    }
    addSubmenu(name + " type", name + " type", PPLAYConfig::networkTypeOption(i), typeItems,
               Submenu::ValueType::String);
    addSubmenu(name + " timeout", "Connection timeout", PPLAYConfig::networkTimeoutOption(i), makeAdjustItems(),
               Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 1.0f, 300.0f, 5.0f, "s");
    addSubmenu(name + " tls", "TLS certificates checked (HTTPS)", PPLAYConfig::networkTlsOption(i),
               makeIntItems({{"On", 1}, {"Off", 0}}), Submenu::ValueType::Integer);
    addSubmenu(name + " sync", "Torrents and watched marks refreshed every", PPLAYConfig::networkSyncOption(i),
               makeAdjustItems(), Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 10.0f, 600.0f, 10.0f, "s");
#ifdef __SMB2__
    addSubmenu(name + " smb", "SMB preload buffer", PPLAYConfig::networkSmbBufferOption(i), makeAdjustItems(),
               Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 1.0f, 100.0f, 1.0f, "MB");
#endif
}

std::vector<MenuItem> MenuMain::networkCategoryItems() {
    std::vector<MenuItem> items;
    for (int i = 1; i <= main->getConfig()->getNetworkCount(); i++) {
        items.emplace_back(networkKey(i), networkIcon(main->getConfig(), i), MenuItem::Position::Top);
    }
    items.emplace_back("Add network", "net_add.png", MenuItem::Position::Top);
    size_t settings = items.size();
    items.insert(items.end(), networkSettings.begin(), networkSettings.end());
    // the settings apart from the networks
    items[settings].line = true;
    for (auto &item: items) {
        item.value = "-";
        parents[item.name] = "Networks";
    }
    return items;
}

void MenuMain::updateNetworks(bool force) {
    int count = main->getConfig()->getNetworkCount();
    for (int i = networkMenus + 1; i <= count; i++) {
        addNetworkMenus(i);
    }
    if (force || count != networkItems) {
        networkItems = count;
        auto category = menuMainOptionsSubmenus.find("Networks");
        if (category != menuMainOptionsSubmenus.end()) {
            MenuItem *selected = category->second->getSelection();
            std::string name = selected != nullptr ? selected->name : "";
            category->second->setItems(networkCategoryItems());
            category->second->select(name);
        }
        setItems(main->moduleItems());
    }
    updateNetworkLabels();
    reloadModules();
}

void MenuMain::showNetwork(int index, const std::string &select) {
    MenuMainOptionsSubmenu *submenu = getMenuMainOptionsSubmenu(networkKey(index));
    if (submenu == nullptr) {
        return;
    }
    submenu->setVisibility(Visibility::Visible, true);
    if (!select.empty()) {
        submenu->select(select);
    }
}

void MenuMain::updateNetworkLabels() {
    for (int i = 1; i <= main->getConfig()->getNetworkCount(); i++) {
        const std::string key = networkKey(i);
        const std::string name = main->getConfig()->getOption(PPLAYConfig::networkNameOption(i))->getString();
        const std::string url =
                PPLAYConfig::withoutCredentials(main->getConfig()->getOption(PPLAYConfig::networkOption(i))->getString());
        const std::string type = main->getConfig()->getOption(PPLAYConfig::networkTypeOption(i))->getString();
        // the item name stays the menu key
        std::string label = pplay::tr("Network") + " " + std::to_string(i);
        if (!url.empty() && !name.empty() && name != key) {
            label += " (" + name + ")";
        }
        for (auto *button: getMenuMainOptionsSubmenu("Networks")->getButtons()) {
            if (button->item.name == key) {
                button->item.label = label;
                button->setIcon(networkIcon(main->getConfig(), i));
                button->refreshText();
            }
        }
        auto submenu = menuMainOptionsSubmenus.find(key);
        if (submenu != menuMainOptionsSubmenus.end()) {
            submenu->second->setTitle(label);
            for (auto *button: submenu->second->getButtons()) {
                if (button->item.name == key + " name") {
                    button->item.label = pplay::tr("Name") + ": " + (name.empty() ? pplay::tr("<empty>") : name);
                } else if (button->item.name == key + " url") {
                    button->item.label = pplay::tr("URL") + ": " + (url.empty() ? pplay::tr("<empty>") : url);
                } else if (button->item.name == key + " type") {
                    button->item.label = pplay::tr("Type") + ": " + pplay::tr(type);
                } else if (button->item.name == key + " user") {
                    std::string user = main->getConfig()->getOption(PPLAYConfig::networkUserOption(i))->getString();
                    button->item.label = pplay::tr("User") + ": " + (user.empty() ? pplay::tr("<empty>") : user);
                } else if (button->item.name == key + " password") {
                    bool set = !main->getConfig()->getOption(PPLAYConfig::networkPasswordOption(i))->getString().empty();
                    button->item.label = pplay::tr("Password") + ": " + (set ? "******" : pplay::tr("<empty>"));
                } else if (button->item.name == key + " token") {
                    // its start only
                    std::string token = main->getConfig()->getOption(PPLAYConfig::networkTokenOption(i))->getString();
                    button->item.label = pplay::tr("Token") + ": "
                                         + (token.empty() ? pplay::tr("<empty>")
                                                          : token.size() > 9 ? token.substr(0, 9) + "..." : token);
                } else if (button->item.name == key + " search") {
                    button->item.label = pplay::tr("Search servers");
                } else if (button->item.name == key + " timeout") {
                    button->item.label = pplay::tr("Connection timeout");
                } else if (button->item.name == key + " tls") {
                    button->item.label = pplay::tr("Certificate check");
                } else if (button->item.name == key + " sync") {
                    button->item.label = pplay::tr("Synchronization");
                } else if (button->item.name == key + " smb") {
                    button->item.label = pplay::tr("SMB buffer");
                } else if (button->item.name == key + " up") {
                    button->item.label = pplay::tr("Move up");
                } else if (button->item.name == key + " down") {
                    button->item.label = pplay::tr("Move down");
                } else if (button->item.name == key + " delete") {
                    button->item.label = pplay::tr("Delete network");
                }
                button->refreshText();
            }
        }
        for (const char *suffix: {" name", " url", " user", " password", " token", " type", " tls", " search"}) {
            auto sub = menuMainOptionsSubmenus.find(key + suffix);
            if (sub != menuMainOptionsSubmenus.end()) {
                std::string what = std::string(suffix) == " name"       ? "Name"
                                   : std::string(suffix) == " url"      ? "URL"
                                   : std::string(suffix) == " user"     ? "User"
                                   : std::string(suffix) == " password" ? "Password"
                                   : std::string(suffix) == " token"    ? "Token"
                                   : std::string(suffix) == " type"     ? "Type"
                                   : std::string(suffix) == " tls"      ? "Certificate check"
                                                                        : "Search servers";
                sub->second->setTitle(label + ": " + pplay::tr(what));
            }
        }
    }
}

void MenuMain::openSettings(bool fromPlayer) {
    menuMainOptions->fromPlayer = fromPlayer;
    menuMainOptions->setSide(!fromPlayer);
    for (auto &submenu: menuMainOptionsSubmenus) {
        submenu.second->setSide(!fromPlayer);
    }
    if (fileEditor != nullptr) {
        fileEditor->setSide(!fromPlayer);
    }
    menuMainOptions->setVisibility(Visibility::Visible);
}

void MenuMain::closePlayerSettings() {
    if (!menuMainOptions->fromPlayer) {
        return;
    }
    menuMainOptions->setVisibility(Visibility::Hidden, true);
    for (auto &submenu: menuMainOptionsSubmenus) {
        submenu.second->setVisibility(Visibility::Hidden, true);
    }
    if (fileEditor != nullptr) {
        fileEditor->setVisibility(Visibility::Hidden, true);
    }
}

MenuFileEditor *MenuMain::getFileEditor() {
    return fileEditor;
}

void MenuMain::reloadModules() {
    updateNetworkLabels();
    std::vector<bool> shownBefore;
    for (auto &button: buttons) {
        shownBefore.push_back(button->item.enabled);
    }
    // shown with something in them (or opened)
    for (auto &button: buttons) {
        int id = button->item.id;
        if (id == MODULE_SAVED || id == MODULE_PLAYLISTS) {
            // hidden by the settings: still there while open
            bool allowed = main->getConfig()->getOption(id == MODULE_SAVED ? OPT_SHOW_SAVED : OPT_SHOW_PLAYLISTS)
                                   ->getInteger() != 0;
            bool shown = main->getcurrentModuleIndex() == id
                         || (allowed && (id == MODULE_SAVED ? pplay::Saved::any() : pplay::Playlists::any()));
            button->item.enabled = shown;
            button->item.selectable = shown;
        }
    }
    for (auto &button: buttons) {
        if (button->item.id < 1 || button->item.id > main->getConfig()->getNetworkCount()) {
            continue;
        }

        const int networkIndex = button->item.id;
        const std::string url = main->getConfig()->getOption(PPLAYConfig::networkOption(networkIndex))->getString();
        std::string name = main->getConfig()->getOption(PPLAYConfig::networkNameOption(networkIndex))->getString();
        if (name.empty() || name == "Network " + std::to_string(networkIndex)) {
            name = pplay::tr("Network") + " " + std::to_string(networkIndex);
        }

        button->item.label = name;
        button->setIcon(networkIcon(main->getConfig(), networkIndex));
        button->item.enabled = !url.empty();
        button->item.selectable = !url.empty();
        button->refreshText();
        button->setVisibility(!url.empty() && button->isVisible() ? Visibility::Visible : Visibility::Hidden);
    }

    // the networks apart: a line above the first one shown, another under the last one
    MenuButton *firstNetwork = nullptr;
    for (auto &button: buttons) {
        if (button->item.id >= 1 && button->item.id <= main->getConfig()->getNetworkCount() && button->item.enabled) {
            firstNetwork = firstNetwork == nullptr ? button : firstNetwork;
        }
    }
    for (auto &button: buttons) {
        bool network = button->item.id >= 1 && button->item.id <= main->getConfig()->getNetworkCount();
        button->setLine(firstNetwork != nullptr && (button == firstNetwork || (!network && button->item.name == "Settings")));
    }

    // other rows: the height (and the slide) made to them again
    for (size_t i = 0; i < buttons.size(); i++) {
        if (buttons[i]->item.enabled != shownBefore[i]) {
            fit();
            break;
        }
    }

    if (!isButtonSelectable(index)) {
        reset();
    } else {
        updateSelectionState();
        ensureSelectionVisible();
    }
    updateScroll();
    updateSelectionHighlight();
}

void MenuMain::setSelection(int moduleId) {
    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "MenuMain::setSelection moduleId=" + std::to_string(moduleId));
    for (auto &button: buttons) {
        pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                            "MenuMain::setSelection btnId=" + std::to_string(button->item.id));
        if (button->item.id == moduleId) {
            current->setVisibility(button->isVisible() ? Visibility::Visible : Visibility::Hidden);
            if (isVisible()) {
                pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                                    "MenuMain::setSelection btnId=" + std::to_string(button->item.id) + " tweenTo()");
                current->tweenTo(button->getPosition());
            } else {
                pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                                    "MenuMain::setSelection btnId=" + std::to_string(button->item.id)
                                            + " setPosition()");
                current->setPosition(button->getPosition());
                current->tweenTo(button->getPosition());
            }
            break;
        }
    }
}

void MenuMain::updateSelectionHighlight() {
    int index = main->getcurrentModuleIndex();
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                        "MenuMain::updateSelectionHighlight index=" + std::to_string(index));
    setSelection(index);
}

void MenuMain::onOptionSelection(MenuItem *item) {
    if (item->name == "Local") {
        setVisibility(Visibility::Hidden, true);
        main->setCurrentModuleIndex(item->id);
        main->show(Main::MenuType::Local);
    } else if (item->name == "Settings") {
        setVisibility(Visibility::Hidden, true);
        openSettings(false);
    } else if (item->name == "Playlists") {
        setVisibility(Visibility::Hidden, true);
        main->setCurrentModuleIndex(item->id);
        main->show(Main::MenuType::Playlists);
    } else if (item->name == "Saved") {
        setVisibility(Visibility::Hidden, true);
        main->setCurrentModuleIndex(item->id);
        main->show(Main::MenuType::Saved);
#ifdef __SWITCH__
    } else if (item->name == "Usb") {
        setVisibility(Visibility::Hidden, true);
        main->setCurrentModuleIndex(item->id);
        main->show(Main::MenuType::Usb);
#endif
    } else if (item->id) {
        setVisibility(Visibility::Hidden, true);
        main->setCurrentModuleIndex(item->id);
        main->show(Main::MenuType::Network);
    } else if (item->name == "Exit") {
        main->quit();
    }
}

void MenuMain::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    if (visibility == Visibility::Visible) {
        reloadModules();
    }
    int index = main->getcurrentModuleIndex();
    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "MenuMain::setVisibility highlight=" + std::to_string(index));
    Menu::setVisibility(visibility, tweenPlay);
    setSelection(index);
}

bool MenuMain::onInput(c2d::Input::Player *players) {
    // the arrows change values only (none here): A opens, B goes back
    if (players[0].buttons & Input::B) {
        setVisibility(Visibility::Hidden, true);
        return true;
    }

    bool result = Menu::onInput(players);
    int index = main->getcurrentModuleIndex();
    setSelection(index);
    return result;
}

bool MenuMain::isMenuVisible() {
    if (isVisible() || menuMainOptions->isVisible()) {
        return true;
    }

    for (auto &submenu: menuMainOptionsSubmenus) {
        if (submenu.second->isVisible()) {
            return true;
        }
    }
    if (fileEditor != nullptr && fileEditor->isVisible()) {
        return true;
    }

    return false;
}

void MenuMain::showParent(const std::string &key) {
    auto parent = parents.find(key);
    MenuMainOptionsSubmenu *submenu = parent != parents.end() ? getMenuMainOptionsSubmenu(parent->second) : nullptr;
    if (submenu != nullptr) {
        submenu->setVisibility(Visibility::Visible, true);
    } else {
        menuMainOptions->setVisibility(Visibility::Visible, true);
    }
}

MenuMainOptionsSubmenu *MenuMain::getMenuMainOptionsSubmenu(const std::string &name) {
    auto submenu = menuMainOptionsSubmenus.find(name);
    if (submenu != menuMainOptionsSubmenus.end()) {
        submenu->second->refresh();
        return submenu->second;
    }

    return nullptr;
}
