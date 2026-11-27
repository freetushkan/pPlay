//
// Filer directories: listing, navigation, file picking.
//

#include <algorithm>
#include <map>
#include "main.h"
#include "paths.h"
#include "filer.h"
#include "utility.h"
#include "saved.h"
#include "playlists.h"
#include "torrserve.h"
#include "p2play.h"

#define EMPTY_NAME "<Empty>"

using namespace c2d;

void Filer::pick() {
    MediaFile selection = getSelection();
    if (selection.path.empty() || isPlaceholder(selection)) {
        return;
    }
    if (selection.type == Io::Type::Directory) {
        enter(item_index);
        return;
    }
    for (auto &ext: pickExtensions) {
        if (Utility::endsWith(selection.name, ext, false)) {
            auto callback = pickCallback;
            endPick();
            callback(selection);
            return;
        }
    }
}

void Filer::startPick(const std::vector<std::string> &extensions, const std::string &dir, const std::string &hint,
                      std::function<void(const MediaFile &)> onPick) {
    pickPrevPath = path;
    pickPrevSelection = files.empty() ? "" : getSelection().path;
    pickPrevIndexHistory = item_index_prev;
    pickPrevPathHistory = path_prev;
    pickExtensions = extensions;
    pickCallback = std::move(onPick);
    picking = true;
    details->setFocused(false);
    details->setPicking(true);
    main->getStatus()->show("Info", hint, true);
    if (!getDir(dir)) {
        getDir(pickPrevPath);
    }
}

void Filer::endPick() {
    picking = false;
    details->setPicking(false);
    pickCallback = nullptr;
    main->getStatus()->hide();
    // listed again without the picked extensions
    if (getDir(pickPrevPath) && !pickPrevSelection.empty()) {
        selectByPath(pickPrevSelection);
    }
    item_index_prev = pickPrevIndexHistory;
    path_prev = pickPrevPathHistory;
    pickPrevIndexHistory.clear();
    pickPrevPathHistory.clear();
}

namespace {
    const char *COVER_NAMES[] = {"cover", "folder", "poster", "front"};
    const char *COVER_EXTENSIONS[] = {".jpg", ".jpeg", ".png", ".webp"};

    // cover.jpg and the like: its order in COVER_NAMES, -1 if not one
    int coverRank(const Io::File &file) {
        if (file.type != Io::Type::File) {
            return -1;
        }
        std::string name = Utility::toLower(file.name);
        for (int i = 0; i < 4; i++) {
            for (auto *ext: COVER_EXTENSIONS) {
                if (name == std::string(COVER_NAMES[i]) + ext) {
                    return i;
                }
            }
        }
        return -1;
    }
}

std::string Filer::folderCoverOf(const std::string &filePath) const {
    // the directory part (TorrServer paths: a query after the name)
    size_t slash = filePath.rfind('/', filePath.find('?'));
    if (slash == std::string::npos) {
        return "";
    }
    auto it = folderCovers.find(filePath.substr(0, slash));
    return it != folderCovers.end() ? it->second : "";
}

static bool compare(const MediaFile &a, const MediaFile &b) {
    if (a.type == Io::Type::Directory && b.type != Io::Type::Directory) {
        return true;
    }
    if (a.type != Io::Type::Directory && b.type == Io::Type::Directory) {
        return false;
    }

    std::string aa = a.name;
    std::string bb = b.name;

    return Utility::toLower(aa) < Utility::toLower(bb);
}

bool Filer::getDir(const std::string &p) {
    if (p.empty()) {
        mutex->lock();
        files.clear();
        Io::File empty(EMPTY_NAME, "", Io::Type::Unknown, 0);
        files.emplace_back(empty, MediaInfo(empty));
        path.clear();
        top = 0;
        scrollOffset = 0;
        dirty = true;
        mutex->unlock();
        details->setFolderCover("");
        setSelection(0);
        return true;
    }
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
    // the pictures too, for the directory cover
    std::vector<std::string> listed = ext;
    listed.insert(listed.end(), std::begin(COVER_EXTENSIONS), std::end(COVER_EXTENSIONS));
    pplay::Io::DeviceType type = ((pplay::Io *) main->getIo())->getDeviceType(p);
    int timeout = ((pplay::Io *) main->getIo())->networkTimeout(p);
    bool hidden = main->getConfig()->getOption(OPT_SHOW_HIDDEN)->getInteger() != 0;
    bool all = !picking && main->getConfig()->getOption(OPT_SHOW_ALL_FILES)->getInteger() != 0;
    if (all) {
        listed.clear();
    }
    // a playlist: its entries, as listed in it; the saved copies: from their index
    bool playlist = pplay::Playlists::isPlaylist(path);
    bool saved = !playlist && type == pplay::Io::DeviceType::Local && pplay::Saved::isInside(path);
    std::vector<Io::File> _files =
            playlist ? pplay::Playlists::entries(path)
            : saved  ? pplay::Saved::list(path)
                     : ((pplay::Io *) main->getIo())->getDirList(type, listed, path, timeout, false, hidden);
    if (pplay::Playlists::isRoot(path)) {
        pplay::Playlists::filter(_files);
    }
    // a network one not listed (no kept listing either): why
    std::string failed = ((pplay::Io *) main->getIo())->getLastError();
    if (!playlist && !saved && type != pplay::Io::DeviceType::Local && !failed.empty()) {
        main->getStatus()->show("Error", failed);
    }
    if (!hidden) {
        _files.erase(std::remove_if(_files.begin(), _files.end(),
                                    [](const Io::File &f) { return f.name.size() > 1 && f.name[0] == '.' && f.name != ".."; }),
                     _files.end());
    }
    int rank = -1;
    std::string cover;
    for (auto &file: _files) {
        int r = coverRank(file);
        if (r >= 0 && (rank < 0 || r < rank)) {
            rank = r;
            cover = file.path;
        }
        bool wanted = file.type != Io::Type::File || playlist || all;
        for (auto &e: ext) {
            wanted = wanted || Utility::endsWith(file.name, e, false);
        }
        if (wanted) {
            MediaFile media(file, MediaInfo(file));
            if (pplay::P2Play::isContinueWatchingPath(path)) {
                media.label = pplay::P2Play::progressLabel(file.path);
            }
            files.emplace_back(std::move(media));
        }
    }
    // smb:// is opened by mpv only
    bool smb = Utility::startWith(cover, "smb");
    folderCovers[path] = cover.empty() || smb ? "" : "dir:" + cover;
    details->setFolderCover(folderCovers[path]);

    // all the torrent files: by index, a name met twice with its directory
    if (pplay::TorrServe::isAllFiles(path)) {
        std::map<std::string, int> names;
        for (auto &file: files) {
            names[file.name]++;
        }
        for (auto &file: files) {
            if (names[file.name] > 1) {
                std::string dir = pplay::TorrServe::dirOf(file.path);
                file.label = file.name + " (" + (dir.empty() ? "/" : dir) + ")";
            }
        }
    } else if (!playlist && !pplay::P2Play::isSearchResultsPath(path) &&
               !pplay::P2Play::isContinueWatchingPath(path)) {
        std::sort(files.begin(), files.end(), compare);
        // a torrent root: its all files first
        std::stable_partition(files.begin(), files.end(),
                              [](const MediaFile &f) { return pplay::TorrServe::isAllFiles(f.path); });
    }
    std::stable_partition(files.begin(), files.end(), [](const MediaFile &f) {
        return pplay::P2Play::isSearchPromptPath(f.path);
    });

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
    queueDurations();
    for (size_t i = 0; i < files.size(); i++) {
        pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                            "Filer::file[" + std::to_string(i) + "] name=" + files[i].name + " path=" + files[i].path
                                    + " type=" + std::to_string((int) files[i].type));
    }
    top = 0;
    scrollOffset = 0;
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
        int module = main->getcurrentModuleIndex();
        return (module == MODULE_SAVED && pplay::Saved::isRoot(p))
               || (module == MODULE_PLAYLISTS && pplay::Playlists::isRoot(p));
    }
    std::string s = pplay::Paths::normalizeSmbPathSeparators(p);
    if (!Utility::endsWith(s, "/")) {
        s += "/";
    }
    for (int i = 1; i <= main->getConfig()->getNetworkCount(); i++) {
        std::string root = main->getConfig()->getOption(PPLAYConfig::networkOption(i))->getString();
        root = pplay::Paths::normalizeSmbPathSeparators(root);
        if (!root.empty() && !Utility::endsWith(root, "/")) {
            root += "/";
        }
        if (!root.empty() && s == root) {
            return true;
        }
    }
    return false;
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

    const std::string previousPath = path;
    if (!file.path.empty() && file.name != "..") {
        success = getDir(file.path);
    } else if (path == "/") {
        success = getDir(path + file.name);
    } else {
        success = getDir(path + "/" + file.name);
    }
    if (success) {
        path_prev.push_back(previousPath);
        item_index_prev.push_back(index);
        setSelection(item_index);
    }
}

void Filer::exit() {
    // if (path.empty()) return;
    // std::string p = path;
    const bool hasHistory = !path_prev.empty();
    std::string p = hasHistory ? path_prev.back() : path;

    // Textual parent traversal is only a fallback for paths that were not opened
    // through this filer (for example, a restored last location). Virtual filesystems
    // may expose routes whose semantic parent is not the preceding URL segment.
    if (!hasHistory) {
        if (isTopLevel(p)) {
            return;
        }
        while (!p.empty() && p.back() != '/') {
            p.erase(p.size() - 1);
        }
        if (p.size() > 1 && Utility::endsWith(p, "/")) {
            p.erase(p.size() - 1);
        }
    }

    if (!p.empty() && getDir(p)) {
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
        if (hasHistory) {
            path_prev.pop_back();
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
    path_prev.clear();
}

std::string Filer::getPath() {
    return path;
}
