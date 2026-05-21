//
// Created by cpasjuste on 07/12/18.
//

#ifndef PPLAY_MENU_MAIN_OPTIONS_SUBMENU_H
#define PPLAY_MENU_MAIN_OPTIONS_SUBMENU_H

#include "menu.h"

class MenuMainOptionsSubmenu : public Menu {

public:

    enum class ValueType {
        String,
        Integer,
        Float
    };

    enum class MenuType {
        List,
        Adjust
    };

    MenuMainOptionsSubmenu(Main *main, const c2d::FloatRect &rect,
                           const std::string &_title, const std::vector<MenuItem> &items,
                           const std::string &optionName, ValueType valueType = ValueType::String,
                           MenuType menuType = MenuType::List, float minValue = 0.0f,
                           float maxValue = 0.0f, float stepValue = 1.0f,
                           const std::string &unit = "");

    bool onInput(c2d::Input::Player *players) override;

    void setVisibility(c2d::Visibility visibility, bool tweenPlay = true) override;

    void onUpdate() override;

    void setSelection(const std::string &name);

    void refresh();

private:

    void onOptionSelection(MenuItem *item) override;

    void updateSelectionHighlight() override;

    std::string formatValue(float value) const;

    float getCurrentValue() const;

    void setCurrentValue(float value);

    Highlight *highlight_selection;
    std::string option_name;
    ValueType value_type;
    MenuType menu_type;
    float min_value;
    float max_value;
    float step_value;
    std::string value_unit;
};

#endif //PPLAY_MENU_MAIN_OPTIONS_SUBMENU_H
