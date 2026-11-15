// cr3::androidFonts on made-up Android systems (no device): the order the port tries a phone's fonts in when
// the AOSP files (Roboto-Regular.ttf, NotoSansCJK-Regular.ttc) aren't where AOSP puts them (CR2-U #4). Then,
// with this machine's own fonts linked into a made-up system (skipped without them or FreeType), the main
// font and the CJK fallbacks the viewer would take from it.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "gfx/android_fonts.hpp"
#include "gfx/text.hpp"

namespace fs = std::filesystem;

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) ++failures, std::printf("FAIL: %s\n", what);
}

void put(const std::string& path, const std::string& text = "x") {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string rel(const std::string& root, const std::string& p) { return p.rfind(root, 0) == 0 ? p.substr(root.size()) : p; }

// where `p` (face `face`) is in the list; -1: not in it
int at(const std::vector<cr3::AndroidFont>& list, const std::string& root, const char* p, int face = 0) {
    for (size_t i = 0; i < list.size(); ++i)
        if (rel(root, list[i].path) == p && list[i].face == face) return int(i);
    return -1;
}

void dump(const char* name, const std::string& root, const std::vector<cr3::AndroidFont>& list) {
    std::printf("%s:", name);
    for (const cr3::AndroidFont& f : list) std::printf(f.face ? " %s#%d" : " %s", rel(root, f.path).c_str(), f.face);
    std::printf("\n");
}

std::string firstExisting(std::initializer_list<const char*> paths) {
    std::error_code ec;
    for (const char* p : paths)
        if (fs::is_regular_file(p, ec)) return p;
    return {};
}
} // namespace

int main() {
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec);
    const std::string base = (ec ? fs::path(".") : tmp).string() + "/cr3-android-fonts-test";
    fs::remove_all(base, ec); // (an earlier run's - only files this test made)

    // AOSP (Android 12 - 14): fonts.xml, Roboto at every weight, the CJK collection's faces per language
    // (ja 0, ko 1, zh-Hans 2, zh-Hant 3), the serif collection only as serif's stand-in; a comment that
    // mentions a family
    {
        const std::string r = base + "/aosp";
        put(r + "/system/etc/fonts.xml", R"(<?xml version="1.0" encoding="utf-8"?>
<!-- <family lang="ja"><font weight="400">Commented.ttf</font></family> -->
<familyset version="23">
    <family name="sans-serif">
        <font weight="100" style="normal">Roboto-Regular.ttf
            <axis tag="wght" stylevalue="100" />
        </font>
        <font weight="400" style="normal">Roboto-Regular.ttf
            <axis tag="wght" stylevalue="400" />
        </font>
        <font weight="400" style="italic">Roboto-Italic.ttf</font>
    </family>
    <alias name="sans-serif-thin" to="sans-serif" weight="100" />
    <family lang="zh-Hans">
        <font weight="400" style="normal" index="2">NotoSansCJK-Regular.ttc</font>
        <font weight="400" style="normal" index="2"
              fallbackFor="serif">NotoSerifCJK-Regular.ttc</font>
    </family>
    <family lang="zh-Hant,zh-Bopo">
        <font weight="400" style="normal" index="3">NotoSansCJK-Regular.ttc</font>
    </family>
    <family lang="ja">
        <font weight="400" style="normal" index="0">NotoSansCJK-Regular.ttc</font>
    </family>
    <family lang="ko">
        <font weight="400" style="normal" index="1">NotoSansCJK-Regular.ttc</font>
    </family>
    <family lang="und-Thai" variant="elegant">
        <font weight="400" style="normal">NotoSansThai-Regular.ttf</font>
    </family>
</familyset>
)");
        for (const char* f : {"Roboto-Regular.ttf", "Roboto-Italic.ttf", "NotoSansCJK-Regular.ttc", "NotoSerifCJK-Regular.ttc",
                              "NotoSansThai-Regular.ttf", "Commented.ttf"})
            put(r + "/system/fonts/" + f);
        const auto list = cr3::androidFonts(r);
        dump("aosp", r, list);
        check(at(list, r, "/system/fonts/NotoSansCJK-Regular.ttc", 0) == 0, "AOSP: Japanese's face first");
        check(at(list, r, "/system/fonts/NotoSansCJK-Regular.ttc", 2) == 1, "AOSP: Simplified Chinese's face next");
        check(at(list, r, "/system/fonts/NotoSansCJK-Regular.ttc", 3) == 2, "AOSP: Traditional Chinese's face next");
        check(at(list, r, "/system/fonts/NotoSansCJK-Regular.ttc", 1) == 3, "AOSP: Korean's face after Chinese");
        check(at(list, r, "/system/fonts/Roboto-Regular.ttf") == 4, "AOSP: the sans-serif family after the CJK ones");
        check(at(list, r, "/system/fonts/NotoSerifCJK-Regular.ttc") > 4, "AOSP: the serif stand-in only from the folder scan");
        check(at(list, r, "/system/fonts/Roboto-Italic.ttf") < 0, "AOSP: no italics");
        check(at(list, r, "/system/fonts/Commented.ttf") > 4, "AOSP: a commented-out family not ranked");
    }

    // AOSP Android 15: font_fallback.xml, variable Roboto (one file at every weight), the CJK collection for
    // Simplified Chinese with the serif collection as its serif stand-in
    {
        const std::string r = base + "/aosp15";
        put(r + "/system/etc/font_fallback.xml", R"(<?xml version="1.0" encoding="utf-8"?>
<familyset>
    <family name="sans-serif" supportedAxes="wght,ital">
        <font weight="100" style="normal">Roboto-Regular.ttf
            <axis tag="wdth" stylevalue="100" />
            <axis tag="wght" stylevalue="100" />
        </font>
        <font weight="400" style="normal">Roboto-Regular.ttf
            <axis tag="wght" stylevalue="400" />
        </font>
        <font weight="400" style="italic">Roboto-Regular.ttf
            <axis tag="ital" stylevalue="1" />
        </font>
    </family>
    <family lang="und-Arab" variant="elegant">
        <font weight="400" style="normal" postScriptName="NotoNaskhArabic">NotoNaskhArabic-Regular.ttf</font>
    </family>
    <family lang="zh-Hans">
        <font weight="400" style="normal" index="2" postScriptName="NotoSansCJKjp-Regular">
            NotoSansCJK-Regular.ttc
            <axis tag="wght" stylevalue="400" />
        </font>
        <font weight="400" style="normal" index="2" fallbackFor="serif"
              postScriptName="NotoSerifCJKjp-Regular">NotoSerifCJK-Regular.ttc
        </font>
    </family>
</familyset>
)");
        for (const char* f : {"Roboto-Regular.ttf", "NotoNaskhArabic-Regular.ttf", "NotoSansCJK-Regular.ttc", "NotoSerifCJK-Regular.ttc"})
            put(r + "/system/fonts/" + f);
        const auto list = cr3::androidFonts(r);
        dump("aosp15", r, list);
        check(at(list, r, "/system/fonts/NotoSansCJK-Regular.ttc", 2) == 0, "AOSP 15: the CJK collection's Chinese face first");
        check(at(list, r, "/system/fonts/Roboto-Regular.ttf") == 1, "AOSP 15: the sans-serif family next");
        check(at(list, r, "/system/fonts/NotoSerifCJK-Regular.ttc") > 1, "AOSP 15: the serif stand-in only from the folder scan");
    }

    // A Helio phone's kind: none of the AOSP files; the font list and its fonts on the vendor partition only,
    // one family for every CJK language, Korean listed before it
    {
        const std::string r = base + "/vendor";
        put(r + "/vendor/etc/fonts.xml", R"(<familyset version="23">
<family name="sans-serif"><font weight="400" style="normal">MtkSans-Regular.ttf</font>
<font weight="700" style="normal">MtkSans-Bold.ttf</font></family>
<family lang="ko"><font weight="400" style="normal">MtkKorean.ttf</font></family>
<family lang="zh-Hans,zh-Hant,ja"><font weight="400" style="normal">MtkFallback.ttf</font></family>
</familyset>
)");
        for (const char* f : {"MtkSans-Regular.ttf", "MtkSans-Bold.ttf", "MtkKorean.ttf", "MtkFallback.ttf"}) put(r + "/vendor/fonts/" + f);
        put(r + "/system/fonts/NotoColorEmoji.ttf");
        const auto list = cr3::androidFonts(r);
        dump("vendor", r, list);
        check(at(list, r, "/vendor/fonts/MtkFallback.ttf") == 0, "vendor: the family with Japanese first");
        check(at(list, r, "/vendor/fonts/MtkKorean.ttf") == 1, "vendor: Korean next");
        check(at(list, r, "/vendor/fonts/MtkSans-Regular.ttf") == 2, "vendor: the default family's regular weight");
        check(at(list, r, "/vendor/fonts/MtkSans-Bold.ttf") == 3, "vendor: then its bold");
        check(at(list, r, "/system/fonts/NotoColorEmoji.ttf") < 0, "vendor: no emoji font");
    }

    // A maker's own names in /system/etc/fonts.xml, the CJK font on the product partition; Japanese's regular
    // weight before its bold; a listed file that isn't there
    {
        const std::string r = base + "/oem";
        put(r + "/system/etc/fonts.xml", R"(<familyset version="23">
<family name="sans-serif"><font weight="400" style="normal">MiSansVF.ttf</font></family>
<family lang="ko"><font weight="400" style="normal">OemKorean.ttf</font></family>
<family lang="zh-Hans zh-Hant"><font weight="400" style="normal">MiSansFallback.ttf</font></family>
<family lang="ja"><font weight="700" style="normal">OemGothic-Bold.ttf</font><font weight="400" style="normal">OemGothic.ttf</font></family>
<family lang="ja"><font weight="400" style="normal">Missing.ttf</font></family>
</familyset>
)");
        for (const char* f : {"MiSansVF.ttf", "OemKorean.ttf", "OemGothic.ttf", "OemGothic-Bold.ttf"}) put(r + "/system/fonts/" + f);
        put(r + "/product/fonts/MiSansFallback.ttf");
        const auto list = cr3::androidFonts(r);
        dump("oem", r, list);
        check(at(list, r, "/system/fonts/OemGothic.ttf") == 0, "OEM: Japanese's regular weight first");
        check(at(list, r, "/system/fonts/OemGothic-Bold.ttf") == 1, "OEM: Japanese's bold next");
        check(at(list, r, "/product/fonts/MiSansFallback.ttf") == 2, "OEM: Chinese on the product partition, found");
        check(at(list, r, "/system/fonts/OemKorean.ttf") == 3, "OEM: Korean after Chinese");
        check(at(list, r, "/system/fonts/MiSansVF.ttf") == 4, "OEM: the default family after the CJK ones");
        check(at(list, r, "/system/fonts/Missing.ttf") < 0 && at(list, r, "/product/fonts/Missing.ttf") < 0, "OEM: a file not there left out");
    }

    // Android 4's lists: system_fonts.xml (<nameset> / <fileset>), fallback_fonts.xml with a language on a file
    {
        const std::string r = base + "/old";
        put(r + "/system/etc/system_fonts.xml", R"(<familyset>
    <family>
        <nameset><name>sans-serif</name><name>arial</name></nameset>
        <fileset>
            <file>Roboto-Regular.ttf</file>
            <file>Roboto-Bold.ttf</file>
            <file>Roboto-Italic.ttf</file>
        </fileset>
    </family>
    <family>
        <nameset><name>serif</name></nameset>
        <fileset><file>NotoSerif-Regular.ttf</file></fileset>
    </family>
</familyset>
)");
        put(r + "/system/etc/fallback_fonts.xml", R"(<familyset>
    <family><fileset><file variant="elegant">DroidNaskh-Regular.ttf</file></fileset></family>
    <family><fileset><file lang="ja">MTLmr3m.ttf</file></fileset></family>
</familyset>
)");
        for (const char* f : {"Roboto-Regular.ttf", "Roboto-Bold.ttf", "Roboto-Italic.ttf", "NotoSerif-Regular.ttf", "DroidNaskh-Regular.ttf",
                              "MTLmr3m.ttf"})
            put(r + "/system/fonts/" + f);
        const auto list = cr3::androidFonts(r);
        dump("old", r, list);
        check(at(list, r, "/system/fonts/MTLmr3m.ttf") == 0, "old: the Japanese file first");
        check(at(list, r, "/system/fonts/Roboto-Regular.ttf") == 1, "old: sans-serif's regular next");
        check(at(list, r, "/system/fonts/Roboto-Bold.ttf") == 2, "old: then its bold");
        check(at(list, r, "/system/fonts/Roboto-Italic.ttf") < 0, "old: no italics");
        check(at(list, r, "/system/fonts/NotoSerif-Regular.ttf") > 2, "old: the serif family only from the folder scan");
    }

    // No font list at all: the folders' files, CJK-named first, sans faces next, no emoji or italics
    {
        const std::string r = base + "/bare";
        for (const char* f : {"HarmonyOS_Sans.ttf", "NotoColorEmoji.ttf", "SourceHanSansCN-Regular.otf", "Roboto-Italic.ttf",
                              "Clock.ttf", "readme.txt"})
            put(r + "/system/fonts/" + f);
        const auto list = cr3::androidFonts(r);
        dump("bare", r, list);
        check(at(list, r, "/system/fonts/SourceHanSansCN-Regular.otf") == 0, "bare: the CJK-named font first");
        check(at(list, r, "/system/fonts/HarmonyOS_Sans.ttf") == 1, "bare: a sans face next");
        check(at(list, r, "/system/fonts/Clock.ttf") == 2, "bare: the rest last");
        check(at(list, r, "/system/fonts/NotoColorEmoji.ttf") < 0, "bare: no emoji font");
        check(at(list, r, "/system/fonts/Roboto-Italic.ttf") < 0, "bare: no italics");
        check(at(list, r, "/system/fonts/readme.txt") < 0, "bare: fonts only");
    }

    check(cr3::androidFonts(base + "/none").empty(), "nothing there: nothing listed");

    // The viewer's picks from a list, with real fonts (this machine's, linked in): a Japanese family whose
    // file isn't a font, a CJK collection listed for Korean (face 1), a Latin-only default family
    const std::string latin = firstExisting({"/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                                             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                             "/usr/share/fonts/liberation/LiberationSans-Regular.ttf"});
    const std::string cjk = firstExisting({"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
                                           "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                                           "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc"});
    cr3::TextRasterizer probe;
    if (latin.empty() || cjk.empty() || !probe.load(latin)) {
        std::printf("real fonts: skipped (no Latin and CJK font here, or no FreeType)\n");
    } else {
        const std::string r = base + "/real";
        put(r + "/vendor/etc/fonts.xml", R"(<familyset>
<family lang="ja"><font weight="400">Broken.ttf</font></family>
<family lang="zh-Hans"><font weight="400">OemSans.ttf</font></family>
<family lang="ko"><font weight="400" index="1">OemCJK.ttc</font></family>
<family name="sans-serif"><font weight="400">OemSans.ttf</font></family>
</familyset>
)");
        put(r + "/vendor/fonts/Broken.ttf", "not a font");
        fs::create_symlink(latin, r + "/vendor/fonts/OemSans.ttf", ec);
        fs::create_symlink(cjk, r + "/vendor/fonts/OemCJK.ttc", ec);
        const auto list = cr3::androidFonts(r);
        dump("real", r, list);
        cr3::TextRasterizer main;
        check(cr3::loadAndroidFont(main, list) && rel(r, main.path()) == "/vendor/fonts/OemSans.ttf",
              "real: the main font the first that loads with Latin letters");
        check(cr3::androidFallbacks(main, list).size() == 1 && at(cr3::androidFallbacks(main, list), r, "/vendor/fonts/OemCJK.ttc", 1) == 0,
              "real: the CJK collection's listed face the one fallback for a Latin font");
        cr3::TextRasterizer whole;
        whole.load(cjk);
        check(cr3::androidFallbacks(whole, list).empty(), "real: no fallback for a font with every script");
        cr3::TextRasterizer untouched;
        check(!cr3::loadAndroidFont(untouched, {{r + "/vendor/fonts/Broken.ttf", 0}}) && !untouched.loaded(),
              "real: nothing loaded from a list without a font");
    }

    fs::remove_all(base, ec);
    std::printf(failures ? "%d failure(s)\n" : "android fonts: all passed\n", failures);
    return failures ? 1 : 0;
}
