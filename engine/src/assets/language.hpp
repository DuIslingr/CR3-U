#pragma once
// The languages a release can show. CR3's messages hold two (message\*.dat: slot 0 Japanese,
// slot 1 English); the iPhone/Android releases have both. The Vita's is Japanese alone (its slot
// 1: placeholders), but the fan translation (gbatemp's "Chaos Rings III Translation Project", a
// rePatch for PCSG00500) puts the iPhone's English in slot 0 of its own copies of CRDB.psp2.mvgl
// and four more archives, with the UI's textures, names, scripts and the movies' subtitles
// translated. Given over an unpatched Vita release as an overlay (Database::addOverlay), it is a
// language of its own - "English (fan translation)" - and Japanese stays; a Vita release with the
// patch already installed over it has that English alone.

#include <string>
#include <vector>

#include "assets/mvgl.hpp"

namespace cr3 {

enum class Language { Japanese, English, FanEnglish };

struct LanguageChoice {
    Language id;
    const char* label; // "Japanese", "English", "English (fan translation)"
    int slot;          // the messages' slot
    bool overlay;      // the overlay on
};

// the release's languages (its own, then the overlay's)
std::vector<LanguageChoice> releaseLanguages(const Database& db);
// the release's own text is the fan translation (patched in place: slot 0 English)
bool fanPatchedInPlace(const Database& db);
// `want` if the release has it, else its first; the overlay on or off for it. The slot it reads
LanguageChoice applyLanguage(Database& db, Language want);

const char* languageKey(Language l); // "ja", "en", "fan-en" (settings, --language)
bool parseLanguage(const std::string& key, Language& out);

} // namespace cr3
