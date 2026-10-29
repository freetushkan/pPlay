//
// mpv "smb2://" stream protocol (libsmb2), with an adaptive read ahead buffer.
//

#ifdef __SMB2__

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <vector>
#include <fcntl.h>

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include "smb.h"
#include "utility.h"

using pplay::Utility;

// first read ahead after open / seek, doubled on each sequential refill
#define MIN_READ_AHEAD (256 * 1024)
#define MAX_BUFFER_MB 100

namespace {
    struct Settings {
        uint32_t bufferSize;
        int timeout;
    };

    Settings g_settings = {1024 * 1024, 60};

    struct Stream {
        smb2_context *smb2 = nullptr;
        smb2fh *fh = nullptr;
        int64_t fileSize = -1;
        uint64_t pos = 0;
        uint32_t maxReadSize = 0;
        uint32_t bufferSize = 0;
        std::vector<uint8_t> buffer;
        uint64_t bufferStart = 0;
        size_t bufferLen = 0;
        uint64_t nextFill = 0;
        // logged on close
        uint64_t statBytes = 0;
        uint64_t statReads = 0;
        uint64_t statSeeks = 0;
    };

    uint64_t initialFill(const Stream *s) {
        return std::min<uint64_t>(MIN_READ_AHEAD, s->bufferSize);
    }

    void fail(const std::string &what, smb2_context *smb2) {
        Utility::log(Utility::LogLevel::Error, "smb2: " + what + ": " + (smb2 ? smb2_get_error(smb2) : ""));
    }

    int64_t readAt(Stream *s, uint8_t *dst, uint64_t requested, uint64_t offset) {
        uint64_t total = 0;
        while (total < requested) {
            uint64_t chunk = std::min<uint64_t>(requested - total, std::numeric_limits<uint32_t>::max());
            if (s->maxReadSize > 0) {
                chunk = std::min<uint64_t>(chunk, s->maxReadSize);
            }
            int ret = smb2_pread(s->smb2, s->fh, dst + total, (uint32_t) chunk, offset + total);
            s->statReads++;
            if (ret < 0) {
                return total > 0 ? (int64_t) total : -1;
            }
            if (ret == 0) {
                break;
            }
            s->statBytes += (uint64_t) ret;
            total += (uint64_t) ret;
        }
        return (int64_t) total;
    }

    bool readFromBuffer(Stream *s, char *buf, uint64_t nbytes, int64_t *result) {
        if (s->bufferLen == 0 || s->pos < s->bufferStart || s->pos - s->bufferStart >= s->bufferLen) {
            return false;
        }
        uint64_t offset = s->pos - s->bufferStart;
        size_t copied = (size_t) std::min<uint64_t>(nbytes, s->bufferLen - offset);
        std::memcpy(buf, s->buffer.data() + offset, copied);
        s->pos += copied;
        *result = (int64_t) copied;
        return true;
    }

    int64_t onRead(void *cookie, char *buf, uint64_t nbytes) {
        auto *s = (Stream *) cookie;
        if (s == nullptr || s->fh == nullptr) {
            return -1;
        }
        if (nbytes == 0) {
            return 0;
        }
        int64_t buffered = 0;
        if (readFromBuffer(s, buf, nbytes, &buffered)) {
            return buffered;
        }

        uint64_t size = nbytes;
        if (s->bufferSize > 0) {
            size = std::max<uint64_t>(size, s->nextFill);
            // sequential access: read more next time
            s->nextFill = std::min<uint64_t>(s->nextFill * 2, s->bufferSize);
        }
        if (s->fileSize >= 0 && s->pos < (uint64_t) s->fileSize) {
            size = std::min<uint64_t>(size, (uint64_t) s->fileSize - s->pos);
        }

        if (s->bufferSize > 0 && size > nbytes) {
            s->buffer.resize((size_t) size);
            int64_t ret = readAt(s, s->buffer.data(), size, s->pos);
            if (ret < 0) {
                fail("read", s->smb2);
                return -1;
            }
            s->bufferStart = s->pos;
            s->bufferLen = (size_t) ret;
            if (s->bufferLen == 0) {
                return 0;
            }
            size_t copied = (size_t) std::min<uint64_t>(nbytes, s->bufferLen);
            std::memcpy(buf, s->buffer.data(), copied);
            s->pos += copied;
            return (int64_t) copied;
        }

        int64_t ret = readAt(s, (uint8_t *) buf, nbytes, s->pos);
        if (ret < 0) {
            fail("read", s->smb2);
            return -1;
        }
        s->pos += (uint64_t) ret;
        return ret;
    }

    int64_t onSeek(void *cookie, int64_t offset) {
        auto *s = (Stream *) cookie;
        if (s == nullptr || s->fh == nullptr || offset < 0) {
            return -1;
        }
        if ((uint64_t) offset == s->pos) {
            return offset;
        }
        s->pos = (uint64_t) offset;
        // still inside the read ahead buffer (small seeks while probing)
        if (s->bufferLen > 0 && s->pos >= s->bufferStart && s->pos < s->bufferStart + s->bufferLen) {
            return offset;
        }
        s->statSeeks++;
        s->bufferStart = s->pos;
        s->bufferLen = 0;
        s->nextFill = initialFill(s);
        return offset;
    }

    int64_t onSize(void *cookie) {
        auto *s = (Stream *) cookie;
        return s != nullptr ? s->fileSize : -1;
    }

    void destroy(Stream *s) {
        if (s->fh != nullptr) {
            smb2_close(s->smb2, s->fh);
        }
        if (s->smb2 != nullptr) {
            smb2_disconnect_share(s->smb2);
            smb2_destroy_context(s->smb2);
        }
        delete s;
    }

    void onClose(void *cookie) {
        auto *s = (Stream *) cookie;
        if (s == nullptr) {
            return;
        }
        Utility::log(Utility::LogLevel::Info,
                     "smb2: closed, file_size=" + std::to_string(s->fileSize) + " read=" + std::to_string(s->statBytes)
                             + " requests=" + std::to_string(s->statReads) + " seeks=" + std::to_string(s->statSeeks));
        destroy(s);
    }

    int onOpen(void *userData, char *uri, mpv_stream_cb_info *info) {
        Settings settings = userData ? *(Settings *) userData : g_settings;
        std::string url = uri ? uri : "";
        if (c2d::Utility::startWith(url, "smb2://")) {
            url.replace(0, std::strlen("smb2://"), "smb://");
        } else if (!c2d::Utility::startWith(url, "smb://")) {
            return MPV_ERROR_UNSUPPORTED;
        }
        pplay::Smb::Url parts = pplay::Smb::parseUrl(url);

        auto *s = new (std::nothrow) Stream();
        if (s == nullptr) {
            return MPV_ERROR_NOMEM;
        }
        s->smb2 = smb2_init_context();
        if (s->smb2 == nullptr) {
            delete s;
            return MPV_ERROR_NOMEM;
        }
        smb2_set_security_mode(s->smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);
        smb2_set_timeout(s->smb2, settings.timeout);
        if (!parts.domain.empty()) {
            smb2_set_domain(s->smb2, parts.domain.c_str());
        }
        if (!parts.user.empty()) {
            smb2_set_user(s->smb2, parts.user.c_str());
        }
        if (!parts.password.empty()) {
            smb2_set_password(s->smb2, parts.password.c_str());
        }

        std::string path = parts.path;
        path.erase(path.find_last_not_of('/') + 1);
        std::string libsmbUrl = "smb://" + (parts.domain.empty() ? "" : parts.domain + ";") + parts.user + "@"
                                + parts.server + ":" + std::to_string(parts.port) + path;
        smb2_url *smbUrl = smb2_parse_url(s->smb2, libsmbUrl.c_str());
        if (smbUrl == nullptr) {
            fail("url", s->smb2);
            destroy(s);
            return MPV_ERROR_LOADING_FAILED;
        }
        if (smb2_connect_share(s->smb2, smbUrl->server, smbUrl->share, smbUrl->user) < 0) {
            fail("connect " + std::string(smbUrl->server ? smbUrl->server : "") + "/"
                         + (smbUrl->share ? smbUrl->share : ""),
                 s->smb2);
            smb2_destroy_url(smbUrl);
            smb2_destroy_context(s->smb2);
            s->smb2 = nullptr;
            delete s;
            return MPV_ERROR_LOADING_FAILED;
        }
        s->fh = smb2_open(s->smb2, smbUrl->path && smbUrl->path[0] ? smbUrl->path : "/", O_RDONLY);
        smb2_destroy_url(smbUrl);
        if (s->fh == nullptr) {
            fail("open", s->smb2);
            destroy(s);
            return MPV_ERROR_LOADING_FAILED;
        }

        s->maxReadSize = smb2_get_max_read_size(s->smb2);
        s->bufferSize = settings.bufferSize;
        s->nextFill = initialFill(s);
        struct smb2_stat_64 st {};
        s->fileSize = smb2_fstat(s->smb2, s->fh, &st) == 0 ? (int64_t) st.smb2_size : -1;
        Utility::log(Utility::LogLevel::Info,
                     "smb2: opened " + parts.server + path + ", size=" + std::to_string(s->fileSize) + " max_read="
                             + std::to_string(s->maxReadSize) + " buffer=" + std::to_string(s->bufferSize));

        info->cookie = s;
        info->read_fn = onRead;
        info->seek_fn = onSeek;
        info->size_fn = onSize;
        info->close_fn = onClose;
        info->cancel_fn = nullptr;
        return 0;
    }
}

void pplay::Smb::configureStream(int bufferMB, int timeout) {
    g_settings.bufferSize = bufferMB > 0 ? (uint32_t) std::min(bufferMB, MAX_BUFFER_MB) * 1024 * 1024 : 0;
    g_settings.timeout = timeout > 0 ? timeout : 60;
    Utility::log(Utility::LogLevel::Info, "smb2: buffer=" + std::to_string(g_settings.bufferSize)
                                                  + " timeout=" + std::to_string(g_settings.timeout));
}

int pplay::Smb::registerStream(void *mpv) {
    if (mpv == nullptr) {
        return -1;
    }
    int res = mpv_stream_cb_add_ro((mpv_handle *) mpv, "smb2", &g_settings, onOpen);
    if (res < 0) {
        Utility::log(Utility::LogLevel::Error, std::string("smb2: stream protocol: ") + mpv_error_string(res));
    }
    return res;
}

#endif
