//
// Audio files screen.
//

#include <algorithm>

#include "main.h"
#include "audio_view.h"
#include "image.h"
#include "poster_loader.h"

using namespace c2d;

FloatRect AudioView::zone() {
    // 1024x576 at 1080p, under the status bar
    return {0.2333f, 0.0926f, 0.5333f, 0.5333f};
}

AudioView::AudioView(Main *m) : Rectangle(m->getSize()), main(m) {
    Vector2f size = main->getSize();
    FloatRect z = zone();

    // over the mpv frame (its cover picture), but the visualizer
    background = new RectangleShape(size);
    background->setFillColor(Color::Black);
    AudioView::add(background);

    loader = new PosterLoader(main);
    // the sprite never points to a deleted texture
    placeholder = new C2DTexture(Vector2i{4, 4});
    picture = new Sprite(placeholder);
    picture->setOrigin(Origin::Center);
    picture->setPosition(size.x / 2, (z.top + z.height / 2) * size.y);
    picture->setVisibility(Visibility::Hidden);
    picture->add(new TweenAlpha(0, 255, 0.3f));
    AudioView::add(picture);

    note = new Sprite(new C2DTexture(main->getIo()->getDataPath() + "skin/audio_note.png"));
    float noteSize = z.height * size.y * 0.4f;
    note->setScale(noteSize / note->getTextureRect().width, noteSize / note->getTextureRect().height);
    note->setOrigin(Origin::Center);
    note->setPosition(size.x / 2, (z.top + z.height / 2) * size.y);
    note->setFillColor(Color(140, 140, 140));
    AudioView::add(note);

    AudioView::setVisibility(Visibility::Hidden);
}

AudioView::~AudioView() {
    delete (loader);
    delete (pictureTexture);
    delete (placeholder);
}

void AudioView::showPicture(c2d::Texture *texture) {
    picture->setTexture(placeholder, true);
    delete (pictureTexture);
    pictureTexture = texture;
    picture->setTexture(texture, true);
    // in the zone, the screen width at most
    Vector2f size = getSize();
    Vector2f tex = {(float) texture->getTextureRect().width, (float) texture->getTextureRect().height};
    float scale = std::min(size.x * 0.9f / tex.x, zone().height * size.y / tex.y);
    picture->setScale(scale, scale);
    picture->setOrigin(Origin::Center);
    picture->setVisibility(Visibility::Visible, true);
    // darker: the texts stay readable
    Color average = pplay::Image::average(texture);
    int high = std::max(average.r, std::max(average.g, average.b));
    float dark = std::min(0.5f, 90.0f / (float) std::max(1, high));
    background->setFillColor({(uint8_t) (average.r * dark), (uint8_t) (average.g * dark),
                              (uint8_t) (average.b * dark), 255});
}

void AudioView::hidePicture() {
    picture->setVisibility(Visibility::Hidden);
    picture->setTexture(placeholder, true);
    delete (pictureTexture);
    pictureTexture = nullptr;
    background->setFillColor(Color::Black);
}

void AudioView::updatePicture() {
    Player *player = main->getPlayer();
    const std::vector<std::string> &urls = player->getPictureUrls();
    if (player->getAudioScreen() != Player::AudioScreen::Picture) {
        if (pictureTexture != nullptr) {
            hidePicture();
        }
        pictureUrls.clear();
        loader->request("");
        return;
    }
    if (urls != pictureUrls) {
        pictureUrls = urls;
        pictureIndex = 0;
        hidePicture();
        loader->request(pictureUrls.empty() ? "" : pictureUrls[0]);
    }
    Texture *texture = nullptr;
    std::string error;
    if (!loader->poll({(int) getSize().x, (int) (zone().height * getSize().y)}, &texture, &error)) {
        return;
    }
    if (texture != nullptr) {
        showPicture(texture);
    } else if (++pictureIndex < pictureUrls.size()) {
        loader->request(pictureUrls[pictureIndex]);
    } else {
        player->onPicturesFailed();
    }
}

void AudioView::onUpdate() {
    update();
    Rectangle::onUpdate();
}

void AudioView::update() {
    Player *player = main->getPlayer();
    Mpv *mpv = player->getMpv();
    Player::AudioScreen screen = player->getAudioScreen();

    bool shown = screen != Player::AudioScreen::None && !mpv->isStopped();
    setVisibility(shown ? Visibility::Visible : Visibility::Hidden);
    updatePicture();
    if (!shown) {
        return;
    }
    background->setVisibility(screen != Player::AudioScreen::Visualizer ? Visibility::Visible : Visibility::Hidden);
    note->setVisibility(screen == Player::AudioScreen::Note ? Visibility::Visible : Visibility::Hidden);
}
