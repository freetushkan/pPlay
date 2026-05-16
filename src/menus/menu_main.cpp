//
// Created by cpasjuste on 07/12/18.
//

#include "main.h"
#include "menu_main.h"

using namespace c2d;

MenuMain::MenuMain(Main *main, const c2d::FloatRect &rect, const std::vector<MenuItem> &items)
        : Menu(main, rect, "pPlay v" APP_VERSION, items, true) {
    std::vector<MenuItem> it;

#ifdef __SWITCH__
    it.emplace_back(OPT_CPU_BOOST, "cpu.png", MenuItem::Position::Top);
    it.emplace_back(OPT_UMS_DEVICE, "usb.png", MenuItem::Position::Top);
#endif
    menuMainOptions = new MenuMainOptions(main, rect, it);
    menuMainOptions->setLayer(2);
    menuMainOptions->setVisibility(Visibility::Hidden, false);
    main->add(menuMainOptions);

#ifdef __SWITCH__
    // Cpu Speed
    it.clear();
    it.emplace_back("Disabled", "", MenuItem::Position::Top);
    it.emplace_back("Enabled", "", MenuItem::Position::Top);
    menuMainOptionsCpu = new MenuMainOptionsSubmenu(main, rect, OPT_CPU_BOOST, it, OPT_CPU_BOOST);
    menuMainOptionsCpu->setLayer(2);
    menuMainOptionsCpu->setVisibility(Visibility::Hidden, false);
    menuMainOptionsCpu->setSelection(main->getConfig()->getOption(OPT_CPU_BOOST)->getString());
    main->add(menuMainOptionsCpu);

    std::string umsPath;
    it.clear();
    for (int i = 0; i <= 9; i++) {
        umsPath = "ums" + std::to_string(i) + ":/";
        it.emplace_back(umsPath, "", MenuItem::Position::Top);
    }
    menuMainOptionsUsb = new MenuMainOptionsSubmenu(main, rect, OPT_UMS_DEVICE, it, OPT_UMS_DEVICE);
    menuMainOptionsUsb->setLayer(2);
    menuMainOptionsUsb->setVisibility(Visibility::Hidden, false);
    menuMainOptionsUsb->setSelection(main->getConfig()->getOption(OPT_UMS_DEVICE)->getString());
    main->add(menuMainOptionsUsb);
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

void MenuMain::setSelection(int moduleId) {
    for (auto &button: buttons) {
        if (button->item.id == moduleId) {
            if (isVisible()) {
                highlight_selection->tweenTo(button->getPosition());
            } else {
                highlight_selection->setPosition(button->getPosition());
            }
            break;
        }
    }
}

void MenuMain::onOptionSelection(MenuItem *item) {
    if (item->name == "Local") {
        setVisibility(Visibility::Hidden, true);
        main->setCurrentModuleIndex(item->id);
        main->show(Main::MenuType::Local);
    } else if (item->name == "Options") {
        setVisibility(Visibility::Hidden, true);
        menuMainOptions->setVisibility(Visibility::Visible);
#ifdef __SWITCH__
    } else if (item->name == "Usb") {
        setVisibility(Visibility::Hidden, true);
        main->setCurrentModuleIndex(item->id);
        main->show(Main::MenuType::Usb);
#endif
    } else if (item->id > 0) {
        setVisibility(Visibility::Hidden, true);
        main->setCurrentModuleIndex(item->id);
        main->show(Main::MenuType::Network);
    } else if (item->name == "Exit") {
        main->quit();
    }
}

void MenuMain::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    setSelection(main->getcurrentModuleIndex());
    Menu::setVisibility(visibility, tweenPlay);
}

bool MenuMain::onInput(c2d::Input::Player *players) {
    if (players[0].buttons & Input::Right || players[0].buttons & Input::B) {
        setVisibility(Visibility::Hidden, true);
        return true;
    }

    if (players[0].buttons & Input::Left) {
        MenuItem *item = getSelection();
        if (item->name == "Options") {
            onOptionSelection(item);
        }
        return true;
    }

    return Menu::onInput(players);
}

bool MenuMain::isMenuVisible() {
    return isVisible()
           || menuMainOptions->isVisible()
#ifdef __SWITCH__
        || menuMainOptionsCpu->isVisible()
        || menuMainOptionsUsb->isVisible()
#endif
            ;
}

MenuMainOptions *MenuMain::getMenuMainOptions() {
    return menuMainOptions;
}

MenuMainOptionsSubmenu *MenuMain::getMenuMainOptionsSubmenu(const std::string &name) {
#ifdef __SWITCH__
    if (name == OPT_CPU_BOOST) {
        return menuMainOptionsCpu;
    }
    if (name == OPT_UMS_DEVICE) {
        return menuMainOptionsUsb;
    }
#endif
    return nullptr;
}
