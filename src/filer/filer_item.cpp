//
// Created by cpasjuste on 03/12/18.
//

#include <cmath>
#include <cstdio>

#include "cross2d/c2d.h"
#include "main.h"
#include "filer_item.h"
#include "playlists.h"
#include "folder_marks.h"
#include "utility.h"
#include "torrserve.h"
#include "encodings.h"
#include "i18n.h"
#include "filer.h"
#include "clip_rect.h"
#include "skin.h"
#include "external_tracks.h"
#include "saved.h"

using namespace c2d;

namespace {
    std::string iconName(const MediaFile &file) {
        // opened as directories
        if (pplay::Playlists::isPlaylist(file.path)) {
            return "playlist.png";
        }
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

    savedMark = new Sprite();
    savedMark->setOrigin(Origin::Right);
    savedMark->setVisibility(Visibility::Hidden);
    FilerItem::add(savedMark);

    // clipped on pixels (not characters), the end faded
    textX = 16 * main->getScaling().x + iconSize + 12 * main->getScaling().x;
    // the scrollbar on the right
    textWidth = FilerItem::getSize().x - textX - 24 * main->getScaling().x;
    info = new Text("", main->getFontSize(Main::FontSize::Small), main->getFont());
    info->setOrigin(Origin::Right);
    info->setPosition(textX + textWidth, FilerItem::getSize().y / 2);
    info->setFillColor(Color(140, 148, 158, 255));
    FilerItem::add(info);
    check = new Text("", main->getFontSize(Main::FontSize::Small), main->getFont());
    check->setOrigin(Origin::Right);
    // both kept: a second mark overlapping, as in the messengers
    check2 = new Text("✓", main->getFontSize(Main::FontSize::Small), main->getFont());
    check2->setOrigin(Origin::Right);
    FilerItem::add(check2);
    FilerItem::add(check);
    clip = new pplay::ClipRect({textX, 0, textWidth, FilerItem::getSize().y});
    textTitle->setPosition(0, FilerItem::getSize().y / 2);
    clip->add(textTitle);
    FilerItem::add(clip);
    float fadeWidth = 48 * main->getScaling().x;
    fade = new pplay::FadeEdge({textX + textWidth - fadeWidth, 0, fadeWidth, FilerItem::getSize().y});
    fade->setBackground(COLOR_SURFACE);
    fade->setVisibility(Visibility::Hidden);
    FilerItem::add(fade);

    float line = std::max(2.0f, std::round(3 * main->getScaling().y));
    // right under the name, as long as the row up to the time; inside the row (its highlight)
    float lineY = std::round(FilerItem::getSize().y / 2 + (float) textTitle->getCharacterSize() * 0.55f);
    lineY = std::min(lineY, std::floor(FilerItem::getSize().y - line));
    // the watched part only (no track under it)
    watchedWidth = textWidth;
    watchedTrack = new RectangleShape({textX, lineY, textWidth, line});
    watchedTrack->setVisibility(Visibility::Hidden);
    FilerItem::add(watchedTrack);
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
    viewedGeneration = pplay::TorrServe::viewedGeneration();
    savedGeneration = pplay::Saved::generation();
    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "FilerItem::setFile name=" + file.name + " path=" + file.path
                                                                 + " type=" + std::to_string((int) file.type));

    if (Filer::isPlaceholder(file)) {
        savedMark->setVisibility(Visibility::Hidden);
        textTitle->setString(pplay::tr("<Empty>"));
        info->setString("");
        check->setString("");
        check2->setVisibility(Visibility::Hidden);
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
    // the duration on the right, the name up to it
    long duration = file.type == Io::Type::File ? file.mediaInfo.duration : 0;
    char text[32] = "";
    if (duration >= 3600) {
        snprintf(text, sizeof(text), "%ld:%02ld:%02ld", duration / 3600, duration / 60 % 60, duration % 60);
    } else if (duration > 0) {
        snprintf(text, sizeof(text), "%ld:%02ld", duration / 60, duration % 60);
    }
    // a folder: "›"
    info->setString(file.type == Io::Type::Directory && file.name != ".." ? "›" : text);
    std::string marks;
    bool both = false;
    if (file.type == Io::Type::File) {
        bool local = pplay::Utility::isWatchLaterExist(pplay::TorrServe::toStreamUrl(file.path));
        bool server = pplay::TorrServe::isFileViewed(file.path);
        marks = local || server ? "✓" : "";
        both = local && server;
    }
    // a directory: all watched (the accent), or some started (gray)
    bool partial = false;
    if (file.type == Io::Type::Directory && file.name != ".." && !Filer::isPlaceholder(file)) {
        pplay::FolderMarks::State state = pplay::FolderMarks::cached(file);
        // some watched or started: gray
        if (state.ready && state.files > 0 && (state.unwatched < state.files || state.marked > 0)) {
            marks = "✓";
            partial = state.unwatched > 0;
        }
    }
    check->setString(marks);
    check->setFillColor(partial ? Color(140, 148, 158, 255) : COLOR_ACCENT);
    // from the right: the time or the arrow, the watched marks, the saved mark (the ones shown)
    float x = textX + textWidth;
    info->setPosition(x, getSize().y / 2);
    x -= info->getString().empty() ? 0 : info->getLocalBounds().width + 16 * main->getScaling().x;
    check->setPosition(x, getSize().y / 2);
    float shift = std::round(check->getLocalBounds().width * 0.45f);
    check2->setVisibility(both ? Visibility::Visible : Visibility::Hidden);
    check2->setFillColor(COLOR_ACCENT);
    check2->setPosition(x - shift, getSize().y / 2);
    x -= marks.empty() ? 0 : check->getLocalBounds().width + (both ? shift : 0) + 10 * main->getScaling().x;
    x -= updateSavedMark(x);
    float width = x - textX;
    if (width != clip->getSize().x) {
        clip->setSize(width, clip->getSize().y);
        fade->setPosition(textX + width - fade->getSize().x, fade->getPosition().y);
    }
    fade->setVisibility(textTitle->getLocalBounds().width > clip->getSize().x ? Visibility::Visible
                                                                              : Visibility::Hidden);
    updateWatched();
}

float FilerItem::updateSavedMark(float right) {
    pplay::Saved::State state;
    bool network = file.name != ".." && pplay::Saved::canSave(file);
    if (network) {
        state = pplay::Saved::stateOf(file.path);
    }
    float size = std::round(getSize().y * 0.45f);
    std::string name;
    if (state.pending > 0) {
        char ring[32];
        snprintf(ring, sizeof(ring), "save_ring%d.png", 1 + std::min(7, (int) (state.progress * 8)));
        name = ring;
    } else if (state.saved > 0) {
        name = "saved.png";
    }
    Texture *texture = name.empty() ? nullptr : pplay::Skin::texture(name);
    if (texture == nullptr) {
        savedMark->setVisibility(Visibility::Hidden);
        return 0;
    }
    savedMark->setTexture(texture, true);
    float scale = size / (float) texture->getTextureSize().x;
    savedMark->setScale(scale, scale);
    savedMark->setPosition(right, getSize().y / 2);
    // quiet, as the icons; the accent while saving
    savedMark->setFillColor(state.pending > 0 ? COLOR_ACCENT : Color(140, 148, 158, 255));
    savedMark->setVisibility(Visibility::Visible);
    return size + 10 * main->getScaling().x;
}

void FilerItem::updateWatched() {
    float part = 0;
    if (file.type == Io::Type::File && file.mediaInfo.duration > 0) {
        double position = pplay::TorrServe::keptPosition(file.path, main->getConfig()->getOption(OPT_TS_PROGRESS)->getInteger());
        part = (float) std::min(1.0, position / (double) file.mediaInfo.duration);
    }
    if (part < 0.01f) {
        watchedTrack->setVisibility(Visibility::Hidden);
        return;
    }
    watchedTrack->setSize(std::max(1.0f, std::round(watchedWidth * part)), watchedTrack->getSize().y);
    watchedTrack->setFillColor(COLOR_ACCENT);
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

void FilerItem::onDraw(Transform &transform, bool draw) {
    // what is behind the fade: the list, the selection highlight over the part it covers now;
    // here: the highlight (drawn before the rows) has moved for this frame already
    if (draw && fade->isVisible()) {
        float x = (fade->getPosition().x + fade->getSize().x / 2) / std::max(1.0f, getSize().x);
        float rowTop = getPosition().y, height = getSize().y;
        float lightTop = highlight != nullptr && highlight->isVisible() ? highlight->getPosition().y : -1e9f;
        float from = std::max(rowTop, lightTop), to = std::min(rowTop + height, lightTop + height);
        if (to - from < 0.5f) {
            fade->setBackground(COLOR_SURFACE);
        } else {
            Color lit = highlight->seenThrough(COLOR_SURFACE, x);
            if (from <= rowTop + 0.5f) {
                fade->setBackgrounds(lit, COLOR_SURFACE, (to - rowTop) / height);
            } else {
                fade->setBackgrounds(COLOR_SURFACE, lit, (from - rowTop) / height);
            }
        }
    }
    Rectangle::onDraw(transform, draw);
}

void FilerItem::onUpdate() {
    const bool isFullscreen = main->getPlayer() && main->getPlayer()->isFullscreen();
    const bool fullscreenExited = lastFullscreen && !isFullscreen;
    lastFullscreen = isFullscreen;
    // the TorrServer marks came (asked in the background)
    if (file.type == Io::Type::File && !isFullscreen && pplay::TorrServe::viewedGeneration() != viewedGeneration) {
        setFile(file);
    } else if (!isFullscreen && pplay::Saved::generation() != savedGeneration) {
        setFile(file);
    } else if (file.type == Io::Type::Directory && !isFullscreen
               && (pplay::FolderMarks::rowsGeneration() != folderGeneration
                   || folderClock.getElapsedTime().asSeconds() > 30.0f)) {
        folderGeneration = pplay::FolderMarks::rowsGeneration();
        folderClock.restart();
        setFile(file);
    }
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
    // the filer fade in sets all the alphas: the line follows the text
    uint8_t alpha = textTitle->getAlpha();
    watchedTrack->setFillColor(Color(COLOR_ACCENT.r, COLOR_ACCENT.g, COLOR_ACCENT.b, alpha));
    Rectangle::onUpdate();
}
