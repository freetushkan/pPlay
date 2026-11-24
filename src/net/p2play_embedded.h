//
// Embedded P2Play core lifecycle. The module talks only to the P2Play C API.
//
#ifndef PPLAY_P2PLAY_EMBEDDED_H
#define PPLAY_P2PLAY_EMBEDDED_H

#include <string>

namespace pplay::P2PlayEmbedded {
    bool activate(const std::string &dataDir, const std::string &ownerPassword);
    void deactivate();
    void update();
    bool isReady();
    void shutdown();
}

#endif // PPLAY_P2PLAY_EMBEDDED_H
