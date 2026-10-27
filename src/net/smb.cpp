//
// SMB2/3 (libsmb2): url parsing, directory listing and mpv stream callbacks.
//

#ifdef __SMB2__

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <sstream>
#include <vector>
#include <fcntl.h>

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include "smb.h"
#include "utility.h"

using namespace pplay;

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
    // adaptive read ahead: small after open/seek, doubled on each sequential refill
    uint64_t next_fill;
    // stats (logged on close)
    uint64_t stat_bytes;
    uint64_t stat_reads;
    uint64_t stat_seeks;
} Smb2MpvCtx;

#define SMB2_MIN_READ_AHEAD (256 * 1024)

static uint64_t smb2_initial_fill(const Smb2MpvCtx *ctx) {
    return std::min<uint64_t>(SMB2_MIN_READ_AHEAD, ctx->read_buffer_size);
}

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
        ctx->stat_reads++;
        if (ret > 0) {
            ctx->stat_bytes += (uint64_t) ret;
        }
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
        request_size = std::max<uint64_t>(request_size, ctx->next_fill);
        // sequential access: read more next time
        ctx->next_fill = std::min<uint64_t>(ctx->next_fill * 2, ctx->read_buffer_size);
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

    if ((uint64_t) offset == ctx->pos) {
        return (int64_t) ctx->pos;
    }

    ctx->pos = (uint64_t)offset;
    if (ctx->buffer_len > 0 && ctx->pos >= ctx->buffer_start
        && ctx->pos < ctx->buffer_start + ctx->buffer_len) {
        // still inside the read ahead buffer (small seeks while probing)
        smb2_dbg("smb2_mpv_seek_cb: new_pos=" + std::to_string(ctx->pos) + " (in buffer)");
        return (int64_t) ctx->pos;
    }

    ctx->stat_seeks++;
    ctx->buffer_start = ctx->pos;
    ctx->buffer_len = 0;
    ctx->next_fill = smb2_initial_fill(ctx);
    smb2_dbg("smb2_mpv_seek_cb: new_pos=" + std::to_string(ctx->pos) + " (buffer invalidated)");

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

    pplay::Utility::log(pplay::Utility::LogLevel::Info,
        "smb2: closed, file_size=" + std::to_string(ctx->file_size)
        + " read=" + std::to_string(ctx->stat_bytes)
        + " requests=" + std::to_string(ctx->stat_reads)
        + " seeks=" + std::to_string(ctx->stat_seeks));

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
    ctx->next_fill = smb2_initial_fill(ctx);
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

std::vector<c2d::Io::File> Smb::list(const std::string &path, int timeout, std::string *error) {
    using Io = c2d::Io;
    std::vector<Io::File> files;
    SmbUrlParts smbUrl = parseSmbUrl(path);
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
                 "Smb::list: server=" + std::string(url->server ? url->server : "")
                 + " port=" + std::to_string(smbUrl.port)
                 + " share=" + std::string(url->share ? url->share : "")
                 + " path=" + std::string(url->path ? url->path : "")
                 + " user=" + std::string(url->user ? url->user : "")
                 + " domain=" + smbUrl.domain);

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
        bool isParentEntry = ent->name[0] == '.'
                             && (ent->name[1] == '\0'
                                 || (ent->name[1] == '.' && ent->name[2] == '\0'));
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
