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

    MenuMainOptions *getMenuMainOptions();

    MenuMainOptionsSubmenu *getMenuMainOptionsSubmenu(const std::string &name);

    // back from a settings menu: its category (or group), else the settings
    void showParent(const std::string &key);

    void reloadModules();

    MenuFileEditor *getFileEditor();

    bool isMenuVisible();

    void setSelection(int moduleId);

    void setVisibility(c2d::Visibility visibility, bool tweenPlay = true) override;

    void onUpdate() override;

    Highlight *highlight_selection;

private:

    bool onInput(c2d::Input::Player *players) override;

    void onOptionSelection(MenuItem *item) override;

    void updateSelectionHighlight() override;

    // "Network x (name)" labels
    void updateNetworkLabels();

    MenuMainOptions *menuMainOptions;
    MenuFileEditor *fileEditor = nullptr;
    std::map<std::string, MenuMainOptionsSubmenu *> menuMainOptionsSubmenus;
    std::map<std::string, std::string> parents;
};


#endif //PPLAY_MENU_MAIN_H
