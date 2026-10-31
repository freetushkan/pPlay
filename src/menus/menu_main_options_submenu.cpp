//
// Created by cpasjuste on 07/12/18.
//

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

#include "main.h"
#include "utility.h"
#include "menu_main_options_submenu.h"
#include "menu_file_editor.h"

#include "keyboard.h"
#include "i18n.h"

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

    if (option_name == OPT_SEEK_SHORT_SEC || option_name == OPT_SEEK_LONG_SEC || option_name == OPT_NETWORK_TIMEOUT) {
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
        oss << " " << value_unit;
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
        setSelection(main->getConfig()->getOption(option_name)->getString());
    }
}

std::string MenuMainOptionsSubmenu::currentText() {
    auto *option = main->getConfig()->getOption(option_name);
    if (menu_type == MenuType::TextInput) {
        std::string value = option->getString();
        return value.empty() ? pplay::tr("<empty>") : value;
    }
    if (menu_type == MenuType::Adjust) {
        return formatValue(getCurrentValue());
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
    for (auto *button: buttons) {
        auto *sub = main->getMenuMain()->getMenuMainOptionsSubmenu(button->item.name);
        bool option = sub != nullptr && sub->hasOption();
        button->item.value = option ? sub->currentText() : "";
        button->item.opens = sub != nullptr && !option;
        button->refreshValue();
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
    auto *option = main->getConfig()->getOption(option_name);
    const std::string oldValue = option->getString();
    std::string newValue = oldValue;

    if (!pplay::Keyboard::edit(title->getString(), newValue) || newValue == oldValue) {
        return false;
    }

    option->setType(c2d::config::Option::Type::String);
    option->setString(newValue);
    main->getConfig()->save();
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
        if (item->name == "Clear cache") {
            int count = pplay::Utility::clearCache();
            main->getFiler()->clearMediaInfo();
            main->getStatus()->show(pplay::tr("Info..."), pplay::tr("Cache cleared") + ": " + std::to_string(count));
            return;
        }
        if (item->name == "Edit files") {
            setVisibility(Visibility::Hidden, true);
            main->getMenuMain()->getFileEditor()->setVisibility(Visibility::Visible);
            return;
        }
        auto *submenu = main->getMenuMain()->getMenuMainOptionsSubmenu(item->name);
        if (submenu != nullptr) {
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
    auto *option = main->getConfig()->getOption(option_name);
    const std::string oldValue = option->getString();
    // the item value, if any (accent color, track languages), or its name
    std::string newValue = item->data_str.empty() ? item->name : item->data_str;
    option->setType(c2d::config::Option::Type::String);
    if (option_name == OPT_ACCENT_COLOR) {
        pplay::Utility::setAccentColor(item->data_str);
    }
    option->setString(newValue);
    main->getConfig()->save();
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
                        "Options: " + option_name + " changed from " + oldValue + " to " + newValue);
    main->applyConfig();
    setSelection(item->name);
}

void MenuMainOptionsSubmenu::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
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
        if (onOptionChange(item, (keys & Input::Right) ? 1 : -1)) {
            return true;
        }
        if (keys & Input::Left) {
            onOptionSelection(item);
            return true;
        }
    }
    if (keys & Input::Right || keys & Input::B) {
        setVisibility(Visibility::Hidden, true);
        main->getMenuMain()->showParent(key);
        return true;
    }

    bool result = Menu::onInput(players);
    updateSelectionHighlight();
    return result;
}
