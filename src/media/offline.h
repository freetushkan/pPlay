//
// Offline listings: the last listing of each network directory written to
// <home>/.pplay_save/.lists/<16 hex> (named by a hash of its path), shown only when the
// network can't list it (the status bar shows it then).
//

#ifndef PPLAY_OFFLINE_H
#define PPLAY_OFFLINE_H

#include <string>
#include <vector>
#include "cross2d/c2d.h"

namespace pplay::Offline {

    // a listing done: written if new or changed, online again
    void store(const std::string &path, const std::vector<c2d::Io::File> &files);

    // the network failed: the kept listing if any (offline then)
    bool load(const std::string &path, std::vector<c2d::Io::File> *files);

    // the last network listing was the kept one
    bool active();
}

#endif //PPLAY_OFFLINE_H
