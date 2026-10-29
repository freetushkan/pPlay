//
// Filer preview panel.
//

#include <algorithm>
#include <cmath>

#include "main.h"
#include "clip_rect.h"
#include "filer.h"
#include "input_map.h"
#include "key_hints.h"
#include "player.h"
#include "poster_loader.h"
#include "preview_info.h"
#include "preview_view.h"
#include "text_wrap.h"
#include "torrserve.h"

using namespace c2d;

#define MARGIN 24
// details right of the poster up to this part of the panel width
#define POSTER_SIDE 0.4f
// not focused poster height, of the panel width (a 2:3 poster is on the side)
#define POSTER_HEIGHT 0.55f

PreviewView::PreviewView(Main *m, const c2d::FloatRect &rect) : Rectangle(rect), main(m) {
    float margin = MARGIN * main->getScaling().x;
    float bar = std::max(2.0f, 3 * main->getScaling().x);

    loader = new PosterLoader(main);
    // the poster sprite never points to a deleted texture
    placeholder = new C2DTexture(Vector2i{4, 4});

    view = new pplay::ClipRect({0, 0, rect.width, 1});
    PreviewView::add(view);
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
    PreviewView::add(scrollBar);

    focusBar = new RectangleShape(Vector2f{bar, 1});
    focusBar->setVisibility(Visibility::Hidden);
    PreviewView::add(focusBar);

    separator = new RectangleShape(Vector2f{rect.width - margin * 2, std::max(1.0f, main->getScaling().y)});
    separator->setFillColor(Color(255, 255, 255, 50));
    PreviewView::add(separator);

    hints = new KeyHints(main, rect.width - margin * 2);
    PreviewView::add(hints);
}

PreviewView::~PreviewView() {
    delete (loader);
    delete (posterTexture);
    delete (placeholder);
}

bool PreviewView::isResettable(const MediaFile &file) {
    return file.type == Io::Type::File && !Filer::isPlaceholder(file)
           && (pplay::Utility::isWatchLaterExist(pplay::TorrServe::toStreamUrl(file.path))
               || pplay::TorrServe::isFileViewed(file.path));
}

void PreviewView::setFile(const MediaFile &f) {
    if (f.path != file.path || f.name != file.name) {
        scroll = scrollShown = 0;
        posterError.clear();
    }
    file = f;
    pplay::PreviewInfo info = pplay::PreviewInfo::of(file);
    heading = info.title;
    details = info.details;
    if (info.poster != shownPoster) {
        hidePoster();
    }
    posterUrl = info.poster;
    loader->request(posterUrl == shownPoster ? "" : posterUrl);
    updateHints();
    layout();
}

void PreviewView::setFocused(bool value) {
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

void PreviewView::updateHints() {
    using pplay::InputMap::buttonName;
    bool empty = Filer::isPlaceholder(file);
    const char *open = file.type == Io::Type::Directory ? "Open" : "Play";
    bool resettable = isResettable(file);

    std::vector<KeyHints::Hint> list;
    auto add = [&list](const std::string &key, const char *action, Action id) {
        list.push_back({key, action, (int) id});
    };
    if (focused) {
        if (!empty) {
            add(buttonName(Input::A), open, Action::Open);
        }
        if (playing) {
            add(buttonName(Input::X), "Player", Action::Player);
        }
        if (resettable) {
            add(buttonName(Input::Y), "Reset progress", Action::Reset);
        }
        add(buttonName(Input::B), "Back", Action::Files);
    } else {
        add(buttonName(Input::Up) + buttonName(Input::Down), "Select", Action::None);
        if (!empty) {
            add(buttonName(Input::A), open, Action::Open);
        }
        add(buttonName(Input::B), "Back", Action::Parent);
        add(buttonName(Input::Left), "Menu", Action::Menu);
        add(buttonName(Input::Right), "Details", Action::Panel);
        if (playing) {
            add(buttonName(Input::X), "Player", Action::Player);
        }
        if (resettable) {
            add(buttonName(Input::Y), "Reset progress", Action::Reset);
        }
    }
    hints->setHints(list);
    if (button >= hints->count()) {
        button = hints->count() - 1;
    }
    hints->setFocus(button);
}

void PreviewView::showPoster(c2d::Texture *texture) {
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

void PreviewView::hidePoster() {
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

void PreviewView::layout() {
    Vector2f scaling = main->getScaling();
    float margin = MARGIN * scaling.x;
    float top = (32 + MARGIN) * scaling.y;
    float width = getSize().x - margin * 2;
    float gap = margin / 2;

    // fixed bottom: the keys (above the minimized player), a separator
    float limit = main->getSize().y - margin;
    if (playerShown) {
        limit = main->getSize().y * 0.75f - 16 - gap;
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
    title->setString(pplay::TextWrap::wrap(main->getFont(), title->getCharacterSize(), heading, width));
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

void PreviewView::scrollBy(float delta) {
    float max = std::max(0.0f, contentHeight - view->getSize().y);
    scroll = std::max(0.0f, std::min(max, scroll + delta));
    updateScroll();
}

void PreviewView::updateScroll() {
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

PreviewView::Action PreviewView::onKeys(unsigned int keys) {
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
            button = -1;
        } else if (keys & Input::Left) {
            if (button == 0) {
                return Action::Files;
            }
            button--;
        } else if (keys & Input::Right) {
            button = std::min(button + 1, hints->count() - 1);
        } else if (keys & Input::A) {
            return (Action) hints->idAt(button);
        }
    }
    hints->setFocus(button);
    return Action::None;
}

void PreviewView::onUpdate() {
    float dt = frameClock.restart().asSeconds();

    Texture *texture;
    std::string error;
    float width = getSize().x - MARGIN * main->getScaling().x * 2;
    if (loader->poll({(int) width, (int) width * 3}, &texture, &error)) {
        if (texture != nullptr) {
            showPoster(texture);
        } else {
            posterError = error;
            layout();
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
