//
// FTP directory listing (ftplib).
//

#ifndef PPLAY_NET_FTP_H
#define PPLAY_NET_FTP_H

#include <string>
#include <vector>
#include "cross2d/c2d.h"

namespace pplay::Ftp {
    void init();

    // ftp://user:password@host:port/path
    std::vector<c2d::Io::File> list(const std::string &path, int timeout, std::string *error = nullptr);
}

#endif //PPLAY_NET_FTP_H
