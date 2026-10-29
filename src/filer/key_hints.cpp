//
// Preview panel key hints / buttons.
//

#include "main.h"
#include "key_hints.h"

using namespace c2d;

#define PAD_X 10
#define PAD_Y 4
#define GAP_X 10
#define GAP_Y 8

KeyHints::KeyHints(Main *m, float width) : Rectangle({width, 0}), main(m) {}

int KeyHints::idAt(int index) const {
    return index >= 0 && index < (int) hints.size() ? hints[index].id : 0;
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
    hints = list;

    for (auto &hint: hints) {
        auto *text = new C2DText(hint.key + ": " + pplay::tr(hint.action), main->getFontSize(Main::FontSize::Small),
                                 main->getFont());
        auto *item = new RectangleShape(Vector2f{1, 1});
        item->add(text);
        KeyHints::add(item);
        items.push_back(item);
        texts.push_back(text);
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
    float itemHeight = ref.height + pad.y * 2;

    float x = 0, y = 0;
    for (size_t i = 0; i < items.size(); i++) {
        auto *item = items[i];
        auto *text = texts[i];
        FloatRect bounds = text->getLocalBounds();
        float w = bounds.left + bounds.width + pad.x * 2;
        if (x > 0 && x + w > getSize().x) {
            x = 0;
            y += itemHeight + gap.y;
        }
        item->setSize(w, itemHeight);
        item->setPosition(x, y);
        text->setPosition(pad.x, pad.y - ref.top);
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
        } else {
            items[i]->setFillColor(buttons ? Color(255, 255, 255, 28) : Color::Transparent);
            text->setFillColor(buttons ? COLOR_FONT : COLOR_GRAY_LIGHT);
        }
    }
}

void KeyHints::onUpdate() {
    // the accent color may change
    applyColors();
    Rectangle::onUpdate();
}
