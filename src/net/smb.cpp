//
// SMB2/3 (libsmb2): url parsing and directory listing.
//

#ifdef __SMB2__

#include <algorithm>
#include <cstdlib>

#include "smb.h"
#include "utility.h"

using namespace pplay;

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

Smb::Url Smb::parseUrl(const std::string &input) {
    Url parts;
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
        parts.port = 445;
        const std::string portText = hostPort.substr(colon + 1);
        if (!portText.empty()) {
            char *end = nullptr;
            long parsedPort = std::strtol(portText.c_str(), &end, 10);
            if (end != portText.c_str() && *end == '\0' && parsedPort > 0 && parsedPort <= 65535) {
                parts.port = static_cast<int>(parsedPort);
            }
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

std::vector<c2d::Io::File> Smb::list(const std::string &path, int timeout, std::string *error) {
    using Io = c2d::Io;
    std::vector<Io::File> files;
    Url smbUrl = parseUrl(path);
    const std::string smb_path = smbUrl.displayUrl;

    auto setError = [error](const std::string &message) {
        Utility::log(Utility::LogLevel::Error, "Smb::list: " + message);
        if (error) {
            *error = "SMB: " + message;
        }
    };

    struct smb2_context *smb2 = smb2_init_context();
    if (!smb2) {
        setError("failed to init smb2 context");
        return files;
    }

    smb2_set_timeout(smb2, timeout);
    smb2_set_security_mode(smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);
    if (!smbUrl.domain.empty()) {
        smb2_set_domain(smb2, smbUrl.domain.c_str());
    }
    if (!smbUrl.user.empty()) {
        smb2_set_user(smb2, smbUrl.user.c_str());
    }
    if (!smbUrl.password.empty()) {
        smb2_set_password(smb2, smbUrl.password.c_str());
    }

    smb2_url *url = smb2_parse_url(smb2, smbUrl.libsmbUrl.c_str());
    if (!url) {
        setError(std::string("invalid url: ") + smb2_get_error(smb2));
        smb2_destroy_context(smb2);
        return files;
    }

    Utility::log(Utility::LogLevel::Info,
                 "Smb::list: server=" + std::string(url->server ? url->server : "") + " port="
                         + std::to_string(smbUrl.port) + " share=" + std::string(url->share ? url->share : "")
                         + " path=" + std::string(url->path ? url->path : "")
                         + " user=" + std::string(url->user ? url->user : "") + " domain=" + smbUrl.domain);

    if (smb2_connect_share(smb2, url->server, url->share, url->user) < 0) {
        setError(std::string("connection failed: ") + smb2_get_error(smb2));
        smb2_destroy_url(url);
        smb2_destroy_context(smb2);
        return files;
    }

    const char *dirPath = url->path && url->path[0] ? url->path : "";
    smb2dir *dir = smb2_opendir(smb2, dirPath);
    if (!dir) {
        setError(std::string("can't open directory: ") + smb2_get_error(smb2));
        smb2_destroy_url(url);
        smb2_disconnect_share(smb2);
        smb2_destroy_context(smb2);
        return files;
    }

    smb2dirent *ent;
    while ((ent = smb2_readdir(smb2, dir))) {
        bool isParentEntry =
                ent->name[0] == '.' && (ent->name[1] == '\0' || (ent->name[1] == '.' && ent->name[2] == '\0'));
        if (isParentEntry) {
            continue;
        }
        switch (ent->st.smb2_type) {
            case SMB2_TYPE_FILE:
                files.emplace_back(ent->name, smb_path + ent->name, Io::Type::File, ent->st.smb2_size);
                break;
            case SMB2_TYPE_DIRECTORY:
                files.emplace_back(ent->name, smb_path + ent->name, Io::Type::Directory);
                break;
            default:
                break;
        }
    }
    Utility::log(Utility::LogLevel::Info, "Smb::list: entries=" + std::to_string(files.size()));

    smb2_closedir(smb2, dir);
    smb2_destroy_url(url);
    smb2_disconnect_share(smb2);
    smb2_destroy_context(smb2);
    return files;
}

#endif
