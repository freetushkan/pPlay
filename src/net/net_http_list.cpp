//
// HTTP directory listings: index pages and WebDAV.
//

#include <algorithm>
#include <cctype>
#include <set>
#include <strings.h>

#include "net_http.h"
#include "utility.h"

using namespace pplay;

namespace {

    bool istartsWith(const std::string &s, size_t pos, const char *prefix) {
        size_t n = strlen(prefix);
        if (pos + n > s.size()) {
            return false;
        }
        for (size_t i = 0; i < n; i++) {
            if (std::tolower((unsigned char) s[pos + i]) != std::tolower((unsigned char) prefix[i])) {
                return false;
            }
        }
        return true;
    }

    std::string trim(const std::string &s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) {
            return {};
        }
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    std::string decodeEntities(const std::string &s) {
        static const std::pair<const char *, char> entities[] = {{"&amp;", '&'},  {"&lt;", '<'},   {"&gt;", '>'},
                                                                 {"&quot;", '"'}, {"&#39;", '\''}, {"&apos;", '\''}};
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
                if (done) {
                    continue;
                }
            }
            out += s[i];
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
            if (scheme == std::string::npos) {
                return;
            }
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
            if (c == '%') {
                out += "%25";
            } else if (c == '?') {
                out += "%3F";
            } else if (c == '#') {
                out += "%23";
            } else {
                out += c;
            }
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
        if (!istartsWith(html, pos, "<a") || pos + 2 >= html.size() || !std::isspace((unsigned char) html[pos + 2])) {
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
        if (href.empty() || href[0] == '?' || href[0] == '#' || istartsWith(href, 0, "javascript:")
            || istartsWith(href, 0, "mailto:")) {
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
        if (relative.empty() || relative.find('/') != std::string::npos || relative == "." || relative == "..") {
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
        if (end == std::string::npos) {
            break;
        }
        std::string tag = xml.substr(pos + 1, end - pos - 1);
        size_t textStart = end + 1;
        pos = end + 1;
        if (tag.empty() || tag[0] == '?' || tag[0] == '!') {
            continue;
        }

        bool closing = tag[0] == '/';
        bool selfClosing = !closing && tag.back() == '/';
        std::string name = closing ? tag.substr(1) : tag;
        if (selfClosing) {
            name.pop_back();
        }
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
                if (textEnd == std::string::npos) {
                    break;
                }
                std::string text = trim(decodeEntities(xml.substr(textStart, textEnd - textStart)));
                if (name == "href") {
                    href = text;
                } else {
                    length = text;
                }
            }
            continue;
        }

        if (name != "response" || !inResponse) {
            continue;
        }
        inResponse = false;

        std::string relative;
        if (href.empty() || !childOf(base, resolve(base, href), relative)) {
            continue; // the directory itself
        }
        if (c2d::Utility::endsWith(relative, "/")) {
            relative.pop_back();
            isDir = true;
        }
        if (relative.empty() || relative.find('/') != std::string::npos) {
            continue;
        }
        if (!seen.insert(relative).second) {
            continue;
        }

        size_t size = isDir ? 0 : (size_t) std::strtoull(length.c_str(), nullptr, 10);
        files.emplace_back(relative, dirUrl + pathSafe(relative),
                           isDir ? c2d::Io::Type::Directory : c2d::Io::Type::File, size);
    }

    Utility::log(Utility::LogLevel::Info, "Http::listWebDav " + dirUrl + " entries=" + std::to_string(files.size()));
    return files;
}
