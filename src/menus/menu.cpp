//
// Created by cpasjuste on 20/10/18.
//

#include <algorithm>
#include <cmath>

#include "main.h"
#include "menu.h"
#include "text_wrap.h"
#include "round_rect.h"
#include "input_map.h"

using namespace c2d;

static std::vector<Menu *> &menuRegistry() {
    // never destroyed: menus are deleted late, by the renderer destructor
    static auto *menus = new std::vector<Menu *>();
    return *menus;
}

Menu::Menu(Main *m, const c2d::FloatRect &rect, const std::string &_title, const std::vector<MenuItem> &items,
           bool left)
    : RectangleShape(rect), main(m) {
    // a rounded card, no border
    Menu::setFillColor(Color::Transparent);
    card = new RoundRect(Menu::getSize(), 16 * main->getMenuScaling().y);
    card->setFillColor({COLOR_BG.r, COLOR_BG.g, COLOR_BG.b, 245});
    card->setLayer(-2);
    Menu::add(card);

    highlight = new Highlight({Menu::getSize().x, itemHeight()});
    highlight->setOrigin(Origin::Left);
    highlight->setPosition(0, 0);
    Menu::add(highlight);

    current = new Highlight({Menu::getSize().x, itemHeight()}, Highlight::CursorPosition::Left);
    // the current value: the accent bar only
    current->setFillColor(COLOR_ACCENT, 0);
    current->setCursorColor(COLOR_ACCENT);
    current->setOrigin(Origin::Left);
    current->setPosition(0, 0);
    current->setLayer(-1);
    current->setVisibility(Visibility::Hidden);
    Menu::add(current);

    // on several lines when needed
    titleKey = _title;
    title = new Text("", main->getMenuFontSize(Main::FontSize::Big), main->getFont());
    title->setPosition(main->getMenuScaled(40, 40));
    title->setFillColor(COLOR_FONT);
    setTitleText(pplay::tr(_title));
    Menu::add(title);
    menuRegistry().push_back(this);

    setItems(items);

    // the height made to the items (fit()), the screen at most
    inset = rect.top;
    leftSide = left;
    appliedMenuScaling = main->getMenuScaling();
    if (left) {
        // as far from the left edge as from the top
        Menu::add(slide = new TweenPosition({-rect.width - rect.top, rect.top}, {rect.top, rect.top}, 0.2f));
    } else {
        // as far from the right edge as from the top
        Menu::add(slide = new TweenPosition({main->getSize().x, rect.top},
                                    {main->getSize().x - Menu::getSize().x - rect.top, rect.top}, 0.2f));
    }
    fit();
}

Menu::~Menu() {
    auto &menus = menuRegistry();
    menus.erase(std::remove(menus.begin(), menus.end(), this), menus.end());
}

const std::vector<Menu *> &Menu::all() {
    return menuRegistry();
}

bool Menu::routeInput(c2d::Input::Player *players) {
    auto &menus = menuRegistry();
    // Later-created menus are overlays of their parents (for example player submenus).
    for (auto it = menus.rbegin(); it != menus.rend(); ++it) {
        Menu *menu = *it;
        if (menu != nullptr && menu->isVisible()) {
            return menu->onInput(players);
        }
    }
    return false;
}

float Menu::menuTop() const {
    // under the title; when they fit, the items and the bottom ones (exit / stop, a row
    // apart) centered in the space left
    float top = title->getPosition().y + std::max((float) title->getCharacterSize(), title->getLocalBounds().height)
                + 56.0f * main->getMenuScaling().y;
    int bottomItems = 0, topItems = 0;
    for (auto *button: buttons) {
        bottomItems += button->item.position == MenuItem::Position::Bottom;
        topItems += button->item.enabled && button->item.position == MenuItem::Position::Top;
    }
    float area = getLocalBounds().height - 32.0f * main->getMenuScaling().y - top;
    // from the first item center to the last one bottom
    float used = ((float) (topItems + bottomItems - 1) + (bottomItems > 0 ? 0.5f : 0)) * itemHeight() + itemHeight() / 2;
    return used <= area ? std::round(top + (area - used) / 2) : top;
}

float Menu::menuBottom() {
    // above the bottom items (exit, stop...)
    int bottomItems = 0;
    for (auto *button: buttons) {
        bottomItems += button->item.position == MenuItem::Position::Bottom;
    }
    return getSize().y - 32.0f * main->getMenuScaling().y - (float) bottomItems * itemHeight();
}

float Menu::itemHeight() const {
    if (compact) {
        return std::round((float) main->getMenuFontSize(Main::FontSize::Small) * 1.7f);
    }
    return BUTTON_HEIGHT * main->getMenuScaling().y;
}

int Menu::visibleSlots() {
    float usable = menuBottom() - menuTop() - itemHeight();
    // the last one fits with half of its height under the center
    return usable <= 0 ? 1 : (int) ((usable + itemHeight() / 2) / itemHeight()) + 1;
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
    // Input points are already in Main's logical coordinates, so button hit tests
    // must use the menu transform without Main's renderer/window transform.
    const Transform menuTransform = getTransform();

    // Recover from a lost mouse-up / finger-up event. Without this, a menu could
    // remain in pointer-capture mode and ignore every later click until reopened.
    if (pointerDown && !(keys & Input::Touch) && !(keys & pplay::InputMap::PointerUp)) {
        if (pointerSlider >= 0 && pointerSlider < (int) buttons.size()) {
            buttons[pointerSlider]->clearSliderPreview();
        }
        pointerDown = pointerDragging = false;
        pointerSlider = -1;
        pointerButton = -1;
        pointerWasSelected = false;
    }

    if (buttons.empty()) {
        return true;
    }

    const bool pointerStarted = (keys & pplay::InputMap::PointerDown) != 0;
    if (pointerStarted) {
        // A fresh physical press always starts a new capture, even if the previous
        // mouse-up / finger-up was lost while a menu was hidden or a frame was skipped.
        if (pointerSlider >= 0 && pointerSlider < (int) buttons.size()) {
            buttons[pointerSlider]->clearSliderPreview();
        }
        pointerDown = pointerDragging = false;
        pointerSlider = -1;
        pointerButton = -1;
        pointerWasSelected = false;
    }

    if (keys & Input::Touch) {
        Vector2f touch = players[0].touch;
        if (!pointerDown || pointerStarted) {
            pointerDown = true;
            pointerDragging = false;
            pointerSlider = -1;
            pointerButton = -1;
            pointerWasSelected = false;
            pointerStart = pointerLast = touch;
            if (getGlobalBounds().contains(touch)) {
                for (int i = 0; i < (int) buttons.size(); i++) {
                    if (!isButtonSelectable(i) || !buttons[i]->isVisible()
                        || !buttons[i]->containsPointer(touch, menuTransform)) {
                        continue;
                    }
                    pointerButton = i;
                    pointerWasSelected = index == i;
                    index = i;
                    updateSelectionState();

                    // A slider track is an immediate pointer target; it does not need
                    // a separate focus tap. Its setting is committed only on PointerUp.
                    const float part = buttons[i]->sliderPartAt(touch, menuTransform);
                    if (buttons[i]->item.slider >= 0 && part >= 0.0f) {
                        pointerSlider = i;
                        pointerWasSelected = true;
                        buttons[i]->setSliderPreview(part);
                    }
                    highlight->tweenTo({0, buttons[index]->getPosition().y});
                    break;
                }
            }
        } else if (pointerSlider >= 0 && pointerSlider < (int) buttons.size()) {
            const float distance = touch.y - pointerStart.y;
            if (std::fabs(distance) > 12 * main->getMenuScaling().y
                || std::fabs(touch.x - pointerStart.x) > 12 * main->getMenuScaling().x) {
                pointerDragging = true;
            }
            const float part = buttons[pointerSlider]->sliderPartAt(touch, menuTransform, true);
            buttons[pointerSlider]->setSliderPreview(part);
            pointerLast = touch;
            return true;
        } else {
            float dy = touch.y - pointerLast.y;
            float distance = touch.y - pointerStart.y;
            if (std::fabs(distance) > 12 * main->getMenuScaling().y) {
                pointerDragging = true;
            }
            if (pointerDragging) {
                float height = itemHeight();
                float maxOffset = (float) std::max(0, topButtonCount() - visibleSlots()) * height;
                scrollOffset = std::max(0.0f, std::min(maxOffset, scrollOffset - dy));
                updateScroll();
            }
            pointerLast = touch;
        }
        return true;
    } else if (keys & pplay::InputMap::PointerUp) {
        Vector2f touch = players[0].touch;
        bool dragged = pointerDragging;
        int button = pointerButton;
        int slider = pointerSlider;
        bool wasSelected = pointerWasSelected;
        pointerDown = false;
        pointerDragging = false;
        pointerSlider = -1;
        pointerButton = -1;
        pointerWasSelected = false;

        // Commit one value after the gesture. In particular, changing a menu scale
        // cannot move its own track underneath the pointer while that pointer is down.
        if (slider >= 0 && slider == button && slider < (int) buttons.size()) {
            MenuButton *sliderButton = buttons[slider];
            const float part = sliderButton->sliderPartAt(touch, menuTransform, true);
            sliderButton->clearSliderPreview();
            if (isButtonSelectable(slider) && sliderButton->isVisible() && part >= 0.0f
                && onOptionSlider(&sliderButton->item, part)) {
                sliderButton->refreshValue();
                onOptionSliderEnd(&sliderButton->item);
            }
            return true;
        }

        if (!dragged) {
            if (button < 0) {
                if (!getGlobalBounds().contains(touch)) {
                    onPointerBack();
                }
            } else if (button < (int) buttons.size() && isButtonSelectable(button)
                       && buttons[button]->isVisible()
                       && buttons[button]->containsPointer(touch, menuTransform)) {
                MenuItem &item = buttons[button]->item;
                if (item.slider >= 0) {
                    if (wasSelected) {
                        onOptionSliderReset(&item);
                        buttons[button]->refreshValue();
                    }
                    // A second tap outside the track restores the default value.
                } else if (item.opens || wasSelected) {
                    // An item whose sole purpose is opening a submenu transitions at once.
                    // Other actions require a second tap after the row received focus.
                    onOptionSelection(&item);
                }
                if (index >= 0 && index < (int) buttons.size()) {
                    highlight->tweenTo({0, buttons[index]->getPosition().y});
                }
            }
        }
        return true;
    } else if (keys & Input::Up) {
        moveSelection(-1);
        updateSelectionState();
    } else if (keys & Input::Down) {
        moveSelection(1);
        updateSelectionState();
    } else if (keys & (Input::Left | Input::Right) && isButtonSelectable(index)
               && onOptionChange(&buttons[index]->item, (keys & Input::Right) ? 1 : -1)) {
        buttons[index]->refreshValue();
    } else if (keys & Input::A && isButtonSelectable(index)) {
        if (buttons[index]->item.slider >= 0) {
            onOptionSliderReset(&buttons[index]->item);
            buttons[index]->refreshValue();
        } else {
            onOptionSelection(&buttons[index]->item);
        }
    }

    if (!skipEnsure) {
        ensureSelectionVisible();
    }
    skipEnsure = false;
    if (index >= 0 && index < (int) buttons.size()) {
        highlight->tweenTo({0, buttons[index]->getPosition().y});
    }

    return true;
}
void Menu::applyScale() {
    const Vector2f next = main->getMenuScaling();
    if (std::fabs(next.x - appliedMenuScaling.x) < 0.001f
        && std::fabs(next.y - appliedMenuScaling.y) < 0.001f) {
        return;
    }

    const float ratioX = next.x / std::max(0.001f, appliedMenuScaling.x);
    const float ratioY = next.y / std::max(0.001f, appliedMenuScaling.y);
    setSize(std::round(getSize().x * ratioX), getSize().y);
    inset *= ratioY;
    scrollOffset *= ratioY;
    appliedMenuScaling = next;

    title->setPosition(main->getMenuScaled(40, 40));
    setTitleText(pplay::tr(titleKey));
    highlight->resize({getSize().x, itemHeight()});
    current->resize({getSize().x, itemHeight()});

    delete card;
    card = new RoundRect(getSize(), 16 * next.y);
    card->setFillColor({COLOR_BG.r, COLOR_BG.g, COLOR_BG.b, 245});
    card->setLayer(-2);
    Menu::add(card);

    std::vector<MenuItem> items;
    items.reserve(buttons.size());
    for (auto *button: buttons) {
        items.push_back(button->item);
    }
    // Carry a live pointer capture across the button rebuild. This matters when a
    // menu-size slider is itself being dragged from the Interface category.
    const bool wasPointerDown = pointerDown;
    const bool wasPointerDragging = pointerDragging;
    const int capturedSlider = pointerSlider;
    const int capturedButton = pointerButton;
    const bool selectedBeforePress = pointerWasSelected;
    const Vector2f capturedStart = pointerStart;
    const Vector2f capturedLast = pointerLast;

    setItems(items);
    pointerDown = wasPointerDown;
    pointerDragging = wasPointerDragging;
    pointerSlider = capturedSlider;
    pointerButton = capturedButton;
    pointerWasSelected = selectedBeforePress;
    pointerStart = capturedStart;
    pointerLast = capturedLast;
    updateSelectionHighlight();
}

void Menu::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    // hidden without the slide (menus created hidden, rebuilt on each file)
    C2DObject::setVisibility(visibility, tweenPlay || visibility == Visibility::Visible);
    if (visibility != Visibility::Visible) {
        if (pointerSlider >= 0 && pointerSlider < (int) buttons.size()) {
            buttons[pointerSlider]->clearSliderPreview();
        }
        pointerDown = pointerDragging = false;
        pointerSlider = -1;
        pointerButton = -1;
        pointerWasSelected = false;
    }
    for (auto &button: buttons) {
        button->setVisibility(visibility, false);
    }
    if (visibility == Visibility::Visible) {
        // Items may have been reflowed while the menu was hidden (for example after
        // a DPI / UI-scale change). Keep the logical selection and realign its highlight
        // to the newly laid-out button instead of leaving it at the old screen position.
        if (buttons.empty()) {
            pointerDown = pointerDragging = false;
            pointerSlider = -1;
            pointerButton = -1;
            pointerWasSelected = false;
            return;
        }
        if (index < 0 || index >= (int) buttons.size()
            || !buttons[index]->item.enabled || !isButtonSelectable(index)) {
            index = findFirstSelectableIndex();
        }
        updateSelectionState();
        if (!skipEnsure) {
            ensureSelectionVisible();
        }
        skipEnsure = false;
        updateScroll();
        if (index >= 0 && index < (int) buttons.size()) {
            // Re-target the existing tween as well as the position. A tween left active
            // when the menu was hidden must not carry the highlight to its stale endpoint.
            highlight->tweenTo({0, buttons[index]->getPosition().y});
        }
    }
}

MenuItem *Menu::getSelection() {
    return &buttons[index]->item;
}

bool Menu::isButtonSelectable(int buttonIndex) const {
    return buttonIndex >= 0 && buttonIndex < (int) buttons.size()
           && buttons[buttonIndex]->item.enabled && buttons[buttonIndex]->item.selectable;
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
    // a held key stops on the last item, a new press wraps
    if (pplay::InputMap::isRepeat()) {
        int ahead = index + direction;
        while (ahead >= 0 && ahead < (int) buttons.size() && !isButtonSelectable(ahead)) {
            ahead += direction;
        }
        if (ahead < 0 || ahead >= (int) buttons.size()) {
            return;
        }
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

    // the bottom ones: half a row under the list when it fits, else at the bottom
    int bottomCount = 0;
    for (auto &button: buttons) {
        bottomCount += button->item.position == MenuItem::Position::Bottom;
    }
    float last = top + ((float) (topIndex + bottomCount) - 0.5f) * height;
    bool fits = last + height / 2 <= getSize().y - 32.0f * main->getMenuScaling().y + 1;
    float lowest = fits ? last : getSize().y - 32.0f * main->getMenuScaling().y;
    int k = 0;
    for (auto &button: buttons) {
        if (button->item.position == MenuItem::Position::Bottom) {
            button->setPosition(0, lowest - (float) k++ * height);
        }
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
    current->setFillColor(COLOR_ACCENT, 0);
    current->setCursorColor(COLOR_ACCENT);
    RectangleShape::onUpdate();
}

void Menu::setItems(const std::vector<MenuItem> &list) {
    // Keep the current item across a rebuild. Main-menu module lists are rebuilt when
    // networks or availability change; using the old numeric index can point at a
    // completely different row (or the title area after the layout changes).
    std::string selectedName;
    int selectedId = 0;
    if (index >= 0 && index < (int) buttons.size()) {
        selectedName = buttons[index]->item.name;
        selectedId = buttons[index]->item.id;
    }

    // the bottom ones (exit, stop) last in the list: no footer
    std::vector<MenuItem> items;
    for (int bottom = 0; bottom < 2; bottom++) {
        for (auto &item: list) {
            if ((item.position == MenuItem::Position::Bottom) == (bottom == 1)) {
                items.push_back(item);
                items.back().position = MenuItem::Position::Top;
            }
        }
    }
    pointerDown = pointerDragging = false;
    pointerSlider = -1;
    pointerButton = -1;
    pointerWasSelected = false;
    for (auto *button: buttons) {
        delete (button);
    }
    buttons.clear();

    const float height = itemHeight();
    const unsigned int fontSize = main->getMenuFontSize(compact ? Main::FontSize::Small : Main::FontSize::Medium);
    FloatRect top = {0, menuTop(), Menu::getSize().x, height};
    FloatRect bottom = {0, Menu::getSize().y - 32.0f * main->getMenuScaling().y, Menu::getSize().x, height};
    for (auto &item: items) {
        FloatRect &rect = item.position == MenuItem::Position::Top ? top : bottom;
        auto *button = new MenuButton(main, item, rect, fontSize);
        Menu::add(button);
        buttons.push_back(button);
        rect.top += item.position == MenuItem::Position::Top ? height : -height;
    }

    index = findFirstSelectableIndex();
    // Prefer the stable id + name pair; fall back to the name if an id was renumbered.
    bool restored = false;
    if (!selectedName.empty()) {
        for (int i = 0; i < (int) buttons.size(); i++) {
            if (isButtonSelectable(i) && buttons[i]->item.name == selectedName
                && buttons[i]->item.id == selectedId) {
                index = i;
                restored = true;
                break;
            }
        }
        if (!restored) {
            for (int i = 0; i < (int) buttons.size(); i++) {
                if (isButtonSelectable(i) && buttons[i]->item.name == selectedName) {
                    index = i;
                    restored = true;
                    break;
                }
            }
        }
    }
    updateSelectionState();
    moveDirection = 0;
    moveWrapped = false;
    skipEnsure = false;
    fit();
    ensureSelectionVisible();
    updateScroll();
    if (!buttons.empty()) {
        highlight->tweenTo({0, buttons[index]->getPosition().y});
    } else {
        highlight->setPosition(0, menuTop());
    }
}
void Menu::fit() {
    // as high as the title and the items (the bottom ones half a row apart), centered
    const float sc = main->getMenuScaling().y, h = itemHeight();
    int topItems = 0, bottomItems = 0;
    for (auto *button: buttons) {
        topItems += button->item.enabled && button->item.position == MenuItem::Position::Top;
        bottomItems += button->item.position == MenuItem::Position::Bottom;
    }
    float titleBottom = title->getPosition().y + std::max((float) title->getCharacterSize(), title->getLocalBounds().height);
    float needed = titleBottom + 56 * sc + ((float) (topItems + bottomItems) + (bottomItems > 0 ? 0.5f : 0)) * h
                   + 32 * sc - h / 2;
    float height = std::round(std::min(needed, main->getSize().y - 2 * inset));
    float y = std::round((main->getSize().y - height) / 2);
    setSize(getSize().x, height);
    card->setSize(getSize().x, height);
    float x = leftSide ? inset : main->getSize().x - getSize().x - inset;
    if (slide == nullptr) {
        return;
    }
    slide->setFromTo({leftSide ? -getSize().x - inset : main->getSize().x, y}, {x, y}, 0.2f);
    if (isVisible()) {
        setPosition(x, y);
    }
}

void Menu::setSide(bool left) {
    if (leftSide == left) {
        return;
    }
    leftSide = left;
    fit();
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
    float width = getSize().x - 32 * main->getMenuScaling().x * 2;
    // smaller when a word doesn't fit (not cut)
    unsigned int size = main->getMenuFontSize(Main::FontSize::Big);
    unsigned int minSize = main->getMenuFontSize(Main::FontSize::Small);
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

void Menu::select(const std::string &name) {
    for (size_t i = 0; i < buttons.size(); i++) {
        if (buttons[i]->item.name == name && isButtonSelectable((int) i)) {
            index = (int) i;
            updateSelectionState();
            ensureSelectionVisible();
            updateScroll();
            highlight->tweenTo({0, buttons[index]->getPosition().y});
            return;
        }
    }
}

void Menu::selectId(int id) {
    for (size_t i = 0; i < buttons.size(); i++) {
        if (buttons[i]->item.id == id && isButtonSelectable((int) i) && buttons[i]->item.enabled) {
            index = (int) i;
            updateSelectionState();
            ensureSelectionVisible();
            updateScroll();
            highlight->tweenTo({0, buttons[index]->getPosition().y});
            return;
        }
    }
}

void Menu::reset() {
    pointerDown = pointerDragging = false;
    pointerSlider = -1;
    pointerButton = -1;
    pointerWasSelected = false;
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
