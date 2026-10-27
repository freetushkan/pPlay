//
// FTP directory listing (ftplib).
//

#include <cstdio>
#include "ftplib.h"
#include "ftp.h"
#include "utility.h"

using namespace pplay;

void Ftp::init() {
    FtpInit();
}

std::vector<c2d::Io::File> Ftp::list(const std::string &path, int timeout, std::string *error) {
    using Io = c2d::Io;
    std::vector<Io::File> files;
    (void) timeout;

    auto setError = [error](const std::string &message) {
        Utility::log(Utility::LogLevel::Error, "Ftp::list: " + message);
        if (error) {
            *error = "FTP: " + message;
        }
    };

    // ftp://[user[:password]@]host[:port]/path
    std::string ftp_path = path;
    if (!c2d::Utility::endsWith(ftp_path, "/")) {
        ftp_path += "/";
    }
    const std::string scheme = "ftp://";
    if (!c2d::Utility::startWith(ftp_path, scheme)) {
        setError("invalid url");
        return files;
    }
    size_t hostStart = scheme.size();
    size_t slash = ftp_path.find('/', hostStart);
    std::string authority = ftp_path.substr(hostStart, slash - hostStart);
    std::string dir = ftp_path.substr(slash + 1);

    std::string user = "anonymous", password = "anonymous@";
    size_t at = authority.rfind('@');
    if (at != std::string::npos) {
        std::string userInfo = authority.substr(0, at);
        authority = authority.substr(at + 1);
        size_t colon = userInfo.find(':');
        user = colon == std::string::npos ? userInfo : userInfo.substr(0, colon);
        password = colon == std::string::npos ? "" : userInfo.substr(colon + 1);
    }
    std::string hostPort = authority;
    if (hostPort.find(':') == std::string::npos) {
        hostPort += ":21";
    }

    Utility::log(Utility::LogLevel::Info, "Ftp::list host=" + hostPort + " user=" + user + " path=" + dir);

    netbuf *ftp_con = nullptr;
    if (!FtpConnect(hostPort.c_str(), &ftp_con)) {
        setError("could not connect to " + hostPort);
        return files;
    }

    if (!FtpLogin(user.c_str(), password.c_str(), ftp_con)) {
        setError("login failed");
        FtpQuit(ftp_con);
        return files;
    }

    std::vector<Io::File> entries = FtpDirList(dir.c_str(), ftp_con);
    for (auto &file: entries) {
        if (file.name == "." || file.name == "..") {
            continue;
        }
        file.path = ftp_path + file.name;
        files.push_back(file);
    }

    FtpQuit(ftp_con);
    return files;
}
