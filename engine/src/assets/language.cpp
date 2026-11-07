#include "assets/language.hpp"

#include "assets/text.hpp"

namespace cr3 {

namespace {

// how much of slot 0 of the release's own interface text is ASCII (Japanese: almost none)
double slot0Ascii(const Database& db) {
    const Database::Entry* e = nullptr;
    for (const Database::Entry& x : db.entries()) // (its own copy, never the overlay's)
        if (Database::key(x.name) == "message\\texts.dat") e = &x;
    std::vector<uint8_t> d;
    TextData t;
    if (!e || !db.read(*e, d) || !t.parse(d)) return 0;
    size_t ascii = 0, all = 0;
    for (int n = 1; n <= t.count(); ++n)
        for (unsigned char c : t.message(n, 0)) all += 1, ascii += c < 0x80;
    return all ? double(ascii) / double(all) : 0;
}

// a fan translation's slot 0 (the release's own copies, or an overlay's): Spanish where its text
// has Spanish's letters - ñ, á é í ó ú ü, ¿ ¡ (UTF-8) - at all often, else English
Language fanTextLanguage(const Database& db, int overlay) {
    static const char* const kSpanish[] = {"\xc3\xb1", "\xc3\xa1", "\xc3\xa9", "\xc3\xad", "\xc3\xb3", "\xc3\xba", "\xc3\xbc", "\xc2\xbf", "\xc2\xa1"};
    size_t letters = 0, spanish = 0, files = 0;
    for (const Database::Entry& e : overlay >= 0 ? db.overlayEntries(size_t(overlay)) : db.entries()) {
        const std::string k = Database::key(e.name);
        if (k.compare(0, 8, "message\\") != 0 || k.size() < 4 || k.compare(k.size() - 4, 4, ".dat") != 0) continue;
        std::vector<uint8_t> d;
        TextData t;
        if (!db.read(e, d) || !t.parse(d)) continue;
        for (int n = 1; n <= t.count(); ++n) {
            const std::string m = t.message(n, 0);
            letters += m.size();
            for (const char* sp : kSpanish)
                for (size_t at = m.find(sp); at != std::string::npos; at = m.find(sp, at + 2)) ++spanish;
        }
        if (++files >= 40 && letters > 20000) break; // (enough to tell)
    }
    return letters && double(spanish) / double(letters) > 0.001 ? Language::FanSpanish : Language::FanEnglish;
}
const char* fanLabel(Language l) { return l == Language::FanSpanish ? "Spanish (fan translation)" : "English (fan translation)"; }

} // namespace

bool fanLanguage(Language l) { return l == Language::FanEnglish || l == Language::FanSpanish; }

Language overlayLanguage(const Database& db, size_t overlay) { return fanTextLanguage(db, int(overlay)); }

int pictureSlot(Language l) { return l == Language::Japanese ? 0 : l == Language::FanSpanish ? 4 : 1; }

bool fanPatchedInPlace(const Database& db) { return db.kind() == Database::Kind::Vita && slot0Ascii(db) > 0.9; }

std::vector<LanguageChoice> releaseLanguages(const Database& db) {
    std::vector<LanguageChoice> out;
    if (db.kind() == Database::Kind::Vita) {
        if (fanPatchedInPlace(db)) {
            const Language l = fanTextLanguage(db, -1);
            out.push_back({l, fanLabel(l), 0, -1});
        }
        else out.push_back({Language::Japanese, "Japanese", 0, -1});
    } else {
        out.push_back({Language::Japanese, "Japanese", 0, -1});
        out.push_back({Language::English, "English", 1, -1});
    }
    // each fan translation over it whose language it hasn't yet (two of one: the first). Over a
    // copy patched in place (its slot 0 not Japanese) the overlays' text would only stand for its own
    for (size_t i = 0; i < db.overlayCount() && !fanLanguage(out.front().id); ++i) {
        const Language l = fanTextLanguage(db, int(i));
        bool have = false;
        for (const LanguageChoice& c : out) have = have || c.id == l;
        if (!have) out.push_back({l, fanLabel(l), 0, int(i)});
    }
    return out;
}

LanguageChoice applyLanguage(Database& db, Language want) {
    const std::vector<LanguageChoice> all = releaseLanguages(db);
    LanguageChoice pick = all.front();
    for (const LanguageChoice& c : all)
        if (c.id == want) pick = c;
    // (a translation asked for where only another is - the release's English, a fan translation: that one)
    if (pick.id != want && want != Language::Japanese)
        for (const LanguageChoice& c : all)
            if (c.id == Language::English || fanLanguage(c.id)) pick = c;
    db.setActiveOverlay(pick.overlay);
    return pick;
}

const char* languageKey(Language l) {
    switch (l) {
    case Language::Japanese: return "ja";
    case Language::English: return "en";
    case Language::FanSpanish: return "fan-es";
    default: return "fan-en";
    }
}

bool parseLanguage(const std::string& key, Language& out) {
    if (key == "ja" || key == "jp" || key == "japanese") return out = Language::Japanese, true;
    if (key == "en" || key == "english") return out = Language::English, true;
    if (key == "fan-en" || key == "fan" || key == "fan-english") return out = Language::FanEnglish, true;
    if (key == "fan-es" || key == "es" || key == "spanish" || key == "fan-spanish") return out = Language::FanSpanish, true;
    return false;
}

} // namespace cr3
