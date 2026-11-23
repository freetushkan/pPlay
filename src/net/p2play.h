//
// P2Play MediaMesh network client. This module is intentionally separate from torrserve.*.
//
#ifndef PPLAY_P2PLAY_H
#define PPLAY_P2PLAY_H

#include <string>
#include <vector>

#include "cross2d/c2d.h"

namespace pplay::P2Play {

    // Browse discovered/subscribed MediaMesh lists and records as a virtual filesystem.
    std::vector<c2d::Io::File> getDirList(const std::string &path, int timeout, std::string *error = nullptr);

    // Convert a P2Play virtual file path into its authenticated native stream URL.
    std::string toStreamUrl(const std::string &path);

    // Called when a network's credentials or TLS policy change.
    void forgetFailures();
}

#endif // PPLAY_P2PLAY_H
