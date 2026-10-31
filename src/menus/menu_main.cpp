//
// Created by cpasjuste on 07/12/18.
//

#include <utility>

#include "main.h"
#include "menu_main.h"
#include "menu_file_editor.h"
#include "menu_main_items.h"
#include "io.h"
#include "i18n.h"

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
                                  MenuItem("Short seek step", "short_seek.png", Pos::Top),
                                  MenuItem("Long seek step", "long_seek.png", Pos::Top),
                                  MenuItem("Playback retry", "pl_retry.png", Pos::Top),
                                  MenuItem("Audio language", "audio.png", Pos::Top),
                                  MenuItem("Subtitles language", "subtitles.png", Pos::Top),
                                  MenuItem("External tracks", "external.png", Pos::Top),
                                  MenuItem("Volume normalization", "normalize.png", Pos::Top),
                                  MenuItem("Visualization", "visualizer.png", Pos::Top),
                                  MenuItem("Video quality", "quality.png", Pos::Top),
                                  MenuItem("Buffer length", "buffer.png", Pos::Top),
                                  MenuItem("Buffer memory", "buffer.png", Pos::Top),
                          }});
    categories.push_back({"Interface",
                          {
                                  MenuItem("Language", "translate.png", Pos::Top),
                                  MenuItem("Text encoding", "translate.png", Pos::Top),
                                  MenuItem("Accent color", "color.png", Pos::Top),
                                  MenuItem("Swap controls", "swap.png", Pos::Top),
                          }});
    std::vector<MenuItem> networks;
    for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
        networks.emplace_back(networkKey(i), "network.png", Pos::Top);
    }
    networks.emplace_back("Connection timeout", "timeout.png", Pos::Top);
    networks.emplace_back("TorrServer progress", "time.png", Pos::Top);
#ifdef __SMB2__
    networks.emplace_back("SMB buffer", "buffer.png", Pos::Top);
#endif
    categories.push_back({"Networks", networks});
    std::vector<MenuItem> system = {
            MenuItem("Local home path", "home.png", Pos::Top),
            MenuItem("Edit files", "edit.png", Pos::Top),
            MenuItem("Media information", "media_info.png", Pos::Top),
            MenuItem("Clear cache", "clear_cache.png", Pos::Top),
    };
#ifdef __SWITCH__
    system.emplace_back("CPU boost", "cpu.png", Pos::Top);
#endif
    system.emplace_back("Logging", "log.png", Pos::Top);
    categories.push_back({"System", system});

    std::vector<MenuItem> it = {
            MenuItem("Playback", "pl_mode.png", Pos::Top), MenuItem("Interface", "interface.png", Pos::Top),
            MenuItem("Networks", "network.png", Pos::Top), MenuItem("System", "system.png", Pos::Top),
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
    const FloatRect wide = {-420 * main->getScaling().x, rect.top, 420 * main->getScaling().x, rect.height};

    menuMainOptions = new MenuMainOptions(main, wide, it);
    // over the shade under the menus (see Main)
    menuMainOptions->setLayer(3);
    menuMainOptions->setVisibility(Visibility::Hidden, false);
    main->add(menuMainOptions);

    auto addSubmenu = [&](const std::string &name, const std::string &title, const std::string &optionName,
                          const std::vector<MenuItem> &submenuItems,
                          Submenu::ValueType valueType = Submenu::ValueType::String,
                          Submenu::MenuType menuType = Submenu::MenuType::List, float minValue = 0.0f,
                          float maxValue = 0.0f, float stepValue = 1.0f, const std::string &unit = "") {
        auto *submenu = new MenuMainOptionsSubmenu(main, wide, title, submenuItems, optionName, valueType, menuType,
                                                   minValue, maxValue, stepValue, unit);
        submenu->setLayer(3);
        submenu->setVisibility(Visibility::Hidden, false);
        submenu->setKey(name);
        submenu->refresh();
        main->add(submenu);
        menuMainOptionsSubmenus[name] = submenu;
    };

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

    for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
        const std::string name = networkKey(i);
        addSubmenu(name, name, "",
                   {MenuItem(name + " name", "", MenuItem::Position::Top),
                    MenuItem(name + " url", "", MenuItem::Position::Top),
                    MenuItem(name + " type", "", MenuItem::Position::Top)},
                   Submenu::ValueType::String);
        parents[name + " name"] = name;
        parents[name + " url"] = name;
        parents[name + " type"] = name;
        addTextSubmenu(name + " name", name + " name", PPLAYConfig::networkNameOption(i));
        addTextSubmenu(name + " url", name + " url", PPLAYConfig::networkOption(i));
        std::vector<MenuItem> typeItems;
        for (auto &typeName: pplay::Io::networkTypeNames()) {
            typeItems.emplace_back(typeName, "", MenuItem::Position::Top);
        }
        addSubmenu(name + " type", name + " type", PPLAYConfig::networkTypeOption(i), typeItems,
                   Submenu::ValueType::String);
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
                             {"Cyrillic (1251)", 1251},
                             {"Central European (1250)", 1250},
                             {"Western (1252)", 1252},
                             {"Greek (1253)", 1253},
                             {"Turkish (1254)", 1254},
                             {"Hebrew (1255)", 1255},
                             {"Arabic (1256)", 1256},
                             {"Baltic (1257)", 1257}}),
               Submenu::ValueType::Integer);
    addTextSubmenu("Local home path", "Local home path", OPT_HOME_PATH);

#ifdef __SWITCH__
    addSubmenu("CPU boost", "CPU boost", OPT_CPU_BOOST,
               {MenuItem("Disabled", "", MenuItem::Position::Top), MenuItem("Enabled", "", MenuItem::Position::Top)});
#endif

    addSubmenu("Connection timeout", "Connection timeout", OPT_NETWORK_TIMEOUT, makeAdjustItems(),
               Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 1.0f, 300.0f, 5.0f, "s");
    addSubmenu("TorrServer progress", "Playback position on the server", OPT_TS_PROGRESS,
               makeIntItems({{"Off", 0}, {"On", 1}}), Submenu::ValueType::Integer);
    addSubmenu("Playback retry", "Playback retry", OPT_NETWORK_RETRIES, makeAdjustItems(), Submenu::ValueType::Integer,
               Submenu::MenuType::Adjust, 0.0f, 10.0f, 1.0f);
#ifdef __SMB2__
    addSubmenu("SMB buffer", "SMB preload buffer", OPT_SMB_READ_BUFFER_MB, makeAdjustItems(),
               Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 1.0f, 100.0f, 1.0f, "MB");
#endif
    addSubmenu("Playback mode", "At playback end..", OPT_AUTOPLAY_MODE,
               makeIntItems({{"Stop", 0}, {"Play next", 1}, {"Loop file", 2}, {"Loop directory", 3}}),
               Submenu::ValueType::Integer);
    addSubmenu("Swap controls", "Triggers and buttons", OPT_SWAP_CONTROLS,
               makeIntItems({{"Normal", 0}, {"Swapped", 1}}), Submenu::ValueType::Integer);
    addSubmenu("Short seek step", "Seek short step", OPT_SEEK_SHORT_SEC, makeAdjustItems(), Submenu::ValueType::Float,
               Submenu::MenuType::Adjust, 1.0f, 600.0f, 1.0f, "s");
    addSubmenu("Long seek step", "Seek long step", OPT_SEEK_LONG_SEC, makeAdjustItems(), Submenu::ValueType::Float,
               Submenu::MenuType::Adjust, 10.0f, 3600.0f, 10.0f, "s");
    addSubmenu("Audio language", "Audio language", OPT_AUDIO_LANGUAGE, makeTrackLanguageItems("Auto"),
               Submenu::ValueType::String);
    addSubmenu("Subtitles language", "Subtitles language", OPT_SUB_LANGUAGE, makeTrackLanguageItems("Off"),
               Submenu::ValueType::String);
    addSubmenu("External tracks", "External audio and subtitles", OPT_EXTERNAL_TRACKS,
               makeIntItems({{"Off", 0}, {"On", 1}}), Submenu::ValueType::Integer);
    addSubmenu("Volume normalization", "Volume normalization", OPT_AUDIO_NORMALIZE,
               makeIntItems({{"Off", 0}, {"Normalize", 1}, {"Night mode", 2}}), Submenu::ValueType::Integer);
    addSubmenu("Visualization", "Audio files screen", OPT_AUDIO_VISUALIZER,
               makeIntItems({{"Cover > visualizer", 0}, {"Visualizer", 1}, {"Icon only", 2}}),
               Submenu::ValueType::Integer);
    addSubmenu("Media information", "Media information on selection", OPT_MEDIA_PROBE,
               makeIntItems({{"Off", 0}, {"On", 1}}), Submenu::ValueType::Integer);
    addSubmenu("Video quality", "Video quality", OPT_VIDEO_QUALITY,
               makeIntItems({{"Best", 0}, {"Balanced", 1}, {"Fast", 2}}), Submenu::ValueType::Integer);
    addSubmenu("Buffer length", "Network buffer length", OPT_CACHE_SECS, makeAdjustItems(), Submenu::ValueType::Integer,
               Submenu::MenuType::Adjust, 10.0f, 600.0f, 10.0f, "s");
    addSubmenu("Buffer memory", "Network buffer memory", OPT_CACHE_MB, makeAdjustItems(), Submenu::ValueType::Integer,
               Submenu::MenuType::Adjust, 50.0f, (float) CACHE_MB_MAX, 50.0f, "MB");
    addSubmenu("Logging", "Log to file", OPT_LOG_LEVEL,
               makeIntItems({{"Off", 0}, {"Error", 1}, {"Info", 2}, {"Debug", 3}, {"Trace", 4}}),
               Submenu::ValueType::Integer);
    addSubmenu("Accent color", "Accent color", OPT_ACCENT_COLOR, makeAccentColorItems(), Submenu::ValueType::String);

    fileEditor = new MenuFileEditor(main, rect);
    fileEditor->setLayer(3);
    fileEditor->setVisibility(Visibility::Hidden, false);
    main->add(fileEditor);

    updateNetworkLabels();
}

void MenuMain::updateNetworkLabels() {
    for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
        const std::string key = networkKey(i);
        const std::string name = main->getConfig()->getOption(PPLAYConfig::networkNameOption(i))->getString();
        const std::string url = main->getConfig()->getOption(PPLAYConfig::networkOption(i))->getString();
        const std::string type = main->getConfig()->getOption(PPLAYConfig::networkTypeOption(i))->getString();
        // the item name stays the menu key
        std::string label = pplay::tr("Network") + " " + std::to_string(i);
        if (!url.empty() && !name.empty() && name != key) {
            label += " (" + name + ")";
        }
        for (auto *button: getMenuMainOptionsSubmenu("Networks")->getButtons()) {
            if (button->item.name == key) {
                button->item.label = label;
                button->refreshText();
            }
        }
        auto submenu = menuMainOptionsSubmenus.find(key);
        if (submenu != menuMainOptionsSubmenus.end()) {
            submenu->second->setTitle(label);
            for (auto *button: submenu->second->getButtons()) {
                if (button->item.name == key + " name") {
                    button->item.label = pplay::tr("Name") + ": " + name;
                } else if (button->item.name == key + " url") {
                    button->item.label = pplay::tr("URL") + ": " + (url.empty() ? pplay::tr("<empty>") : url);
                } else if (button->item.name == key + " type") {
                    button->item.label = pplay::tr("Type") + ": " + pplay::tr(type);
                }
                button->refreshText();
            }
        }
        for (const char *suffix: {" name", " url", " type"}) {
            auto sub = menuMainOptionsSubmenus.find(key + suffix);
            if (sub != menuMainOptionsSubmenus.end()) {
                std::string what = std::string(suffix) == " name"  ? "Name"
                                   : std::string(suffix) == " url" ? "URL"
                                                                   : "Type";
                sub->second->setTitle(label + ": " + pplay::tr(what));
            }
        }
    }
}

MenuFileEditor *MenuMain::getFileEditor() {
    return fileEditor;
}

void MenuMain::reloadModules() {
    updateNetworkLabels();
    for (auto &button: buttons) {
        if (button->item.id < 1 || button->item.id > PPLAY_MAX_NETWORKS) {
            continue;
        }

        const int networkIndex = button->item.id;
        const std::string url = main->getConfig()->getOption(PPLAYConfig::networkOption(networkIndex))->getString();
        std::string name = main->getConfig()->getOption(PPLAYConfig::networkNameOption(networkIndex))->getString();
        if (name.empty() || name == "Network " + std::to_string(networkIndex)) {
            name = pplay::tr("Network") + " " + std::to_string(networkIndex);
        }

        button->item.label = name;
        button->item.enabled = !url.empty();
        button->item.selectable = !url.empty();
        button->refreshText();
        button->setVisibility(!url.empty() && button->isVisible() ? Visibility::Visible : Visibility::Hidden);
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
        menuMainOptions->setVisibility(Visibility::Visible);
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

MenuMainOptions *MenuMain::getMenuMainOptions() {
    return menuMainOptions;
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
