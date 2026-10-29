//
// Created by cpasjuste on 20/10/18.
//

#include <algorithm>
#include <cmath>

#include "main.h"
#include "menu.h"
#include "text_wrap.h"

using namespace c2d;

static std::vector<Menu *> &menuRegistry() {
    // never destroyed: menus are deleted late, by the renderer destructor
    static auto *menus = new std::vector<Menu *>();
    return *menus;
}

Menu::Menu(Main *m, const c2d::FloatRect &rect, const std::string &_title, const std::vector<MenuItem> &items,
           bool left)
    : RectangleShape(rect), main(m) {
    Menu::setFillColor(COLOR_BG);
    Menu::setAlpha(245);
    Menu::setOutlineColor(Color::GrayLight);
    Menu::setOutlineThickness(main->getScaling().y * 2);

    highlight = new Highlight({Menu::getSize().x, itemHeight()});
    highlight->setOrigin(Origin::Left);
    highlight->setPosition(0, menuTop());
    Menu::add(highlight);

    current = new Highlight({Menu::getSize().x, itemHeight()}, Highlight::CursorPosition::Left);
    current->setFillColor(COLOR_ACCENT, 60);
    current->setCursorColor(COLOR_ACCENT);
    current->setOrigin(Origin::Left);
    current->setPosition(0, menuTop());
    current->setLayer(-1);
    current->setVisibility(Visibility::Hidden);
    Menu::add(current);

    // on several lines when needed
    titleKey = _title;
    title = new Text("", main->getFontSize(Main::FontSize::Big), main->getFont());
    title->setPosition(main->getScaled(32, 32));
    title->setFillColor(COLOR_FONT);
    setTitleText(pplay::tr(_title));
    Menu::add(title);
    menuRegistry().push_back(this);

    setItems(items);

    if (left) {
        Menu::add(new TweenPosition({-rect.width, 0}, {0, 0}, 0.2f));
    } else {
        Menu::add(new TweenPosition({main->getSize().x, 0}, {main->getSize().x - Menu::getSize().x, 0}, 0.2f));
    }
}

Menu::~Menu() {
    auto &menus = menuRegistry();
    menus.erase(std::remove(menus.begin(), menus.end(), this), menus.end());
}

const std::vector<Menu *> &Menu::all() {
    return menuRegistry();
}

float Menu::menuTop() const {
    return 200.0f * main->getScaling().y;
}

float Menu::menuBottom() {
    // above the bottom items (exit, stop...)
    int bottomItems = 0;
    for (auto *button: buttons) {
        bottomItems += button->item.position == MenuItem::Position::Bottom;
    }
    return getSize().y - 32.0f * main->getScaling().y - (float) bottomItems * itemHeight();
}

float Menu::itemHeight() const {
    if (compact) {
        return std::round((float) main->getFontSize(Main::FontSize::Small) * 1.7f);
    }
    return BUTTON_HEIGHT * main->getScaling().y;
}

int Menu::visibleSlots() {
    float usable = menuBottom() - menuTop() - itemHeight();
    return usable <= 0 ? 1 : (int) (usable / itemHeight()) + 1;
}

int Menu::topButtonCount() const {
    int count = 0;
    for (auto *button: buttons) {
        if (button->item.enabled && button->item.position == MenuItem::Position::Top) {
            count++;
        }
    }
    return count;
}

int Menu::topIndexOf(int buttonIndex) const {
    int topIndex = 0;
    for (int i = 0; i < (int) buttons.size(); i++) {
        if (!buttons[i]->item.enabled || buttons[i]->item.position != MenuItem::Position::Top) {
            continue;
        }
        if (i == buttonIndex) {
            return topIndex;
        }
        topIndex++;
    }
    return 0;
}

bool Menu::onInput(c2d::Input::Player *players) {
    unsigned int keys = players[0].buttons;

    if (buttons.empty()) {
        return true;
    }

    if (keys & Input::Touch) {
        moveDirection = 0;
        moveWrapped = false;
        skipEnsure = false;
        Vector2f touch = players[0].touch;
        if (!getGlobalBounds().contains(touch)) {
            setVisibility(Visibility::Hidden, true);
        } else {
            for (unsigned int i = 0; i < buttons.size(); i++) {
                if (isButtonSelectable((int) i) && buttons[i]->isVisible()
                    && buttons[i]->getGlobalBounds().contains(touch)) {
                    index = (int) i;
                    onOptionSelection(&buttons[i]->item);
                    break;
                }
            }
        }
    } else if (keys & Input::Up) {
        moveSelection(-1);
        updateSelectionState();
    } else if (keys & Input::Down) {
        moveSelection(1);
        updateSelectionState();
    } else if (keys & Input::A && isButtonSelectable(index)) {
        onOptionSelection(&buttons[index]->item);
    }

    if (!skipEnsure) {
        ensureSelectionVisible();
    }
    skipEnsure = false;
    highlight->tweenTo({0, buttons[index]->getPosition().y});

    return true;
}

void Menu::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    C2DObject::setVisibility(visibility, true);
    for (auto &button: buttons) {
        button->setVisibility(visibility, false);
    }
    if (visibility == Visibility::Visible) {
        if (!skipEnsure) {
            ensureSelectionVisible();
        }
        skipEnsure = false;
        updateScroll();
    }
}

MenuItem *Menu::getSelection() {
    return &buttons[index]->item;
}

bool Menu::isButtonSelectable(int buttonIndex) const {
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
    moveDirection = direction;
    moveWrapped = false;
    skipEnsure = false;

    int next = index;
    do {
        next += direction;
        if (next < 0 || next >= (int) buttons.size()) {
            // wrapped: the other end shown
            next = next < 0 ? (int) buttons.size() - 1 : 0;
            moveWrapped = true;
            skipEnsure = true;
            scrollOffset = next == 0 ? 0 : (float) std::max(0, topButtonCount() - visibleSlots()) * itemHeight();
            updateScroll();
        }
    } while (!isButtonSelectable(next) && next != index);

    if (isButtonSelectable(next)) {
        moveWrapped = (direction > 0 && next < index) || (direction < 0 && next > index);
        index = next;
    }
}

void Menu::updateScroll() {
    const float top = menuTop();
    const float bottom = menuBottom();
    const float height = itemHeight();
    const int startSlot = std::max(0, (int) (scrollOffset / height + 0.5f));

    int topIndex = 0;
    for (auto &button: buttons) {
        if (button->item.position != MenuItem::Position::Top) {
            continue;
        }
        if (!button->item.enabled) {
            button->setVisibility(Visibility::Hidden);
            continue;
        }
        const float y = top + (float) (topIndex - startSlot) * height;
        button->setPosition(0, y);
        button->setVisibility(y + height > top && y < bottom ? Visibility::Visible : Visibility::Hidden);
        topIndex++;
    }
}

void Menu::ensureSelectionVisible() {
    if (buttons.empty() || buttons[index]->item.position != MenuItem::Position::Top) {
        return;
    }

    const float height = itemHeight();
    const int total = topButtonCount();
    const int selected = topIndexOf(index);
    const int slots = visibleSlots();
    const int maxStart = std::max(0, total - slots);
    const int start = std::max(0, std::min((int) (scrollOffset / height + 0.5f), maxStart));

    int wanted = start;
    if (total <= slots) {
        wanted = 0;
    } else if (moveWrapped) {
        wanted = moveDirection > 0 ? 0 : maxStart;
    } else if (moveDirection > 0) {
        // one more item seen below the selection
        if (selected >= start + std::max(0, slots - 1)) {
            wanted = selected - std::max(0, slots - 2);
        }
        if (selected >= total - 1) {
            wanted = maxStart;
        }
    } else if (moveDirection < 0) {
        if (selected <= start) {
            wanted = std::max(0, selected - 1);
        }
    } else if (selected < start) {
        wanted = selected;
    } else if (selected >= start + slots) {
        wanted = selected - slots + 1;
    }

    scrollOffset = (float) std::max(0, std::min(wanted, maxStart)) * height;
    updateScroll();
}

void Menu::updateSelectionState() {
    for (int i = 0; i < (int) buttons.size(); i++) {
        buttons[i]->selected = i == index;
    }
}

void Menu::showCurrent(MenuButton *button) {
    current->setVisibility(button->isVisible() ? Visibility::Visible : Visibility::Hidden);
    current->tweenTo(button->getPosition());
}

void Menu::hideCurrent() {
    current->setVisibility(Visibility::Hidden);
}

void Menu::onUpdate() {
    highlight->setFillColor(COLOR_HIGHLIGHT);
    highlight->setCursorColor(COLOR_ACCENT);
    current->setFillColor(COLOR_ACCENT, 60);
    current->setCursorColor(COLOR_ACCENT);
    RectangleShape::onUpdate();
}

void Menu::setItems(const std::vector<MenuItem> &items) {
    for (auto *button: buttons) {
        delete (button);
    }
    buttons.clear();

    const float height = itemHeight();
    const unsigned int fontSize = main->getFontSize(compact ? Main::FontSize::Small : Main::FontSize::Medium);
    FloatRect top = {0, menuTop(), Menu::getSize().x, height};
    FloatRect bottom = {0, Menu::getSize().y - 32.0f * main->getScaling().y, Menu::getSize().x, height};
    for (auto &item: items) {
        FloatRect &rect = item.position == MenuItem::Position::Top ? top : bottom;
        auto *button = new MenuButton(main, item, rect, fontSize);
        Menu::add(button);
        buttons.push_back(button);
        rect.top += item.position == MenuItem::Position::Top ? height : -height;
    }

    index = findFirstSelectableIndex();
    updateSelectionState();
    scrollOffset = 0.0f;
    moveDirection = 0;
    moveWrapped = false;
    skipEnsure = false;
    updateScroll();
    if (!buttons.empty()) {
        highlight->setPosition(0, buttons[index]->getPosition().y);
    }
}

void Menu::setCompact(bool value) {
    if (compact == value) {
        return;
    }
    compact = value;
    for (auto *h: {highlight, current}) {
        h->resize({getSize().x, itemHeight()});
        // the origin offset is computed from the size
        h->setOrigin(Origin::Left);
    }
    std::vector<MenuItem> items;
    for (auto *button: buttons) {
        items.push_back(button->item);
    }
    setItems(items);
}

void Menu::setTitle(const std::string &_title) {
    titleKey = _title;
    setTitleText(pplay::tr(titleKey));
}

void Menu::setTitleText(const std::string &text) {
    // the same margins on both sides
    float width = getSize().x - 32 * main->getScaling().x * 2;
    // smaller when a word doesn't fit (not cut)
    unsigned int size = main->getFontSize(Main::FontSize::Big);
    unsigned int minSize = main->getFontSize(Main::FontSize::Small);
    for (bool fits = false; !fits && size > minSize; size -= fits ? 0 : 1) {
        fits = true;
        size_t start = 0;
        while (start <= text.size() && fits) {
            size_t end = std::min(text.find(' ', start), text.size());
            fits = pplay::TextWrap::width(main->getFont(), size, text.substr(start, end - start)) <= width;
            start = end + 1;
        }
    }
    title->setCharacterSize(size);
    title->setString(pplay::TextWrap::wrap(main->getFont(), size, text, width, false));
}

void Menu::retranslate() {
    setTitleText(pplay::tr(titleKey));
    for (auto *button: buttons) {
        button->refreshText();
    }
}

void Menu::reset() {
    index = findFirstSelectableIndex();
    updateSelectionState();
    scrollOffset = 0.0f;
    moveDirection = 0;
    moveWrapped = false;
    skipEnsure = false;
    updateScroll();
    if (!buttons.empty()) {
        highlight->tweenTo({0, buttons[index]->getPosition().y});
    }
}
