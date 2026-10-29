//
// Right side panel of the filer: title, poster and details of the selection, key
// hints at the bottom. Focused (right), it scrolls and the hints become buttons.
//

#ifndef PPLAY_DETAILS_VIEW_H
#define PPLAY_DETAILS_VIEW_H

#include <string>
#include "cross2d/c2d.h"
#include "media_file.h"

class Main;

class KeyHints;

class PosterLoader;

namespace pplay {
    class ClipRect;
}

class DetailsView : public c2d::Rectangle {

public:

    // what the filer does on a key (or a hint button)
    enum class Action {
        None,
        Open,
        Parent,
        Menu,
        Panel,
        Files,
        Player,
        Reset
    };

    DetailsView(Main *main, const c2d::FloatRect &rect);

    ~DetailsView() override;

    void setFile(const MediaFile &file);

    // watch later / TorrServer viewed state to reset
    static bool isResettable(const MediaFile &file);

    bool isFocused() const { return focused; }

    void setFocused(bool focused);

    // the filer picks a file (external tracks)
    void setPicking(bool value) { picking = value; }

    // keys while focused
    Action onKeys(unsigned int keys);

    void onUpdate() override;

private:

    void updateHints();

    void showPoster(c2d::Texture *texture);

    void hidePoster();

    void layout();

    void scrollBy(float delta);

    void updateScroll();

    Main *main;
    MediaFile file;
    std::string posterUrl;
    std::string shownPoster;
    std::string posterError;
    std::string heading;
    std::string details;
    PosterLoader *loader;
    c2d::Texture *posterTexture = nullptr;
    c2d::Texture *placeholder;

    pplay::ClipRect *view;
    c2d::Rectangle *content;
    c2d::Text *title;
    c2d::Sprite *poster;
    c2d::RectangleShape *posterFrame;
    c2d::Text *detailsText;
    c2d::RectangleShape *scrollBar;
    c2d::RectangleShape *focusBar;
    c2d::RectangleShape *separator;
    KeyHints *hints;

    bool focused = false;
    bool picking = false;
    // focused: -1 the content, else a hint button
    int button = -1;
    float scroll = 0;
    float scrollShown = 0;
    float contentHeight = 0;
    bool playing = false;
    // the minimized player covers the panel bottom
    bool playerShown = false;
    c2d::C2DClock frameClock;
};

#endif //PPLAY_DETAILS_VIEW_H
