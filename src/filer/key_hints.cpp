//
// Details panel key hints / buttons.
//

#include <cmath>

#include "main.h"
#include "input_map.h"
#include "key_hints.h"
#include "skin.h"

using namespace c2d;

#define PAD_X 10
#define PAD_Y 4
#define GAP_X 10
#define GAP_Y 8

KeyHints::KeyHints(Main *m, float width) : Rectangle({width, 0}), main(m) {}

int KeyHints::idAt(int index) const {
    return index >= 0 && index < (int) hints.size() ? hints[index].id : 0;
}

int KeyHints::neighbor(int index, int direction) {
    if (index < 0 || index >= (int) items.size()) {
        return -1;
    }
    float y = items[index]->getPosition().y;
    float x = items[index]->getPosition().x + items[index]->getSize().x / 2;
    // the next row y
    float rowY = -1;
    for (auto *item: items) {
        float iy = item->getPosition().y;
        if ((direction < 0 && iy < y && (rowY < 0 || iy > rowY))
            || (direction > 0 && iy > y && (rowY < 0 || iy < rowY))) {
            rowY = iy;
        }
    }
    int best = -1;
    float bestDistance = 0;
    for (int i = 0; i < (int) items.size(); i++) {
        if (rowY < 0 || items[i]->getPosition().y != rowY) {
            continue;
        }
        float distance = std::fabs(items[i]->getPosition().x + items[i]->getSize().x / 2 - x);
        if (best < 0 || distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

void KeyHints::setHints(const std::vector<Hint> &list) {
    // the same, unless the language changed
    if (list == hints && language == pplay::I18n::current()) {
        return;
    }
    language = pplay::I18n::current();
    for (auto *item: items) {
        delete (item);
    }
    items.clear();
    texts.clear();
    icons.clear();
    hints = list;

    for (auto &hint: hints) {
        auto *item = new RectangleShape(Vector2f{1, 1});
        c2d::Texture *glyph = pplay::Skin::texture(pplay::InputMap::buttonIcon(hint.keys));
        Sprite *icon = nullptr;
        std::string label = pplay::tr(hint.action);
        if (glyph != nullptr) {
            icon = new Sprite(glyph);
            item->add(icon);
        } else {
            // no glyph: the key name
            std::string name;
            for (unsigned int bit = 1; bit != 0; bit <<= 1) {
                name += (hint.keys & bit) ? pplay::InputMap::buttonName(bit) : "";
            }
            label = name + " " + label;
        }
        auto *text = new C2DText(label, main->getFontSize(Main::FontSize::Small), main->getFont());
        item->add(text);
        KeyHints::add(item);
        items.push_back(item);
        texts.push_back(text);
        icons.push_back(icon);
    }
    if (focus >= (int) items.size()) {
        focus = (int) items.size() - 1;
    }
    layout();
    applyColors();
}

void KeyHints::setButtons(bool value) {
    buttons = value;
    if (!buttons) {
        focus = -1;
    }
    layout();
    applyColors();
}

void KeyHints::setFocus(int index) {
    focus = index < (int) items.size() ? index : (int) items.size() - 1;
    applyColors();
}

void KeyHints::layout() {
    // plain hints: aligned with the panel texts
    Vector2f pad = main->getScaled(buttons ? PAD_X : 0, PAD_Y);
    Vector2f gap = main->getScaled(buttons ? GAP_X : GAP_X * 3, GAP_Y);
    // the same height for all, whatever their letters
    C2DText probe("Ay", main->getFontSize(Main::FontSize::Small), main->getFont());
    FloatRect ref = probe.getLocalBounds();
    float glyphHeight = std::round(ref.height * 1.3f);
    float itemHeight = glyphHeight + pad.y * 2;

    float x = 0, y = 0;
    for (size_t i = 0; i < items.size(); i++) {
        auto *item = items[i];
        auto *text = texts[i];
        // the glyph a bit higher than the letters, then the action
        float iconWidth = 0;
        if (icons[i] != nullptr) {
            float scale = glyphHeight / (float) icons[i]->getTextureRect().height;
            icons[i]->setScale(scale, scale);
            iconWidth = (float) icons[i]->getTextureRect().width * scale + 6 * main->getScaling().x;
            icons[i]->setPosition(pad.x, pad.y);
        }
        FloatRect bounds = text->getLocalBounds();
        float w = iconWidth + bounds.left + bounds.width + pad.x * 2;
        if (x > 0 && x + w > getSize().x) {
            x = 0;
            y += itemHeight + gap.y;
        }
        item->setSize(w, itemHeight);
        item->setPosition(x, y);
        text->setPosition(pad.x + iconWidth, std::round((itemHeight - ref.height) / 2 - ref.top));
        x += w + gap.x;
    }
    height = items.empty() ? 0 : y + itemHeight;
    setSize(getSize().x, height);
}

void KeyHints::applyColors() {
    for (int i = 0; i < (int) items.size(); i++) {
        auto *text = texts[i];
        if (buttons && i == focus) {
            items[i]->setFillColor(COLOR_ACCENT);
            text->setFillColor(COLOR_WHITE);
            if (icons[i] != nullptr) {
                icons[i]->setFillColor(COLOR_WHITE);
            }
        } else {
            items[i]->setFillColor(buttons ? Color(255, 255, 255, 28) : Color::Transparent);
            text->setFillColor(buttons ? COLOR_FONT : COLOR_GRAY_LIGHT);
            if (icons[i] != nullptr) {
                icons[i]->setFillColor(buttons ? COLOR_FONT : COLOR_GRAY_LIGHT);
            }
        }
    }
}

void KeyHints::onUpdate() {
    // the accent color may change
    applyColors();
    Rectangle::onUpdate();
}
