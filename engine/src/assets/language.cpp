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

} // namespace

bool fanPatchedInPlace(const Database& db) { return db.kind() == Database::Kind::Vita && slot0Ascii(db) > 0.9; }

std::vector<LanguageChoice> releaseLanguages(const Database& db) {
    std::vector<LanguageChoice> out;
    if (db.kind() == Database::Kind::Vita) {
        if (fanPatchedInPlace(db)) out.push_back({Language::FanEnglish, "English (fan translation)", 0, false});
        else out.push_back({Language::Japanese, "Japanese", 0, false});
    } else {
        out.push_back({Language::Japanese, "Japanese", 0, false});
        out.push_back({Language::English, "English", 1, false});
    }
    if (db.hasOverlay() && out.front().id != Language::FanEnglish)
        out.push_back({Language::FanEnglish, "English (fan translation)", 0, true});
    return out;
}

LanguageChoice applyLanguage(Database& db, Language want) {
    const std::vector<LanguageChoice> all = releaseLanguages(db);
    LanguageChoice pick = all.front();
    for (const LanguageChoice& c : all)
        if (c.id == want) pick = c;
    // (English asked for where only the other English is: that one)
    if (pick.id != want && want != Language::Japanese)
        for (const LanguageChoice& c : all)
            if (c.id == Language::English || c.id == Language::FanEnglish) pick = c;
    db.setOverlayActive(pick.overlay);
    return pick;
}

const char* languageKey(Language l) {
    switch (l) {
    case Language::Japanese: return "ja";
    case Language::English: return "en";
    default: return "fan-en";
    }
}

bool parseLanguage(const std::string& key, Language& out) {
    if (key == "ja" || key == "jp" || key == "japanese") return out = Language::Japanese, true;
    if (key == "en" || key == "english") return out = Language::English, true;
    if (key == "fan-en" || key == "fan" || key == "fan-english") return out = Language::FanEnglish, true;
    return false;
}

} // namespace cr3
