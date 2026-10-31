//
// Filer details panel.
//

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "main.h"
#include "clip_rect.h"
#include "filer.h"
#include "input_map.h"
#include "key_hints.h"
#include "player.h"
#include "poster_loader.h"
#include "details_info.h"
#include "details_view.h"
#include "text_wrap.h"
#include "torrserve.h"
#include "utility.h"
#include "torrserve.h"

using namespace c2d;

#define MARGIN 24
// details right of the poster up to this part of the panel width
#define POSTER_SIDE 0.4f
// not focused poster height, of the panel width (a 2:3 poster is on the side)
#define POSTER_HEIGHT 0.55f

DetailsView::DetailsView(Main *m, const c2d::FloatRect &rect) : Rectangle(rect), main(m) {
    float margin = MARGIN * main->getScaling().x;
    float bar = std::max(2.0f, 3 * main->getScaling().x);

    loader = new PosterLoader(main);
    // the poster sprite never points to a deleted texture
    placeholder = new C2DTexture(Vector2i{4, 4});

    view = new pplay::ClipRect({0, 0, rect.width, 1});
    DetailsView::add(view);
    content = new Rectangle(FloatRect{0, 0, rect.width, 1});
    view->add(content);

    title = new C2DText("", main->getFontSize(Main::FontSize::Big), main->getFont());
    title->setFillColor(COLOR_FONT);
    content->add(title);

    posterFrame = new RectangleShape(Vector2f{1, 1});
    posterFrame->setFillColor(Color::Transparent);
    posterFrame->setOutlineThickness(2);
    posterFrame->setVisibility(Visibility::Hidden);
    content->add(posterFrame);

    poster = new Sprite();
    poster->setVisibility(Visibility::Hidden);
    poster->add(new TweenAlpha(0, 255, 0.3f));
    content->add(poster);

    detailsText = new C2DText("", main->getFontSize(Main::FontSize::Small), main->getFont());
    detailsText->setFillColor(COLOR_FONT);
    content->add(detailsText);

    scrollBar = new RectangleShape(Vector2f{bar, 1});
    scrollBar->setVisibility(Visibility::Hidden);
    DetailsView::add(scrollBar);

    focusBar = new RectangleShape(Vector2f{bar, 1});
    focusBar->setVisibility(Visibility::Hidden);
    DetailsView::add(focusBar);

    separator = new RectangleShape(Vector2f{rect.width - margin * 2, std::max(1.0f, main->getScaling().y)});
    separator->setFillColor(Color(255, 255, 255, 50));
    DetailsView::add(separator);

    hints = new KeyHints(main, rect.width - margin * 2);
    DetailsView::add(hints);
}

DetailsView::~DetailsView() {
    delete (loader);
    delete (posterTexture);
    delete (placeholder);
}

bool DetailsView::isResettable(const MediaFile &file) {
    return file.type == Io::Type::File && !Filer::isPlaceholder(file)
           && (pplay::Utility::isWatchLaterExist(pplay::TorrServe::toStreamUrl(file.path))
               || pplay::TorrServe::isFileViewed(file.path));
}

void DetailsView::setFile(const MediaFile &f) {
    if (f.path != file.path || f.name != file.name) {
        scroll = scrollShown = 0;
        posterError.clear();
    }
    file = f;
    pplay::DetailsInfo info = pplay::DetailsInfo::of(file);
    heading = info.title;
    details = info.details;
    // the file own cover, the directory one, the torrent one: the next one if it fails
    posterUrls.clear();
    for (auto &url: {info.cover, file.type == Io::Type::File ? folderCover : "", info.poster}) {
        if (!url.empty()) {
            posterUrls.push_back(url);
        }
    }
    std::string wanted = nextPoster();
    if (wanted != shownPoster) {
        hidePoster();
    }
    posterUrl = wanted;
    loader->request(posterUrl == shownPoster ? "" : posterUrl);
    updateHints();
    layout();
}

std::string DetailsView::nextPoster() const {
    for (auto &url: posterUrls) {
        if (failedPosters.count(url) == 0) {
            return url;
        }
    }
    return "";
}

void DetailsView::setFocused(bool value) {
    if (focused == value) {
        return;
    }
    focused = value;
    button = -1;
    scroll = 0;
    hints->setButtons(focused);
    focusBar->setVisibility(focused ? Visibility::Visible : Visibility::Hidden);
    updateHints();
    layout();
}

void DetailsView::updateHints() {
    bool empty = Filer::isPlaceholder(file);
    std::string open = file.type == Io::Type::Directory ? "Open" : "Play";
    // a kept position: where it goes on
    if (file.type == Io::Type::File) {
        double position = pplay::Utility::watchLaterPosition(pplay::TorrServe::toStreamUrl(file.path));
        if (position <= 0 && main->getConfig()->getOption(OPT_TS_PROGRESS)->getInteger() != 0) {
            position = pplay::TorrServe::fileProgress(file.path);
        }
        if (position >= 10) {
            int t = (int) position;
            char time[32];
            if (t >= 3600) {
                snprintf(time, sizeof(time), "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
            } else {
                snprintf(time, sizeof(time), "%d:%02d", t / 60, t % 60);
            }
            open = pplay::tr("Continue from") + " " + time;
        }
    }
    bool resettable = isResettable(file);

    std::vector<KeyHints::Hint> list;
    auto add = [&list](unsigned int keys, const std::string &action, Action id) {
        list.push_back({keys, action, (int) id});
    };
    // the action first, the navigation, the reset last
    if (focused) {
        if (!empty) {
            add(Input::A, open, Action::Open);
        }
        if (playing) {
            add(Input::X, "Player", Action::Player);
        }
        add(Input::B, "Back", Action::Files);
        if (resettable) {
            add(Input::Y, "Reset progress", Action::Reset);
        }
    } else if (picking) {
        add(Input::Up | Input::Down, "Select", Action::None);
        if (!empty) {
            add(Input::A, file.type == Io::Type::Directory ? "Open" : "Attach", Action::Open);
        }
        add(Input::B, "Back", Action::Parent);
        add(Input::X, "cancel", Action::None);
    } else {
        if (!empty) {
            add(Input::A, open, Action::Open);
        }
        add(Input::Right, "Details", Action::Panel);
        add(Input::Up | Input::Down, "Select", Action::None);
        add(Input::B, "Back", Action::Parent);
        add(Input::Left, "Menu", Action::Menu);
        if (playing) {
            add(Input::X, "Player", Action::Player);
        }
        if (resettable) {
            add(Input::Y, "Reset progress", Action::Reset);
        }
    }
    hints->setHints(list);
    if (button >= hints->count()) {
        button = hints->count() - 1;
    }
    hints->setFocus(button);
}

void DetailsView::showPoster(c2d::Texture *texture) {
    poster->setTexture(placeholder, true);
    delete (posterTexture);
    posterTexture = texture;
    poster->setTexture(posterTexture, true);
    poster->setVisibility(Visibility::Visible, true);
    posterFrame->setVisibility(Visibility::Visible);
    shownPoster = posterUrl;
    posterError.clear();
    layout();
}

void DetailsView::hidePoster() {
    poster->setVisibility(Visibility::Hidden);
    posterFrame->setVisibility(Visibility::Hidden);
    if (posterTexture != nullptr) {
        // Shape::setTexture(nullptr) is a no-op
        poster->setTexture(placeholder, true);
        delete (posterTexture);
        posterTexture = nullptr;
    }
    shownPoster.clear();
}

void DetailsView::layout() {
    Vector2f scaling = main->getScaling();
    float margin = MARGIN * scaling.x;
    // the clock is over the panel (the path line)
    float top = MARGIN * scaling.y;
    float width = getSize().x - margin * 2;
    float gap = margin / 2;

    // fixed bottom: the keys (above the minimized player), a separator
    // (screen heights, the panel is under the path line)
    float limit = main->getSize().y - margin - getPosition().y;
    if (playerShown) {
        limit = main->getSize().y * 0.75f - 16 - gap - getPosition().y;
    }
    hints->setPosition(margin, limit - hints->getHeight());
    float separatorY = hints->getPosition().y - margin * 0.6f;
    separator->setPosition(margin, separatorY);
    float viewHeight = std::max(1.0f, separatorY - margin * 0.6f - top);
    view->setPosition(0, top);
    view->setSize(getSize().x, viewHeight);
    focusBar->setPosition(0, top);
    focusBar->setSize(focusBar->getSize().x, limit - top);

    // title, the poster with the details right of it (narrow) or below
    float y = 0;
    title->setString(pplay::TextWrap::wrap(main->getFont(), title->getCharacterSize(), heading, width, false));
    title->setPosition(margin, y);
    if (!heading.empty()) {
        y += title->getLocalBounds().top + title->getLocalBounds().height + gap;
    }
    float x = margin, detailsWidth = width, detailsY = y, bottom = y;
    if (posterTexture != nullptr) {
        Vector2f tex = {(float) posterTexture->getTextureSize().x, (float) posterTexture->getTextureSize().y};
        // focused: the panel width at most; never upscaled
        float scale = std::min(1.0f, width / tex.x);
        if (!focused) {
            scale = std::min(scale, width * POSTER_HEIGHT / tex.y);
        }
        Vector2f size = {std::round(tex.x * scale), std::round(tex.y * scale)};
        poster->setSize(size);
        posterFrame->setSize(size);
        poster->setPosition(margin, y);
        posterFrame->setPosition(margin, y);
        if (size.x <= width * POSTER_SIDE) {
            x = margin + size.x + margin;
            detailsWidth = width - size.x - margin;
        } else {
            detailsY = y + size.y + gap;
        }
        bottom = y + size.y;
    }
    std::string text = details;
    if (!posterError.empty()) {
        text += (text.empty() ? "" : "\n") + pplay::tr("Poster") + ": " + posterError;
    }
    detailsText->setString(pplay::TextWrap::wrap(main->getFont(), detailsText->getCharacterSize(), text, detailsWidth));
    detailsText->setPosition(x, detailsY);
    if (!text.empty()) {
        FloatRect bounds = detailsText->getLocalBounds();
        bottom = std::max(bottom, detailsY + bounds.top + bounds.height);
    }
    // the last line descenders
    contentHeight = bottom + (float) detailsText->getCharacterSize() * 0.4f;
    content->setSize(getSize().x, contentHeight);
    scrollBy(0);
}

void DetailsView::scrollBy(float delta) {
    float max = std::max(0.0f, contentHeight - view->getSize().y);
    scroll = std::max(0.0f, std::min(max, scroll + delta));
    updateScroll();
}

void DetailsView::updateScroll() {
    content->setPosition(0, -std::round(scrollShown));
    float viewHeight = view->getSize().y;
    if (contentHeight <= viewHeight + 1) {
        scrollBar->setVisibility(Visibility::Hidden);
        return;
    }
    float barHeight = std::max(viewHeight * viewHeight / contentHeight, 24 * main->getScaling().y);
    float max = contentHeight - viewHeight;
    scrollBar->setSize(scrollBar->getSize().x, barHeight);
    scrollBar->setPosition(getSize().x - MARGIN * main->getScaling().x / 2,
                           view->getPosition().y + (viewHeight - barHeight) * std::min(1.0f, scrollShown / max));
    scrollBar->setVisibility(Visibility::Visible);
}

DetailsView::Action DetailsView::onKeys(unsigned int keys) {
    if (keys & Input::B) {
        return Action::Files;
    }
    if (keys & (Input::LB | Input::RB)) {
        return Action::Menu;
    }
    if (keys & Input::X) {
        return Action::Player;
    }
    if (keys & Input::Y) {
        return Action::Reset;
    }

    if (button < 0) {
        float max = std::max(0.0f, contentHeight - view->getSize().y);
        float step = std::max(view->getSize().y * 0.35f, (float) detailsText->getCharacterSize() * 3);
        if (keys & (Input::Down | Input::Right)) {
            // at the end: the buttons
            if (scroll < max - 0.5f) {
                scrollBy(step);
            } else if (hints->count() > 0) {
                button = 0;
            }
        } else if (keys & Input::Up) {
            scrollBy(-step);
        } else if (keys & Input::Left) {
            return Action::Files;
        } else if (keys & Input::A) {
            return Action::Open;
        }
    } else {
        if (keys & Input::Up) {
            // the row above, or the content
            button = hints->neighbor(button, -1);
        } else if (keys & Input::Down) {
            int below = hints->neighbor(button, 1);
            button = below >= 0 ? below : button;
        } else if (keys & Input::Left) {
            // the row start: out of the panel
            if (!hints->sameRow(button, button - 1)) {
                return Action::Files;
            }
            button--;
        } else if (keys & Input::Right) {
            button = hints->sameRow(button, button + 1) ? button + 1 : button;
        } else if (keys & Input::A) {
            return (Action) hints->idAt(button);
        }
    }
    hints->setFocus(button);
    return Action::None;
}

void DetailsView::onUpdate() {
    float dt = frameClock.restart().asSeconds();

    Texture *texture;
    std::string error;
    float width = getSize().x - MARGIN * main->getScaling().x * 2;
    if (loader->poll({(int) width, (int) width * 3}, &texture, &error)) {
        if (texture != nullptr) {
            showPoster(texture);
        } else {
            failedPosters.insert(posterUrl);
            std::string next = nextPoster();
            if (!next.empty()) {
                posterUrl = next;
                loader->request(posterUrl);
            } else {
                posterError = error;
                layout();
            }
        }
    }

    Player *player = main->getPlayer();
    bool nowPlaying = !player->getMpv()->isStopped();
    bool nowShown = player->isVisible() && !player->isFullscreen();
    if (nowPlaying != playing || nowShown != playerShown) {
        playing = nowPlaying;
        playerShown = nowShown;
        updateHints();
        layout();
    }

    if (scrollShown != scroll) {
        scrollShown += (scroll - scrollShown) * std::min(1.0f, dt * 14);
        if (std::fabs(scroll - scrollShown) < 0.5f) {
            scrollShown = scroll;
        }
        updateScroll();
    }

    posterFrame->setOutlineColor(COLOR_ACCENT);
    focusBar->setFillColor(COLOR_ACCENT);
    scrollBar->setFillColor(focused && button < 0 ? COLOR_ACCENT : Color(255, 255, 255, 60));

    Rectangle::onUpdate();
}
