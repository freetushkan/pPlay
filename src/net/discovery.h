//
// Servers of the local network for a network type: DLNA (SSDP), TorrServer and Samba (the
// addresses of this /24 tried).
//

#ifndef PPLAY_DISCOVERY_H
#define PPLAY_DISCOVERY_H

#include <string>
#include <vector>
#include "io.h"

namespace pplay::Discovery {

    struct Server {
        // the network name
        std::string name;
        // as listed (the address too)
        std::string label;
        std::string url;
    };

    // a type with a search
    bool supports(Io::NetworkType type);

    // a search in a thread
    void start(Io::NetworkType type);

    // the servers of the last search of this type, false while one runs (another type: started)
    bool result(Io::NetworkType type, std::vector<Server> *servers);
}

#endif // PPLAY_DISCOVERY_H
