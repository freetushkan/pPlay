//
// The current path as small blocks: root › dir › ... › current (its tail when too long).
//

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>

#include "main.h"
#include "breadcrumb.h"
#include "encodings.h"
#include "i18n.h"
#include "round_rect.h"
#include "text_wrap.h"
#include "pplay_config.h"
#include "saved.h"
#include "paths.h"
#include "playlists.h"
#include "torrserve.h"
#include "io.h"
#include "net_http.h"
#include "p2play.h"
#include "menu_main.h"

using namespace c2d;

#define CRUMB_MAX 10

namespace {
    std::string decode(const std::string &text) {
        std::string out;
        for (size_t i = 0; i < text.size(); i++) {
            if (text[i] == '%' && i + 2 < text.size() && std::isxdigit((unsigned char) text[i + 1])
                && std::isxdigit((unsigned char) text[i + 2])) {
                out += (char) std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16);
                i += 2;
            } else {
                out += text[i];
            }
        }
        return out;
    }

    std::string appendPath(std::string base, const std::string &part) {
        if (base.empty()) {
            return part;
        }
        if (!base.empty() && base.back() != '/') {
            base += '/';
        }
        return base + part;
    }
}

std::vector<Breadcrumb::Entry> Breadcrumb::split(const std::string &path) const {
    std::vector<Entry> parts;

    const std::string playlistRoot = pplay::Playlists::root();
    if (path == playlistRoot || pplay::Playlists::isPlaylist(path)) {
        parts.push_back({pplay::tr("Playlists"), playlistRoot});
        if (path != playlistRoot) {
            parts.push_back({encoding::fix(pplay::Paths::getLeafName(path)), path});
        }
        return parts;
    }

    const std::string savedRoot = pplay::Saved::root();
    if (pplay::Saved::isInside(path)) {
        parts.push_back({pplay::tr("Saved"), savedRoot});
        std::vector<std::string> display = pplay::Saved::displayParts(path);
        std::string relative = path.substr(std::min(path.size(), savedRoot.size()));
        while (!relative.empty() && relative.front() == '/') {
            relative.erase(relative.begin());
        }
        std::string target = savedRoot;
        size_t start = 0;
        size_t index = 0;
        while (start <= relative.size()) {
            size_t end = relative.find('/', start);
            std::string part = relative.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!part.empty() && part != ".") {
                target = appendPath(target, part);
                std::string label = index < display.size() ? display[index] : encoding::fix(decode(part));
                parts.push_back({label, target});
                ++index;
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return parts;
    }

    std::string rest = path;
    std::string rootLabel = pplay::tr("Local");
    std::string rootPath = "/";
    size_t best = 0;
    for (int i = 1; i <= main->getConfig()->getNetworkCount(); i++) {
        std::string url = main->getConfig()->getOption(PPLAYConfig::networkOption(i))->getString();
        while (!url.empty() && url.back() == '/') {
            url.pop_back();
        }
        const bool boundary = path.size() == url.size()
                             || (path.size() > url.size() && path[url.size()] == '/');
        if (!url.empty() && boundary && url.size() > best && path.compare(0, url.size(), url) == 0) {
            best = url.size();
            std::string name = main->getConfig()->getOption(PPLAYConfig::networkNameOption(i))->getString();
            rootLabel = name.empty() ? pplay::tr("Network") + " " + std::to_string(i) : name;
            rootPath = url;
            rest = path.substr(url.size());
        }
    }

    if (best == 0 && path.compare(0, 3, "ums") == 0) {
        rootLabel = pplay::tr("Usb");
        size_t slash = path.find('/');
        if (slash == std::string::npos) {
            rootPath = path;
            rest.clear();
        } else {
            rootPath = path.substr(0, slash + 1);
            rest = path.substr(slash + 1);
        }
    } else if (best == 0 && path.find("://") != std::string::npos) {
        // an unconfigured network: host as the root crumb
        size_t start = path.find("://") + 3, slash = path.find('/', start);
        rootPath = slash == std::string::npos ? path : path.substr(0, slash + 1);
        rootLabel = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        rootLabel = rootLabel.substr(rootLabel.rfind('@') == std::string::npos ? 0 : rootLabel.rfind('@') + 1);
        rootLabel = rootLabel.substr(0, rootLabel.find(':'));
        rest = slash == std::string::npos ? "" : path.substr(slash + 1);
    } else if (best == 0) {
        // local paths: the root crumb returns to the filesystem root
        rootPath = "/";
        rest = path;
    }

    if (best > 0 && pplay::Io::get() != nullptr &&
        (pplay::Io::get()->getNetworkType(path) == pplay::Io::NetworkType::P2Play ||
         pplay::Io::get()->getNetworkType(path) == pplay::Io::NetworkType::P2PlayEmbedded)) {
        parts.push_back({rootLabel, rootPath});
        std::vector<std::string> segments;
        size_t cursor = 0;
        while (cursor <= rest.size()) {
            const size_t end = rest.find('/', cursor);
            const std::string raw = rest.substr(cursor, end == std::string::npos ? std::string::npos : end - cursor);
            if (!raw.empty()) segments.push_back(decode(raw));
            if (end == std::string::npos) break;
            cursor = end + 1;
        }
        if (segments.size() < 2 || segments[0] != ".p2play") return parts;
        const std::string libraryPath = appendPath(appendPath(rootPath, ".p2play"), "library");
        if (segments[1] == "library") {
            parts.push_back({pplay::tr("My torrents"), libraryPath});
            if (segments.size() >= 4 && segments[2].rfind("q-", 0) != 0) {
                const std::string hash = segments[2];
                const std::string title = segments[3];
                const std::string torrentPath = pplay::P2Play::directTorrentPath(rootPath, hash, title);
                parts.push_back({encoding::fix(title), torrentPath});
                for (size_t i = 4; i < segments.size(); ++i) {
                    const std::string target = appendPath(torrentPath, pplay::Http::escape(segments[i]));
                    parts.push_back({segments[i] == pplay::TorrServe::ALL_FILES ? pplay::tr(segments[i]) :
                                     encoding::fix(segments[i]), target});
                }
            }
            return parts;
        }
        if (segments[1] == "file" && segments.size() >= 6) {
            parts.push_back({pplay::tr("My torrents"), libraryPath});
            const std::string torrentPath = pplay::P2Play::directTorrentPath(rootPath, segments[2], segments[4]);
            parts.push_back({encoding::fix(segments[4]), torrentPath});
            for (size_t i = 5; i < segments.size(); ++i) {
                const std::string target = appendPath(torrentPath, pplay::Http::escape(segments[i]));
                parts.push_back({encoding::fix(segments[i]), target});
            }
            return parts;
        }
        if (segments[1] == "results" || segments[1] == "result" || segments[1] == "record") {
            const std::string searchPath = appendPath(appendPath(rootPath, ".p2play"), "search");
            parts.push_back({pplay::tr("Search torrents"), searchPath});
            if (segments.size() >= 3) {
                std::string label = segments[1] == "results" ? segments[2] : segments[2].substr(0, 12);
                const std::string target = appendPath(appendPath(rootPath, ".p2play"), segments[1]);
                std::string itemPath = appendPath(target, pplay::Http::escape(segments[2]));
                parts.push_back({encoding::fix(label), itemPath});
            }
            return parts;
        }
        if (segments[1] == "list" || segments[1] == "disabled") {
            const std::string listsPath = appendPath(appendPath(rootPath, ".p2play"), "lists");
            parts.push_back({pplay::tr("My lists"), listsPath});
            if (segments.size() >= 3) {
                const std::string label = pplay::tr("List") + " " + segments[2].substr(0, 12);
                const std::string target = appendPath(appendPath(rootPath, ".p2play"), segments[1]);
                parts.push_back({encoding::fix(label), appendPath(target, pplay::Http::escape(segments[2]))});
            }
            return parts;
        }
        std::string section = segments[1] == "search" ? "Search torrents" :
                              segments[1] == "progress" ? "Continue watching" :
                              segments[1] == "discover" ? "Discover" :
                              segments[1] == "lists" ? "My lists" : encoding::fix(segments[1]);
        const std::string sectionPath = appendPath(appendPath(rootPath, ".p2play"), pplay::Http::escape(segments[1]));
        parts.push_back({pplay::tr(section), sectionPath});
        for (size_t i = 2; i < segments.size(); ++i) {
            parts.push_back({encoding::fix(segments[i]),
                appendPath(parts.back().path, pplay::Http::escape(segments[i]))});
        }
        return parts;
    }

    parts.push_back({rootLabel, rootPath});
    while (!rest.empty() && rest.front() == '/') {
        rest.erase(rest.begin());
    }
    size_t start = 0;
    while (start <= rest.size()) {
        size_t end = rest.find('/', start);
        std::string part = rest.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty() && part != ".") {
            std::string label = decode(part);
            parts.push_back({label == pplay::TorrServe::ALL_FILES ? pplay::tr(label) : encoding::fix(label),
                             appendPath(parts.back().path, part)});
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return parts;
}

Breadcrumb::Breadcrumb(Main *m, const FloatRect &rect) : Rectangle(rect), main(m) {
    const float sc = main->getScaling().y;
    last = new RoundRect({16, std::round(26 * sc)});
    last->setOrigin(Origin::Left);
    Breadcrumb::add(last);

    // A compact hamburger in the existing crumb strip: no extra row or height.
    const float buttonSize = 32 * sc;
    menuButton = new RoundRect({buttonSize, buttonSize});
    // Use top-left local coordinates for both the round background and its child lines.
    // This avoids applying the parent's centered origin twice to the hamburger glyph.
    menuButton->setOrigin(Origin::TopLeft);
    menuButton->setPosition(0, std::round((getSize().y - buttonSize) / 2));
    menuButton->setFillColor(Color(255, 255, 255, 35));
    Breadcrumb::add(menuButton);
    for (int i = 0; i < 3; i++) {
        auto *line = new RectangleShape({15 * sc, std::max(1.0f, 2 * sc)});
        line->setOrigin(Origin::Center);
        line->setPosition(buttonSize / 2, buttonSize / 2 + (float) (i - 1) * 6 * sc);
        line->setFillColor(COLOR_FONT);
        menuButton->add(line);
        menuLines.push_back(line);
    }

    for (int i = 0; i < CRUMB_MAX; i++) {
        auto *name = new Text("", main->getFontSize(Main::FontSize::Small), main->getFont());
        name->setOrigin(Origin::Left);
        Breadcrumb::add(name);
        names.push_back(name);
        auto *arrow = new Text("›", main->getFontSize(Main::FontSize::Small), main->getFont());
        arrow->setOrigin(Origin::Left);
        arrow->setFillColor(Color(110, 118, 128, 255));
        Breadcrumb::add(arrow);
        arrows.push_back(arrow);
    }
}

void Breadcrumb::setWidth(float width) {
    if (std::fabs(width - getSize().x) < 1) {
        return;
    }
    setSize(width, getSize().y);
    std::string path = current;
    current.clear();
    setPath(path);
}

void Breadcrumb::setPath(const std::string &path) {
    const bool showMenu = main->getConfig()->getOption(OPT_SHOW_MENU_BUTTON)->getInteger() != 0;
    if (path == current && showMenu == menuShown) {
        return;
    }
    current = path;
    menuShown = showMenu;
    menuButton->setVisibility(menuShown ? Visibility::Visible : Visibility::Hidden);

    const float sc = main->getScaling().y;
    const float pad = 10 * sc, gap = 6 * sc, y = getSize().y / 2;
    const float menuOffset = menuShown ? 42 * sc : 0.0f;
    const float available = std::max(1.0f, getSize().x - menuOffset);

    std::vector<Entry> shown = split(path);
    Font *font = main->getFont();
    unsigned int fontSize = names[0]->getCharacterSize();
    auto widthOf = [&](const std::string &text) { return pplay::TextWrap::width(font, fontSize, text); };
    float arrowWidth = widthOf("›") + 2 * gap;
    auto total = [&]() {
        float w = 2 * pad;
        for (const auto &part: shown) {
            w += widthOf(part.label) + arrowWidth;
        }
        return w;
    };
    const float minWidth = widthOf("abcdef…");
    while (total() > available) {
        int widest = -1;
        for (int i = 1; i + 1 < (int) shown.size(); i++) {
            if (shown[i].label != ".." && widthOf(shown[i].label) > minWidth
                && (widest < 0 || widthOf(shown[i].label) > widthOf(shown[widest].label))) {
                widest = i;
            }
        }
        if (widest > 0) {
            shown[widest].label = pplay::TextWrap::ellipsize(font, fontSize, shown[widest].label,
                                                             std::max(minWidth, widthOf(shown[widest].label) * 0.8f));
            continue;
        }
        // Replace the hidden middle levels with one useful back target (the current parent).
        if (shown.size() > 3 || (shown.size() == 3 && shown[1].label != "..")) {
            Entry collapsed{"..", shown[shown.size() - 2].path};
            shown.erase(shown.begin() + 1, shown.end() - 1);
            shown.insert(shown.begin() + 1, collapsed);
            continue;
        }

        // No middle levels remain: shorten the current directory first, then the root.
        // Keep the Entry path untouched so the crumb remains a valid navigation target.
        if (shown.empty()) {
            break;
        }
        const float over = total() - available;
        Entry &cut = widthOf(shown.back().label) > minWidth ? shown.back() : shown.front();
        const std::string shorter = pplay::TextWrap::ellipsize(
            font, fontSize, cut.label, std::max(minWidth, widthOf(cut.label) - over));
        if (shorter == cut.label) {
            break;
        }
        cut.label = shorter;
    }
    if ((int) shown.size() > CRUMB_MAX) {
        Entry collapsed{"..", shown[shown.size() - 2].path};
        shown.erase(shown.begin() + 1, shown.end() - (CRUMB_MAX - 1));
        shown[1] = collapsed;
    }

    visibleTargets.assign(names.size(), "");
    float x = menuOffset;
    for (size_t i = 0; i < names.size(); i++) {
        bool used = i < shown.size();
        names[i]->setVisibility(used ? Visibility::Visible : Visibility::Hidden);
        arrows[i]->setVisibility(used && i + 1 < shown.size() ? Visibility::Visible : Visibility::Hidden);
        if (!used) continue;

        visibleTargets[i] = shown[i].path;
        bool isLast = i + 1 == shown.size();
        names[i]->setString(shown[i].label);
        float w = names[i]->getLocalBounds().width;
        if (isLast) {
            float pill = std::ceil((w + 2 * pad) / (8 * sc)) * 8 * sc;
            last->setSize(pill, last->getSize().y);
            last->setPosition(x, y);
            Color tint = COLOR_ACCENT;
            tint.a = 70;
            last->setFillColor(tint);
            names[i]->setPosition(x + (pill - w) / 2, y);
            names[i]->setFillColor(COLOR_FONT);
        } else {
            names[i]->setPosition(x, y);
            names[i]->setFillColor(Color(150, 158, 168, 255));
            x += w + gap;
            arrows[i]->setPosition(x, y);
            x += arrows[i]->getLocalBounds().width + gap;
        }
    }
    last->setVisibility(shown.empty() ? Visibility::Hidden : Visibility::Visible);
}

bool Breadcrumb::isMenuButtonAt(const Vector2f &position) const {
    if (!menuShown || menuButton == nullptr || !menuButton->isVisible()) {
        return false;
    }
    // Hit the entire reserved hamburger slot (and the full crumb-row height), not just
    // the 32 px round visual backing. This keeps the visible control easy to tap.
    const FloatRect bounds = getGlobalBounds();
    const float hitWidth = std::min(bounds.width, 42 * main->getScaling().y);
    return position.x >= bounds.left && position.x <= bounds.left + hitWidth
           && position.y >= bounds.top && position.y <= bounds.top + bounds.height;
}

bool Breadcrumb::pathAt(const Vector2f &position, std::string *target) const {
    if (target == nullptr) return false;
    for (size_t i = 0; i < names.size() && i < visibleTargets.size(); i++) {
        if (!visibleTargets[i].empty() && names[i]->isVisible()
            && names[i]->getGlobalBounds().contains(position)) {
            *target = visibleTargets[i];
            return true;
        }
    }
    if (last != nullptr && last->isVisible() && last->getGlobalBounds().contains(position)) {
        for (size_t i = names.size(); i > 0; --i) {
            size_t index = i - 1;
            if (!visibleTargets[index].empty() && names[index]->isVisible()) {
                *target = visibleTargets[index];
                return true;
            }
        }
    }
    return false;
}
