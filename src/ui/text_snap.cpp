//
// c2d::Text::onDraw replaced (pPlay objects are linked first, --allow-multiple-definition):
// the text is drawn on whole pixels.
// The font atlas is point filtered and the glyphs are placed at "font offset * size / 24"
// (half pixels for some sizes: -4 * 27 / 24 = -4.5): there, each glyph samples its texels
// right on their edges and some letters ("р", "ц"...) end up a pixel lower than the others.
// Its glyph quads checked against its font page size too: made at its update, a page grown
// later (new glyphs of another text in the frame, its own, or while its parent was not
// updated) drew them on the wrong texels (garbage while texts change: a file opened).
//

#include <cmath>

#include "cross2d/c2d.h"

namespace c2d {

    void Text::onDraw(Transform &transform, bool draw) {
        if (!m_font || m_string.empty()) {
            return;
        }

        if (draw) {
            // (a page may grow again while the quads are made)
            for (int pass = 0; pass < 3 && !m_font->isBmFont(); pass++) {
                Vector2i texSize = m_font->getTexture(m_characterSize)->getTextureSize();
                if (texSize == m_textureSize) {
                    break;
                }
                m_textureSize = texSize;
                m_geometryNeedUpdate = true;
                ensureGeometryUpdate();
            }
            Transform combined = transform * getTransform();
            const float *m = combined.getMatrix();
            // not rotated: snap the glyphs origin to the pixel grid (the window scale too)
            if (m[1] == 0.f && m[4] == 0.f) {
                const float scale = (float) m_characterSize / (float) C2D_DEFAULT_CHAR_SIZE;
                const Vector2f offset = m_font->getOffset();
                const float x = m[12] + offset.x * scale * m[0];
                const float y = m[13] + offset.y * scale * m[5];
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
