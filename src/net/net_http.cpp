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

    bool istartsWith(const std::string &s, size_t pos, const char *prefix) {
        size_t n = strlen(prefix);
        if (pos + n > s.size()) return false;
        for (size_t i = 0; i < n; i++) {
            if (std::tolower((unsigned char) s[pos + i]) != std::tolower((unsigned char) prefix[i])) {
                return false;
            }
        }
        return true;
    }

    std::string trim(const std::string &s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return {};
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    std::string decodeEntities(const std::string &s) {
        static const std::pair<const char *, char> entities[] = {
                {"&amp;",  '&'}, {"&lt;", '<'}, {"&gt;", '>'},
                {"&quot;", '"'}, {"&#39;", '\''}, {"&apos;", '\''}
        };
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '&') {
                bool done = false;
                for (auto &e: entities) {
                    if (s.compare(i, strlen(e.first), e.first) == 0) {
                        out += e.second;
                        i += strlen(e.first) - 1;
                        done = true;
                        break;
                    }
                }
                if (done) continue;
            }
            out += s[i];
        }
        return out;
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

    // child of dirUrl ? returns the (unescaped) relative part
    bool childOf(const std::string &dirUrl, const std::string &url, std::string &relative) {
        std::string dir = Http::unescape(dirUrl);
        std::string full = Http::unescape(url);
        // ignore the user info when comparing (servers never return it)
        auto stripAuth = [](std::string &u) {
            size_t scheme = u.find("://");
            if (scheme == std::string::npos) return;
            size_t slash = u.find('/', scheme + 3);
            size_t at = u.find('@', scheme + 3);
            if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
                u.erase(scheme + 3, at - (scheme + 3) + 1);
            }
        };
        stripAuth(dir);
        stripAuth(full);
        if (full.size() <= dir.size()) {
            return false;
        }
        if (strncasecmp(full.c_str(), dir.c_str(), dir.size()) != 0) {
            return false;
        }
        relative = full.substr(dir.size());
        return true;
    }

    std::string dirUrlOf(const std::string &url) {
        std::string dir = url;
        size_t query = dir.find('?');
        if (query != std::string::npos) {
            dir.erase(query);
        }
        if (!c2d::Utility::endsWith(dir, "/")) {
            dir += "/";
        }
        return dir;
    }

    // readable names in paths, only the characters changing the url meaning escaped
    std::string pathSafe(const std::string &name) {
        std::string out;
        for (char c: name) {
            if (c == '%') out += "%25";
            else if (c == '?') out += "%3F";
            else if (c == '#') out += "%23";
            else out += c;
        }
        return out;
    }

    // local tag name without namespace prefix, lower case
    std::string localName(const std::string &tag) {
        std::string name = tag;
        size_t colon = name.find(':');
        if (colon != std::string::npos) {
            name = name.substr(colon + 1);
        }
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
        return name;
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

std::vector<c2d::Io::File> Http::listIndex(const std::string &url, int timeout, std::string *error) {
    std::vector<c2d::Io::File> files;
    std::string dirUrl = dirUrlOf(url);
    Response response = get(dirUrl, timeout);
    if (!response.ok()) {
        if (error) {
            *error = response.error.empty() ? "HTTP error " + std::to_string(response.status) : response.error;
        }
        return files;
    }

    // relative links are relative to the final url (redirects)
    std::string base = response.effectiveUrl.empty() ? dirUrl : dirUrlOf(response.effectiveUrl);
    const std::string &html = response.body;
    std::set<std::string> seen;

    size_t pos = 0;
    while ((pos = html.find('<', pos)) != std::string::npos) {
        if (!istartsWith(html, pos, "<a") || pos + 2 >= html.size()
            || !std::isspace((unsigned char) html[pos + 2])) {
            pos++;
            continue;
        }
        size_t end = html.find('>', pos);
        if (end == std::string::npos) {
            break;
        }
        std::string tag = html.substr(pos, end - pos);
        pos = end;

        // href value
        size_t h = 0;
        std::string href;
        while ((h = tag.find_first_of("hH", h)) != std::string::npos) {
            if (istartsWith(tag, h, "href") && (h == 0 || std::isspace((unsigned char) tag[h - 1]))) {
                size_t eq = tag.find_first_not_of(" \t\r\n", h + 4);
                if (eq != std::string::npos && tag[eq] == '=') {
                    size_t v = tag.find_first_not_of(" \t\r\n", eq + 1);
                    if (v != std::string::npos) {
                        if (tag[v] == '"' || tag[v] == '\'') {
                            size_t ve = tag.find(tag[v], v + 1);
                            href = tag.substr(v + 1, ve == std::string::npos ? std::string::npos : ve - v - 1);
                        } else {
                            size_t ve = tag.find_first_of(" \t\r\n", v);
                            href = tag.substr(v, ve == std::string::npos ? std::string::npos : ve - v);
                        }
                    }
                }
                break;
            }
            h++;
        }

        href = trim(decodeEntities(href));
        if (href.empty() || href[0] == '?' || href[0] == '#'
            || istartsWith(href, 0, "javascript:") || istartsWith(href, 0, "mailto:")) {
            continue;
        }
        size_t cut = href.find_first_of("?#");
        if (cut != std::string::npos) {
            href.erase(cut);
        }

        std::string relative;
        if (!childOf(base, resolve(base, href), relative)) {
            continue; // parent, other host, sort links...
        }
        bool isDir = c2d::Utility::endsWith(relative, "/");
        if (isDir) {
            relative.pop_back();
        }
        if (relative.empty() || relative.find('/') != std::string::npos
            || relative == "." || relative == "..") {
            continue;
        }
        if (!seen.insert(relative).second) {
            continue;
        }
        files.emplace_back(relative, dirUrl + pathSafe(relative),
                           isDir ? c2d::Io::Type::Directory : c2d::Io::Type::File, 0);
    }

    Utility::log(Utility::LogLevel::Info, "Http::listIndex " + dirUrl + " entries=" + std::to_string(files.size()));
    return files;
}

std::vector<c2d::Io::File> Http::listWebDav(const std::string &url, int timeout, std::string *error) {
    std::vector<c2d::Io::File> files;
    std::string dirUrl = dirUrlOf(url);

    Request request;
    request.url = dirUrl;
    request.method = "PROPFIND";
    request.timeout = timeout;
    request.headers = {"Depth: 1", "Content-Type: application/xml; charset=utf-8"};
    request.body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                   "<d:propfind xmlns:d=\"DAV:\"><d:prop>"
                   "<d:resourcetype/><d:getcontentlength/>"
                   "</d:prop></d:propfind>";
    Response response = perform(request);
    if (!response.error.empty() || (response.status != 207 && response.status != 200)) {
        if (error) {
            *error = response.error.empty() ? "WebDAV error " + std::to_string(response.status) : response.error;
        }
        return files;
    }

    const std::string &xml = response.body;
    std::string base = response.effectiveUrl.empty() ? dirUrl : dirUrlOf(response.effectiveUrl);
    std::set<std::string> seen;

    // tiny xml walker, namespace prefixes are ignored
    bool inResponse = false, isDir = false;
    std::string href, length, current;
    size_t pos = 0;
    while ((pos = xml.find('<', pos)) != std::string::npos) {
        size_t end = xml.find('>', pos);
        if (end == std::string::npos) break;
        std::string tag = xml.substr(pos + 1, end - pos - 1);
        size_t textStart = end + 1;
        pos = end + 1;
        if (tag.empty() || tag[0] == '?' || tag[0] == '!') continue;

        bool closing = tag[0] == '/';
        bool selfClosing = !closing && tag.back() == '/';
        std::string name = closing ? tag.substr(1) : tag;
        if (selfClosing) name.pop_back();
        name = name.substr(0, name.find_first_of(" \t\r\n"));
        name = localName(name);

        if (!closing) {
            if (name == "response") {
                inResponse = true;
                isDir = false;
                href.clear();
                length.clear();
            } else if (inResponse && name == "collection") {
                isDir = true;
            } else if (inResponse && !selfClosing && (name == "href" || name == "getcontentlength")) {
                size_t textEnd = xml.find('<', textStart);
                if (textEnd == std::string::npos) break;
                std::string text = trim(decodeEntities(xml.substr(textStart, textEnd - textStart)));
                if (name == "href") href = text; else length = text;
            }
            continue;
        }

        if (name != "response" || !inResponse) continue;
        inResponse = false;

        std::string relative;
        if (href.empty() || !childOf(base, resolve(base, href), relative)) {
            continue; // the directory itself
        }
        if (c2d::Utility::endsWith(relative, "/")) {
            relative.pop_back();
            isDir = true;
        }
        if (relative.empty() || relative.find('/') != std::string::npos) continue;
        if (!seen.insert(relative).second) continue;

        size_t size = isDir ? 0 : (size_t) std::strtoull(length.c_str(), nullptr, 10);
        files.emplace_back(relative, dirUrl + pathSafe(relative),
                           isDir ? c2d::Io::Type::Directory : c2d::Io::Type::File, size);
    }

    Utility::log(Utility::LogLevel::Info, "Http::listWebDav " + dirUrl + " entries=" + std::to_string(files.size()));
    return files;
}
