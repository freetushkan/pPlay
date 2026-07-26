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

#ifdef __SWITCH__
#include <switch.h>
#endif

using namespace c2d;

MenuMainOptionsSubmenu::MenuMainOptionsSubmenu(
        Main *main, const c2d::FloatRect &rect, const std::string &_title,
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

    // highlight
    highlight_selection = new Highlight({MenuMainOptionsSubmenu::getSize().x, BUTTON_HEIGHT * main->getScaling().y},
                                        Highlight::CursorPosition::Left);
    highlight_selection->setFillColor(COLOR_ACCENT);
    highlight_selection->setAlpha(60);
    highlight_selection->setCursorColor(COLOR_ACCENT);
    highlight_selection->setOrigin(Origin::Left);
    highlight_selection->setPosition(0, 200 * main->getScaling().y);
    highlight_selection->setLayer(-1);
    MenuMainOptionsSubmenu::add(highlight_selection);

    reload_modules = option_name.rfind("NETWORK", 0) == 0;

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

#ifdef __PS4__
    std::string formatUTCOffset(float hours) {
        bool isNegative = hours < 0.0f;
        float absHours = std::abs(hours);
        int h = static_cast<int>(absHours);
        int m = static_cast<int>(std::round((absHours - h) * 60.0f));
        if (m == 60) {
            h += 1;
            m = 0;
        }
        std::ostringstream oss;
        oss << (isNegative ? "-" : "+")
            << h << ":"
            << std::setfill('0') << std::setw(2) << m;
        return oss.str();
    }
#endif
}

std::string MenuMainOptionsSubmenu::formatValue(float value) const {
    std::ostringstream oss;
#ifdef __PS4__
    if (option_name == OPT_UTC_OFFSET) {
        return formatUTCOffset(value);
    }
#endif
    if (option_name == OPT_SEEK_SHORT_SEC
        || option_name == OPT_SEEK_LONG_SEC
        || option_name == OPT_NETWORK_TIMEOUT) {
        return formatTimeMMSS(value);
    }

    if (value_type == ValueType::Float) {
        int precision = (step_value - std::floor(step_value) > 0.0f) && 
                        (std::fmod(step_value * 10.0f, 1.0f) > 0.0f) ? 2 : 1;
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
        buttons[0]->name->setString(buttons[0]->item.name);
        highlight_selection->setVisibility(buttons[0]->isVisible() ? Visibility::Visible : Visibility::Hidden);
        highlight_selection->tweenTo(buttons[0]->getPosition());
        return;
    }

    if (option_name.empty()) {
        return;
    }

    if (value_type == ValueType::Integer) {
        const int value = (int) getCurrentValue();
        for (auto btn: buttons) {
            if (btn->item.id == value) {
                highlight_selection->setVisibility(btn->isVisible() ? Visibility::Visible : Visibility::Hidden);
                highlight_selection->tweenTo(btn->getPosition());
                break;
            }
        }
    } else if (value_type == ValueType::String) {
        setSelection(main->getConfig()->getOption(option_name)->getString());
    }
}

void MenuMainOptionsSubmenu::refresh() {
    if (menu_type == MenuType::Adjust && buttons.size() >= 3) {
        highlight_selection->setVisibility(Visibility::Hidden);
        buttons[0]->item.name = "Value: " + formatValue(getCurrentValue());
        buttons[0]->name->setString(buttons[0]->item.name);
        std::string stepStr = formatValue(step_value);
        if (stepStr[0] == '+' || stepStr[0] == '-') {
            stepStr = stepStr.substr(1);
        }
        buttons[1]->item.name = "-" + stepStr;
        buttons[1]->name->setString(buttons[1]->item.name);
        buttons[2]->item.name = "+" + stepStr;
        buttons[2]->name->setString(buttons[2]->item.name);
        return;
    }

    highlight_selection->setVisibility(Visibility::Visible);

    updateSelectionHighlight();
}

void MenuMainOptionsSubmenu::setSelection(const std::string &name) {
    for (auto btn: buttons) {
        if (Utility::toLower(btn->item.name) == Utility::toLower(name)
        || Utility::toLower(btn->item.data_str) == Utility::toLower(name)) {
            highlight_selection->setVisibility(btn->isVisible() ? Visibility::Visible : Visibility::Hidden);
            highlight_selection->tweenTo(btn->getPosition());
        }
    }
}

bool MenuMainOptionsSubmenu::editTextValue() {
    auto *option = main->getConfig()->getOption(option_name);
    const std::string oldValue = option->getString();
    std::string newValue = oldValue;

#ifdef __SWITCH__
    char out[512] = {0};
    SwkbdConfig kbd;
    swkbdCreate(&kbd, 0);
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, option_name.c_str());
    swkbdConfigSetInitialText(&kbd, oldValue.c_str());
    const Result rc = swkbdShow(&kbd, out, sizeof(out));
    swkbdClose(&kbd);
    if (R_FAILED(rc)) {
        return false;
    }
    newValue = out;
#elif __PS4__
    int res = Dialog::initImeDialog(option_name.c_str(), oldValue.c_str(), 512, (OrbisImeType)0, 0.0f, 0.0f);
    if (res < 0) return false;
    int status = 0;
    while (true) {
        status = Dialog::updateImeDialog();
        if (status == 2) { 
            newValue = reinterpret_cast<char*>(Dialog::getImeDialogInputText());
            break;
        }
        if (status == 3 || status == 0) return false;
        sceKernelUsleep(16000);
    }
#elif __PS5__
    int res = Dialog::initImeDialog(option_name.c_str(), oldValue.c_str(), 1024, (SceImeDialogType)0, 0.0f, 0.0f);
    if (res < 0) {
        return false;
    }
    int status = 0;
    while (true) {
        status = Dialog::updateImeDialog();
        if (status == 2) {
            newValue = reinterpret_cast<char*>(Dialog::getImeDialogInputText());
            break;
        }
        if (status == 3 || status == 0) {
            return false;
        }
        sceKernelUsleep(16000);
    }
#else
    return false;
#endif

    if (newValue == oldValue) {
        return false;
    }

    option->setType(c2d::config::Option::Type::String);
    option->setString(newValue);
    main->getConfig()->save();
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
                        "Options: " + option_name + " changed from " + oldValue + " to " + newValue);
    if (reload_modules) {
        main->reloadMainMenuModules();
    }
    refresh();
    return true;
}

void MenuMainOptionsSubmenu::onOptionSelection(MenuItem *item) {
    if (menu_type == MenuType::TextInput) {
        editTextValue();
        return;
    }

    if (option_name.empty()) {
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
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                "Options: " + option_name + " changed from " + oldValue
                                + " to " + formatValue(getCurrentValue()));
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
                                "Options: " + option_name + " changed from " + oldValue
                                + " to " + option->getString());
            if (item->id == (int) pplay::Utility::LogLevel::Off) {
                pplay::Utility::setLogLevel(pplay::Utility::LogLevel::Off);
            }
        } else {
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                "Options: " + option_name + " changed from " + oldValue
                                + " to " + option->getString());
        }
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
    std::string newValue = item->name;
    option->setType(c2d::config::Option::Type::String);
    if (option_name == OPT_ACCENT_COLOR) {
        pplay::Utility::setAccentColor(item->data_str);
        newValue = item->data_str;
    }
    option->setString(newValue);
    main->getConfig()->save();
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
        "Options: " + option_name + " changed from " + oldValue + " to " + newValue);
    setSelection(item->name);
}

void MenuMainOptionsSubmenu::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    Menu::setVisibility(visibility, tweenPlay);
    if (visibility == Visibility::Visible) {
        refresh();
    }
}

bool MenuMainOptionsSubmenu::onInput(c2d::Input::Player *players) {
    if (players[0].buttons & Input::Right || players[0].buttons & Input::B) {
        setVisibility(Visibility::Hidden, true);
        if (option_name.rfind("NETWORK", 0) == 0) {
            auto *submenu = main->getMenuMain()->getMenuMainOptionsSubmenu(
                    "Network " + option_name.substr(7, 1));
            if (submenu != nullptr) {
                submenu->setVisibility(Visibility::Visible, true);
                return true;
            }
        }
        main->getMenuMain()->getMenuMainOptions()->setVisibility(Visibility::Visible, true);
        return true;
    }

    bool result = Menu::onInput(players);
    updateSelectionHighlight();
    return result;
}

void MenuMainOptionsSubmenu::onUpdate() {
    highlight_selection->setFillColor(COLOR_ACCENT);
    highlight_selection->setAlpha(60);
    highlight_selection->setCursorColor(COLOR_ACCENT);
    Menu::onUpdate();
}