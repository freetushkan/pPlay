//
// Filer directories: listing, navigation, file picking.
//

#include <algorithm>
#include "main.h"
#include "paths.h"
#include "filer.h"
#include "utility.h"

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
    pickExtensions = extensions;
    pickCallback = std::move(onPick);
    picking = true;
    preview->setFocused(false);
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
    std::vector<Io::File> _files = ((pplay::Io *) main->getIo())->getDirList(type, ext, path, timeout, false);

    for (auto &file: _files) {
        MediaFile mf(file, MediaInfo(file));
        files.emplace_back(mf);
    }

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
                            "Filer::file[" + std::to_string(i) + "] name=" + files[i].name + " path=" + files[i].path
                                    + " type=" + std::to_string((int) files[i].type));
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
