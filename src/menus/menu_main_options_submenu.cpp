//
// Created by cpasjuste on 07/12/18.
//

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "main.h"
#include "log_view.h"
#include "utility.h"
#include "menu_main_options_submenu.h"
#include "menu_file_editor.h"

#include "keyboard.h"
#include "i18n.h"
#include "input_map.h"
#include "io.h"
#include "discovery.h"

using namespace c2d;

MenuMainOptionsSubmenu::MenuMainOptionsSubmenu(Main *main, const c2d::FloatRect &rect, const std::string &_title,
                                               const std::vector<MenuItem> &items, const std::string &optionName,
                                               ValueType valueType, MenuType menuType, float minValue, float maxValue,
                                               float stepValue, const std::string &unit)
    : Menu(main, rect, _title, items, true) {

    option_name = optionName;
    value_type = valueType;
    menu_type = menuType;
    min_value = minValue;
    max_value = maxValue;
    step_value = stepValue;
    value_unit = unit;
    refresh();
}

namespace {
    std::string formatTimeMMSS(float seconds) {
        int min = static_cast<int>(seconds) / 60;
        int sec = static_cast<int>(std::round(seconds)) % 60;
        std::ostringstream oss;
        oss << min << ":" << std::setfill('0') << std::setw(2) << sec;
        return oss.str();
    }
}

std::string MenuMainOptionsSubmenu::formatValue(float value) const {
    std::ostringstream oss;

    if (PPLAYConfig::networkOf(option_name, "_TIMEOUT") > 0) {
        return formatTimeMMSS(value);
    }

    if (value_type == ValueType::Float) {
        int precision =
                (step_value - std::floor(step_value) > 0.0f) && (std::fmod(step_value * 10.0f, 1.0f) > 0.0f) ? 2 : 1;
        oss << std::fixed << std::setprecision(precision) << value;
    } else {
        oss << (int) value;
    }

    if (!value_unit.empty()) {
        oss << " " << pplay::tr(value_unit);
    }

    return oss.str();
}

float MenuMainOptionsSubmenu::getCurrentValue() const {
    auto *option = main->getConfig()->getOption(option_name);
    if (option->getType() == c2d::config::Option::Type::String) {
        const std::string stringValue = option->getString();
        char *end = nullptr;
        const float value = std::strtof(stringValue.c_str(), &end);
        if (end != stringValue.c_str()) {
            return value;
        }
    }

    if (value_type == ValueType::Float) {
        return option->getFloat();
    }

    return (float) option->getInteger();
}

void MenuMainOptionsSubmenu::setCurrentValue(float value) {
    value = std::max(min_value, std::min(value, max_value));

    auto *option = main->getConfig()->getOption(option_name);
    if (value_type == ValueType::Float) {
        option->setType(c2d::config::Option::Type::Float);
        option->setFloat(value);
    } else {
        option->setType(c2d::config::Option::Type::Integer);
        option->setInteger((int) value);
    }
}

void MenuMainOptionsSubmenu::updateSelectionHighlight() {
    if (menu_type == MenuType::Adjust) {
        return;
    }

    if (menu_type == MenuType::TextInput && !buttons.empty()) {
        buttons[0]->item.name = main->getConfig()->getOption(option_name)->getString();
        if (PPLAYConfig::networkOf(option_name, "") > 0) {
            buttons[0]->item.name = PPLAYConfig::withoutCredentials(buttons[0]->item.name);
        }
        if (buttons[0]->item.name.empty()) {
            buttons[0]->item.name = "<empty>";
        }
        buttons[0]->refreshText();
        showCurrent(buttons[0]);
        return;
    }

    if (option_name.empty()) {
        return;
    }

    if (value_type == ValueType::Integer) {
        const int value = (int) getCurrentValue();
        for (auto btn: buttons) {
            if (btn->item.id == value) {
                showCurrent(btn);
                break;
            }
        }
    } else if (value_type == ValueType::String) {
        // an empty value: none of them (the items without a value)
        std::string value = main->getConfig()->getOption(option_name)->getString();
        if (!value.empty()) {
            setSelection(value);
        }
    }
}

std::string MenuMainOptionsSubmenu::currentText() {
    auto *option = main->getConfig()->getOption(option_name);
    if (menu_type == MenuType::TextInput) {
        std::string value = option->getString();
        if (PPLAYConfig::networkOf(option_name, "") > 0) {
            value = PPLAYConfig::withoutCredentials(value);
        }
        return value.empty() ? pplay::tr("<empty>") : Utility::endsWith(option_name, "_PASSWORD") ? "******" : value;
    }
    if (menu_type == MenuType::Adjust) {
        float value = getCurrentValue();
        return value == 0 && !zeroLabel.empty() ? pplay::tr(zeroLabel) : formatValue(value);
    }
    for (auto *button: buttons) {
        if (value_type == ValueType::Integer ? button->item.id == (int) getCurrentValue()
                                             : (Utility::toLower(button->item.name) == Utility::toLower(option->getString())
                                                || Utility::toLower(button->item.data_str)
                                                           == Utility::toLower(option->getString()))) {
            return button->item.text();
        }
    }
    return option->getString();
}

float MenuMainOptionsSubmenu::sliderPart() const {
    if (menu_type != MenuType::Adjust || max_value <= min_value) {
        return -1;
    }
    return std::min(1.0f, std::max(0.0f, (getCurrentValue() - min_value) / (max_value - min_value)));
}

bool MenuMainOptionsSubmenu::step(int direction) {
    if (menu_type == MenuType::TextInput || buttons.empty()) {
        return false;
    }
    if (menu_type == MenuType::Adjust) {
        // the "-" / "+" items
        MenuItem change("", "", MenuItem::Position::Top, direction);
        onOptionSelection(&change);
        return true;
    }
    std::string current = currentText();
    int count = (int) buttons.size(), index = 0;
    for (int i = 0; i < count; i++) {
        if (buttons[i]->item.text() == current) {
            index = i;
        }
    }
    for (int i = 1; i < count; i++) {
        // a held key stops at the last value
        if (pplay::InputMap::isRepeat() && (index + direction * i < 0 || index + direction * i >= count)) {
            return false;
        }
        MenuButton *next = buttons[(index + direction * i + count * i) % count];
        if (next->item.selectable && next->item.position == MenuItem::Position::Top) {
            onOptionSelection(&next->item);
            return true;
        }
    }
    return false;
}

void MenuMainOptionsSubmenu::refreshValues() {
    // (the main menu still being made)
    if (hasOption() || main->getMenuMain() == nullptr) {
        return;
    }
    // the Auto ones: shown with the Auto setting only
    PPLAYConfig *config = main->getConfig();
    bool qualityAuto = config->getOption(OPT_VIDEO_QUALITY)->getInteger() == 3;
    bool decoderAuto = false;
#ifdef PPLAY_HW_DECODING
    decoderAuto = config->getOption(OPT_HW_DECODING)->getInteger() == 2;
#endif
    bool changed = false;
    for (auto *button: buttons) {
        auto *sub = main->getMenuMain()->getMenuMainOptionsSubmenu(button->item.name);
        bool option = sub != nullptr && sub->hasOption();
        button->item.value = option ? sub->currentText() : "";
        button->item.slider = option ? sub->sliderPart() : -1;
        button->item.opens = sub != nullptr && !option;
        button->refreshValue();
        const std::string &key = sub != nullptr ? sub->getOptionName() : "";
        bool shown = true;
        const std::string &name = button->item.name;
        bool up = Utility::endsWith(name, " up"), down = Utility::endsWith(name, " down");
        int network = name.rfind("Network ", 0) == 0 ? std::atoi(name.c_str() + strlen("Network ")) : 0;
        if (sub == nullptr && (up || down) && network > 0) {
            // not past an end
            shown = up ? network > 1 : network < config->getNetworkCount();
        } else if (network > 0 && Utility::endsWith(name, " tls")) {
            // https only
            shown = Utility::startWith(config->getOption(PPLAYConfig::networkOption(network))->getString(), "https://");
        } else if (network > 0 && (Utility::endsWith(name, " sync") || Utility::endsWith(name, " smb"))) {
            pplay::Io::NetworkType type =
                    pplay::Io::parseNetworkType(config->getOption(PPLAYConfig::networkTypeOption(network))->getString());
            shown = Utility::endsWith(name, " sync") ? type == pplay::Io::NetworkType::TorrServe
                                                             || type == pplay::Io::NetworkType::TorrServeSilo
                                                     : type == pplay::Io::NetworkType::Smb;
        } else if (network > 0 && Utility::endsWith(name, " users")) {
            shown = pplay::Io::parseNetworkType(config->getOption(PPLAYConfig::networkTypeOption(network))->getString()) ==
                    pplay::Io::NetworkType::P2Play;
        } else if (network > 0 && Utility::endsWith(name, " token")) {
            // TS Silo only
            shown = pplay::Io::parseNetworkType(config->getOption(PPLAYConfig::networkTypeOption(network))->getString()) ==
                    pplay::Io::NetworkType::TorrServeSilo;
        } else if (network > 0 && (Utility::endsWith(name, " user") || Utility::endsWith(name, " password"))) {
            // DLNA has no login
            shown = pplay::Io::parseNetworkType(config->getOption(PPLAYConfig::networkTypeOption(network))->getString()) !=
                    pplay::Io::NetworkType::Dlna;
        } else if (sub != nullptr && sub->isSearch()) {
            // DLNA networks only, a list behind (no value)
            button->item.value = "";
            button->item.opens = true;
            button->refreshValue();
            shown = pplay::Discovery::supports(pplay::Io::parseNetworkType(config->getOption(
                    PPLAYConfig::networkTypeOption(std::atoi(button->item.name.c_str() + strlen("Network "))))->getString()));
        } else if (key == OPT_AUTO_DROPS || key == OPT_AUTO_DROPS_TIME || key == OPT_AUTO_QUALITY_DELAY) {
            shown = qualityAuto;
        } else if (key == OPT_AUTO_HW_RETRY || key == OPT_AUTO_HW_RETRIES) {
            shown = decoderAuto;
        } else if (key == OPT_AUTO_SWITCH_PAUSE) {
            shown = qualityAuto || decoderAuto;
        }
        if (button->item.enabled != shown) {
            button->item.enabled = shown;
            button->item.selectable = shown;
            changed = true;
        }
    }
    if (changed) {
        fit();
        if (!isButtonSelectable(index)) {
            reset();
        }
        updateScroll();
        updateSelectionHighlight();
    }
}

bool MenuMainOptionsSubmenu::onOptionChange(MenuItem *item, int direction) {
    if (hasOption()) {
        return false;
    }
    auto *sub = main->getMenuMain()->getMenuMainOptionsSubmenu(item->name);
    if (sub == nullptr || !sub->hasOption() || !sub->step(direction)) {
        return false;
    }
    refreshValues();
    return true;
}

void MenuMainOptionsSubmenu::refresh() {
    if (menu_type == MenuType::Adjust && buttons.size() >= 3) {
        hideCurrent();
        buttons[0]->item.name = pplay::tr("Value") + ": " + formatValue(getCurrentValue());
        buttons[0]->refreshText();
        std::string stepStr = formatValue(step_value);
        if (stepStr[0] == '+' || stepStr[0] == '-') {
            stepStr = stepStr.substr(1);
        }
        buttons[1]->item.name = "-" + stepStr;
        buttons[1]->refreshText();
        buttons[2]->item.name = "+" + stepStr;
        buttons[2]->refreshText();
        return;
    }

    // shown on the current value only (none in groups / categories)
    hideCurrent();
    refreshValues();

    updateSelectionHighlight();
}

void MenuMainOptionsSubmenu::setSelection(const std::string &name) {
    for (auto btn: buttons) {
        if (Utility::toLower(btn->item.name) == Utility::toLower(name)
            || Utility::toLower(btn->item.data_str) == Utility::toLower(name)) {
            showCurrent(btn);
        }
    }
}

bool MenuMainOptionsSubmenu::editTextValue() {
    PPLAYConfig *config = main->getConfig();
    auto *option = config->getOption(option_name);
    // a network url: shown without its user and password (their own options)
    int url = PPLAYConfig::networkOf(option_name, "");
    const std::string oldValue = url > 0 ? PPLAYConfig::withoutCredentials(option->getString()) : option->getString();
    std::string newValue = oldValue;

    if (!pplay::Keyboard::edit(title->getString(), newValue) || newValue == oldValue) {
        return false;
    }

    option->setType(c2d::config::Option::Type::String);
    if (url > 0) {
        config->setNetworkUrl(url, newValue);
    } else {
        option->setString(newValue);
        int network = std::max(PPLAYConfig::networkOf(option_name, "_USER"),
                               PPLAYConfig::networkOf(option_name, "_PASSWORD"));
        if (network > 0) {
            config->updateNetworkCredentials(network);
        }
    }
    config->save();
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
                        "Options: " + option_name + " changed from " + oldValue + " to " + newValue);
    main->applyConfig();
    refresh();
    return true;
}

void MenuMainOptionsSubmenu::onOptionSelection(MenuItem *item) {
    if (menu_type == MenuType::TextInput) {
        editTextValue();
        return;
    }

    if (option_name.empty()) {
        // Network settings -> Users.
        if (item->name.rfind("Network ", 0) == 0 && Utility::endsWith(item->name, " users") &&
            key != item->name) {
            const int network = std::atoi(key.c_str() + strlen("Network "));
            setVisibility(Visibility::Hidden, true);
            main->getMenuMain()->showNetworkUsers(network);
            return;
        }

        // Dynamic list of accounts, with a separated final Add user row.
        if (Utility::endsWith(key, " users")) {
            const int network = std::atoi(key.c_str() + strlen("Network "));
            if (item->name == "Add user") {
                std::string username, password;
                if (!pplay::Keyboard::edit(pplay::tr("New username"), username) || username.empty()) return;
                if (!pplay::Keyboard::edit(pplay::tr("New password"), password) || password.empty()) return;
                const std::string context = main->getConfig()->getOption(PPLAYConfig::networkOption(network))->getString();
                const int timeout = main->getConfig()->getOption(PPLAYConfig::networkTimeoutOption(network))->getInteger();
                std::string error;
                if (!pplay::P2Play::createUser(context, username, password, 10, timeout, &error)) {
                    main->getStatus()->show("Error", error);
                    return;
                }
                main->getStatus()->show("Info", pplay::tr("User created"));
                setVisibility(Visibility::Hidden, true);
                main->getMenuMain()->showNetworkUsers(network);
                return;
            }
            if (!item->data_str.empty()) {
                const std::string userId = item->data_str;
                setVisibility(Visibility::Hidden, true);
                main->getMenuMain()->showNetworkUserDetails(network, userId);
            }
            return;
        }

        // Per-user submenu and its Role child submenu.
        const size_t userMarker = key.find(" user ");
        if (userMarker != std::string::npos) {
            const int network = std::atoi(key.c_str() + strlen("Network "));
            std::string suffix = key.substr(userMarker + strlen(" user "));
            const bool roleMenu = Utility::endsWith(suffix, " role");
            const std::string userId = roleMenu ? suffix.substr(0, suffix.size() - strlen(" role")) : suffix;
            const std::string context = main->getConfig()->getOption(PPLAYConfig::networkOption(network))->getString();
            const int timeout = main->getConfig()->getOption(PPLAYConfig::networkTimeoutOption(network))->getInteger();
            std::string error;

            if (roleMenu) {
                if (!pplay::P2Play::setUserRank(context, userId, item->id, timeout, &error)) {
                    main->getStatus()->show("Error", error);
                    return;
                }
                main->getStatus()->show("Info", pplay::tr("Role updated"));
                setVisibility(Visibility::Hidden, true);
                main->getMenuMain()->showNetworkUserDetails(network, userId);
                return;
            }

            if (item->name == "Change password") {
                std::string password;
                if (!pplay::Keyboard::edit(pplay::tr("New password"), password) || password.empty()) return;
                if (!pplay::P2Play::updateUserPassword(context, userId, password, timeout, &error)) {
                    main->getStatus()->show("Error", error);
                    return;
                }
                main->getStatus()->show("Info", pplay::tr("Password updated"));
                setVisibility(Visibility::Hidden, true);
                main->getMenuMain()->showNetworkUserDetails(network, userId);
                return;
            }
            if (item->name == "Role") {
                auto *role = main->getMenuMain()->getMenuMainOptionsSubmenu(key + " role");
                if (role != nullptr) {
                    setVisibility(Visibility::Hidden, true);
                    role->setVisibility(Visibility::Visible, true);
                }
                return;
            }
            if (item->name == "Delete user") {
                if (main->showMessage(pplay::tr("Delete user"), title->getString(),
                                      pplay::tr("Delete"), pplay::tr("Cancel")) != c2d::MessageBox::LEFT) return;
                if (!pplay::P2Play::deleteUser(context, userId, timeout, &error)) {
                    main->getStatus()->show("Error", error);
                    return;
                }
                main->getStatus()->show("Info", pplay::tr("User deleted"));
                setVisibility(Visibility::Hidden, true);
                main->getMenuMain()->showNetworkUsers(network);
                return;
            }
            return;
        }

        if (item->name == "Add network") {
            int network = main->getConfig()->addNetwork();
            main->getConfig()->save();
            main->getMenuMain()->updateNetworks();
            setVisibility(Visibility::Hidden, true);
            main->getMenuMain()->showNetwork(network);
            return;
        }
        if (key.rfind("Network ", 0) == 0 && (Utility::endsWith(item->name, " up")
                                               || Utility::endsWith(item->name, " down"))) {
            int network = std::atoi(key.c_str() + strlen("Network "));
            int direction = Utility::endsWith(item->name, " up") ? -1 : 1;
            int other = network + direction;
            if (!main->getConfig()->moveNetwork(network, direction)) {
                return;
            }
            main->renumberNetworks([network, other](int i) { return i == network ? other : i == other ? network : i; });
            main->getConfig()->save();
            main->getMenuMain()->updateNetworks(true);
            // the same network, at its new place
            setVisibility(Visibility::Hidden, true);
            // (at an end: the other way)
            bool end = other == 1 || other == main->getConfig()->getNetworkCount();
            std::string moved = "Network " + std::to_string(other) + ((direction < 0) != end ? " up" : " down");
            main->getMenuMain()->showNetwork(other, moved);
            return;
        }
        if (key.rfind("Network ", 0) == 0 && Utility::endsWith(item->name, " delete")) {
            int network = std::atoi(key.c_str() + strlen("Network "));
            if (main->showMessage(pplay::tr("Delete network"), main->getConfig()->networkName(network),
                                  pplay::tr("Delete"), pplay::tr("Cancel")) != c2d::MessageBox::LEFT) {
                return;
            }
            main->getConfig()->removeNetwork(network);
            main->renumberNetworks([network](int i) { return i == network ? 0 : i > network ? i - 1 : i; });
            main->getConfig()->save();
            main->getMenuMain()->updateNetworks(true);
            setVisibility(Visibility::Hidden, true);
            main->getMenuMain()->showParent(key);
            main->getMenuMain()->getMenuMainOptionsSubmenu("Networks")->select("Add network");
            return;
        }
        if (item->name == "Clear cache") {
            int count = pplay::Utility::clearCache();
            main->getFiler()->clearMediaInfo();
            main->getStatus()->show("Info", pplay::tr("Cache cleared") + ": " + std::to_string(count));
            return;
        }
        if (item->name == "Clear log") {
            pplay::Utility::clearLog();
            main->getStatus()->show("Info", pplay::tr("Log cleared"));
            return;
        }
        if (item->name == "View log") {
            main->getLogView()->show();
            return;
        }
        if (item->name == "Edit files") {
            setVisibility(Visibility::Hidden, true);
            main->getMenuMain()->getFileEditor()->setVisibility(Visibility::Visible);
            return;
        }
        auto *submenu = main->getMenuMain()->getMenuMainOptionsSubmenu(item->name);
        // a slider: changed with left / right only
        if (submenu != nullptr && submenu->menu_type != MenuType::Adjust) {
            setVisibility(Visibility::Hidden, true);
            submenu->setVisibility(Visibility::Visible, true);
        }
        return;
    }

    if (menu_type == MenuType::Adjust) {
        if (item->id != 0) {
            const std::string oldValue = formatValue(getCurrentValue());
            setCurrentValue(getCurrentValue() + ((float) item->id * step_value));
            main->getConfig()->save();
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "Options: " + option_name + " changed from " + oldValue
                                                                        + " to " + formatValue(getCurrentValue()));
            main->applyConfig();
            refresh();
        }
        return;
    }

    if (value_type == ValueType::Integer) {
        auto *option = main->getConfig()->getOption(option_name);
        const std::string oldValue = option->getString();
        option->setType(c2d::config::Option::Type::Integer);
        option->setInteger(item->id);
        main->getConfig()->save();
        if (option_name == OPT_LOG_LEVEL) {
            if (item->id != (int) pplay::Utility::LogLevel::Off) {
                pplay::Utility::setLogLevel((pplay::Utility::LogLevel) item->id);
            }
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                "Options: " + option_name + " changed from " + oldValue + " to " + option->getString());
            if (item->id == (int) pplay::Utility::LogLevel::Off) {
                pplay::Utility::setLogLevel(pplay::Utility::LogLevel::Off);
            }
        } else {
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                "Options: " + option_name + " changed from " + oldValue + " to " + option->getString());
        }
        main->applyConfig();
        refresh();
        return;
    }

#ifdef __SWITCH__
    if (option_name == OPT_CPU_BOOST) {
        if (item->name == "Enabled") {
            pplay::Utility::setCpuClock(pplay::Utility::CpuClock::Max);
        } else {
            pplay::Utility::setCpuClock(pplay::Utility::CpuClock::Min);
        }
    }
#endif
    if (isSearch()) {
        if (item->name == "Search again") {
            showSearch(true);
            return;
        }
        if (item->data_str.empty()) {
            return;
        }
        // its url, its name (an unnamed network)
        int network = std::atoi(key.c_str() + strlen("Network "));
        auto *url = main->getConfig()->getOption(option_name);
        auto *name = main->getConfig()->getOption(PPLAYConfig::networkNameOption(network));
        url->setType(c2d::config::Option::Type::String);
        main->getConfig()->setNetworkUrl(network, item->data_str);
        if (name->getString().empty() || name->getString() == "Network " + std::to_string(network)) {
            name->setType(c2d::config::Option::Type::String);
            name->setString(item->name);
        }
        main->getConfig()->save();
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Options: " + option_name + " set to " + item->data_str);
        main->applyConfig();
        setVisibility(Visibility::Hidden, true);
        main->getMenuMain()->showParent(key);
        return;
    }

    auto *option = main->getConfig()->getOption(option_name);
    const std::string oldValue = option->getString();
    // the item value, if any (accent color, track languages), or its name
    std::string newValue = item->data_str.empty() ? item->name : item->data_str;
    option->setType(c2d::config::Option::Type::String);
    if (option_name == OPT_ACCENT_COLOR) {
        pplay::Utility::setAccentColor(item->data_str);
    }
    option->setString(newValue);
    int network = PPLAYConfig::networkOf(option_name, "_TYPE");
    if (network > 0) {
        // DLNA: no login in its urls
        main->getConfig()->updateNetworkCredentials(network);
    }
    main->getConfig()->save();
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
                        "Options: " + option_name + " changed from " + oldValue + " to " + newValue);
    main->applyConfig();
    setSelection(item->name);
}

bool MenuMainOptionsSubmenu::isSearch() const {
    return key.size() > 7 && key.compare(key.size() - 7, 7, " search") == 0;
}

pplay::Io::NetworkType MenuMainOptionsSubmenu::searchType() const {
    int network = std::atoi(key.c_str() + strlen("Network "));
    return pplay::Io::parseNetworkType(main->getConfig()->getOption(PPLAYConfig::networkTypeOption(network))->getString());
}

void MenuMainOptionsSubmenu::showSearch(bool start) {
    std::vector<MenuItem> items;
    std::vector<pplay::Discovery::Server> servers;
    if (start) {
        pplay::Discovery::start(searchType());
    }
    searching = !pplay::Discovery::result(searchType(), &servers);
    if (searching) {
        items.emplace_back("Searching...", "", MenuItem::Position::Top, 0, false);
    } else {
        for (auto &server: servers) {
            MenuItem item(server.name, "", MenuItem::Position::Top, 0, true, server.url);
            item.label = server.label;
            items.push_back(item);
        }
        if (servers.empty()) {
            items.emplace_back("No servers found", "", MenuItem::Position::Top, 0, false);
        }
        items.emplace_back("Search again", "", MenuItem::Position::Top);
    }
    setItems(items);
    refresh();
}

void MenuMainOptionsSubmenu::onUpdate() {
    if (searching && isVisible()) {
        std::vector<pplay::Discovery::Server> servers;
        if (pplay::Discovery::result(searchType(), &servers)) {
            showSearch(false);
        }
    }
    Menu::onUpdate();
}

void MenuMainOptionsSubmenu::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    if (visibility == Visibility::Visible && isSearch() && !isVisible()) {
        showSearch(true);
    }
    // the items shown (and the height made to them) before it slides in
    if (visibility == Visibility::Visible) {
        refresh();
    }
    Menu::setVisibility(visibility, tweenPlay);
    if (visibility == Visibility::Visible) {
        refresh();
    }
}

bool MenuMainOptionsSubmenu::onInput(c2d::Input::Player *players) {
    unsigned int keys = players[0].buttons;
    // a group: left / right change the values of its options (back: B)
    if ((keys & (Input::Left | Input::Right)) && !hasOption() && !buttons.empty()) {
        MenuItem *item = getSelection();
        onOptionChange(item, (keys & Input::Right) ? 1 : -1);
        return true;
    }
    if (keys & Input::B) {
        setVisibility(Visibility::Hidden, true);
        main->getMenuMain()->showParent(key);
        return true;
    }

    bool result = Menu::onInput(players);
    updateSelectionHighlight();
    return result;
}
