//
// Audio files screen: the cover / visualizer (mpv) / note zone, the title, the format and
// a progress bar under it.
//

#ifndef PPLAY_AUDIO_VIEW_H
#define PPLAY_AUDIO_VIEW_H

#include <string>
#include <vector>
#include "cross2d/c2d.h"

class Main;

class PosterLoader;

class AudioView : public c2d::Rectangle {

public:

    explicit AudioView(Main *main);

    ~AudioView() override;

    // the zone of the frame above the texts (ratios of the size)
    static c2d::FloatRect zone();

private:

    void onUpdate() override;

    void update();

    // the cover: the player candidates one after the other
    void updatePicture();

    void showPicture(c2d::Texture *texture);

    void hidePicture();

    Main *main;
    c2d::RectangleShape *background = nullptr;
    c2d::Sprite *picture = nullptr;
    c2d::Texture *pictureTexture = nullptr;
    c2d::Texture *placeholder = nullptr;
    PosterLoader *loader = nullptr;
    std::vector<std::string> pictureUrls;
    size_t pictureIndex = 0;
    c2d::Sprite *note = nullptr;
};

#endif //PPLAY_AUDIO_VIEW_H
