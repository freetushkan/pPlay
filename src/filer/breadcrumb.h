//
// The current path as small blocks: root › dir › ... › current (its tail when too long).
//

#ifndef PPLAY_BREADCRUMB_H
#define PPLAY_BREADCRUMB_H

#include <string>
#include <vector>

#include "cross2d/c2d.h"

class Main;

class RoundRect;

class Breadcrumb : public c2d::Rectangle {

public:

    Breadcrumb(Main *main, const c2d::FloatRect &rect);

    void setPath(const std::string &path);

    // the width left by the status bar: the path laid out again on a change
    void setWidth(float width);

    bool isMenuButtonAt(const c2d::Vector2f &position) const;
    bool pathAt(const c2d::Vector2f &position, std::string *target) const;

private:

    struct Entry {
        std::string label;
        std::string path;
    };

    // the shown names: the place (device / host), then the directories
    std::vector<Entry> split(const std::string &path) const;

    Main *main;
    std::string current;
    std::vector<c2d::Text *> names;
    std::vector<c2d::Text *> arrows;
    RoundRect *last = nullptr;
    RoundRect *menuButton = nullptr;
    std::vector<c2d::RectangleShape *> menuLines;
    std::vector<std::string> visibleTargets;
    bool menuShown = false;
};

#endif //PPLAY_BREADCRUMB_H
