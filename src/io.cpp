//
// Created by cpasjuste on 31/03/19.
//

#include <algorithm>
#include <regex>
#include "io.h"
#include "main.h"
#include "media_info.h"
#include "utility.h"
#include "ftplib.h"
#include "Browser/Browser.hpp"
#include "Browser/json.hpp"
#include "pplay_config.h"
#include "torrserve.h"

using namespace pplay;


#ifdef __SMB2__
struct SmbUrlParts {
    std::string domain;
    std::string user;
    std::string password;
    std::string server;
    int port = 445;
    std::string path;
    std::string libsmbUrl;
    std::string displayUrl;
};

static size_t findFirstOf(const std::string &str, const std::string &chars, size_t from) {
    size_t pos = std::string::npos;
    for (char ch: chars) {
        size_t p = str.find(ch, from);
        if (p != std::string::npos && (pos == std::string::npos || p < pos)) {
            pos = p;
        }
    }
    return pos;
}

static std::string normalizeSeparators(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

static SmbUrlParts parseSmbUrl(const std::string &input) {
    SmbUrlParts parts;
    std::string smbPath = input;
    if (!c2d::Utility::endsWith(smbPath, "/") && !c2d::Utility::endsWith(smbPath, "\\")) {
        smbPath += "/";
    }

    const std::string scheme = "smb://";
    size_t authStart = scheme.size();
    size_t at = smbPath.find_last_of('@');
    if (at != std::string::npos && at >= authStart) {
        std::string auth = smbPath.substr(authStart, at - authStart);
        size_t colon = auth.find(':');
        std::string userWithDomain = colon == std::string::npos ? auth : auth.substr(0, colon);
        if (colon != std::string::npos) {
            parts.password = auth.substr(colon + 1);
        }

        size_t domainSep = userWithDomain.find('.');
        if (domainSep != std::string::npos) {
            parts.domain = userWithDomain.substr(0, domainSep);
            parts.user = userWithDomain.substr(domainSep + 1);
        } else {
            parts.user = userWithDomain;
        }
    } else {
        at = std::string::npos;
    }

    size_t serverStart = at == std::string::npos ? authStart : at + 1;
    size_t pathStart = findFirstOf(smbPath, "/\\", serverStart);
    std::string hostPort;
    if (pathStart == std::string::npos) {
        hostPort = smbPath.substr(serverStart);
        parts.path = "/";
    } else {
        hostPort = smbPath.substr(serverStart, pathStart - serverStart);
        parts.path = normalizeSeparators(smbPath.substr(pathStart));
    }

    size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos) {
        parts.server = hostPort.substr(0, colon);
        try {
            parts.port = std::stoi(hostPort.substr(colon + 1));
        } catch (...) {
            parts.port = 445;
        }
    } else {
        parts.server = hostPort;
        parts.port = 445;
    }

    parts.displayUrl = scheme;
    if (!parts.user.empty()) {
        if (!parts.domain.empty()) {
            parts.displayUrl += parts.domain + ".";
        }
        parts.displayUrl += parts.user;
        if (!parts.password.empty()) {
            parts.displayUrl += ":" + parts.password;
        }
        parts.displayUrl += "@";
    }
    parts.displayUrl += parts.server + parts.path;

    parts.libsmbUrl = scheme;
    if (!parts.user.empty()) {
        if (!parts.domain.empty()) {
            parts.libsmbUrl += parts.domain + ";";
        }
        parts.libsmbUrl += parts.user + "@";
    }
    parts.libsmbUrl += parts.server + ":" + std::to_string(parts.port) + parts.path;

    return parts;
}
#endif

static size_t find_Nth(const std::string &str, unsigned int n, const std::string &find) {
    size_t pos = std::string::npos, from = 0;
    unsigned int i = 0;

    if (n == 0) {
        return std::string::npos;
    }

    while (i < n) {
        pos = str.find(find, from);
        if (std::string::npos == pos) { break; }
        from = pos + 1;
        ++i;
    }
    return pos;
}

Io::Io() : c2d::C2DIo() {

    // http io
    browser = new Browser();
    browser->set_handle_gzip(true);
    browser->set_handle_redirect(true);
    browser->fetch_forms(false);

    // ftp io
    FtpInit();
}

std::vector<c2d::Io::File> Io::getDirList(const pplay::Io::DeviceType &type, const std::vector<std::string> &extensions,
                                          const std::string &path, int timeout, bool sort, bool showHidden) {

    std::vector<c2d::Io::File> files;

    printf("Io::getDirList(%s)\n", path.c_str());
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Io::getDirList path=" + path);

    if (type == DeviceType::Local) {
        files = c2d::C2DIo::getDirList(path, sort, showHidden);
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Io::Local path=" + path
                                                            + " entries=" + std::to_string(files.size()));
    } else if (type == DeviceType::Http) {
        std::string http_path = path;
        if (!c2d::Utility::endsWith(http_path, "/")) {
            http_path += "/";
        }
        // extract home from path
        size_t pos = find_Nth(http_path, 3, "/");
        std::string home = http_path.substr(0, pos + 1);
        std::string dir = browser->escape(http_path.substr(pos + 1, http_path.length() - 1));
        dir = std::regex_replace(dir, std::regex("%2F"), "/");
        //printf("home: %s | dir: %s\n", home.c_str(), dir.c_str());
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Io::Browser->open url=" + home + dir);
        browser->open(home + dir, timeout);
        pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Io::Browser->open finished.");
        if (browser->error() || browser->links.size() < 1) {
            return files;
        }

        // add up/back ("..")
        files.emplace_back("..", "..", Io::Type::Directory, 0);

        for (int i = 0; i < browser->links.size(); i++) {
            // skip apache2 stuff
            if (browser->links[i].name() == ".."
                || browser->links[i].name() == "../"
                || browser->links[i].name() == "Name"
                || browser->links[i].name() == "Last modified"
                || browser->links[i].name() == "Size"
                || browser->links[i].name() == "Description"
                || browser->links[i].name() == "Parent Directory") {
                continue;
            }

            Io::Type t = c2d::Utility::endsWith(browser->links[i].name(), "/") ?
                         Io::Type::Directory : Io::Type::File;
            std::string name = browser->unescape(browser->links[i].name());
            if (c2d::Utility::endsWith(name, "/")) {
                name = c2d::Utility::removeLastSlash(name);
            }
            files.emplace_back(name, http_path + name, t);
        }
        if (sort) {
            std::sort(files.begin(), files.end(), compare);
        }
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Io::Browser path=" + path
                                                            + " entries=" + std::to_string(files.size()));
    } else if (type == DeviceType::TorrServe) {
        files = pplay::TorrServe::getDirList(browser, path, timeout);
        if (sort) {
            std::sort(files.begin(), files.end(), compare);
        }
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Io::TorrServe path=" + path
                                                            + " entries=" + std::to_string(files.size()));
    } else if (type == DeviceType::Ftp) {
        std::string ftp_path = path;
        if (!c2d::Utility::endsWith(ftp_path, "/")) {
            ftp_path += "/";
        }
        // split user/pwd/host/port/path
        // TODO: check for nullptr etc..
        size_t colon_2 = find_Nth(ftp_path, 2, ":");
        size_t colon_3 = ftp_path.find_last_of(':');
        size_t at = ftp_path.find_last_of('@');
        size_t last_slash = find_Nth(ftp_path, 3, "/");
        std::string user = ftp_path.substr(6, colon_2 - 6);
        std::string pwd = ftp_path.substr(colon_2 + 1, at - colon_2 - 1);
        std::string host = ftp_path.substr(at + 1, colon_3 - at - 1);
        std::string port = ftp_path.substr(colon_3 + 1, ftp_path.find('/', colon_3) - (colon_3 + 1));
        std::string host_port = host + ":" + port;
        std::string new_path = ftp_path.substr(last_slash, ftp_path.length() - last_slash);
        if (c2d::Utility::startWith(new_path, "/")) {
            new_path.erase(0, 1);
        }

        printf("Io::getDirList: user: %s, pwd: %s, host: %s, port: %s, path: %s\n",
               user.c_str(), pwd.c_str(), host.c_str(), port.c_str(), new_path.c_str());

        netbuf *ftp_con = nullptr;
        if (!FtpConnect(host_port.c_str(), &ftp_con)) {
            printf("Io::getDirList: could not connect to ftp server");
            return files;
        }

        if (!FtpLogin(user.c_str(), pwd.c_str(), ftp_con)) {
            printf("Io::getDirList: could not connect to ftp server");
            FtpQuit(ftp_con);
            return files;
        }

        std::vector<Io::File> _files = FtpDirList(new_path.c_str(), ftp_con);
        _files.insert(_files.begin(), Io::File("..", "..", Io::Type::Directory, 0));
        for (auto &file: _files) {
            if (file.path != "..") {
                file.path = ftp_path + file.name;
            }
            files.push_back(file);
        }

        FtpQuit(ftp_con);
    }
#ifdef __SMB2__
    else if (type == DeviceType::Smb) {
        SmbUrlParts smbUrl = parseSmbUrl(path);
        std::string smb_path = smbUrl.displayUrl;
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
            std::string("Io::getDirList: type=smb path=") + path);

        smb2 = smb2_init_context();
        if (!smb2) {
            printf("Io::getDirList: failed to init smb2 context\n");
            pplay::Utility::log(pplay::Utility::LogLevel::Error,
                "Io::getDirList: failed to init smb2 context");
            return files;
        }

        smb2_set_timeout(smb2, timeout);
        if (!smbUrl.domain.empty()) {
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                "Io::getDirList::smb2 set domain");
            smb2_set_domain(smb2, smbUrl.domain.c_str());
        }
        if (!smbUrl.user.empty()) {
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                "Io::getDirList::smb2 set user");
            smb2_set_user(smb2, smbUrl.user.c_str());
        }
        if (!smbUrl.password.empty()) {
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                "Io::getDirList::smb2 set password");
            smb2_set_password(smb2, smbUrl.password.c_str());
        }
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
            std::string("Io::getDirList::smb2 libsmbUrl=") + smbUrl.libsmbUrl);

        smb2_url *url = smb2_parse_url(smb2, smbUrl.libsmbUrl.c_str());
        if (!url) {
            printf("Io::getDirList: failed to parse url: %s\n", smb2_get_error(smb2));
            pplay::Utility::log(pplay::Utility::LogLevel::Error,
                std::string("Io::getDirList: failed to init smb2 context")
                + std::string(smb2_get_error(smb2)));
            smb2_destroy_context(smb2);
            return files;
        }

        printf("Io::getDirList: domain: %s, user: %s, host: %s, share: %s, path: %s\n",
               smbUrl.domain.c_str(), smbUrl.user.c_str(), url->server, url->share, url->path ? url->path : "");

        // set security
        smb2_set_security_mode(smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
            "Io::getDirList: connecting to server=" + std::string(url->server) +
            ", port=" + std::to_string(smbUrl.port > 0 ? smbUrl.port : 445) +
            ", share=" + std::string(url->share) +
            ", path=" + std::string(url->path) +
            ", path_len=" + std::to_string(url->path ? strlen(url->path) : -1) +
            ", user=" + std::string(url->user) +
            ", pass=" + smbUrl.password +
            ", domain=" + smbUrl.domain);
        if (smb2_connect_share(smb2, url->server, url->share, url->user) < 0) {
            printf("Io::getDirList: smb2_connect_share failed: %s\n", smb2_get_error(smb2));
            pplay::Utility::log(pplay::Utility::LogLevel::Error,
                std::string("Io::getDirList: smb2_connect_share failed: ") + smb2_get_error(smb2));
            smb2_destroy_url(url);
            smb2_destroy_context(smb2);
            return files;
        }

        // open dir
        const char *dirPath = url->path && url->path[0] ? url->path : "";
        pplay::Utility::log(pplay::Utility::LogLevel::Info, 
            "Io::getDirList: connected successfully, opening dir: '" + std::string(dirPath) + "'");
        smb2dir *dir = smb2_opendir(smb2, dirPath);
        if (!dir) {
            printf("Io::getDirList: smb2_opendir failed: %s\n", smb2_get_error(smb2));
            pplay::Utility::log(pplay::Utility::LogLevel::Error,
                std::string("Io::getDirList: smb2_opendir failed: ") + smb2_get_error(smb2));
            // cleanup
            smb2_destroy_url(url);
            smb2_disconnect_share(smb2);
            smb2_destroy_context(smb2);
            return files;
        }

        // get dir list
        smb2dirent *ent;
        int count = 0;
        while ((ent = smb2_readdir(smb2, dir))) {
            bool isParentEntry = ent->name[0] == '.'
                                 && (ent->name[1] == '\0'
                                     || (ent->name[1] == '.' && ent->name[2] == '\0'));
            if (isParentEntry) {
                continue;
            }
            count++;
            switch (ent->st.smb2_type) {
                case SMB2_TYPE_FILE:
                    files.emplace_back(ent->name, smb_path + ent->name,
                                       Io::Type::File, ent->st.smb2_size);
                    break;
                case SMB2_TYPE_DIRECTORY:
                    files.emplace_back(ent->name, smb_path + ent->name, Io::Type::Directory);
                    break;
                case SMB2_TYPE_LINK:
                default:
                    break;
            }
        }

        pplay::Utility::log(pplay::Utility::LogLevel::Info, 
            "Io::getDirList: finished reading, entries found: " + std::to_string(count));
        // cleanup
        smb2_destroy_url(url);
        smb2_closedir(smb2, dir);
        smb2_disconnect_share(smb2);
        smb2_destroy_context(smb2);
    }
#endif
    // remove items by extensions, if provided
    if (!extensions.empty()) {
        files.erase(
                std::remove_if(files.begin(), files.end(), [extensions](const Io::File &file) {
                    for (auto &ext: extensions) {
                        if (c2d::Utility::endsWith(file.name, ext, false)) {
                            return false;
                        }
                    }
                    return file.type == c2d::Io::Type::File;
                }), files.end());
    }

    return files;
}

Io::DeviceType Io::getDeviceType(const std::string &path) {

    Io::DeviceType type = Io::DeviceType::Local;

    if (c2d::Utility::startWith(path, "http://")) {
        type = pplay::Io::DeviceType::Http;
    } else if (c2d::Utility::startWith(path, "https://")) {
        type = pplay::Io::DeviceType::Http;
    } else if (c2d::Utility::startWith(path, "ftp://")) {
        type = pplay::Io::DeviceType::Ftp;
    } else if (c2d::Utility::startWith(path, "smb://")) {
        type = pplay::Io::DeviceType::Smb;
    } else if (c2d::Utility::startWith(path, "tss://")) {
        type = pplay::Io::DeviceType::TorrServe;
    } else if (c2d::Utility::startWith(path, "ts://")) {
        type = pplay::Io::DeviceType::TorrServe;
    }

    return type;
}

Io::~Io() {
    delete (browser);
}
