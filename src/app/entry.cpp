//
// pPlay entry point: platform, data directory, configuration and language are set
// up before the renderer.
//

#include "main.h"
#include "io.h"
#include "i18n.h"
#include "net_time.h"
#include "platform.h"
#include "pplay_config.h"
#include "utility.h"


int main() {
    if (!pplay::Platform::init()) {
        return 0;
    }

    auto *io = new pplay::Io();
    io->create(io->getDataPath());
    pplay::Utility::setDataPath(io->getDataPath());
#if defined(__PS4__) || defined(__SWITCH__)
    // romfs (skin, mpv, lang...) -> data directory
    io->syncRomFs();
#endif

    auto *config = new PPLAYConfig(io);
    io->setConfig(config);
    pplay::Utility::setLogLevel((pplay::Utility::LogLevel) config->getOption(OPT_LOG_LEVEL)->getInteger());
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "pPlay " APP_VERSION " starting");

    // bundled languages, then the user ones
    pplay::I18n::setSearchPaths({io->getRomFsPath() + "lang", io->getDataPath() + "lang"});
    pplay::I18n::load(config->getOption(OPT_LANGUAGE)->getString());

    c2d::Vector2f size = pplay::Platform::resolution();

    auto *main = new Main(size, io, config);
    pplay::Platform::started(main);
    pplay::NetTime::start();

    while (main->isRunning()) {
        main->flip();
        pplay::Platform::frameDone(main);
    }

    pplay::NetTime::stop();
    delete (main);

    pplay::Platform::exit();

    return 0;
}
