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

using namespace c2d;

#define CRUMB_MAX 10
#define CRUMB_CHARS 24

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

    // utf-8 aware cut: a name longer than CRUMB_CHARS ends with ".."
    std::string shorten(const std::string &text) {
        size_t chars = 0, i = 0;
        for (; i < text.size(); i++) {
            if (((unsigned char) text[i] & 0xC0) != 0x80 && ++chars > CRUMB_CHARS) {
                return text.substr(0, i) + "..";
            }
        }
        return text;
    }
}

std::vector<std::string> Breadcrumb::split(const std::string &path) {
    std::vector<std::string> parts;
    std::string rest = path;
    size_t scheme = path.find("://");
    if (scheme != std::string::npos) {
        // the host only: no credentials, no port
        size_t start = scheme + 3, end = path.find('/', start);
        std::string host = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t at = host.rfind('@');
        if (at != std::string::npos) {
            host = host.substr(at + 1);
        }
        size_t colon = host.find(':');
        if (colon != std::string::npos) {
            host = host.substr(0, colon);
        }
        parts.push_back(host);
        rest = end == std::string::npos ? "" : path.substr(end);
    } else {
        parts.push_back(pplay::tr("Local"));
    }
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

    std::vector<std::string> parts = split(path);
    for (auto &part: parts) {
        part = shorten(part);
    }
    // the tail that fits, ".." first for the rest
    auto widthOf = [this](const std::string &text) {
        names[0]->setString(text);
        return names[0]->getLocalBounds().width;
    };
    float arrowWidth = widthOf("›") + 2 * gap;
    std::vector<std::string> shown;
    float width = 0;
    for (int i = (int) parts.size() - 1; i >= 0 && (int) shown.size() < CRUMB_MAX - 1; i--) {
        float w = widthOf(parts[i]) + (shown.empty() ? 2 * pad : arrowWidth);
        if (!shown.empty() && width + w + widthOf("..") + arrowWidth > getSize().x) {
            shown.insert(shown.begin(), "..");
            break;
        }
        width += w;
        shown.insert(shown.begin(), parts[i]);
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
