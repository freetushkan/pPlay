//
// Main: the local / network / usb module shown, the last location saved.
//

#include <cstdlib>

#include "main.h"
#include "paths.h"

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
