//
// Created by cpasjuste on 12/04/18.
//

#include <algorithm>
#include "main.h"
#include "paths.h"
#include "filer.h"
#include "utility.h"
#ifdef PPLAY_ENABLE_SCRAPPING
#include "p_search.h"
#endif
#include "torrserve.h"
#include "preview_view.h"

#define ITEM_HEIGHT 30
#define EMPTY_NAME "<Empty>"

using namespace c2d;

Filer::Filer(Main *m, const std::string &path, const c2d::FloatRect &rect) :
        Rectangle(m->getScaled(rect)) {

    main = m;

    mutex = new C2DMutex();

    Filer::setSize(main->getSize().x, main->getScaling().y);
    Vector2f size;
#ifdef PPLAY_ENABLE_SCRAPPING
    scrapping_enabled = main->getConfig()->getOption(OPT_ENABLE_SCRAPPING)->getInteger() == 1;
    if (scrapping_enabled) {
        // force scrap view width to scrapped backdrop width
        scrapView = new ScrapView(main, {rect.width - (780 * m->getScaling().x), 0,
                                        780 * m->getScaling().x, rect.height});
        Filer::add(scrapView);
        /// scaling this too much is not pretty, so no scaling for filer
        size = {rect.width - (670 * m->getScaling().x),
            rect.height - (64 * m->getScaling().y)};
    } else {
        size = {rect.width, rect.height - (64 * m->getScaling().y)};
    }
#else
    size = {rect.width, rect.height - (64 * m->getScaling().y)};
#endif
    fullWidth = size.x;
    listWidth = size.x;
#ifdef PPLAY_ENABLE_SCRAPPING
    if (!scrapping_enabled)
#endif
    {
        float panelWidth = 560 * m->getScaling().x;
        listWidth = size.x - panelWidth;
        preview = new PreviewView(main, {listWidth, 0, panelWidth, size.y});
        Filer::add(preview);
    }

    // highlight
    highlight = new Highlight({size.x, (float) ITEM_HEIGHT * m->getScaling().y}, Highlight::CursorPosition::Left);
    Filer::add(highlight);

    // files items
    item_height = highlight->getSize().y;
    item_max = (int) (size.y / item_height);
    if (((float) item_max * item_height) < size.y) {
        item_height = size.y / (float) item_max;
    }

    for (unsigned int i = 0; i < (unsigned int) item_max; i++) {
        FloatRect r = {0, item_height * (float) i, size.x, item_height};
        items.emplace_back(new FilerItem(main, r));
        Filer::add(items[i]);
    }

    setPreviewEnabled(main->getConfig()->getOption(OPT_PREVIEW_PANEL)->getInteger() == 1);

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

#ifdef PPLAY_ENABLE_SCRAPPING
void Filer::setScrapInfo(const Io::File &target, const std::vector<pscrap::Movie> &movies) {
    mutex->lock();
    for (size_t i = 0; i < files.size(); i++) {
        if (files[i].path == target.path) {
            files[i].movies = movies;
            dirty = true;
            break;
        }
    }
    mutex->unlock();
}
#endif

void Filer::setSelection(int index) {
    item_index = index;
    int page = item_index / item_max;
    unsigned int index_start = (unsigned int) page * item_max;
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "Filer::setSelection item_index=" + std::to_string(item_index)
        + " item_max=" + std::to_string(item_max));

    mutex->lock();

    for (unsigned int i = 0; i < (unsigned int) item_max; i++) {
        if (index_start + i >= files.size()) {
            items[i]->setVisibility(Visibility::Hidden);
        } else {
            // load media info, set file
            MediaFile file = files[index_start + i];
            pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                "Filer::setSelection item idx="
                + std::to_string(index_start + i) + " name=" + file.name
                + " path=" + file.path + " type=" + std::to_string((int) file.type));
            items[i]->setFile(file);
            items[i]->setVisibility(Visibility::Visible);
#ifdef PPLAY_ENABLE_SCRAPPING
            if (!file.movies.empty()) {
                items[i]->setTitle(file.movies[0].title);
            }
#endif
            // set highlight position
            if (index_start + i == (unsigned int) item_index) {
                highlight->tweenTo(items[i]->getPosition());
                if (previewEnabled) {
                    preview->setFile(file);
                }
#ifdef PPLAY_ENABLE_SCRAPPING
                if (scrapping_enabled) {
                    if (file.type == Io::Type::File) {
                        if (!scrapView->isVisible()) {
                            scrapView->setVisibility(Visibility::Visible);
                        }
                        scrapView->setMovie(file);
                    } else {
                        scrapView->setVisibility(Visibility::Hidden);
                    }
                }
#endif
            }
        }
    }

    if (files.empty()) {
        highlight->setVisibility(Visibility::Hidden);
    } else {
        highlight->setVisibility(Visibility::Visible);
    }

    mutex->unlock();
    MediaFile selected = getSelection();
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "Filer::setSelection finished index=" + std::to_string(item_index) + " path=" + path
        + " selectedName=" + selected.name + " selectedPath=" + selected.path
        + " selectedType=" + std::to_string((int) selected.type));
    main->syncLastLocation();
}

MediaFile Filer::getSelection() const {
    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Filer::getSelection started.");
    mutex->lock();
    if (!files.empty() && files.size() > (unsigned int) item_index) {
        MediaFile file = files[item_index];
        mutex->unlock();
        return file;
    }
    mutex->unlock();
    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Filer::getSelection finished.");

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
    if (main->getMenuMain()->isMenuVisible()
        || main->getPlayer()->isFullscreen()) {
        return false;
    }

    mutex->lock();
    size_t filesSize = files.size();
    mutex->unlock();

    unsigned int keys = players[0].buttons;
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "Filer::onInput keys=" + pplay::Utility::getKeysString(keys));

    if (picking) {
        if (keys & (c2d::Input::LB | c2d::Input::RB | Input::Left | Input::Y)) {
            return true;
        }
        if (keys & (Input::X | Input::Right)) {
            endPick();
            main->getPlayer()->setFullscreen(true);
            return true;
        }
        if (keys & Input::A) {
            if (filesSize == 0 || isPlaceholder(getSelection())) {
                return true;
            }
            MediaFile selection = getSelection();
            if (selection.type == Io::Type::Directory) {
                enter(item_index);
            } else {
                for (auto &ext: pickExtensions) {
                    if (Utility::endsWith(selection.name, ext, false)) {
                        auto callback = pickCallback;
                        endPick();
                        callback(selection);
                        break;
                    }
                }
            }
            return true;
        }
    }

    if (keys & c2d::Input::LB || keys & c2d::Input::RB) {
        main->getMenuMain()->setVisibility(Visibility::Visible, true);
    } else if (keys & Input::Up) {
        item_index--;
        if (item_index < 0)
            item_index = (int) (filesSize - 1);
        setSelection(item_index);
#ifdef PPLAY_ENABLE_SCRAPPING
        if (scrapping_enabled) scrapView->unload();
#endif
    } else if (keys & Input::Down) {
        item_index++;
        if (item_index >= (int) filesSize) {
            item_index = 0;
        }
        setSelection(item_index);
#ifdef PPLAY_ENABLE_SCRAPPING
        if (scrapping_enabled) scrapView->unload();
#endif
    } else if (keys & Input::Left) {
        main->getMenuMain()->setVisibility(Visibility::Visible, true);
    } else if (keys & Input::Right) {
        if (!main->getPlayer()->getMpv()->isStopped()
            && !main->getPlayer()->isFullscreen()) {
            main->getPlayer()->setFullscreen(true);
        }
    } else if (keys & Input::A) {
        if (getSelection().type == Io::Type::Directory) {
#ifdef PPLAY_ENABLE_SCRAPPING
            if (scrapping_enabled) scrapView->unload();
#endif
            enter(item_index);
        } else if (pplay::Utility::isMedia(getSelection())) {
            main->getPlayer()->setFullscreen(true);
            main->getPlayer()->load(files[item_index]);
        }
    } else if (keys & Input::B) {
#ifdef PPLAY_ENABLE_SCRAPPING
        if (scrapping_enabled) scrapView->unload();
#endif
        exit();
    } else if (keys & Input::X) {
#ifdef PPLAY_ENABLE_SCRAPPING
        if (scrapping_enabled) {
            main->getScrapper()->scrap(path);
        }
#endif
    } else if (keys & Input::Y) {
        // the opened file would write its position back (periodic / pause save)
        if (main->getPlayer()->isPlaying(files[item_index].path)) {
            main->getPlayer()->forgetProgress();
        }
        if (pplay::Utility::isWatchLaterExist(pplay::TorrServe::toStreamUrl(files[item_index].path))) {
            main->getStatus()->show("Info...", "Removing local watch later data...");
            pplay::Utility::deleteWatchLater(pplay::TorrServe::toStreamUrl(files[item_index].path));
        }
        if (pplay::TorrServe::isFileViewed(files[item_index].path)) {
            main->getStatus()->show("Info...", "Removing TorrServer watched state...");
            pplay::TorrServe::remFileViewed(files[item_index].path);
        }
        dirty = true;
    }

    return true;
}

void Filer::startPick(const std::vector<std::string> &extensions, const std::string &dir,
                      const std::string &hint, std::function<void(const MediaFile &)> onPick) {
    pickPrevPath = path;
    pickPrevSelection = files.empty() ? "" : getSelection().path;
    pickExtensions = extensions;
    pickCallback = std::move(onPick);
    picking = true;
    main->getStatus()->show("Info...", hint, true);
    if (!getDir(dir)) {
        getDir(pickPrevPath);
    }
}

void Filer::endPick() {
    picking = false;
    pickCallback = nullptr;
    main->getStatus()->hide();
    // listed again without the picked extensions
    if (getDir(pickPrevPath) && !pickPrevSelection.empty()) {
        selectByPath(pickPrevSelection);
    }
}

void Filer::onUpdate() {
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
    highlight->setFillColor(COLOR_HIGHLIGHT);
    highlight->setCursorColor(COLOR_ACCENT);

    C2DObject::onUpdate();
}


static bool compare(const MediaFile &a, const MediaFile &b) {
    if (a.type == Io::Type::Directory && b.type != Io::Type::Directory) {
        return true;
    }
    if (a.type != Io::Type::Directory && b.type == Io::Type::Directory) {
        return false;
    }

#ifdef PPLAY_ENABLE_SCRAPPING
    std::string aa = a.movies.empty() ? a.name : a.movies[0].title;
    std::string bb = b.movies.empty() ? b.name : b.movies[0].title;
#else
    std::string aa = a.name;
    std::string bb = b.name;
#endif

    return Utility::toLower(aa) < Utility::toLower(bb);
}

bool Filer::getDir(const std::string &p) {
    printf("getDir(%s)\n", p.c_str());
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Filer::getDir path=" + p);

    mutex->lock();
    files.clear();
    path = p;
    if (path.size() > 1 && Utility::endsWith(path, "/")) {
        path = Utility::removeLastSlash(path);
    }

    std::vector<std::string> ext = pplay::Utility::getMediaExtensions();
    if (picking) {
        ext.insert(ext.end(), pickExtensions.begin(), pickExtensions.end());
    }
    pplay::Io::DeviceType type = ((pplay::Io *) main->getIo())->getDeviceType(p);
    int timeout = main->getConfig()->getOption(OPT_NETWORK_TIMEOUT)->getInteger();
    std::vector<Io::File> _files =
        ((pplay::Io *) main->getIo())->getDirList(type, ext, path, timeout, false);

    for (auto &file: _files) {
        MediaFile mf(file, MediaInfo(file));
#ifdef PPLAY_ENABLE_SCRAPPING
        if (file.type == Io::Type::File) {
            pscrap::Search search;
            std::string scrapPath = pplay::Utility::getMediaScrapPath(file);
            if (main->getIo()->exist(scrapPath)) {
                search.load(scrapPath);
                if (search.total_results > 0) {
                    mf.movies = search.movies;
                }
            }
        }
#endif
        files.emplace_back(mf);
    }

    // sort after title have been scrapped
    std::sort(files.begin(), files.end(), compare);

    files.erase(std::remove_if(files.begin(), files.end(),
                               [](const MediaFile &f) { return f.name == ".." || f.name == "."; }),
                files.end());
    if (!isTopLevel(path)) {
        Io::File file("..", p + "/..", Io::Type::Directory, 0);
        files.insert(files.begin(), MediaFile{file, MediaInfo(file)});
    }
    if (files.empty()) {
        Io::File file(EMPTY_NAME, "", Io::Type::Unknown, 0);
        files.emplace_back(file, MediaInfo(file));
    }

    mutex->unlock();
    for (size_t i = 0; i < files.size(); i++) {
        pplay::Utility::log(pplay::Utility::LogLevel::Debug,
            "Filer::file[" + std::to_string(i) + "] name=" + files[i].name
            + " path=" + files[i].path + " type=" + std::to_string((int) files[i].type));
    }
    setSelection(0);

    return true;
}

bool Filer::isPlaceholder(const MediaFile &file) {
    return file.type == Io::Type::Unknown && file.path.empty() && file.name == EMPTY_NAME;
}

bool Filer::isTopLevel(const std::string &p) {
    if (p.empty() || p == "/" || p.find('/') == std::string::npos) {
        return true;
    }
#ifdef __SWITCH__
    if (Utility::startWith(p, "ums") && p.find('/') == p.size() - 1) {
        return true;
    }
#endif
    pplay::Io::DeviceType type = ((pplay::Io *) main->getIo())->getDeviceType(p);
    if (type == pplay::Io::DeviceType::Local) {
        return false;
    }
    std::string s = pplay::Paths::normalizeSmbPathSeparators(p);
    if (!Utility::endsWith(s, "/")) {
        s += "/";
    }
    for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
        std::string root = main->getConfig()->getOption(PPLAYConfig::networkOption(i))->getString();
        root = pplay::Paths::normalizeSmbPathSeparators(root);
        if (!root.empty() && !Utility::endsWith(root, "/")) root += "/";
        if (!root.empty() && s == root) {
            return true;
        }
    }
    return false;
}

void Filer::setPreviewEnabled(bool enabled) {
    previewEnabled = enabled && preview != nullptr;
    float width = previewEnabled ? listWidth : fullWidth;
    if (preview != nullptr) {
        preview->setVisibility(previewEnabled ? Visibility::Visible : Visibility::Hidden);
    }
    highlight->setWidth(width);
    for (auto *item: items) {
        item->setWidth(width);
    }
    dirty = true;
}

void Filer::enter(int index) {
    MediaFile file = getSelection();
    bool success;

    if (isPlaceholder(file)) {
        return;
    }

    if (file.name == "..") {
        exit();
        return;
    }

    if (!file.path.empty() && file.name != "..") {
        success = getDir(file.path);
    } else if (path == "/") {
        success = getDir(path + file.name);
    } else {
        success = getDir(path + "/" + file.name);
    }
    if (success) {
        item_index_prev.push_back(index);
        setSelection(item_index);
    }
}

void Filer::exit() {
    std::string p = path;

    if (isTopLevel(p)) {
        return;
    }

    while (p.back() != '/') {
        p.erase(p.size() - 1);
    }

    if (p.size() > 1 && Utility::endsWith(p, "/")) {
        p.erase(p.size() - 1);
    }

    if (getDir(p)) {
        if (!item_index_prev.empty()) {
            int last = (int) item_index_prev.size() - 1;
            mutex->lock();
            size_t filesSize = files.size();
            mutex->unlock();
            if (item_index_prev[last] < (int) filesSize) {
                item_index = item_index_prev[last];
            }
            item_index_prev.erase(item_index_prev.end() - 1);
        }
        setSelection(item_index);
    }
}

std::string Filer::getError() {
    std::string error = ((pplay::Io *) main->getIo())->getLastError();
    return error.empty() ? "Could not open: " + path : error;
}

void Filer::clearHistory() {
    item_index_prev.clear();
}

std::string Filer::getPath() {
    return path;
}

Filer::~Filer() {
    delete (mutex);
}
