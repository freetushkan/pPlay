//
// DLNA / UPnP media servers (see dlna.h): SSDP search, the device description, ContentDirectory
// Browse. The titles make the paths; their object ids and file urls are kept (memory and disk),
// a path not known yet is found from the root, title by title.
//

#include <atomic>
#include <cctype>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <thread>

// as ftplib (all the platforms)
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "dlna.h"
#include "io.h"
#include "net_http.h"
#include "utility.h"

using namespace pplay;

namespace {

    // a tolerant XML tree: local names (no prefix), the entities decoded
    struct Node {
        std::string name;
        std::map<std::string, std::string> attrs;
        std::string text;
        std::vector<Node> children;

        const Node *child(const char *n) const {
            for (auto &c: children) {
                if (c.name == n) {
                    return &c;
                }
            }
            return nullptr;
        }

        std::string childText(const char *n) const {
            const Node *c = child(n);
            return c != nullptr ? c->text : "";
        }

        // depth first
        const Node *find(const char *n) const {
            for (auto &c: children) {
                if (c.name == n) {
                    return &c;
                }
                if (const Node *f = c.find(n)) {
                    return f;
                }
            }
            return nullptr;
        }
    };

    std::string localName(const std::string &name) {
        size_t colon = name.find(':');
        return colon == std::string::npos ? name : name.substr(colon + 1);
    }

    void appendUtf8(std::string &out, unsigned long c) {
        if (c < 0x80) {
            out += (char) c;
        } else if (c < 0x800) {
            out += (char) (0xC0 | (c >> 6));
            out += (char) (0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += (char) (0xE0 | (c >> 12));
            out += (char) (0x80 | ((c >> 6) & 0x3F));
            out += (char) (0x80 | (c & 0x3F));
        } else if (c < 0x110000) {
            out += (char) (0xF0 | (c >> 18));
            out += (char) (0x80 | ((c >> 12) & 0x3F));
            out += (char) (0x80 | ((c >> 6) & 0x3F));
            out += (char) (0x80 | (c & 0x3F));
        }
    }

    std::string decode(const std::string &s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++) {
            size_t end;
            if (s[i] != '&' || (end = s.find(';', i)) == std::string::npos || end - i > 10) {
                out += s[i];
                continue;
            }
            std::string e = s.substr(i + 1, end - i - 1);
            if (e == "amp") {
                out += '&';
            } else if (e == "lt") {
                out += '<';
            } else if (e == "gt") {
                out += '>';
            } else if (e == "quot") {
                out += '"';
            } else if (e == "apos") {
                out += '\'';
            } else if (e.size() > 1 && e[0] == '#') {
                bool hex = e[1] == 'x' || e[1] == 'X';
                appendUtf8(out, std::strtoul(e.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10));
            } else {
                out += s.substr(i, end - i + 1);
            }
            i = end;
        }
        return out;
    }

    Node parseXml(const std::string &xml) {
        Node root;
        std::vector<Node *> stack{&root};
        size_t pos = 0;
        while (pos < xml.size()) {
            size_t lt = xml.find('<', pos);
            std::string text = xml.substr(pos, lt == std::string::npos ? std::string::npos : lt - pos);
            if (!text.empty()) {
                stack.back()->text += decode(text);
            }
            if (lt == std::string::npos) {
                break;
            }
            if (xml.compare(lt, 9, "<![CDATA[") == 0) {
                size_t end = xml.find("]]>", lt);
                stack.back()->text += xml.substr(lt + 9, end == std::string::npos ? std::string::npos : end - lt - 9);
                pos = end == std::string::npos ? xml.size() : end + 3;
                continue;
            }
            if (xml.compare(lt, 4, "<!--") == 0) {
                size_t end = xml.find("-->", lt);
                pos = end == std::string::npos ? xml.size() : end + 3;
                continue;
            }
            size_t gt = xml.find('>', lt);
            if (gt == std::string::npos) {
                break;
            }
            pos = gt + 1;
            if (xml[lt + 1] == '?' || xml[lt + 1] == '!') {
                continue;
            }
            if (xml[lt + 1] == '/') {
                if (stack.size() > 1) {
                    stack.pop_back();
                }
                continue;
            }
            // attribute values may hold '>': the real end
            size_t i = lt + 1;
            while (i < xml.size() && !std::isspace((unsigned char) xml[i]) && xml[i] != '>' && xml[i] != '/') {
                i++;
            }
            Node node;
            node.name = localName(xml.substr(lt + 1, i - lt - 1));
            bool closed = false;
            while (i < xml.size()) {
                while (i < xml.size() && std::isspace((unsigned char) xml[i])) {
                    i++;
                }
                if (i >= xml.size() || xml[i] == '>') {
                    break;
                }
                if (xml[i] == '/') {
                    closed = true;
                    i++;
                    continue;
                }
                size_t eq = xml.find('=', i);
                if (eq == std::string::npos) {
                    break;
                }
                std::string attr = localName(xml.substr(i, eq - i));
                while (!attr.empty() && std::isspace((unsigned char) attr.back())) {
                    attr.pop_back();
                }
                size_t q = eq + 1;
                while (q < xml.size() && std::isspace((unsigned char) xml[q])) {
                    q++;
                }
                if (q >= xml.size() || (xml[q] != '"' && xml[q] != '\'')) {
                    break;
                }
                size_t qe = xml.find(xml[q], q + 1);
                if (qe == std::string::npos) {
                    break;
                }
                node.attrs[attr] = decode(xml.substr(q + 1, qe - q - 1));
                i = qe + 1;
            }
            pos = std::min(xml.size(), i + 1);
            stack.back()->children.push_back(std::move(node));
            if (!closed) {
                stack.push_back(&stack.back()->children.back());
            }
        }
        return root;
    }

    std::string trim(const std::string &s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) {
            return {};
        }
        return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
    }

    std::string lower(std::string s) {
        for (auto &c: s) {
            c = (char) std::tolower((unsigned char) c);
        }
        return s;
    }

    // ContentDirectory of a server
    struct Control {
        std::string url;
        std::string service;
    };

    struct Entry {
        bool dir = false;
        std::string id;
        std::string url;
        std::string art;
    };

    std::mutex g_mutex;
    bool g_loaded = false;
    bool g_dirty = false;
    // by network root (no trailing slash)
    std::map<std::string, Control> g_controls;
    // by path
    std::map<std::string, Entry> g_entries;

    std::string storePath() {
        return Utility::getDataPath() + "cache/dlna";
    }

    std::string field(const std::string &s) {
        std::string out;
        for (char c: s) {
            out += c == '\t' || c == '\n' || c == '\r' ? ' ' : c;
        }
        return out;
    }

    std::vector<std::string> split(const std::string &line) {
        std::vector<std::string> out(1);
        for (char c: line) {
            if (c == '\t') {
                out.emplace_back();
            } else {
                out.back() += c;
            }
        }
        return out;
    }

    // under g_mutex
    void load() {
        if (g_loaded) {
            return;
        }
        g_loaded = true;
        std::string text;
        if (!Io::get()->readText(storePath(), text)) {
            return;
        }
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) {
                break;
            }
            std::vector<std::string> f = split(text.substr(start, end - start));
            start = end + 1;
            if (f[0] == "C" && f.size() == 4) {
                g_controls[f[1]] = {f[2], f[3]};
            } else if ((f[0] == "D" || f[0] == "F") && f.size() == 5) {
                g_entries[f[1]] = {f[0] == "D", f[2], f[3], f[4]};
            }
        }
    }

    // under g_mutex
    void save() {
        if (!g_dirty) {
            return;
        }
        g_dirty = false;
        std::string text;
        for (auto &c: g_controls) {
            text += "C\t" + field(c.first) + "\t" + field(c.second.url) + "\t" + field(c.second.service) + "\n";
        }
        for (auto &e: g_entries) {
            text += std::string(e.second.dir ? "D" : "F") + "\t" + field(e.first) + "\t" + field(e.second.id) + "\t"
                    + field(e.second.url) + "\t" + field(e.second.art) + "\n";
        }
        Io::get()->create(Utility::getDataPath() + "cache");
        Io::get()->writeText(storePath(), text);
    }

    std::string rootOf(const std::string &path) {
        std::string root = Io::get() != nullptr ? Io::get()->getNetworkRoot(path) : "";
        while (!root.empty() && root.back() == '/') {
            root.pop_back();
        }
        return root;
    }

    std::string noSlash(std::string path) {
        while (path.size() > 1 && path.back() == '/') {
            path.pop_back();
        }
        return path;
    }

    Http::Response httpGet(const std::string &url, int timeout) {
        Http::Request request;
        request.url = url;
        request.timeout = timeout;
        request.maxSize = 1024 * 1024;
        return Http::perform(request);
    }

    // a device description: its name, its ContentDirectory (false if none)
    bool describe(const std::string &location, const std::string &xml, std::string *name, Control *control) {
        Node doc = parseXml(xml);
        const Node *root = doc.child("root");
        if (root == nullptr) {
            return false;
        }
        const Node *device = root->child("device");
        if (name != nullptr && device != nullptr) {
            *name = trim(device->childText("friendlyName"));
        }
        std::string base = trim(root->childText("URLBase"));
        // the services of the device and of the embedded ones
        std::vector<const Node *> pending{root};
        while (!pending.empty()) {
            const Node *node = pending.back();
            pending.pop_back();
            for (auto &c: node->children) {
                if (c.name == "service" && c.childText("serviceType").find(":ContentDirectory:") != std::string::npos) {
                    if (control != nullptr) {
                        control->service = trim(c.childText("serviceType"));
                        control->url = Http::resolve(base.empty() ? location : base, trim(c.childText("controlURL")));
                    }
                    return true;
                }
                pending.push_back(&c);
            }
        }
        return false;
    }

    // the description of a network url: as given, else at the usual places of its host
    bool controlOf(const std::string &root, int timeout, Control *control, std::string *error) {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            load();
            auto it = g_controls.find(root);
            if (it != g_controls.end()) {
                *control = it->second;
                return true;
            }
        }
        std::vector<std::string> urls{root};
        for (const char *p: {"/rootDesc.xml", "/description.xml", "/DeviceDescription.xml", "/dmr/description.xml"}) {
            urls.push_back(Http::origin(root) + p);
        }
        std::string failed;
        for (auto &url: urls) {
            Http::Response response = httpGet(url, timeout);
            if (!response.ok()) {
                if (failed.empty()) {
                    failed = response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error;
                }
                // the server itself not answering: no other place to try
                if (response.status == 0) {
                    break;
                }
                continue;
            }
            if (describe(url, response.body, nullptr, control)) {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_controls[root] = *control;
                g_dirty = true;
                return true;
            }
        }
        *error = failed.empty() ? "no media server there" : failed;
        return false;
    }

    std::string xmlEscape(const std::string &s) {
        std::string out;
        for (char c: s) {
            out += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '"' ? "&quot;" : std::string(1, c);
        }
        return out;
    }

    // the children of a container (all the pages)
    bool browse(const Control &control, const std::string &id, int timeout, std::vector<Node> *out, std::string *error) {
        const int page = 200;
        int start = 0;
        while (true) {
            Http::Request request;
            request.url = control.url;
            request.method = "POST";
            request.timeout = timeout;
            request.headers = {"Content-Type: text/xml; charset=\"utf-8\"",
                               "SOAPAction: \"" + control.service + "#Browse\""};
            request.body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                           "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                           "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
                           "<u:Browse xmlns:u=\"" + control.service + "\">"
                           "<ObjectID>" + xmlEscape(id) + "</ObjectID>"
                           "<BrowseFlag>BrowseDirectChildren</BrowseFlag><Filter>*</Filter>"
                           "<StartingIndex>" + std::to_string(start) + "</StartingIndex>"
                           "<RequestedCount>" + std::to_string(page) + "</RequestedCount>"
                           "<SortCriteria></SortCriteria></u:Browse></s:Body></s:Envelope>";
            Http::Response response = Http::perform(request);
            Node soap = parseXml(response.body);
            if (!response.ok()) {
                std::string upnp = trim(soap.find("errorDescription") != nullptr ? soap.find("errorDescription")->text : "");
                *error = !upnp.empty() ? upnp
                         : response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error;
                return false;
            }
            const Node *result = soap.find("Result");
            if (result == nullptr) {
                *error = "bad answer";
                return false;
            }
            Node didl = parseXml(result->text);
            const Node *list = didl.child("DIDL-Lite");
            int returned = 0;
            if (list != nullptr) {
                for (auto &c: list->children) {
                    if (c.name == "container" || c.name == "item") {
                        out->push_back(c);
                        returned++;
                    }
                }
            }
            const Node *total = soap.find("TotalMatches");
            long matches = total != nullptr ? std::strtol(total->text.c_str(), nullptr, 10) : 0;
            start += returned;
            if (returned == 0 || start >= matches) {
                return true;
            }
        }
    }

    // a title as a path part
    std::string partOf(const std::string &title) {
        std::string out;
        for (size_t i = 0; i < title.size(); i++) {
            unsigned char c = (unsigned char) title[i];
            if (c == '/' || c == '\\') {
                out += "\xE2\x88\x95"; // ∕
            } else if (c == '?') {
                // the query of the other networks paths: none in a name
                continue;
            } else if (c >= ' ') {
                out += (char) c;
            }
        }
        out = trim(out);
        return out.empty() || out == "." || out == ".." ? "_" : out;
    }

    // the file of an item: http-get, the original (not converted) first, of its kind
    const Node *pickRes(const Node &item, std::string *mime) {
        std::string kind = item.childText("class").find("videoItem") != std::string::npos   ? "video/"
                           : item.childText("class").find("audioItem") != std::string::npos ? "audio/"
                           : item.childText("class").find("imageItem") != std::string::npos ? "image/"
                                                                                            : "";
        const Node *best = nullptr;
        int bestRank = -1;
        for (auto &c: item.children) {
            if (c.name != "res" || trim(c.text).empty()) {
                continue;
            }
            auto info = c.attrs.find("protocolInfo");
            std::string protocol = info != c.attrs.end() ? info->second : "";
            std::vector<std::string> parts(1);
            for (char ch: protocol) {
                if (ch == ':' && parts.size() < 4) {
                    parts.emplace_back();
                } else {
                    parts.back() += ch;
                }
            }
            if (!protocol.empty() && parts[0] != "http-get") {
                continue;
            }
            std::string type = parts.size() > 2 ? lower(parts[2]) : "";
            bool converted = parts.size() > 3 && parts[3].find("DLNA.ORG_CI=1") != std::string::npos;
            int rank = (converted ? 0 : 2) + (kind.empty() || type.rfind(kind, 0) == 0 ? 1 : 0);
            if (rank > bestRank) {
                best = &c;
                bestRank = rank;
                *mime = type;
            }
        }
        return best;
    }

    // a file name extension from the url, else from the type
    std::string extensionOf(const std::string &url, const std::string &mime) {
        std::string path = url.substr(0, url.find_first_of("?#"));
        size_t slash = path.rfind('/'), dot = path.rfind('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash) && path.size() - dot <= 6
            && path.size() - dot >= 3) {
            std::string ext = lower(path.substr(dot));
            bool alnum = true;
            for (size_t i = 1; i < ext.size(); i++) {
                alnum = alnum && std::isalnum((unsigned char) ext[i]);
            }
            if (alnum) {
                return ext;
            }
        }
        static const std::pair<const char *, const char *> types[] = {
                {"video/mp4", ".mp4"},        {"video/x-matroska", ".mkv"}, {"video/x-mkv", ".mkv"},
                {"video/avi", ".avi"},        {"video/x-msvideo", ".avi"},  {"video/mpeg", ".mpg"},
                {"video/mp2t", ".ts"},        {"video/vnd.dlna.mpeg-tts", ".ts"}, {"video/quicktime", ".mov"},
                {"video/x-ms-wmv", ".wmv"},   {"video/webm", ".webm"},      {"video/x-flv", ".flv"},
                {"video/3gpp", ".3gp"},       {"audio/mpeg", ".mp3"},       {"audio/mp4", ".m4a"},
                {"audio/x-flac", ".flac"},    {"audio/flac", ".flac"},      {"audio/ogg", ".ogg"},
                {"audio/wav", ".wav"},        {"audio/x-wav", ".wav"},      {"audio/x-ms-wma", ".wma"},
                {"audio/aac", ".aac"},        {"image/jpeg", ".jpg"},       {"image/png", ".png"},
                {"image/webp", ".webp"}};
        for (auto &t: types) {
            if (mime == t.first) {
                return t.second;
            }
        }
        return "";
    }

    // the listing of a container (its entries kept, the gone ones forgotten)
    std::vector<c2d::Io::File> listOf(const std::string &dir, const std::vector<Node> &nodes) {
        std::vector<c2d::Io::File> files;
        std::map<std::string, Entry> entries;
        std::set<std::string> names;
        auto unique = [&names](const std::string &name, const std::string &ext) {
            std::string base = name.substr(0, name.size() - ext.size());
            std::string out = name;
            for (int n = 2; !names.insert(lower(out)).second; n++) {
                out = base + " (" + std::to_string(n) + ")" + ext;
            }
            return out;
        };
        for (auto &node: nodes) {
            auto id = node.attrs.find("id");
            if (id == node.attrs.end()) {
                continue;
            }
            std::string title = partOf(node.childText("title"));
            std::string art = trim(node.childText("albumArtURI"));
            if (node.name == "container") {
                std::string name = unique(title, "");
                std::string path = dir + "/" + name;
                files.emplace_back(name, path, c2d::Io::Type::Directory, 0);
                entries[path] = {true, id->second, "", art};
                continue;
            }
            std::string mime;
            const Node *res = pickRes(node, &mime);
            if (res == nullptr) {
                continue;
            }
            std::string url = trim(res->text);
            std::string ext = extensionOf(url, mime);
            if (!ext.empty() && (title.size() < ext.size() || lower(title.substr(title.size() - ext.size())) != ext)) {
                title += ext;
            }
            std::string name = unique(title, ext);
            std::string path = dir + "/" + name;
            auto size = res->attrs.find("size");
            files.emplace_back(name, path, c2d::Io::Type::File,
                               size != res->attrs.end() ? std::strtoull(size->second.c_str(), nullptr, 10) : 0);
            entries[path] = {false, id->second, url, art};
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        load();
        // the direct children of the directory replaced
        std::string prefix = dir + "/";
        for (auto it = g_entries.lower_bound(prefix); it != g_entries.end() && it->first.compare(0, prefix.size(), prefix) == 0;) {
            bool direct = it->first.find('/', prefix.size()) == std::string::npos;
            auto now = entries.find(it->first);
            if (direct && now == entries.end()) {
                it = g_entries.erase(it);
                g_dirty = true;
                continue;
            }
            ++it;
        }
        for (auto &e: entries) {
            Entry &old = g_entries[e.first];
            if (old.dir != e.second.dir || old.id != e.second.id || old.url != e.second.url || old.art != e.second.art) {
                old = e.second;
                g_dirty = true;
            }
        }
        save();
        return files;
    }

    // the object id of a directory path: kept, else its parent listed (from the root)
    bool idOf(const std::string &root, const Control &control, const std::string &dir, int timeout, std::string *id,
              std::string *error, int depth = 0) {
        if (dir == root) {
            *id = "0";
            return true;
        }
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            load();
            auto it = g_entries.find(dir);
            if (it != g_entries.end() && it->second.dir) {
                *id = it->second.id;
                return true;
            }
        }
        size_t slash = dir.rfind('/');
        if (depth > 64 || slash == std::string::npos || slash < root.size()) {
            *error = "not found";
            return false;
        }
        std::string parent = dir.substr(0, slash), parentId;
        if (!idOf(root, control, parent, timeout, &parentId, error, depth + 1)) {
            return false;
        }
        std::vector<Node> nodes;
        if (!browse(control, parentId, timeout, &nodes, error)) {
            return false;
        }
        listOf(parent, nodes);
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_entries.find(dir);
        if (it == g_entries.end() || !it->second.dir) {
            *error = "not found";
            return false;
        }
        *id = it->second.id;
        return true;
    }

    // SSDP
    std::mutex g_searchMutex;
    std::vector<Dlna::Server> g_found;
    std::atomic<bool> g_searching{false};
    std::thread *g_searchThread = nullptr;

    std::string headerOf(const std::string &answer, const char *name) {
        std::string low = lower(answer);
        std::string key = std::string("\n") + name + ":";
        size_t at = low.find(key);
        if (at == std::string::npos) {
            return "";
        }
        size_t end = answer.find('\n', at + 1);
        return trim(answer.substr(at + key.size(), end == std::string::npos ? std::string::npos : end - at - key.size()));
    }
}

namespace pplay::Dlna {

    std::vector<Server> search(int seconds) {
        std::vector<Server> servers;
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            Utility::log(Utility::LogLevel::Error, "Dlna: no socket");
            return servers;
        }
#ifdef IP_MULTICAST_TTL
        unsigned char ttl = 2;
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char *) &ttl, sizeof(ttl));
#endif
        struct sockaddr_in to {};
#if defined(__PS4__) || defined(__PS5__)
        to.sin_len = sizeof(to);
#endif
        to.sin_family = AF_INET;
        to.sin_port = htons(1900);
        to.sin_addr.s_addr = inet_addr("239.255.255.250");
        std::set<std::string> locations;
        c2d::C2DClock clock;
        int sent = 0;
        while (clock.getElapsedTime().asSeconds() < (float) seconds) {
            // sent again (UDP): at the start, then once a second
            if (sent < 3 && clock.getElapsedTime().asSeconds() >= (float) sent) {
                for (const char *target: {"urn:schemas-upnp-org:device:MediaServer:1",
                                          "urn:schemas-upnp-org:service:ContentDirectory:1"}) {
                    std::string message = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
                                                      "MAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") + target + "\r\n\r\n";
                    sendto(sock, message.c_str(), message.size(), 0, (struct sockaddr *) &to, sizeof(to));
                }
                sent++;
            }
            fd_set set;
            FD_ZERO(&set);
            FD_SET(sock, &set);
            struct timeval tv {0, 200 * 1000};
            if (select(sock + 1, &set, nullptr, nullptr, &tv) <= 0) {
                continue;
            }
            char buffer[2048];
            ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0) {
                continue;
            }
            buffer[n] = '\0';
            std::string location = headerOf(buffer, "location");
            if (!location.empty()) {
                locations.insert(location);
            }
        }
        close(sock);
        for (auto &location: locations) {
            Http::Response response = httpGet(location, 5);
            std::string name;
            if (response.ok() && describe(location, response.body, &name, nullptr)) {
                servers.push_back({name.empty() ? Http::origin(location) : name, location});
            }
        }
        Utility::log(Utility::LogLevel::Info, "Dlna: search, " + std::to_string(locations.size()) + " answers, "
                                                      + std::to_string(servers.size()) + " media servers");
        return servers;
    }

    void startSearch() {
        if (g_searching) {
            return;
        }
        if (g_searchThread != nullptr) {
            g_searchThread->join();
            delete g_searchThread;
        }
        g_searching = true;
        g_searchThread = new std::thread([] {
            std::vector<Server> servers = search(4);
            std::lock_guard<std::mutex> lock(g_searchMutex);
            g_found = servers;
            g_searching = false;
        });
    }

    bool searchResult(std::vector<Server> *servers) {
        if (g_searching) {
            return false;
        }
        std::lock_guard<std::mutex> lock(g_searchMutex);
        *servers = g_found;
        return true;
    }

    std::vector<c2d::Io::File> list(const std::string &p, int timeout, std::string *error) {
        std::string path = noSlash(p);
        std::string root = rootOf(path);
        Control control;
        std::string id, failed;
        std::vector<Node> nodes;
        if (root.empty() || !controlOf(root, timeout, &control, &failed)
            || !idOf(root, control, path, timeout, &id, &failed) || !browse(control, id, timeout, &nodes, &failed)) {
            Utility::log(Utility::LogLevel::Error, "Dlna: " + path + ": " + failed);
            *error = failed.empty() ? "error" : failed;
            return {};
        }
        std::vector<c2d::Io::File> files = listOf(path, nodes);
        Utility::log(Utility::LogLevel::Info, "Dlna: " + path + " (" + id + "), " + std::to_string(files.size()) + " entries");
        return files;
    }

    std::string streamUrl(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        load();
        auto it = g_entries.find(path);
        return it != g_entries.end() && !it->second.dir ? it->second.url : "";
    }

    std::string posterOf(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        load();
        std::string p = noSlash(path);
        for (int i = 0; i < 2 && !p.empty(); i++) {
            auto it = g_entries.find(p);
            if (it != g_entries.end() && !it->second.art.empty()) {
                return it->second.art;
            }
            size_t slash = p.rfind('/');
            p = slash == std::string::npos ? "" : p.substr(0, slash);
        }
        return "";
    }
}
