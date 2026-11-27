#pragma once
// A line of text's outline in one image (Options > Graphics > Text Outline: Fixed): the glyphs'
// coverage dilated by a disc `radius` pixels across, less the glyphs' own - drawn once, black,
// under the white text. The original's eight offset copies (ProvisionalMenuText's bordering)
// overlap at their antialiased edges, darker where they do. From metastigma's CRO-U fork
// (CRO-U 109c216's gfx/font_outline.hpp), here padded by the radius on every side: the outline
// of a glyph at the bitmap's edge isn't cut off.

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "assets/image.hpp"

namespace cr3 {

// `glyph`: white, its coverage in alpha (TextRasterizer::render's). `out`: (w + 2r) x (h + 2r), white,
// the outline's coverage in alpha; false: nothing to make
inline bool makeGlyphOutline(const Image& glyph, int radius, Image& out) {
    if (glyph.width <= 0 || glyph.height <= 0 || radius <= 0 || radius > 64) return false;
    if (glyph.rgba.size() < size_t(glyph.width) * size_t(glyph.height) * 4) return false;
    const int w = glyph.width + 2 * radius, h = glyph.height + 2 * radius, r2 = radius * radius;
    auto alpha = [&](int x, int y) -> uint8_t { // (the glyph's, in the padded frame; 0 outside it)
        x -= radius, y -= radius;
        if (x < 0 || y < 0 || x >= glyph.width || y >= glyph.height) return 0;
        return glyph.rgba[(size_t(y) * size_t(glyph.width) + size_t(x)) * 4 + 3];
    };
    out = {};
    out.width = w, out.height = h;
    out.rgba.assign(size_t(w) * size_t(h) * 4, 255);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint8_t face = alpha(x, y);
            uint8_t grown = face;
            for (int dy = -radius; dy <= radius && grown < 255; ++dy)
                for (int dx = -radius; dx <= radius; ++dx)
                    if (dx * dx + dy * dy <= r2) grown = std::max(grown, alpha(x + dx, y + dy));
            out.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3] = grown > face ? uint8_t(grown - face) : uint8_t(0);
        }
    return true;
}

} // namespace cr3
