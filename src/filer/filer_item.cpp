//
// Created by cpasjuste on 03/12/18.
//

#include <cmath>

#include "cross2d/c2d.h"
#include "main.h"
#include "filer_item.h"
#include "utility.h"
#include "torrserve.h"
#include "encodings.h"
#include "i18n.h"
#include "filer.h"
#include "clip_rect.h"
#include "skin.h"
#include "external_tracks.h"

using namespace c2d;

namespace {
    std::string iconName(const MediaFile &file) {
        if (file.type == Io::Type::Directory) {
            return "file_folder.png";
        }
        if (pplay::ExternalTracks::isAudio(file.name)) {
            return "file_audio.png";
        }
        if (pplay::ExternalTracks::isSubtitle(file.name)) {
            return "subtitles.png";
        }
        if (Utility::endsWith(file.name, ".m3u", false) || Utility::endsWith(file.name, ".m3u8", false)) {
            return "playlist.png";
        }
        return pplay::Utility::isMedia(file) ? "file_video.png" : "file_other.png";
    }
}

FilerItem::FilerItem(Main *m, const c2d::FloatRect &rect, const MediaFile &f) : Rectangle(rect) {

    main = m;
    file = f;

    textTitle = new Text(file.name.empty() ? "AjIilp" : file.name, main->getFontSize(Main::FontSize::Bigger),
                         main->getFont());
    textTitle->setOrigin(Origin::Left);

    iconSize = std::round(FilerItem::getSize().y * 0.7f);
    icon = new Sprite();
    icon->setOrigin(Origin::Left);
    icon->setPosition(16 * main->getScaling().x, FilerItem::getSize().y / 2);
    icon->setVisibility(Visibility::Hidden);
    FilerItem::add(icon);

    // clipped on pixels (not characters), the end faded
    float textX = 16 * main->getScaling().x + iconSize + 12 * main->getScaling().x;
    float textWidth = FilerItem::getSize().x - textX - 16 * main->getScaling().x;
    clip = new pplay::ClipRect({textX, 0, textWidth, FilerItem::getSize().y});
    textTitle->setPosition(0, FilerItem::getSize().y / 2);
    clip->add(textTitle);
    FilerItem::add(clip);
    float fadeWidth = 48 * main->getScaling().x;
    fade = new pplay::FadeEdge({textX + textWidth - fadeWidth, 0, fadeWidth, FilerItem::getSize().y});
    fade->setBackground(COLOR_BG);
    fade->setVisibility(Visibility::Hidden);
    FilerItem::add(fade);

    float line = std::max(2.0f, std::round(3 * main->getScaling().y));
    watchedTrack = new RectangleShape({textX, FilerItem::getSize().y - line - 2 * main->getScaling().y, textWidth, line});
    watchedTrack->setFillColor(Color(255, 255, 255, 30));
    watchedTrack->setVisibility(Visibility::Hidden);
    FilerItem::add(watchedTrack);
    watched = new RectangleShape({0, 0, 1, line});
    watchedTrack->add(watched);
}

void FilerItem::setIcon(const std::string &name) {
    if (name.empty()) {
        icon->setVisibility(Visibility::Hidden);
        return;
    }
    Texture *texture = pplay::Skin::texture(name);
    if (texture == nullptr) {
        icon->setVisibility(Visibility::Hidden);
        return;
    }
    icon->setTexture(texture, true);
    float scale = iconSize / (float) texture->getTextureSize().x;
    icon->setScale(scale, scale);
    icon->setVisibility(Visibility::Visible);
}

void FilerItem::setColor(const c2d::Color &color) {
    uint8_t alpha = textTitle->getAlpha();
    textTitle->setFillColor(color);
    textTitle->setAlpha(alpha);
    // the icons quieter than the names, the accent for the playing file only
    bool playing = file.type == Io::Type::File && main->getPlayer() != nullptr && main->getPlayer()->isPlaying(file.path);
    Color iconColor = playing ? COLOR_ACCENT : color == COLOR_FONT ? Color(140, 148, 158, 255) : color;
    icon->setFillColor(iconColor);
    icon->setAlpha(alpha);
}

void FilerItem::setFile(const MediaFile &f) {

    this->file = f;
    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "FilerItem::setFile name=" + file.name + " path=" + file.path
                                                                 + " type=" + std::to_string((int) file.type));

    if (Filer::isPlaceholder(file)) {
        textTitle->setString(pplay::tr("<Empty>"));
        fade->setVisibility(Visibility::Hidden);
        watchedTrack->setVisibility(Visibility::Hidden);
        setIcon("");
        setColor(COLOR_GRAY);
        return;
    }
    if (file.name == "..") {
        textTitle->setString(pplay::tr("BACK"));
        setIcon("file_back.png");
    } else {
        const bool playing =
                file.type == Io::Type::File && main->getPlayer() != nullptr && main->getPlayer()->isPlaying(file.path);
        textTitle->setString(encoding::fix(file.name));
        setIcon(playing ? "file_playing.png" : iconName(file));
    }
    setColor(color());
    fade->setVisibility(textTitle->getLocalBounds().width > clip->getSize().x ? Visibility::Visible
                                                                              : Visibility::Hidden);
    updateWatched();
}

void FilerItem::updateWatched() {
    float part = 0;
    if (file.type == Io::Type::File && file.mediaInfo.duration > 0) {
        double position = pplay::Utility::watchLaterPosition(pplay::TorrServe::toStreamUrl(file.path));
        if (position <= 0 && main->getConfig()->getOption(OPT_TS_PROGRESS)->getInteger() != 0) {
            position = pplay::TorrServe::fileProgress(file.path);
        }
        part = (float) std::min(1.0, position / (double) file.mediaInfo.duration);
    }
    if (part < 0.01f) {
        watchedTrack->setVisibility(Visibility::Hidden);
        return;
    }
    watched->setSize(std::round(watchedTrack->getSize().x * part), watchedTrack->getSize().y);
    watched->setFillColor(COLOR_ACCENT);
    watchedTrack->setVisibility(Visibility::Visible);
}

c2d::Color FilerItem::color() {
    if (file.type == Io::Type::Directory) {
        return COLOR_FONT;
    }
    if (file.type == Io::Type::File) {
        const std::string streamUrl = pplay::TorrServe::toStreamUrl(file.path);
        bool watched = pplay::Utility::isWatchLaterExist(streamUrl) || pplay::TorrServe::isFileViewed(file.path);
        return watched ? COLOR_VIEWED : COLOR_FONT;
    }
    return COLOR_FONT;
}

void FilerItem::onUpdate() {
    const bool isFullscreen = main->getPlayer() && main->getPlayer()->isFullscreen();
    const bool fullscreenExited = lastFullscreen && !isFullscreen;
    lastFullscreen = isFullscreen;
    // watched state changes (playback elsewhere), the accent color
    if (file.type == Io::Type::Directory
        || (file.type == Io::Type::File && !isFullscreen
            && (updateClock.getElapsedTime().asSeconds() > 30.0f || fullscreenExited))) {
        setColor(color());
        if (file.type == Io::Type::File) {
            updateWatched();
        }
        updateClock.restart();
    }
    // what is behind the fade: the list, the selection highlight over it
    float x = (fade->getPosition().x + fade->getSize().x / 2) / std::max(1.0f, highlightWidth);
    fade->setBackground(selected ? Highlight::seenThrough(COLOR_BG, x, Highlight::CursorPosition::Left) : COLOR_BG);
    // the filer fade in sets all the alphas: the track keeps its own
    uint8_t alpha = textTitle->getAlpha();
    watchedTrack->setFillColor(Color(255, 255, 255, (uint8_t) (30 * alpha / 255)));
    watched->setFillColor(Color(COLOR_ACCENT.r, COLOR_ACCENT.g, COLOR_ACCENT.b, alpha));
    Rectangle::onUpdate();
}
