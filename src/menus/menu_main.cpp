//
// Created by cpasjuste on 07/12/18.
//

#include <utility>

#include "main.h"
#include "menu_main.h"

using namespace c2d;

namespace {
    using Submenu = MenuMainOptionsSubmenu;

    std::vector<MenuItem> makeAdjustItems() {
        std::vector<MenuItem> items;
        items.emplace_back("Value", "", MenuItem::Position::Top, 0, false);
        items.emplace_back("-", "", MenuItem::Position::Top, -1);
        items.emplace_back("+", "", MenuItem::Position::Top, 1);
        return items;
    }

    std::vector<MenuItem> makeIntItems(const std::vector<std::pair<std::string, int>> &values) {
        std::vector<MenuItem> items;
        for (auto &value: values) {
            items.emplace_back(value.first, "", MenuItem::Position::Top, value.second);
        }
        return items;
    }
}

MenuMain::MenuMain(Main *main, const c2d::FloatRect &rect, const std::vector<MenuItem> &items)
        : Menu(main, rect, "pPlay v" APP_VERSION, items, true) {
    std::vector<MenuItem> it;

    it.emplace_back("Playback mode", "pl_mode.png", MenuItem::Position::Top);
    it.emplace_back("Short seek step", "short_seek.png", MenuItem::Position::Top);
    it.emplace_back("Long seek step", "long_seek.png", MenuItem::Position::Top);
    it.emplace_back("Swap controls", "swap.png", MenuItem::Position::Top);
    it.emplace_back("Accent color", "color.png", MenuItem::Position::Top);
    it.emplace_back("Connection timeout", "timeout.png", MenuItem::Position::Top);
    for (int i = 1; i <= 5; i++) {
        it.emplace_back("Network " + std::to_string(i), "network.png", MenuItem::Position::Top);
    }
    it.emplace_back("Local home path", "home.png", MenuItem::Position::Top);
#ifdef PPLAY_ENABLE_SCRAPPING
    it.emplace_back("TMDB language", "home.png", MenuItem::Position::Top);
#endif
    it.emplace_back("Playback retry", "pl_retry.png", MenuItem::Position::Top);
#ifdef __SMB2__
    it.emplace_back("SMB buffer", "buffer.png", MenuItem::Position::Top);
#endif
#ifdef PPLAY_ENABLE_SCRAPPING
    it.emplace_back("Scrapping", "", MenuItem::Position::Top);
    it.emplace_back("Cache", "", MenuItem::Position::Top);
#endif
#ifdef __SWITCH__
    it.emplace_back("CPU", "cpu.png", MenuItem::Position::Top);
#endif
    it.emplace_back("Logging", "log.png", MenuItem::Position::Top);

    menuMainOptions = new MenuMainOptions(main, rect, it);
    menuMainOptions->setLayer(2);
    menuMainOptions->setVisibility(Visibility::Hidden, false);
    main->add(menuMainOptions);

    auto addSubmenu = [&](const std::string &name, const std::string &title, const std::string &optionName,
                          const std::vector<MenuItem> &submenuItems,
                          Submenu::ValueType valueType = Submenu::ValueType::String,
                          Submenu::MenuType menuType = Submenu::MenuType::List,
                          float minValue = 0.0f, float maxValue = 0.0f,
                          float stepValue = 1.0f, const std::string &unit = "") {
        auto *submenu = new MenuMainOptionsSubmenu(main, rect, title, submenuItems, optionName,
                                                   valueType, menuType, minValue, maxValue, stepValue, unit);
        submenu->setLayer(2);
        submenu->setVisibility(Visibility::Hidden, false);
        submenu->refresh();
        main->add(submenu);
        menuMainOptionsSubmenus[name] = submenu;
    };

    auto addTextSubmenu = [&](const std::string &name, const std::string &title, const std::string &optionName) {
        addSubmenu(name, title, optionName,
                   {MenuItem("", "", MenuItem::Position::Top)},
                   Submenu::ValueType::String, Submenu::MenuType::TextInput);
    };

    for (int i = 1; i <= 5; i++) {
        const std::string name = "Network " + std::to_string(i);
        addSubmenu(name, name, "",
                   {MenuItem(PPLAYConfig::networkOption(i), "", MenuItem::Position::Top),
                    MenuItem(PPLAYConfig::networkNameOption(i), "", MenuItem::Position::Top)},
                   Submenu::ValueType::String);
        addTextSubmenu(PPLAYConfig::networkOption(i), PPLAYConfig::networkOption(i), PPLAYConfig::networkOption(i));
        addTextSubmenu(PPLAYConfig::networkNameOption(i), PPLAYConfig::networkNameOption(i), PPLAYConfig::networkNameOption(i));
    }
    addTextSubmenu("HOME_PATH", "HOME_PATH", OPT_HOME_PATH);
#ifdef PPLAY_ENABLE_SCRAPPING
    addTextSubmenu("TMDB_LANGUAGE", "TMDB_LANGUAGE", OPT_TMDB_LANGUAGE);
#endif

#ifdef __SWITCH__
    addSubmenu("CPU", "CPU", OPT_CPU_BOOST,
               {MenuItem("Disabled", "", MenuItem::Position::Top),
                MenuItem("Enabled", "", MenuItem::Position::Top)});
#endif

    addSubmenu("Connection timeout", "Connection timeout", OPT_NETWORK_TIMEOUT, makeAdjustItems(),
               Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 1.0f, 300.0f, 5.0f, "s");
    addSubmenu("Playback retry", "Playback retry", OPT_NETWORK_RETRIES, makeAdjustItems(),
               Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 0.0f, 10.0f, 1.0f);
#ifdef __SMB2__
    addSubmenu("SMB buffer", "SMB preload buffer", OPT_SMB_READ_BUFFER_MB, makeAdjustItems(),
               Submenu::ValueType::Integer, Submenu::MenuType::Adjust, 1.0f, 100.0f, 1.0f, "MB");
#endif
    addSubmenu("Playback mode", "At playback end..", OPT_AUTOPLAY_MODE,
               makeIntItems({{"Stop", 0}, {"Play next", 1},
                             {"Loop file", 2}, {"Loop directory", 3}}), Submenu::ValueType::Integer);
    addSubmenu("Swap controls", "Triggers and buttons", OPT_SWAP_CONTROLS,
               makeIntItems({{"Normal", 0}, {"Swapped", 1}}), Submenu::ValueType::Integer);
    addSubmenu("Short seek step", "Seek short step", OPT_SEEK_SHORT_SEC, makeAdjustItems(),
               Submenu::ValueType::Float, Submenu::MenuType::Adjust, 1.0f, 600.0f, 1.0f, "s");
    addSubmenu("Long seek step", "Seek long step", OPT_SEEK_LONG_SEC, makeAdjustItems(),
               Submenu::ValueType::Float, Submenu::MenuType::Adjust, 10.0f, 3600.0f, 10.0f, "s");
    addSubmenu("Logging", "Log to file", OPT_LOG_LEVEL,
               makeIntItems({{"Off", 0}, {"Error", 1}, {"Info", 2}, {"Debug", 3}, {"Trace", 4}}),
               Submenu::ValueType::Integer);
    addSubmenu("Accent color", "Accent color", OPT_ACCENT_COLOR,
                {MenuItem("Base Blue", "", MenuItem::Position::Top, 0, true, "#1078C8"),
                MenuItem("Muted Cyan", "", MenuItem::Position::Top, 0, true, "#008EA0"),
                MenuItem("Muted Crimson", "", MenuItem::Position::Top, 0, true, "#D64545"),
                MenuItem("Soft Crimson", "", MenuItem::Position::Top, 0, true, "#EB4D4B"),
                MenuItem("Terracotta Orange", "", MenuItem::Position::Top, 0, true, "#DB6B30"),
                MenuItem("Burnt Orange", "", MenuItem::Position::Top, 0, true, "#FA8231"),
                MenuItem("Ochre Yellow", "", MenuItem::Position::Top, 0, true, "#C29346"),
                MenuItem("Olive Gold", "", MenuItem::Position::Top, 0, true, "#9EA133"),
                MenuItem("Olive Lime", "", MenuItem::Position::Top, 0, true, "#88B04B"),
                MenuItem("Sage Green", "", MenuItem::Position::Top, 0, true, "#5D9E54"),
                MenuItem("Soft Emerald", "", MenuItem::Position::Top, 0, true, "#3A9668"),
                MenuItem("Bright Emerald", "", MenuItem::Position::Top, 0, true, "#20BF6B"),
                MenuItem("Deep Turquoise", "", MenuItem::Position::Top, 0, true, "#2E8B96"),
                MenuItem("Slate Blue", "", MenuItem::Position::Top, 0, true, "#4378A6"),
                MenuItem("Dark Indigo", "", MenuItem::Position::Top, 0, true, "#535EA8"),
                MenuItem("Soft Purple", "", MenuItem::Position::Top, 0, true, "#A55EEA"),
                MenuItem("Amethyst Purple", "", MenuItem::Position::Top, 0, true, "#7A5299"),
                MenuItem("Plum Violet", "", MenuItem::Position::Top, 0, true, "#9C498D"),
                MenuItem("Dusty Rose", "", MenuItem::Position::Top, 0, true, "#B5526C"),
                MenuItem("Deep Red", "", MenuItem::Position::Top, 0, true, "#D63031"),
                MenuItem("Classic Red", "", MenuItem::Position::Top, 0, true, "#FF0000")},
               Submenu::ValueType::String);
#ifdef PPLAY_ENABLE_SCRAPPING
    addSubmenu("Scrapping", "Scrape", OPT_ENABLE_SCRAPPING,
               makeIntItems({{"Off", 0}, {"On", 1}}), Submenu::ValueType::Integer);
    addSubmenu("Cache", "Cache", OPT_CACHE_MEDIA_INFO,
               makeIntItems({{"Off", 0}, {"On", 1}}), Submenu::ValueType::Integer);
#endif

    // highlight
    highlight_selection = new Highlight({MenuMain::getSize().x, BUTTON_HEIGHT * main->getScaling().y},
                                        Highlight::CursorPosition::Left);
    highlight_selection->setFillColor(COLOR_ACCENT);
    highlight_selection->setAlpha(60);
    highlight_selection->setCursorColor(COLOR_ACCENT);
    highlight_selection->setOrigin(Origin::Left);
    highlight_selection->setPosition(0, 200 * main->getScaling().y);
    highlight_selection->setLayer(-1);
    MenuMain::add(highlight_selection);
}

void MenuMain::reloadModules() {
    for (auto &button: buttons) {
        if (button->item.id < 1 || button->item.id > 5) {
            continue;
        }

        const int networkIndex = button->item.id;
        const std::string url = main->getConfig()->getOption(PPLAYConfig::networkOption(networkIndex))->getString();
        std::string name = main->getConfig()->getOption(PPLAYConfig::networkNameOption(networkIndex))->getString();
        if (name.empty()) {
            name = "Network " + std::to_string(networkIndex);
        }

        button->item.name = name;
        button->item.enabled = !url.empty();
        button->item.selectable = !url.empty();
        button->name->setString(name);
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
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
        "MenuMain::setSelection moduleId=" + std::to_string(moduleId));
    for (auto &button: buttons) {
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
            "MenuMain::setSelection btnId=" + std::to_string(button->item.id));
        if (button->item.id == moduleId) {
            highlight_selection->setVisibility(button->isVisible() ? Visibility::Visible : Visibility::Hidden);
            if (isVisible()) {
                pplay::Utility::log(pplay::Utility::LogLevel::Info,
                    "MenuMain::setSelection btnId=" + std::to_string(button->item.id)
                    + " tweenTo()");
                highlight_selection->tweenTo(button->getPosition());
            } else {
                pplay::Utility::log(pplay::Utility::LogLevel::Info,
                    "MenuMain::setSelection btnId=" + std::to_string(button->item.id)
                    + " setPosition()");
                highlight_selection->setPosition(button->getPosition());
                highlight_selection->tweenTo(button->getPosition());
            }
            break;
        }
    }
}

void MenuMain::updateSelectionHighlight() {
    int index = main->getcurrentModuleIndex();
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
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
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
        "MenuMain::setVisibility highlight=" + std::to_string(index));
    Menu::setVisibility(visibility, tweenPlay);
    setSelection(index);
}

bool MenuMain::onInput(c2d::Input::Player *players) {
    if (players[0].buttons & Input::Right || players[0].buttons & Input::B) {
        setVisibility(Visibility::Hidden, true);
        return true;
    }

    if (players[0].buttons & Input::Left) {
        MenuItem *item = getSelection();
        if (item->name == "Settings") {
            onOptionSelection(item);
        }
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

    return false;
}

MenuMainOptions *MenuMain::getMenuMainOptions() {
    return menuMainOptions;
}

MenuMainOptionsSubmenu *MenuMain::getMenuMainOptionsSubmenu(const std::string &name) {
    auto submenu = menuMainOptionsSubmenus.find(name);
    if (submenu != menuMainOptionsSubmenus.end()) {
        submenu->second->refresh();
        return submenu->second;
    }

    return nullptr;
}

void MenuMain::onUpdate() {
    highlight_selection->setFillColor(COLOR_ACCENT);
    highlight_selection->setAlpha(60);
    highlight_selection->setCursorColor(COLOR_ACCENT);
    Menu::onUpdate();
}
