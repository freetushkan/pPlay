//
// The current path as small blocks.
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
}

std::vector<std::string> Breadcrumb::split(const std::string &path) const {
    // the root as named in the main menu: a network (its url starts the path), USB, local
    std::vector<std::string> parts;
    std::string rest = path;
    std::string root = pplay::tr("Local");
    size_t best = 0;
    for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
        std::string url = main->getConfig()->getOption(PPLAYConfig::networkOption(i))->getString();
        while (!url.empty() && url.back() == '/') {
            url.pop_back();
        }
        if (!url.empty() && url.size() > best && path.compare(0, url.size(), url) == 0) {
            best = url.size();
            std::string name = main->getConfig()->getOption(PPLAYConfig::networkNameOption(i))->getString();
            root = name.empty() ? pplay::tr("Network") + " " + std::to_string(i) : name;
            rest = path.substr(url.size());
        }
    }
    if (best == 0 && path.compare(0, 3, "ums") == 0) {
        root = pplay::tr("Usb");
        size_t slash = path.find('/');
        rest = slash == std::string::npos ? "" : path.substr(slash);
    } else if (best == 0 && path.find("://") != std::string::npos) {
        // not a configured network: its host
        size_t start = path.find("://") + 3, end = path.find('/', start);
        root = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        root = root.substr(root.rfind('@') == std::string::npos ? 0 : root.rfind('@') + 1);
        root = root.substr(0, root.find(':'));
        rest = end == std::string::npos ? "" : path.substr(end);
    }
    parts.push_back(root);
    size_t start = 0;
    while (start <= rest.size()) {
        size_t end = rest.find('/', start);
        std::string part = rest.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty() && part != ".") {
            parts.push_back(encoding::fix(decode(part)));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

Breadcrumb::Breadcrumb(Main *m, const FloatRect &rect) : Rectangle(rect), main(m) {
    const float sc = main->getScaling().y;
    last = new RoundRect({16, std::round(26 * sc)});
    last->setOrigin(Origin::Left);
    Breadcrumb::add(last);
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

void Breadcrumb::setPath(const std::string &path) {
    if (path == current) {
        return;
    }
    current = path;
    const float sc = main->getScaling().y;
    const float pad = 10 * sc, gap = 6 * sc, y = getSize().y / 2;

    // the whole path when it fits; else the middle names shortened first, then gone
    // (".." for them), the current one and the root last
    std::vector<std::string> shown = split(path);
    Font *font = main->getFont();
    unsigned int fontSize = names[0]->getCharacterSize();
    auto widthOf = [&](const std::string &text) { return pplay::TextWrap::width(font, fontSize, text); };
    float arrowWidth = widthOf("›") + 2 * gap;
    auto total = [&]() {
        float w = 2 * pad;
        for (auto &part: shown) {
            w += widthOf(part) + arrowWidth;
        }
        return w;
    };
    const float minWidth = widthOf("abcdef…");
    float available = getSize().x;
    while (total() > available) {
        // the widest middle name, a bit shorter
        int widest = -1;
        for (int i = 1; i + 1 < (int) shown.size(); i++) {
            if (shown[i] != ".." && widthOf(shown[i]) > minWidth
                && (widest < 0 || widthOf(shown[i]) > widthOf(shown[widest]))) {
                widest = i;
            }
        }
        if (widest > 0) {
            shown[widest] = pplay::TextWrap::ellipsize(font, fontSize, shown[widest],
                                                       std::max(minWidth, widthOf(shown[widest]) * 0.8f));
            continue;
        }
        // the middle names gone, from the root side
        if (shown.size() > 3 || (shown.size() == 3 && shown[1] != "..")) {
            shown.erase(shown.begin() + 1);
            if (shown[1] != "..") {
                shown.insert(shown.begin() + 1, "..");
            }
            continue;
        }
        // the current one, then the root
        float over = total() - available;
        std::string &cut = widthOf(shown.back()) > minWidth ? shown.back() : shown.front();
        std::string shorter = pplay::TextWrap::ellipsize(font, fontSize, cut, std::max(minWidth, widthOf(cut) - over));
        if (shorter == cut) {
            break;
        }
        cut = shorter;
    }
    if ((int) shown.size() > CRUMB_MAX) {
        shown.erase(shown.begin() + 1, shown.end() - (CRUMB_MAX - 1));
        shown[1] = "..";
    }

    float x = 0;
    for (size_t i = 0; i < names.size(); i++) {
        bool used = i < shown.size();
        names[i]->setVisibility(used ? Visibility::Visible : Visibility::Hidden);
        arrows[i]->setVisibility(used && i + 1 < shown.size() ? Visibility::Visible : Visibility::Hidden);
        if (!used) {
            continue;
        }
        bool isLast = i + 1 == shown.size();
        names[i]->setString(shown[i]);
        float w = names[i]->getLocalBounds().width;
        if (isLast) {
            // the current one: a pill, quantized widths (shared textures)
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
