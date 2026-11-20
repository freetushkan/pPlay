//
// Created by cpasjuste on 07/12/18.
//

#ifndef PPLAY_MENU_MAIN_H
#define PPLAY_MENU_MAIN_H

#include <map>

#include "menu.h"
#include "menu_main_options.h"
#include "menu_main_options_submenu.h"

class MenuFileEditor;

class MenuMain : public Menu {

public:

    MenuMain(Main *main, const c2d::FloatRect &rect, const std::vector<MenuItem> &items);

    MenuMainOptionsSubmenu *getMenuMainOptionsSubmenu(const std::string &name);

    // back from a settings menu: its category (or group), else the settings
    void showParent(const std::string &key);

    void reloadModules();

    MenuFileEditor *getFileEditor();

    // the settings shown; from the player: on its side, back to its menu
    void openSettings(bool fromPlayer);

    bool isMenuVisible();

    void setSelection(int moduleId);

    void setVisibility(c2d::Visibility visibility, bool tweenPlay = true) override;

    // the networks changed: their menus, items, labels (force: their items made again, a move)
    void updateNetworks(bool force = false);

    // a network settings menu shown, that item selected
    void showNetwork(int index, const std::string &select = "");

private:

    void addSubmenu(const std::string &name, const std::string &title, const std::string &optionName,
                    const std::vector<MenuItem> &submenuItems,
                    MenuMainOptionsSubmenu::ValueType valueType = MenuMainOptionsSubmenu::ValueType::String,
                    MenuMainOptionsSubmenu::MenuType menuType = MenuMainOptionsSubmenu::MenuType::List,
                    float minValue = 0.0f, float maxValue = 0.0f, float stepValue = 1.0f, const std::string &unit = "");

    // the menus of a network (made once an index exists)
    void addNetworkMenus(int index);

    // Settings > Networks: the networks, "Add network", the network settings
    std::vector<MenuItem> networkCategoryItems();

    bool onInput(c2d::Input::Player *players) override;

    void onOptionSelection(MenuItem *item) override;

    void updateSelectionHighlight() override;

    // "Network x (name)" labels
    void updateNetworkLabels();

    MenuMainOptions *menuMainOptions;
    MenuFileEditor *fileEditor = nullptr;
    std::map<std::string, MenuMainOptionsSubmenu *> menuMainOptionsSubmenus;
    std::map<std::string, std::string> parents;
    c2d::FloatRect wide;
    std::vector<MenuItem> networkSettings;
    int networkMenus = 0;
    int networkItems = -1;
};

#endif //PPLAY_MENU_MAIN_H
