//
// Created by cpasjuste on 20/10/18.
//

#ifndef PPLAY_MENU_H
#define PPLAY_MENU_H

#include "cross2d/c2d.h"
#include "highlight.h"
#include "menu_button.h"

class RoundRect;

class Main;

class Menu : public c2d::RectangleShape {

public:

    Menu(Main *main, const c2d::FloatRect &rect, const std::string &title, const std::vector<MenuItem> &items,
         bool left);

    ~Menu() override;

    // all the menus
    static const std::vector<Menu *> &all();

    // Route input to the topmost visible menu before the player or filer can consume it.
    static bool routeInput(c2d::Input::Player *players);

    void reset();

    // Rebuild cached geometry, fonts and pointer hit areas at the current menu scale.
    void applyScale();

    // the item of that name selected (if shown)
    void select(const std::string &name);
    // the item of that id selected (if shown)
    void selectId(int id);

    // language change
    virtual void retranslate();

    void setTitle(const std::string &title);

    // the title, wrapped
    void setTitleText(const std::string &text);

    void setItems(const std::vector<MenuItem> &items);

    // smaller texts and items (long lists)
    void setCompact(bool compact);

    // on the left or the right of the screen
    void setSide(bool left);

    void setVisibility(c2d::Visibility visibility, bool tweenPlay = true) override;

    void onUpdate() override;

    void updateSelectionState();

    virtual void onOptionSelection(MenuItem *item) {};

    // A tap outside a submenu: hide it or return to its parent.
    virtual void onPointerBack() { setVisibility(c2d::Visibility::Hidden, true); }

    // left / right on an item: its value changed, false if it has none
    virtual bool onOptionChange(MenuItem *item, int direction) { return false; }

    // Pointer placed on a slider (0..1); false if this item has no direct slider action.
    virtual bool onOptionSlider(MenuItem *item, float part) { return false; }
    virtual void onOptionSliderEnd(MenuItem *item) {}

    // A second click on the left side of a selected slider restores its default value.
    virtual void onOptionSliderReset(MenuItem *item) { onOptionSelection(item); }

    virtual MenuItem *getSelection();

    bool isButtonSelectable(int buttonIndex) const;

    virtual std::vector<MenuButton *> getButtons() { return buttons; }

protected:

    bool onInput(c2d::Input::Player *players) override;

    int findFirstSelectableIndex() const;

    void moveSelection(int direction);

    virtual void updateScroll();

    virtual void updateSelectionHighlight() {}

    void ensureSelectionVisible();

    // the current value marker (settings, tracks...)
    void showCurrent(MenuButton *button);

    void hideCurrent();

    float menuTop() const;

    float menuBottom();

    float itemHeight() const;

    Main *main;
    c2d::Text *title;
    std::string titleKey;
    std::vector<MenuButton *> buttons;
    Highlight *highlight;
    Highlight *current;
    RoundRect *card;
    c2d::TweenPosition *slide = nullptr;
    float inset = 0;
    bool leftSide = false;

    void fit();
    float scrollOffset = 0.0f;
    int index = 0;

private:

    int visibleSlots();

    int topButtonCount() const;

    int topIndexOf(int buttonIndex) const;

    bool compact = false;
    // last move, for the scrolling
    int moveDirection = 0;
    bool moveWrapped = false;
    bool skipEnsure = false;
    bool pointerDown = false;
    bool pointerDragging = false;
    int pointerSlider = -1;
    int pointerButton = -1;
    bool pointerWasSelected = false;
    c2d::Vector2f appliedMenuScaling = {1, 1};
    c2d::Vector2f pointerStart;
    c2d::Vector2f pointerLast;
};

#endif //PPLAY_MENU_H
