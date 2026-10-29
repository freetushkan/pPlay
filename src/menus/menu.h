//
// Created by cpasjuste on 20/10/18.
//

#ifndef PPLAY_MENU_H
#define PPLAY_MENU_H

#include "cross2d/c2d.h"
#include "highlight.h"
#include "menu_button.h"

class Main;

class Menu : public c2d::RectangleShape {

public:

    Menu(Main *main, const c2d::FloatRect &rect, const std::string &title, const std::vector<MenuItem> &items,
         bool left);

    ~Menu() override;

    // all the menus
    static const std::vector<Menu *> &all();

    void reset();

    // language change
    virtual void retranslate();

    void setTitle(const std::string &title);

    void setItems(const std::vector<MenuItem> &items);

    // smaller texts and items (long lists)
    void setCompact(bool compact);

    void setVisibility(c2d::Visibility visibility, bool tweenPlay = true) override;

    void onUpdate() override;

    void updateSelectionState();

    virtual void onOptionSelection(MenuItem *item) {};

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
};

#endif //PPLAY_MENU_H
