//
// Created by cpasjuste on 07/12/18.
//

#ifndef PPLAY_MENU_MAIN_OPTIONS_SUBMENU_H
#define PPLAY_MENU_MAIN_OPTIONS_SUBMENU_H

#include "menu.h"
#include "io.h"

class MenuMainOptionsSubmenu : public Menu {

public:

    enum class ValueType {
        String,
        Integer,
        Float
    };

    enum class MenuType {
        List,
        Adjust,
        TextInput
    };

    MenuMainOptionsSubmenu(Main *main, const c2d::FloatRect &rect, const std::string &_title,
                           const std::vector<MenuItem> &items, const std::string &optionName,
                           ValueType valueType = ValueType::String, MenuType menuType = MenuType::List,
                           float minValue = 0.0f, float maxValue = 0.0f, float stepValue = 1.0f,
                           const std::string &unit = "");

    bool onInput(c2d::Input::Player *players) override;

    void onUpdate() override;

    void setVisibility(c2d::Visibility visibility, bool tweenPlay = true) override;

    void setSelection(const std::string &name);

    void refresh();

    // an option menu: its value as shown, one step to the next / previous one
    bool hasOption() const { return !option_name.empty(); }

    const std::string &getOptionName() const { return option_name; }

    std::string currentText();

    bool step(int direction);

    void setZeroLabel(const std::string &label) { zeroLabel = label; }

    float sliderPart() const;

    bool setSliderPart(float part);
    void finishSliderChange();

    // Restore the option's constructor default (the minus/reset gesture).
    bool resetSliderToDefault();

    // name in MenuMain (back navigation)
    void setKey(const std::string &k) { key = k; }

private:

    void onOptionSelection(MenuItem *item) override;

    bool onOptionChange(MenuItem *item, int direction) override;

    void onPointerBack() override;

    bool onOptionSlider(MenuItem *item, float part) override;

    void onOptionSliderReset(MenuItem *item) override;
    void onOptionSliderEnd(MenuItem *item) override;

    // a group: the values of its options
    void refreshValues();

    void updateSelectionHighlight() override;

    std::string formatValue(float value) const;

    float getCurrentValue() const;

    void setCurrentValue(float value);

    bool editTextValue();

    // a network "search": the servers found (a search started when shown)
    bool isSearch() const;

    pplay::Io::NetworkType searchType() const;

    void showSearch(bool start);

    std::string option_name;
    ValueType value_type;
    MenuType menu_type;
    float min_value;
    float max_value;
    float step_value;
    std::string value_unit;
    std::string key;
    std::string zeroLabel;
    bool searching = false;
};

#endif //PPLAY_MENU_MAIN_OPTIONS_SUBMENU_H
