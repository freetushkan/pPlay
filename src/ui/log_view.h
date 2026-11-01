//
// Settings > System > View log: the end of pplay.log, scrolled with the keys.
//

#ifndef PPLAY_LOG_VIEW_H
#define PPLAY_LOG_VIEW_H

#include <string>
#include <vector>
#include "cross2d/c2d.h"

class Main;

class LogView : public c2d::RectangleShape {

public:

    explicit LogView(Main *main);

    // read again, the last lines shown
    void show();

    bool onInput(c2d::Input::Player *players) override;

private:

    void load();

    void render();

    Main *main;
    c2d::RectangleShape *box;
    c2d::Text *title;
    c2d::Text *body;
    c2d::Text *hint;
    c2d::RectangleShape *scrollBar;
    std::vector<std::string> lines;
    int first = 0;
    int maxLines = 1;
    c2d::Vector2f bodySize;
};

#endif //PPLAY_LOG_VIEW_H
