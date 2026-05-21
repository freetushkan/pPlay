//
// Created by cpasjuste on 20/10/18.
//

#include <algorithm>

#include "cross2d/c2d.h"
#include "main.h"
#include "menu.h"
#include "utility.h"

using namespace c2d;

MenuButton::MenuButton(Main *main, const MenuItem &item, const FloatRect &rect) : Rectangle(rect) {

    this->item = item;
    MenuButton::setOrigin(Origin::Left);

    if (!item.icon.empty()) {
        icon = new C2DTexture(main->getIo()->getRomFsPath() + "skin/" + item.icon);
        icon->setOrigin(Origin::Left);
        icon->setPosition(16 * main->getScaling().x, MenuButton::getSize().y / 2);
        icon->setScale(main->getScaled(main->getSize().x / 1920, main->getSize().y / 1080));
        icon->setFillColor(COLOR_FONT);
        MenuButton::add(icon);
    }

    name = new Text(item.name, main->getFontSize(Main::FontSize::Medium), main->getFont());
    name->setOrigin(Origin::Left);
    if (pplay::Utility::isValidHexColor(item.data_str)) {
        name->setFillColor(pplay::Utility::hexToColor(item.data_str));
    } else {
        name->setFillColor(COLOR_FONT);
    }
    if (!item.icon.empty()) {
        name->setPosition((ICON_SIZE + 32) * main->getScaling().x, MenuButton::getSize().y / 2);
        name->setSizeMax((MenuButton::getSize().x - ICON_SIZE + 32) * main->getScaling().x, 0);
    } else {
        name->setPosition(16 * main->getScaling().x, MenuButton::getSize().y / 2);
        name->setSizeMax((MenuButton::getSize().x - (16 * main->getScaling().x)) * main->getScaling().x, 0);
    }
    MenuButton::add(name);
}

Menu::Menu(Main *m, const c2d::FloatRect &rect, const std::string &_title,
           const std::vector<MenuItem> &items, bool left) : RectangleShape(rect) {
    main = m;

    Menu::setFillColor(COLOR_BG);
    Menu::setAlpha(245);
    Menu::setOutlineColor(Color::GrayLight);
    Menu::setOutlineThickness(main->getScaling().y * 2);

    // highlight
    highlight = new Highlight({Menu::getSize().x, BUTTON_HEIGHT * main->getScaling().y});
    highlight->setOrigin(Origin::Left);
    highlight->setPosition(0, 200 * main->getScaling().y);
    Menu::add(highlight);

    // title
    title = new Text(_title, main->getFontSize(Main::FontSize::Big), main->getFont());
    title->setPosition(main->getScaled(32, 32));
    title->setFillColor(COLOR_FONT);
    Menu::add(title);

    // options
    FloatRect top = {0, 200 * main->getScaling().y,
                     Menu::getSize().x, BUTTON_HEIGHT * main->getScaling().y};
    FloatRect bottom = {0, Menu::getSize().y - (32 * main->getScaling().y),
                        Menu::getSize().x, BUTTON_HEIGHT * main->getScaling().y};

    for (auto &item: items) {
        if (item.position == MenuItem::Position::Top) {
            auto *option = new MenuButton(main, item, top);
            add(option);
            buttons.push_back(option);
            top.top += 64 * main->getScaling().y;
        } else {
            auto *option = new MenuButton(main, item, bottom);
            Menu::add(option);
            buttons.push_back(option);
            bottom.top -= 64 * main->getScaling().y;
        }
    }

    index = findFirstSelectableIndex();
    updateScroll();
    highlight->setPosition(0, buttons.empty() ? 0 : buttons[index]->getPosition().y);

    // tween!
    if (left) {
        Menu::add(new TweenPosition({-rect.width, 0}, {0, 0}, 0.2f));
    } else {
        Menu::add(new TweenPosition({main->getSize().x, 0},
                                    {main->getSize().x - Menu::getSize().x, 0}, 0.2f));
    }
}

bool Menu::onInput(c2d::Input::Player *players) {
    unsigned int keys = players[0].buttons;

    if (buttons.empty()) {
        return true;
    }

    if (keys & Input::Touch) {
        Vector2f touch = players[0].touch;
        if (!getGlobalBounds().contains(touch)) {
            setVisibility(Visibility::Hidden, true);
        } else {
            for (unsigned int i = 0; i < buttons.size(); i++) {
                if (!isButtonSelectable((int) i) || !buttons.at(i)->isVisible()) {
                    continue;
                }
                if (buttons.at(i)->getGlobalBounds().contains(touch)) {
                    index = (int) i;
                    onOptionSelection(&buttons.at(i)->item);
                    break;
                }
            }
        }
    } else {
        if (keys & Input::Up) {
            moveSelection(-1);
        } else if (keys & Input::Down) {
            moveSelection(1);
        } else if (keys & Input::A && isButtonSelectable(index)) {
            onOptionSelection(&buttons[index]->item);
        }
    }

    ensureSelectionVisible();
    highlight->tweenTo({0, buttons[index]->getPosition().y});

    return true;
}

void Menu::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    C2DObject::setVisibility(visibility, true);
    if (visibility == Visibility::Visible) {
        ensureSelectionVisible();
        updateScroll();
    }
}

MenuItem *Menu::getSelection() {
    return &buttons[index]->item;
}

bool Menu::isButtonSelectable(int buttonIndex) const {
    // return true;
    return buttonIndex >= 0 && buttonIndex < (int) buttons.size() && buttons[buttonIndex]->item.selectable;
}

int Menu::findFirstSelectableIndex() const {
    for (int i = 0; i < (int) buttons.size(); i++) {
        if (isButtonSelectable(i)) {
            return i;
        }
    }

    return 0;
}

void Menu::moveSelection(int direction) {
    if (buttons.empty()) {
        return;
    }

    int next = index;
    do {
        next += direction;
        if (next < 0) {
            next = (int) buttons.size() - 1;
        } else if (next >= (int) buttons.size()) {
            next = 0;
        }
    } while (!isButtonSelectable(next) && next != index);

    if (isButtonSelectable(next)) {
        index = next;
    }
}

void Menu::updateScroll() {
    const float top = 200 * main->getScaling().y;
    const float bottom = Menu::getSize().y - (32 * main->getScaling().y);
    const float buttonHeight = BUTTON_HEIGHT * main->getScaling().y;
    const float spacing = 64 * main->getScaling().y;

    int topIndex = 0;
    for (auto &button: buttons) {
        if (button->item.position == MenuItem::Position::Top) {
            const float y = top + ((float) topIndex * spacing) - scrollOffset;
            button->setPosition(0, y);
            button->setVisibility(y >= top && y + buttonHeight <= bottom
                                  ? Visibility::Visible : Visibility::Hidden);
            topIndex++;
        }
    }
}

void Menu::ensureSelectionVisible() {
    if (buttons.empty() || buttons[index]->item.position != MenuItem::Position::Top) {
        return;
    }

    const float top = 200 * main->getScaling().y;
    const float bottom = Menu::getSize().y - (32 * main->getScaling().y);
    const float buttonHeight = BUTTON_HEIGHT * main->getScaling().y;
    const float spacing = 64 * main->getScaling().y;

    int selectedTopIndex = 0;
    int topButtonCount = 0;
    for (int i = 0; i < (int) buttons.size(); i++) {
        if (buttons[i]->item.position != MenuItem::Position::Top) {
            continue;
        }
        if (i == index) {
            selectedTopIndex = topButtonCount;
        }
        topButtonCount++;
    }

    const float contentBottom = top + ((float) std::max(topButtonCount - 1, 0) * spacing) + buttonHeight;
    const float maxScrollOffset = std::max(0.0f, contentBottom - bottom);
    const float selectedTop = top + ((float) selectedTopIndex * spacing) - scrollOffset;

    if (selectedTop < top) {
        scrollOffset = (float) selectedTopIndex * spacing;
    } else if (selectedTop + buttonHeight > bottom) {
        scrollOffset = top + ((float) selectedTopIndex * spacing) + buttonHeight - bottom;
    }

    scrollOffset = std::max(0.0f, std::min(scrollOffset, maxScrollOffset));
    updateScroll();
}

void Menu::onUpdate() {
    highlight->setFillColor(COLOR_HIGHLIGHT);
    highlight->setCursorColor(COLOR_ACCENT);
    RectangleShape::onUpdate();
}

void Menu::reset() {
    index = findFirstSelectableIndex();
    scrollOffset = 0.0f;
    updateScroll();
    if (!buttons.empty()) {
        highlight->tweenTo({0, buttons[index]->getPosition().y});
    }
}
