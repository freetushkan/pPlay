//
// libcurl based HTTP helpers.
//

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>
#include <strings.h>

#include <curl/curl.h>

#include "net_http.h"
#include "utility.h"

using namespace pplay;

namespace {

    struct WriteContext {
        std::string *out;
        size_t maxSize;
    };

    size_t writeCallback(void *ptr, size_t size, size_t count, void *userdata) {
        auto *ctx = static_cast<WriteContext *>(userdata);
        size_t len = size * count;
        if (ctx->out->size() + len > ctx->maxSize) {
            return 0; // abort, too big
        }
        ctx->out->append(static_cast<char *>(ptr), len);
        return len;
    }

    // "Date" header
    size_t headerCallback(char *ptr, size_t size, size_t count, void *userdata) {
        size_t len = size * count;
        if (len > 5 && strncasecmp(ptr, "date:", 5) == 0) {
            std::string value(ptr + 5, len - 5);
            time_t date = curl_getdate(value.c_str(), nullptr);
            if (date > 0) {
                *static_cast<time_t *>(userdata) = date;
            }
        }
        return len;
    }

    bool isHex(char c) {
        return std::isxdigit((unsigned char) c) != 0;
    }

    // escape what is not allowed in an url path/query, keep valid %XX sequences
    std::string normalizeUrl(const std::string &url) {
        static const char *hex = "0123456789ABCDEF";
        size_t scheme = url.find("://");
        size_t start = scheme == std::string::npos ? 0 : url.find('/', scheme + 3);
        if (start == std::string::npos) {
            return url;
        }
        std::string out = url.substr(0, start);
        for (size_t i = start; i < url.size(); i++) {
            unsigned char c = (unsigned char) url[i];
            if (c == '%' && i + 2 < url.size() && isHex(url[i + 1]) && isHex(url[i + 2])) {
                out += (char) c;
            } else if (std::isalnum(c) || strchr("-._~!$&'()*+,;=:@/?#", c) != nullptr) {
                out += (char) c;
            } else {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
        return out;
    }

}

void Http::init() {
    static bool done = false;
    if (!done) {
        curl_global_init(CURL_GLOBAL_ALL);
        done = true;
    }
}

Http::Response Http::perform(const Request &request) {
    Utility::NetworkLock lock;
    Response response;
    CURL *curl = curl_easy_init();
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = 0;
    WriteContext ctx{&response.body, request.maxSize};
    std::string url = normalizeUrl(request.url);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.date);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long) request.timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long) std::min(request.timeout, 10));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "pPlay/" APP_VERSION);
#if defined(__PS4__) || defined(__PS5__)
    curl_easy_setopt(curl, CURLOPT_CAINFO, Utility::getCertificatesPath().c_str());
#elif defined(__LINUX__)
    std::string certificates = Utility::getCertificatesPath();
    if (!certificates.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, certificates.c_str());
    }
#endif

    struct curl_slist *headers = nullptr;
    for (auto &h: request.headers) {
        headers = curl_slist_append(headers, h.c_str());
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    if (request.method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    }
    if (!request.body.empty() || request.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) request.body.size());
    }

    Utility::log(Utility::LogLevel::Debug, "Http::" + request.method + " " + url);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        response.error = std::string(curl_easy_strerror(res)) + (errbuf[0] ? std::string(": ") + errbuf : "");
        Utility::log(Utility::LogLevel::Error, "Http::" + request.method + " " + url + " failed: " + response.error);
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    char *effective = nullptr;
    if (curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective) == CURLE_OK && effective) {
        response.effectiveUrl = effective;
    }
    Utility::log(Utility::LogLevel::Debug, "Http::" + request.method + " status=" + std::to_string(response.status)
                                                   + " size=" + std::to_string(response.body.size()));

    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    return response;
}

Http::Response Http::get(const std::string &url, int timeout) {
    Request request;
    request.url = url;
    request.timeout = timeout;
    return perform(request);
}

Http::Response Http::postJson(const std::string &url, const std::string &json, int timeout) {
    Request request;
    request.url = url;
    request.method = "POST";
    request.body = json;
    request.timeout = timeout;
    request.headers = {"Content-Type: application/json", "Accept: application/json"};
    return perform(request);
}

std::string Http::normalize(const std::string &url) {
    return normalizeUrl(url);
}

std::string Http::escape(const std::string &value) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c: value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char) c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string Http::unescape(const std::string &value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); i++) {
        if (value[i] == '%' && i + 2 < value.size() && isHex(value[i + 1]) && isHex(value[i + 2])) {
            out += (char) ((Utility::hexValue(value[i + 1]) << 4) | Utility::hexValue(value[i + 2]));
            i += 2;
        } else {
            out += value[i];
        }
    }
    return out;
}

std::string Http::origin(const std::string &url) {
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) {
        return url;
    }
    size_t slash = url.find('/', scheme + 3);
    return slash == std::string::npos ? url : url.substr(0, slash);
}

std::string Http::resolve(const std::string &base, const std::string &href) {
    if (href.find("://") != std::string::npos) {
        return href;
    }
    if (c2d::Utility::startWith(href, "//")) {
        return base.substr(0, base.find("://") + 1) + href;
    }
    if (c2d::Utility::startWith(href, "/")) {
        return origin(base) + href;
    }
    std::string dir = base.substr(0, base.find_last_of('/') + 1);
    std::string rel = href;
    while (c2d::Utility::startWith(rel, "./")) {
        rel.erase(0, 2);
    }
    return dir + rel;
}
