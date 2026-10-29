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
    icon->setFillColor(color);
    icon->setAlpha(alpha);
}

void FilerItem::setFile(const MediaFile &f) {

    this->file = f;
    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "FilerItem::setFile name=" + file.name + " path=" + file.path
                                                                 + " type=" + std::to_string((int) file.type));

    if (Filer::isPlaceholder(file)) {
        textTitle->setString(pplay::tr("<Empty>"));
        fade->setVisibility(Visibility::Hidden);
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
}

c2d::Color FilerItem::color() {
    if (file.type == Io::Type::Directory) {
        return COLOR_ACCENT;
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
        updateClock.restart();
    }
    Rectangle::onUpdate();
}
