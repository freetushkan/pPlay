//
// Created by cpasjuste on 12/04/18.
//

#include <algorithm>
#include <cmath>
#include "main.h"
#include "paths.h"
#include "filer.h"
#include "external_tracks.h"
#include "history.h"
#include "folder_marks.h"
#include "round_rect.h"
#include "utility.h"
#include "torrserve.h"
#include "saved.h"
#include "offline.h"
#include "playlists.h"
#include "keyboard.h"
#include "encodings.h"
#include "details_view.h"
#include "media_probe.h"
#include "input_map.h"

#define ITEM_HEIGHT 30

using namespace c2d;

Filer::Filer(Main *m, const std::string &path, const c2d::FloatRect &rect) : Rectangle(m->getScaled(rect)) {

    main = m;

    mutex = new C2DMutex();

    Filer::setSize(main->getSize().x, main->getScaling().y);
    Vector2f size;
    size = {rect.width, rect.height - (16 * m->getScaling().y)};
    float panelWidth = 560 * m->getScaling().x;
    listWidth = size.x - panelWidth;
    const float inset = 8 * m->getScaling().y;
    float crumbHeight = std::round(44 * m->getScaling().y);
    auto *listCard = new RoundRect({listWidth - 2 * inset, rect.height - crumbHeight - inset},
                                      13 * m->getScaling().y);
    listCard->setPosition(inset, crumbHeight);
    listCard->setFillColor(COLOR_SURFACE);
    Filer::add(listCard);

    auto *detailsCard = new RoundRect({panelWidth - 2 * inset, rect.height - crumbHeight - inset},
                                      13 * m->getScaling().y);
    detailsCard->setPosition(listWidth + inset, crumbHeight);
    detailsCard->setFillColor(COLOR_SURFACE);
    Filer::add(detailsCard);

    details = new DetailsView(main, {listWidth, crumbHeight, panelWidth, size.y - crumbHeight});
    probe = new MediaProbe();
    Filer::add(details);

    crumbsWidth = size.x - 150 * m->getScaling().x;
    crumbs = new Breadcrumb(main, {16 * m->getScaling().x, 0, crumbsWidth, crumbHeight});
    Filer::add(crumbs);
    float padTop = std::round(12 * m->getScaling().y);
    size.y -= crumbHeight + padTop;

    float rowWidth = listWidth - 2 * inset;
    list = new pplay::ClipRect({inset, crumbHeight + padTop, rowWidth, size.y});
    Filer::add(list);

    item_height = (float) ITEM_HEIGHT * m->getScaling().y;
    item_max = (int) (size.y / item_height);
    if (((float) item_max * item_height) < size.y) {
        item_height = size.y / (float) item_max;
    }
    highlight = new Highlight({rowWidth, item_height}, Highlight::CursorPosition::Left);
    list->add(highlight);

    scrollBar = new RectangleShape(Vector2f{std::max(2.0f, 3 * m->getScaling().x), 1});
    scrollBar->setPosition(inset + rowWidth - (12 * m->getScaling().x - inset), list->getPosition().y);
    scrollBar->setVisibility(Visibility::Hidden);
    Filer::add(scrollBar);

    for (int i = 0; i < item_max + 2; i++) {
        items.emplace_back(new FilerItem(main, {0, item_height * (float) (i - 1), rowWidth, item_height}));
        list->add(items.back());
    }

    cover = new RectangleShape({rect.width, rect.height});
    cover->setFillColor(COLOR_BG);
    cover->setLayer(100);
    cover->setVisibility(Visibility::Hidden);
    Filer::add(cover);
}

void Filer::setVisibility(Visibility visibility, bool tweenPlay) {
    if (!tweenPlay) {
        fadeDirection = 0;
        cover->setVisibility(Visibility::Hidden);
        Rectangle::setVisibility(visibility, false);
        return;
    }
    if (visibility == Visibility::Visible) {
        // from the background (or where a fade out was)
        if (!isVisible()) {
            coverAlpha = 255;
        }
        Rectangle::setVisibility(Visibility::Visible, false);
        fadeDirection = -1;
    } else {
        if (!isVisible()) {
            return;
        }
        fadeDirection = 1;
    }
    cover->setFillColor({COLOR_BG.r, COLOR_BG.g, COLOR_BG.b, (uint8_t) coverAlpha});
    cover->setVisibility(Visibility::Visible);
    fadeClock.restart();
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
    item_index = count > 0 ? std::max(0, std::min(index, count - 1)) : 0;
    index = item_index;
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
            if (main->getConfig()->getOption(OPT_MEDIA_PROBE)->getInteger() != 0 || details->isFocused()) {
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
        items[i]->highlight = highlight;
    }
    mutex->lock();
    int count = (int) files.size();
    mutex->unlock();
    if (count <= item_max) {
        scrollBar->setVisibility(Visibility::Hidden);
        return;
    }
    float viewHeight = list->getSize().y;
    float barHeight = std::max(viewHeight * (float) item_max / (float) count, 24 * main->getScaling().y);
    float shown = std::max(0.0f, std::min(1.0f, ((float) top - scrollOffset / item_height) / (float) (count - item_max)));
    scrollBar->setSize(scrollBar->getSize().x, std::round(barHeight));
    scrollBar->setPosition(scrollBar->getPosition().x, std::round(list->getPosition().y + (viewHeight - barHeight) * shown));
    scrollBar->setVisibility(Visibility::Visible);
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
            // a held key stops at the end, a new press wraps
            if (pplay::InputMap::isRepeat()) {
                next = std::max(0, std::min(next, (int) filesSize - 1));
            }
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
                if (!player->getMpv()->isStopped()
                    && pplay::Saved::originalOf(player->getFile().path) == pplay::Saved::originalOf(selection.path)) {
                    player->setFullscreen(true);
                } else if (pplay::Offline::isOffline(selection.path) && pplay::Saved::localCopy(selection.path).empty()) {
                    main->getStatus()->show("Error", pplay::tr("Offline: the file is not accessible"));
                } else {
                    player->setFullscreen(true);
                    player->load(selection);
                }
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
            // opened on purpose: read even if not by itself
            probe->request(getSelection());
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
            // nothing to reset: marks watched
            if (!DetailsView::isResettable(selection)) {
                if (selection.type == Io::Type::File && !isPlaceholder(selection) && pplay::Utility::isMedia(selection)) {
                    markWatched(selection);
                    pplay::FolderMarks::forget();
                }
                break;
            }
            resetProgress(selection);
            pplay::FolderMarks::forget();
            break;
        case Action::StartOver:
            if (pplay::Utility::isMedia(selection)) {
                details->setFocused(false);
                player->setFullscreen(true);
                player->load(selection, true, "pause=yes,start=0");
            }
            break;
        case Action::Watched:
            markWatched(selection);
            pplay::FolderMarks::forget();
            break;
        case Action::ResetFolder:
            markFolder(selection, false);
            break;
        case Action::WatchedFolder:
            markFolder(selection, true);
            break;
        case Action::Save:
            pplay::Saved::save(selection);
            break;
        case Action::Unsave:
            unsave(selection);
            break;
        case Action::AddToPlaylist:
            addToPlaylist(selection);
            break;
        case Action::RenamePlaylist:
            renamePlaylist(selection);
            break;
        case Action::DeletePlaylist:
            deletePlaylist(selection);
            break;
        case Action::RemoveFromPlaylist:
            removeFromPlaylist();
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
        main->getStatus()->show("Info", "Removing local watch later data");
        pplay::Utility::deleteWatchLater(url);
    }
    if (pplay::TorrServe::isFileViewed(file.path)) {
        main->getStatus()->show("Info", "Removing TorrServer watched state");
        pplay::TorrServe::remFileViewed(file.path);
    }
    forgetPlayback(file.path);
    dirty = true;
}

// with the progress: the last playback date, the external files bound to it
void Filer::forgetPlayback(const std::string &path) {
    std::string key = Player::keyOf(path);
    pplay::History::forget(key);
    pplay::ExternalTracks::save(key, {});
}

void Filer::unsave(const MediaFile &file) {
    pplay::Saved::State state = pplay::Saved::stateOf(file.path);
    if (state.saved == 0 && state.pending == 0) {
        return;
    }
    Player *player = main->getPlayer();
    bool local = pplay::Saved::isInside(file.path);
    // the copy being played
    if (local && !player->getMpv()->isStopped()
        && pplay::Saved::isInside(player->getFile().path)
        && (player->getFile().path == file.path || c2d::Utility::startWith(player->getFile().path, file.path + "/"))) {
        main->getStatus()->show("Error", "Stop the playback first");
        return;
    }
    if (state.saved > 0) {
        std::string what = file.type == Io::Type::Directory ? pplay::tr("Saved files") + ": " + std::to_string(state.saved)
                                                             : encoding::fix(file.name);
        if (main->showMessage(pplay::tr("Delete saved"), what, pplay::tr("Delete"), pplay::tr("Cancel"))
            != MessageBox::LEFT) {
            return;
        }
    }
    int count = pplay::Saved::remove(file.path);
    main->getStatus()->show("Info", pplay::tr("Saved files deleted") + ": " + std::to_string(count));
    // the saved list without them
    if (local) {
        int index = item_index;
        // the emptied directories are gone
        std::string dir = path;
        while (!pplay::Saved::isRoot(dir) && pplay::Saved::isInside(dir) && !pplay::Saved::hasDir(dir)) {
            dir = dir.substr(0, dir.rfind('/'));
        }
        getDir(dir);
        mutex->lock();
        int listed = (int) files.size();
        mutex->unlock();
        setSelection(std::max(0, std::min(index, listed - 1)));
    }
    dirty = true;
}

void Filer::addToPlaylist(const MediaFile &file) {
    if (isPlaceholder(file) || file.name == ".." || file.path.empty()) {
        return;
    }
    // the last one confirmed at once, another name typed
    PPLAYConfig *config = main->getConfig();
    std::string name = config->getOption(OPT_LAST_PLAYLIST)->getString();
    bool other = name.empty();
    if (!other) {
        int choice = main->showMessage(pplay::tr("Add to playlist"), pplay::tr("Playlist") + ": " + name,
                                       pplay::tr("Add"), pplay::tr("Other playlist"));
        if (choice != MessageBox::LEFT && choice != MessageBox::RIGHT) {
            return;
        }
        other = choice == MessageBox::RIGHT;
    }
    if (other && (!pplay::Keyboard::edit(pplay::tr("Playlist name"), name) || name.empty())) {
        return;
    }
    config->getOption(OPT_LAST_PLAYLIST)->setString(name);
    config->save();
    pplay::Playlists::add(name, file);
}

void Filer::renamePlaylist(const MediaFile &file) {
    std::string name = pplay::Playlists::nameOf(file.path);
    std::string old = name;
    if (!pplay::Keyboard::edit(pplay::tr("Playlist name"), name) || name.empty() || name == old) {
        return;
    }
    if (!pplay::Playlists::rename(file.path, name)) {
        main->getStatus()->show("Error", pplay::tr("Could not rename") + ": " + name);
        return;
    }
    if (main->getConfig()->getOption(OPT_LAST_PLAYLIST)->getString() == old) {
        main->getConfig()->getOption(OPT_LAST_PLAYLIST)->setString(name);
    }
    int index = item_index;
    getDir(path);
    setSelection(index);
}

void Filer::deletePlaylist(const MediaFile &file) {
    if (main->showMessage(pplay::tr("Delete"), pplay::tr("Playlist") + ": " + encoding::fix(file.name),
                          pplay::tr("Delete"), pplay::tr("Cancel"))
        != MessageBox::LEFT) {
        return;
    }
    if (!pplay::Playlists::remove(file.path)) {
        main->getStatus()->show("Error", pplay::tr("Could not delete") + ": " + file.name);
    }
    int index = item_index;
    getDir(path);
    setSelection(index);
}

void Filer::removeFromPlaylist() {
    // this copy: its place in the playlist ("..": the first row)
    mutex->lock();
    int index = item_index;
    bool parent = !files.empty() && files[0].name == "..";
    mutex->unlock();
    if (!pplay::Playlists::removeEntry(path, parent ? index - 1 : index)) {
        return;
    }
    getDir(path);
    setSelection(index);
}

void Filer::markWatched(const MediaFile &file) {
    if (file.type != Io::Type::File || isPlaceholder(file)) {
        return;
    }
    Player *player = main->getPlayer();
    long duration = file.mediaInfo.duration;
    // the opened file: its own duration, and no more saves over the mark
    if (player->isPlaying(file.path)) {
        duration = std::max(duration, player->getMpv()->getDuration());
        player->forgetProgress();
    }
    if (duration <= 0) {
        main->getStatus()->show("Error", "Unknown duration, play the file first");
        return;
    }
    pplay::Utility::setWatchLaterWatched(pplay::TorrServe::toStreamUrl(file.path), (double) duration);
    pplay::TorrServe::setFileViewed(file.path, (double) duration);
    main->getStatus()->show("Info", "Marked as watched");
    dirty = true;
}

void Filer::markFolder(const MediaFile &dir, bool watched) {
    std::vector<c2d::Io::File> list = pplay::FolderMarks::files(dir.path);
    if (list.empty()) {
        return;
    }
    Player *player = main->getPlayer();
    int count = 0;
    for (auto &file: list) {
        std::string path = pplay::Saved::originalOf(file.path);
        std::string url = pplay::TorrServe::toStreamUrl(path);
        bool local = pplay::Utility::isWatchLaterExist(url);
        bool server = pplay::TorrServe::isFileViewed(path);
        // the opened file: no more saves over the change
        if (player->isPlaying(file.path) || player->isPlaying(path)) {
            player->forgetProgress();
        }
        if (watched) {
            if (local ? pplay::Utility::isWatchLaterWatched(url) : server) {
                continue;
            }
            // the duration if known (the progress line full), else the mark only
            long duration = MediaInfo(file).duration;
            pplay::Utility::setWatchLaterWatched(url, duration > 0 ? (double) duration : 0);
            pplay::TorrServe::setFileViewed(path, duration > 0 ? (double) duration : 0);
        } else {
            if (!local && !server) {
                continue;
            }
            pplay::Utility::deleteWatchLater(url);
            if (server) {
                pplay::TorrServe::remFileViewed(path);
            }
            forgetPlayback(path);
        }
        count++;
    }
    main->getStatus()->show("Info", pplay::tr(watched ? "Marked as watched" : "Progress reset") + ": "
                                            + std::to_string(count));
    pplay::FolderMarks::request(dir, true);
    pplay::FolderMarks::forget();
    dirty = true;
}

void Filer::queueDurations() {
    durationsGeneration = pplay::TorrServe::viewedGeneration();
    int mode = main->getConfig()->getOption(OPT_TS_PROGRESS)->getInteger();
    probe->clearBackground();
    if (main->getConfig()->getOption(OPT_MEDIA_PROBE)->getInteger() != 1) {
        return;
    }
    mutex->lock();
    std::vector<MediaFile> list = files;
    mutex->unlock();
    for (auto &file: list) {
        if (file.type == Io::Type::File && file.mediaInfo.duration <= 0 && pplay::Utility::isMedia(file)
            && pplay::TorrServe::keptPosition(file.path, mode) > 0) {
            probe->requestBackground(file);
        }
    }
}

void Filer::onUpdate() {
    if (fadeDirection != 0) {
        float step = std::min(0.1f, fadeClock.restart().asSeconds()) * 255 / 0.5f;
        coverAlpha = std::min(255.0f, std::max(0.0f, coverAlpha + (float) fadeDirection * step));
        cover->setFillColor({COLOR_BG.r, COLOR_BG.g, COLOR_BG.b, (uint8_t) coverAlpha});
        if (fadeDirection < 0 && coverAlpha <= 0) {
            fadeDirection = 0;
            cover->setVisibility(Visibility::Hidden);
        } else if (fadeDirection > 0 && coverAlpha >= 255) {
            fadeDirection = 0;
            Rectangle::setVisibility(Visibility::Hidden, false);
            return;
        }
    }
    StatusBar *bar = main->getStatusBar();
    if (bar != nullptr) {
        crumbs->setWidth(std::max(1.0f, crumbsWidth - bar->getOfflineWidth()));
    }
    crumbs->setPath(path);
    if (pplay::TorrServe::viewedGeneration() != durationsGeneration) {
        queueDurations();
    }
    // the torrents changed on the server: listed again, the selection kept
    if (pplay::TorrServe::torrentsGeneration() != torrentsGeneration) {
        torrentsGeneration = pplay::TorrServe::torrentsGeneration();
        if (pplay::Io::get()->getDeviceType(path) == pplay::Io::DeviceType::TorrServe) {
            std::string selected = getSelection().path;
            int index = item_index;
            getDir(path);
            if (!selectByPath(selected)) {
                setSelection(std::max(0, std::min(index, (int) files.size() - 1)));
            }
        }
    }
    // a playlist written (an addition done): listed again
    if (pplay::Playlists::generation() != playlistGeneration) {
        playlistGeneration = pplay::Playlists::generation();
        if (pplay::Playlists::isPlaylist(path) || pplay::Playlists::isRoot(path)) {
            int index = item_index;
            getDir(path);
            setSelection(index);
        }
    }
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
        std::string error = probe->takeError();
        if (!error.empty()) {
            main->getStatus()->show("Error", encoding::fix(error));
        }
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
    scrollBar->setFillColor(details->isFocused() ? Color(255, 255, 255, 60) : COLOR_ACCENT);

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
