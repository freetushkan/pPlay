//
// Created by cpasjuste on 07/12/18.
//

#include "main.h"
#include "menu_main_options.h"
#include "menu_file_editor.h"
#include "menu_video.h"
#include "input_map.h"

using namespace c2d;

MenuMainOptions::MenuMainOptions(Main *main, const c2d::FloatRect &rect, const std::vector<MenuItem> &items)
    : Menu(main, rect, "Settings", items, true) {}

void MenuMainOptions::onOptionSelection(MenuItem *item) {
    if (item->name == "Edit files") {
        setVisibility(Visibility::Hidden);
        main->getMenuMain()->getFileEditor()->setVisibility(Visibility::Visible);
        return;
    }
    auto *submenu = main->getMenuMain()->getMenuMainOptionsSubmenu(item->name);
    if (submenu != nullptr) {
        setVisibility(Visibility::Hidden);
        submenu->setVisibility(Visibility::Visible);
    }
}

void MenuMainOptions::onPointerBack() {
    setVisibility(Visibility::Hidden, true);
    if (fromPlayer) main->getMenuVideo()->setVisibility(Visibility::Visible, true);
    else main->getMenuMain()->setVisibility(Visibility::Visible, true);
}

bool MenuMainOptions::onOptionSlider(MenuItem *item, float part) {
    auto *submenu = main->getMenuMain()->getMenuMainOptionsSubmenu(item->name);
    if (submenu == nullptr || !submenu->setSliderPart(part)) {
        return false;
    }
    // The category rows show the value of their child setting.
    for (auto *button: buttons) {
        if (button->item.name == item->name) {
            button->item.value = submenu->currentText();
            button->item.slider = submenu->sliderPart();
            button->refreshValue();
            break;
        }
    }
    return true;
}

void MenuMainOptions::onOptionSliderEnd(MenuItem *item) {
    auto *submenu = main->getMenuMain()->getMenuMainOptionsSubmenu(item->name);
    if (submenu != nullptr) {
        submenu->finishSliderChange();
    }
}

void MenuMainOptions::onOptionSliderReset(MenuItem *item) {
    auto *submenu = main->getMenuMain()->getMenuMainOptionsSubmenu(item->name);
    if (submenu == nullptr || !submenu->resetSliderToDefault()) {
        return;
    }
    for (auto *button: buttons) {
        if (button->item.name == item->name) {
            button->item.value = submenu->currentText();
            button->item.slider = submenu->sliderPart();
            button->refreshValue();
            break;
        }
    }
}

bool MenuMainOptions::onInput(c2d::Input::Player *players) {
    if (players[0].buttons & Input::B) {
        setVisibility(Visibility::Hidden, true);
        if (fromPlayer) {
            main->getMenuVideo()->setVisibility(Visibility::Visible, true);
        } else {
            main->getMenuMain()->setVisibility(Visibility::Visible, true);
        }
        return true;
    }

    return Menu::onInput(players);
}
