//
// Created by cpasjuste on 12/04/18.
//

#include <algorithm>
#include <cmath>
#include "main.h"
#include "paths.h"
#include "filer.h"
#include "utility.h"
#include "torrserve.h"
#include "details_view.h"
#include "media_probe.h"

#define ITEM_HEIGHT 30

using namespace c2d;

Filer::Filer(Main *m, const std::string &path, const c2d::FloatRect &rect) : Rectangle(m->getScaled(rect)) {

    main = m;

    mutex = new C2DMutex();

    Filer::setSize(main->getSize().x, main->getScaling().y);
    Vector2f size;
    size = {rect.width, rect.height - (64 * m->getScaling().y)};
    // files list, information panel on the right
    float panelWidth = 560 * m->getScaling().x;
    listWidth = size.x - panelWidth;
    details = new DetailsView(main, {listWidth, 0, panelWidth, size.y});
    probe = new MediaProbe();
    Filer::add(details);

    list = new pplay::ClipRect({0, 0, listWidth, size.y});
    Filer::add(list);

    highlight = new Highlight({listWidth, (float) ITEM_HEIGHT * m->getScaling().y}, Highlight::CursorPosition::Left);
    list->add(highlight);

    // files items
    item_height = highlight->getSize().y;
    item_max = (int) (size.y / item_height);
    if (((float) item_max * item_height) < size.y) {
        item_height = size.y / (float) item_max;
    }

    for (int i = 0; i < item_max + 2; i++) {
        items.emplace_back(new FilerItem(main, {0, item_height * (float) (i - 1), listWidth, item_height}));
        list->add(items.back());
    }

    // tween
    Filer::add(new TweenAlpha(0, 255, 0.5f));
}

void Filer::setMediaInfo(const MediaFile &target, const MediaInfo &mediaInfo) {
    mutex->lock();
    for (size_t i = 0; i < files.size(); i++) {
        if (files[i].path == target.path) {
            files[i].mediaInfo = mediaInfo;
            dirty = true;
            break;
        }
    }
    mutex->unlock();
}

void Filer::setSelection(int index) {
    mutex->lock();
    int count = (int) files.size();
    item_index = index;
    // the selection kept in view, a row at a time scrolled smoothly
    int first = top;
    if (index < first) {
        first = index;
    } else if (index >= first + item_max) {
        first = index - item_max + 1;
    }
    first = std::max(0, std::min(first, count - item_max));
    if (std::abs(first - top) == 1) {
        scrollOffset = std::max(-item_height, std::min(item_height, scrollOffset + (float) (first - top) * item_height));
    } else if (first != top) {
        scrollOffset = 0;
    }
    top = first;

    for (int i = 0; i < (int) items.size(); i++) {
        int fileIndex = top - 1 + i;
        if (fileIndex < 0 || fileIndex >= count) {
            items[i]->setVisibility(Visibility::Hidden);
            continue;
        }
        const MediaFile &file = files[fileIndex];
        items[i]->setFile(file);
        items[i]->setVisibility(Visibility::Visible);
        if (fileIndex == item_index) {
            details->setFile(file);
            if (main->getConfig()->getOption(OPT_MEDIA_PROBE)->getInteger() != 0) {
                probe->request(file);
            }
        }
    }
    highlight->tweenTo({0, (float) (item_index - top) * item_height});
    highlight->setVisibility(files.empty() ? Visibility::Hidden : Visibility::Visible);
    mutex->unlock();
    placeItems();

    MediaFile selected = getSelection();
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                        "Filer::setSelection finished index=" + std::to_string(item_index) + " path=" + path
                                + " selectedName=" + selected.name + " selectedPath=" + selected.path
                                + " selectedType=" + std::to_string((int) selected.type));
    main->syncLastLocation();
}

void Filer::placeItems() {
    for (int i = 0; i < (int) items.size(); i++) {
        items[i]->setPosition(0, std::round((float) (i - 1) * item_height + scrollOffset));
    }
}

MediaFile Filer::getSelection() const {
    mutex->lock();
    if (!files.empty() && files.size() > (unsigned int) item_index) {
        MediaFile file = files[item_index];
        mutex->unlock();
        return file;
    }
    mutex->unlock();
    return {};
}

bool Filer::getNextMediaFile(const MediaFile &current, MediaFile &next) {
    mutex->lock();
    if (files.empty()) {
        mutex->unlock();
        return false;
    }

    int currentIndex = -1;
    for (size_t i = 0; i < files.size(); i++) {
        if (files[i].path == current.path) {
            currentIndex = (int) i;
            break;
        }
    }

    if (currentIndex < 0) {
        mutex->unlock();
        return false;
    }

    for (size_t off = 1; off < files.size(); off++) {
        size_t idx = ((size_t) currentIndex + off) % files.size();
        if (pplay::Utility::isMedia(files[idx])) {
            next = files[idx];
            mutex->unlock();
            return true;
        }
    }

    mutex->unlock();
    return false;
}

std::vector<MediaFile> Filer::getFilesSnapshot() const {
    mutex->lock();
    std::vector<MediaFile> _files = files;
    std::vector<MediaFile> snapshot;
    for (auto &file: _files) {
        if (file.type == Io::Type::File) {
            snapshot.emplace_back(file);
        }
    }
    mutex->unlock();
    return snapshot;
}

bool Filer::selectByPath(const std::string &targetPath) {
    if (targetPath.empty()) {
        return false;
    }
    mutex->lock();
    for (size_t i = 0; i < files.size(); i++) {
        if (files[i].path == targetPath) {
            mutex->unlock();
            setSelection((int) i);
            return true;
        }
    }
    mutex->unlock();
    return false;
}

bool Filer::onInput(c2d::Input::Player *players) {
    if (main->getMenuMain()->isMenuVisible() || main->getPlayer()->isFullscreen()) {
        return false;
    }

    mutex->lock();
    size_t filesSize = files.size();
    mutex->unlock();

    unsigned int keys = players[0].buttons;
    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Filer::onInput keys=" + pplay::Utility::getKeysString(keys));

    if (picking) {
        if (keys & (Input::LB | Input::RB | Input::Left | Input::Right | Input::Y)) {
            return true;
        }
        if (keys & Input::X) {
            endPick();
            main->getPlayer()->setFullscreen(true);
            return true;
        }
        if (keys & Input::A) {
            pick();
            return true;
        }
    }

    using Action = DetailsView::Action;
    Action action = Action::None;
    if (details->isFocused()) {
        action = details->onKeys(keys);
    } else if (keys & (Input::Up | Input::Down)) {
        if (filesSize > 0) {
            int next = item_index + ((keys & Input::Up) ? -1 : 1);
            setSelection(next < 0 ? (int) filesSize - 1 : next >= (int) filesSize ? 0 : next);
        }
    } else if (keys & (Input::LB | Input::RB | Input::Left)) {
        action = Action::Menu;
    } else if (keys & Input::Right) {
        action = Action::Panel;
    } else if (keys & Input::A) {
        action = Action::Open;
    } else if (keys & Input::B) {
        action = Action::Parent;
    } else if (keys & Input::X) {
        action = Action::Player;
    } else if (keys & Input::Y) {
        action = Action::Reset;
    }
    run(action);

    return true;
}

void Filer::run(DetailsView::Action action) {
    using Action = DetailsView::Action;
    Player *player = main->getPlayer();
    MediaFile selection = getSelection();
    switch (action) {
        case Action::Open:
            details->setFocused(false);
            if (selection.type == Io::Type::Directory) {
                enter(item_index);
            } else if (pplay::Utility::isMedia(selection)) {
                player->setFullscreen(true);
                player->load(selection);
            }
            break;
        case Action::Parent:
            exit();
            break;
        case Action::Menu:
            details->setFocused(false);
            main->getMenuMain()->setVisibility(Visibility::Visible, true);
            break;
        case Action::Panel:
            details->setFocused(true);
            break;
        case Action::Files:
            details->setFocused(false);
            break;
        case Action::Player:
            if (!player->getMpv()->isStopped() && !player->isFullscreen()) {
                details->setFocused(false);
                player->setFullscreen(true);
            }
            break;
        case Action::Reset:
            resetProgress(selection);
            break;
        default:
            break;
    }
}

void Filer::resetProgress(const MediaFile &file) {
    if (!DetailsView::isResettable(file)) {
        return;
    }
    // the opened file would write its position back (periodic / pause save)
    if (main->getPlayer()->isPlaying(file.path)) {
        main->getPlayer()->forgetProgress();
    }
    std::string url = pplay::TorrServe::toStreamUrl(file.path);
    if (pplay::Utility::isWatchLaterExist(url)) {
        main->getStatus()->show("Info...", "Removing local watch later data...");
        pplay::Utility::deleteWatchLater(url);
    }
    if (pplay::TorrServe::isFileViewed(file.path)) {
        main->getStatus()->show("Info...", "Removing TorrServer watched state...");
        pplay::TorrServe::remFileViewed(file.path);
    }
    dirty = true;
}

void Filer::onUpdate() {
    float dt = std::min(0.1f, scrollClock.restart().asSeconds());
    if (scrollOffset != 0) {
        scrollOffset -= scrollOffset * std::min(1.0f, dt * 16);
        if (std::fabs(scrollOffset) < 0.5f) {
            scrollOffset = 0;
        }
        placeItems();
    }
    MediaFile probed;
    if (probe->poll(&probed)) {
        setMediaInfo(probed, probed.mediaInfo);
    }
    if (dirty) {
        setSelection(item_index);
        dirty = false;
    } else {
        if (files.empty()) {
            highlight->setVisibility(Visibility::Hidden, true);
        } else {
            highlight->setVisibility(Visibility::Visible, true);
        }
    }
    // the panel focused: dimmed
    highlight->setFillColor(COLOR_HIGHLIGHT, details->isFocused() ? 25 : 60);
    highlight->setCursorColor(details->isFocused() ? COLOR_GRAY : COLOR_ACCENT);

    C2DObject::onUpdate();
}

Filer::~Filer() {
    delete (probe);
    delete (mutex);
}

void Filer::clearMediaInfo() {
    mutex->lock();
    for (auto &file: files) {
        file.mediaInfo = MediaInfo();
    }
    dirty = true;
    mutex->unlock();
    probe->reset();
}
