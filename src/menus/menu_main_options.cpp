//
// Created by cpasjuste on 07/12/18.
//

#include "main.h"
#include "menu_main_options.h"

using namespace c2d;

MenuMainOptions::MenuMainOptions(Main *main, const c2d::FloatRect &rect, const std::vector<MenuItem> &items)
        : Menu(main, rect, "OPTIONS", items, true) {
}

void MenuMainOptions::onOptionSelection(MenuItem *item) {
    auto *submenu = main->getMenuMain()->getMenuMainOptionsSubmenu(item->name);
    if (submenu != nullptr) {
        setVisibility(Visibility::Hidden);
        submenu->setVisibility(Visibility::Visible);
    }
}

bool MenuMainOptions::onInput(c2d::Input::Player *players) {
    if (players[0].buttons & Input::Right || players[0].buttons & Input::B) {
        setVisibility(Visibility::Hidden, true);
        main->getMenuMain()->setVisibility(Visibility::Visible, true);
        return true;
    }

    if (players[0].buttons & Input::Left) {
        onOptionSelection(getSelection());
        return true;
    }

    return Menu::onInput(players);
}
