#include "gfx/android_fonts.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "gfx/text.hpp"

namespace cr3 {

namespace {

const char* const kFontDirs[] = {"/system/fonts/", "/product/fonts/", "/system_ext/fonts/", "/vendor/fonts/"};

bool isFile(const std::string& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

// `key`'s value in a tag ("" if none), the key after white space, either quote
std::string attr(const std::string& tag, const char* key) {
    const std::string k = std::string(key) + "=";
    for (size_t at = tag.find(k); at != std::string::npos; at = tag.find(k, at + 1)) {
        if (at == 0 || !std::isspace(static_cast<unsigned char>(tag[at - 1]))) continue;
        const size_t b = at + k.size();
        if (b >= tag.size() || (tag[b] != '"' && tag[b] != '\'')) continue;
        const size_t e = tag.find(tag[b], b + 1);
        return e == std::string::npos ? std::string() : tag.substr(b + 1, e - b - 1);
    }
    return {};
}

// a family's rank: 0 Japanese, 1 Simplified Chinese, 2 Traditional, 3 Korean, 4 the default family, -1 none
int rank(const std::string& lang, const std::string& name) {
    int best = -1;
    size_t i = 0;
    while (i < lang.size()) {
        size_t e = lang.find_first_of(" ,", i);
        if (e == std::string::npos) e = lang.size();
        const std::string t = lang.substr(i, e - i);
        i = e + 1;
        int r = -1;
        if (t.rfind("ja", 0) == 0 || t.find("Jpan") != std::string::npos) r = 0;
        else if (t.rfind("zh-Hans", 0) == 0 || t.find("Hans") != std::string::npos || t == "zh" || t == "zh-CN") r = 1;
        else if (t.rfind("zh-Hant", 0) == 0 || t.find("Hant") != std::string::npos || t == "zh-TW" || t == "zh-HK") r = 2;
        else if (t.rfind("ko", 0) == 0 || t.find("Kore") != std::string::npos) r = 3;
        if (r >= 0 && (best < 0 || r < best)) best = r;
    }
    if (best < 0 && name == "sans-serif") best = 4;
    return best;
}

} // namespace

std::vector<AndroidFont> androidFonts(const std::string& root) {
    auto locate = [&root](const std::string& name) -> std::string {
        if (!name.empty() && name[0] == '/') return isFile(root + name) ? root + name : std::string();
        for (const char* d : kFontDirs)
            if (const std::string p = root + d + name; isFile(p)) return p;
        return {};
    };
    std::vector<AndroidFont> ranked[5];
    for (const char* xml : {"/product/etc/fonts_customization.xml", "/system/etc/font_fallback.xml", "/system/etc/fonts.xml",
                            "/vendor/etc/fonts.xml", "/system/etc/fallback_fonts.xml", "/system/etc/system_fonts.xml"}) {
        std::ifstream in(root + xml, std::ios::binary);
        if (!in) continue;
        std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (size_t c = s.find("<!--"); c != std::string::npos; c = s.find("<!--", c)) { // (comments out: AOSP's mention tags)
            const size_t e = s.find("-->", c);
            s.erase(c, e == std::string::npos ? std::string::npos : e + 3 - c);
        }
        for (size_t f = s.find("<family"); f != std::string::npos; f = s.find("<family", f + 1)) {
            if (f + 7 >= s.size() || (!std::isspace(static_cast<unsigned char>(s[f + 7])) && s[f + 7] != '>')) continue; // (<familyset>)
            const size_t tagEnd = s.find('>', f), end = s.find("</family>", f);
            if (tagEnd == std::string::npos || end == std::string::npos) break;
            const std::string tag = s.substr(f, tagEnd - f);
            std::string name = attr(tag, "name");
            if (name.empty()) // (the old system_fonts.xml's: <nameset><name>sans-serif</name>...)
                if (size_t n = s.find("<name>", tagEnd); n != std::string::npos && n < end) {
                    const size_t ne = s.find('<', n + 6);
                    if (ne != std::string::npos) name = s.substr(n + 6, ne - n - 6);
                }
            const int familyRank = rank(attr(tag, "lang"), name);
            std::vector<AndroidFont> regular[5], other[5]; // (the 400 weight first)
            // its files: <font ...>Name.ttf</font> (<font path=""/> older), the old lists' <file lang="">Name.ttf</file>
            for (size_t t = s.find('<', tagEnd); t != std::string::npos && t < end; t = s.find('<', t + 1)) {
                const bool fontTag = s.compare(t, 5, "<font") == 0, fileTag = s.compare(t, 5, "<file") == 0;
                if ((!fontTag && !fileTag) || t + 5 >= end) continue;
                if (std::isalpha(static_cast<unsigned char>(s[t + 5])) || s[t + 5] == '-') continue; // (<fontVariation>, <fileset>)
                const size_t te = s.find('>', t);
                if (te == std::string::npos || te > end) break;
                const std::string ftag = s.substr(t, te - t);
                int r = familyRank;
                if (const int own = rank(attr(ftag, "lang"), std::string()); own >= 0 && (r < 0 || own < r)) r = own;
                if (r < 0) continue;
                std::string file;
                if (s[te - 1] != '/') { // (the file the element's text: <font ...>Name.ttc <axis .../></font>)
                    size_t a = te + 1, b = s.find('<', a);
                    if (b == std::string::npos) break;
                    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
                    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
                    file = s.substr(a, b - a);
                }
                if (file.empty()) file = attr(ftag, "path");
                std::string lower = file;
                for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
                // (not italics, nor a serif family's stand-in - fallbackFor="serif")
                if (file.empty() || attr(ftag, "style") == "italic" || lower.find("italic") != std::string::npos ||
                    !attr(ftag, "fallbackFor").empty())
                    continue;
                const std::string w = attr(ftag, "weight"), index = attr(ftag, "index");
                AndroidFont font{file, 0};
                if (!index.empty() && index.size() < 4 &&
                    std::all_of(index.begin(), index.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
                    font.face = std::stoi(index);
                (w == "400" || w.empty() ? regular[r] : other[r]).push_back(font);
            }
            for (int r = 0; r < 5; ++r)
                for (auto* list : {&regular[r], &other[r]})
                    for (AndroidFont& font : *list)
                        if (std::string p = locate(font.path); !p.empty()) ranked[r].push_back({std::move(p), font.face});
        }
    }
    std::vector<AndroidFont> out;
    auto add = [&out](const AndroidFont& font) {
        if (std::none_of(out.begin(), out.end(), [&](const AndroidFont& o) { return o.path == font.path && o.face == font.face; }))
            out.push_back(font);
    };
    for (const auto& list : ranked)
        for (const AndroidFont& font : list) add(font);
    // The font folders themselves (a list that names none of these): CJK-named first, then sans faces, the rest
    std::vector<std::string> cjk, sans, rest;
    for (const char* d : kFontDirs) {
        std::vector<std::string> names;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(root + d, ec), last; !ec && it != last; it.increment(ec))
            if (it->is_regular_file(ec)) names.push_back(it->path().filename().string());
        std::sort(names.begin(), names.end()); // (the folder's order: none)
        for (const std::string& name : names) {
            std::string lower = name;
            for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
            const std::string ext = lower.size() > 4 ? lower.substr(lower.size() - 4) : std::string();
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
            auto has = [&lower](const char* k) { return lower.find(k) != std::string::npos; };
            if (has("emoji") || has("symbol") || has("italic")) continue;
            const std::string path = root + d + name;
            if (has("cjk") || has("jp") || has("sc-") || has("tc-") || has("kr") || has("han")) cjk.push_back(path);
            else if (has("roboto") || has("sans")) sans.push_back(path);
            else rest.push_back(path);
        }
    }
    for (auto* list : {&cjk, &sans, &rest}) {
        std::stable_sort(list->begin(), list->end(), [](const std::string& a, const std::string& b) {
            return a.find("Regular") != std::string::npos && b.find("Regular") == std::string::npos;
        });
        for (const std::string& p : *list) add({p, 0});
    }
    return out;
}

bool loadAndroidFont(TextRasterizer& font, const std::vector<AndroidFont>& list) {
    for (const AndroidFont& f : list) {
        TextRasterizer probe; // (tried apart: a face without Latin letters doesn't replace `font`'s)
        if (probe.load(f.path, nullptr, f.face) && probe.covers("Aa")) return font.load(f.path, nullptr, f.face);
    }
    return false;
}

std::vector<AndroidFont> androidFallbacks(const TextRasterizer& font, const std::vector<AndroidFont>& list) {
    // (kana, Han, a Simplified Chinese hanzi - 们 -, Hangul)
    std::vector<const char*> missing;
    for (const char* s : {"\xE3\x81\x82", "\xE6\xBC\xA2", "\xE4\xBB\xAC", "\xED\x95\x9C"})
        if (!font.loaded() || !font.covers(s)) missing.push_back(s);
    std::vector<AndroidFont> out;
    for (const AndroidFont& f : list) {
        if (missing.empty()) break;
        TextRasterizer probe;
        if (!probe.load(f.path, nullptr, f.face)) continue;
        const auto had = missing.size();
        missing.erase(std::remove_if(missing.begin(), missing.end(), [&probe](const char* s) { return probe.covers(s); }), missing.end());
        if (missing.size() < had) out.push_back(f);
    }
    return out;
}

} // namespace cr3
