//
// Right side panel of the filer: poster and information about the selection.
//

#include <algorithm>
#include <cstdio>
#include <sstream>

#include "main.h"
#include "io.h"
#include "utility.h"
#include "net_http.h"
#include "torrserve.h"
#include "preview_view.h"
#include "filer.h"
#include "i18n.h"
#include "encodings.h"
#include "image.h"
#include "history.h"
#include "input_map.h"
#include "player.h"

using namespace c2d;

#define POSTER_W 300
#define POSTER_H 450
#define MARGIN 24

PreviewView::PreviewView(Main *m, const c2d::FloatRect &rect) : Rectangle(rect) {
    main = m;

    // below the status bar
    top = (32 + MARGIN) * main->getScaling().y;
    float margin = MARGIN * main->getScaling().x;
    Vector2f posterSize = main->getScaled(POSTER_W, POSTER_H);

    posterFrame = new RectangleShape(posterSize);
    posterFrame->setPosition(margin, top);
    posterFrame->setFillColor(Color::Transparent);
    posterFrame->setOutlineColor(COLOR_ACCENT);
    posterFrame->setOutlineThickness(2);
    posterFrame->setVisibility(Visibility::Hidden);
    PreviewView::add(posterFrame);

    // the poster sprite never points to a deleted texture
    placeholder = new C2DTexture(Vector2i{4, 4});
    poster = new Sprite();
    poster->setPosition(margin, top);
    poster->setVisibility(Visibility::Hidden);
    poster->add(new TweenAlpha(0, 255, 0.3f));
    PreviewView::add(poster);

    float textWidth = rect.width - margin * 2;

    title = new C2DText("", main->getFontSize(Main::FontSize::Big), main->getFont());
    title->setFillColor(COLOR_FONT);
    title->setOverflow(Text::NewLine);
    title->setSizeMax(textWidth, main->getFontSize(Main::FontSize::Big) * 3.5f);
    PreviewView::add(title);

    details = new C2DText("", main->getFontSize(Main::FontSize::Small), main->getFont());
    details->setFillColor(COLOR_FONT);
    details->setOverflow(Text::NewLine);
    details->setSizeMax(textWidth, rect.height - top);
    PreviewView::add(details);

    // screen bottom (under the files list height): the progress reset button, a separator line above
    hint = new C2DText("", main->getFontSize(Main::FontSize::Small), main->getFont());
    hint->setFillColor(COLOR_GRAY_LIGHT);
    hint->setOrigin(Origin::BottomLeft);
    hint->setPosition(margin, main->getSize().y - margin);
    hint->setVisibility(Visibility::Hidden);
    PreviewView::add(hint);

    separator = new RectangleShape({textWidth, std::max(1.0f, main->getScaling().y)});
    separator->setFillColor(Color(255, 255, 255, 50));
    separator->setVisibility(Visibility::Hidden);
    PreviewView::add(separator);

    hidePoster();
}

std::string PreviewView::posterCachePath(const std::string &url) const {
    return main->getIo()->getDataPath() + "cache/posters/" + pplay::Utility::md5hash(url);
}

void PreviewView::hidePoster() {
    poster->setVisibility(Visibility::Hidden);
    posterFrame->setVisibility(Visibility::Hidden);
    if (posterTexture != nullptr) {
        // Shape::setTexture(nullptr) is a no-op
        delete (posterTexture);
        posterTexture = nullptr;
        poster->setTexture(placeholder, true);
    }
    shownPoster.clear();
    layout();
}

void PreviewView::layout() {
    float margin = MARGIN * main->getScaling().x;
    float y = top;
    float gap = margin;
    if (!shownPoster.empty()) {
        y += poster->getSize().y + margin;
        gap = margin / 2;
    }
    title->setPosition(margin, y);
    // the bounds are not updated for an empty string
    float titleHeight = title->getString().empty() ? 0 : title->getLocalBounds().height;
    if (titleHeight > 0) {
        y += titleHeight + gap;
    }
    details->setPosition(margin, y);
    float bottom = getSize().y - margin;
    if (hint->isVisible()) {
        // fixed at the screen bottom: hint, separator, then the details above
        float lineHeight = (float) main->getFontSize(Main::FontSize::Small);
        float separatorY = hint->getPosition().y - lineHeight * 1.2f - margin * 0.6f;
        separator->setPosition(margin, separatorY);
        bottom = separatorY - margin * 0.6f;
    }
    separator->setVisibility(hint->isVisible() ? Visibility::Visible : Visibility::Hidden);
    // the last line descenders are drawn under its maximum height
    float descent = (float) main->getFontSize(Main::FontSize::Small) * 0.35f;
    details->setSizeMax(getSize().x - margin * 2, std::max(0.0f, bottom - y - descent));
}

void PreviewView::showPoster(const std::string &url) {
    std::string path = posterCachePath(url);
    std::string error;
    Vector2f shown = main->getScaled(POSTER_W, POSTER_H);
    c2d::Texture *texture = pplay::Image::load(path, {(int) shown.x * 2, (int) shown.y * 2}, &error);
    if (texture == nullptr) {
        pplay::Utility::deleteFile(path);
        failedPoster = url;
        setPosterStatus(pplay::tr("Poster") + ": " + pplay::tr("unsupported image") + " (" + error + ")");
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Preview: poster decode failed: " + url + ": " + error);
        return;
    }
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Preview: poster " + url + " "
                                                        + std::to_string(texture->getTextureSize().x) + "x"
                                                        + std::to_string(texture->getTextureSize().y));
    setPosterStatus("");

    if (posterTexture != nullptr) {
        poster->setTexture(placeholder, true);
        delete (posterTexture);
    }
    posterTexture = texture;
    poster->setTexture(posterTexture, true);

    Vector2i texSize = posterTexture->getTextureSize();
    float maxW = main->getScaled(POSTER_W, 0).x;
    float maxH = getSize().y / 2 - top;
    float scale = std::min(maxW / (float) texSize.x, maxH / (float) texSize.y);
    Vector2f size = {(float) texSize.x * scale, (float) texSize.y * scale};
    poster->setSize(size);
    posterFrame->setSize(size);
    poster->setVisibility(Visibility::Visible, true);
    posterFrame->setVisibility(Visibility::Visible);
    shownPoster = url;
    layout();
}

static const char *imageFormat(const std::string &data) {
    auto u = [&data](size_t i) { return (unsigned char) data[i]; };
    if (data.size() < 12) return nullptr;
    if (u(0) == 0xFF && u(1) == 0xD8) return "jpeg";
    if (u(0) == 0x89 && data.compare(1, 3, "PNG") == 0) return "png";
    if (data.compare(0, 3, "GIF") == 0) return "gif";
    if (data.compare(0, 2, "BM") == 0) return "bmp";
    if (data.compare(0, 4, "RIFF") == 0 && data.compare(8, 4, "WEBP") == 0) return "webp";
    if (data.compare(4, 4, "ftyp") == 0) return "avif";  // avif / heif
    return nullptr;
}

int PreviewView::downloadThread(void *data) {
    auto *self = (PreviewView *) data;
    pplay::Http::Request request;
    request.url = self->downloadUrl;
    request.timeout = 15;
    request.maxSize = 8 * 1024 * 1024;
    request.headers = {"Accept: image/jpeg,image/png;q=0.9,image/*;q=0.5,*/*;q=0.1",
                       "User-Agent: Mozilla/5.0 (PlayStation; pPlay) AppleWebKit/605.1.15 (KHTML, like Gecko)"};
    pplay::Http::Response response = pplay::Http::perform(request);

    std::string status;
    bool ok = false;
    if (!response.ok()) {
        status = response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error;
    } else {
        const char *format = imageFormat(response.body);
        if (format == nullptr) {
            status = "not an image";
        } else {
            FILE *f = fopen(self->downloadPath.c_str(), "wb");
            ok = f != nullptr && fwrite(response.body.data(), 1, response.body.size(), f) == response.body.size();
            if (f) fclose(f);
            if (!ok) {
                pplay::Utility::deleteFile(self->downloadPath);
                status = "can't write cache file";
            }
        }
    }
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
                        "Preview: poster download " + self->downloadUrl + " status=" + std::to_string(response.status)
                        + " size=" + std::to_string(response.body.size()) + (ok ? " ok" : " failed: " + status));
    self->downloadStatus = status;
    self->downloadOk = ok;
    self->downloadDone = true;
    return 0;
}

void PreviewView::setPosterStatus(const std::string &status) {
    posterStatus = status;
    details->setString(posterStatus.empty() ? infoText : infoText + "\n" + posterStatus);
}

static std::string resolutionName(int width, int height) {
    if (width >= 3800) return "2160p";
    if (width >= 1900) return "1080p";
    if (width >= 1260) return "720p";
    return std::to_string(height) + "p";
}

void PreviewView::setFile(const MediaFile &file) {
    std::ostringstream info;
    std::string name = file.name;
    wantedPoster.clear();

    pplay::TorrServe::Info ts;
    if (Filer::isPlaceholder(file)) {
        name.clear();
    } else if (file.name == "..") {
        name = pplay::tr("BACK");
        info << pplay::tr("Parent directory");
    } else if (pplay::TorrServe::getInfo(file.path, ts)) {
        name = ts.title;
        wantedPoster = ts.poster;
        if (!ts.category.empty()) {
            info << pplay::tr("Category") << ": " << encoding::fix(ts.category) << "\n";
        }
        if (ts.torrentSize > 0) {
            info << pplay::tr("Torrent size") << ": " << pplay::Utility::formatSize((size_t) ts.torrentSize) << "\n";
        }
        info << pplay::tr("Files") << ": " << ts.fileCount;
        if (ts.filesSize > 0) {
            info << " (" << pplay::Utility::formatSize((size_t) ts.filesSize) << ")";
        }
        info << "\n";
        if (ts.fileIndex >= 0) {
            info << "\n" << encoding::fix(ts.fileName) << "\n";
            if (ts.fileSize > 0) {
                info << pplay::tr("Size") << ": " << pplay::Utility::formatSize((size_t) ts.fileSize) << "\n";
            }
        }
    } else if (file.type == Io::Type::Directory) {
        info << pplay::tr("Directory");
    } else {
        if (file.size > 0) {
            info << pplay::tr("Size") << ": " << pplay::Utility::formatSize(file.size) << "\n";
        }
    }

    bool media = file.type == Io::Type::File && !Filer::isPlaceholder(file);
    if (media) {
        time_t last = pplay::History::lastPlayed(Player::keyOf(file.path));
        if (last > 0) {
            struct tm tm{};
            localtime_r(&last, &tm);
            char date[32];
            strftime(date, sizeof(date), "%d.%m.%Y %H:%M", &tm);
            info << pplay::tr("Last played") << ": " << date << "\n";
        }
    }
    // watch later / TorrServer viewed state: Y resets it (filer)
    bool resettable = media && (pplay::Utility::isWatchLaterExist(pplay::TorrServe::toStreamUrl(file.path))
                                || pplay::TorrServe::isFileViewed(file.path));
    hint->setString(pplay::InputMap::buttonName(Input::Button::Y) + " " + pplay::tr("Reset progress"));
    hint->setVisibility(resettable ? Visibility::Visible : Visibility::Hidden);

    const MediaInfo &mi = file.mediaInfo;
    if (file.type == Io::Type::File && (mi.duration > 0 || !mi.videos.empty() || !mi.audios.empty())) {
        if (mi.duration > 0) {
            info << pplay::tr("Duration") << ": " << pplay::Utility::formatTime((double) mi.duration) << "\n";
        }
        if (!mi.videos.empty()) {
            const auto &v = mi.videos[0];
            info << pplay::tr("Video") << ": " << c2d::Utility::toUpper(v.codec);
            if (v.width > 0 && v.height > 0) {
                info << ", " << resolutionName(v.width, v.height) << " (" << v.width << "x" << v.height << ")";
            }
            info << "\n";
        }
        if (!mi.audios.empty()) {
            info << pplay::tr("Audio") << ": ";
            for (size_t i = 0; i < mi.audios.size() && i < 4; i++) {
                if (i > 0) info << ", ";
                info << c2d::Utility::toUpper(mi.audios[i].codec);
                if (mi.audios[i].language != "N/A" && !mi.audios[i].language.empty()) {
                    info << " " << mi.audios[i].language;
                }
            }
            if (mi.audios.size() > 4) info << "...";
            info << "\n";
        }
        if (!mi.subtitles.empty()) {
            info << pplay::tr("Subtitles") << ": " << mi.subtitles.size() << "\n";
        }
    }

    title->setString(encoding::fix(name));
    infoText = info.str();
    posterStatus.clear();
    details->setString(infoText);

    if (wantedPoster != shownPoster) {
        hidePoster();
    } else {
        layout();
    }
    selectionClock.restart();
}

void PreviewView::onUpdate() {
    posterFrame->setOutlineColor(COLOR_ACCENT);

    if (thread != nullptr && downloadDone) {
        thread->join();
        delete (thread);
        thread = nullptr;
        if (!downloadOk) {
            failedPoster = downloadUrl;
            if (downloadUrl == wantedPoster) {
                setPosterStatus(pplay::tr("Poster") + ": " + downloadStatus);
            }
        }
    }

    if (!wantedPoster.empty() && wantedPoster != shownPoster && wantedPoster != failedPoster
        && thread == nullptr && selectionClock.getElapsedTime().asMilliseconds() > 250) {
        std::string path = posterCachePath(wantedPoster);
        if (main->getIo()->exist(path)) {
            showPoster(wantedPoster);
        } else {
            std::string dir = main->getIo()->getDataPath() + "cache/posters";
            if (!main->getIo()->exist(dir)) {
                main->getIo()->create(dir);
            }
            downloadUrl = wantedPoster;
            downloadPath = path;
            downloadDone = false;
            downloadOk = false;
            thread = new C2DThread(downloadThread, this);
        }
    }

    Rectangle::onUpdate();
}

PreviewView::~PreviewView() {
    if (thread != nullptr) {
        thread->join();
        delete (thread);
    }
    if (posterTexture != nullptr) {
        delete (posterTexture);
    }
    delete (placeholder);
}
