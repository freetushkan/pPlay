//
// Key hints of the details panel bottom. In the panel they are buttons too: TV
// remotes (CEC) have no square / triangle.
//

#ifndef PPLAY_KEY_HINTS_H
#define PPLAY_KEY_HINTS_H

#include <string>
#include <vector>
#include "cross2d/c2d.h"

class Main;

class KeyHints : public c2d::Rectangle {

public:

    struct Hint {
        unsigned int keys = 0; // a glyph, or the key name
        std::string action;    // menu key, translated
        int id = 0;

        bool operator==(const Hint &other) const {
            return keys == other.keys && action == other.action && id == other.id;
        }
    };

    KeyHints(Main *main, float width);

    // rebuilt on changes only
    void setHints(const std::vector<Hint> &hints);

    void setButtons(bool buttons);

    // -1: none
    void setFocus(int index);

    int getFocus() const { return focus; }

    int count() const { return (int) hints.size(); }

    int idAt(int index) const;

    // the closest item of the row above (-1) or below (1), -1 if none
    int neighbor(int index, int direction);

    bool sameRow(int a, int b) const;

    float getHeight() const { return height; }

    void onUpdate() override;

private:

    void layout();

    void applyColors();

    Main *main;
    std::vector<Hint> hints;
    std::string language;
    std::vector<c2d::RectangleShape *> items;
    std::vector<c2d::Text *> texts;
    std::vector<c2d::Sprite *> icons;
    bool buttons = false;
    int focus = -1;
    float height = 0;
};

#endif //PPLAY_KEY_HINTS_H
