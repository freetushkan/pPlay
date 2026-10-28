//
// Right side panel of the filer: poster and information about the selection.
//

#ifndef PPLAY_PREVIEW_VIEW_H
#define PPLAY_PREVIEW_VIEW_H

#include <atomic>
#include <string>
#include "cross2d/c2d.h"
#include "media_file.h"

class Main;

class PreviewView : public c2d::Rectangle {

public:

    PreviewView(Main *main, const c2d::FloatRect &rect);

    ~PreviewView() override;

    void setFile(const MediaFile &file);

    void onUpdate() override;

private:

    static int downloadThread(void *data);

    std::string posterCachePath(const std::string &url) const;

    void showPoster(const std::string &url);

    void hidePoster();

    void setPosterStatus(const std::string &status);

    void layout();

    Main *main;
    c2d::Sprite *poster;
    c2d::Texture *posterTexture = nullptr;
    c2d::Texture *placeholder = nullptr;
    float top = 0;
    std::string infoText;
    std::string posterStatus;
    c2d::RectangleShape *posterFrame;
    c2d::Text *title;
    c2d::Text *details;
    c2d::Text *hint;
    c2d::RectangleShape *separator;

    // poster loading (debounced, threaded download to data/cache/posters)
    std::string wantedPoster;
    std::string shownPoster;
    std::string downloadUrl;
    std::string downloadPath;
    std::string failedPoster;
    std::string downloadStatus;
    c2d::C2DThread *thread = nullptr;
    std::atomic<bool> downloadDone{false};
    std::atomic<bool> downloadOk{false};
    c2d::C2DClock selectionClock;
};

#endif //PPLAY_PREVIEW_VIEW_H
