//
// c2d::Text::onDraw replaced (pPlay objects are linked first, --allow-multiple-definition):
// the text is drawn on whole pixels.
// The font atlas is point filtered and the glyphs are placed at "font offset * size / 24"
// (half pixels for some sizes: -4 * 27 / 24 = -4.5): there, each glyph samples its texels
// right on their edges and some letters ("р", "ц"...) end up a pixel lower than the others.
//

#include <cmath>

#include "cross2d/c2d.h"

namespace c2d {

    void Text::onDraw(Transform &transform, bool draw) {
        if (!m_font || m_string.empty()) {
            return;
        }

        if (draw) {
            Transform combined = transform * getTransform();
            const float *m = combined.getMatrix();
            // not scaled / rotated (tweens): snap the glyphs origin to the pixel grid
            if (m[0] == 1.f && m[5] == 1.f && m[1] == 0.f && m[4] == 0.f) {
                const float scale = (float) m_characterSize / (float) C2D_DEFAULT_CHAR_SIZE;
                const Vector2f offset = m_font->getOffset();
                const float x = m[12] + offset.x * scale;
                const float y = m[13] + offset.y * scale;
                Transform snap;
                snap.translate(std::round(x) - x, std::round(y) - y);
                combined = snap * combined;
            }
            if (getOutlineThickness() > 0) {
                c2d_renderer->draw(&m_outlineVertices, combined, m_font->getTexture(m_characterSize));
            }
            c2d_renderer->draw(&m_vertices, combined, m_font->getTexture(m_characterSize));
        }
        C2DObject::onDraw(transform, draw);
    }
}
