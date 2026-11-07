//
// Main: the local / network / usb module shown, the last location saved.
//

#include <cstdlib>

#include "main.h"
#include "paths.h"
#include "saved.h"
#include "playlists.h"

using namespace c2d;
using namespace pplay;

static int parseNetworkModule(const std::string &module) {
    if (module.rfind("NETWORK", 0) != 0) {
        return -1;
    }
    int index = std::atoi(module.substr(7).c_str());
    return index >= 1 && index <= PPLAY_MAX_NETWORKS ? index : 1;
}

static std::string networkModuleName(int index) {
    return "NETWORK" + std::to_string(index);
}

void Main::showLast() {
    setCurrentModuleIndex(parseNetworkModule(config->getOption(OPT_LAST_MODULE)->getString()));
#ifdef __SWITCH__
    if (config->getOption(OPT_LAST_MODULE)->getString() == "USB") {
        show(MenuType::Usb);
        return;
    }
#endif
    if (config->getOption(OPT_LAST_MODULE)->getString() == "SAVED") {
        show(MenuType::Saved);
        return;
    }
    if (config->getOption(OPT_LAST_MODULE)->getString() == "PLAYLISTS") {
        show(MenuType::Playlists);
        return;
    }
    show(currentModuleIndex > 0 ? MenuType::Network : MenuType::Local);
}

void Main::show(MenuType type) {
    if (player->getMpv()->isStopped() && player->isFullscreen()) {
        player->setFullscreen(false);
    }

    filer->setVisibility(Visibility::Visible, true);
    if (type == MenuType::Local) {
#ifdef __SWITCH__
        usbExit();
#endif
        currentModuleIndex = -1;
        std::string path = Paths::normalizePath(config->getOption(OPT_LAST_LOCAL_PATH)->getString());
        if (path.empty()) {
            path = config->getOption(OPT_HOME_PATH)->getString();
        }
        std::string dirPath = Paths::extractDirPath(path);
        if (!filer->getDir(dirPath)) {
            if (filer->getDir(config->getOption(OPT_HOME_PATH)->getString())) {
                filer->clearHistory();
            }
        } else {
            filer->selectByPath(path);
        }
    } else if (type == MenuType::Saved) {
#ifdef __SWITCH__
        usbExit();
#endif
        currentModuleIndex = MODULE_SAVED;
        std::string root = c2d::Utility::removeLastSlash(pplay::Saved::root());
        if (!pplayIo->exist(root)) {
            pplayIo->create(root);
        }
        std::string path = config->getOption(OPT_LAST_SAVED_PATH)->getString();
        if (!pplay::Saved::isInside(path)) {
            path = root;
        }
        std::string dir = Paths::extractDirPath(path);
        if (path == root || !pplay::Saved::hasDir(dir) || !filer->getDir(dir)) {
            filer->getDir(root);
        } else {
            filer->selectByPath(path);
        }
        filer->clearHistory();
    } else if (type == MenuType::Playlists) {
#ifdef __SWITCH__
        usbExit();
#endif
        currentModuleIndex = MODULE_PLAYLISTS;
        std::string root = pplay::Playlists::root();
        if (!pplayIo->exist(root)) {
            pplayIo->create(root);
        }
        std::string path = config->getOption(OPT_LAST_PLAYLISTS_PATH)->getString();
        std::string dir = Paths::extractDirPath(path);
        dir = c2d::Utility::removeLastSlash(dir);
        // the last playlist opened, else the list
        if (pplay::Playlists::isPlaylist(dir) && pplayIo->exist(dir) && filer->getDir(dir)) {
            filer->setSelection(std::atoi(Paths::getLeafName(path).c_str()));
        } else {
            filer->getDir(root);
            filer->selectByPath(path);
        }
        filer->clearHistory();
#ifdef __SWITCH__
    } else if (type == MenuType::Usb) {
        currentModuleIndex = -2;
        usbInit();
        usbWaitForDevice();
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Main::usb path=ums_list");
        if (!filer->getDir("ums_list")) {
            show(MenuType::Local);
            return;
        }
#endif
    } else {
#ifdef __SWITCH__
        usbExit();
#endif
        std::string network = config->getOption(PPLAYConfig::networkOption(currentModuleIndex))->getString();
        if (network.empty()) {
            show(MenuType::Local);
            return;
        }
        std::string root = Paths::ensureTrailingSlash(network);
        std::string path = config->getOption(PPLAYConfig::networkLastOption(currentModuleIndex))->getString();
        if (pplayIo->getDeviceType(path) == pplay::Io::DeviceType::Local || !c2d::Utility::startWith(path, root)
            || path.empty()) {
            path = root;
        }
        std::string dirPath = Paths::extractDirPath(path);
        if (!filer->getDir(dirPath)) {
            if (!filer->getDir(root)) {
                showMessage(pplay::tr("ERROR"), filer->getError(), pplay::tr("OK"));
                show(MenuType::Local);
            }
        } else {
            filer->selectByPath(path);
        }
        filer->clearHistory();
    }
}

int Main::moduleOf(const std::string &path) {
    if (pplayIo->getDeviceType(path) == pplay::Io::DeviceType::Local) {
        return pplay::Saved::isInside(path) ? MODULE_SAVED : -1;
    }
    for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
        std::string network = config->getOption(PPLAYConfig::networkOption(i))->getString();
        if (!network.empty() && c2d::Utility::startWith(path, Paths::ensureTrailingSlash(network))) {
            return i;
        }
    }
    return -1;
}

void Main::closeForRest() {
    quit();
}

void Main::keepSession() {
    std::string session;
    if (!player->getMpv()->isStopped()) {
        player->keepProgressNow();
        session = std::string(player->isFullscreen() ? "1" : "0") + "|" + player->getFile().path;
        // the last location: the playing file (the list may show another one)
        std::string path = session.substr(2);
        int module = moduleOf(path);
        if (module > 0) {
            config->getOption(OPT_LAST_MODULE)->setString(networkModuleName(module));
            config->getOption(PPLAYConfig::networkLastOption(module))->setString(path);
        } else if (module == MODULE_SAVED) {
            config->getOption(OPT_LAST_MODULE)->setString("SAVED");
            config->getOption(OPT_LAST_SAVED_PATH)->setString(path);
        } else {
            config->getOption(OPT_LAST_MODULE)->setString("LOCAL");
            config->getOption(OPT_LAST_LOCAL_PATH)->setString(path);
        }
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Main: session kept: " + path);
    }
    config->getOption(OPT_REST_SESSION)->setString(session);
}

void Main::restoreSession() {
    std::string session = config->getOption(OPT_REST_SESSION)->getString();
    if (session.size() < 3) {
        return;
    }
    // once: a file failing here is not retried at each start
    config->getOption(OPT_REST_SESSION)->setString("");
    config->save();
    bool fullscreen = session[0] == '1';
    std::string path = session.substr(2);
    MediaFile file = filer->getSelection();
    // gone meanwhile (a removed torrent, an unreachable share): the list only
    if (file.path != path || !pplay::Utility::isMedia(file)) {
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Main: session not restored: " + path);
        return;
    }
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Main: session restored: " + path);
    // its position: the usual choice (local / TorrServer, asked now)
    player->setFullscreen(true);
    player->loadPausedOnce();
    player->load(file);
    if (!fullscreen) {
        player->setFullscreen(false);
    }
}

void Main::syncLastLocation() {
    MediaFile selected = filer->getSelection();
    std::string selectedPath = selected.path.empty() ? filer->getPath() + "/.." : selected.path;
    std::string path = Paths::normalizePath(selectedPath);

    if (currentModuleIndex > 0) {
        pplay::Utility::log(
                pplay::Utility::LogLevel::Debug,
                "Main::syncLastLocation module="
                        + std::string(currentModuleIndex > 0 ? networkModuleName(currentModuleIndex) : "LOCAL")
                        + " index=" + std::to_string(currentModuleIndex) + " path=" + path);

        config->getOption(OPT_LAST_MODULE)->setString(networkModuleName(currentModuleIndex));
        const char *lastOption = PPLAYConfig::networkLastOption(currentModuleIndex);
        if (Paths::getLeafName(path).empty()) {
            config->getOption(lastOption)->setString(Paths::ensureTrailingSlash(path));
        } else {
            config->getOption(lastOption)->setString(path);
        }
    } else if (currentModuleIndex == MODULE_SAVED) {
        config->getOption(OPT_LAST_MODULE)->setString("SAVED");
        config->getOption(OPT_LAST_SAVED_PATH)->setString(path);
    } else if (currentModuleIndex == MODULE_PLAYLISTS) {
        config->getOption(OPT_LAST_MODULE)->setString("PLAYLISTS");
        // an entry: by its playlist (its own path may be anywhere)
        std::string list = filer->getPath();
        config->getOption(OPT_LAST_PLAYLISTS_PATH)->setString(
                pplay::Playlists::isPlaylist(list) ? list + "/" + std::to_string(filer->getSelectionIndex()) : path);
#ifdef __SWITCH__
    } else if (c2d::Utility::startWith(selectedPath, "ums")) {
        pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Main::syncLastLocation module=USB");

        config->getOption(OPT_LAST_MODULE)->setString("USB");
#endif
    } else {
        pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Main::syncLastLocation module=LOCAL path=" + path);

        config->getOption(OPT_LAST_MODULE)->setString("LOCAL");
        if (Paths::getLeafName(path).empty()) {
            config->getOption(OPT_LAST_LOCAL_PATH)->setString(Paths::ensureTrailingSlash(path));
        } else {
            config->getOption(OPT_LAST_LOCAL_PATH)->setString(path);
        }
    }
    config->save();
}
