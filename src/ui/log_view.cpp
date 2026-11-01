//
// Log view (see log_view.h).
//

#include <algorithm>
#include <cstdio>

#include "main.h"
#include "log_view.h"
#include "input_map.h"
#include "text_wrap.h"
#include "i18n.h"

using namespace c2d;

// the end of a big log only
#define LOG_TAIL_BYTES (192 * 1024)

LogView::LogView(Main *m) : RectangleShape(m->getSize()), main(m) {
    const Vector2f size = main->getSize();
    const float scale = main->getScaling().y;
    const float margin = 24 * scale;

    // as the line editor (Linux)
    LogView::setFillColor(Color(0, 0, 0, 170));
    box = new RectangleShape({size.x * 0.06f, size.y * 0.06f, size.x * 0.88f, size.y * 0.88f});
    box->setFillColor(COLOR_BG);
    box->setOutlineThickness(2 * scale);
    LogView::add(box);

    title = new Text("", main->getFontSize(Main::FontSize::Big), main->getFont());
    title->setFillColor(COLOR_FONT);
    title->setPosition(margin, margin);
    box->add(title);

    hint = new Text("", main->getFontSize(Main::FontSize::Small), main->getFont());
    hint->setFillColor(COLOR_GRAY_LIGHT);
    hint->setOrigin(Origin::BottomLeft);
    hint->setPosition(margin, box->getSize().y - margin);
    box->add(hint);

    const unsigned int fontSize = main->getFontSize(Main::FontSize::XS);
    const float top = margin * 2 + (float) main->getFontSize(Main::FontSize::Big);
    const float height = box->getSize().y - top - margin * 2 - (float) main->getFontSize(Main::FontSize::Small);
    body = new Text("", fontSize, main->getFont());
    body->setFillColor(COLOR_FONT);
    body->setLineSpacingModifier((int) (2 * scale));
    body->setPosition(margin, top);
    bodySize = {box->getSize().x - margin * 2, height};
    body->setSizeMax(bodySize);
    box->add(body);
    maxLines = std::max(1, (int) (height / ((float) fontSize + 2 * scale) * 0.92f));

    scrollBar = new RectangleShape(Vector2f{std::max(2.0f, 3 * scale), 1});
    scrollBar->setFillColor(Color(255, 255, 255, 60));
    scrollBar->setPosition(box->getSize().x - margin / 2, top);
    box->add(scrollBar);

    LogView::setVisibility(Visibility::Hidden);
}

void LogView::load() {
    lines.clear();
    std::string text;
    std::string path = pplay::Utility::getLogPath();
    if (FILE *f = path.empty() ? nullptr : fopen(path.c_str(), "rb")) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        long from = std::max(0L, size - (long) LOG_TAIL_BYTES);
        fseek(f, from, SEEK_SET);
        text.resize((size_t) (size - from));
        text.resize(fread(&text[0], 1, text.size(), f));
        fclose(f);
        // a cut line, the bom
        if (from > 0) {
            size_t eol = text.find('\n');
            text.erase(0, eol == std::string::npos ? text.size() : eol + 1);
        } else if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            text.erase(0, 3);
        }
    }
    const float width = bodySize.x;
    size_t start = 0;
    while (start < text.size()) {
        size_t eol = text.find('\n', start);
        std::string line = text.substr(start, eol == std::string::npos ? std::string::npos : eol - start);
        start = eol == std::string::npos ? text.size() : eol + 1;
        // the time: date and milliseconds kept, the raw clock not
        size_t raw = line.find(" UTC (");
        size_t bar = raw == std::string::npos ? raw : line.find(") | ", raw);
        if (bar != std::string::npos) {
            line = line.substr(0, raw) + " " + line.substr(bar + 4);
        }
        std::string wrapped = pplay::TextWrap::wrap(main->getFont(), body->getCharacterSize(), line, width);
        size_t from = 0;
        while (true) {
            size_t nl = wrapped.find('\n', from);
            lines.push_back(wrapped.substr(from, nl == std::string::npos ? std::string::npos : nl - from));
            if (nl == std::string::npos) {
                break;
            }
            from = nl + 1;
        }
    }
    if (lines.empty()) {
        lines.emplace_back(pplay::tr("<Empty>"));
    }
    first = std::max(0, (int) lines.size() - maxLines);
}

void LogView::show() {
    using pplay::InputMap::buttonName;
    title->setString(pplay::tr("Log") + ": " + pplay::Utility::getLogPath());
    hint->setString(buttonName(Input::Up) + buttonName(Input::Down) + " / " + buttonName(Input::Left)
                    + buttonName(Input::Right) + ": " + pplay::tr("Scroll") + "   " + buttonName(Input::Y) + ": "
                    + pplay::tr("Clear log") + "   " + buttonName(Input::B) + ": " + pplay::tr("Back"));
    box->setOutlineColor(COLOR_ACCENT);
    load();
    render();
    setVisibility(Visibility::Visible);
}

void LogView::render() {
    first = std::max(0, std::min(first, (int) lines.size() - maxLines));
    std::string text;
    for (int i = first; i < (int) lines.size() && i < first + maxLines; i++) {
        text += (i > first ? "\n" : "") + (lines[i].empty() ? " " : lines[i]);
    }
    body->setString(text);

    float height = bodySize.y;
    if ((int) lines.size() <= maxLines) {
        scrollBar->setVisibility(Visibility::Hidden);
        return;
    }
    float barHeight = std::max(height * (float) maxLines / (float) lines.size(), 24 * main->getScaling().y);
    float shown = (float) first / (float) (lines.size() - maxLines);
    scrollBar->setSize(scrollBar->getSize().x, barHeight);
    scrollBar->setPosition(scrollBar->getPosition().x, body->getPosition().y + (height - barHeight) * shown);
    scrollBar->setVisibility(Visibility::Visible);
}

bool LogView::onInput(c2d::Input::Player *players) {
    unsigned int keys = players[0].buttons;
    if (keys & Input::B) {
        setVisibility(Visibility::Hidden);
        return true;
    }
    if (keys & Input::Y) {
        pplay::Utility::clearLog();
        main->getStatus()->show(pplay::tr("Info"), pplay::tr("Log cleared"));
        load();
    } else if (keys & (Input::Up | Input::Down)) {
        first += (keys & Input::Up) ? -1 : 1;
    } else if (keys & (Input::Left | Input::Right | Input::LB | Input::RB)) {
        first += ((keys & (Input::Left | Input::LB)) ? -1 : 1) * std::max(1, maxLines - 1);
    }
    render();
    return true;
}
