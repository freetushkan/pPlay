//
// SMB2/3 (libsmb2): directory listing and mpv stream callbacks ("smb2://" urls).
//

#ifndef PPLAY_NET_SMB_H
#define PPLAY_NET_SMB_H

#ifdef __SMB2__

#include <string>
#include <vector>
#include "cross2d/c2d.h"

#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-raw.h"

namespace pplay::Smb {

    struct Url {
        std::string domain;
        std::string user;
        std::string password;
        std::string server;
        int port = 445;
        std::string path;
        std::string libsmbUrl;
        std::string displayUrl;
    };

    // smb://[domain.]user:password@server[:port]/share/path
    Url parseUrl(const std::string &url);

    std::vector<c2d::Io::File> list(const std::string &path, int timeout, std::string *error = nullptr);

    // mpv "smb2://" stream protocol
    void configureStream(int bufferMB, int timeout);

    int registerStream(void *mpv);
}

#endif

#endif //PPLAY_NET_SMB_H
