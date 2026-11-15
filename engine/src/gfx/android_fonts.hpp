#pragma once
// Android's system fonts beyond the AOSP file names (CR2-U #4: a Helio G100 phone's system has none of
// /system/fonts/Roboto-Regular.ttf, NotoSansCJK-Regular.ttc ... - its maker names and places its own). The
// system's own font lists name each family's files and languages; the port reads them after its fixed
// paths, then the font folders themselves. Compiled everywhere (its test runs on made-up systems), called
// on Android only.

#include <string>
#include <vector>

namespace cr3 {

class TextRasterizer;

struct AndroidFont {
    std::string path;
    int face = 0; // (a collection's: the list's index="")
};

// The system's fonts under `root` ("" on a device) in the order to try: from its font lists
// (/product/etc/fonts_customization.xml, /system/etc/font_fallback.xml - Android 15 -, /system/etc/fonts.xml,
// /vendor/etc/fonts.xml, the old fallback_fonts.xml / system_fonts.xml) the Japanese families' first, then
// Simplified and Traditional Chinese, Korean, the default sans-serif family (a family's 400 weight first; no
// italics, nor a serif family's stand-ins - fallbackFor=""); each file looked for in /system, /product,
// /system_ext and /vendor fonts/. Then the font folders' other files: CJK-named first, then sans faces, the
// rest (no emoji, symbol or italic files)
std::vector<AndroidFont> androidFonts(const std::string& root = std::string());

// The first of `list` that loads and has Latin letters ("Aa") into `font`; false (`font` untouched): none
bool loadAndroidFont(TextRasterizer& font, const std::vector<AndroidFont>& list);

// Fallbacks for the scripts `font` lacks - kana, Han, Simplified Chinese, Hangul -: each of `list`, in its
// order, that has one still missing, until none is
std::vector<AndroidFont> androidFallbacks(const TextRasterizer& font, const std::vector<AndroidFont>& list);

} // namespace cr3
