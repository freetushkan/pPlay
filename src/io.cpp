//
// Created by cpasjuste on 31/03/19.
//

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <regex>
#include <sstream>
#include <vector>
#include "io.h"
#include "main.h"
#include "media_info.h"
#include "utility.h"
#include "ftplib.h"
#include "Browser/Browser.hpp"
#include "Browser/json.hpp"
#include "pplay_config.h"
#include "torrserve.h"

#ifdef __SMB2__
#include <mpv/client.h>
#include <mpv/stream_cb.h>
#endif

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

// MPV STREAM CALLBACKS
typedef struct {
    struct smb2_context *smb2;
    struct smb2fh *fh;
    int64_t file_size;
    uint64_t pos;
    uint32_t max_read_size;
    uint32_t read_buffer_size;
    std::vector<uint8_t> read_buffer;
    uint64_t buffer_start;
    size_t buffer_len;
} Smb2MpvCtx;

struct Smb2MpvSettings {
    uint32_t read_buffer_size;
    int timeout_seconds;
};

static Smb2MpvSettings smb2_mpv_settings = {1024 * 1024, 60};

static uint32_t clamp_smb_read_buffer_mb(int readBufferMB) {
    if (readBufferMB <= 0) {
        return 0;
    }
    constexpr int maxReadBufferMB = 100;
    return (uint32_t)std::min(readBufferMB, maxReadBufferMB) * 1024 * 1024;
}

void configure_smb_mpv(int readBufferMB, int timeoutSeconds) {
    smb2_mpv_settings.read_buffer_size = clamp_smb_read_buffer_mb(readBufferMB);
    smb2_mpv_settings.timeout_seconds = timeoutSeconds > 0 ? timeoutSeconds : 60;

    pplay::Utility::log(
        pplay::Utility::LogLevel::Info,
        "configure_smb_mpv: read_buffer_size=" + std::to_string(smb2_mpv_settings.read_buffer_size) +
        " timeout_seconds=" + std::to_string(smb2_mpv_settings.timeout_seconds)
    );
}

static std::string ptr_to_str(const void *p) {
    std::ostringstream oss;
    oss << p;
    return oss.str();
}

static std::string safe_cstr(const char *s) {
    return s ? s : "(null)";
}

static void smb2_dbg(const std::string &msg) {
    pplay::Utility::log(pplay::Utility::LogLevel::Debug, msg);
}


static int64_t smb2_read_at(Smb2MpvCtx *ctx, uint8_t *dst, uint64_t requested, uint64_t offset) {
    uint64_t total = 0;

    while (total < requested) {
        uint64_t chunk = std::min<uint64_t>(requested - total, std::numeric_limits<uint32_t>::max());
        if (ctx->max_read_size > 0) {
            chunk = std::min<uint64_t>(chunk, ctx->max_read_size);
        }

        int ret = smb2_pread(ctx->smb2, ctx->fh, dst + total, (uint32_t)chunk, offset + total);
        smb2_dbg("smb2_read_at: offset=" + std::to_string(offset + total) +
                 " count=" + std::to_string(chunk) +
                 " ret=" + std::to_string(ret));

        if (ret < 0) {
            return total > 0 ? (int64_t)total : -1;
        }
        if (ret == 0) {
            break;
        }

        total += (uint64_t)ret;
    }

    return (int64_t)total;
}

static bool smb2_try_read_from_buffer(Smb2MpvCtx *ctx, char *buf, uint64_t nbytes, int64_t *result) {
    if (ctx->buffer_len == 0 || ctx->pos < ctx->buffer_start) {
        return false;
    }

    uint64_t buffer_offset = ctx->pos - ctx->buffer_start;
    if (buffer_offset >= ctx->buffer_len) {
        return false;
    }

    uint64_t available = ctx->buffer_len - buffer_offset;
    size_t copied = (size_t)std::min<uint64_t>(nbytes, available);
    std::memcpy(buf, ctx->read_buffer.data() + buffer_offset, copied);
    ctx->pos += copied;
    *result = (int64_t)copied;

    smb2_dbg("smb2_mpv_read_cb: served from buffer offset=" + std::to_string(buffer_offset) +
             " copied=" + std::to_string(copied) +
             " remaining=" + std::to_string(ctx->buffer_len - (buffer_offset + copied)));

    return true;
}

static int64_t smb2_mpv_read_cb(void *cookie, char *buf, uint64_t nbytes) {
    Smb2MpvCtx *ctx = (Smb2MpvCtx *)cookie;

    smb2_dbg("smb2_mpv_read_cb: cookie=" + ptr_to_str(cookie) +
             " ctx=" + ptr_to_str(ctx) +
             " fh=" + (ctx ? ptr_to_str(ctx->fh) : "(null)") +
             " pos=" + (ctx ? std::to_string(ctx->pos) : "(null)") +
             " nbytes=" + std::to_string(nbytes));

    if (!ctx || !ctx->fh) {
        smb2_dbg("smb2_mpv_read_cb: invalid ctx/fh");
        return -1;
    }

    if (nbytes == 0) {
        return 0;
    }

    int64_t buffered = 0;
    if (smb2_try_read_from_buffer(ctx, buf, nbytes, &buffered)) {
        return buffered;
    }

    uint64_t request_size = nbytes;
    if (ctx->read_buffer_size > 0) {
        request_size = std::max<uint64_t>(request_size, ctx->read_buffer_size);
    }

    if (ctx->file_size >= 0 && ctx->pos < (uint64_t)ctx->file_size) {
        request_size = std::min<uint64_t>(request_size, (uint64_t)ctx->file_size - ctx->pos);
    }

    if (ctx->read_buffer_size > 0 && request_size > nbytes) {
        ctx->read_buffer.resize((size_t)request_size);
        int64_t ret = smb2_read_at(ctx, ctx->read_buffer.data(), request_size, ctx->pos);
        if (ret < 0) {
            smb2_dbg("smb2_mpv_read_cb: buffered read failed: " + std::string(smb2_get_error(ctx->smb2)));
            return -1;
        }

        ctx->buffer_start = ctx->pos;
        ctx->buffer_len = (size_t)ret;
        if (ctx->buffer_len == 0) {
            return 0;
        }

        size_t copied = (size_t)std::min<uint64_t>(nbytes, ctx->buffer_len);
        std::memcpy(buf, ctx->read_buffer.data(), copied);
        ctx->pos += copied;

        smb2_dbg("smb2_mpv_read_cb: filled buffer size=" + std::to_string(ctx->buffer_len) +
                 " copied=" + std::to_string(copied));

        return (int64_t)copied;
    }

    int64_t ret = smb2_read_at(ctx, (uint8_t *)buf, nbytes, ctx->pos);
    if (ret < 0) {
        smb2_dbg("smb2_mpv_read_cb: read failed: " + std::string(smb2_get_error(ctx->smb2)));
        return -1;
    }

    ctx->pos += (uint64_t)ret;
    return ret;
}

static int64_t smb2_mpv_seek_cb(void *cookie, int64_t offset) {
    Smb2MpvCtx *ctx = (Smb2MpvCtx *)cookie;

    smb2_dbg("smb2_mpv_seek_cb: cookie=" + ptr_to_str(cookie) +
             " ctx=" + ptr_to_str(ctx) +
             " fh=" + (ctx ? ptr_to_str(ctx->fh) : "(null)") +
             " offset=" + std::to_string(offset));

    if (!ctx || !ctx->fh) {
        smb2_dbg("smb2_mpv_seek_cb: invalid ctx/fh");
        return -1;
    }

    if (offset < 0) {
        smb2_dbg("smb2_mpv_seek_cb: negative offset rejected");
        return -1;
    }

    ctx->pos = (uint64_t)offset;
    smb2_dbg("smb2_mpv_seek_cb: new_pos=" + std::to_string(ctx->pos));

    return (int64_t)ctx->pos;
}

static int64_t smb2_mpv_size_cb(void *cookie) {
    Smb2MpvCtx *ctx = (Smb2MpvCtx *)cookie;

    smb2_dbg("smb2_mpv_size_cb: cookie=" + ptr_to_str(cookie) +
             " ctx=" + ptr_to_str(ctx) +
             (ctx ? " file_size=" + std::to_string(ctx->file_size) : ""));

    return ctx ? ctx->file_size : -1;
}

static void smb2_mpv_close_cb(void *cookie) {
    Smb2MpvCtx *ctx = (Smb2MpvCtx *)cookie;

    smb2_dbg("smb2_mpv_close_cb: cookie=" + ptr_to_str(cookie) +
             " ctx=" + ptr_to_str(ctx) +
             " fh=" + (ctx ? ptr_to_str(ctx->fh) : "(null)") +
             " smb2=" + (ctx ? ptr_to_str(ctx->smb2) : "(null)"));

    if (!ctx) {
        return;
    }

    if (ctx->fh) {
        smb2_dbg("smb2_mpv_close_cb: smb2_close begin");
        smb2_close(ctx->smb2, ctx->fh);
        smb2_dbg("smb2_mpv_close_cb: smb2_close done");
    }

    if (ctx->smb2) {
        smb2_dbg("smb2_mpv_close_cb: smb2_disconnect_share begin");
        smb2_disconnect_share(ctx->smb2);
        smb2_dbg("smb2_mpv_close_cb: smb2_disconnect_share done");

        smb2_dbg("smb2_mpv_close_cb: smb2_destroy_context begin");
        smb2_destroy_context(ctx->smb2);
        smb2_dbg("smb2_mpv_close_cb: smb2_destroy_context done");
    }

    delete ctx;
    smb2_dbg("smb2_mpv_close_cb: ctx freed");
}

static int smb2_mpv_open_cb(void *user_data, char *uri, mpv_stream_cb_info *info)
{
    Smb2MpvSettings settings = user_data ? *(Smb2MpvSettings *)user_data : smb2_mpv_settings;

    smb2_dbg("smb2_mpv_open_cb: enter uri=" + safe_cstr(uri) +
             " info=" + ptr_to_str(info));

    std::string full_uri = uri ? uri : "";
    smb2_dbg("smb2_mpv_open_cb: full_uri(in)=" + full_uri);

    if (c2d::Utility::startWith(full_uri, "smb2://")) {
        full_uri.replace(0, std::strlen("smb2://"), "smb://");
    } else if (!c2d::Utility::startWith(full_uri, "smb://")) {
        smb2_dbg("smb2_mpv_open_cb: unsupported scheme, uri=" + full_uri);
        return MPV_ERROR_UNSUPPORTED;
    }

    smb2_dbg("smb2_mpv_open_cb: full_uri(normalized)=" + full_uri);

    SmbUrlParts parts = parseSmbUrl(full_uri);

    Smb2MpvCtx *ctx = new (std::nothrow) Smb2MpvCtx{};
    smb2_dbg("smb2_mpv_open_cb: ctx alloc=" + ptr_to_str(ctx));
    if (!ctx) {
        return MPV_ERROR_NOMEM;
    }

    ctx->smb2 = smb2_init_context();
    smb2_dbg("smb2_mpv_open_cb: smb2_init_context=" + ptr_to_str(ctx->smb2));
    if (!ctx->smb2) {
        delete ctx;
        smb2_dbg("smb2_mpv_open_cb: init_context failed");
        return MPV_ERROR_NOMEM;
    }

    smb2_set_security_mode(ctx->smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);
    smb2_set_timeout(ctx->smb2, settings.timeout_seconds);

    if (!parts.domain.empty()) {
        smb2_dbg("smb2_mpv_open_cb: set_domain=" + parts.domain);
        smb2_set_domain(ctx->smb2, parts.domain.c_str());
    }
    if (!parts.user.empty()) {
        smb2_dbg("smb2_mpv_open_cb: set_user=" + parts.user);
        smb2_set_user(ctx->smb2, parts.user.c_str());
    }
    if (!parts.password.empty()) {
        smb2_dbg("smb2_mpv_open_cb: set_password=(set)");
        smb2_set_password(ctx->smb2, parts.password.c_str());
    }

    std::string path = parts.path;
    path.erase(path.find_last_not_of('/') + 1);
    std::string libsmb_url = "smb://" 
        + (parts.domain.empty() ? "" : parts.domain + ";")
        + parts.user + "@" + parts.server + ":" + std::to_string(parts.port) + path;
    smb2_dbg("smb2_mpv_open_cb: libsmb_url=" + libsmb_url);

    smb2_url *url = smb2_parse_url(ctx->smb2, libsmb_url.c_str());
    smb2_dbg("smb2_mpv_open_cb: smb2_parse_url=" + ptr_to_str(url));
    if (!url) {
        smb2_dbg("smb2_mpv_open_cb: parse_url failed: " + std::string(smb2_get_error(ctx->smb2)));
        smb2_destroy_context(ctx->smb2);
        delete ctx;
        return MPV_ERROR_LOADING_FAILED;
    }
    smb2_dbg("smb2_mpv_open_cb: connect_share begin server=" + safe_cstr(url->server) +
             " share=" + safe_cstr(url->share));

    if (smb2_connect_share(ctx->smb2, url->server, url->share, url->user) < 0) {
        smb2_dbg("smb2_mpv_open_cb: connect_share failed: " + std::string(smb2_get_error(ctx->smb2)));
        smb2_destroy_url(url);
        smb2_destroy_context(ctx->smb2);
        delete ctx;
        return MPV_ERROR_LOADING_FAILED;
    }
    smb2_dbg("smb2_mpv_open_cb: connect_share OK");

    const char *open_path = (url->path && url->path[0]) ? url->path : "/";
    ctx->fh = smb2_open(ctx->smb2, open_path, O_RDONLY);
    smb2_dbg("smb2_mpv_open_cb: smb2_open fh=" + ptr_to_str(ctx->fh));

    smb2_destroy_url(url);

    if (!ctx->fh) {
        smb2_dbg("smb2_mpv_open_cb: open failed: " + std::string(smb2_get_error(ctx->smb2)));
        smb2_disconnect_share(ctx->smb2);
        smb2_destroy_context(ctx->smb2);
        delete ctx;
        return MPV_ERROR_LOADING_FAILED;
    }

    ctx->pos = 0;
    ctx->max_read_size = smb2_get_max_read_size(ctx->smb2);
    ctx->read_buffer_size = settings.read_buffer_size;
    smb2_dbg("smb2_mpv_open_cb: max_read_size=" + std::to_string(ctx->max_read_size) +
             " read_buffer_size=" + std::to_string(ctx->read_buffer_size));

    struct smb2_stat_64 st;
    if (smb2_fstat(ctx->smb2, ctx->fh, &st) == 0) {
        ctx->file_size = st.smb2_size;
        smb2_dbg("smb2_mpv_open_cb: file_size=" + std::to_string(ctx->file_size));
    } else {
        ctx->file_size = -1;
        smb2_dbg("smb2_mpv_open_cb: file_size unknown");
    }

    info->cookie    = ctx;
    info->read_fn   = smb2_mpv_read_cb;
    info->seek_fn   = smb2_mpv_seek_cb;
    info->size_fn   = smb2_mpv_size_cb;
    info->close_fn  = smb2_mpv_close_cb;
    info->cancel_fn = nullptr;

    smb2_dbg("smb2_mpv_open_cb: SUCCESS");
    return 0;
}

int register_smb_mpv(void *mpv_ctx) {
    pplay::Utility::log(
        pplay::Utility::LogLevel::Debug,
        std::string("register_smb_mpv: ctx=") + (mpv_ctx ? "non-null" : "null")
    );

    if (!mpv_ctx) {
        return -1;
    }

    int res = mpv_stream_cb_add_ro((mpv_handle *)mpv_ctx, "smb2", &smb2_mpv_settings, smb2_mpv_open_cb);

    pplay::Utility::log(
        pplay::Utility::LogLevel::Debug,
        std::string("register_smb_mpv: mpv_stream_cb_add_ro(smb2) = ") +
            std::to_string(res) +
            (res < 0 ? std::string(" (") + mpv_error_string(res) + ")" : "")
    );

    return res;
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
        files.emplace_back("..", http_path + "..", Io::Type::Directory, 0);

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
            file.path = ftp_path + file.name;
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
