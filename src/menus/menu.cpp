//
// Created by cpasjuste on 20/10/18.
//

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "cross2d/c2d.h"
#include "main.h"
#include "menu.h"
#include "utility.h"
#include "i18n.h"
#include "clip_rect.h"

using namespace c2d;

namespace {
    struct ScrollState {
        int direction = 0;
        bool wrapped = false;
        bool skipEnsure = false;
    };
    std::unordered_map<Menu *, ScrollState> g_scrollStates;

    inline float menuTop(Main *main) {
        return 200.0f * main->getScaling().y;
    }

    inline float menuBottom(Menu *menu, Main *main) {
        return menu->getSize().y - (32.0f * main->getScaling().y);
    }

    inline float menuSpacing(Main *main) {
        return 64.0f * main->getScaling().y;
    }

    inline float menuButtonHeight(Main *main) {
        return BUTTON_HEIGHT * main->getScaling().y;
    }

    inline int visibleSlots(Main *main, float top, float bottom) {
        const float spacing = menuSpacing(main);
        const float buttonHeight = menuButtonHeight(main);
        if (spacing <= 0.0f) {
            return 1;
        }

        const float usable = bottom - top - buttonHeight;
        if (usable <= 0.0f) {
            return 1;
        }

        return std::max(1, static_cast<int>(usable / spacing) + 1);
    }

    inline int topButtonCount(const std::vector<MenuButton *> &buttons) {
        int count = 0;
        for (auto *button: buttons) {
            if (!button->item.enabled) continue;
            if (button->item.position == MenuItem::Position::Top) {
                ++count;
            }
        }
        return count;
    }

    inline int topIndexOfButton(const std::vector<MenuButton *> &buttons, int index) {
        int topIndex = 0;
        for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
            if (!buttons[i]->item.enabled) continue;
            if (buttons[i]->item.position != MenuItem::Position::Top) {
                continue;
            }
            if (i == index) {
                return topIndex;
            }
            ++topIndex;
        }
        return 0;
    }

    inline ScrollState currentScrollState(Menu *menu) {
        const auto it = g_scrollStates.find(menu);
        if (it == g_scrollStates.end()) {
            return {};
        }
        return it->second;
    }

    inline int clampInt(int value, int minValue, int maxValue) {
        return std::max(minValue, std::min(value, maxValue));
    }
}

namespace {
    // the selected item text scrolls (by pixel) when it doesn't fit: pause, one
    // turn, pause...
#define SCROLL_WAIT_MS 1500
#define SCROLL_SPEED 90.0f    // pixels per second at 1080p
#define SCROLL_GAP "     "
    c2d::C2DClock g_menuScrollClock;

    long long scrollNowMs() {
        return (long long) g_menuScrollClock.getElapsedTime().asMilliseconds();
    }

    std::string sanitizeText(const std::string &input) {
        std::string result;
        result.reserve(input.size());
        bool inSpace = false;
        for (char ch : input) {
            if (ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ') {
                if (!inSpace) {
                    result += ' ';
                    inSpace = true;
                }
            } else {
                result += ch;
                inSpace = false;
            }
        }
        if (!result.empty() && result.front() == ' ') result.erase(0, 1);
        if (!result.empty() && result.back() == ' ') result.pop_back();
        
        return result;
    }
}

static std::vector<Menu *> &menuRegistry() {
    // never destroyed: menus are deleted late, by the renderer destructor
    static auto *menus = new std::vector<Menu *>();
    return *menus;
}

std::string MenuItem::text() const {
    return label.empty() ? pplay::tr(name) : label;
}

void MenuButton::refreshText() {
    name->setSizeMax(0, 0);
    name->setString(item.text());
    scrollable = name->getLocalBounds().width > maxTextWidth;
    scrolling = false;
    scrollStartMs = -1;
    showStatic();
}

void MenuButton::showStatic() {
    // cut by character, as before
    name->setString(item.text());
    name->setSizeMax(maxTextWidth, MenuButton::getSize().y);
    name->setPosition(0, clip->getSize().y / 2);
}

void MenuButton::startScroll() {
    // text, gap, text: at one turn (text + gap width) it looks like the start
    std::string text = sanitizeText(item.text());
    name->setSizeMax(0, 0);
    name->setString(text);
    float single = name->getLocalBounds().width;
    name->setString(text + SCROLL_GAP + text);
    scrollCycle = std::max(1.0f, name->getLocalBounds().width - single);
}

MenuButton::MenuButton(Main *main, const MenuItem &item, const FloatRect &rect) : Rectangle(rect) {

    this->item = item;
    MenuButton::setOrigin(Origin::Left);

    if (!item.icon.empty()) {
        icon = new C2DTexture(main->getIo()->getDataPath() + "skin/" + item.icon);
        icon->setOrigin(Origin::Left);
        icon->setPosition(16 * main->getScaling().x, MenuButton::getSize().y / 2);
        icon->setScale(main->getScaling()); // same size as before at 1080p, linear with the resolution
        icon->setFillColor(COLOR_FONT);
        MenuButton::add(icon);
    }

    name = new Text(item.text(), main->getFontSize(Main::FontSize::Medium), main->getFont());
    name->setOrigin(Origin::Left);
    if (pplay::Utility::isValidHexColor(item.data_str)) {
        name->setFillColor(pplay::Utility::hexToColor(item.data_str));
    } else {
        name->setFillColor(COLOR_FONT);
    }

    float textX = !item.icon.empty()
                  ? (ICON_SIZE + 32.0f) * main->getScaling().x
                  : 16.0f * main->getScaling().x;
    float rightPadding = 16.0f * main->getScaling().x;
    float maxWidth = std::max(0.0f, MenuButton::getSize().x - textX - rightPadding);

    maxTextWidth = maxWidth;
    scrollSpeed = SCROLL_SPEED * main->getScaling().x;
    clip = new pplay::ClipRect({textX, 0, maxWidth, MenuButton::getSize().y});
    clip->add(name);
    MenuButton::add(clip);
    refreshText();
}

void MenuButton::onUpdate() {
    Rectangle::onUpdate();
    if (!scrollable || !isVisible()) {
        return;
    }
    if (!selected) {
        if (scrollStartMs >= 0) {
            scrolling = false;
            scrollStartMs = -1;
            showStatic();
        }
        return;
    }

    long long now = scrollNowMs();
    if (scrollStartMs < 0) {
        scrollStartMs = now;
        return;
    }
    long long elapsed = now - scrollStartMs - SCROLL_WAIT_MS;
    if (elapsed < 0) {
        return;
    }
    if (!scrolling) {
        startScroll();
        scrolling = true;
    }
    auto turnMs = (long long) (scrollCycle / scrollSpeed * 1000.0f);
    long long phase = elapsed % (turnMs + SCROLL_WAIT_MS);
    float offset = phase < turnMs ? (float) phase * scrollSpeed / 1000.0f : 0;
    name->setPosition(-std::round(offset), clip->getSize().y / 2);
}

MenuButton::~MenuButton() = default;

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
    highlight->setPosition(0, menuTop(main));
    Menu::add(highlight);

    // title
    // title, on several lines when needed
    titleKey = _title;
    title = new Text(pplay::tr(_title), main->getFontSize(Main::FontSize::Big), main->getFont());
    title->setPosition(main->getScaled(32, 32));
    title->setFillColor(COLOR_FONT);
    title->setOverflow(Text::NewLine);
    title->setSizeMax(Menu::getSize().x - main->getScaled(48, 0).x,
                      menuTop(main) - main->getScaled(0, 32 + 24).y);
    Menu::add(title);
    menuRegistry().push_back(this);

    setItems(items);

    // tween!
    if (left) {
        Menu::add(new TweenPosition({-rect.width, 0}, {0, 0}, 0.2f));
    } else {
        Menu::add(new TweenPosition({main->getSize().x, 0},
                                    {main->getSize().x - Menu::getSize().x, 0}, 0.2f));
    }
}

const std::vector<Menu *> &Menu::all() {
    return menuRegistry();
}

Menu::~Menu() {
    auto &menus = menuRegistry();
    menus.erase(std::remove(menus.begin(), menus.end(), this), menus.end());
    g_scrollStates.erase(this);
}

bool Menu::onInput(c2d::Input::Player *players) {
    unsigned int keys = players[0].buttons;

    if (buttons.empty()) {
        return true;
    }

    if (keys & Input::Touch) {
        g_scrollStates[this] = {};
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
            updateSelectionState();
        } else if (keys & Input::Down) {
            moveSelection(1);
            updateSelectionState();
        } else if (keys & Input::A && isButtonSelectable(index)) {
            onOptionSelection(&buttons[index]->item);
        }
    }

    if (!g_scrollStates[this].skipEnsure) {
        ensureSelectionVisible();
    }
    g_scrollStates[this].skipEnsure = false;
    highlight->tweenTo({0, buttons[index]->getPosition().y});

    return true;
}

void Menu::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    C2DObject::setVisibility(visibility, true);
    for (auto &button: buttons) {
        button->setVisibility(visibility, false);
    }
    if (visibility == Visibility::Visible) {
        if (!g_scrollStates[this].skipEnsure) {
            ensureSelectionVisible();
        }
        g_scrollStates[this].skipEnsure = false;
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

    {
        auto &state = g_scrollStates[this];
        state.direction = direction;
        state.wrapped = false;
        state.skipEnsure = false;
    }

    int next = index;
    do {
        next += direction;
        if (next < 0) {
            next = (int) buttons.size() - 1;
            auto &state = g_scrollStates[this];
            state.wrapped = true;
            state.skipEnsure = true;
            scrollOffset = (float) std::max(0, topButtonCount(buttons) - visibleSlots(main, menuTop(main), menuBottom(this, main))) * menuSpacing(main);
            updateScroll();
        } else if (next >= (int) buttons.size()) {
            next = 0;
            auto &state = g_scrollStates[this];
            state.wrapped = true;
            state.skipEnsure = true;
            scrollOffset = 0.0f;
            updateScroll();
        }
    } while (!isButtonSelectable(next) && next != index);

    if (isButtonSelectable(next)) {
        const int previous = index;
        index = next;
        auto &state = g_scrollStates[this];
        state.direction = direction;
        state.wrapped = (direction > 0 && next < previous) || (direction < 0 && next > previous);
    }
}

void Menu::updateScroll() {
    const float top = menuTop(main);
    const float bottom = menuBottom(this, main);
    const float buttonHeight = menuButtonHeight(main);
    const float spacing = menuSpacing(main);

    const int startSlot = (spacing > 0.0f)
        ? std::max(0, static_cast<int>(scrollOffset / spacing + 0.5f))
        : 0;

    int topIndex = 0;
    for (auto &button: buttons) {
        if (button->item.position == MenuItem::Position::Top) {
            if (!button->item.enabled) {
                button->setVisibility(Visibility::Hidden);
                continue;
            }
            const float y = top + ((float) (topIndex - startSlot) * spacing);
            button->setPosition(0, y);

            const bool visible = (y + buttonHeight > top) && (y < bottom);
            button->setVisibility(visible ? Visibility::Visible : Visibility::Hidden);
            topIndex++;
        }
    }
}

void Menu::ensureSelectionVisible() {
    if (buttons.empty() || buttons[index]->item.position != MenuItem::Position::Top) {
        return;
    }

    const float top = menuTop(main);
    const float bottom = menuBottom(this, main);
    const float spacing = menuSpacing(main);
    const int totalTopButtons = topButtonCount(buttons);
    const int selectedTop = topIndexOfButton(buttons, index);
    const int slotCount = visibleSlots(main, top, bottom);
    const int maxStart = std::max(0, totalTopButtons - slotCount);
    const int currentStart = (spacing > 0.0f)
        ? clampInt(static_cast<int>(scrollOffset / spacing + 0.5f), 0, maxStart)
        : 0;
    const ScrollState state = currentScrollState(this);

    int desiredStart = currentStart;

    if (totalTopButtons <= slotCount || spacing <= 0.0f) {
        desiredStart = 0;
    } else if (state.wrapped) {
        desiredStart = (state.direction > 0) ? 0 : maxStart;
    } else if (state.direction > 0) {
        const int keepSlot = std::max(0, slotCount - 2);
        const int scrollThreshold = currentStart + std::max(0, slotCount - 1);
        if (selectedTop >= scrollThreshold) {
            desiredStart = selectedTop - keepSlot;
        }

        if (selectedTop >= totalTopButtons - 1) {
            desiredStart = maxStart;
        }
    } else if (state.direction < 0) {
        if (selectedTop <= currentStart) {
            desiredStart = std::max(0, selectedTop - 1);
        }

        if (selectedTop <= 0) {
            desiredStart = 0;
        }
    } else {
        if (selectedTop < currentStart) {
            desiredStart = selectedTop;
        } else if (selectedTop >= currentStart + slotCount) {
            desiredStart = selectedTop - slotCount + 1;
        }
    }

    desiredStart = clampInt(desiredStart, 0, maxStart);
    scrollOffset = (float) desiredStart * spacing;

    updateScroll();
}

void Menu::updateSelectionState() {
    for (int i = 0; i < (int)buttons.size(); i++) {
        buttons[i]->selected = (i == index);
    }
}

void Menu::onUpdate() {
    highlight->setFillColor(COLOR_HIGHLIGHT);
    highlight->setCursorColor(COLOR_ACCENT);
    RectangleShape::onUpdate();
}

void Menu::setItems(const std::vector<MenuItem> &items) {
    for (auto *button: buttons) {
        delete (button);
    }
    buttons.clear();

    FloatRect top = {0, menuTop(main),
                     Menu::getSize().x, BUTTON_HEIGHT * main->getScaling().y};
    FloatRect bottom = {0, Menu::getSize().y - (32 * main->getScaling().y),
                        Menu::getSize().x, BUTTON_HEIGHT * main->getScaling().y};

    for (auto &item: items) {
        if (item.position == MenuItem::Position::Top) {
            auto *option = new MenuButton(main, item, top);
            Menu::add(option);
            buttons.push_back(option);
            top.top += menuSpacing(main);
        } else {
            auto *option = new MenuButton(main, item, bottom);
            Menu::add(option);
            buttons.push_back(option);
            bottom.top -= menuSpacing(main);
        }
    }

    index = findFirstSelectableIndex();
    updateSelectionState();
    scrollOffset = 0.0f;
    g_scrollStates[this] = {};
    updateScroll();
    if (!buttons.empty()) {
        highlight->setPosition(0, buttons[index]->getPosition().y);
    }
}

void Menu::setTitle(const std::string &_title) {
    titleKey = _title;
    title->setString(pplay::tr(titleKey));
}

void Menu::retranslate() {
    title->setString(pplay::tr(titleKey));
    for (auto *button: buttons) {
        button->refreshText();
    }
}

void Menu::reset() {
    index = findFirstSelectableIndex();
    updateSelectionState();
    scrollOffset = 0.0f;
    g_scrollStates[this] = {};
    updateScroll();
    if (!buttons.empty()) {
        highlight->tweenTo({0, buttons[index]->getPosition().y});
    }
}
