//
// DLNA / UPnP media servers: the ContentDirectory tree as a network.
//

#ifndef PPLAY_DLNA_H
#define PPLAY_DLNA_H

#include <string>
#include <vector>
#include "cross2d/c2d.h"

namespace pplay::Dlna {

    struct Server {
        std::string name;
        // its device description url: the network url
        std::string location;
    };

    // the media servers answering (SSDP) within these seconds
    std::vector<Server> search(int seconds);

    // a search in a thread (started again if done)
    void startSearch();

    // false while searching; the servers found
    bool searchResult(std::vector<Server> *servers);

    // a network path: <description url>/<container titles...>/<item title>
    std::vector<c2d::Io::File> list(const std::string &path, int timeout, std::string *error);

    // the server url of a listed file, "" if unknown
    std::string streamUrl(const std::string &path);

    // its picture (the item one, else its container one), "" if none
    std::string posterOf(const std::string &path);
}

#endif // PPLAY_DLNA_H
