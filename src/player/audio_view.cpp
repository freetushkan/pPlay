//
// Audio files screen.
//

#include <algorithm>
#include <cstdio>

#include "main.h"
#include "audio_view.h"
#include "encodings.h"
#include "image.h"
#include "player_osd.h"
#include "poster_loader.h"

using namespace c2d;

namespace {
    std::string clock(double seconds) {
        int s = std::max(0, (int) seconds);
        char text[32];
        if (s >= 3600) {
            snprintf(text, sizeof(text), "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
        } else {
            snprintf(text, sizeof(text), "%d:%02d", s / 60, s % 60);
        }
        return text;
    }

    std::string channels(long count) {
        switch (count) {
            case 1:
                return "mono";
            case 2:
                return "stereo";
            case 6:
                return "5.1";
            case 8:
                return "7.1";
            default:
                return count > 0 ? std::to_string(count) + "ch" : "";
        }
    }
}

FloatRect AudioView::zone() {
    // 1024x576 at 1080p, under the status bar
    return {0.2333f, 0.0926f, 0.5333f, 0.5333f};
}

AudioView::AudioView(Main *m) : Rectangle(m->getSize()), main(m) {
    Vector2f size = main->getSize();
    FloatRect z = zone();
    float bottom = (z.top + z.height) * size.y;

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

    title = new Text("", main->getFontSize(Main::FontSize::Bigger), main->getFont());
    title->setFillColor(COLOR_FONT);
    title->setOrigin(Origin::Center);
    title->setPosition(size.x / 2, bottom + 0.055f * size.y);
    title->setSizeMax(size.x * 0.8f, 0);
    AudioView::add(title);

    format = new Text("", main->getFontSize(Main::FontSize::Small), main->getFont());
    format->setFillColor(Color(150, 150, 150));
    format->setOrigin(Origin::Center);
    format->setPosition(size.x / 2, bottom + 0.1f * size.y);
    AudioView::add(format);

    // the bar and its times, hidden with the OSD
    float width = size.x * 0.47f;
    float height = std::max(2.0f, 4 * main->getScaling().y);
    bar = new Rectangle({(size.x - width) / 2, bottom + 0.145f * size.y, width, height});
    AudioView::add(bar);

    progress = new Progress({0, 0, width, height});
    progress->setBgColor(Color(255, 255, 255, 40));
    bar->add(progress);
    buffered = new RectangleShape({0, 0, 0, height});
    buffered->setFillColor(Color(255, 255, 255, 50));
    progress->add(buffered);
    buffered->setLayer(-1);

    float radius = std::max(3.0f, 6 * main->getScaling().y);
    knob = new CircleShape(radius);
    knob->setOrigin(Origin::Center);
    knob->setFillColor(COLOR_WHITE);
    bar->add(knob);

    float gap = 14 * main->getScaling().x;
    elapsed = new Text("0:00", main->getFontSize(Main::FontSize::Small), main->getFont());
    elapsed->setFillColor(Color(170, 170, 170));
    elapsed->setOrigin(Origin::Right);
    elapsed->setPosition(-gap, height / 2);
    bar->add(elapsed);

    remaining = new Text("-0:00", main->getFontSize(Main::FontSize::Small), main->getFont());
    remaining->setFillColor(Color(170, 170, 170));
    remaining->setOrigin(Origin::Left);
    remaining->setPosition(width + gap, height / 2);
    bar->add(remaining);

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

void AudioView::updateTexts() {
    Player *player = main->getPlayer();
    Mpv *mpv = player->getMpv();

    // "Artist — Title", else the player title (file name)
    std::string artist = encoding::fix(mpv->getString("metadata/by-key/artist"));
    std::string name = encoding::fix(mpv->getString("metadata/by-key/title"));
    if (name.empty()) {
        name = encoding::fix(player->getTitle());
    }
    title->setString(artist.empty() ? name : artist + " — " + name);

    std::vector<std::string> parts;
    std::string codec = c2d::Utility::toUpper(mpv->getString("audio-codec-name"));
    long rate = mpv->getInt("audio-params/samplerate", 0);
    long bitrate = mpv->getInt("audio-bitrate", 0);
    char text[32];
    if (!codec.empty()) {
        parts.push_back(codec);
    }
    if (rate > 0) {
        snprintf(text, sizeof(text), "%g kHz", (double) rate / 1000);
        parts.emplace_back(text);
    }
    std::string layout = channels(mpv->getInt("audio-params/channel-count", 0));
    if (!layout.empty()) {
        parts.push_back(layout);
    }
    if (bitrate > 0) {
        parts.push_back(std::to_string(bitrate / 1000) + " kb/s");
    }
    long duration = mpv->getDuration();
    if (duration > 0) {
        parts.push_back(clock((double) duration));
    }
    std::string line;
    for (auto &part: parts) {
        line += (line.empty() ? "" : " · ") + part;
    }
    format->setString(line);
}

void AudioView::onUpdate() {
    update();
    // the texts geometry
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
        playerTitle.clear();
        return;
    }
    background->setVisibility(screen != Player::AudioScreen::Visualizer ? Visibility::Visible : Visibility::Hidden);
    note->setVisibility(screen == Player::AudioScreen::Note ? Visibility::Visible : Visibility::Hidden);

    // the texts in the full screen player only (the minimized one: the picture)
    bool texts = player->isFullscreen();
    title->setVisibility(texts ? Visibility::Visible : Visibility::Hidden);
    format->setVisibility(texts ? Visibility::Visible : Visibility::Hidden);
    bool osd = player->getOSD()->isVisible();
    bar->setVisibility(texts && !osd ? Visibility::Visible : Visibility::Hidden);
    if (!texts) {
        return;
    }

    if (playerTitle != player->getTitle() || textClock.getElapsedTime().asSeconds() >= 1) {
        playerTitle = player->getTitle();
        textClock.restart();
        updateTexts();
    }

    if (!osd) {
        double position = mpv->getTime();
        double duration = (double) mpv->getDuration();
        float done = duration > 0 ? (float) std::min(1.0, std::max(0.0, position) / duration) : 0;
        progress->setProgress(done);
        progress->setFgColor(COLOR_ACCENT);
        float cache = 0;
        if (duration > 0) {
            double end = std::max(0.0, position) + std::max(0.0, mpv->getCacheDuration());
            cache = (float) std::min(1.0, end / duration);
        }
        buffered->setSize(cache * progress->getSize().x, buffered->getSize().y);
        knob->setPosition(done * progress->getSize().x, progress->getSize().y / 2);
        elapsed->setString(clock(position));
        double speed = std::max(0.01, mpv->getSpeed());
        remaining->setString("-" + clock(std::max(0.0, (duration - position) / speed)));
    }
}
