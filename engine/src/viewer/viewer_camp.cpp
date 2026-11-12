#include "viewer/viewer.hpp"

namespace cr3_viewer {

// the options' window, its question and the manual (in a battle or the camp) taking a key:
// false - none of them open
bool Viewer::optionsKey(SDL_Keycode k) {
    if (settingsKey(k)) return true;
    if (manualStage == 2) { // (the tutorial: Return a page)
        if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && manualEvents) manualEvents->advance();
        return true;
    }
    if (manualStage == 1) { // (the list: Up / Down, Return plays, Backspace back)
        const int n = std::max(int(manualEntries.size()), 1);
        if (k == SDLK_UP) manualCursor = (manualCursor + n - 1) % n;
        else if (k == SDLK_DOWN) manualCursor = (manualCursor + 1) % n;
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) playManual(manualCursor);
        else if (k == SDLK_BACKSPACE) closeManual();
        return true;
    }
    if (optionsAsk) { // (the question: Return yes, Backspace no)
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) optionTap(3201);
        else if (k == SDLK_BACKSPACE || k == SDLK_N) optionTap(3202);
        return true;
    }
    if (optionsOpen) { // (the options: Backspace / Return back, Left / Right the focused bar; G / Tab
                       // CR3-U's Display & Graphics)
        if (k == SDLK_G || k == SDLK_TAB) openSettings();
        else if (k == SDLK_BACKSPACE || k == SDLK_RETURN || k == SDLK_KP_ENTER) optionsOpen = false;
        else if (k == SDLK_UP) optionFocus = (optionFocus + 2) % 3;
        else if (k == SDLK_DOWN) optionFocus = (optionFocus + 1) % 3;
        else if (k == SDLK_LEFT) optionVolume[optionFocus] = std::max(optionVolume[optionFocus] - 5, 0), volumeSet(optionFocus);
        else if (k == SDLK_RIGHT) optionVolume[optionFocus] = std::min(optionVolume[optionFocus] + 5, 100), volumeSet(optionFocus);
        return true;
    }
    return false;
}

// a volume set (OptionMenu::TouchSimpleRelease: the effects' bar let go - Cr3OptionData::SetSE -
// plays SE 0xc6 at it)
void Viewer::volumeSet(int bar) {
    audio.setVolumes(float(optionVolume[0]) / 100.0f, float(optionVolume[1]) / 100.0f, float(optionVolume[2]) / 100.0f);
    if (bar == 1) audio.playSe(0xc6, false);
}

// a tap on the options' window, its question or the manual (`x`: across, UI units): false -
// none of them open
bool Viewer::optionsTapped(const TapRect& r, float x) {
    if (settingsTapped(r.id)) return true;
    if (manualStage == 1) {
        if (r.id == 100) closeManual();
        else if (r.id >= 5000) playManual(r.id - 5000);
        else if (r.id >= 1000) manualCursor = r.id - 1000; // (another row: the focus)
        return true;
    }
    if (optionsAsk) {
        if (r.id == 3201 || r.id == 3202) optionTap(r.id);
        return true;
    }
    if (optionsOpen) { // (Back; a bar's slide area: its volume by the point across it)
        if (r.id == 100) optionsOpen = false;
        else if (r.id == 3110) openSettings();
        else if ((r.id >= 3100 && r.id < 3300) || r.id == 4098 || r.id == 4099) optionTap(r.id);
        else if (r.id >= 2000 && r.id < 2003) {
            const float a = std::min(r.x0, r.x1), b = std::max(r.x0, r.x1);
            optionVolume[r.id - 2000] = std::clamp(int(std::lround((x - a) / std::max(b - a, 1e-4f) * 20.0f)) * 5, 0, 100);
            optionFocus = r.id - 2000;
            volumeSet(optionFocus);
        }
        return true;
    }
    return false;
}

void Viewer::stepLanguage(int by) {
    if (languages.size() < 2) return;
    const size_t n = languages.size();
    useLanguage((languageIndex + n + size_t(by % int(n) + int(n))) % n, true);
    settings.language = int(languages[languageIndex].id) + 1; // (kept for the next start)
    saveSettings();
}

// the save's slot (the options it stores): the language with it, the one in use if it has it
void Viewer::languageFromSlot() {
    if (languages.empty()) return;
    if (languages[languageIndex].slot == optionLanguage) return;
    for (size_t i = 0; i < languages.size(); ++i)
        if (languages[i].slot == optionLanguage) return useLanguage(i, dev != nullptr);
    optionLanguage = languages[languageIndex].slot; // (a slot it hasn't: as it was)
}

// A language in use: its fan translation over the release (or none), the messages' slot it
// reads; `live`, the game running - what was read in the one before read again (the events'
// message files, the name tables, the font, the pictures a translation changes)
void Viewer::useLanguage(size_t i, bool live) {
    if (languages.empty()) return;
    i = std::min(i, languages.size() - 1);
    const cr3::LanguageChoice l = languages[i];
    const int before = archive.activeOverlay();
    languageIndex = i;
    optionLanguage = l.slot;
    originalTextJapanese = l.id == cr3::Language::Japanese;
    japaneseText = l.slot == 0 && l.overlay < 0;
    if (before == l.overlay) {
        archive.setActiveOverlay(l.overlay);
        if (live) setEventLanguage();
        return;
    }
    std::vector<std::string> changed; // (the cached pictures the other translation has otherwise: in place)
    auto picture = [&](const std::string& name, std::vector<uint8_t>& out) { // (decode's file: images_eng\ first where it reads that)
        return (englishTextures && archive.read("images_eng\\" + name + ".img", out)) || archive.read("images\\" + name + ".img", out);
    };
    if (live)
        for (auto& [name, t] : textures) {
            if (!t.id) continue;
            std::vector<uint8_t> a, b;
            archive.setActiveOverlay(before);
            const bool ra = picture(name, a);
            archive.setActiveOverlay(l.overlay);
            const bool rb = picture(name, b);
            if (ra != rb || a != b) changed.push_back(name);
        }
    archive.setActiveOverlay(l.overlay);
    if (!live) return;
    for (const std::string& name : changed) reloadTexture(name, textures[name]);
    ++textGeneration;
    messageFiles.clear();
    for (auto& [k, q] : uiTexts) dev->destroy(q.tex);
    uiTexts.clear();
    for (TextQuad& q : textQuads) dev->destroy(q.tex);
    for (TextQuad& q : nameQuads) dev->destroy(q.tex);
    textQuads.clear(), nameQuads.clear(), shownText.clear(), nameText.clear();
    if (!archiveFont.empty()) { // (the Spanish translation's own font - its á, ñ, ¿ - while it's in use)
        std::vector<uint8_t> bytes;
        std::string err;
        const std::string from = archive.sourceOf(archiveFont);
        if (from != archiveFontFrom && archive.read(archiveFont, bytes) && font.loadMemory(std::move(bytes), from, &err, 1)) archiveFontFrom = from;
        else if (!err.empty()) SDL_Log("font: %s", err.c_str());
    }
    for (cr3::EventRuntime* ev : {event.get(), areaEvents.get(), cr3BattleEvents.get(), titleEvents.get(), manualEvents.get(),
                                  script ? &script->events() : nullptr, battleScript ? &battleScript->events() : nullptr})
        if (ev) ev->reloadTexts();
    setEventLanguage();
    SDL_Log("language: %s%s (%zu pictures changed)", l.label, l.overlay >= 0 ? (" over " + archive.overlayPath(l.overlay)).c_str() : "",
            changed.size());
}

void Viewer::setEventLanguage() {
    for (cr3::EventRuntime* ev : {event.get(), areaEvents.get(), cr3BattleEvents.get(), titleEvents.get(), manualEvents.get(),
                                  script ? &script->events() : nullptr, battleScript ? &battleScript->events() : nullptr})
        if (ev) ev->language = optionLanguage;
}

// OptionMenu::TouchSimpleRelease: the seat's tap areas 1 Title Screen, 2 Manual, 3 / 4 the
// quality back / on (circle 98 on), 5 / 6 the language back / on (circle 99 on); the question's
// 3201 yes, 3202 no
void Viewer::optionTap(int id) {
    switch (id) {
    case 3101:
        optionsOpen = false, optionsAsk = true, optionsAskStart = uiClock;
        break;
    case 3102: // (BattleOptionMenu 4 -> 9 -> 0x22: InstructionsWindowMenu, the tutorials' list)
        optionsOpen = false;
        openManual();
        break;
    case 3110: openSettings(); break; // (CR3-U's Display & Graphics)
    case 3103: optionQuality = (optionQuality + 2) % 3; break;
    case 3104: case 4098: optionQuality = (optionQuality + 1) % 3; break;
    case 3105: stepLanguage(-1); break;
    case 3106: case 4099: stepLanguage(1); break;
    case 3201: // (8: the music stopped, InterfaceMain to the title, 0x1d)
        optionsAsk = false, toTitle = true;
        SDL_Log("options: to the title screen");
        break;
    case 3202: // (7: the options again)
        optionsAsk = false, optionsOpen = true, optionsStart = uiClock;
        break;
    default: break;
    }
    if (id >= 3103 && id <= 3106) SDL_Log("options: language %d, quality %d", optionLanguage, optionQuality);
}

void Viewer::openManual() {
    manualEntries.clear();
    std::vector<uint8_t> d;
    if (archive.read("message\\instructions.dat", d)) {
        auto u32 = [&](size_t o) { return o + 4 <= d.size() ? uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24) : 0u; };
        const size_t records = u32(0), strings = u32(4);
        for (uint32_t k = 0, n = u32(8); k < n; ++k) {
            const size_t r = records + size_t(k) * 0x28;
            const int flag = int(u32(r + 0x20));
            if (flag != 0 && !game.flag(flag)) continue; // (Cr3UtilFlagCheck)
            ManualEntry e;
            for (size_t c = strings + u32(r + 4u * size_t(std::clamp(optionLanguage, 0, 7))); c < d.size() && d[c]; ++c) e.title += char(d[c]);
            e.id = int(u32(r + 0x24));
            manualEntries.push_back(e);
        }
    }
    manualStage = 1, manualStart = uiClock, manualCursor = 0;
    SDL_Log("manual: %zu entries", manualEntries.size());
}

void Viewer::closeManual() {
    manualStage = 0, manualEvents.reset();
    optionsOpen = true, optionsStart = uiClock;
}

void Viewer::playManual(int k) {
    if (k < 0 || k >= int(manualEntries.size())) return;
    manualCursor = k;
    if (playTutorial("manual_disp", manualEntries[size_t(k)].id, true))
        SDL_Log("manual: tutorial %d (%s)", manualEntries[size_t(k)].id, manualEntries[size_t(k)].title.c_str());
}

// title.nut's `function` played over the battle (the manual's manual_disp, the options' own)
bool Viewer::playTutorial(const char* function, int id, bool toList) {
    manualEvents = std::make_unique<cr3::EventRuntime>(archive, game);
    manualEvents->language = optionLanguage, manualEvents->autoPage = pageSeconds;
    manualEvents->tutorialId = id;
    if (!manualEvents->start("title", function)) {
        SDL_Log("tutorial: %s", manualEvents->error().c_str());
        manualEvents.reset();
        if (!toList) optionsOpen = true;
        return false;
    }
    manualStage = 2, manualClock = 0, manualToList = toList;
    return true;
}

void Viewer::updateManual(float dt) {
    if (manualStage != 2 || !manualEvents) return;
    manualClock = std::min(manualClock + dt, 0.25f);
    for (; manualClock >= cr3::EventRuntime::kTick; manualClock -= cr3::EventRuntime::kTick)
        if (!manualEvents->tick()) SDL_Log("manual: %s", manualEvents->error().c_str());
    if (!manualEvents->running()) { // (InstructionsWindowMenu 3 -> 0: the list again)
        manualEvents.reset();
        if (manualToList) manualStage = 1, manualStart = uiClock;
        else manualStage = 0, optionsOpen = true, optionsStart = uiClock;
        SDL_Log("tutorial over");
    }
}

void Viewer::drawManualList(const Mat4& view, int w, int h) {
    tapRects.clear();
    std::vector<ListRow> rows;
    for (size_t k = 0; k < manualEntries.size(); ++k) {
        ListRow r;
        r.id = 5000 + int(k), r.name = manualEntries[k].title, r.number = -1;
        rows.push_back(r);
    }
    listRows = rows;
    drawSkillList(rows, 5000 + manualCursor, uiClock - manualStart, view, w, h, -1, -1);
}

void Viewer::drawOptionsAsk(const Mat4& view, int w, int h) {
    tapRects.clear();
    const float t = uiClock - optionsAskStart;
    UiFig& win = uiFig("syswin_io");
    win.fig.place = cr3::Mat34();
    poseUi(win, "syswin_io", t);
    drawFigure(win.fig, view.data());
    float x, y;
    if (uiJoint(win, "typeM_L01", x, y)) drawUiText(announce(0x6d), 26, x, y, w, h, true);
    UiFig& ch = uiFig("choice_io");
    ch.fig.place = cr3::Mat34();
    poseUi(ch, "choice_io", t);
    drawFigure(ch.fig, view.data());
    for (int n : {11, 12}) { // (call_cBtn_fr11 Yes, 12 No: circleBtn_fr at those frames)
        char jn[24];
        std::snprintf(jn, sizeof jn, "call_cBtn_fr%d", n);
        if (!uiJoint(ch, jn, x, y)) continue;
        UiFig& c = uiFig("circlebtn_fr");
        c.fig.place = cr3::Mat34();
        c.fig.place.m[3] = x, c.fig.place.m[7] = y;
        poseUi(c, "circlebtn_fr", float(n) / 20.0f);
        drawFigure(c.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) tapRects.push_back({n == 11 ? 3201 : 3202, sx, sy, ex, ey});
    }
}

void Viewer::drawOptions(const Mat4& view, const Mat4& proj, int w, int h) {
    tapRects.clear();
    const float t = uiClock - optionsStart;
    UiFig& title = uiFig("menu");
    title.fig.place = cr3::Mat34();
    poseUi(title, "menuopt_io", t);
    drawFigure(title.fig, view.data());
    UiFig& seat = uiFig("seatopt_io");
    seat.fig.place = cr3::Mat34();
    poseUi(seat, "seatopt_io", t);
    drawFigure(seat.fig, view.data());
    static const int kLabels[7] = {103, 104, 105, 102, 106, 108, 101};
    float x, y;
    for (int k = 0; k < 7; ++k) {
        char jn[16];
        std::snprintf(jn, sizeof jn, "typeM_L%02d", k + 1);
        if (uiJoint(seat, jn, x, y)) drawUiText(messageText("announcemenuname", kLabels[k]), 26, x, y, w, h, true);
    }
    for (const std::string& j : seat.joints) {
        if (j.rfind("call_vlmBar_per", 0) == 0) {
            const int k = std::atoi(j.c_str() + 15) - 13;
            if (k < 0 || k > 2 || !uiJoint(seat, j, x, y)) continue;
            UiFig& bar = uiFig("vlmbar_per");
            bar.fig.place = cr3::Mat34();
            bar.fig.place.m[3] = x, bar.fig.place.m[7] = y;
            poseUi(bar, "vlmbar_per", float(optionVolume[k]) / 20.0f, false);
            drawFigure(bar.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(bar, "slideAreaS01", sx, sy) && uiJoint(bar, "slideAreaE01", ex, ey)) tapRects.push_back({2000 + k, sx, sy, ex, ey});
        } else if (j.rfind("call_circleBtn_fr", 0) == 0 && uiJoint(seat, j, x, y)) {
            UiFig& c = uiFig("circlebtn_fr");
            c.fig.place = cr3::Mat34();
            c.fig.place.m[3] = x, c.fig.place.m[7] = y;
            // (its frame its number - 36 On, 37 Off -; the language's, 99, by the language -
            // UpdateLanguageButton: 0x534750's 41 Japanese, 42 English, ...)
            const int n = std::atoi(j.c_str() + 17);
            static const int kLanguageFrame[8] = {41, 42, 44, 45, 46, 47, 48, 40}; // (0x534728)
            const int picture = languages.empty() ? optionLanguage : cr3::pictureSlot(languages[languageIndex].id); // (a fan translation's own language's)
            poseUi(c, "circlebtn_fr", float(n == 99 ? kLanguageFrame[std::clamp(picture, 0, 7)] : n == 98 ? 0 : n) / 20.0f); // (98, the quality's: frame 0, its text over it)
            drawFigure(c.fig, view.data());
            float sx, sy, ex, ey;
            if ((n == 98 || n == 99) && uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) tapRects.push_back({4000 + n, sx, sy, ex, ey});
        } else if (j.rfind("call_mainIcon_id", 0) == 0 && uiJoint(seat, j, x, y)) {
            UiFig& ic = uiFig("mainicon_id");
            ic.fig.place = cr3::Mat34();
            ic.fig.place.m[3] = x, ic.fig.place.m[7] = y;
            poseUi(ic, "mainicon_id", float(std::atoi(j.c_str() + 16)) / 20.0f);
            drawFigure(ic.fig, view.data());
        }
    }
    if (uiJoint(seat, "typeM_C01", x, y)) drawUiText(messageText("announcemenuname", 143 + optionQuality), 26, x, y, w, h); // (the quality: over its button)

    for (int k = 1; k <= 6; ++k) { // (the seat's own tap areas)
        char sa[16], ea[16];
        std::snprintf(sa, sizeof sa, "tapAreaS%02d", k), std::snprintf(ea, sizeof ea, "tapAreaE%02d", k);
        float sx, sy, ex, ey;
        if (uiJoint(seat, sa, sx, sy) && uiJoint(seat, ea, ex, ey)) tapRects.push_back({3100 + k, sx, sy, ex, ey});
    }
    // CR3-U's own: Display & Graphics - one of the game's oval buttons (circleBtn_fr's frame 0, as the
    // quality's) in the top band, before the quality's back arrow, at its height (G / Tab, or a tap)
    for (const TapRect& q : std::vector<TapRect>(tapRects))
        if (q.id == 3103) {
            UiFig& c = uiFig("circlebtn_fr");
            c.fig.place = cr3::Mat34();
            poseUi(c, "circlebtn_fr", 0);
            float sx, sy, ex, ey;
            if (!uiJoint(c, "tapAreaS01", sx, sy) || !uiJoint(c, "tapAreaE01", ex, ey)) break;
            const float bw = std::abs(ex - sx), cx = std::min(q.x0, q.x1) - 0.015f - bw * 0.5f, cy = (q.y0 + q.y1) * 0.5f;
            const float ox = cx - (sx + ex) * 0.5f, oy = cy - (sy + ey) * 0.5f;
            c.fig.place.m[3] = ox, c.fig.place.m[7] = oy;
            poseUi(c, "circlebtn_fr", 0);
            drawFigure(c.fig, view.data());
            drawUiText("Graphics", 26, cx, cy, w, h, false, 1.0f, bw * 0.9f);
            tapRects.push_back({3110, sx + ox, sy + oy, ex + ox, ey + oy});
            break;
        }
    UiFig& bk = uiFig("btlback");
    bk.fig.place = cr3::Mat34();
    poseUi(bk, "btlback_in", t);
    drawFigure(bk.fig, view.data());
    float sx, sy, ex, ey;
    if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
    (void)proj;
}

void Viewer::openCamp() {
    campOpen = true, campStart = uiClock, campList = 0, campCursor = 0;
    fieldTabOpen = false, fieldTabAt = -10;
    optionsOpen = optionsAsk = false, manualStage = 0;
    SDL_Log("camp: open");
}

void Viewer::closeCamp() {
    campOpen = false, campList = 0, campUse = 0, campItem = 0, campLink = 0;
    optionsOpen = optionsAsk = false, manualStage = 0, manualEvents.reset();
    SDL_Log("camp: closed");
}

auto Viewer::campRows() -> std::vector<ListRow> {
    std::vector<ListRow> rows;
    if (campList == 16) {
        static const int kCampItems[] = {4, 5, 6, 7, 36, 37, 38, 39, 40, 41}; // (Cr3UtilUseItemByCamp: 4, and the library's list)
        auto add = [&](int id) {
            const cr3::BattleData::Item* it = battleData.item(id);
            const int n = game.itemCount[id];
            if (!it || n <= 0) return;
            const bool inCategory = campCategory == 0 ? it->type <= 1 : it->type == 4; // (CategoryCheck)
            if (!inCategory) return;
            const bool usable = std::find(std::begin(kCampItems), std::end(kCampItems), id) != std::end(kCampItems) && !(id == 36 && game.flag(0x3e9));
            rows.push_back({id, it->name[1], n, it->icon, usable, it->text[1]});
        };
        add(36);
        for (int id = 1; id < 300; ++id)
            if (id != 36) add(id);
    } else if (campList == 18) {
        for (int id = 1; id < 32; ++id)
            if (game.sopiaOwned >> id & 1u)
                if (const cr3::BattleData::Sopia* so = battleData.sopia(id)) rows.push_back({id, so->name[1], -1, 0, true, ""});
    }
    return rows;
}

auto Viewer::statusOf(int ch) const -> StatusShown {
    StatusShown r;
    r.id = ch + 1, r.level = std::max(game.level[ch], 1);
    int eq[8];
    for (int e = 0; e < 8; ++e) eq[e] = game.equip[ch][e];
    const cr3::BattleRules::PlayerStats st = cr3::BattleRules::playerStats(battleData, r.id, r.level, eq, game.sopiaLevel, game.seed[ch], game.itemCount);
    r.hpMax = st.maxHp, r.mpMax = st.maxMp;
    r.hp = r.hpMax; // (Cr3RestorePlayerHP after each battle)
    r.mp = game.mp[ch] < 0 ? r.mpMax : std::min(game.mp[ch], r.mpMax);
    const int lo = cr3::levelExp(r.level), hi = cr3::levelExp(r.level + 1);
    r.next = r.level >= 200 ? 0 : hi - game.exp[ch];
    r.percent = r.level >= 200 || hi <= lo ? 0 : int(std::clamp((int64_t(game.exp[ch]) - lo) * 100 / (hi - lo), int64_t(0), int64_t(100)));
    return r;
}

void Viewer::drawCamp(const Mat4& view, const Mat4& proj, int w, int h) {
    tapRects.clear();
    if (manualStage == 1) return drawManualList(view, w, h);
    if (manualStage == 2) return; // (the tutorial's picture and windows alone)
    if (optionsAsk) return drawOptionsAsk(view, w, h);
    if (settingsOpen) return drawSettings(view, w, h);
    if (optionsOpen) return drawOptions(view, proj, w, h);
    if (campUse) return drawItemUse(view, proj, w, h);
    if (campLink == 201) return drawScenarios(view, proj, w, h);
    if (campLink == 202) return drawPubRequest(view, w, h);
    const float t = uiClock - campStart;
    if (campList == 17) return drawEquipment(view, proj, w, h);
    if (campList) { // (the list, its tab - the items' 7, the sopia's 9 - and its own back)
        const std::vector<ListRow> rows = campRows();
        listRows = rows;
        const int focused = rows.empty() ? -1 : rows[size_t(std::clamp(campCursor, 0, int(rows.size()) - 1))].id;
        if (campList == 18) drawSopiaWindows(focused, t, view, proj, w, h); // (InterfaceMain 0xf / 0x10: its windows beside it)
        drawSkillList(rows, focused, t, view, w, h, campList == 16 ? 7 : 9, -1);
        return;
    }
    UiFig& title = uiFig("menu");
    title.fig.place = cr3::Mat34();
    poseUi(title, "menucamp_io", t);
    drawFigure(title.fig, view.data());
    StatusShown rows[2];
    int count = 0;
    for (int k = 0; k < 2; ++k)
        if (game.party[k] >= 0 && game.party[k] < 9) rows[count++] = statusOf(game.party[k]);
    drawStatusWindows(rows, count, t, view, proj);
    bool anyItem = false;
    for (int id = 1; id < 300; ++id) anyItem = anyItem || game.itemCount[id] > 0;
    buttonLook = [&](int id) { return id == 16 && !anyItem ? 0 : id == campCursorButton() && keysUsed ? 2 : 1; };
    drawCommandMenu("campbtn_io", t, view, {}, {}, w, h, 0, "campbtn_io");
    buttonLook = nullptr;
    for (TapRect& r : tapRects)
        if (r.id == 16 && !anyItem) r.id = -1;
    // the links (CampPubLink: campLnk1_io, the pub hidden, before flag 0x4b0; campLnk2_io after)
    UiFig& lnk = uiFig("camplnk");
    lnk.fig.place = cr3::Mat34();
    const bool pub = game.flag(0x4b0);
    poseUi(lnk, pub ? "camplnk2_io" : "camplnk1_io", t);
    for (ViewMesh& vm : lnk.fig.meshes) {
        const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
        vm.hidden = !pub && j < lnk.joints.size() && lnk.joints[j] == "GEO_pub";
        const uint32_t mi = vm.source.material; // (MTR_cmpLnkR at half without a request)
        const bool dim = pub && !game.globalWork[7] && mi < lnk.materials.size() && lnk.materials[mi] == "MTR_cmpLnkR";
        vm.material.params[0x33] = dim ? std::vector<float>{0.5f, 0.5f, 0.5f, 1.0f} : vm.baseMaterial.params[0x33];
        if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
    }
    drawFigure(lnk.fig, view.data());
    for (int k = 1; k <= (pub ? 2 : 1); ++k) {
        char sa[16], ea[16];
        std::snprintf(sa, sizeof sa, "tapAreaS%02d", k), std::snprintf(ea, sizeof ea, "tapAreaE%02d", k);
        float sx, sy, ex, ey;
        if (uiJoint(lnk, sa, sx, sy) && uiJoint(lnk, ea, ex, ey)) tapRects.push_back({200 + k, sx, sy, ex, ey});
    }
    // the Oz and the clock
    float x, y;
    UiFig& oz = uiFig("ozwin_io");
    oz.fig.place = cr3::Mat34();
    poseUi(oz, "ozwin_io", t);
    drawFigure(oz.fig, view.data());
    if (uiJoint(oz, "call_status_fr13", x, y)) drawStatusSign(13, x, y, proj);
    if (uiJoint(oz, "call_dotNum_id07", x, y)) drawDigits(std::clamp(game.oz, 0, 9999999), 110, 7, x, y, proj);
    UiFig& tw = uiFig("timewin_io");
    tw.fig.place = cr3::Mat34();
    poseUi(tw, "timewin_io", t);
    drawFigure(tw.fig, view.data());
    if (uiJoint(tw, "call_status_fr14", x, y)) drawStatusSign(14, x, y, proj);
    { // (PlayTimeWindowMenu: the clock's - GetCurrentHourTime - hours at id04, glyph 163's ":" at id03, minutes at id01; set 90)
        const std::time_t now = std::time(nullptr);
        std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &now);
#else
        localtime_r(&now, &lt);
#endif
        if (uiJoint(tw, "call_dotNum_id04", x, y)) drawDigits(lt.tm_hour, 90, 2, x, y, proj);
        if (uiJoint(tw, "call_dotNum_id03", x, y)) drawDigits(0, 163, 1, x, y, proj);
        if (uiJoint(tw, "call_dotNum_id01", x, y)) drawDigits(lt.tm_min, 90, 2, x, y, proj);
    }
    UiFig& bk = uiFig("btlback");
    bk.fig.place = cr3::Mat34();
    poseUi(bk, "btlback_in", t);
    drawFigure(bk.fig, view.data());
    float sx, sy, ex, ey;
    if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
}

int Viewer::campCursorButton() const {
    static const int kButtons[4] = {16, 17, 18, 20};
    return kButtons[std::clamp(campCursor, 0, 3)];
}

// The characters the camp offers (Cr3UtilValidCharacter: 0..6 by flags 0x3fd..0x403 - those
// who have joined), else the party's two
std::vector<int> Viewer::validChars() const {
    std::vector<int> out;
    for (int c = 0; c < 7; ++c)
        if (game.flag(0x3fd + c)) out.push_back(c);
    if (out.empty())
        for (int c : game.party)
            if (c >= 0 && c < 9) out.push_back(c);
    return out;
}

cr3::BattleRules::PlayerStats Viewer::statsOf(int ch) const {
    int eq[8];
    for (int e = 0; e < 8; ++e) eq[e] = game.equip[ch][e];
    return cr3::BattleRules::playerStats(battleData, ch + 1, std::max(game.level[ch], 1), eq, game.sopiaLevel, game.seed[ch], game.itemCount);
}

int Viewer::mpOf(int ch) const {
    const int most = statsOf(ch).maxMp;
    return game.mp[ch] < 0 ? most : std::min(game.mp[ch], most);
}

// CharaStatusWindowBMenu: one character's window (campWin with campWin3_io, the third's joints
// id90..): its face, name, level (3 of set 130), HP (5 of 110) and MP (4), their bars (btlHpBar_per
// / btlMpBar_per at the percent / 20 s), its labels, and an arrow by HP / MP (sign 5 up, 6 down)
// when what is chosen changes them
void Viewer::drawStatusB(int ch, int hpChange, int mpChange, float t, const Mat4& view, const Mat4& proj) {
    UiFig& win = uiFig("campwin");
    win.fig.place = cr3::Mat34();
    poseUi(win, "campwin3_io", t);
    for (ViewMesh& vm : win.fig.meshes) {
        const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
        const std::string jn = j < win.joints.size() ? win.joints[j] : "";
        vm.hidden = jn == "GEO_campWin1" || jn == "GEO_campWin2";
    }
    drawFigure(win.fig, view.data());
    auto at = [&](const std::string& joint, const std::string& fig, float pose) -> UiFig* {
        float x, y;
        if (!uiJoint(win, joint, x, y)) return nullptr;
        UiFig& c = uiFig(fig);
        c.fig.place = cr3::Mat34();
        c.fig.place.m[3] = x, c.fig.place.m[7] = y;
        poseUi(c, fig, pose);
        return &c;
    };
    const int id = ch + 1;
    const cr3::BattleRules::PlayerStats st = statsOf(ch);
    const int mp = mpOf(ch);
    if (UiFig* face = at("call_cmpFace_id9000", "campface", 0)) {
        for (ViewMesh& vm : face->fig.meshes) vm.texture = texture("cmpFaceC00" + std::to_string(id)), vm.extraMap = texture("cmpFaceA00" + std::to_string(id));
        drawFigure(face->fig, view.data());
    }
    if (UiFig* n = at("call_pcNameEN_id9021", "pcnameen_id", float(id) / 20.0f)) drawFigure(n->fig, view.data());
    float x, y;
    for (int s : {1, 2, 3, 7})
        if (uiJoint(win, "call_status_fr900" + std::to_string(s), x, y)) drawStatusSign(s, x, y, proj);
    if (hpChange && uiJoint(win, "call_status_fr0005", x, y)) drawStatusSign(hpChange > 0 ? 5 : 6, x, y, proj);
    if (mpChange && uiJoint(win, "call_status_fr0006", x, y)) drawStatusSign(mpChange > 0 ? 5 : 6, x, y, proj);
    if (uiJoint(win, "call_dotNum_id9031", x, y)) drawDigits(std::max(game.level[ch], 1), 130, 3, x, y, proj);
    if (uiJoint(win, "call_dotNum_id9041", x, y)) drawDigits(st.maxHp, 110, 5, x, y, proj); // (the HP: whole between battles)
    if (uiJoint(win, "call_dotNum_id9061", x, y)) drawDigits(mp, 110, 4, x, y, proj);
    if (UiFig* b = at("call_cmpHpBar_per9011", "btlhpbar_per", 5.0f)) drawFigure(b->fig, view.data());
    if (UiFig* b = at("call_cmpMpBar_per9012", "btlmpbar_per", st.maxMp > 0 ? float(mp) * 5.0f / float(st.maxMp) : 0.0f)) drawFigure(b->fig, view.data());
}

// CharaStatusCapacityMenu (statusWin_io): the five parameters (4 of set 90 at call_dotNum_id001
// / 101 / 201 / 301 / 401), what is chosen would change of each (4 of set 140 at id011 .., sign
// 5 or 6 beside), the labels (call_status_fr16..20), the sopia's combination's name (typeS_L01)
void Viewer::drawCapacity(int ch, const int* change, float t, const Mat4& view, const Mat4& proj, int w, int h) {
    UiFig& sw = uiFig("statuswin_io");
    sw.fig.place = cr3::Mat34();
    poseUi(sw, "statuswin_io", t);
    drawFigure(sw.fig, view.data());
    const cr3::BattleRules::PlayerStats st = statsOf(ch);
    float x, y;
    for (int k = 0; k < 5; ++k) {
        const std::string r = std::to_string(k);
        if (uiJoint(sw, "call_dotNum_id" + r + "01", x, y)) drawDigits(st.param[k], 90, 4, x, y, proj);
        if (change && change[k]) {
            if (uiJoint(sw, "call_dotNum_id" + r + "11", x, y)) drawDigits(std::abs(change[k]), 140, 4, x, y, proj);
            if (uiJoint(sw, "call_status_fr" + r + (change[k] > 0 ? "05" : "06"), x, y)) drawStatusSign(change[k] > 0 ? 5 : 6, x, y, proj);
        }
    }
    for (int s = 16; s <= 20; ++s)
        if (uiJoint(sw, "call_status_fr" + std::to_string(s), x, y)) drawStatusSign(s, x, y, proj);
    int eq[8];
    for (int e = 0; e < 8; ++e) eq[e] = game.equip[ch][e];
    const int combo = cr3::BattleRules::sopiaCombo(battleData, eq, game.itemCount);
    if (combo > 0 && uiJoint(sw, "typeS_L01", x, y)) drawUiText(messageText("autoskillname", combo), 22, x, y, w, h, true);
}

void Viewer::useItemOn(int ch, int item, int n) { // (Cr3UtilParameterUpByItem, Cr3UpdatePlayerData; PlaySoundMPRecovery)
    const cr3::BattleData::Item* it = battleData.item(item);
    if (!it || n <= 0) return;
    if (it->usedUp) game.itemCount[item] = static_cast<short>(std::max(game.itemCount[item] - n, 0));
    const int mp0 = mpOf(ch);
    for (int k = 0; k < 5; ++k) game.seed[ch][k] = static_cast<short>(std::clamp(game.seed[ch][k] + it->param[k] * n, -9999, 9999));
    const int most = statsOf(ch).maxMp;
    const int mp = std::clamp(std::min(mp0 + it->mp * n, 9999), 0, most);
    game.mp[ch] = mp >= most ? -1 : mp;
    bool any = it->mp != 0;
    for (int k = 0; k < 5; ++k) any = any || it->param[k] != 0;
    if (any) audio.playSe(0xc6, false);
    SDL_Log("camp: %s x%d on %d: MP %d -> %d", it->name[1].c_str(), n, ch + 1, mp0, mp);
}

void Viewer::campUseTap(int id) {
    const std::vector<int> chars = validChars();
    const cr3::BattleData::Item* it = battleData.item(campItem);
    if (campUse == 1) {
        if (id == 100) { campUse = 0, audio.playSe(0xb5, false); return; }
        const int k = id - 7000;
        if (k < 0 || k >= int(chars.size())) return;
        if (k != campFace) { campFace = k, audio.playSe(0xb3, false); return; }
        audio.playSe(0xb4, false);
        if (it && it->usedUp) campUse = 2, campCount = 1, campUseStart = uiClock;
        else useItemOn(chars[size_t(k)], campItem, 1), campUse = 0;
    } else if (campUse == 2) {
        const int most = std::max(int(game.itemCount[campItem]), 1);
        if (id == 7101) campCount = std::max(campCount - 1, 1);
        else if (id == 7102) campCount = std::min(campCount + 1, most);
        else if (id == 7201) { // (yes: used, its effect)
            if (campFace < int(chars.size())) useItemOn(chars[size_t(campFace)], campItem, campCount);
            campUse = 0, campItem = 0;
        } else if (id == 7202) campUse = 1, audio.playSe(0xb5, false);
    } else if (campUse == 3) {
        if (id == 7201) { // (FldMain: the camp's result 2 - its use)
            game.itemCount[36] = static_cast<short>(std::max(game.itemCount[36] - 1, 0));
            audio.playSe(0xb4, false);
            closeCamp();
            audio.stopBgm(20), audio.playSe(0x14f, false);
            game.setFlag(0x3ed, true);
            SDL_Log("camp: Emergency Exit");
            enterMap("t0101", "t0101_p1");
        } else if (id == 7202) campUse = 0, campItem = 0, audio.playSe(0xb5, false);
    }
}

void Viewer::drawItemUse(const Mat4& view, const Mat4& proj, int w, int h) {
    const float t = uiClock - campUseStart;
    const cr3::BattleData::Item* it = battleData.item(campItem);
    const std::string name = it ? it->name[1] : "";
    if (campUse == 3) return drawAsk(formatAnnounce(announce(0x73), {name}), t, view, w, h, 7201, 7202);
    const std::vector<int> chars = validChars();
    campFace = std::clamp(campFace, 0, std::max(int(chars.size()) - 1, 0));
    const int ch = chars.empty() ? 0 : chars[size_t(campFace)];
    drawStatusB(ch, it ? it->hp : 0, it ? it->mp : 0, t, view, proj);
    drawCapacity(ch, it ? it->param : nullptr, t, view, proj, w, h);
    if (campUse == 2) { // (how many, and the question)
        UiFig& num = uiFig("shopnum_io");
        num.fig.place = cr3::Mat34();
        poseUi(num, "shopnum_io", t);
        drawFigure(num.fig, view.data());
        float x, y, sx, sy, ex, ey;
        if (uiJoint(num, "call_dotNum_id01", x, y)) drawDigits(campCount, 90, 2, x, y, proj);
        for (int k = 1; k <= 2; ++k) {
            char sa[16], ea[16];
            std::snprintf(sa, sizeof sa, "tapAreaS%02d", k), std::snprintf(ea, sizeof ea, "tapAreaE%02d", k);
            if (uiJoint(num, sa, sx, sy) && uiJoint(num, ea, ex, ey)) tapRects.push_back({7100 + k, sx, sy, ex, ey});
        }
        drawAsk(formatAnnounce(announce(0x74), {name, std::to_string(game.itemCount[campItem])}), t, view, w, h, 7201, 7202); // (Cr3UtilDPrint: the count had)
        return;
    }
    UiFig& fw = uiFig("facewin_io");
    fw.fig.place = cr3::Mat34();
    poseUi(fw, "facewin_io", t);
    drawFigure(fw.fig, view.data());
    for (size_t k = 0; k < chars.size(); ++k) {
        float x, y;
        if (!uiJoint(fw, "call_faceBtn" + std::to_string(k + 1), x, y)) continue;
        UiFig& b = uiFig("facebtn");
        b.fig.place = cr3::Mat34();
        b.fig.place.m[3] = x, b.fig.place.m[7] = y;
        poseUi(b, "facebtn", 0);
        if (cr3::Texture* tex = texture("faceBtn_00" + std::to_string(chars[k] + 1)))
            for (ViewMesh& vm : b.fig.meshes) vm.texture = tex;
        drawFigure(b.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(b, "tapAreaS01", sx, sy) && uiJoint(b, "tapAreaE01", ex, ey)) tapRects.push_back({7000 + int(k), sx, sy, ex, ey});
        if (int(k) == campFace && uiJoint(b, "call_faceSlct", sx, sy)) {
            UiFig& sl = uiFig("faceslct");
            sl.fig.place = cr3::Mat34();
            sl.fig.place.m[3] = sx, sl.fig.place.m[7] = sy;
            poseUi(sl, "faceslct", t, true);
            drawFigure(sl.fig, view.data());
        }
    }
    UiFig& help = uiFig("camphelp_io");
    help.fig.place = cr3::Mat34();
    poseUi(help, "camphelp_io", t);
    drawFigure(help.fig, view.data());
    float x, y;
    if (uiJoint(help, "typeM_L01", x, y)) drawUiText(formatAnnounce(announce(0x84), {name}), 24, x, y, w, h, true);
    UiFig& bk = uiFig("btlback");
    bk.fig.place = cr3::Mat34();
    poseUi(bk, "btlback_in", t);
    drawFigure(bk.fig, view.data());
    float sx, sy, ex, ey;
    if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
}

// an item picked on the camp's list (CampItemListMenu::Update 5: 0xb4; 10 for item 36, else 9)
void Viewer::campPickItem(int item) {
    const std::vector<ListRow> rows = campRows();
    const auto r = std::find_if(rows.begin(), rows.end(), [&](const ListRow& x) { return x.id == item; });
    if (r == rows.end() || !r->selectable) return (void)SDL_Log("camp: item %d not usable here", item);
    audio.playSe(0xb4, false);
    campItem = item, campUseStart = uiClock, campFace = 0;
    SDL_Log("camp: using %s", r->name.c_str());
    campUse = item == 36 ? 3 : 1;
}

// The sopia's windows (CampSopiaStatusMenu, sopiaWin_io: its picture - sopiaPic with
// sopiaPic_NNN, its number, 26..29 as 301 / 303 / 305 / 307, past 7 + 193 - at call_sopiaPic,
// its name at typeM_C2, its level (cost_id at the level + 6 / 20 s), its kind's sign (kinds 1 2
// 3 4 -> 2 1 4 3), the next level's bar and EXP (cmpNxBar_per; 5 at the most); CampSopiaSkillMenu,
// skillWin_io: its five levels' command or auto skill - "???" (menu name 0x3d) past its level)
void Viewer::drawSopiaWindows(int sopia, float t, const Mat4& view, const Mat4& proj, int w, int h) {
    const cr3::BattleData::Sopia* so = sopia > 0 ? battleData.sopia(sopia) : nullptr;
    UiFig& sw = uiFig("sopiawin_io");
    sw.fig.place = cr3::Mat34();
    poseUi(sw, "sopiawin_io", t);
    drawFigure(sw.fig, view.data());
    UiFig& kw = uiFig("skillwin_io");
    kw.fig.place = cr3::Mat34();
    poseUi(kw, "skillwin_io", t);
    drawFigure(kw.fig, view.data());
    float x, y;
    if (uiJoint(kw, "call_status_fr11", x, y)) drawStatusSign(11, x, y, proj);
    if (uiJoint(sw, "call_status_fr12", x, y)) drawStatusSign(12, x, y, proj);
    if (uiJoint(sw, "call_status_fr03", x, y)) drawStatusSign(3, x, y, proj);
    if (!so) return;
    const int level = sopia < 32 ? std::max(int(game.sopiaLevel[sopia]), 0) : 1;
    if (uiJoint(sw, "call_sopiaPic", x, y)) {
        const int n = sopia == 26 ? 301 : sopia == 27 ? 303 : sopia == 28 ? 305 : sopia == 29 ? 307 : sopia > 7 ? sopia + 193 : sopia;
        char tn[24];
        std::snprintf(tn, sizeof tn, "sopiaPic_%03d", n);
        UiFig& pic = uiFig("sopiapic");
        pic.fig.place = cr3::Mat34();
        pic.fig.place.m[3] = x, pic.fig.place.m[7] = y;
        poseUi(pic, "sopiapic", 0);
        if (cr3::Texture* tex = texture(tn))
            for (ViewMesh& vm : pic.fig.meshes) vm.texture = tex;
        drawFigure(pic.fig, view.data());
    }
    if (uiJoint(sw, "typeM_C2", x, y)) drawUiText(so->name[1], 24, x, y, w, h);
    if (uiJoint(sw, "call_cost_id", x, y)) {
        UiFig& co = uiFig("cost_id");
        co.fig.place = cr3::Mat34();
        co.fig.place.m[3] = x, co.fig.place.m[7] = y;
        poseUi(co, "cost_id", float(level + 6) / 20.0f);
        drawFigure(co.fig, view.data());
    }
    static const int kKindSign[5] = {0, 2, 1, 4, 3};
    if (so->kind >= 1 && so->kind <= 4 && uiJoint(sw, "call_enchant_id", x, y)) drawEnchant(kKindSign[so->kind], x, y, proj);
    const bool monster = so->growth != 0;
    const int lo = level > 0 ? cr3::sopiaExp(monster, level) : 0, hi = level < 5 ? cr3::sopiaExp(monster, level + 1) : lo;
    const int exp = sopia < 32 ? game.sopiaExp[sopia] : 0;
    if (uiJoint(sw, "call_cmpNxBar_per", x, y)) {
        UiFig& bar = uiFig("cmpnxbar_per");
        bar.fig.place = cr3::Mat34();
        bar.fig.place.m[3] = x, bar.fig.place.m[7] = y;
        poseUi(bar, "cmpnxbar_per", level >= 5 || hi <= lo ? 5.0f : float(std::clamp((int64_t(exp) - lo) * 100 / (hi - lo), int64_t(0), int64_t(100))) / 20.0f);
        drawFigure(bar.fig, view.data());
    }
    if (uiJoint(sw, "call_dotNum_id1", x, y)) drawDigits(level >= 5 ? 0 : std::max(hi - exp, 0), 90, 6, x, y, proj);
    for (int k = 0; k < 5; ++k) {
        char jn[16];
        std::snprintf(jn, sizeof jn, "typeS_C%02d", k + 1);
        if (!uiJoint(kw, jn, x, y)) continue;
        std::string n;
        if (k >= level) n = messageText("announcemenuname", 0x3d);
        else if (so->commands[k] > 0) n = messageText("commandname", so->commands[k]);
        else if (so->skills[k] > 0) n = messageText("autoskillname", so->skills[k]);
        drawUiText(n, 22, x, y, w, h);
    }
}

std::string Viewer::menuName(int id) { return messageText("announcemenuname", id); }

auto Viewer::equipRows() -> std::vector<EquipRow> {
    std::vector<EquipRow> rows;
    const int ch = campEquipChar;
    const short* eq = game.equip[ch];
    auto itemName = [&](int id) { const cr3::BattleData::Item* it = battleData.item(id); return it ? it->name[1] : std::string(); };
    auto sopiaName = [&](int id) { const cr3::BattleData::Sopia* so = battleData.sopia(id); return so ? so->name[1] : std::string(); };
    auto owns = [&](int type) {
        for (int id = 1; id < 300; ++id)
            if (const cr3::BattleData::Item* it = game.itemCount[id] > 0 ? battleData.item(id) : nullptr)
                if (it->type == type && (it->wearers >> ch & 1)) return true;
        return false;
    };
    auto ownsSopia = [&](bool monster) {
        for (int s = 1; s < 32; ++s)
            if (game.sopiaOwned >> s & 1u)
                if (const cr3::BattleData::Sopia* so = battleData.sopia(s); so && (so->growth == 1) == monster) return true;
        return false;
    };
    if (campEquipMode == 0) {
        rows.push_back({301, itemName(eq[0])});
        rows.push_back({302, itemName(eq[1])});
        if (eq[2] > 0) rows.push_back({303, itemName(eq[2])});
        else rows.push_back({303, menuName(0x4b), owns(3)});
        rows.push_back({0, sopiaName(eq[3]), false, false, eq[3]});
        if (ch == 0) rows.push_back({304, eq[4] > 0 ? sopiaName(eq[4]) : menuName(0x4d), eq[4] > 0 || !humanSopia().empty(), false, eq[4]});
        for (int s = 0; s < 3; ++s)
            rows.push_back({305 + s, eq[5 + s] > 0 ? sopiaName(eq[5 + s]) : menuName(0x4e + s), eq[5 + s] > 0 || ownsSopia(true), false, eq[5 + s]});
        rows.push_back({309, menuName(0x54)});
    } else if (campEquipMode >= 1 && campEquipMode <= 3) {
        const int slot = campEquipMode - 1;
        for (int id = 1; id < 300; ++id) {
            const cr3::BattleData::Item* it = game.itemCount[id] > 0 ? battleData.item(id) : nullptr;
            if (!it || it->type != campEquipMode || !(it->wearers >> ch & 1)) continue;
            int free = game.itemCount[id]; // (Cr3UtilGetNotEquippedItemNumber)
            for (int c : validChars())
                for (int e = 0; e < 3; ++e) free -= game.equip[c][e] == id;
            EquipRow r{id, it->name[1]};
            r.worn = eq[slot] == id;
            r.selectable = !r.worn && free > 0;
            rows.push_back(r);
        }
        if (campEquipMode == 3 && eq[2] > 0) rows.push_back({399, menuName(0x53)});
    } else if (campEquipMode == 4) {
        for (int s : humanSopia()) rows.push_back({400 + s, sopiaName(s), eq[4] != s, eq[4] == s, s});
        if (eq[4] > 0) rows.push_back({399, menuName(0x53)});
    } else if (campEquipMode >= 5 && campEquipMode <= 7) {
        for (int s = 1; s < 32; ++s) {
            const cr3::BattleData::Sopia* so = game.sopiaOwned >> s & 1u ? battleData.sopia(s) : nullptr;
            if (!so || so->growth != 1) continue;
            const bool mine = eq[5] == s || eq[6] == s || eq[7] == s;
            rows.push_back({400 + s, so->name[1], !mine, mine, s});
        }
        if (eq[campEquipMode] > 0) rows.push_back({399, menuName(0x53)});
    } else if (campEquipMode == 9) { // (SetSkillList: what the equipment and the sopia give)
        for (int e = 0; e < 3; ++e)
            if (const cr3::BattleData::Item* it = eq[e] > 0 ? battleData.item(eq[e]) : nullptr) {
                if (it->command > 0) rows.push_back({0, messageText("commandname", it->command), true});
                for (int s : it->skills)
                    if (s > 0) rows.push_back({0, messageText("autoskillname", s), true});
            }
        for (int k = 3; k < 8; ++k)
            if (const cr3::BattleData::Sopia* so = eq[k] > 0 ? battleData.sopia(eq[k]) : nullptr)
                for (int l = 0; l < std::min(int(game.sopiaLevel[eq[k]]), 5); ++l) {
                    if (so->commands[l] > 0) rows.push_back({0, messageText("commandname", so->commands[l]), true});
                    else if (so->skills[l] > 0) rows.push_back({0, messageText("autoskillname", so->skills[l]), true});
                }
    }
    return rows;
}

// Darwin's human sopia choices (SetHumanSopiaList: the character sopia had, the n-th past the first with flag 0xbe1 + n)
std::vector<int> Viewer::humanSopia() const {
    std::vector<int> out;
    int n = 0;
    for (int s = 1; s < 32; ++s) {
        const cr3::BattleData::Sopia* so = game.sopiaOwned >> s & 1u ? battleData.sopia(s) : nullptr;
        if (!so || so->growth != 0) continue;
        if (n >= 1 && n <= 6 && game.flag(0xbe1 + n)) out.push_back(s);
        ++n;
    }
    return out;
}

void Viewer::campEquipPick(int id) {
    const std::vector<EquipRow> rows = equipRows();
    const auto r = std::find_if(rows.begin(), rows.end(), [&](const EquipRow& x) { return x.id == id && x.selectable; });
    if (r == rows.end()) return;
    short* eq = game.equip[campEquipChar];
    if (campEquipMode == 0) {
        if (id == 309 || (id >= 301 && id <= 307)) {
            audio.playSe(0xb4, false);
            campEquipMode = id - 300, campEquipCursor = 0, campStart = uiClock;
        }
        return;
    }
    if (campEquipMode >= 1 && campEquipMode <= 3) {
        eq[campEquipMode - 1] = static_cast<short>(id == 399 ? 0 : id);
    } else if (campEquipMode == 4) {
        eq[4] = static_cast<short>(id == 399 ? 0 : id - 400);
    } else if (campEquipMode >= 5 && campEquipMode <= 7) {
        const int s = id == 399 ? 0 : id - 400;
        for (int c : validChars()) // (another wears it: asked first)
            if (c != campEquipChar && s > 0)
                for (int k = 5; k < 8; ++k)
                    if (game.equip[c][k] == s) {
                        audio.playSe(0xb4, false);
                        campTakeSopia = s, campTakeFrom = c, campUseStart = uiClock;
                        return;
                    }
        eq[campEquipMode] = static_cast<short>(s);
    } else return;
    audio.playSe(199, false);
    clampMp(campEquipChar);
    SDL_Log("camp: %d equips %d in mode %d", campEquipChar + 1, id, campEquipMode);
    campEquipMode = 0, campEquipCursor = 0, campStart = uiClock;
}

void Viewer::clampMp(int ch) { // (Cr3UtilAdjustment: the MP within the most)
    if (game.mp[ch] >= 0) game.mp[ch] = std::min(game.mp[ch], statsOf(ch).maxMp);
}

void Viewer::campTakeAnswer(bool yes) {
    if (yes) {
        for (int k = 5; k < 8; ++k)
            if (game.equip[campTakeFrom][k] == campTakeSopia) game.equip[campTakeFrom][k] = 0;
        clampMp(campTakeFrom);
        game.equip[campEquipChar][campEquipMode] = static_cast<short>(campTakeSopia);
        clampMp(campEquipChar);
        audio.playSe(199, false);
        campEquipMode = 0, campEquipCursor = 0, campStart = uiClock;
    } else audio.playSe(0xb4, false);
    campTakeSopia = 0, campTakeFrom = -1;
}

void Viewer::campEquipBack() {
    audio.playSe(0xb5, false);
    if (campTakeSopia) return campTakeAnswer(false);
    if (campEquipMode) campEquipMode = 0, campEquipCursor = 0, campStart = uiClock;
    else campList = 0, campCursor = 0, campStart = uiClock;
}

void Viewer::campEquipChange(int dir) { // (another character: the eye buttons, Left / Right)
    const std::vector<int> chars = validChars();
    if (chars.empty() || campEquipMode) return;
    const auto it = std::find(chars.begin(), chars.end(), campEquipChar);
    const int k = it == chars.end() ? 0 : int(it - chars.begin());
    campEquipChar = chars[size_t((k + dir + int(chars.size())) % int(chars.size()))];
    audio.playSe(0xb7, false);
}

void Viewer::drawEquipment(const Mat4& view, const Mat4& proj, int w, int h) {
    const float t = uiClock - campStart;
    const std::vector<EquipRow> rows = equipRows();
    campEquipCursor = std::clamp(campEquipCursor, 0, std::max(int(rows.size()) - 1, 0));
    const EquipRow* focused = rows.empty() ? nullptr : &rows[size_t(campEquipCursor)];
    // the right: the sopia's windows for a sopia, else the character's with the change the focused item makes
    const bool sopiaShown = focused && (campEquipMode == 0 ? (focused->id == 0 && focused->sopia > 0) || (focused->id >= 304 && focused->id <= 307)
                                                           : campEquipMode >= 4 && campEquipMode <= 7);
    if (sopiaShown) drawSopiaWindows(focused->sopia, t, view, proj, w, h);
    else {
        int change[5] = {0, 0, 0, 0, 0}, hpChange = 0, mpChange = 0;
        if (campEquipMode >= 1 && campEquipMode <= 3 && focused && focused->id != 0) { // (SetEquipStatusParamerter)
            int eq[8], eq2[8];
            for (int e = 0; e < 8; ++e) eq[e] = eq2[e] = game.equip[campEquipChar][e];
            eq2[campEquipMode - 1] = focused->id == 399 ? 0 : focused->id;
            const int lv = std::max(game.level[campEquipChar], 1);
            const auto a = cr3::BattleRules::playerStats(battleData, campEquipChar + 1, lv, eq, game.sopiaLevel, game.seed[campEquipChar], game.itemCount);
            const auto b = cr3::BattleRules::playerStats(battleData, campEquipChar + 1, lv, eq2, game.sopiaLevel, game.seed[campEquipChar], game.itemCount);
            for (int k = 0; k < 5; ++k) change[k] = b.param[k] - a.param[k];
            hpChange = b.maxHp - a.maxHp, mpChange = b.maxMp - a.maxMp;
        }
        drawStatusB(campEquipChar, hpChange, mpChange, t, view, proj);
        drawCapacity(campEquipChar, change, t, view, proj, w, h);
    }
    // the character buttons
    if (campEquipMode == 0) {
        UiFig& pos = uiFig("eyebtnpos");
        pos.fig.place = cr3::Mat34();
        poseUi(pos, "eyebtnpos", t);
        for (int c : validChars()) {
            float x, y;
            if (!uiJoint(pos, "call_eyeBtn_id" + std::string(c + 1 < 10 ? "0" : "") + std::to_string(c + 1), x, y)) continue;
            UiFig& b = uiFig("eyebtn_id");
            b.fig.place = cr3::Mat34();
            b.fig.place.m[3] = x, b.fig.place.m[7] = y;
            poseUi(b, "eyebtn_id", float(c + 1) / 20.0f);
            drawFigure(b.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(b, "tapAreaS01", sx, sy) && uiJoint(b, "tapAreaE01", ex, ey)) tapRects.push_back({7300 + c, sx, sy, ex, ey});
            if (c == campEquipChar) {
                for (const char* f : {"eyeslct", "eyecsr_loop"}) {
                    float px, py;
                    if (!uiJoint(b, std::string("call_") + (f[3] == 's' ? "eyeSlct" : "eyeCsr_loop"), px, py)) continue;
                    UiFig& s = uiFig(f);
                    s.fig.place = cr3::Mat34();
                    s.fig.place.m[3] = px, s.fig.place.m[7] = py;
                    poseUi(s, f, t, true);
                    drawFigure(s.fig, view.data());
                }
            }
        }
    }
    // the list (its tab - SetListItemParam's ListTag: none for the view, 1 the weapons, 3 the
    // armour, 5 the accessories, 9 the sopia and the skills)
    std::vector<ListRow> list;
    for (const EquipRow& r : rows) {
        ListRow lr;
        lr.id = r.id, lr.name = r.name, lr.number = -1, lr.selectable = r.selectable;
        if (r.id > 0 && r.id < 300)
            if (const cr3::BattleData::Item* it = battleData.item(r.id)) lr.icon = it->icon, lr.help = it->text[1];
        list.push_back(lr);
    }
    listRows = list;
    static const int kTabs[10] = {-1, 1, 3, 5, 9, 9, 9, 9, 9, 9};
    drawSkillList(list, focused ? focused->id : -1, t, view, w, h, kTabs[std::clamp(campEquipMode, 0, 9)], -1);
    if (campTakeSopia) {
        const std::string who = menuName(0x29 + campTakeFrom);
        drawAsk(formatAnnounce(announce(0x8a), {who}), uiClock - campUseStart, view, w, h, 7201, 7202);
    }
}

void Viewer::scrollScenarios(float dy) { scenarioScroll = std::clamp(scenarioScroll + dy, 0.0f, game.flag(1) ? 1.472f : 0.704f); }

void Viewer::drawScenarios(const Mat4& view, const Mat4& proj, int w, int h) {
    static const short kPanels[23] = {0, 1, 2, 5, 8, 11, 7, 10, 12, 3, 9, 13, 14, 4, 6, 17, 18, 19, 20, 15, 16, 21, 22};
    static const short kPanelFlags[23] = {151, 155, 158, 160, 166, 172, 168, 169, 171, 159, 167, 173, 175, 161, 165, 1, 178, 179, 180, 1, 2, 3, 4};
    static const short kChapters[4] = {3, 1, 2, 4}, kChapterFlags[4] = {11, 18, 19, 20}, kChapterNames[4] = {14, 15, 16, 17};
    const float t = uiClock - campStart;
    UiFig& bg = uiFig("scenariosbg");
    bg.fig.place = cr3::Mat34();
    bg.fig.place.m[7] = scenarioScroll;
    poseUi(bg, "scenariosbg", t);
    for (ViewMesh& vm : bg.fig.meshes) {
        const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
        const std::string jn = j < bg.joints.size() ? bg.joints[j] : "";
        vm.hidden = false;
        for (int k = 0; k < 23; ++k)
            if (jn == "GEO_panel0" + std::to_string(kPanels[k])) vm.hidden = !game.flag(kPanelFlags[k]);
        for (int k = 0; k < 4; ++k)
            if (jn == "GEO_chapter" + std::to_string(kChapters[k])) vm.hidden = game.flag(kChapterFlags[k]);
    }
    drawFigure(bg.fig, view.data());
    float x, y;
    for (int k = 0; k < 4; ++k)
        if (game.flag(kChapterFlags[k]) && uiJoint(bg, "typeM_L0" + std::to_string(k + 1), x, y)) drawUiText(menuName(kChapterNames[k]), 24, x, y, w, h, true);
    for (const char* f : {"snrhi_loop", "snrlow_loop"}) { // (the arrows: more above, more below)
        const float most = game.flag(1) ? 1.472f : 0.704f;
        if ((f[3] == 'h' && scenarioScroll <= 0) || (f[3] == 'l' && scenarioScroll >= most)) continue;
        UiFig& l = uiFig(f);
        l.fig.place = cr3::Mat34();
        poseUi(l, f, t, true);
        drawFigure(l.fig, view.data());
    }
    int opened = 0;
    for (int f = 1; f < 5; ++f) opened += game.flag(f);
    for (int f = 0x97; f < 0xc1; ++f) opened += game.flag(f);
    UiFig& pc = uiFig("percent_io");
    pc.fig.place = cr3::Mat34();
    poseUi(pc, "percent_io", t);
    drawFigure(pc.fig, view.data());
    if (uiJoint(pc, "call_dotNum_id1", x, y)) drawDigits(opened * 100 / 46, 80, 3, x, y, proj);
    UiFig& title = uiFig("menu");
    title.fig.place = cr3::Mat34();
    poseUi(title, "menusnr_io", t);
    drawFigure(title.fig, view.data());
    UiFig& bk = uiFig("btlback");
    bk.fig.place = cr3::Mat34();
    poseUi(bk, "btlback_in", t);
    drawFigure(bk.fig, view.data());
    float sx, sy, ex, ey;
    if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
}

void Viewer::drawPubRequest(const Mat4& view, int w, int h) {
    const float t = uiClock - campStart;
    const int req = game.globalWork[7];
    UiFig& win = uiFig("pubwin_io");
    win.fig.place = cr3::Mat34();
    poseUi(win, "pubwin_io", t);
    drawFigure(win.fig, view.data());
    const std::string text = messageText("pubtxt", req + 60);
    float x, y;
    size_t from = 0;
    for (int line = 1; line <= 8 && from <= text.size(); ++line) {
        const size_t at = text.find("\\n", from);
        if (uiJoint(win, "typeS_L0" + std::to_string(line), x, y))
            drawUiText(text.substr(from, at == std::string::npos ? std::string::npos : at - from), 22, x, y, w, h, true);
        if (at == std::string::npos) break;
        from = at + 2;
    }
    UiFig& girls = uiFig("pubgirls_io");
    girls.fig.place = cr3::Mat34();
    poseUi(girls, "pubgirls_io", t);
    drawFigure(girls.fig, view.data());
    UiFig& help = uiFig("camphelp_io");
    help.fig.place = cr3::Mat34();
    poseUi(help, "camphelp_io", t);
    drawFigure(help.fig, view.data());
    if (uiJoint(help, "typeM_L01", x, y)) drawUiText(messageText("pubtxt", req), 24, x, y, w, h, true);
    UiFig& bk = uiFig("btlback");
    bk.fig.place = cr3::Mat34();
    poseUi(bk, "btlback_in", t);
    drawFigure(bk.fig, view.data());
    float sx, sy, ex, ey;
    if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
}

// a camp button pressed (CampMenu::TouchSimpleRelease: 0xb4; InterfaceMain's camp step by it)
void Viewer::campPress(int id) {
    if (campUse) return campUseTap(id);
    if (campLink) {
        if (id == 100) audio.playSe(0xb5, false), campLink = 0, campStart = uiClock;
        return;
    }
    if (id == 201) scenarioScroll = 0;
    if ((id == 201 || (id == 202 && game.globalWork[7])) && !campList) {
        audio.playSe(0xb4, false);
        game.setFlag(id == 201 ? 0x41d : 0x41e, false);
        campLink = id, campStart = uiClock;
        SDL_Log("camp: %s", id == 201 ? "scenarios" : "the pub's request");
        return;
    }
    if (campList == 17) {
        if (id == 100) campEquipBack();
        else if (campTakeSopia && (id == 7201 || id == 7202)) campTakeAnswer(id == 7201);
        else if (id >= 7300 && id < 7310 && !campEquipMode) { if (id - 7300 != campEquipChar) audio.playSe(0xb7, false); campEquipChar = id - 7300; }
        else if (id >= 300 && id < 500) campEquipPick(id);
        return;
    }
    if (id == 100) {
        audio.playSe(0xb5, false);
        if (campList) campList = 0, campCursor = 0, campStart = uiClock;
        else closeCamp();
        return;
    }
    if (campList) return;
    audio.playSe(0xb4, false);
    if (id == 17) { // (InterfaceMain's camp step 2: the equipment, the first character's)
        const std::vector<int> chars = validChars();
        campList = 17, campEquipMode = 0, campEquipCursor = 0, campStart = uiClock;
        campEquipChar = !chars.empty() ? (std::find(chars.begin(), chars.end(), game.party[0]) != chars.end() ? game.party[0] : chars[0]) : 0;
        SDL_Log("camp: equipment of %d", campEquipChar + 1);
        return;
    }
    if (id == 16 || id == 18) {
        campList = id, campCategory = 0, campCursor = 0, campStart = uiClock;
        SDL_Log("camp: %s - %zu", id == 16 ? "items" : "sopia", campRows().size());
    } else if (id == 20) {
        optionsOpen = true, optionsStart = uiClock; // (CampOptionMenu)
        if (!game.flag(84)) optionsOpen = false, playTutorial("Tutorial_Option", 0, false);
    } else {
        SDL_Log("camp: button %d (not here yet)", id);
    }
}

void Viewer::campKey(SDL_Keycode k) {
    keysUsed = true;
    if (optionsKey(k)) return;
    if (campUse) { // (whom: Left / Right, Return; how many: Left / Right, Return yes, Backspace no)
        const int n = std::max(int(validChars().size()), 1);
        if (campUse == 1) {
            if (k == SDLK_LEFT || k == SDLK_UP) campUseTap(7000 + (campFace + n - 1) % n);
            else if (k == SDLK_RIGHT || k == SDLK_DOWN) campUseTap(7000 + (campFace + 1) % n);
            else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) campUseTap(7000 + campFace);
            else if (k == SDLK_BACKSPACE) campUseTap(100);
        } else {
            if (k == SDLK_LEFT || k == SDLK_DOWN) campUseTap(7101);
            else if (k == SDLK_RIGHT || k == SDLK_UP) campUseTap(7102);
            else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) campUseTap(7201);
            else if (k == SDLK_BACKSPACE || k == SDLK_N) campUseTap(7202);
        }
        return;
    }
    if (k == SDLK_BACKSPACE) return campPress(100);
    if (campLink == 201 && (k == SDLK_UP || k == SDLK_DOWN)) return scrollScenarios(k == SDLK_DOWN ? 0.1f : -0.1f);
    if (campList == 17) {
        if (campTakeSopia) {
            if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) campTakeAnswer(true);
            else if (k == SDLK_N) campTakeAnswer(false);
            return;
        }
        const std::vector<EquipRow> rows = equipRows();
        const int n = std::max(int(rows.size()), 1);
        if (k == SDLK_UP) campEquipCursor = (campEquipCursor + n - 1) % n;
        else if (k == SDLK_DOWN) campEquipCursor = (campEquipCursor + 1) % n;
        else if (k == SDLK_LEFT || k == SDLK_RIGHT) campEquipChange(k == SDLK_LEFT ? -1 : 1);
        else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && !rows.empty()) campEquipPick(rows[size_t(std::clamp(campEquipCursor, 0, n - 1))].id);
        return;
    }
    if (campList) {
        const int n = std::max(int(campRows().size()), 1);
        if (k == SDLK_UP) campCursor = (campCursor + n - 1) % n;
        else if (k == SDLK_DOWN) campCursor = (campCursor + 1) % n;
        else if (k == SDLK_LEFT || k == SDLK_RIGHT) { // (the items' two categories)
            if (campList == 16) campCategory = 1 - campCategory, campCursor = 0;
        } else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && campList == 16) {
            const std::vector<ListRow> rows = campRows();
            if (!rows.empty()) campPickItem(rows[size_t(std::clamp(campCursor, 0, int(rows.size()) - 1))].id);
        }
        return;
    }
    if (k == SDLK_UP) campCursor = (campCursor + 3) % 4;
    else if (k == SDLK_DOWN) campCursor = (campCursor + 1) % 4;
    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) campPress(campCursorButton());
}

auto Viewer::libraryRows() -> std::vector<ListRow> {
    static const short kOpenFlags[50] = {0, 1613, 1628, 1647, 1777, 0, 1573, 1573, 1573, 1573, 0, 0, 0, 0, 0, 1171, 1172,
                                         1173, 1174, 1175, 1176, 1177, 1178, 1179, 1180, 1181}; // (ArchiveOpenFlagTable)
    static const short kStarts[13] = {0, 5, 10, 14, 26, 30, 32, 34, 39, 42, 45, 47, 50}; // (ArchiveCategoryStartTable)
    std::vector<ListRow> rows;
    const int c = game.libraryArchive;
    if (c < 1 || c > 12) return rows;
    for (int k = kStarts[c - 1]; k < kStarts[c]; ++k)
        if (kOpenFlags[k] == 0 || game.flag(kOpenFlags[k])) {
            ListRow r;
            r.id = k + 1, r.name = messageText("librarytxt", k + 1), r.number = -1, r.icon = 69;
            if (game.flag(1350 + r.id)) r.seal = 2;
            rows.push_back(r);
        }
    return rows;
}

void Viewer::openLibrary() {
    library = Library();
    library.stage = 1, library.at = uiClock;
    SDL_Log("library: shelf %d, %zu entries", game.libraryArchive, libraryRows().size());
    if (libraryRows().empty()) library.stage = 0, pendingBranch = true;
}

void Viewer::libraryPick(const ListRow& r) {
    audio.playSe(0xb4, false);
    game.libraryId = r.id;
    library.stage = 2;
    SDL_Log("library: entry %d", r.id);
    callGimmick("library_disp");
    if (!script->running()) library.stage = 1, library.at = uiClock; // (no reading: the list again)
}

void Viewer::libraryBack() {
    audio.playSe(0xb5, false);
    library.stage = 0, pendingBranch = true; // (back to the field)
}

void Viewer::libraryTap(int id) {
    const std::vector<ListRow> rows = libraryRows();
    if (id == 100) return libraryBack();
    if (id >= 1000 && id < 1000 + int(rows.size())) library.focus = id - 1000;
    else
        for (const ListRow& r : rows)
            if (r.id == id) return libraryPick(r);
}

void Viewer::libraryKey(SDL_Keycode k) {
    const std::vector<ListRow> rows = libraryRows();
    const int n = std::max(int(rows.size()), 1);
    if (k == SDLK_UP) library.focus = (library.focus + n - 1) % n;
    else if (k == SDLK_DOWN) library.focus = (library.focus + 1) % n;
    else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && library.focus < int(rows.size())) libraryPick(rows[size_t(library.focus)]);
    else if (k == SDLK_BACKSPACE) libraryBack();
}

void Viewer::drawLibrary(const Mat4& view, int w, int h) {
    const std::vector<ListRow> rows = libraryRows();
    if (rows.empty()) return;
    library.focus = std::clamp(library.focus, 0, int(rows.size()) - 1);
    drawSkillList(rows, rows[size_t(library.focus)].id, uiClock - library.at, view, w, h, -1, -1);
}

std::vector<int> Viewer::partyChoices() const {
    std::vector<int> c;
    for (int ch = 0; ch < 7; ++ch)
        if (game.flag(1011 + ch) && game.party[0] != ch && game.party[1] != ch) c.push_back(ch);
    return c;
}

void Viewer::partyTap(int id) {
    auto stage = [&](int s) { party.stage = s, party.at = uiClock, party.focus = 0; };
    if (party.stage == 1) {
        if (id == 25) audio.playSe(0xb4, false), stage(2);
        else if (id == 13) audio.playSe(0xb4, false), party.stage = 0, callGimmick("party_talk"); // (FldPartyOrganize 3)
        else if (id == 100) audio.playSe(0xb5, false), party.stage = 0, pendingBranch = true;    // (2: the field)
    } else if (party.stage == 2) {
        if (id == 1 || id == 2) {
            if (id == 1 && game.flag(0x3fa)) return (void)audio.playSe(0xb6, false); // (the first fixed: flag 0x3fa)
            audio.playSe(0xb4, false), party.slot = id - 1, stage(3);
        } else if (id == 100) audio.playSe(0xb5, false), party.stage = 0, callGimmick("party_end"); // (4)
    } else if (party.stage == 3) {
        const std::vector<int> c = partyChoices();
        if (id >= 7000 && id < 7000 + int(c.size())) { // (a face: that one in the slot)
            game.party[party.slot] = c[size_t(id - 7000)];
            audio.playSe(0xb4, false);
            SDL_Log("party: %d, %d", game.party[0], game.party[1]);
            stage(2);
        } else if (id == 100) audio.playSe(0xb5, false), stage(2);
    }
}

void Viewer::partyKey(SDL_Keycode k) {
    if (party.stage == 1) {
        if (k == SDLK_1) partyTap(25);
        else if (k == SDLK_2) partyTap(13);
    } else if (party.stage == 2) {
        if (k == SDLK_1 || k == SDLK_2) partyTap(int(k - SDLK_1) + 1);
    } else if (party.stage == 3) {
        const int n = std::max(int(partyChoices().size()), 1);
        if (k == SDLK_LEFT || k == SDLK_UP) party.focus = (party.focus + n - 1) % n;
        else if (k == SDLK_RIGHT || k == SDLK_DOWN) party.focus = (party.focus + 1) % n;
        else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && !partyChoices().empty()) partyTap(7000 + party.focus);
    }
    if (k == SDLK_BACKSPACE) partyTap(100);
}

void Viewer::drawParty(const Mat4& view, const Mat4& proj, int w, int h) {
    const float t = uiClock - party.at;
    auto keepLook = buttonLook;
    buttonLook = nullptr;
    { // (PartySelectTitleMenu: `menu` on menuPty_io, in once - held after the first stage, as the shop's)
        UiFig& title = uiFig("menu");
        title.fig.place = cr3::Mat34();
        poseUi(title, "menupty_io", t + (party.stage > 1 ? 10.0f : 0.0f));
        drawFigure(title.fig, view.data());
    }
    auto back = [&] {
        UiFig& bk = uiFig("btlback");
        bk.fig.place = cr3::Mat34();
        poseUi(bk, "btlback_in", t);
        drawFigure(bk.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
    };
    auto portrait = [&](int layout, int ch) { // (the cut-in windows' portrait of a character)
        if (ch < 0) return;
        ViewFigure& f = cutInFigures[size_t(layout)];
        if (f.meshes.empty()) return;
        char tex[24];
        std::snprintf(tex, sizeof tex, ch == 5 ? "cutIn006_L02" : "cutIn00%d_L01", ch + 1);
        f.place = cr3::Mat34();
        pose(f, cr3::EventRuntime::kCutInSeconds, false);
        cr3::Texture* face = texture(tex);
        for (ViewMesh& vm : f.meshes) vm.texture = face ? face : texture(vm.baseMaterial.diffuse());
        drawFigure(f, view.data());
    };
    auto under = [&] { // (partyUnder: across a wide screen)
        UiFig& u = uiFig("partyunder");
        u.fig.place = cr3::Mat34();
        const float aspect = float(w) / float(std::max(h, 1));
        if (aspect >= 1.5f) u.fig.place.m[0] = aspect / 1.5f;
        else u.fig.place.m[7] = -0.04f;
        poseUi(u, "partyunder", t);
        drawFigure(u.fig, view.data());
    };
    auto slots = [&](const char* anim, bool both) { // (partyBtn: the slots' circle buttons, 1 and 2)
        UiFig& b = uiFig("partybtn");
        b.fig.place = cr3::Mat34();
        poseUi(b, anim, t);
        drawFigure(b.fig, view.data());
        int k = 0;
        for (const std::string& j : b.joints) {
            if (j.rfind("call_circleBtn_fr", 0) != 0) continue;
            if (++k > (both ? 2 : 1)) break;
            const int ch = game.party[both ? k - 1 : party.slot];
            float x, y;
            if (!uiJoint(b, j, x, y)) continue;
            UiFig& c = uiFig("circlebtn_fr");
            c.fig.place = cr3::Mat34();
            c.fig.place.m[3] = x, c.fig.place.m[7] = y;
            poseUi(c, "circlebtn_fr", float(ch >= 0 ? ch + 1 : 0) / 20.0f);
            for (ViewMesh& vm : c.fig.meshes) { // (the first fixed: not selectable)
                vm.material.params[0x33] = both && k == 1 && game.flag(0x3fa) ? std::vector<float>{0.5f, 0.5f, 0.5f, 1.0f} : vm.baseMaterial.params[0x33];
                if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
            }
            focusLook(c, both ? k : -2);
            drawFigure(c.fig, view.data());
            float lx, ly;
            if (ch >= 0 && uiJoint(c, "call_dotNum_id30", lx, ly)) drawDigits(game.level[ch], 120, 3, lx, ly, proj); // (SetLevelNumbers)
            float sx, sy, ex, ey;
            if (both && uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) tapRects.push_back({k, sx, sy, ex, ey});
        }
    };
    if (party.stage == 1) { // (console_io to half its length, as ShopSecondMenu's: Initialize's ChangeAnimeTime)
        UiFig& con = uiFig("console_io");
        poseUi(con, "console_io", 0);
        drawCommandMenu("console_io", std::min(t, con.fig.animLoaded ? con.fig.anim.duration() * 0.5f : t), view, {}, {}, w, h, 0, "console_io");
        back();
    } else if (party.stage == 2) {
        portrait(4, game.party[0]), portrait(3, game.party[1]);
        under(), slots("partybtn1_io", true), back();
    } else if (party.stage == 3) {
        portrait(4, game.party[party.slot]);
        under(), slots("partybtn2_io", false);
        const std::vector<int> c = partyChoices();
        UiFig& fw = uiFig("facewin_io");
        fw.fig.place = cr3::Mat34();
        poseUi(fw, "facewin_io", t);
        drawFigure(fw.fig, view.data());
        party.focus = std::clamp(party.focus, 0, std::max(int(c.size()) - 1, 0));
        for (size_t k = 0; k < c.size(); ++k) {
            float x, y;
            if (!uiJoint(fw, "call_faceBtn" + std::to_string(k + 1), x, y)) continue;
            UiFig& b = uiFig("facebtn");
            b.fig.place = cr3::Mat34();
            b.fig.place.m[3] = x, b.fig.place.m[7] = y;
            poseUi(b, "facebtn", 0);
            if (cr3::Texture* tex = texture("faceBtn_00" + std::to_string(c[k] + 1)))
                for (ViewMesh& vm : b.fig.meshes) vm.texture = tex;
            drawFigure(b.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(b, "tapAreaS01", sx, sy) && uiJoint(b, "tapAreaE01", ex, ey)) tapRects.push_back({7000 + int(k), sx, sy, ex, ey});
            if (int(k) == party.focus) {
                UiFig& sl = uiFig("faceslct");
                sl.fig.place = cr3::Mat34();
                sl.fig.place.m[3] = x, sl.fig.place.m[7] = y;
                poseUi(sl, "faceslct", t, true);
                drawFigure(sl.fig, view.data());
            }
        }
        back();
    }
    buttonLook = keepLook;
}

std::vector<int> Viewer::cr3PartyMembers() const {
    std::vector<int> out;
    for (int c : game.cr3Party)
        if (c > 0) out.push_back(c);
    return out;
}

// (Gene Swap - CampcomBtnMenu::SetVisibleGeneChangeButton: once flag 41 is set, and in the VS mode
// or once a gift has been merged, flag 21)
bool Viewer::cr3PanelLocked(int member) const {
    for (int slot = 0, k = 0; slot < 5; ++slot)
        if (game.cr3Party[slot] > 0 && k++ == member) return cr3PanelLock[size_t(slot)];
    return false;
}

// (SetVisibleGeneChangeButton 0xa360cc, both modes: also a gene in the 50 box places - 0x14bf8d0 -
// 0x16a8, any id > 0 -, CR3-U's genes not stocked. The event mode - InterfaceMain::SetCampWindowParty
// 0x8f00dc, CampcomBtnMenu::SetParamNumber 5 0xa35e44 -: Equipment and Status, Gene Swap by that
// rule, the rest not selectable)
bool Viewer::cr3CampButtonOn(int id) const {
    const bool geneSwap = game.flag(41) && (vs.screen != 0 || game.flag(21)) &&
                          std::any_of(game.cr3Genes.begin(), game.cr3Genes.end(), [](const cr3::Cr3Gene& g) { return !g.stocked; });
    if (cr3CampEvent) return id == 1 || id == 3 || (id == 2 && geneSwap);
    return id != 2 || geneSwap;
}

void Viewer::openCr3Camp() {
    cr3Camp.stage = 1, cr3Camp.since = 0, cr3Camp.button = 0, cr3Camp.panel = -1;
    campTopAsk = 0, campTopStalled = false;
    audio.playSe("com_003", false);
    SDL_Log("camp: open (CR3)");
}

void Viewer::drawCr3Camp(int w, int h, const Mat4& view) {
    if (cr3Camp.stage == 10) {
        drawMyPage(w, h, view);
        if (myPage.screen == 3) drawCr3CardView(); // (the web view over all)
        return;
    }
    const float t = cr3Camp.since;
    float x, y, cx, cy;
    // (CampTopMenu: the background and the party's panels)
    UiFig& top = cr3UiPut("camtopposition", "", 0, 0, 0, view);
    if (cr3UiCall(top, "call_comBgMenu", x, y, w, h)) cr3UiPut("combgmenu", "", 0, x, y, view);
    dev->clearDepth();
    if (cr3Camp.stage == 3) return drawCr3CampGenes(w, h, view);
    if (cr3Camp.stage == 4) {
        if (campItems.step != 2) drawCr3CampFooter(w, h, view);
        return drawCampItems(w, h, view);
    }
    if (cr3Camp.stage == 5) return drawCampEquip(w, h, view);
    if (cr3Camp.stage == 6) return drawCampGifts(w, h, view);
    if (cr3Camp.stage == 7) return drawCampQuests(w, h, view);
    if (cr3Camp.stage == 8) return drawCampStatus(w, h, view);
    if (cr3Camp.stage == 9) return drawCampOptions(w, h, view);
    if (cr3Camp.stage == 1 && cr3UiCall(top, "call_camTopCharaMenu", x, y, w, h)) {
        UiFig& chara = cr3UiPut("camtopcharamenu", "", 0, x, y, view);
        const std::vector<int> members = cr3PartyMembers();
        for (int k = 4; k >= 0; --k) { // (right to left: each panel's A8 mask - drawn black, depth alone - clips the art of the one before it, as the reference's curved edges show)
            if (k >= int(members.size())) {
                if (k > 0 && cr3UiCall(chara, "call_camTopNoEntory_fr00_00" + std::to_string(k), cx, cy)) cr3UiPut("camtopnoentory_fr00", "", 0, cx, cy, view);
                continue;
            }
            if (!cr3UiCall(chara, "call_camTopCharaPanel_00" + std::to_string(k + 1), cx, cy)) continue;
            // (SetPanelLock: SetSelectStatus(3) - the panel and its parts' diffuse 0.5 0.5 0.5, the
            // status colours DAT_00f63890 / 0x00f638a0's -, kept till unlocked)
            const bool locked = cr3PanelLocked(k);
            struct Restore {
                float* t;
                ~Restore() { t[0] = t[1] = t[2] = 1; }
            } restore{cr3UiTint};
            if (locked) cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 0.5f;
            UiFig& panel = cr3UiPut("camtopcharapanel", "", 0, cx, cy, view);
            // (the Vita's focus - FUN_81114054: +0x16c 2, the diffuse white, no tint - is camTopSort
            // alone, drawn below)
            const bool focused = vitaButtons() && k == cr3Camp.panel && !locked;
            const int gk = game.wornGene(members[size_t(k)]);
            const cr3::Cr3Gene* g = gk >= 0 ? &game.cr3Genes[size_t(gk)] : nullptr;
            const cr3::Cr3OfflineDb::GeneInfo* gi = g && game.cr3Db ? game.cr3Db->geneInfo(g->master) : nullptr;
            float px, py;
            if (cr3UiCall(panel, "call_camTopGene_id00", px, py)) {
                UiFig& pic = uiFig("camtopgene_id00");
                char name[16];
                std::snprintf(name, sizeof name, "cutin_%03d", gi ? gi->series : 0);
                cr3UiMaterial(pic, "mat_cutin_001", name);
                // (SetMaterialOffsetSet1: InterfaceMain::GetCardOffsetParameter(series, 0 / 1) -
                // UI_cardUV.dat, six floats a series)
                const float u = cardUV(gi ? gi->series : 0, 0), v = cardUV(gi ? gi->series : 0, 1);
                for (ViewMesh& vm : pic.fig.meshes) {
                    const uint32_t mi = vm.source.material;
                    if (mi < pic.materials.size() && pic.materials[mi] == "mat_cutin_001") vm.material.params[0x5e] = {u, v};
                }
                cr3UiPut("camtopgene_id00", "", 0, px, py, view);
            }
            if (cr3UiCall(panel, "call_camTopStatusPanel", px, py)) {
                UiFig& st = cr3UiPut("camtopstatuspanel", "", 0, px, py, view);
                float sx, sy;
                if (cr3UiCall(st, "call_camTopIcon_fr00", sx, sy)) cr3UiPut("camtopicon_fr00", "", float(members[size_t(k)] - 1) / 30, sx, sy, view);
                if (gi) {
                    const int level = game.cr3Db->geneLevel(*g);
                    // (the character's HP and MP - its own, its gene's, its accessory's -, the MP left)
                    cr3::Cr3Fighter cf;
                    cr3CharaStats(members[size_t(k)], cf);
                    const int mp = cr3CharaMp(members[size_t(k)], cf.maxMp);
                    if (cr3UiCall(st, "call_camTopRerity_fr00", sx, sy)) cr3UiPut("camtoprerity_fr00", "", float(gi->grade) / 30, sx, sy, view);
                    if (cr3UiCall(st, "call_camTopLvBar_per", sx, sy)) cr3UiPut("camtoplvbar_per", "", float(level) * 100.0f / float(std::max(gi->maxLevel, 1)) / 30, sx, sy, view);
                    if (cr3UiCall(st, "call_camTopHpBar_per", sx, sy)) cr3UiPut("camtophpbar_per", "", 100.0f / 30, sx, sy, view);
                    if (cr3UiCall(st, "call_camTopMpBar_per", sx, sy)) cr3UiPut("camtopmpbar_per", "", float(cf.maxMp > 0 ? mp * 100 / cf.maxMp : 0) / 30, sx, sy, view);
                    for (const auto& [joint, fig, value, places] :
                         {std::tuple<const char*, const char*, int, int>{"call_camTopLv_num00", "camtoplv_num00", level, 3},
                          {"call_camTopHp_num00", "camtophp_num00", cf.maxHp, 5}, {"call_camTopMp_num00", "camtopmp_num00", mp, 4}}) {
                        if (!cr3UiCall(st, joint, sx, sy)) continue;
                        UiFig& num = cr3UiPut(fig, "", 0, sx, sy, view);
                        cr3UiNumber(num, std::strcmp(fig, "camtoplv_num00") == 0 ? "call_camTopNumLv_fr00" : "call_camTopNum_fr00",
                                    std::strcmp(fig, "camtoplv_num00") == 0 ? "camtopnumlv_fr00" : "camtopnum_fr00", value, places, 0, view);
                    }
                }
            }
            // (CampCharaPanel::Render 0x8111308a: camTopSort - its loop camTopSort_loop - at
            // call_camTopSort while +0x16c is 2: the focused panel, arrow01 / arrow02 hidden
            // (FUN_81114054); taken up by ○ held, SetSelectStatus(2) 0xa33f70 shows them. Its +0.5 z
            // isn't followed: the panels are flat)
            if (focused && cr3UiCall(panel, "call_camTopSort", px, py)) {
                UiFig& sort = uiFig("camtopsort");
                for (const char* arrow : {"arrow01", "arrow02"}) showJoint(sort, arrow, cr3Camp.confirmHeld);
                cr3UiPut("camtopsort", "camtopsort_loop", t, px, py, view, 1, true);
            }
        }
    }
    if (cr3Camp.stage == 1 && cr3UiCall(top, "call_camTopFramMenu", x, y, w, h)) cr3UiPut("camtopframmenu", "", 0, x, y, view);
    if (cr3Camp.stage == 1 && !campTopTold.empty()) drawCr3Told(campTopTold, uiClock - campTopToldAt, 502, w, h, view, &cr3CampTaps);
    // (CampcomBtnMenu: its buttons)
    if (cr3Camp.stage == 1) {
        UiFig& bp = cr3UiPut("combtnsposition", "", 0, 0, 0, view);
        if (cr3UiCall(bp, "call_comBtnSMenu", x, y, w, h)) {
            UiFig& menu = cr3UiPut("combtnsmenu", "combtnsmenu_io", t, x, y, view);
            for (int b = 0; b < 7; ++b) {
                char joint[32];
                std::snprintf(joint, sizeof joint, "call_comBtnSPanel_%03d", 4 + b);
                float px, py;
                if (!cr3UiCall(menu, joint, px, py)) continue;
                const int id = kCampButtons[b];
                const bool off = !cr3CampButtonOn(id);
                UiFig& panel = cr3UiPut("combtnspanel", "", 0, px, py, view, off ? 0.5f : 1.0f);
                if (cr3UiCall(panel, "call_comBtnBg_fr00", cx, cy)) cr3UiPut("combtnbg_fr00", "", float(b == cr3Camp.button && cr3Camp.panel < 0 ? 1 : 0) / 30, cx, cy, view, off ? 0.5f : 1.0f);
                if (cr3UiCall(panel, "call_comBtnIconS_fr00", cx, cy)) cr3UiPut("combtnicons_fr00", "", float(id) / 30, cx, cy, view, off ? 0.5f : 1.0f);
                if (cr3UiCall(panel, "call_comBtnR", cx, cy)) cr3UiPut("combtnr", "", 0, cx, cy, view);
                float ax, ay, ex, ey;
                if (!off && cr3UiCall(panel, "tapAreaCP01", ax, ay) && cr3UiCall(panel, "tapAreaAP01", ex, ey)) cr3CampTaps.push_back(circleTap(ax, ay, ex, ey, 200 + b));
            }
        }
    }
    if (cr3Camp.stage == 2) {
        drawCr3CampCharas(w, h, view);
        drawCr3Header(t, 3, {}, w, h, view);
    }
    if (cr3Camp.stage == 1) { // (SetCampWindowParameter: HeaderMenu 0x42, title 0 - "Camp"; an interface of its own, over the panels' depth masks)
        dev->clearDepth();
        drawCr3Header(t, 0, {}, w, h, view);
    }
    drawCr3CampFooter(w, h, view);
    // (the event mode's question, 0x1f0, over all: its Yes and No the only taps - CampTopMenu state 7
    // takes no button nor panel -; Yes taken, nothing till the camp closes)
    if (cr3Camp.stage == 1 && campTopAsk != 0) cr3CampTaps.clear();
    if (cr3Camp.stage == 1 && campTopAsk == 1)
        drawCr3Asked(menuAnnounce(0x1f0), uiClock - campTopAskAt, vitaButtons() ? campTopAskFocus : 0, true, 503, 504, w, h, view);
}

// (CampTopMenu::Update 0xa38130 state 2: 0.23333 s after Yes - InterfaceMain +0x98, the event mode's
// DeleteBattleAnnouncementMenu and GameMain::BuildUnitModel(true) -, 0x1c: the header, footer and
// buttons closed, RePauseFieldInterface; CheckAliveCampWindowParty false. No Cr3FadeIn: +0x13b
// isn't set by SetCampWindowParty)
void Viewer::closeCampTopEvent() {
    campTopAsk = 0, campTopStalled = false;
    cr3Camp.stage = 0, cr3Camp.since = 0;
    cr3RebuildLeader();
    SDL_Log("camp: closed (CR3)");
}

// (CampFutterMenu / ItemFutterMenu: the team - its name at typeM_L01 -, the back)
void Viewer::drawCr3CampFooter(int w, int h, const Mat4& view) {
    float x, y, cx, cy;
    UiFig& foot = cr3UiPut("comftrposition", "", 0, 0, 0, view);
    if (cr3UiCall(foot, "call_comFtrTeamMenu", x, y, w, h)) {
        UiFig& team = cr3UiPut("comftrteammenu", "", 0, x, y, view);
        // (ItemFutterMenu::SetNumberOfLevel: the team level's three digits, zeros kept)
        float nx, ny;
        if (cr3UiCall(team, "call_comFtrNumPanel_num00", nx, ny)) {
            UiFig& num = cr3UiPut("comftrnumpanel_num00", "", 0, nx, ny, view);
            const int lv = std::clamp(game.cr3TeamLevel, 0, 999), digit[3] = {lv / 100, lv / 10 % 10, lv % 10};
            for (int k = 0; k < 3; ++k) {
                float dx, dy;
                if (cr3UiCall(num, "call_comFtrNum_fr00_00" + std::to_string(k + 1), dx, dy)) cr3UiPut("comftrnum_fr00", "", float(digit[k]) / 30, dx, dy, view);
            }
        }
        const std::string name = game.cr3Names["team"];
        float tx, ty;
        if (cr3UiCall(team, "typeM_L01", tx, ty)) {
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            drawUiText(name, 24, (tx + 0.075f) / 10, (ty - 0.15f) / 10, w, h, true, 0.25f); // (dark, as the reference)
        }
    }
    if (cr3UiCall(foot, "call_comBackMenu_001", x, y, w, h)) {
        UiFig& menu = cr3UiPut("combackmenu", "", 0, x, y, view);
        float ax, ay, ex, ey;
        if (cr3UiCall(menu, "call_comBack_fr00", cx, cy)) cr3UiPut("comback_fr00", "", 0, cx, cy, view);
        if (cr3UiCall(menu, "tapAreaS01", ax, ay) && cr3UiCall(menu, "tapAreaE01", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, -1});
    }
}

// Genes' characters (GeneCharaListMenu: comListMenu, rows of comListLGene 1.24 apart)
void Viewer::drawCr3CampCharas(int w, int h, const Mat4& view) {
    float x, y, lx, ly, cx, cy;
    UiFig& pos = cr3UiPut("comlistposition", "comlistposition_io", cr3Camp.since, 0, 0, view);
    if (!cr3UiCall(pos, "call_comListMenu", x, y, w, h)) return;
    UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
    if (!cr3UiCall(menu, "call_comListPanel", lx, ly)) return;
    const std::vector<int> members = cr3PartyMembers();
    const int top = std::clamp(cr3Camp.row - 1, 0, std::max(int(members.size()) - 3, 0));
    for (int k = top; k < int(members.size()) && k < top + 3; ++k) {
        const int ch = members[size_t(k)];
        UiFig& row = cr3UiPut("comlistlpanel", "", 0, lx, ly - 1.24f * float(k - top), view);
        if (k == cr3Camp.row && cr3UiCall(row, "call_comListLCursor", cx, cy)) cr3UiPut("comlistlcursor", "", 0, cx, cy, view);
        if (cr3UiCall(row, "call_comListLGene", cx, cy)) {
            UiFig& gene = cr3UiPut("comlistlgene", "", 0, cx, cy, view);
            float gx, gy;
            if (cr3UiCall(gene, "call_comListCharaIcon_fr00", gx, gy)) cr3UiPut("comlistcharaicon_fr00", "", float(ch - 1) / 30, gx, gy, view);
            cr3UiLabel(gene, "typeM_L01", cr3NameOf("char_name.mbe", "CharName", ch), w, h, 0.26f);
            const int gk = game.wornGene(ch);
            if (gk >= 0 && game.cr3Db) {
                const cr3::Cr3Gene& g = game.cr3Genes[size_t(gk)];
                if (const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db->geneInfo(g.master)) {
                    if (cr3UiCall(gene, "call_comListGeneIcon_id00", gx, gy)) {
                        UiFig& icon = uiFig("comlistgeneicon_id00");
                        char pic[16];
                        std::snprintf(pic, sizeof pic, "card_%03d", gi->series);
                        cr3UiMaterial(icon, "mat_card_001", pic);
                        cr3UiPut("comlistgeneicon_id00", "", 0, gx, gy, view);
                    }
                    const int level = game.cr3Db->geneLevel(g);
                    if (cr3UiCall(gene, "call_comListLv_per", gx, gy)) cr3UiPut("comlistlv_per", "", float(level) * 100.0f / float(std::max(gi->maxLevel, 1)) / 30, gx, gy, view);
                    if (cr3UiCall(gene, "call_comListNumL_num00", gx, gy)) {
                        UiFig& num = cr3UiPut("comlistnuml_num00", "", 0, gx, gy, view);
                        cr3UiNumber(num, "call_comListNum_fr00", "comlistnum_fr00", level, 3, 10, view, 1, level >= gi->maxLevel ? 40 : -1);
                    }
                    cr3UiLabel(gene, "typeM_L02", cr3NameOf("gene_name.mbe", "GeneName", g.master), w, h, 0.26f);
                }
            }
        }
        float ax, ay, ex, ey;
        if (cr3UiCall(row, "tapAreaS01", ax, ay) && cr3UiCall(row, "tapAreaE01", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, 300 + k});
    }
}

// a character's gene: the lab's cards (camp: all held; the focused card's status), each card
// worn by another marked with its camEquChr_fr00 (the character - 1)
void Viewer::drawCr3CampGenes(int w, int h, const Mat4& view) {
    if (geneInspectOpen) {
        drawGeneBase(w, h, view, &inspectedGene);
        cr3CampTaps.clear();
        cr3CampTaps.push_back({-100, -100, 100, 100, 9010});
        return;
    }
    // (the base select's drawing, its genes and focus the camp's)
    std::string help;
    if (vitaButtons() && cr3Camp.geneButton && cr3Camp.focus < int(cr3Camp.genes.size())) {
        const auto& gene = game.cr3Genes[size_t(cr3Camp.genes[size_t(cr3Camp.focus)])];
        int at = 0;
        for (const auto& slot : gene.slots)
            if (slot[1] >= 0 && at++ == cr3Camp.geneSkill) {
                help = slot[0] == 1 ? cr3NameOf("skill_help.mbe", "SkillHelp", slot[1]) : cr3NameOf("command_help.mbe", "CommandHelp", slot[1]);
                break;
            }
    }
    std::swap(geneMenu.genes, cr3Camp.genes), std::swap(geneMenu.focus, cr3Camp.focus);
    const int mode = geneMenu.mode;
    const float since = geneMenu.since;
    geneMenu.mode = 0, geneMenu.since = cr3Camp.since;
    geneLabTaps.clear();
    drawGeneBase(w, h, view);
    geneMenu.mode = mode, geneMenu.since = since;
    std::swap(geneMenu.genes, cr3Camp.genes), std::swap(geneMenu.focus, cr3Camp.focus);
    for (const Cr3GeneLabTap& t : geneLabTaps) cr3CampTaps.push_back(t);
    // (the wearers' marks over the cards in view)
    UiFig& lp = uiFig("genecnglistmenu");
    float px[9], py[9];
    int slots = 0;
    for (const std::string& j : lp.joints)
        if (j.compare(0, 21, "call_geneCngListPanel") == 0 && slots < 9 && uiJoint(lp, j, px[slots], py[slots])) ++slots;
    if (slots == 9)
        for (int s = 1; s <= 8; ++s) {
            const int g = cr3Camp.focus + s - 5;
            if (g < 0 || g >= int(cr3Camp.genes.size())) continue;
            const int wearer = game.geneWearer(cr3Camp.genes[size_t(g)]);
            if (wearer <= 0) continue;
            const float scale = s == 5 ? 4.0f : 3.0f;
            cr3UiPut("camequchr_fr00", "", float(wearer - 1) / 30, px[s] - 0.1f * scale, py[s] + 0.25f + 0.18f * scale, view);
        }
    float x, y, cx, cy;
    UiFig& foot = cr3UiPut("comftrposition", "", 0, 0, 0, view);
    if (cr3UiCall(foot, "call_comBackMenu_001", x, y, w, h)) {
        UiFig& menu = cr3UiPut("combackmenu", "", 0, x, y, view);
        float ax, ay, ex, ey;
        if (cr3UiCall(menu, "call_comBack_fr00", cx, cy)) cr3UiPut("comback_fr00", "", 0, cx, cy, view);
        if (cr3UiCall(menu, "tapAreaS01", ax, ay) && cr3UiCall(menu, "tapAreaE01", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, -1});
    }
    drawCr3Header(cr3Camp.since, 3, help, w, h, view);
    if (cr3Camp.geneTake >= 0) drawCr3Asked(menuAnnounce(0x241), cr3Camp.since, cr3Camp.geneAskFocus, true, 9001, 9002, w, h, view);
}

void Viewer::chooseCr3Camp(int id) {
    if (cr3Camp.stage == 3 && geneInspectOpen) { geneInspectOpen = false; audio.playSe("com_004", false); return; }
    if (cr3Camp.stage == 3 && cr3Camp.geneTake >= 0 && (id == 9001 || id == 9002 || id == -1)) {
        if (id == 9001) chooseCr3Camp(kTapCard + cr3Camp.geneTake);
        else cr3Camp.geneTake = -1, audio.playSe("com_004", false);
        return;
    }
    if (cr3Camp.stage == 0) {
        if (id == 100) { // (feBtnOpen: the panel opened - com_008 - or closed - com_009)
            cr3Camp.panelOpen = !cr3Camp.panelOpen, cr3Camp.panelAt = uiClock;
            cr3Camp.barCursor = 5; // (the Vita's cursor: on Camp, opened or shut)
            audio.playSe(cr3Camp.panelOpen ? "com_008" : "com_009", false);
            SDL_Log("field: the buttons %s", cr3Camp.panelOpen ? "opened" : "closed");
        } else if (id == 106) // (feBtnShop - the Camp picture: TapChkFieldTabMenu 1, Fld2Main state 6 - SM_Camp)
            openCr3Camp();
        else if (id == 103) // (feBtnBattle: Fld2TaskEncount::ForceBattleIn - a dungeon's alone; SE 0x16d)
            audio.playSe(0x16d, false), forceEncounter();
        else if (id == 104) // (feBtnCamp - the My Page picture: 6, state 9 - SM_MyPage)
            openMyPage();
        else if (id == 105) // (feBtnMypage - the SpShop picture: 7, state 10 - SM_Shop: CreatePiucomTop, the premium shop - PiuComMenu, data\shops' com_1)
            openPremiumShop();
        else if (id == 102) { // (Fld2TabMenu tab 4: Fld2TaskEncount::SetEnable; SE 0x16d)
            encountersOff = !encountersOff;
            audio.playSe(0x16d, false);
            SDL_Log("field: encounters %s", encountersOff ? "off" : "on");
        } else if (id == 108 || id == 110) { // (feBtnTown: Fld2TabMenu tab 8 - the map's, SetDirectTownMove -, 10 - the panel's)
            // (tab 10 in a dungeon: CreateTownMoveDungeonWindow, then Fld2Main::SM_TownJump - the area
            // script's DungeonExit, which asks and takes the party home)
            if (id == 108 || !cr3InDungeon()) audio.playSe(0x16d, false), openTownMove(true);
            else audio.playSe(0x16d, false), runAreaFunction("DungeonExit");
        } else if (id >= 120 && id < 126) { // (the goods button - 125 the ride's)
            useGoods(id - 120);
        } else if (id == 130 || id == 131) { // (ToyCameraMenu: feGoodsToyBtn - the shutter -, feGoodsBack)
            toyCamPress(id == 130);
        } else if (id == 101) { // (feBtnSave: Fld2TabMenu::Save)
            audio.playSe(0x16d, false);
            writeCr3Save();
        } else
            SDL_Log("field: button %d (not in yet)", id - 100);
        return;
    }
    if (cr3Camp.stage == 10) return chooseMyPage(id);
    if (!campTopTold.empty()) { // (an alert over the top: any press puts it away)
        if (id == 502 || id == -1 || id >= 200) campTopTold.clear(), audio.playSe("com_004", false);
        return;
    }
    if (cr3Camp.stage == 1 && campTopAsk == 2) return; // (Yes taken: closing)
    if (cr3Camp.stage == 1 && campTopAsk == 1) { // (CampTopMenu::Update state 7)
        if (id == 503) { // (Yes - GetBattleYNChoicePushBottom 1: com_003, state 1 .. 2)
            audio.playSe("com_003", false);
            campTopAsk = 2, campTopAskAt = uiClock;
            SDL_Log("camp: the party chosen (0x1f0 Yes)");
        } else if (id == 504) { // (No - 2: com_004, state 0)
            audio.playSe("com_004", false);
            campTopAsk = 0;
            SDL_Log("camp: the party not chosen (0x1f0 No)");
        } else if (id == -1) { // (the hard return, checked first: com_004, the YN ended, state 6)
            audio.playSe("com_004", false);
            campTopAsk = 0, campTopStalled = true;
            SDL_Log("camp: the question put away - the buttons not taken (state 6)");
        }
        return;
    }
    // (CampTopMenu::SetParamNumber 2 0xa389cc, the event mode - +0xf5 -: the footer's com_004,
    // ResetCampTopFutter; GetPartyMemberEquipGene 0x8fbd6c - every member of CreateListForCamp
    // wearing a gene -: +0xc0 7, SetBasicYNChoiceWindow(0x1f0); one without, the alert 0x307
    // (SetAutoEndAlertWindowMenu: any tap ends it), the camp staying at state 0)
    if (id == -1 && cr3Camp.stage == 1 && cr3CampEvent) {
        audio.playSe("com_004", false);
        campTopStalled = false;
        bool all = true;
        for (int m : cr3PartyMembers()) all = all && game.wornGene(m) >= 0;
        if (!all) {
            campTopTold = menuAnnounce(0x307), campTopToldAt = uiClock;
            SDL_Log("camp: a member without a gene - 0x307");
        } else {
            campTopAsk = 1, campTopAskAt = uiClock, campTopAskFocus = 0;
            SDL_Log("camp: is this the party? (0x1f0)");
        }
        return;
    }
    // (CampTopMenu::SetParamNumber 2 0xa389cc: the VS mode's camp closing with flag 42 off -
    // GetPartyMainMemberEquipGene 0x8fbdf8, CreateListForCamp's first three alone -: each wearing a
    // gene sets 42 and 3505 (the VS tutorial's next step); one without, alert 0x307 and the camp stays)
    if (id == -1 && cr3Camp.stage == 1 && vs.screen != 0 && !cr3CampEvent && !game.flag(42)) {
        bool all = true;
        const std::vector<int> members = cr3PartyMembers();
        for (size_t k = 0; k < members.size() && k < 3; ++k) all = all && game.wornGene(members[k]) >= 0;
        if (!all) {
            campTopTold = menuAnnounce(0x307), campTopToldAt = uiClock;
            audio.playSe("com_004", false);
            SDL_Log("camp: a member without a gene - 0x307");
            return;
        }
        game.setFlag(42, true), game.setFlag(3505, true);
        SDL_Log("camp: every member wears a gene - flags 42, 3505");
    }
    if (id == -1) {
        audio.playSe("com_004", false);
        cr3Camp.stage = cr3Camp.stage == 3 ? 2 : cr3Camp.stage == 2 ? 1 : 0, cr3Camp.since = 0;
        if (cr3Camp.stage == 0) SDL_Log("camp: closed (CR3)");
        return;
    }
    if (cr3Camp.stage == 4) return chooseCampItems(id);
    if (cr3Camp.stage == 5) return chooseCampEquip(id);
    if (cr3Camp.stage == 6) return chooseCampGifts(id);
    if (cr3Camp.stage == 7) return chooseCampQuests(id);
    if (cr3Camp.stage == 8) return chooseCampStatus(id);
    if (cr3Camp.stage == 9) return chooseCampOptions(id);
    if (cr3Camp.stage == 1 && id >= 200) {
        // (InterfaceMain::Update 0x1d: by the button's place - 0 Items, 1 Equipment, 2 Gene Swap,
        // 3 Fuse Gifts, 4 Quests, 5 Status, 6 Options)
        // (button 7 - the event mode's Gene Swap, CampTopMenu::Update 0xa38130 state 0: case 7 is
        // CreateGeneCharaListMenu with BanCoinButtonHeaderMenu, CR3-U's header having no coin tap: 2's)
        const int b = id - 200;
        if (b < 0 || b >= 7) return;
        if (campTopStalled) return; // (state 6: GetTapButtonListButtonNumberCamp not polled)
        if (!cr3CampButtonOn(kCampButtons[b])) return (void)audio.playSe("com_005", false);
        audio.playSe("com_003", false);
        cr3Camp.button = b;
        if (b == 0) openCampItems();
        else if (b == 1) openCampEquip();
        else if (b == 3) openCampGifts();
        else if (b == 4) openCampQuests();
        else if (b == 5) openCampStatus();
        else if (b == 6) openCampOptions();
        else if (b == 2) {
            cr3Camp.stage = 2, cr3Camp.since = 0, cr3Camp.row = 0;
            SDL_Log("camp: genes");
        }
        return;
    }
    if (cr3Camp.stage == 2 && id >= 300) {
        const std::vector<int> members = cr3PartyMembers();
        const int k = id - 300;
        if (k < 0 || k >= int(members.size())) return;
        if (k != cr3Camp.row) {
            cr3Camp.row = k;
            audio.playSe("com_002", false);
            return;
        }
        audio.playSe("com_003", false);
        cr3Camp.chara = members[size_t(k)];
        cr3Camp.genes.clear();
        for (size_t g = 0; g < game.cr3Genes.size(); ++g)
            if (!game.cr3Genes[g].stocked) cr3Camp.genes.push_back(int(g));
        const int worn = game.wornGene(cr3Camp.chara);
        const auto it = std::find(cr3Camp.genes.begin(), cr3Camp.genes.end(), worn);
        cr3Camp.focus = it != cr3Camp.genes.end() ? int(it - cr3Camp.genes.begin()) : 0;
        cr3Camp.stage = 3, cr3Camp.since = 0, cr3Camp.geneButton = false, cr3Camp.geneSkill = -1, cr3Camp.geneTake = -1;
        SDL_Log("camp: %d's gene (%zu cards)", cr3Camp.chara, cr3Camp.genes.size());
        return;
    }
    if (cr3Camp.stage == 3 && id >= kTapCard && id < kTapRow) {
        const int g = id - kTapCard;
        if (g < 0 || g >= int(cr3Camp.genes.size())) return;
        if (g != cr3Camp.focus) {
            cr3Camp.focus = g;
            audio.playSe("com_002", false);
            return;
        }
        // (SetEquipGeneCardGeneChangeMenuCamp: worn - taken from whoever wore it)
        cr3::Cr3Gene& gene = game.cr3Genes[size_t(cr3Camp.genes[size_t(g)])];
        const int wearer = game.geneWearer(cr3Camp.genes[size_t(g)]);
        if (vitaButtons() && wearer > 0 && wearer != cr3Camp.chara && cr3Camp.geneTake != g) {
            cr3Camp.geneTake = g, cr3Camp.geneAskFocus = 1;
            audio.playSe("com_003", false);
            SDL_Log("camp: take gene from character %d?", wearer);
            return;
        }
        cr3Camp.geneTake = -1;
        const int uid = game.geneUid(gene);
        for (auto it = game.cr3GeneOf.begin(); it != game.cr3GeneOf.end();)
            it = it->second == uid ? game.cr3GeneOf.erase(it) : std::next(it);
        game.cr3GeneOf[cr3Camp.chara] = uid;
        if (vs.screen == 0 && cr3Camp.chara == 1) game.setFlag(22, true); // (FutterMenu::Update: the hero's gene swapped - the story waits on it)
        audio.playSe("com_003", false);
        SDL_Log("camp: %d wears gene %d", cr3Camp.chara, gene.master);
        cr3Camp.stage = 2, cr3Camp.since = 0;
    }
}

bool Viewer::cr3CampClick(float fx, float fy) {
    if (cr3Camp.stage == 0 && (!cr3FieldIdle() || cr3CampTaps.empty())) return false;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    for (auto it = cr3CampTaps.rbegin(); it != cr3CampTaps.rend(); ++it)
        if (ux >= std::min(it->x0, it->x1) && ux <= std::max(it->x0, it->x1) && uy >= std::min(it->y0, it->y1) && uy <= std::max(it->y0, it->y1)) {
            chooseCr3Camp(it->id);
            return true;
        }
    return cr3Camp.stage != 0; // (the camp holds the screen; the field's taps go on)
}

void Viewer::cr3CampPadHeld(bool held) {
    // (a panel taken up and put down - CampTopMenu 3 -> 4 -> 0: state 6's buttons polled again)
    if (cr3Camp.confirmHeld && !held && cr3Camp.stage == 1 && cr3Camp.panel >= 0 && !cr3PanelLocked(cr3Camp.panel) && campTopStalled) {
        campTopStalled = false;
        SDL_Log("camp: a panel put down - the buttons taken again");
    }
    cr3Camp.confirmHeld = held;
}

bool Viewer::campGeneShoulder(bool right) {
    (void)right;
    if (!vitaButtons() || cr3Camp.stage != 3 || cr3Camp.geneTake >= 0) return false;
    cr3Camp.geneTab ^= 1;
    cr3Camp.geneButton = cr3Camp.geneTab != 0;
    cr3Camp.geneSkill = cr3Camp.geneButton ? 0 : -1;
    audio.playSe("com_003", false);
    SDL_Log("camp: gene tab %s", cr3Camp.geneTab ? "Skill" : "Gene");
    return true;
}

void Viewer::sortCampGenes() {
    if (!game.cr3Db || cr3Camp.genes.empty()) return;
    const int selected = cr3Camp.genes[size_t(std::clamp(cr3Camp.focus, 0, int(cr3Camp.genes.size()) - 1))];
    const int order = cr3Camp.geneSort;
    std::stable_sort(cr3Camp.genes.begin(), cr3Camp.genes.end(), [&](int a, int b) {
        const auto& ga = game.cr3Genes[size_t(a)];
        const auto& gb = game.cr3Genes[size_t(b)];
        const auto* ia = game.cr3Db->geneInfo(ga.master);
        const auto* ib = game.cr3Db->geneInfo(gb.master);
        // GeneSortStatusListUp: UID, element, level descending / ascending,
        // rarity descending / ascending. Non-UID ties use the master ID.
        if (order == 0) return (ga.uid > 0 ? ga.uid : a) < (gb.uid > 0 ? gb.uid : b);
        const int va = order == 1 ? (ia ? ia->kind : -1) : order < 4 ? game.cr3Db->geneLevel(ga) : ia ? ia->grade : -1;
        const int vb = order == 1 ? (ib ? ib->kind : -1) : order < 4 ? game.cr3Db->geneLevel(gb) : ib ? ib->grade : -1;
        return va == vb ? ga.master < gb.master : order == 2 || order == 4 ? va > vb : va < vb;
    });
    cr3Camp.focus = int(std::find(cr3Camp.genes.begin(), cr3Camp.genes.end(), selected) - cr3Camp.genes.begin());
    SDL_Log("camp: gene sort %d", order);
}

bool Viewer::cr3CampKey(SDL_Keycode key) {
    if (cr3Camp.stage == 0) {
        if (key == SDLK_C && cr3FieldIdle() && game.flag(26)) return openCr3Camp(), true; // (the Camp button's flag: feBtnShop's, 26)
        if (key == SDLK_TAB && cr3FieldIdle()) return chooseCr3Camp(100), true; // (Tab, the controller's triangle: feBtnOpen)
        return false;
    }
    if (cr3Camp.stage == 4) return campItemsKey(key), true;
    if (cr3Camp.stage == 5) return campEquipKey(key), true;
    if (cr3Camp.stage == 6) return campGiftsKey(key), true;
    if (cr3Camp.stage == 7) return campQuestsKey(key), true;
    if (cr3Camp.stage == 8) return campStatusKey(key), true;
    if (cr3Camp.stage == 9) return campOptionsKey(key), true;
    if (cr3Camp.stage == 10) return myPageKey(key), true;
    if (cr3Camp.stage == 3 && geneInspectionKey(key)) return true;
    const bool up = key == SDLK_UP, down = key == SDLK_DOWN, enter = key == SDLK_RETURN || key == SDLK_SPACE;
    if (vitaButtons() && cr3Camp.stage == 3 && cr3Camp.geneTake >= 0) {
        if (key == SDLK_LEFT || key == SDLK_RIGHT || up || down) cr3Camp.geneAskFocus ^= 1, audio.playSe("com_002", false);
        else if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE || (enter && cr3Camp.geneAskFocus)) cr3Camp.geneTake = -1, audio.playSe("com_004", false);
        else if (enter) chooseCr3Camp(kTapCard + cr3Camp.geneTake);
        return true;
    }
    if (cr3Camp.stage == 1 && campTopAsk == 2) return true;
    if (cr3Camp.stage == 1 && campTopAsk == 1) { // (the question's Yes and No - FUN_81118718 state 7: × its hard return first)
        if (key == SDLK_LEFT || key == SDLK_RIGHT || up || down) campTopAskFocus ^= 1, audio.playSe("com_002", false);
        else if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) chooseCr3Camp(-1);
        else if (enter) chooseCr3Camp(campTopAskFocus == 0 ? 503 : 504);
        return true;
    }
    if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) chooseCr3Camp(-1);
    else if (cr3Camp.stage == 1) {
        const int n = int(cr3PartyMembers().size());
        if (vitaButtons() && (key == SDLK_LEFT || key == SDLK_RIGHT)) {
            if (cr3Camp.panel < 0) { if (key == SDLK_LEFT && n) cr3Camp.panel = n - 1; }
            else {
                const int dir = key == SDLK_RIGHT ? 1 : -1;
                int to = cr3Camp.panel + dir;
                // (a locked panel: not taken up - CheckTopArea -, and passed over by the one moved -
                // TouchSimpleMove skips it as a place to swap with)
                if (cr3Camp.confirmHeld)
                    while (to >= 0 && to < n && cr3PanelLocked(to)) to += dir;
                if (to >= 0 && to < n && cr3Camp.confirmHeld && cr3PanelLocked(cr3Camp.panel)) {
                    SDL_Log("camp: party panel %d is locked", cr3Camp.panel);
                } else if (to >= 0 && to < n) {
                    if (cr3Camp.confirmHeld) {
                        int positions[5], count = 0;
                        for (int slot = 0; slot < 5; ++slot) if (game.cr3Party[slot] > 0) positions[count++] = slot;
                        std::swap(game.cr3Party[positions[cr3Camp.panel]], game.cr3Party[positions[to]]);
                        SDL_Log("camp: party order moved %d to %d", cr3Camp.panel, to);
                    }
                    cr3Camp.panel = to;
                } else if (to == n && !cr3Camp.confirmHeld) cr3Camp.panel = -1;
            }
            audio.playSe("com_002", false);
            SDL_Log("camp: party panel %d", cr3Camp.panel);
        } else if (cr3Camp.panel < 0 && (up || down)) cr3Camp.button = (cr3Camp.button + (down ? 1 : 6)) % 7, audio.playSe("com_002", false);
        else if (enter && cr3Camp.panel < 0) chooseCr3Camp(200 + cr3Camp.button);
    } else if (cr3Camp.stage == 2) {
        const int n = int(cr3PartyMembers().size());
        if ((up || down) && n > 0) cr3Camp.row = std::clamp(cr3Camp.row + (down ? 1 : -1), 0, n - 1), audio.playSe("com_002", false);
        else if (enter && n > 0) chooseCr3Camp(300 + cr3Camp.row);
    } else if (cr3Camp.stage == 3) {
        const int n = int(cr3Camp.genes.size());
        if (vitaButtons() && key == SDLK_X) cr3Camp.geneSort = (cr3Camp.geneSort + 1) % 6, sortCampGenes(), audio.playSe("com_002", false);
        else if (vitaButtons() && key == SDLK_LEFT && !cr3Camp.geneButton) cr3Camp.geneButton = true, cr3Camp.geneSkill = -1, audio.playSe("com_002", false);
        else if (vitaButtons() && cr3Camp.geneButton) {
            if (key == SDLK_RIGHT && cr3Camp.geneSkill < 0) cr3Camp.geneSkill = 0;
            else if (key == SDLK_RIGHT && cr3Camp.geneSkill % 2 == 0) ++cr3Camp.geneSkill;
            else if (key == SDLK_RIGHT) cr3Camp.geneButton = false;
            else if (key == SDLK_LEFT) cr3Camp.geneSkill = cr3Camp.geneSkill > 0 && cr3Camp.geneSkill % 2 ? cr3Camp.geneSkill - 1 : -1;
            else if (up || down) cr3Camp.geneSkill = cr3Camp.geneSkill < 0 && down ? 0 : std::clamp(cr3Camp.geneSkill + (down ? 2 : -2), -1, 7);
            else if (enter && cr3Camp.geneSkill < 0 && n > 0) {
                auto& gene = game.cr3Genes[size_t(cr3Camp.genes[size_t(cr3Camp.focus)])];
                gene.locked = !gene.locked;
                audio.playSe("com_003", false);
                SDL_Log("camp: gene %d %s", gene.master, gene.locked ? "locked" : "unlocked");
            } else if (enter && cr3Camp.geneSkill >= 0 && n > 0) {
                const int g = cr3Camp.genes[size_t(cr3Camp.focus)];
                inspectGene(game.cr3Genes[size_t(g)].master, g);
            }
            SDL_Log("camp: gene skill focus %d", cr3Camp.geneSkill);
        } else if ((up || down) && n > 0) chooseCr3Camp(kTapCard + std::clamp(cr3Camp.focus + (down ? 1 : -1), 0, n - 1));
        else if (enter && n > 0) chooseCr3Camp(kTapCard + cr3Camp.focus);
    }
    return true;
}

// an item's row in item_para (by its full id's hundred-thousand: 0 Item, 1 Weapon, 2 Armor, 3
// Accessory, 4 Cheet, 5 Event, 6 Gift) and its name and help (item_name / item_help's tables)
bool Viewer::cr3ItemRow(int id, cr3::MbeFile::Row& r) {
    static const char* const kTables[7] = {"Item", "Weapon", "Armor", "Accessory", "Cheet", "Event", "Gift"};
    const int group = id / 100000;
    return group >= 0 && group < 7 && cr3TablesReady() && cr3BattleTables.item.find(kTables[group], id % 100000, "iiii", r);
}

// an equipment's help (InterfaceMain::SetEquipHelp, without an attachment - every list's line
// for a weapon, armour or accessory): a weapon "Weight: n/Power: n/Hits: n" (0x268 .. 0x26a), its
// magic and elements' ATK up / down (0x278 / 0x27f, 0x279 + e / 0x280 + e - Weapon +0x4c, +0x54
// ..) and "/Phy skill MP cost: xa.bb" (0x2a3: +0x44 / 100); armour "Weight: n" (0x288 - +0x40),
// its physical, magic and elements' resist up / down (0x289 / 0x291, 0x28a / 0x292, 0x28b + e /
// 0x293 + e - Armor +0x44 ..), "/No burning" / "/No freezing" (0x299 / 0x29a: kinds 45 / 46 in
// its +0x70 list); an accessory its HP, attack, defence, speed, luck that aren't 0 (0x29b ..
// 0x29f, "+ n" / "− n" 0x2a1 / 0x2a2, "/" 0x2a0 between); a number below 0 as "− n"
std::string Viewer::cr3EquipHelp(int full) {
    const int group = full / 100000, id = full % 100000;
    if (group < 1 || group > 3 || !cr3TablesReady()) return {};
    const auto sub = [&](int text, const char* key, const std::string& v) {
        std::string t = menuAnnounce(text);
        if (const size_t at = t.find(key); at != std::string::npos) t.replace(at, std::strlen(key), v);
        return t;
    };
    const auto signedNumber = [&](int v) { return v < 0 ? sub(0x2a2, "{paramerter}", cr3Number(-v)) : cr3Number(v); };
    cr3::MbeFile::Row r;
    std::string out;
    if (group == 1) {
        if (!cr3BattleTables.item.find("Weapon", id, "iiiiiiiiiiiiiiiiiiiiiiiiiii", r)) return {};
        out = sub(0x268, "{weightpara}", signedNumber(r.i(13))) + sub(0x269, "{powerpara}", signedNumber(r.i(14))) + sub(0x26a, "{hitpara}", signedNumber(r.i(15)));
        for (int e = -1; e < 6; ++e) { // (-1 magic, +0x4c; 0 .. 5 the elements 1 .. 6, +0x54 ..)
            const int v = e < 0 ? r.i(19) : r.i(21 + size_t(e));
            if (v != 0) out += sub(e < 0 ? (v > 0 ? 0x278 : 0x27f) : (v > 0 ? 0x279 : 0x280) + e, "{elementpercent}", cr3Number(std::abs(v)));
        }
        const int c = r.i(17);
        std::string mp = sub(0x2a3, "{parama}", cr3Number(c / 100));
        if (const size_t at = mp.find("{paramb}"); at != std::string::npos) mp.replace(at, 8, cr3Number(c % 100, 2));
        return out + mp;
    }
    if (group == 2) {
        if (!cr3BattleTables.item.find("Armor", id, "iiiiiiiiiiiii_piiiiiiiiiii_p", r)) return {};
        out = sub(0x288, "{weightpara}", signedNumber(r.i(14)));
        for (int e = -2; e < 6; ++e) { // (-2 physical +0x44, -1 magic +0x48, 0 .. 5 the elements 1 .. 6 +0x50 ..)
            const int v = e == -2 ? r.i(15) : e == -1 ? r.i(16) : r.i(18 + size_t(e));
            if (v == 0) continue;
            const int up = e == -2 ? 0x289 : e == -1 ? 0x28a : 0x28b + e, down = e == -2 ? 0x291 : e == -1 ? 0x292 : 0x293 + e;
            out += sub(v > 0 ? up : down, "{elementpercent}", cr3Number(std::abs(v)));
        }
        bool burn = false, freeze = false; // (MbArmorInfo::IsValidStatusEffect: in its list, kept off)
        for (int k = 0; k < r.i(24); ++k) burn |= r.element(25, size_t(k)) == 45, freeze |= r.element(25, size_t(k)) == 46;
        if (burn) out += menuAnnounce(0x299);
        if (freeze) out += menuAnnounce(0x29a);
        return out;
    }
    if (!cr3BattleTables.item.find("Accessory", id, "iiiiiiiiiiiiiiiiii", r)) return {};
    const int fields[5] = {12, 14, 15, 16, 17}; // (HP, attack, defence, speed, luck - MP, +0x34, not shown)
    for (int k = 0; k < 5; ++k) {
        const int v = r.i(size_t(fields[k]));
        if (v == 0) continue;
        if (!out.empty()) out += menuAnnounce(0x2a0);
        out += menuAnnounce(0x29b + k) + sub(v > 0 ? 0x2a1 : 0x2a2, "{paramerter}", cr3Number(std::abs(v)));
    }
    return out;
}

std::string Viewer::cr3ItemText(int id, bool help) {
    static const char* const kNames[7] = {"ItemName", "WeaponName", "ArmorName", "AccessoryName", "CheetName", "EventName", "GiftName"};
    static const char* const kHelps[7] = {"ItemHelp", "WeaponHelp", "ArmorHelp", "AccessoryHelp", "CheetHelp", "EventHelp", "GiftHelp"};
    const int group = id / 100000;
    if (group < 0 || group >= 7) return {};
    if (help && group >= 1 && group <= 3) return cr3EquipHelp(id); // (GetItemHelp: SetEquipHelp's)
    static std::map<std::string, cr3::MbeFile> files;
    static int readIn = -1; // (the language it was read in: textGeneration)
    if (readIn != textGeneration) files = std::map<std::string, cr3::MbeFile>(), readIn = textGeneration;
    const char* file = help ? "item_help.mbe" : "item_name.mbe";
    cr3::MbeFile& f = files[file];
    if (f.tables().empty()) {
        std::vector<uint8_t> d;
        if (archive.read(std::string("data\\") + file, d)) f.parse(d);
    }
    cr3::MbeFile::Row r;
    if (!f.find(help ? kHelps[group] : kNames[group], id % 100000, "ipp", r)) return {};
    std::string s = optionLanguage == 0 || r.s(2).empty() ? r.s(1) : r.s(2);
    if (s.empty()) s = r.s(2);
    for (size_t p; (p = s.find('\r')) != std::string::npos;) s.erase(p, 1);
    return s;
}

// an Item's field effect k (field_item.mbe's FieldItem: the id, then four (kind, value))
std::pair<int, int> Viewer::cr3FieldEffect(int id, int k) {
    static cr3::MbeFile f;
    static int readIn = -1; // (the language it was read in: textGeneration)
    if (readIn != textGeneration) f = cr3::MbeFile(), readIn = textGeneration;
    if (f.tables().empty()) {
        std::vector<uint8_t> d;
        if (archive.read("data\\field_item.mbe", d)) f.parse(d);
    }
    cr3::MbeFile::Row r;
    if (id <= 0 || id >= 100000 || !f.find("FieldItem", id, "iiiiiiiii", r)) return {0, 0};
    return {r.i(size_t(1 + 2 * k)), r.i(size_t(2 + 2 * k))};
}

bool Viewer::cr3ItemInCategory(int id, int category) {
    const int group = id / 100000;
    cr3::MbeFile::Row r;
    const int type = group == 0 && cr3ItemRow(id, r) ? r.i(2) : -1;
    switch (category) {
    case 0: return group != 4 || cr3ItemRow(id, r);
    case 1: return group == 0 && type == 0;
    case 2: return group == 6;
    case 3: return group == 0 && type == 0x12;
    case 4: return group == 0 && type == 1;
    case 5: return group == 0 && type == 0x13;
    case 6: return group == 0 && type == 0x14;
    case 7: return group == 5;
    case 8: return group == 1;
    case 9: return group == 2;
    case 10: return group == 3;
    }
    return false;
}

// what can be chosen: a gift, an Item with a field effect
bool Viewer::cr3ItemUsable(int id) { return id / 100000 == 6 || (id < 100000 && cr3FieldEffect(id).first > 0); }

void Viewer::campItemRows() {
    campItems.rows.clear();
    for (const auto& [id, n] : game.cr3Items)
        if (id > 0 && n > 0 && cr3ItemInCategory(id, campItems.category)) campItems.rows.push_back({id, n});
    campItems.row = std::clamp(campItems.row, 0, std::max(int(campItems.rows.size()) - 1, 0));
}

// who wears a weapon / armour / accessory by its full id (0 none)
int Viewer::cr3ItemWearer(int id) {
    const std::map<int, int>* worn = id / 100000 == 1 ? &game.cr3Weapon : id / 100000 == 2 ? &game.cr3Armour : id / 100000 == 3 ? &game.cr3Accessory : nullptr;
    if (worn)
        for (const auto& [ch, item] : *worn)
            if (item == id % 100000) return ch;
    return 0;
}

// the system window asked (Yes / No - `yes` / `no` the taps', Yes shown dimmed and untappable
// unless `yesOn`) or told (`told` a tap anywhere's): the lab's, drawn for the camp
void Viewer::drawCr3Asked(const std::string& text, float since, int focus, bool yesOn, int yes, int no, int w, int h, const Mat4& view,
                  std::vector<Cr3GeneLabTap>* out) {
    const int screen = geneMenu.screen, labFocus = geneLabFocus;
    const float labSince = geneMenu.since;
    std::vector<Cr3GeneLabTap> keep;
    keep.swap(geneLabTaps);
    geneMenu.screen = 4, geneMenu.since = since, geneMenu.message = text, geneLabFocus = focus;
    for (const char* part : {"combtnpanel", "combtnbg_fr00", "combtnicon_fr00", "combtnr", "combtntext_fr00", "combtntexttap_fr00"}) {
        UiFig& u = uiFig(part);
        u.fig.tint[0] = u.fig.tint[1] = u.fig.tint[2] = 1.0f;
    }
    drawGeneAsk(w, h, view);
    geneMenu.screen = screen, geneMenu.since = labSince, geneMenu.message.clear(), geneLabFocus = labFocus;
    for (const Cr3GeneLabTap& tp : geneLabTaps)
        if (tp.id == kTapNo || (tp.id == kTapYes && yesOn)) (out ? *out : cr3CampTaps).push_back({tp.x0, tp.y0, tp.x1, tp.y1, tp.id == kTapYes ? yes : no});
    geneLabTaps.swap(keep);
}

void Viewer::drawCr3Told(const std::string& text, float since, int told, int w, int h, const Mat4& view, std::vector<Cr3GeneLabTap>* out) {
    const std::string alert = geneMenu.alert;
    const float at = geneMenu.alertSince;
    std::vector<Cr3GeneLabTap> keep;
    keep.swap(geneLabTaps);
    geneMenu.alert = text, geneMenu.alertSince = since;
    drawGeneAlert(w, h, view);
    geneMenu.alert = alert, geneMenu.alertSince = at;
    (out ? *out : cr3CampTaps).push_back({-6, -4, 6, 4, told});
    geneLabTaps.swap(keep);
}

void Viewer::campItemsTell(const std::string& text) {
    campItems.told = text, campItems.toldSince = 0;
    SDL_Log("camp: items - told");
}

void Viewer::openCampItems() {
    cr3Camp.stage = 4, cr3Camp.since = 0;
    campItems = Cr3CampItems();
    campItemRows();
    SDL_Log("camp: items (%zu)", campItems.rows.size());
}

void Viewer::drawCampItems(int w, int h, const Mat4& view) {
    const float t = cr3Camp.since;
    float x, y, cx, cy;
    const Cr3CampItems& ci = campItems;
    std::string sentence;
    if (ci.step == 0 && ci.row < int(ci.rows.size())) sentence = cr3ItemText(ci.rows[size_t(ci.row)].first, true);
    if (ci.step == 2 || ci.step == 3) { // (0x14b: the item, "select the target")
        sentence = menuAnnounce(0x14b);
        if (const size_t at = sentence.find("{itemname}"); at != std::string::npos) sentence.replace(at, 10, cr3ItemText(ci.item, false));
    }
    if (ci.step == 2) {
        drawCr3CampGenes(w, h, view);
        drawCr3Header(t, 1, sentence, w, h, view);
        return;
    }
    if (ci.step == 3) drawCr3CampCharas(w, h, view);
    else {
        UiFig& pos = cr3UiPut("comlistposition", "comlistposition_io", ci.step == 0 ? t : 1.0f, 0, 0, view);
        if (cr3UiCall(pos, "call_comListMenu", x, y, w, h)) {
            UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
            float lx, ly;
            // (the category buttons; the focused one's name over the rows - TextRender)
            float labelX = 0, labelY = -100;
            drawCr3Categories(menu, kItemTabs, 11, ci.category, 400, labelX, labelY, view);
            // (the rows)
            if (cr3UiCall(menu, "call_comListPanel", lx, ly)) {
                const int n = int(ci.rows.size());
                const int top = ci.row - 2; // (comListMenu: the focused row the third, the list past it)
                drawCr3ListBar(menu, false, ci.row, n, 1, view, 0.15f);
                drawCr3ListEnds(lx, ly, n, ci.row, view);
                for (int k = std::max(top, 0); k < n && k <= ci.row + 2; ++k) {
                    const auto [id, count] = ci.rows[size_t(k)];
                    const bool usable = cr3ItemUsable(id) || id / 100000 != 0;
                    const float alpha = usable ? 1.0f : 0.5f;
                    Cr3FocusRow focusRow(*this, k == ci.row);
                    UiFig& panel = cr3UiPut("comlistpanel", "", 0, cr3ListRowX(lx, k, ci.row), cr3ListRowY(ly, k, top, ci.row), view, alpha);
                    if (k == ci.row && cr3UiCall(panel, "call_comListCursor", cx, cy)) cr3UiPut("comlistcursor", "", 0, cx, cy, view);
                    cr3::MbeFile::Row r;
                    if (cr3ItemRow(id, r) && cr3UiCall(panel, "call_comListIcon_fr00", cx, cy))
                        cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), r.i(3)), cx, cy, view, alpha);
                    if (const int wearer = cr3ItemWearer(id); wearer > 0 && cr3UiCall(panel, "call_camEquChr_fr00", cx, cy))
                        cr3UiPut("camequchr_fr00", "", float(wearer - 1) / 30, cx, cy, view);
                    if (cr3UiCall(panel, "call_comListNumI_num00", cx, cy)) {
                        UiFig& num = cr3UiPut("comlistnumi_num00", "", 0, cx, cy, view, alpha);
                        const std::string digits = std::to_string(std::max(count, 0));
                        int slots = 0;
                        for (const std::string& j : num.joints) slots += j.rfind("call_comListNum_fr00_", 0) == 0;
                        for (int d = 0; d < int(digits.size()) && d < slots; ++d) {
                            float dx, dy; // (right-aligned in its slots)
                            if (cr3UiCall(num, "call_comListNum_fr00_00" + std::to_string(slots - int(digits.size()) + d + 1), dx, dy))
                                cr3UiPut("comlistnum_fr00", "", float(digits[size_t(d)] - '0') / 30, dx, dy, view, alpha);
                        }
                    }
                    cr3UiLabel(panel, "typeM_L01", cr3ItemText(id, false), w, h, usable ? 0.3f : 0.6f);
                    float ax, ay, bx, by;
                    if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 300 + k});
                }
            }
            if (labelY > -100) {
                dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                drawUiText(menuAnnounce(kItemTabs[ci.category] + 0x29), 20, labelX / 10, (labelY - 0.1f) / 10, w, h, false, 1.0f);
            }
        }
    }
    if (ci.step == 1) drawCampItemCount(w, h, view);
    if (ci.step == 5) drawCr3Asked(areaEvents ? areaEvents->filterMessage(menuAnnounce(0x2a6)) : menuAnnounce(0x2a6), t, 0, true, 500, 501, w, h, view);
    if (ci.step == 4) {
        std::string text = menuAnnounce(0x1d0);
        if (const size_t at = text.find("{name}"); at != std::string::npos) text.replace(at, 6, cr3ItemText(ci.item, false));
        drawCr3Asked(text, t, ci.countFocus == 3 ? 1 : 0, true, 500, 501, w, h, view);
    }
    if (!ci.told.empty()) drawCr3Told(ci.told, ci.toldSince, 502, w, h, view);
    drawCr3Header(t, 1, sentence, w, h, view);
}

// how many (CampItemMenu: camItemPosition and its io animator - the reels at
// ragdoll_call_camItemReelMenu_fit0, the count held at ..StockPriceMenu_fit4's
// call_camItemStock_num00 - and the system window's question with Yes / No)
void Viewer::drawCampItemCount(int w, int h, const Mat4& view) {
    const float t = cr3Camp.since;
    float x, y, cx, cy;
    UiFig& pos = cr3UiPut("camitemposition", "camitemposition_io", t, 0, 0, view);
    if (cr3UiCall(pos, "ragdoll_call_camItemReelMenu", x, y, w, h)) {
        UiFig& reel = uiFig("camitemreelmenu");
        reel.fig.place = cr3::Mat34(); // (posed first: its animation's UVs under the reels')
        reel.fig.place.m[3] = x, reel.fig.place.m[7] = y;
        poseUi(reel, "camitemreelmenu", 0);
        reel.fig.tint[3] = 1;
        const int digits[2] = {campItems.count / 10 % 10, campItems.count % 10};
        for (int c = 0; c < 2; ++c) {
            float v = 0.14f * float(2 - digits[c]); // (NumberScroll: the strip's V, -0.7 .. 0.7)
            while (v > 0.7f) v -= 1.4f;
            while (v <= -0.7f) v += 1.4f;
            const std::string a = "mat_num_0" + std::to_string(1 + 2 * c), b = "mat_num_0" + std::to_string(2 + 2 * c);
            for (ViewMesh& vm : reel.fig.meshes) {
                const uint32_t mi = vm.source.material;
                if (mi >= reel.materials.size()) continue;
                if (reel.materials[mi] == a) vm.material.params[0x5e] = {0.0f, v};
                if (reel.materials[mi] == b) vm.material.params[0x5e] = {0.13f, v < 0 ? v + 0.7f : v - 0.7f};
                if (reel.materials[mi] == a || reel.materials[mi] == b)
                    vm.material.params[0x33] = vitaButtons() && campItems.countFocus == c ? std::vector<float>{0.5f, 1, 1.5f, 1} : std::vector<float>{1, 1, 1, 1};
            }
        }
        drawFigure(reel.fig, view.data());
        for (int c = 0; c < 2; ++c) { // (a reel's upper half up, its lower down)
            float ax, ay, bx, by;
            if (!cr3UiCall(reel, "tapAreaS0" + std::to_string(c + 1), ax, ay) || !cr3UiCall(reel, "tapAreaE0" + std::to_string(c + 1), bx, by)) continue;
            const float mid = (ay + by) / 2;
            cr3CampTaps.push_back({ax, ay, bx, mid, 600 + 2 * c});
            cr3CampTaps.push_back({ax, mid, bx, by, 601 + 2 * c});
        }
    }
    if (cr3UiCall(pos, "ragdoll_call_camItemStockPriceMenu", x, y, w, h)) {
        UiFig& sp = cr3UiPut("camitemstockpricemenu", "", 0, x, y, view);
        if (cr3UiCall(sp, "call_camItemStock_num00", cx, cy)) {
            UiFig& stock = cr3UiPut("camitemstock_num00", "", 0, cx, cy, view);
            cr3UiDigits(stock, "call_camItemNum_fr00", "camitemnum_fr00", campItems.held, 3, 0, view);
        }
    }
    drawCr3Asked(menuAnnounce(0xdc), t, campItems.countFocus == 3 ? 1 : 0, campItems.count >= 1 && campItems.count <= campItems.held, 500, 501, w, h, view);
}

bool Viewer::campItemCanTargetGene(const cr3::Cr3Gene& gene) const {
    const int kind = campItems.kind;
    if (kind >= 1 && kind <= 4) {
        const int most = game.cr3Db ? game.cr3Db->constant("GENE_ADD_MAX", 40) : 40;
        return gene.adds[0] + gene.adds[1] + gene.adds[2] + gene.adds[3] < most;
    }
    if (kind == 5) {
        const auto* gi = game.cr3Db ? game.cr3Db->geneInfo(gene.master) : nullptr;
        return gi && game.cr3Db->geneLevel(gene) < gi->maxLevel;
    }
    if (kind == 7 || kind == 8) {
        const int type = kind == 7 ? 0 : 1;
        bool hasEmpty = false;
        for (int k = 0; k < 8; ++k) {
            if (gene.slots[k][1] < 0) hasEmpty = true;
            else if (gene.slots[k][0] == type && gene.slots[k][1] == campItems.amount) return false;
        }
        return hasEmpty;
    }
    return true;
}

// the item used on the gene `g` (a cr3Genes index) / the character `ch`
void Viewer::campItemOnGene(int g) {
    if (!game.cr3Db || g < 0 || size_t(g) >= game.cr3Genes.size()) return;
    cr3::Cr3Gene& gene = game.cr3Genes[size_t(g)];
    if (!campItemCanTargetGene(gene)) return (void)audio.playSe("com_005", false);
    CampItemsUsed used(*this);
    const int kind = campItems.kind, n = campItems.count;
    if (kind >= 1 && kind <= 4) game.cr3Db->enforceGene(game, g, 9, campItems.item, n);
    else if (kind == 5) { // (the levels' EXP, what they teach)
        cr3::Cr3Gene after = gene;
        game.cr3Db->addGeneLevels(after, campItems.amount * n);
        game.cr3Db->enforceGene(game, g, 6, campItems.item, n, after.exp, after.slots);
    } else if (kind == 7 || kind == 8) { // (a command / skill: first empty slot 0..7)
        int slots[8][2];
        int at = -1;
        for (int k = 0; k < 8; ++k) {
            slots[k][0] = gene.slots[k][0], slots[k][1] = gene.slots[k][1];
            if (at < 0 && slots[k][1] < 0) at = k;
        }
        if (at >= 0) {
            slots[at][0] = kind == 7 ? 0 : 1, slots[at][1] = campItems.amount;
            game.cr3Db->enforceGene(game, g, 4, campItems.item, 1, 0, slots);
        }
    }
    SDL_Log("camp: item %d x%d on gene %d (kind %d)", campItems.item, n, gene.master, kind);
}

void Viewer::campItemOnChara(int ch) {
    if (!game.cr3Db) return;
    cr3::Cr3Fighter cf;
    const int most = cr3CharaStats(ch, cf) ? cf.maxMp : 0;
    const int now = cr3CharaMp(ch, most);
    if (now >= most) return (void)audio.playSe("com_005", false); // (CrxUtilAddMPOverMAX: block full-MP target)
    CampItemsUsed used(*this);
    const int mp = std::min(now + campItems.amount * campItems.count, most);
    if (mp >= most) game.cr3Mp.erase(ch);
    else game.cr3Mp[ch] = mp;
    game.cr3Db->useItem(game, campItems.item, campItems.count);
    SDL_Log("camp: item %d x%d on %d - MP %d / %d", campItems.item, campItems.count, ch, mp, most);
}

void Viewer::chooseCampItems(int id) {
    Cr3CampItems& ci = campItems;
    if (!ci.told.empty()) {
        if (id == 502 || id == -1) ci.told.clear();
        return;
    }
    if (id == -1) {
        audio.playSe("com_004", false);
        if (ci.step == 0) cr3Camp.stage = 1, cr3Camp.since = 0;
        else ci.step = 0, cr3Camp.stage = 4, cr3Camp.since = 1;
        return;
    }
    if (ci.step == 0 && id >= 400 && id < 411) {
        if (id - 400 == ci.category) return;
        ci.category = id - 400, ci.row = 0;
        audio.playSe("com_002", false);
        campItemRows();
        return;
    }
    if (ci.step == 0 && id >= 300 && id < 400) {
        const int k = id - 300;
        if (k >= int(ci.rows.size())) return;
        if (k != ci.row) {
            ci.row = k;
            audio.playSe("com_002", false);
            return;
        }
        const auto [item, n] = ci.rows[size_t(k)];
        if (item == 1 && ci.category < 2) { // (CampItemMain 1: the Microcommunicator, from New or Field - asked 0x2a6)
            audio.playSe("com_003", false);
            ci.item = item, ci.step = 5, cr3Camp.since = 0;
            return;
        }
        if (!cr3ItemUsable(item)) return (void)audio.playSe("com_005", false);
        audio.playSe("com_003", false);
        ci.item = item, ci.held = n, ci.count = 1, ci.countFocus = 0;
        std::tie(ci.kind, ci.amount) = cr3FieldEffect(item);
        if (item / 100000 == 6) ci.step = 4;
        else if (ci.kind == 7 || ci.kind == 8) campItemsTarget();
        else ci.step = 1;
        cr3Camp.since = 0;
        SDL_Log("camp: item %d chosen (kind %d, value %d)", item, ci.kind, ci.amount);
        return;
    }
    if (ci.step == 1) {
        if (id >= 600 && id < 604) { // (a reel turned)
            const int step = id < 602 ? 10 : 1, up = id % 2 == 0 ? 1 : -1;
            int tens = ci.count / 10, ones = ci.count % 10;
            (step == 10 ? tens : ones) = ((step == 10 ? tens : ones) + up + 10) % 10;
            ci.count = tens * 10 + ones;
            audio.playSe("com_002", false);
            SDL_Log("camp: item count %d", ci.count);
        } else if (id == 501) {
            audio.playSe("com_004", false);
            ci.step = 0, cr3Camp.since = 1;
            SDL_Log("camp: item count cancelled");
        } else if (id == 500 && ci.count >= 1 && ci.count <= ci.held) {
            audio.playSe("com_003", false);
            campItemsTarget();
        }
        return;
    }
    if (ci.step == 5 && (id == 500 || id == 501)) {
        // (yes: CampItemMain 6 - faded out, the camp left with 0x21: Fld2Main::SM_Camp 6, the area
        // script's DungeonExit - home to the town -, not the camp's evt001)
        audio.playSe(id == 500 ? "com_003" : "com_004", false);
        ci.step = 0;
        if (id == 501) return;
        cr3Camp.stage = 0, cr3CampWasOpen = false;
        SDL_Log("camp: the Microcommunicator - %s's DungeonExit", area.name.c_str());
        runAreaFunction("DungeonExit");
        return;
    }
    if (ci.step == 4 && (id == 500 || id == 501)) {
        if (id == 501) {
            audio.playSe("com_004", false);
            ci.step = 0;
            return;
        }
        const std::map<int, int> before = game.cr3Items;
        const bool specialGift = ci.item == 600065 || ci.item == 600067 || ci.item == 600068;
        bool ok = game.cr3Db && game.cr3Db->itemizeGift(game, ci.item) == cr3::kDbOk;
        if (!ok && specialGift && game.cr3Items[ci.item] > 0) {
            game.cr3Items[ci.item] = std::max(game.cr3Items[ci.item] - 1, 0);
            ok = true;
        }
        if (ok) {
            audio.playSe("com_034", false); // (CreateGiftItemizeEffect)
            // (DBSystem::SM_ItemizeGift / CampItemMain::Update @ 0x00a2cb18: special gifts & flags)
            if (ci.item == 600022) game.setFlag(28, true);
            if (ci.item >= 600001 && ci.item <= 600004) game.setFlag(5120 + (ci.item - 600000), true);
            if (ci.item == 600065) {
                game.tabButtons |= 2u; // (Fld2AddTabMenu(1))
                game.setFlag(4106, true);
                campItemsTell(menuAnnounce(0x256));
            } else if (ci.item == 600067) {
                game.goodsButtons.insert(1); // (Fld2AddGoodsButton(1))
                game.setFlag(4102, true);
                campItemsTell(menuAnnounce(0x257));
            } else if (ci.item == 600068) {
                game.goodsButtons.insert(3); // (Fld2AddGoodsButton(3))
                game.setFlag(4104, true);
                campItemsTell(menuAnnounce(0x258));
            } else {
                std::string got;
                for (const auto& [item, n] : game.cr3Items)
                    if (item != ci.item && n > (before.count(item) ? before.at(item) : 0)) got = cr3ItemText(item, false);
                if (got.empty()) got = cr3ItemText(ci.item, false);
                std::string text = menuAnnounce(0xfb);
                if (const size_t at = text.find("%s"); at != std::string::npos) text.replace(at, 2, got);
                campItemsTell(text);
            }
            SDL_Log("camp: gift %d itemised", ci.item);
        }
        ci.step = 0;
        campItemRows();
        return;
    }
    if (ci.step == 2 && id >= kTapCard && id < kTapRow) {
        const int g = id - kTapCard;
        if (g != cr3Camp.focus) {
            cr3Camp.focus = g;
            audio.playSe("com_002", false);
            return;
        }
        campItemOnGene(cr3Camp.genes[size_t(g)]);
        return;
    }
    if (ci.step == 3 && id >= 300 && id < 400) {
        const std::vector<int> members = cr3PartyMembers();
        const int k = id - 300;
        if (k >= int(members.size())) return;
        if (k != cr3Camp.row) {
            cr3Camp.row = k;
            audio.playSe("com_002", false);
            return;
        }
        campItemOnChara(members[size_t(k)]);
    }
}

// the item's target: a gene (the held cards) or a character (6: MP - or party-wide @ 0x00886a38)
void Viewer::campItemsTarget() {
    Cr3CampItems& ci = campItems;
    cr3Camp.since = 0;
    if (ci.kind == 6) {
        cr3::MbeFile::Row ir;
        cr3::Cr3Command bc;
        const bool partyWide = cr3TablesReady() && cr3BattleTables.item.find("Item", ci.item % 100000, "iiiiiiiiiiiii", ir) &&
                               ir.i(12) >= 0 && cr3Rules.command(ir.i(12), bc) &&
                               bc.target >= 0 && bc.target <= 11 && ((1u << bc.target) & 0xa24u) != 0;
        if (partyWide) {
            const std::vector<int> members = cr3PartyMembers();
            bool anyNeed = false;
            for (int ch : members) {
                cr3::Cr3Fighter cf;
                const int most = cr3CharaStats(ch, cf) ? cf.maxMp : 0;
                if (cr3CharaMp(ch, most) < most) anyNeed = true;
            }
            if (!anyNeed) return (void)audio.playSe("com_005", false);
            CampItemsUsed used(*this);
            for (int ch : members) {
                cr3::Cr3Fighter cf;
                const int most = cr3CharaStats(ch, cf) ? cf.maxMp : 0;
                const int mp = std::min(cr3CharaMp(ch, most) + ci.amount * ci.count, most);
                if (mp >= most) game.cr3Mp.erase(ch);
                else game.cr3Mp[ch] = mp;
            }
            if (game.cr3Db) game.cr3Db->useItem(game, ci.item, ci.count);
            SDL_Log("camp: party MP item %d x%d used", ci.item, ci.count);
            return;
        }
        ci.step = 3, cr3Camp.row = 0;
        return;
    }
    ci.step = 2, cr3Camp.genes.clear(), cr3Camp.focus = 0;
    const int teachType = ci.kind == 7 ? 0 : ci.kind == 8 ? 1 : -1;
    for (size_t g = 0; g < game.cr3Genes.size(); ++g) {
        const cr3::Cr3Gene& gene = game.cr3Genes[g];
        if (gene.stocked) continue;
        if (teachType >= 0) { // (SetGeneChangeGeneListItemCList / SList: exclude genes that already know it)
            bool known = false;
            for (int s = 0; s < 8; ++s)
                if (gene.slots[s][1] >= 0 && gene.slots[s][0] == teachType && gene.slots[s][1] == ci.amount) known = true;
            if (known) continue;
        }
        cr3Camp.genes.push_back(int(g));
    }
}

void Viewer::campItemsKey(SDL_Keycode key) {
    Cr3CampItems& ci = campItems;
    const bool up = key == SDLK_UP, down = key == SDLK_DOWN, left = key == SDLK_LEFT, right = key == SDLK_RIGHT;
    const bool enter = key == SDLK_RETURN || key == SDLK_SPACE;
    if (!ci.told.empty()) return enter || key == SDLK_ESCAPE ? chooseCampItems(502) : void();
    if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) return chooseCampItems(-1);
    if (ci.step == 0) {
        const int n = int(ci.rows.size());
        if (left || right) chooseCampItems(400 + (ci.category + (right ? 1 : 10)) % 11);
        else if ((up || down) && n > 0) chooseCampItems(300 + std::clamp(ci.row + (down ? 1 : -1), 0, n - 1));
        else if (enter && n > 0) chooseCampItems(300 + ci.row);
    } else if (ci.step == 1) {
        if (vitaButtons() && (left || right)) {
            ci.countFocus = std::clamp(ci.countFocus + (right ? 1 : -1), 0, 3);
            audio.playSe("com_002", false);
            SDL_Log("camp: item count focus %d", ci.countFocus);
        } else if (vitaButtons() && (up || down)) {
            if (ci.countFocus < 2) chooseCampItems(600 + ci.countFocus * 2 + (down ? 1 : 0));
            else ci.countFocus = ci.countFocus == 2 ? 3 : 2, audio.playSe("com_002", false);
        } else if (up || down) chooseCampItems(down ? 603 : 602);
        else if (left || right) chooseCampItems(left ? 601 : 600);
        else if (enter && vitaButtons()) {
            if (ci.countFocus >= 2) chooseCampItems(ci.countFocus == 2 ? 500 : 501);
        } else if (enter || key == SDLK_Y) chooseCampItems(500);
        else if (key == SDLK_N) chooseCampItems(501);
    } else if (ci.step == 4 || ci.step == 5) {
        if (vitaButtons() && (up || down || left || right)) ci.countFocus = ci.countFocus == 3 ? 2 : 3, audio.playSe("com_002", false);
        else if (enter) chooseCampItems(ci.countFocus == 3 ? 501 : 500);
        else if (key == SDLK_Y) chooseCampItems(500);
        else if (key == SDLK_N) chooseCampItems(501);
    } else if (ci.step == 2) {
        const int n = int(cr3Camp.genes.size());
        if ((up || down || left || right) && n > 0) chooseCampItems(kTapCard + std::clamp(cr3Camp.focus + (down || right ? 1 : -1), 0, n - 1));
        else if (enter && n > 0) chooseCampItems(kTapCard + cr3Camp.focus);
    } else if (ci.step == 3) {
        const int n = int(cr3PartyMembers().size());
        if ((up || down) && n > 0) cr3Camp.row = std::clamp(cr3Camp.row + (down ? 1 : -1), 0, n - 1), audio.playSe("com_002", false);
        else if (enter && n > 0) chooseCampItems(300 + cr3Camp.row);
    }
}

std::map<int, int>& Viewer::cr3Worn(int slot) { return slot == 0 ? game.cr3Weapon : slot == 1 ? game.cr3Armour : game.cr3Accessory; }

// a character's worn item's full id in a slot (0 none; where none put on, the starting kit's -
// char_para's +0x10 .. +0x18, j001's armour 83 - as Cr3BattleRules::playerStats wears it)
int Viewer::cr3WornItem(int ch, int slot) {
    const std::map<int, int>& worn = cr3Worn(slot);
    const auto it = worn.find(ch);
    int id = it != worn.end() ? it->second : 0;
    cr3::MbeFile::Row r;
    if (it == worn.end() && cr3TablesReady())
        id = slot == 1 ? cr3BattleTables.defaultArmour(ch) : cr3BattleTables.character.find("Char", ch, "iiiiiii", r) ? r.i(slot == 0 ? 4 : 6) : 0;
    return id > 0 ? (slot + 1) * 100000 + id : 0;
}

bool Viewer::cr3CanWear(int ch, int full) {
    cr3::MbeFile::Row r;
    static const char* const kTables[3] = {"Weapon", "Armor", "Accessory"};
    const int slot = full / 100000 - 1;
    if (slot < 0 || slot > 2 || !cr3TablesReady() || !cr3BattleTables.item.find(kTables[slot], full % 100000, "iiiiiiiiiiii", r)) return false;
    return ch > 0 && ch < 31 && (r.i(11) >> ch & 1) != 0;
}

void Viewer::campEquipRows() {
    Cr3CampEquip& ce = campEquip;
    ce.rows.clear();
    const int group = ce.slot + 1, worn = cr3WornItem(ce.chara, ce.slot);
    std::set<int> ids;
    for (const auto& [id, n] : game.cr3Items)
        if (id / 100000 == group && n > 0) ids.insert(id);
    for (const int ch : cr3PartyMembers())
        if (const int w = cr3WornItem(ch, ce.slot)) ids.insert(w);
    for (const int id : ids)
        if (cr3CanWear(ce.chara, id) || id == worn) ce.rows.push_back(id);
    if (ce.slot == 2) ce.rows.push_back(0); // (none)
    const auto it = std::find(ce.rows.begin(), ce.rows.end(), worn);
    ce.row = it != ce.rows.end() ? int(it - ce.rows.begin()) : 0;
    std::string listed;
    for (int id : ce.rows) listed += " " + std::to_string(id);
    SDL_Log("camp: character %d's slot %d:%s", ce.chara, ce.slot, listed.c_str());
}

void Viewer::openCampEquip() {
    const std::vector<int> members = cr3PartyMembers();
    cr3Camp.stage = 5, cr3Camp.since = 0;
    campEquip = Cr3CampEquip();
    campEquip.chara = members.empty() ? 0 : members[0];
    SDL_Log("camp: equipment (%d)", campEquip.chara);
}

// the item worn (SetEquipPartsUID): one only others wear taken, each given the old one
void Viewer::campEquipWear(int full) {
    Cr3CampEquip& ce = campEquip;
    std::map<int, int>& worn = cr3Worn(ce.slot);
    const int old = cr3WornItem(ce.chara, ce.slot);
    if (full == old) return;
    if (full > 0) {
        int wearers = 0, other = 0;
        for (const int ch : cr3PartyMembers())
            if (ch != ce.chara && cr3WornItem(ch, ce.slot) == full) ++wearers, other = ch;
        const auto held = game.cr3Items.find(full);
        if (other > 0 && (held == game.cr3Items.end() ? 0 : held->second) <= wearers) {
            if (old > 0) worn[other] = old % 100000;
            else worn.erase(other);
        }
        worn[ce.chara] = full % 100000;
    } else
        worn[ce.chara] = 0; // (none: as Unequip leaves it)
    if (ce.slot == 1) cr3BodiesChanged();
    SDL_Log("camp: %d wears %d (was %d)", ce.chara, full, old);
}

// a party member's numbers with one slot's item changed (0: none) - CrxCharacter's
// SetTemporary*ById; slot -1 as worn
bool Viewer::cr3EquipStats(int ch, int slot, int full, cr3::Cr3Fighter& f) {
    if (!cr3TablesReady()) return false;
    cr3PartyRules();
    if (slot >= 0 && slot < 3) {
        auto it = cr3Rules.partyEquip.find(ch);
        if (it == cr3Rules.partyEquip.end()) it = cr3Rules.partyEquip.emplace(ch, std::array<int, 3>{-1, -1, -1}).first;
        it->second[size_t(slot)] = full > 0 ? full % 100000 : 0;
    }
    return cr3Rules.playerStats(cr3BattleTables, ch, f);
}

// a character's gene's name (a guest's: its GuestGene's gene)
std::string Viewer::cr3GeneNameOf(int ch) {
    const int k = game.wornGene(ch);
    if (k >= 0) return cr3NameOf("gene_name.mbe", "GeneName", game.cr3Genes[size_t(k)].master);
    cr3::MbeFile::Row r, g;
    if (cr3TablesReady() && cr3BattleTables.character.find("Char", ch, "iiiiiiii", r) && (r.i(3) & 1) && r.i(7) > 0 &&
        cr3BattleTables.gene.find("GuestGene", r.i(7), "ii", g))
        return cr3NameOf("gene_name.mbe", "GeneName", g.i(1));
    return std::string();
}

// a change shown (CampEquip2Menu::SetNumberOfUpDown: camEquNumUp_num00's comNum03_fr00 - the
// arrow at frame 20 up / 21 down, the digits at their own frames - + 10 down -, leading zeros
// left out; a percent sign after them, 22 / 23; nothing at 0)
void Viewer::cr3EquipChange(const UiFig& panel, const std::string& joint, int value, int places, bool percent, const Mat4& view) {
    float x, y;
    if (value == 0 || !cr3UiCall(panel, joint, x, y)) return;
    UiFig& num = cr3UiPut("camequnumup_num00", "", 0, x, y, view);
    const bool down = value < 0;
    const auto put = [&](int slot, int frame) {
        char name[40];
        std::snprintf(name, sizeof name, "call_comNum03_fr00_%03d", slot);
        float cx, cy;
        if (cr3UiCall(num, name, cx, cy)) cr3UiPut("comnum03_fr00", "", float(frame) / 30, cx, cy, view);
    };
    put(1, down ? 21 : 20);
    long most = 1;
    for (int k = 0; k < places; ++k) most *= 10;
    const long v = std::min(long(std::abs(value)), most - 1);
    bool shown = false;
    for (int k = 0; k < places; ++k) {
        most /= 10;
        const int d = int(v / most % 10);
        if (d > 0 || k == places - 1) shown = true;
        if (shown) put(2 + k, d + (down ? 10 : 0));
    }
    if (percent) put(places + 2, down ? 23 : 22);
}

// a number on the panel (SetNumberOfStatus: camEquNumSts_num00's five comNum00_fr00)
void Viewer::cr3EquipNumber(const UiFig& panel, int k, int value, const Mat4& view) {
    char joint[40];
    std::snprintf(joint, sizeof joint, "call_camEquNumSts_num00_%03d", k);
    float x, y;
    if (!cr3UiCall(panel, joint, x, y)) return;
    UiFig& num = cr3UiPut("camequnumsts_num00", "", 0, x, y, view);
    cr3UiDigits(num, "call_comNum00_fr00", "comnum00_fr00", std::max(value, 0), 5, 0, view);
}

// the panel's numbers (CampEquip2Menu::SetEquipParam / SetEquipParamID): the weapon's and the
// armour's panel - weight, power, hits as worn (ChangeOriginalParameter) and how the item in
// focus changes them, then its resistances (the weapon's lowered ones from the first of
// camEquZok_fr00_010 .., the armour's from _014; those not 0, four at most - camEquZok_fr00 at
// frame 0 physical, 1 magic, 2 .. 7 the elements 1 .. 6 - each as a change with its percent
// sign); the accessory's panel - HP, MP, attack, defence, speed, luck and their changes, the HP
// and MP bars at the share of the worn's left (100 at most). `both`: the weapon's resistances
// and the armour's (statusEquipMenu's)
void Viewer::drawCr3EquipNumbers(const UiFig& panel, int slot, const cr3::Cr3Fighter& was, const cr3::Cr3Fighter& now, const Mat4& view, bool both) {
    char joint[40];
    const auto upJoint = [&](int k) {
        std::snprintf(joint, sizeof joint, "call_camEquNumUp_num00_%03d", k);
        return std::string(joint);
    };
    if (slot == 2) {
        const int a[6] = {was.maxHp, was.maxMp, was.attack, was.defence, was.speed, was.luck};
        const int b[6] = {now.maxHp, now.maxMp, now.attack, now.defence, now.speed, now.luck};
        for (int k = 0; k < 6; ++k) cr3EquipNumber(panel, k, a[k], view), cr3EquipChange(panel, upJoint(k), b[k] - a[k], 5, false, view);
        float x, y;
        const auto share = [](int to, int from) { return from == 0 ? 100.0f : std::clamp(float(to) * 100.0f / float(from), 0.0f, 100.0f); };
        if (cr3UiCall(panel, "call_camEquHp_per", x, y)) cr3UiPut("camequhp_per", "", share(now.maxHp, was.maxHp) / 30, x, y, view);
        if (cr3UiCall(panel, "call_camEquMp_per", x, y)) cr3UiPut("cameqump_per", "", share(now.maxMp, was.maxMp) / 30, x, y, view);
        return;
    }
    const int a[3] = {was.weight, was.weaponConstant, was.weaponHits};
    const int b[3] = {now.weight, now.weaponConstant, now.weaponHits};
    for (int k = 0; k < 3; ++k) cr3EquipNumber(panel, k, a[k], view), cr3EquipChange(panel, upJoint(k), b[k] - a[k], 5, false, view);
    for (int set = 0; set < 2; ++set) {
        if (!both && set != slot) continue;
        int at = set * 4;
        for (int e = 0; e < 8 && at < set * 4 + 4; ++e) {
            const int v = set == 0 ? (e == 0 ? now.lowerPhysical : e == 1 ? now.lowerMagic : now.lowerElement[size_t(e - 1)])
                                   : (e == 0 ? now.physical : e == 1 ? now.magic : now.elementResist[size_t(e - 1)]);
            if (v == 0) continue;
            float x, y;
            std::snprintf(joint, sizeof joint, "call_camEquZok_fr00_01%d", at);
            if (cr3UiCall(panel, joint, x, y)) cr3UiPut("camequzok_fr00", "", float(e) / 30, x, y, view);
            std::snprintf(joint, sizeof joint, "call_camEquNumUp_num00_01%d", at);
            cr3EquipChange(panel, joint, v, 4, true, view);
            ++at;
        }
    }
}

// the model shown (CampEquip2Menu::SetCharaModelID: the character's CrxModel in the item in
// focus - its body the armour's, its weapon -, bb01 looped, at (-2.8, (scale - 2) x -0.7 - 1.54,
// 9) in the interface's space, `scale` x, turned by the yaw; faded in over 0.3 s; lit by
// camp_light's lights)
void Viewer::drawCr3CampModel(int w, int h, const Mat4& view) {
    Cr3CampEquip& ce = campEquip;
    cr3::MbeFile::Row c, m;
    if (!cr3TablesReady() || !cr3BattleTables.character.find("Char", ce.chara, "ii", c) || !cr3BattleTables.model.find("Model", c.i(1), "iippffiffii", m))
        return;
    const std::string name = m.s(2);
    const bool parts = (m.i(10) & 1) != 0;
    const auto focused = [&](int slot) {
        const int id = ce.step == 1 && ce.slot == slot && ce.row < int(ce.rows.size()) ? ce.rows[size_t(ce.row)] : cr3WornItem(ce.chara, slot);
        return id > 0 ? id % 100000 : 0;
    };
    std::map<int, int> armour = {{ce.chara, focused(1)}}, weapon = {{ce.chara, focused(0)}};
    if (game.cr3Armour.find(ce.chara) == game.cr3Armour.end() && armour[ce.chara] == 0) armour.clear();
    const std::string body = parts ? cr3BattleTables.body(name, armour.empty() ? cr3BattleTables.defaultArmour(ce.chara) : armour[ce.chara]) : name;
    std::string wfig, wvar;
    if (!cr3BattleTables.weaponModel(cr3BattleTables.weaponOf(ce.chara, &weapon), wfig, wvar)) wfig.clear();
    Cr3CampModel& cm = campModel;
    const std::string key = body + "/" + wfig + "/" + wvar;
    if (cm.key != key) {
        unload(cm.body), unload(cm.head), unload(cm.weapon);
        cm = Cr3CampModel();
        cm.key = key;
        std::string err;
        for (int part = 0; part < 2; ++part) {
            const std::string fig = part ? (parts ? name + "h01" : std::string()) : body;
            ViewFigure& f = part ? cm.head : cm.body;
            if (fig.empty()) continue;
            if (!loadFigure(fig + ".geom", f, &err)) {
                SDL_Log("camp model %s: %s", fig.c_str(), err.c_str());
                continue;
            }
            const auto it = std::find(f.anims.begin(), f.anims.end(), motionBase(fig) + "_bb01.anim");
            if (it != f.anims.end()) loadAnim(f, size_t(it - f.anims.begin()));
        }
        figureVariant = wvar;
        if (!wfig.empty() && !loadFigure(wfig + ".geom", cm.weapon, &err)) SDL_Log("camp weapon %s: %s", wfig.c_str(), err.c_str());
        figureVariant.clear();
    }
    cr3::SceneLighting l;
    if (!figureLights("camp_light", l)) l = cr3::SceneLighting();
    l.camera[0] = 0, l.camera[1] = 0, l.camera[2] = 8.81f;
    l.time = uiClock;
    dev->setLighting(l);
    const float a = std::clamp(ce.modelSince / 0.3f, 0.0f, 1.0f);
    const float yc = std::cos(ce.yaw) * ce.scale, ys = std::sin(ce.yaw) * ce.scale;
    for (ViewFigure* part : {&cm.body, &cm.head}) {
        if (part->meshes.empty()) continue;
        part->place = cr3::Mat34();
        part->place.m[0] = yc, part->place.m[2] = ys, part->place.m[8] = -ys, part->place.m[10] = yc, part->place.m[5] = ce.scale;
        part->place.m[3] = -2.8f, part->place.m[7] = (ce.scale - 2) * -0.7f - 1.54f, part->place.m[11] = 9.0f;
        part->tint[3] = a;
        pose(*part, uiClock, true);
        drawFigure(*part, view.data());
    }
    drawAttached(cm.weapon, cm.body, "DummyWP", uiClock, view.data());
    dev->setLighting(cr3::SceneLighting());
    (void)w, (void)h;
}

// a drag on the model (TouchSimpleMove: across turns it a radian each 100 of the screen's 960,
// up and down sizes it a hundredth each, 2 .. 3)
bool Viewer::cr3CampDrag(float dx, float dy) {
    if (cr3Camp.stage == 10) { // (My Page's log: dragged up, on down the rows - TouchSimpleMove)
        myPage.scroll = std::clamp(myPage.scroll - dy / 100, 0.0f, 1.92f);
        return true;
    }
    if (cr3Camp.stage != 5 || !campEquip.model) return false;
    campEquip.yaw += dx / 100;
    campEquip.scale = std::clamp(campEquip.scale + dy / 100, 2.0f, 3.0f);
    return true;
}

// CampEquip2Menu's status (the camp's Equipment, the shop's: SetCampEquipStatusCharacter /
// SetShopCampEquipStatusParts): camEquPosition (its own animation at `t`), camEquMenu at its
// ragdoll_call_camEquMenu, camEquWepPanel - a weapon, armour - or camEquJewPanel - an accessory:
// the character's icon, its numbers as they are and with `item` in the slot, its name and its
// gene's. The menu drawn (for the camp's button), or null
// (preview: an item attached to an equipment for the second numbers - the alteration's)
auto Viewer::drawCr3EquipStatus(int chara, int slot, int item, float t, int w, int h, const Mat4& view, const std::pair<int, int>* preview) -> UiFig* {
    float x, y, cx, cy;
    UiFig& pos = cr3UiPut("camequposition", "", t, 0, 0, view);
    if (!cr3UiCall(pos, "ragdoll_call_camEquMenu", x, y, w, h)) return nullptr;
    UiFig& menu = cr3UiPut("camequmenu", "", 0, x, y, view);
    float px, py;
    if (cr3UiCall(menu, slot == 2 ? "call_camEquJewPanel" : "call_camEquWepPanel", px, py)) {
        UiFig& panel = cr3UiPut(slot == 2 ? "camequjewpanel" : "camequweppanel", "", 0, px, py, view);
        if (cr3UiCall(panel, "call_camEquCharaIcon_fr00", cx, cy)) cr3UiPut("genecngcahaicon_fr00", "", float(chara - 1) / 30, cx, cy, view); // (SetCharaParam: the part is geneCngCahaIcon_fr00)
        cr3::Cr3Fighter was, now;
        bool shown = cr3EquipStats(chara, -1, 0, was);
        if (preview && preview->second > 0) {
            const auto keep = game.cr3Attachments;
            game.cr3Attachments[preview->first] = {preview->second, 1};
            shown = shown && cr3EquipStats(chara, slot, item, now);
            game.cr3Attachments = keep;
        } else shown = shown && cr3EquipStats(chara, slot, item, now);
        if (shown) drawCr3EquipNumbers(panel, slot, was, now, view);
        // (SetCharaParam: the name at the first typeM_L, the gene's at the next - lighter, smaller)
        cr3UiLabel(panel, "typeM_L01", cr3NameOf("char_name.mbe", "CharName", chara), w, h, 0.22f);
        float tx, ty;
        const std::string gene = cr3GeneNameOf(chara);
        if (!gene.empty() && cr3UiCall(panel, "typeM_L02", tx, ty)) {
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            drawUiText(gene, 24, (tx + 0.06f) / 10, (ty - 0.12f) / 10, w, h, true, 0.43f);
        }
    }
    return &menu;
}

void Viewer::drawCampEquip(int w, int h, const Mat4& view) {
    const float t = cr3Camp.since;
    Cr3CampEquip& ce = campEquip;
    float x, y, cx, cy;
    const int focusItem = ce.step == 0 ? cr3WornItem(ce.chara, ce.slot) : ce.row < int(ce.rows.size()) ? ce.rows[size_t(ce.row)] : 0;
    // (the status: the slot's panel)
    // (its own animation slides it in over 5 frames; the model view plays it back out -
    // InterfaceCloseAnimeSet - and leaves the button where it was)
    const float slide = ce.model ? std::max(0.1f - ce.modelSince, 0.0f) : t;
    if (UiFig* menuAt = drawCr3EquipStatus(ce.chara, ce.slot, focusItem, slide, w, h, view)) {
        UiFig& menu = *menuAt;
        float px, py;
        if (cr3UiCall(menu, "call_camEquBtnPanel", px, py)) {
            if (!ce.model || ce.btnY < -50) ce.btnX = px, ce.btnY = py; // (+0x4cc: held where it was while the model shows)
            UiFig& bp = cr3UiPut("camequbtnpanel", "", 0, ce.btnX, ce.btnY, view);
            if (cr3UiCall(bp, "call_camEquBtnText_fr00", cx, cy)) cr3UiPut("camequbtntext_fr00", "", ce.model ? 1.0f / 30 : 0.0f, cx, cy, view);
            float ax, ay, ex, ey;
            if (cr3UiCall(bp, "tapAreaS01", ax, ay) && cr3UiCall(bp, "tapAreaE01", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, 800});
        }
    }
    if (ce.model) drawCr3CampModel(w, h, view);
    // (the list: the slots, or a slot's candidates)
    UiFig& lp = cr3UiPut("comlistposition", "comlistposition_io", t, 0, 0, view);
    if (cr3UiCall(lp, "call_comListMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
        float lx, ly, labelX = 0, labelY = -100;
        if (ce.step == 0 && cr3UiCall(menu, "call_comListCategoryPanel", lx, ly)) { // (ReOpenListMenu 3: "Equipment")
            UiFig& cat = cr3UiPut("comlistcategorypanel", "", 0, lx, ly, view);
            const float frame = 3.0f / 30;
            if (cr3UiCall(cat, "call_comListCBg_fr00", cx, cy)) cr3UiPut("comlistcbg_fr00", "", frame, cx, cy, view);
            if (cr3UiCall(cat, "call_comListCIcon_fr00", cx, cy)) cr3UiPut("comlistcicon_fr00", "", frame, cx, cy, view);
            if (cr3UiCall(cat, "call_comListCtext_fr00", cx, cy)) cr3UiPut("comlistctext_fr00", "", frame, cx, cy, view);
        } else if (ce.step == 1) {
            drawCr3Categories(menu, kEquipTabs, 3, ce.slot, 400, labelX, labelY, view);
        }
        if (cr3UiCall(menu, "call_comListPanel", lx, ly)) {
            const int n = ce.step == 0 ? 3 : int(ce.rows.size());
            const int focus = ce.step == 0 ? ce.slot : ce.row;
            const int top = focus - 2; // (comListMenu: the focused row the third, the list past it)
            drawCr3ListBar(menu, false, focus, n, 1, view, 0.15f);
            drawCr3ListEnds(lx, ly, n, focus, view);
            for (int k = std::max(top, 0); k < n && k <= focus + 2; ++k) {
                const int id = ce.step == 0 ? cr3WornItem(ce.chara, k) : ce.rows[size_t(k)];
                Cr3FocusRow focusRow(*this, k == focus);
                UiFig& panel = cr3UiPut("comlistpanel", "", 0, cr3ListRowX(lx, k, focus), cr3ListRowY(ly, k, top, focus), view);
                if (k == focus && cr3UiCall(panel, "call_comListCursor", cx, cy)) cr3UiPut("comlistcursor", "", 0, cx, cy, view);
                cr3::MbeFile::Row r;
                if (id > 0 && cr3ItemRow(id, r) && cr3UiCall(panel, "call_comListIcon_fr00", cx, cy))
                    cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), r.i(3)), cx, cy, view);
                if (ce.step == 1 && id > 0)
                    for (const int ch : cr3PartyMembers())
                        if (cr3WornItem(ch, ce.slot) == id && cr3UiCall(panel, "call_camEquChr_fr00", cx, cy)) {
                            cr3UiPut("camequchr_fr00", "", float(ch - 1) / 30, cx, cy, view);
                            break;
                        }
                cr3UiLabel(panel, "typeM_L01", id > 0 ? cr3ItemText(id, false) : std::string("-"), w, h, 0.3f);
                float ax, ay, bx, by;
                if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 300 + k});
            }
        }
        if (labelY > -100) {
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            drawUiText(menuAnnounce(kEquipTabs[ce.slot] + 0x29), 20, labelX / 10, (labelY - 0.1f) / 10, w, h, false, 1.0f);
        }
    }
    // (the footer: the characters' buttons, the back)
    UiFig& foot = cr3UiPut("comftrposition", "", 0, 0, 0, view);
    if (cr3UiCall(foot, "call_comFtrEquipMenu", x, y, w, h)) {
        UiFig& fm = cr3UiPut("comftrequipmenu", "", 0, x, y, view);
        const std::vector<int> members = cr3PartyMembers();
        for (int k = 0; k < 5 && k < int(members.size()); ++k) {
            if (!cr3UiCall(fm, "call_comFtrCharaBtn_fr00_00" + std::to_string(k + 1), cx, cy)) continue;
            const int ch = members[size_t(k)];
            const int frame = ch <= 5 ? ch - 1 + (ch == ce.chara ? 0 : 5) : ch == ce.chara ? 10 : 11;
            UiFig& b = cr3UiPut("comftrcharabtn_fr00", "", float(frame) / 30, cx, cy, view);
            float ax, ay, bx, by;
            if (cr3UiCall(b, "tapAreaS01", ax, ay) && cr3UiCall(b, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 700 + k});
        }
    }
    if (cr3UiCall(foot, "call_comBackMenu_001", x, y, w, h)) {
        UiFig& bm = cr3UiPut("combackmenu", "", 0, x, y, view);
        float ax, ay, ex, ey;
        if (cr3UiCall(bm, "call_comBack_fr00", cx, cy)) cr3UiPut("comback_fr00", "", 0, cx, cy, view);
        if (cr3UiCall(bm, "tapAreaS01", ax, ay) && cr3UiCall(bm, "tapAreaE01", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, -1});
    }
    drawCr3Header(t, 2, ce.step == 0 ? menuAnnounce(0x18b) : focusItem > 0 ? cr3ItemText(focusItem, true) : std::string(), w, h, view);
}

void Viewer::chooseCampEquip(int id) {
    Cr3CampEquip& ce = campEquip;
    if (id == -1) {
        audio.playSe("com_004", false);
        if (ce.step == 1) ce.step = 0;
        else cr3Camp.stage = 1, cr3Camp.since = 0;
        return;
    }
    if (id == 800) { // (the model view, on or off)
        audio.playSe("com_003", false);
        ce.model = !ce.model, ce.modelSince = 0;
        if (ce.model) ce.yaw = 0, ce.scale = 2;
        SDL_Log("camp: equipment model %s", ce.model ? "shown" : "hidden");
        return;
    }
    if (id >= 700 && id < 705) { // (another character)
        const std::vector<int> members = cr3PartyMembers();
        if (id - 700 >= int(members.size()) || members[size_t(id - 700)] == ce.chara) return;
        ce.chara = members[size_t(id - 700)];
        audio.playSe("com_002", false);
        SDL_Log("camp: equipment character %d", ce.chara);
        if (ce.step == 1) campEquipRows();
        return;
    }
    if (ce.step == 1 && id >= 400 && id < 403) { // (slot category tab)
        if (id - 400 != ce.slot) {
            ce.slot = id - 400;
            audio.playSe("com_002", false);
            campEquipRows();
        }
        return;
    }
    if (id < 300 || id >= 400) return;
    const int k = id - 300;
    if (ce.step == 0) {
        if (k > 2) return;
        if (k != ce.slot) {
            ce.slot = k;
            audio.playSe("com_002", false);
            return;
        }
        audio.playSe("com_003", false);
        ce.step = 1;
        campEquipRows();
        return;
    }
    if (k >= int(ce.rows.size())) return;
    if (k != ce.row) {
        ce.row = k;
        audio.playSe("com_002", false);
        return;
    }
    audio.playSe("com_003", false);
    campEquipWear(ce.rows[size_t(k)]);
    ce.step = 0;
}

void Viewer::campEquipKey(SDL_Keycode key) {
    Cr3CampEquip& ce = campEquip;
    const bool up = key == SDLK_UP, down = key == SDLK_DOWN, left = key == SDLK_LEFT, right = key == SDLK_RIGHT;
    const bool enter = key == SDLK_RETURN || key == SDLK_SPACE;
    if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) return chooseCampEquip(-1);
    if (key == SDLK_TAB) return chooseCampEquip(800);
    if (left || right || (vitaButtons() && key == SDLK_X)) {
        const std::vector<int> members = cr3PartyMembers();
        const auto it = std::find(members.begin(), members.end(), ce.chara);
        if (members.empty() || it == members.end()) return;
        const int at = int(it - members.begin()), n = std::min(int(members.size()), 5);
        return chooseCampEquip(700 + (at + (right || key == SDLK_X ? 1 : n - 1)) % n);
    }
    const int n = ce.step == 0 ? 3 : int(ce.rows.size()), focus = ce.step == 0 ? ce.slot : ce.row;
    if ((up || down) && n > 0) chooseCampEquip(300 + std::clamp(focus + (down ? 1 : -1), 0, n - 1));
    else if (enter && n > 0) chooseCampEquip(300 + focus);
}

// the field's player in its armour's body again (CrxModel::GetBodyModelNo, after the camp)
void Viewer::cr3BodiesChanged() {
    if (!areaMode || areaPlayerName.size() != 7 || areaPlayerName[0] != 'j' || areaPlayerName[4] != 'b') return;
    const std::string body = cr3PartsBody(areaPlayerName.substr(0, 4));
    if (body == areaPlayerName) return;
    std::string err;
    unload(areaPlayer), unload(areaPlayerHead);
    areaPlayer = ViewFigure(), areaPlayerHead = ViewFigure();
    areaPlayerName = body;
    if (!loadFigure(areaPlayerName + ".geom", areaPlayer, &err)) SDL_Log("player %s: %s", areaPlayerName.c_str(), err.c_str());
    if (archive.contains(areaPlayerName.substr(0, 4) + "h01.geom") && !loadFigure(areaPlayerName.substr(0, 4) + "h01.geom", areaPlayerHead, &err))
        SDL_Log("player head: %s", err.c_str());
    areaMotionSet = false;
    setAreaMotion(cr3::Cr3Motion::Idle);
}

// the list's category buttons (CategoryBtnPanel: comListCategoryBtn 0.57 apart from
// call_comListCategoryBtn, six shown, the focused raised (-0.072, +0.057); comListCBtnIcon_fr00
// at the value's frame; ChangeCategoryPanel: the others at 0.8 x, coloured (0.549, 0.549,
// 0.655); the focused's name - AnnounceMenuName value + 0x29 - at its typeM_C01, drawn last)
// and between their arrows (comListMenu's comListCArrowPanel on its _loop, at the panel's
// call_comListCArrowPanel): taps tapBase + k
void Viewer::drawCr3Categories(const UiFig& menu, const int* values, int count, int focus, int tapBase, float& labelX, float& labelY, const Mat4& view) {
    float lx, ly, bx, by, cx, cy;
    labelY = -100;
    if (!cr3UiCall(menu, "call_comListCategoryBtnPanel", lx, ly)) return;
    UiFig& bp = cr3UiPut("comlistcategorybtnpanel", "", 0, lx, ly, view);
    if (cr3UiCall(bp, "call_comListCArrowPanel", cx, cy)) cr3UiPut("comlistcarrowpanel", "comlistcarrowpanel_loop", uiClock, cx, cy, view, 1, true);
    if (!cr3UiCall(bp, "call_comListCategoryBtn", bx, by)) return;
    const int first = std::clamp(focus - 5, 0, std::max(count - 6, 0));
    for (int k = first; k < first + 6 && k < count; ++k) {
        const bool focused = k == focus;
        const float px = bx + 0.057f + 0.57f * float(k - first) - (focused ? 0.072f : 0.0f), py = by - 0.057f + (focused ? 0.057f : 0.0f);
        if (!focused) cr3UiScale = 0.8f, cr3UiTint[0] = cr3UiTint[1] = 0.549f, cr3UiTint[2] = 0.655f;
        UiFig& btn = cr3UiPut("comlistcategorybtn", "", 0, px, py, view);
        if (cr3UiCall(btn, "call_comListCBtnIcon_fr00", cx, cy)) cr3UiPut("comlistcbtnicon_fr00", "", float(values[k]) / 30, cx, cy, view);
        cr3UiScale = 1, cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
        if (focused && cr3UiCall(btn, "typeM_C01", cx, cy)) labelX = cx, labelY = cy;
        float ax, ay, ex, ey;
        if (cr3UiCall(btn, "tapAreaCP00", ax, ay) && cr3UiCall(btn, "tapAreaAP00", ex, ey)) cr3CampTaps.push_back(circleTap(ax, ay, ex, ey, tapBase + k));
    }
}

// a held gift's code (gift_color + gift_number; 0 not a gift)
int Viewer::cr3GiftCode(int id) {
    cr3::MbeFile::Row r;
    if (id / 100000 != 6 || !cr3TablesReady() || !cr3BattleTables.item.find("Gift", id % 100000, "iiiiiiiiiiiiiiiiii", r)) return 0;
    return r.i(16) + r.i(17);
}

// the held gifts a fusion would use, one a code (empty: one missing)
std::vector<int> Viewer::cr3FusionGifts(const cr3::BsonValue& f) {
    std::vector<int> out;
    std::map<int, int> left;
    for (const auto& [id, n] : game.cr3Items)
        if (id / 100000 == 6 && n > 0) left[id] = n;
    if (const cr3::BsonValue* items = f.get("items"))
        for (const cr3::BsonValue& c : items->items) {
            const int code = c.asInt();
            if (code <= 0) continue;
            int found = 0;
            for (auto& [id, n] : left)
                if (n > 0 && cr3GiftCode(id) == code) {
                    found = id, --n;
                    break;
                }
            if (!found) return {};
            out.push_back(found);
        }
    return out;
}

const cr3::BsonValue* Viewer::cr3Fusion(int k) {
    const cr3::BsonValue* t = game.cr3Db ? game.cr3Db->table("mergegifts") : nullptr;
    return t && k >= 0 && size_t(k) < t->items.size() ? &t->items[size_t(k)] : nullptr;
}

void Viewer::campGiftRows() {
    Cr3CampGifts& cg = campGifts;
    cg.rows.clear();
    const cr3::BsonValue* t = game.cr3Db ? game.cr3Db->table("mergegifts") : nullptr;
    const int category = cg.category == 2 ? 3 : cg.category;
    if (t)
        for (size_t k = 0; k < t->items.size(); ++k) {
            const cr3::BsonValue& f = t->items[k];
            const int available = f.integer("available", -1);
            if (f.integer("category") == category && (available < 0 || game.flag(available))) cg.rows.push_back(int(k));
        }
    cg.row = std::clamp(cg.row, 0, std::max(int(cg.rows.size()) - 1, 0));
}

void Viewer::openCampGifts() {
    cr3Camp.stage = 6, cr3Camp.since = 0;
    campGifts = Cr3CampGifts();
    campGiftRows();
    SDL_Log("camp: fuse gifts (%zu)", campGifts.rows.size());
}

void Viewer::drawCampGifts(int w, int h, const Mat4& view) {
    if (geneInspectOpen) {
        drawGeneBase(w, h, view, &inspectedGene);
        cr3CampTaps.clear();
        cr3CampTaps.push_back({-100, -100, 100, 100, 502});
        return;
    }
    const float t = cr3Camp.since;
    Cr3CampGifts& cg = campGifts;
    float x, y, cx, cy;
    const cr3::BsonValue* focus = cg.row < int(cg.rows.size()) ? cr3Fusion(cg.rows[size_t(cg.row)]) : nullptr;
    // (the focused fusion's gifts and gene)
    UiFig& cp = cr3UiPut("camconpolistpositon", "", t, 0, 0, view);
    if (focus && cr3UiCall(cp, "call_camConpoListMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("camconpolistmenu", "", 0, x, y, view);
        std::map<int, int> left;
        for (const auto& [id, n] : game.cr3Items)
            if (id / 100000 == 6 && n > 0) left[id] = n;
        int slot = 0;
        if (const cr3::BsonValue* items = focus->get("items"))
            for (const cr3::BsonValue& c : items->items) {
                const int code = c.asInt();
                if (code <= 0 || slot >= 6) continue;
                float px, py;
                if (!cr3UiCall(menu, "call_camConpoListPanel_00" + std::to_string(++slot), px, py)) continue;
                // (the gift drawn: one held of its code, else the first of its code at all)
                int gift = 0;
                for (auto& [id, n] : left)
                    if (n > 0 && cr3GiftCode(id) == code) {
                        gift = id, --n;
                        break;
                    }
                const bool held = gift != 0;
                if (!held && cr3TablesReady())
                    if (const cr3::MbeFile::Table* gt = cr3BattleTables.item.table("Gift"))
                        for (size_t r = 0; r < gt->rows.size() && !gift; ++r) {
                            cr3::MbeFile::Row row;
                            if (cr3BattleTables.item.row(*gt, r, "iiiiiiiiiiiiiiiiii", row) && row.i(16) + row.i(17) == code) gift = 600000 + row.i(0);
                        }
                UiFig& panel = cr3UiPut("camconpolistpanel", "", 0, px, py, view, held ? 1.0f : 0.4f);
                cr3::MbeFile::Row r;
                if (gift && cr3ItemRow(gift, r) && cr3UiCall(panel, "call_comListIcon_fr00", cx, cy))
                    cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), r.i(3)), cx, cy, view, held ? 1.0f : 0.4f);
            }
    }
    UiFig& mp = cr3UiPut("mixmtrpositon", "", 0, 0, 0, view);
    if (focus && cr3UiCall(mp, "call_GeneCard", x, y)) {
        dev->clearDepth();
        drawCr3Card(focus->integer("gene_id"), x - 0.5f, y - 1.17f, 1.0f, 4.5f, view); // (SetCardModel: 4.5 x, -0.5 / -1.17 / +1 from the joint)
    }
    // (the list)
    UiFig& lp = cr3UiPut("comlistposition", "comlistposition_io", t, 0, 0, view);
    if (cr3UiCall(lp, "call_comListMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
        float labelX = 0, labelY = -100, lx, ly;
        drawCr3Categories(menu, kGiftTabs, 3, cg.category, 400, labelX, labelY, view);
        if (cr3UiCall(menu, "call_comListPanel", lx, ly)) {
            const int n = int(cg.rows.size());
            const int top = std::clamp(cg.row - 1, 0, std::max(n - 3, 0));
            for (int k = top; k < n && k < top + 3; ++k) {
                const cr3::BsonValue* f = cr3Fusion(cg.rows[size_t(k)]);
                if (!f) continue;
                const bool ready = !cr3FusionGifts(*f).empty();
                const float alpha = ready ? 1.0f : 0.5f;
                UiFig& row = cr3UiPut("comlistlpanel", "", 0, lx, ly - 1.24f * float(k - top), view, alpha);
                if (k == cg.row && cr3UiCall(row, "call_comListLCursor", cx, cy)) cr3UiPut("comlistlcursor", "", 0, cx, cy, view);
                if (cr3UiCall(row, "call_comListLMixing", cx, cy)) {
                    UiFig& mixing = cr3UiPut("comlistlmixing", "", 0, cx, cy, view, alpha);
                    const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db ? game.cr3Db->geneInfo(f->integer("gene_id")) : nullptr;
                    drawGeneRowParts(mixing, f->integer("gene_id"), 1, gi ? gi->maxLevel : 1, -1, view, alpha, w, h, 0);
                }
                float ax, ay, bx, by;
                if (cr3UiCall(row, "tapAreaS01", ax, ay) && cr3UiCall(row, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 300 + k});
            }
        }
        if (labelY > -100) {
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            drawUiText(menuAnnounce(kGiftTabs[cg.category] + 0x29), 20, labelX / 10, (labelY - 0.1f) / 10, w, h, false, 1.0f);
        }
    }
    drawCr3CampFooter(w, h, view);
    if (cg.step == 1 && focus) {
        const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db ? game.cr3Db->geneInfo(focus->integer("gene_id")) : nullptr;
        std::string text = menuAnnounce(0x118);
        if (const size_t at = text.find("{rarety}"); at != std::string::npos) text.replace(at, 8, menuAnnounce(0x242 + (gi ? gi->grade : 0)));
        if (const size_t at = text.find("{name}"); at != std::string::npos) text.replace(at, 6, cr3NameOf("gene_name.mbe", "GeneName", focus->integer("gene_id")));
        drawCr3Asked(text, t, cg.askFocus, true, 500, 501, w, h, view);
    }
    if (!cg.told.empty()) drawCr3Told(cg.told, cg.toldSince, 502, w, h, view);
    drawCr3Header(t, 4, menuAnnounce(0x23e + cg.category), w, h, view);
}

void Viewer::chooseCampGifts(int id) {
    if (geneInspectOpen) { geneInspectOpen = false; audio.playSe("com_004", false); return; }
    Cr3CampGifts& cg = campGifts;
    if (!cg.told.empty()) {
        if (id == 502 || id == -1) cg.told.clear();
        return;
    }
    if (id == -1) {
        audio.playSe("com_004", false);
        if (cg.step == 1) cg.step = 0;
        else cr3Camp.stage = 1, cr3Camp.since = 0;
        return;
    }
    if (cg.step == 0 && id >= 400 && id < 403) {
        if (id - 400 == cg.category) return;
        cg.category = id - 400, cg.row = 0;
        audio.playSe("com_002", false);
        SDL_Log("camp: gifts, category %d", cg.category);
        campGiftRows();
        return;
    }
    if (cg.step == 0 && id >= 300 && id < 400) {
        const int k = id - 300;
        if (k >= int(cg.rows.size())) return;
        if (k != cg.row) {
            cg.row = k;
            audio.playSe("com_002", false);
            return;
        }
        const cr3::BsonValue* f = cr3Fusion(cg.rows[size_t(k)]);
        if (!f || cr3FusionGifts(*f).empty()) return (void)audio.playSe("com_005", false);
        const int held = int(std::count_if(game.cr3Genes.begin(), game.cr3Genes.end(), [](const cr3::Cr3Gene& g) { return !g.stocked; }));
        if (game.cr3Db && held >= game.cr3Db->constant("GENE_CAPACITY", 50)) { // (CheckHaveGeneMax)
            audio.playSe("com_005", false);
            cg.told = menuAnnounce(0x232), cg.toldSince = 0;
            return;
        }
        audio.playSe("com_003", false);
        cg.step = 1, cg.askFocus = 0, cr3Camp.since = 0;
        return;
    }
    if (cg.step == 1 && (id == 500 || id == 501)) {
        cg.step = 0;
        if (id == 501) return (void)audio.playSe("com_004", false);
        const cr3::BsonValue* f = cg.row < int(cg.rows.size()) ? cr3Fusion(cg.rows[size_t(cg.row)]) : nullptr;
        if (!f || !game.cr3Db) return;
        const std::vector<int> gifts = cr3FusionGifts(*f);
        if (game.cr3Db->mergeGift(game, f->integer("_id"), gifts) == cr3::kDbOk) {
            game.setFlag(21, true); // (CompleteMergeGift: a gift merged - the story waits on it, s101_d02_00606_set)
            audio.playSe("com_034", false);
            std::string text = menuAnnounce(0xfb);
            if (const size_t at = text.find("%s"); at != std::string::npos) text.replace(at, 2, cr3NameOf("gene_name.mbe", "GeneName", f->integer("gene_id")));
            cg.told = text, cg.toldSince = 0;
            SDL_Log("camp: fused %zu gifts into gene %d", gifts.size(), f->integer("gene_id"));
        }
        campGiftRows();
    }
}

void Viewer::campGiftsKey(SDL_Keycode key) {
    if (geneInspectionKey(key)) return;
    Cr3CampGifts& cg = campGifts;
    const bool up = key == SDLK_UP, down = key == SDLK_DOWN, left = key == SDLK_LEFT, right = key == SDLK_RIGHT;
    const bool enter = key == SDLK_RETURN || key == SDLK_SPACE;
    if (!cg.told.empty()) return enter || key == SDLK_ESCAPE ? chooseCampGifts(502) : void();
    if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) return chooseCampGifts(-1);
    if (cg.step == 1) {
        if (left || right) cg.askFocus = right ? 1 : 0, audio.playSe("com_002", false);
        else if (enter) chooseCampGifts(cg.askFocus ? 501 : 500);
        else if (key == SDLK_Y) chooseCampGifts(500);
        else if (key == SDLK_N) chooseCampGifts(501);
        return;
    }
    const int n = int(cg.rows.size());
    if (vitaButtons() && key == SDLK_TAB && cg.row < n) {
        if (const auto* f = cr3Fusion(cg.rows[size_t(cg.row)])) inspectGene(f->integer("gene_id"));
    } else if (left || right) chooseCampGifts(400 + (cg.category + (right ? 1 : 2)) % 3);
    else if ((up || down) && n > 0) chooseCampGifts(300 + std::clamp(cg.row + (down ? 1 : -1), 0, n - 1));
    else if (enter && n > 0) chooseCampGifts(300 + cg.row);
}

void Viewer::campQuestRows() {
    Cr3CampQuests& cq = campQuests;
    cq.rows.clear();
    if (!game.cr3Db) return;
    const auto& quests = game.cr3Db->quests();
    if (cq.category == 0 || cq.category == 2)
        for (const int k : game.cr3Db->standingQuests())
            if (cq.category == 0 || quests[size_t(k)].rank == 6) cq.rows.push_back(k);
    if (cq.category == 1) cq.rows = game.cr3Db->foundQuests();
    if (cq.category == 3)
        for (size_t k = 0; k < quests.size(); ++k)
            if (game.cr3Quests.begun.count(quests[k].id) && !game.cr3Quests.ended.count(quests[k].id)) cq.rows.push_back(int(k));
    cq.row = std::clamp(cq.row, 0, std::max(int(cq.rows.size()) - 1, 0));
}

void Viewer::openCampQuests() {
    cr3Camp.stage = 7, cr3Camp.since = 0;
    campQuests = Cr3CampQuests();
    if (game.cr3Db) game.cr3Db->findQuests(game, std::time(nullptr));
    campQuestRows();
    SDL_Log("camp: quests (%zu)", campQuests.rows.size());
}

void Viewer::drawCampQuests(int w, int h, const Mat4& view) { drawCr3Quests(false, cr3Camp.since, w, h, view); }

// The quests' list and detail (the camp's - QuestMenuMain -, or the Space Port's - SpacePortMain:
// the header "Get Quest" (0x14), its sentence 0x271): the list hidden while a quest's detail is
// up (OutDeleteListMenu); the footer under both
void Viewer::drawCr3Quests(bool port, float t, int w, int h, const Mat4& view) {
    Cr3CampQuests& cq = campQuests;
    float x, y, cx, cy;
    const auto* quests = game.cr3Db ? &game.cr3Db->quests() : nullptr;
    UiFig& lp = cr3UiPut("comlistposition", "comlistposition_io", t, 0, 0, view);
    if (!cq.detail && quests && cr3UiCall(lp, "call_comListMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
        float labelX = 0, labelY = -100, lx, ly;
        drawCr3Categories(menu, kQuestTabs, 4, cq.category, 400, labelX, labelY, view);
        if (cr3UiCall(menu, "call_comListPanel", lx, ly)) {
            const int n = int(cq.rows.size());
            const int top = cq.row - 2; // (comListMenu: the focused row the third, the list past it)
            drawCr3ListBar(menu, false, cq.row, n, 1, view, 0.15f);
            drawCr3ListEnds(lx, ly, n, cq.row, view);
            for (int k = std::max(top, 0); k < n && k <= cq.row + 2; ++k) {
                const cr3::Cr3OfflineDb::Quest& q = (*quests)[size_t(cq.rows[size_t(k)])];
                const bool ready = questReady(q);
                Cr3FocusRow focusRow(*this, k == cq.row, ready);
                UiFig& panel = cr3UiPut("comlistpanel", "", 0, cr3ListRowX(lx, k, cq.row), cr3ListRowY(ly, k, top, cq.row), view);
                if (k == cq.row && ready && cr3UiCall(panel, "call_comListCursor", cx, cy)) cr3UiPut("comlistcursor", "", 0, cx, cy, view);
                if (cr3UiCall(panel, "call_comListIcon_fr00", cx, cy))
                    cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), q.rank + 0xae), cx, cy, view);
                // (CreateQuestListItem: one ended "Clear" - comListClear -, else its level -
                // comListmapLv, "Recommended LVL", with comListNumL_num00 at its call)
                if (game.cr3Quests.ended.count(q.id)) {
                    if (cr3UiCall(panel, "call_comListClear", cx, cy)) cr3UiPut("comlistclear", "", 0, cx, cy, view);
                } else if (cr3UiCall(panel, "call_comListmapLv", cx, cy)) {
                    UiFig& lv = cr3UiPut("comlistmaplv", "", 0, cx, cy, view);
                    float nx, ny;
                    if (cr3UiCall(lv, "call_comListNumL_num00", nx, ny)) drawCr3ListDigits(cr3UiPut("comlistnuml_num00", "", 0, nx, ny, view), q.level, 1, view);
                }
                cr3UiLabel(panel, "typeM_L01", vsText("Quest_Title", q.id), w, h, 0.3f);
                float ax, ay, bx, by;
                if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 300 + k});
            }
        }
        if (labelY > -100) {
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            drawUiText(menuAnnounce(kQuestTabs[cq.category] + 0x29), 20, labelX / 10, (labelY - 0.1f) / 10, w, h, false, 1.0f);
        }
    }
    drawCr3CampFooter(w, h, view);
    if (cq.detail && quests && cq.row < int(cq.rows.size())) {
        dev->clearDepth();
        drawCr3QuestDetail((*quests)[size_t(cq.rows[size_t(cq.row)])], int(cq.rows.size()), cq.detailSince, w, h, view);
    }
    drawCr3Header(t, port ? 0x14 : 5, port && !cq.detail ? menuAnnounce(0x271) : std::string(), w, h, view);
}

// a quest whose goods are held (CreateQuestListItem: its item none, or GetShopItemHaveStock at
// its count) - its row enabled
bool Viewer::questReady(const cr3::Cr3OfflineDb::Quest& q) const {
    const auto held = game.cr3Items.find(q.item);
    return q.item < 0 || (held != game.cr3Items.end() && held->second >= q.itemCount);
}

// A quest's detail (QuestDitailMenu, interface 0x7d - CreateQuestDetailMenu, the camp's and the
// Space Port's; no background of its own): spQstMenu at spQstPosition's ragdoll_call_spQstMenu -
// the title (Quest_Title) at typeM_C01, dark; "No." and the quest's number (its record's +4:
// spQstNumPanel_num00's three spQstNum_fr00, zeros kept); Quest_text's lines - the reward - at
// typeM_L01 (white, on the red bar), L02, L03 (dark); Quest_message's ("{item_cnt}" the goods
// held) on spQstText's bands - typeM_L01 .. L10, white, five shown: the window masked below
// (SetMaskSprite: a 10.15 x 2.5 quad from y -0.75 down); its bar (spQstBar, the spQstCursor at
// its call - moved with six lines or more, AutoScroll); the OK button (spQstBtn); the clear mark
// once ended (SetClearMark: spQstClear on its _loop at call_spQstClear); with two quests or more
// the flick arrows (spQstArrow on its _loop at the position's ragdoll_call_spQstArrow -
// SetQuestDetailMenuChangeButton). Taps: 500 OK (only enabled - CheckOkButtonPermit), 501 / 502
// the arrows
void Viewer::drawCr3QuestDetail(const cr3::Cr3OfflineDb::Quest& q, int count, float t, int w, int h, const Mat4& view, bool okOn, bool cleared) {
    float x, y, cx, cy, ax, ay, bx, by;
    UiFig& pos = cr3UiPut("spqstposition", "", t, 0, 0, view);
    if (count > 1 && cr3UiCall(pos, "ragdoll_call_spQstArrow", x, y, w, h)) {
        UiFig& arrow = cr3UiPut("spqstarrow", "spqstarrow_loop", uiClock, x, y, view, 1, true);
        if (cr3UiCall(arrow, "tapAreaS01", ax, ay) && cr3UiCall(arrow, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 501});
        if (cr3UiCall(arrow, "tapAreaS02", ax, ay) && cr3UiCall(arrow, "tapAreaE02", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 502});
    }
    if (!cr3UiCall(pos, "ragdoll_call_spQstMenu", x, y, w, h)) return;
    UiFig& menu = cr3UiPut("spqstmenu", "", 0, x, y, view);
    float tx, ty;
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    // (SetStringData: utils::FilterMessage - {name}, {team} - over each)
    auto filtered = [&](const char* file) { return areaEvents ? areaEvents->filterMessage(vsText(file, q.id)) : vsText(file, q.id); };
    if (cr3UiCall(menu, "typeM_C01", tx, ty)) drawUiText(filtered("Quest_Title"), 26, tx / 10, (ty - 0.15f) / 10, w, h, false, 0.3f);
    if (cr3UiCall(menu, "call_spQstNumPanel_num00", cx, cy)) {
        UiFig& num = cr3UiPut("spqstnumpanel_num00", "", 0, cx, cy, view);
        const int digit[3] = {q.no % 1000 / 100, q.no % 100 / 10, q.no % 10};
        for (int k = 0; k < 3; ++k) {
            float dx, dy;
            if (cr3UiCall(num, "call_spQstNum_fr00_00" + std::to_string(k + 1), dx, dy)) cr3UiPut("spqstnum_fr00", "", float(digit[k]) / 30, dx, dy, view);
        }
    }
    auto lines = [](const std::string& text) { // (Cr3UtilNPrint: a line each)
        std::vector<std::string> out;
        size_t from = 0;
        while (from <= text.size()) {
            size_t at = text.find("\\n", from), skip = 2;
            const size_t nl = text.find('\n', from);
            if (nl != std::string::npos && (at == std::string::npos || nl < at)) at = nl, skip = 1;
            out.push_back(text.substr(from, at == std::string::npos ? std::string::npos : at - from));
            if (at == std::string::npos) break;
            from = at + skip;
        }
        return out;
    };
    const std::vector<std::string> reward = lines(filtered("Quest_text"));
    for (size_t k = 0; k < reward.size() && k < 3; ++k)
        cr3UiLabel(menu, ("typeM_L0" + std::to_string(k + 1)).c_str(), reward[k], w, h, k == 0 ? 1.0f : 0.26f);
    if (cr3UiCall(menu, "call_spQstText", cx, cy)) {
        std::string body = filtered("Quest_message");
        // ({item_cnt}: GetQuestDeliveryOfGoodsNumber - GetShopItemHaveStock, the goods held)
        const auto held = game.cr3Items.find(q.item);
        const int have = q.item > 0 && held != game.cr3Items.end() ? std::max(held->second, 0) : 0;
        for (size_t at; (at = body.find("{item_cnt}")) != std::string::npos;) body.replace(at, 10, cr3Number(have));
        const std::vector<std::string> text = lines(body);
        // (the window: the mask below it from y -0.75 - the bands masked there)
        const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
        dev->setClip(true, float(screenX), float(screenY), float(screenW), float(screenY) + float(screenH) / 2 + 0.75f * k - float(screenY));
        UiFig& bands = cr3UiPut("spqsttext", "", 0, cx, cy, view);
        for (size_t n = 0; n < text.size() && n < 10; ++n)
            cr3UiLabel(bands, ("typeM_L" + std::string(n < 9 ? "0" : "") + std::to_string(n + 1)).c_str(), text[n], w, h, 1.0f);
        dev->setClip(false);
    }
    if (cr3UiCall(menu, "call_spQstBar", cx, cy)) {
        UiFig& bar = cr3UiPut("spqstbar", "", 0, cx, cy, view);
        if (cr3UiCall(bar, "call_spQstCursor", cx, cy)) cr3UiPut("spqstcursor", "", 0, cx, cy, view);
    }
    if ((cleared || game.cr3Quests.ended.count(q.id)) && cr3UiCall(menu, "call_spQstClear", cx, cy)) cr3UiPut("spqstclear", "spqstclear_loop", uiClock, cx, cy, view, 1, true);
    if (cr3UiCall(menu, "call_spQstBtn", cx, cy)) {
        if (!okOn) cr3UiTint[0] = cr3UiTint[1] = 0.549f, cr3UiTint[2] = 0.655f; // (ControlQuestDetailMenuOkButton(false))
        UiFig& btn = cr3UiPut("spqstbtn", "", 0, cx, cy, view);
        cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
        if (okOn && cr3UiCall(btn, "tapAreaS01", ax, ay) && cr3UiCall(btn, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 500});
    }
}

void Viewer::drawClearQuest(cr3::EventRuntime& ev, int w, int h) {
    clearQuestTaps.clear();
    const int id = ev.clearQuestShown();
    if (id <= 0 || !game.cr3Db) return;
    const auto& quests = game.cr3Db->quests();
    const auto q = std::find_if(quests.begin(), quests.end(), [id](const cr3::Cr3OfflineDb::Quest& r) { return r.id == id; });
    if (q == quests.end()) return;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    std::vector<Cr3GeneLabTap> keep;
    keep.swap(cr3CampTaps);
    drawCr3QuestDetail(*q, 1, std::min(ev.clearQuestAge(), 1.0f), w, h, view, true, true);
    clearQuestTaps.swap(cr3CampTaps);
    keep.swap(cr3CampTaps);
}

// The team's level-up banner (EventRuntime::levelUpShown - LevelUpMenu)
void Viewer::drawLevelUp(cr3::EventRuntime& ev, int w, int h) {
    const float t = ev.levelUpShown();
    if (t < 0) return;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    float x, y;
    UiFig& pos = cr3UiPut("btrsltuppositon", "btrsltuppositon_in", t, 0, 0, view, std::min(t / 0.2f, 1.0f));
    if (cr3UiCall(pos, "call_btRsltLvUpMenu", x, y, w, h)) cr3UiPut("btrsltlvupmenu", "", t, x, y, view);
}

// CR3's item window (window 7 - EvtItemWindow: InterfaceMain::SetGetItemWindowParameter,
// GetItemMenu): feGetPosition, feGetMenu at its call_feGetMenu on feGetMenu_in (0 .. 0.2 s),
// feGetPanel at call_feGetPanel and on it feGetName_fr00, the item's icon (comListIcon_fr00 -
// the item's icon by its category: gold 0xc2) and feGetDeco; the message at the panel's text
// joint 00, size 32, dark (0.17, 0.18, 0.18)
void Viewer::drawCr3GetItem(cr3::EventRuntime& ev, bool withText) {
    const int w = screenW, h = screenH;
    if (ev.windowClosing(7)) return; // (feGetMenu's out: not drawn)
    const float t = ev.windowAge(7);
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    float x, y, cx, cy;
    UiFig& pos = cr3UiPut("fegetposition", "", 0, 0, 0, view);
    if (!cr3UiCall(pos, "call_feGetMenu", x, y, w, h)) return;
    UiFig& menu = cr3UiPut("fegetmenu", "fegetmenu_in", std::min(t, 0.2f), x, y, view);
    if (!cr3UiCall(menu, "call_feGetPanel", x, y)) return;
    UiFig& panel = cr3UiPut("fegetpanel", "", 0, x, y, view);
    if (cr3UiCall(panel, "call_feGetName_fr", cx, cy)) cr3UiPut("fegetname_fr00", "", 0, cx, cy, view);
    const int item = ev.itemShown();
    int icon = item == -10 ? 0xc2 : -1;
    cr3::MbeFile::Row r;
    if (item > 0 && cr3ItemRow(item, r)) icon = r.i(3);
    if (icon >= 0 && cr3UiCall(panel, "call_comListIcon_fr", cx, cy)) cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), icon), cx, cy, view);
    if (cr3UiCall(panel, "call_feGetDeco", cx, cy)) cr3UiPut("fegetdeco", "", 0, cx, cy, view);
    if (!withText || ev.message().pages.empty()) return;
    std::string line;
    for (const cr3::TextLine& l : ev.message().pages[size_t(std::min<int>(ev.page(), int(ev.message().pages.size()) - 1))].lines)
        line += (line.empty() ? "" : " ") + l.text;
    for (const std::string& j : panel.joints)
        if (j.size() > 6 && j.compare(0, 2, "ty") == 0 && j[5] == '_' && j.compare(j.size() - 2, 2, "00") == 0 && uiJoint(panel, j, cx, cy)) { // (the file's "tyepM_L00")
            const bool left = j[6] == 'L', centred = j[6] == 'C';
            drawUiText(line, 28, (cx + (centred ? 0.0f : 0.08f)) / 10, (cy - 0.08f) / 10, w, h, left || !centred, 0.18f);
            break;
        }
}

void Viewer::answerClearQuest() {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || ev->clearQuestShown() <= 0) return;
    audio.playSe("com_003", false);
    ev->answerClearQuest();
    SDL_Log("quest clear: OK");
}

bool Viewer::clearQuestShown() {
    cr3::EventRuntime* ev = activeEvents();
    return ev && ev->clearQuestShown() > 0;
}

bool Viewer::clearQuestClick(float fx, float fy) {
    if (!clearQuestShown()) return false;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    for (const Cr3GeneLabTap& r : clearQuestTaps)
        if (r.id == 500 && ux >= std::min(r.x0, r.x1) && ux <= std::max(r.x0, r.x1) && uy >= std::min(r.y0, r.y1) && uy <= std::max(r.y0, r.y1)) answerClearQuest();
    return true; // (it holds the screen)
}

void Viewer::chooseCampQuests(int id) {
    Cr3CampQuests& cq = campQuests;
    if (cq.detail) {
        if (id == 500 || id == -1) cq.detail = false, cr3Camp.since = 1, audio.playSe("com_004", false);
        if ((id == 501 || id == 502) && !cq.rows.empty()) { // (the arrows: the list's quest before or after, round)
            const int n = int(cq.rows.size());
            cq.row = (cq.row + (id == 501 ? n - 1 : 1)) % n, cq.detailSince = 0;
            audio.playSe("com_002", false);
        }
        return;
    }
    if (id == -1) {
        audio.playSe("com_004", false);
        cr3Camp.stage = 1, cr3Camp.since = 0;
        return;
    }
    if (id >= 400 && id < 404) {
        if (id - 400 == cq.category) return;
        cq.category = id - 400, cq.row = 0;
        audio.playSe("com_002", false);
        SDL_Log("camp: quests, category %d", cq.category);
        campQuestRows();
        return;
    }
    if (id >= 300 && id < 400) {
        const int k = id - 300;
        if (k >= int(cq.rows.size())) return;
        if (k != cq.row) {
            cq.row = k;
            audio.playSe("com_002", false);
            return;
        }
        audio.playSe("com_003", false);
        cq.detail = true, cq.detailSince = 0;
    }
}

void Viewer::campQuestsKey(SDL_Keycode key) {
    Cr3CampQuests& cq = campQuests;
    const bool up = key == SDLK_UP, down = key == SDLK_DOWN, left = key == SDLK_LEFT, right = key == SDLK_RIGHT;
    const bool enter = key == SDLK_RETURN || key == SDLK_SPACE;
    if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) return chooseCampQuests(-1);
    if (cq.detail) return enter ? chooseCampQuests(500) : left || right ? chooseCampQuests(left ? 501 : 502) : void();
    const int n = int(cq.rows.size());
    if (left || right) chooseCampQuests(400 + (cq.category + (right ? 1 : 3)) % 4);
    else if ((up || down) && n > 0) chooseCampQuests(300 + std::clamp(cq.row + (down ? 1 : -1), 0, n - 1));
    else if (enter && n > 0) chooseCampQuests(300 + cq.row);
}

auto Viewer::campStatusRows() -> std::vector<Cr3StatusRow> {
    std::vector<Cr3StatusRow> out;
    Cr3CampStatus& cs = campStatus;
    if (cs.category < 2) return out;
    if (cs.category == 3) {
        for (int slot = 0; slot < 3; ++slot) {
            const int id = cr3WornItem(cs.chara, slot);
            Cr3StatusRow r;
            cr3::MbeFile::Row row;
            if (id > 0) r.name = cr3ItemText(id, false), r.help = cr3ItemText(id, true), r.icon = cr3ItemRow(id, row) ? row.i(3) : -1;
            else r.name = "-";
            out.push_back(r);
        }
        return out;
    }
    const int k = game.wornGene(cs.chara);
    if (k < 0) return out;
    for (const auto& sl : game.cr3Genes[size_t(k)].slots) {
        if (sl[1] < 0) continue;
        Cr3StatusRow r;
        if (sl[0] == 0) r.name = cr3NameOf("command_name.mbe", "CommandName", sl[1]), r.help = cr3NameOf("command_help.mbe", "CommandHelp", sl[1]);
        else r.name = cr3NameOf("skill_name.mbe", "SkillName", sl[1]), r.help = cr3NameOf("skill_help.mbe", "SkillHelp", sl[1]);
        out.push_back(r);
    }
    return out;
}

void Viewer::openCampStatus() {
    const std::vector<int> members = cr3PartyMembers();
    cr3Camp.stage = 8, cr3Camp.since = 0;
    campStatus = Cr3CampStatus();
    campStatus.chara = members.empty() ? 0 : members[0];
    SDL_Log("camp: status (%d)", campStatus.chara);
}

void Viewer::drawCampStatus(int w, int h, const Mat4& view) {
    const float t = cr3Camp.since;
    Cr3CampStatus& cs = campStatus;
    float x, y, cx, cy;
    const std::vector<Cr3StatusRow> rows = campStatusRows();
    UiFig& pos = cr3UiPut("camstaposition", "", t, 0, 0, view);
    if (cr3UiCall(pos, "call_camStaCut", x, y, w, h)) cr3UiPut("camstacut", "", float(cs.chara - 1) / 30, x + 1.92f, y - 2.56f, view);
    cr3::Cr3Fighter cf;
    // (the panels: statusEquipMenu's own camStaPosition plays its first 4 frames alone - their
    // joint slid in to x 0 -, where StatusMenu's plays on, its "motion" leaving to the left and
    // the picture moving right)
    poseUi(pos, "camstaposition", std::min(t, 4.0f / 30));
    if (cs.category == 1 && cr3CharaStats(cs.chara, cf) && cr3UiCall(pos, "call_camEqustaWPanel", x, y, w, h)) {
        UiFig& panel = cr3UiPut("camequstawpanel", "", 0, x, y, view);
        cr3UiLabel(panel, "typeM_L01", cr3GeneNameOf(cs.chara), w, h, 0.26f);
        drawCr3EquipNumbers(panel, 0, cf, cf, view, true);
    }
    if (cs.category == 0 && cr3CharaStats(cs.chara, cf) && cr3UiCall(pos, "call_camEqustaJPanel", x, y, w, h)) {
        const int mp = cr3CharaMp(cs.chara, cf.maxMp);
        UiFig& panel = cr3UiPut("camequstajpanel", "", 0, x, y, view);
        cr3UiLabel(panel, "typeM_L01", cr3GeneNameOf(cs.chara), w, h, 0.26f);
        if (cr3UiCall(panel, "call_camEquHp_per", cx, cy)) cr3UiPut("camequhp_per", "", 100.0f / 30, cx, cy, view);
        if (cr3UiCall(panel, "call_camEquMp_per", cx, cy)) cr3UiPut("cameqump_per", "", float(cf.maxMp > 0 ? mp * 100 / cf.maxMp : 0) / 30, cx, cy, view);
        const int values[6] = {cf.maxHp, mp, cf.attack, cf.defence, cf.speed, cf.luck};
        for (int v = 0; v < 6; ++v) {
            char joint[40];
            std::snprintf(joint, sizeof joint, "call_camEquNumSts_num00_%03d", v);
            if (!cr3UiCall(panel, joint, cx, cy)) continue;
            UiFig& num = cr3UiPut("camequnumsts_num00", "", 0, cx, cy, view);
            cr3UiDigits(num, "call_comNum00_fr00", "comnum00_fr00", values[v], 5, 0, view);
        }
    }
    // (the list: its categories, the rows)
    UiFig& lp = cr3UiPut("comlistposition", "comlistposition_io", t, 0, 0, view);
    if (cr3UiCall(lp, "call_comListMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
        float labelX = 0, labelY = -100, lx, ly;
        drawCr3Categories(menu, kStatusTabs, 4, cs.category, 400, labelX, labelY, view);
        if (cr3UiCall(menu, "call_comListPanel", lx, ly)) {
            const int n = int(rows.size());
            const int top = cs.row - 2; // (comListMenu: the focused row the third, the list past it)
            drawCr3ListBar(menu, false, cs.row, n, 1, view, 0.15f);
            drawCr3ListEnds(lx, ly, n, cs.row, view);
            for (int r = std::max(top, 0); r < n && r <= cs.row + 2; ++r) {
                Cr3FocusRow focusRow(*this, r == cs.row);
                UiFig& panel = cr3UiPut("comlistpanel", "", 0, cr3ListRowX(lx, r, cs.row), cr3ListRowY(ly, r, top, cs.row), view);
                if (r == cs.row && cr3UiCall(panel, "call_comListCursor", cx, cy)) cr3UiPut("comlistcursor", "", 0, cx, cy, view);
                if (rows[size_t(r)].icon >= 0 && cr3UiCall(panel, "call_comListIcon_fr00", cx, cy))
                    cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), rows[size_t(r)].icon), cx, cy, view);
                cr3UiLabel(panel, "typeM_L01", rows[size_t(r)].name, w, h, 0.3f);
                float ax, ay, bx, by;
                if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 300 + r});
            }
        }
        if (labelY > -100) {
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            drawUiText(menuAnnounce(kStatusTexts[cs.category]), 20, labelX / 10, (labelY - 0.1f) / 10, w, h, false, 1.0f);
        }
    }
    // (the characters' buttons, the back)
    UiFig& foot = cr3UiPut("comftrposition", "", 0, 0, 0, view);
    if (cr3UiCall(foot, "call_comFtrEquipMenu", x, y, w, h)) {
        UiFig& fm = cr3UiPut("comftrequipmenu", "", 0, x, y, view);
        const std::vector<int> members = cr3PartyMembers();
        for (int m = 0; m < 5 && m < int(members.size()); ++m) {
            if (!cr3UiCall(fm, "call_comFtrCharaBtn_fr00_00" + std::to_string(m + 1), cx, cy)) continue;
            const int ch = members[size_t(m)];
            const int frame = ch <= 5 ? ch - 1 + (ch == cs.chara ? 0 : 5) : ch == cs.chara ? 10 : 11;
            UiFig& b = cr3UiPut("comftrcharabtn_fr00", "", float(frame) / 30, cx, cy, view);
            float ax, ay, bx, by;
            if (cr3UiCall(b, "tapAreaS01", ax, ay) && cr3UiCall(b, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 700 + m});
        }
    }
    if (cr3UiCall(foot, "call_comBackMenu_001", x, y, w, h)) {
        UiFig& bm = cr3UiPut("combackmenu", "", 0, x, y, view);
        float ax, ay, ex, ey;
        if (cr3UiCall(bm, "call_comBack_fr00", cx, cy)) cr3UiPut("comback_fr00", "", 0, cx, cy, view);
        if (cr3UiCall(bm, "tapAreaS01", ax, ay) && cr3UiCall(bm, "tapAreaE01", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, -1});
    }
    drawCr3Header(t, 6, cs.row < int(rows.size()) ? rows[size_t(cs.row)].help : std::string(), w, h, view);
}

void Viewer::chooseCampStatus(int id) {
    Cr3CampStatus& cs = campStatus;
    if (id == -1) {
        audio.playSe("com_004", false);
        cr3Camp.stage = 1, cr3Camp.since = 0;
        return;
    }
    if (id >= 700 && id < 705) {
        const std::vector<int> members = cr3PartyMembers();
        if (id - 700 >= int(members.size()) || members[size_t(id - 700)] == cs.chara) return;
        cs.chara = members[size_t(id - 700)], cs.row = 0;
        audio.playSe("com_002", false);
        return;
    }
    if (id >= 400 && id < 404) {
        if (id - 400 == cs.category) return;
        cs.category = id - 400, cs.row = 0;
        audio.playSe("com_002", false);
        return;
    }
    if (id >= 300 && id < 400 && id - 300 != cs.row) cs.row = id - 300, audio.playSe("com_002", false);
}

void Viewer::campStatusKey(SDL_Keycode key) {
    Cr3CampStatus& cs = campStatus;
    const bool up = key == SDLK_UP, down = key == SDLK_DOWN, left = key == SDLK_LEFT, right = key == SDLK_RIGHT;
    if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) return chooseCampStatus(-1);
    if (key == SDLK_TAB) return chooseCampStatus(400 + (cs.category + 1) % 4);
    if (left || right) {
        const std::vector<int> members = cr3PartyMembers();
        const auto it = std::find(members.begin(), members.end(), cs.chara);
        if (members.empty() || it == members.end()) return;
        const int at = int(it - members.begin()), n = std::min(int(members.size()), 5);
        return chooseCampStatus(700 + (at + (right ? 1 : n - 1)) % n);
    }
    const int n = int(campStatusRows().size());
    if ((up || down) && n > 0) chooseCampStatus(300 + std::clamp(cs.row + (down ? 1 : -1), 0, n - 1));
}

bool Viewer::geneIndexKey(SDL_Keycode key) {
    if (!vitaButtons() || myPage.screen != 3 || cardView.gene) return false;
    auto& mp = myPage;
    const bool up = key == SDLK_UP, down = key == SDLK_DOWN, left = key == SDLK_LEFT, right = key == SDLK_RIGHT;
    const bool enter = key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE;
    if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) return chooseGeneIndex(-1), true;
    if (mp.profile > 0) {
        if (left || right) chooseGeneIndex(right ? 701 : 700);
        else if (up || down) {
            const auto* gi = game.cr3Db ? game.cr3Db->geneInfo(mp.profile) : nullptr;
            const int to = std::clamp(mp.rarity + (down ? 1 : -1), 0, gi ? gi->grade : 0);
            if (to != mp.rarity) chooseGeneIndex(800 + to);
        } else if (enter) chooseGeneIndex(903);
        return true;
    }
    if (bookTabsFocus) {
        if (up) bookTabsFocus = false;
        else if (left || right) chooseGeneIndex(600 + (mp.bookTab + (right ? 1 : 7)) % 8);
        return true;
    }
    // A spread has two pages, each a two-column, three-row grid.
    int had = 0;
    const auto cards = geneIndexCards(had);
    const int n = std::min(12, std::max(int(cards.size()) - mp.bookPage * 6, 0));
    bookCursor = std::clamp(bookCursor, 0, std::max(n - 1, 0));
    const int old = bookCursor, page = bookCursor / 6, at = bookCursor % 6;
    if (down && at / 2 == 2) bookTabsFocus = true;
    else if (up || down) bookCursor = std::clamp(bookCursor + (down ? 2 : -2), page * 6, std::min(page * 6 + 5, std::max(n - 1, 0)));
    else if (left || right) {
        if (right && at % 2 == 1 && page == 0 && n > 6) bookCursor += 5;
        else if (left && at % 2 == 0 && page == 1) bookCursor -= 5;
        else bookCursor = std::clamp(bookCursor + (right ? 1 : -1), page * 6, std::min(page * 6 + 5, std::max(n - 1, 0)));
    } else if (enter && n) chooseGeneIndex(1000 + mp.bookPage * 6 + bookCursor);
    if (old != bookCursor || bookTabsFocus) audio.playSe("com_002", false), SDL_Log("gene index: cursor %d%s", bookCursor, bookTabsFocus ? " tabs" : "");
    return true;
}

void Viewer::openGeneIndex() {
    bookCursor = 0, bookTabsFocus = false;
    Cr3MyPage& mp = myPage;
    mp.screen = 3, mp.screenSince = 0, mp.pressed = 0, mp.bookTab = 7, mp.bookPage = 0, mp.profile = 0;
    SDL_Log("my page: gene index (%zu lines)", game.cr3Db ? game.cr3Db->book().size() : size_t(0));
}

// the cards listed: (the line's _id, the gene shown - 0 none)
std::vector<std::pair<int, int>> Viewer::geneIndexCards(int& had) const {
    std::vector<std::pair<int, int>> out;
    had = 0;
    if (!game.cr3Db) return out;
    const int kind = myPage.bookTab == 7 ? -1 : myPage.bookTab == 6 ? 0 : myPage.bookTab + 1;
    for (const cr3::Cr3OfflineDb::BookLine& line : game.cr3Db->book()) {
        int best = 0;
        for (const int g : line.genes) {
            if (g <= 0 || !game.cr3GeneBook.count(g)) continue;
            const auto* gi = game.cr3Db->geneInfo(g);
            if (kind < 0 || (gi && gi->kind == kind)) best = g;
        }
        if (kind < 0 || best > 0) out.push_back({line.id, best});
        had += best > 0;
    }
    return out;
}

int Viewer::geneIndexNew() const { // (GetGeneCardNewnumber: the lines whose highest had isn't looked at)
    int had = 0, n = 0;
    const int tab = myPage.bookTab;
    const_cast<Cr3MyPage&>(myPage).bookTab = 7;
    for (const auto& [line, g] : geneIndexCards(had)) n += g > 0 && !game.cr3GeneSeen.count(g);
    const_cast<Cr3MyPage&>(myPage).bookTab = tab;
    return n;
}

std::vector<std::string> Viewer::geneProfileLines(int gene, int rarity) {
    std::vector<std::string> out;
    if (!game.cr3Db) return out;
    int entry = 0;
    const auto& book = game.cr3Db->book();
    for (size_t k = 0; k < book.size() && !entry; ++k)
        for (const int g : book[k].genes)
            if (g == gene) entry = int(k) + 1;
    std::string text = cardArtwork.originalDescription(gene, rarity, originalTextJapanese);
    if (text.empty()) text = announceText("collection_book" + std::to_string(rarity + 1), entry);
    std::string line;
    for (size_t k = 0; k < text.size() && out.size() < 12; ++k) {
        if (text[k] == '\\') out.push_back(line), line.clear(), ++k; // (Cr3UtilNPrint: a \ and the one after it end the line)
        else if (text[k] != '\n') line += text[k];
    }
    if (!line.empty() && out.size() < 12) out.push_back(line);
    return out;
}

void Viewer::drawGeneIndex(int w, int h, const Mat4& view) {
    Cr3MyPage& mp = myPage;
    float x, y, cx, cy;
    int had = 0;
    const std::vector<std::pair<int, int>> cards = geneIndexCards(had);
    const int total = game.cr3Db ? int(game.cr3Db->book().size()) : 0;
    const int pages = cards.empty() ? 1 : (int(cards.size()) - 1) / 6 + 1;
    mp.bookPage = std::clamp(mp.bookPage, 0, std::max((pages - 1) / 2 * 2, 0));
    const auto digits = [&](const UiFig& on, const char* prefix, const char* fig, int value, int places) {
        cr3UiDigits(on, prefix, fig, std::max(value, 0), places, 0, view);
    };
    if (mp.profile > 0) cr3CampTaps.push_back({-6, -4, 6, 4, 902}); // (outside the profile: it closes)
    UiFig& pos = cr3UiPut("mpcattoppositon", "", 0, 0, 0, view);
    if (cr3UiCall(pos, "call_mpCatTopBgMenu", x, y, w, h)) {
        UiFig& bg = cr3UiPut("mpcattopbgmenu", "", 0, x, y, view);
        float px, py;
        if (cr3UiCall(bg, "call_mpCatTopTitlePanel", px, py)) {
            UiFig& title = cr3UiPut("mpcattoptitlepanel", "", 0, px, py, view);
            cr3UiLabel(title, "typeM_L01", menuAnnounce(0x21c), w, h, 1.0f);
        }
        if (cr3UiCall(bg, "call_mpCatTopCompPanel", px, py)) {
            UiFig& comp = cr3UiPut("mpcattopcomppanel", "", 0, px, py, view);
            float tx, ty;
            if (cr3UiCall(comp, "typeM_L01", tx, ty)) {
                dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                drawUiText(menuAnnounce(0x21d), 20, (tx + 0.06f) / 10, (ty - 0.12f) / 10, w, h, true, 1.0f);
            }
            digits(comp, "call_mpCatNum01_fr00", "mpcatnum01_fr00", total ? had * 100 / total : 0, 3);
        }
        for (int k = 0; k < 2; ++k) { // (the page - the spread's right - and of how many)
            char joint[40];
            std::snprintf(joint, sizeof joint, "call_mpCatTopPage_num00_%03d", k + 1);
            if (!cr3UiCall(bg, joint, px, py)) continue;
            UiFig& num = cr3UiPut("mpcattoppage_num00", "", 0, px, py, view);
            digits(num, "call_mpCatNum02_fr00", "mpcatnum02_fr00", k == 0 ? std::min(mp.bookPage + 2, pages) : pages, 2);
        }
        for (int k = 0; k < 8; ++k) {
            char joint[40];
            std::snprintf(joint, sizeof joint, "call_mpCatTopMat_fr00_%03d", k + 1);
            if (!cr3UiCall(bg, joint, px, py)) continue;
            const bool on = k == mp.bookTab;
            UiFig& u = uiFig("mpcattopmat_fr00");
            u.fig.place = cr3::Mat34();
            u.fig.place.m[0] = u.fig.place.m[5] = u.fig.place.m[10] = on ? 1.0f : 0.75f;
            u.fig.place.m[3] = px, u.fig.place.m[7] = py;
            poseUi(u, "mpcattopmat_fr00", float(k) / 30);
            u.fig.tint[0] = u.fig.tint[1] = u.fig.tint[2] = on ? 1.0f : 0.5f;
            if (vitaButtons() && bookTabsFocus && on) u.fig.tint[0] = 0.5f, u.fig.tint[1] = 1, u.fig.tint[2] = 1.5f;
            drawFigure(u.fig, view.data());
            u.fig.tint[0] = u.fig.tint[1] = u.fig.tint[2] = 1.0f;
            float ax, ay, ex, ey;
            if (!mp.profile && cr3UiCall(u, "tapAreaS01", ax, ay) && cr3UiCall(u, "tapAreaE01", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, 600 + k});
        }
    }
    for (int m = 0; m < 2; ++m) {
        char joint[40];
        std::snprintf(joint, sizeof joint, "call_mpCatTopCardMenu_%03d", m + 1);
        if (!cr3UiCall(pos, joint, x, y, w, h)) continue;
        UiFig& menu = cr3UiPut("mpcattopcardmenu", "", 0, x, y, view);
        for (int k = 0; k < 6; ++k) {
            std::snprintf(joint, sizeof joint, "call_mpCatTopCardPanel_%03d", k + 1);
            float px, py;
            if (!cr3UiCall(menu, joint, px, py)) continue;
            const bool cursor = vitaButtons() && !mp.profile && !bookTabsFocus && bookCursor == m * 6 + k;
            if (cursor) cr3UiTint[0] = 0.5f, cr3UiTint[1] = 1, cr3UiTint[2] = 1.5f;
            UiFig& panel = cr3UiPut("mpcattopcardpanel", "", 0, px, py, view);
            cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
            const int at = (mp.bookPage + m) * 6 + k;
            if (at >= int(cards.size())) continue;
            const auto [line, gene] = cards[size_t(at)];
            // (PictureBookMenuCardPanel::Render: the card, then its number and new mark over it)
            if (gene > 0 && cr3UiCall(panel, "call_GeneCard", cx, cy)) drawCr3Card(gene, cx, cy, 1.0f, 3.6f, view), dev->clearDepth();
            if (cr3UiCall(panel, "call_mpCatTopNo_num00", cx, cy)) digits(cr3UiPut("mpcattopno_num00", "", 0, cx, cy, view), "call_mpCatNum03_fr00", "mpcatnum03_fr00", line, 3);
            if (gene <= 0) continue;
            if (!game.cr3GeneSeen.count(gene) && cr3UiCall(panel, "call_mpCatTopNew", cx, cy)) cr3UiPut("mpcattopnew", "mpcattopnew_loop", uiClock, cx, cy, view, 1, true);
            float ax, ay, ex, ey;
            if (!mp.profile && cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, 1000 + at});
        }
    }
    // (the profile over them)
    if (mp.profile > 0) {
        dev->clearDepth();
        UiFig& dp = cr3UiPut("mpcatdatapositon", "mpcatdatapositon_in", std::min(mp.profileSince, 5.0f / 30), 0, 0, view);
        if (cr3UiCall(dp, "call_mpCatDataMenu", x, y, w, h)) {
            UiFig& menu = cr3UiPut("mpcatdatamenu", "", 0, x, y, view);
            float ax, ay, ex, ey;
            if (cr3UiCall(menu, "tapAreaS01", ax, ay) && cr3UiCall(menu, "tapAreaE01", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, 903}); // (the card: its page)
            float tx, ty;
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            // (SetStringData: "Profile" right-aligned, the name centred, the lines from the left - white)
            if (cr3UiCall(menu, "typeM_R01", tx, ty)) {
                const std::string label = menuAnnounce(0x21e);
                drawUiText(label, 24, (tx - 0.06f) / 10 - uiTextWidth(label, 24, w, h), (ty - 0.12f) / 10, w, h, true, 1.0f);
            }
            if (cr3UiCall(menu, "typeM_C01", tx, ty)) drawUiText(cr3NameOf("gene_name.mbe", "GeneName", mp.profile), 22, tx / 10, (ty - 0.12f) / 10, w, h, false, 0.25f, 0.19f);
            const std::vector<std::string> lines = geneProfileLines(mp.profile, mp.rarity);
            for (size_t k = 0; k < lines.size(); ++k) {
                char joint[16];
                std::snprintf(joint, sizeof joint, "typeM_L%02d", int(k) + 1);
                // (18: the widest of the 690 texts' lines, 5.42 units at 22, fits the box's 4.3 - narrowed
                // past that only)
                if (cr3UiCall(menu, joint, tx, ty)) drawUiText(lines[k], 18, (tx + 0.06f) / 10, (ty - 0.1f) / 10, w, h, true, 1.0f, 0.43f);
            }
            if (cr3UiCall(menu, "call_GeneCard", cx, cy)) drawCr3Card(mp.profile, cx, cy, 1.0f, 5.6f, view);
            const auto* gi = game.cr3Db ? game.cr3Db->geneInfo(mp.profile) : nullptr;
            for (int k = 0; gi && k <= gi->grade && k < 5; ++k) {
                char joint[40];
                std::snprintf(joint, sizeof joint, "call_mpCatRarity_fr00_%03d", k + 1);
                if (!cr3UiCall(menu, joint, cx, cy)) continue;
                UiFig& b = cr3UiPut("mpcatrarity_fr00", "", float(k == mp.rarity ? 2 * k : 2 * k + 1) / 30, cx, cy, view);
                if (cr3UiCall(b, "tapAreaS01", ax, ay) && cr3UiCall(b, "tapAreaE01", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, 800 + k});
            }
        }
    }
    if ((pages > 2 || mp.profile > 0) && cr3UiCall(pos, "call_mpCatTopArrowMenu", x, y, w, h)) {
        UiFig& arrows = cr3UiPut("mpcattoparrowmenu", "mpcattoparrowmenu_loop", uiClock, x, y, view, 1, true);
        float ax, ay, ex, ey;
        if (cr3UiCall(arrows, "tapAreaS01", ax, ay) && cr3UiCall(arrows, "tapAreaE01", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, 700});
        if (cr3UiCall(arrows, "tapAreaS02", ax, ay) && cr3UiCall(arrows, "tapAreaE02", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, 701});
    }
    UiFig& foot = cr3UiPut("comftrposition", "", 0, 0, 0, view); // (ComBackButton)
    if (cr3UiCall(foot, "call_comBackMenu_001", x, y, w, h)) {
        UiFig& bm = cr3UiPut("combackmenu", "", 0, x, y, view);
        float ax, ay, ex, ey;
        if (cr3UiCall(bm, "call_comBack_fr00", cx, cy)) cr3UiPut("comback_fr00", "", 0, cx, cy, view);
        if (cr3UiCall(bm, "tapAreaS01", ax, ay) && cr3UiCall(bm, "tapAreaE01", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, -1});
    }
}

void Viewer::openCr3CardView(int gene) {
    const auto* gi = game.cr3Db ? game.cr3Db->geneInfo(gene) : nullptr;
    if (!gi) return;
    char names[3][16];
    std::snprintf(names[0], sizeof names[0], "%03d.jpg", gi->series);
    std::snprintf(names[1], sizeof names[1], "r_%02d.png", gi->grade);
    std::snprintf(names[2], sizeof names[2], "e_%02d.png", gi->kind);
    cr3::Image img[3];
    for (int k = 0; k < 3; ++k) {
        std::vector<uint8_t> data;
        std::string err;
        const bool read = k == 0 && cardArtwork.readPicture(gi->series, data);
        if ((!read && !archive.read(names[k], data, &err)) || !cr3::decodePicture(data, img[k], &err)) {
            SDL_Log("gene card %d: %s: %s", gene, names[k], err.c_str());
            return;
        }
    }
    closeCr3CardView();
    for (int k = 0; k < 3; ++k) img[k].mipmaps = false, cardView.layers[k] = dev->upload(img[k]);
    cardView.gene = gene;
    SDL_Log("gene card %d: %s, %s, %s", gene, names[0], names[1], names[2]);
}

void Viewer::closeCr3CardView() {
    for (cr3::Texture& t : cardView.layers)
        if (t.id) dev->destroy(t), t = cr3::Texture();
    cardView.gene = 0;
}

void Viewer::drawCr3CardView() {
    if (!cardView.gene || !white.id) return;
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    dev->clearDepth();
    const Mat4 id = cr3::mat4Identity();
    static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
    static const float kBlack[4] = {0, 0, 0, 1}, kWhite[4] = {1, 1, 1, 1};
    fullFrameQuad(white, kScreen, kBlack);
    const float pw = 938, ph = 640; // (the pictures': originalAspect 640 / 938)
    const float sw = float(screenW), sh = float(screenH);
#ifdef SDL_PLATFORM_ANDROID
    const float cw = ph / pw > sh / sw ? sh * pw / ph : sw, ch = ph / pw > sh / sw ? sh : sw * ph / pw;
    const float x0 = -cw / sw, x1 = cw / sw, y0 = 1, y1 = 1 - 2 * ch / sh; // (v 0 the top row)
    const float v[24] = {x0, y1, 0, 1, x1, y1, 1, 1, x1, y0, 1, 0, x0, y1, 0, 1, x1, y0, 1, 0, x0, y0, 0, 0};
#else
    // (upright: its top-left the picture's top-right, its bottom-right the picture's bottom-left)
    const float ch = pw / ph > sh / sw ? sh : sw * pw / ph, cw = pw / ph > sh / sw ? sh * ph / pw : sw;
    const float x0 = -cw / sw, x1 = cw / sw, y0 = 1, y1 = 1 - 2 * ch / sh;
    const float v[24] = {x0, y1, 0, 0, x1, y1, 0, 1, x1, y0, 1, 1, x0, y1, 0, 0, x1, y0, 1, 1, x0, y0, 1, 0};
#endif
    for (const cr3::Texture& t : cardView.layers)
        if (t.id) dev->drawQuad(t, v, id.data(), kWhite);
    cr3CampTaps.clear();
    cr3CampTaps.push_back({-100, -100, 100, 100, 904}); // (the page: a tap anywhere closes it)
}

void Viewer::chooseGeneIndex(int id) {
    Cr3MyPage& mp = myPage;
    if (cardView.gene) { // (the card's page: any tap closes it)
        if (id == 904 || id == -1) closeCr3CardView();
        return;
    }
    if (id == 903 && mp.profile > 0) {
        audio.playSe("com_003", false); // (PictureBookMenu: the card's status)
        return openCr3CardView(mp.profile);
    }
    int had = 0;
    const std::vector<std::pair<int, int>> cards = geneIndexCards(had);
    const int pages = cards.empty() ? 1 : (int(cards.size()) - 1) / 6 + 1;
    if (id == -1 || id == 902) {
        audio.playSe(id == -1 ? "com_004" : "com_003", false);
        if (mp.profile > 0) mp.profile = 0;
        else mp.screen = 0, mp.screenSince = 0, mp.pressed = 0, SDL_Log("my page: gene index closed");
        return;
    }
    if (id >= 600 && id < 608 && !mp.profile) { // (a tab: SetGeneData by its kind)
        if (id - 600 == mp.bookTab) return;
        audio.playSe("com_003", false);
        mp.bookTab = id - 600, mp.bookPage = 0;
        return;
    }
    if (id == 700 || id == 701) {
        audio.playSe("com_003", false);
        const int dir = id == 700 ? -1 : 1;
        if (mp.profile > 0) { // (GetPictureBookMenuGeneID: the list's next or last gene had)
            std::vector<int> genes;
            for (const auto& c : cards)
                if (c.second > 0) genes.push_back(c.second);
            const auto it = std::find(genes.begin(), genes.end(), mp.profile);
            if (genes.empty() || it == genes.end()) return;
            const int at = int(it - genes.begin());
            mp.profile = genes[size_t((at + dir + int(genes.size())) % int(genes.size()))];
            mp.rarity = 0, mp.profileSince = 0;
            game.cr3GeneSeen.insert(mp.profile); // (SetNewOffPictureBookMenu)
            return;
        }
        const int spreads = (pages + 1) / 2;
        mp.bookPage = ((mp.bookPage / 2 + dir + spreads) % spreads) * 2;
        return;
    }
    if (id >= 800 && id < 805 && mp.profile > 0) { // (SetRarityText: com_005 the chosen again)
        if (id - 800 == mp.rarity) return (void)audio.playSe("com_005", false);
        audio.playSe("com_003", false);
        mp.rarity = id - 800;
        return;
    }
    if (id >= 1000 && id - 1000 < int(cards.size()) && !mp.profile) {
        const int gene = cards[size_t(id - 1000)].second;
        if (gene <= 0) return;
        audio.playSe("com_003", false);
        game.cr3GeneSeen.insert(gene); // (CheckTapCard: looked at)
        mp.profile = gene, mp.rarity = 0, mp.profileSince = 0;
        SDL_Log("my page: gene index - profile of %d", gene);
    }
}

// the Options screen over the title or a battle (CR3-U's: the game has it in the camp alone);
// back, back to that
void Viewer::openCr3OptionsFrom(int from) {
    const int hands = campOptions.hands;
    cr3Camp.stage = 9, cr3Camp.since = 0;
    campOptions = Cr3CampOptions();
    campOptions.hands = hands, campOptions.from = from;
    audio.playSe("com_003", false);
    SDL_Log("options: open (from the %s)", from == 1 ? "title" : "battle");
}

bool Viewer::cr3OptionsOver() const { return cr3Camp.stage == 9 && campOptions.from != 0; }

void Viewer::drawCr3OptionsOver(int w, int h) {
    if (!cr3OptionsOver()) return;
    cr3CampTaps.clear();
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    drawCr3Camp(w, h, view);
}

// a click while it's over the title or a battle (all its), or on the title's Options button
bool Viewer::cr3OptionsClick(float fx, float fy) {
    const bool over = cr3OptionsOver(), title = cr3Title.stage == 2 && cr3Camp.stage == 0;
    if (!over && !title) return false;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    for (auto it = cr3CampTaps.rbegin(); it != cr3CampTaps.rend(); ++it)
        if (ux >= std::min(it->x0, it->x1) && ux <= std::max(it->x0, it->x1) && uy >= std::min(it->y0, it->y1) && uy <= std::max(it->y0, it->y1)) {
            if (it->id == kTapTitleOptions) openCr3OptionsFrom(1);
            else chooseCr3Camp(it->id);
            return true;
        }
    return over;
}

// (tests: --input camp:cat<n> / id<n> / back over the title or a battle, as the camp's)
void Viewer::cr3OptionsInput() {
    if (inputScript.empty() || inputScript.front().compare(0, 5, "camp:") != 0 || uiClock < inputNext) return;
    const std::string want = inputScript.front().substr(5);
    inputScript.pop_front();
    inputNext = uiClock + 0.5f;
    if (want == "import") chooseCr3Camp(1300);
    else if (want == "yes") chooseCr3Camp(1301);
    else if (want == "no") chooseCr3Camp(1302);
    else if (want == "ok") chooseCr3Camp(1303);
    else if (want == "back") chooseCr3Camp(-1);
    else if (want.compare(0, 3, "cat") == 0) chooseCr3Camp(400 + std::atoi(want.c_str() + 3));
    else if (want.compare(0, 2, "id") == 0) chooseCr3Camp(std::atoi(want.c_str() + 2));
}

void Viewer::openCampOptions() {
    cr3Camp.stage = 9, cr3Camp.since = 0;
    campOptions.tab = 0;
    SDL_Log("camp: options");
}

void Viewer::drawCr3OptionTabs(int chosen, float x, float y, int w, int h, const Mat4& view) {
    const float half = 1.095f * cr3UiScale; // (half a tab: the bar's dividers at -+1.095)
    UiFig& tab = uiFig("opttab_fr00");
    auto jointOf = [&](const ViewMesh& m) {
        const std::vector<uint32_t>& p = m.source.palette;
        return p.empty() || p[0] >= tab.joints.size() ? std::string() : tab.joints[p[0]];
    };
    float tx[kOptionTabs] = {}, ty[kOptionTabs] = {};
    bool placed[kOptionTabs] = {};
    for (int pass = 0; pass < 2; ++pass) {
        const bool lit = pass == 1, right = lit == (chosen >= 2);
        for (ViewMesh& m : tab.fig.meshes) {
            const std::string j = jointOf(m);
            const bool highlight = j.compare(0, 4, "tap_") == 0;
            m.hidden = (highlight && !lit) || j == (right ? "bg_01" : "bg_03") || j == (right ? "bg_04" : "bg_06") ||
                       j == (right ? "frame_01" : "frame_03") || j == (right ? "tap_01" : "tap_03_02");
        }
        const int frame = !lit ? 0 : chosen == 0 ? 0 : chosen == 3 ? 2 : 1;
        cr3UiPut("opttab_fr00", "", float(frame) / 30, x + (right ? half : -half), y, view);
        for (int k = 0; k < 2; ++k) {
            const int own = right ? k + 2 : k + 1, at = right ? k + 2 : k; // (the bar's tab 1 .. 3; ours 0 .. 3)
            float ax, ay, bx, by;
            if (cr3UiCall(tab, "typeM_C0" + std::to_string(own), tx[at], ty[at])) placed[at] = true;
            if (cr3UiCall(tab, "tapAreaS0" + std::to_string(own), ax, ay) && cr3UiCall(tab, "tapAreaE0" + std::to_string(own), bx, by))
                cr3CampTaps.push_back({ax, ay, bx, by, 400 + at});
        }
    }
    for (ViewMesh& m : tab.fig.meshes) m.hidden = false;
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    for (int k = 0; k < kOptionTabs; ++k) // (white on the tabs' blue: the chosen one's dark, the others' a lighter one - dimmed a little)
        if (placed[k])
            drawUiText(k < 3 ? menuAnnounce(0x190 + k) : std::string("Graphics"), 24, tx[k] / 10, (ty[k] - 0.12f * cr3UiScale) / 10, w, h, false,
                       k == chosen ? 1.0f : 0.8f);
}

// the Options' scale (CR3-U's): on a phone or tablet the game's; on a desktop smaller as the
// window grows - about 78 pixels a unit at the display's scale -, so more fits (the header and
// the footer as they are)
float Viewer::cr3OptionsScale(int w, int h) const {
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    (void)w, (void)h;
    return 1.0f;
#else
    const float k = std::min(float(w) / 9.6f, float(h) / 6.4f);
    const float display = window ? std::max(SDL_GetWindowDisplayScale(window), 1.0f) : 1.0f;
    return std::clamp(78.0f * display / std::max(k, 1.0f), 0.55f, 1.0f);
#endif
}

// the lowest a row reaches: over the footer
float Viewer::cr3OptionsFloor(int w, int h) const {
    const float k = std::min(float(w) / 9.6f, float(h) / 6.4f);
    return -float(h) / k / 2 + 0.75f;
}

// the Graphics tab: CR3-U's settings (settingRows) as the Other tab's Resolution - optSltLRPanel
// rows, the setting at typeM_L00, its value between optSltLbtn and optSltRbtn -, as many as fit over
// the footer (four on a phone or tablet) about the focused one (tinted), arrows right of the first
// and last when there are more; the focused one's help on the header's line
void Viewer::drawCr3GraphicsTab(const UiFig& menu, int w, int h, const Mat4& view) {
    Cr3CampOptions& co = campOptions;
    const std::vector<SettingRow> rows = settingRows();
    const int n = int(rows.size()), focus = std::clamp(settingsCursor, 0, std::max(n - 1, 0));
    const float s = cr3UiScale;
    float x1, y1, x2, y2, cx, cy, ax, ay, bx, by, tx, ty;
    if (!cr3UiCall(menu, "call_optSltPanel_001", x1, y1) || !cr3UiCall(menu, "call_optSltPanel_002", x2, y2)) return;
    const float pitch = std::abs(y1 - y2);
    int shown = kGraphicsRows;
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    if (cr3UiCall(cr3UiPose("optsltlrpanel", "", 0, x1, y1), "typeM_C01", tx, ty) && pitch > 0)
        shown = std::max(1 + int((ty - pitch * 0.5f - cr3OptionsFloor(w, h)) / pitch), 1);
#endif
    shown = std::min(shown, n);
    co.top = std::clamp(co.top, std::max(focus - (shown - 1), 0), focus);
    co.top = std::min(co.top, std::max(n - shown, 0));
    auto tint = [&](float r, float g, float b) { cr3UiTint[0] = r, cr3UiTint[1] = g, cr3UiTint[2] = b; };
    float firstY = 0, lastY = 0, rightX = 0; // (the value's line on the first and last rows shown; the arrows' right end)
    for (int k = co.top; k < n && k < co.top + shown; ++k) {
        const SettingRow& r = rows[size_t(k)];
        const float px = x1 + (x2 - x1) * float(k - co.top), py = y1 + (y2 - y1) * float(k - co.top);
        if (k == focus) tint(0.82f, 0.9f, 1.0f);
        UiFig& panel = cr3UiPut("optsltlrpanel", "", 0, px, py, view);
        tint(1, 1, 1);
        if (!r.enabled) tint(0.549f, 0.549f, 0.655f); // (one that can't change now: its arrows grey, its text light)
        cr3UiLabel(panel, "typeM_L00", r.label, w, h, r.enabled ? 0.26f : 0.62f);
        if (cr3UiCall(panel, "typeM_C01", tx, ty)) {
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            drawUiText(r.value, 24, tx / 10, (ty - 0.12f * s) / 10, w, h, false, r.enabled ? 0.26f : 0.62f, 0.25f * s);
            if (k == co.top) firstY = ty - 0.12f * s;
            lastY = ty - 0.12f * s;
        }
        if (cr3UiCall(panel, "call_optSltLbtn", cx, cy)) {
            UiFig& l = cr3UiPut("optsltlbtn", "", 0, cx, cy, view);
            const bool lt = cr3UiCall(l, "tapAreaS01", ax, ay) && cr3UiCall(l, "tapAreaE01", bx, by);
            if (lt && r.enabled) {
                cr3CampTaps.push_back({ax, ay, ax + 3.0f * s, by, kTapGraphicsRow + k * 2 + 1}); // (the box between the arrows: on)
                cr3CampTaps.push_back({ax, ay, bx, by, kTapGraphicsRow + k * 2});
            }
            UiFig& rb = cr3UiPut("optsltrbtn", "", 0, cx + 3.0f * s, cy, view); // (as the Resolution's: 3 to the right)
            if (cr3UiCall(rb, "tapAreaS01", ax, ay) && cr3UiCall(rb, "tapAreaE01", bx, by)) {
                if (r.enabled) cr3CampTaps.push_back({ax, ay, bx, by, kTapGraphicsRow + k * 2 + 1});
                rightX = std::max(ax, bx);
            }
        }
        tint(1, 1, 1);
    }
    // (more over or under: an arrow right of the first or last row shown, a tap on it a row on)
    const float at = rightX + 0.45f * s, reach = pitch * 0.45f;
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    if (co.top > 0) {
        drawUiText("▲", 22, at / 10, firstY / 10, w, h, false, 0.4f);
        cr3CampTaps.push_back({at - 0.3f * s, firstY - reach, at + 0.3f * s, firstY + reach, kTapGraphicsUp});
    }
    if (co.top + shown < n) {
        drawUiText("▼", 22, at / 10, lastY / 10, w, h, false, 0.4f);
        cr3CampTaps.push_back({at - 0.3f * s, lastY - reach, at + 0.3f * s, lastY + reach, kTapGraphicsDown});
    }
}

// CR3-U's Reset, left of the back: the game's round button (comBtnSPanel, its comBtnBg_fr00)
// with comBtnIcon_fr00's circling arrows (frame 1), "Reset" under them; it asks, then puts the
// tab's settings back
void Viewer::drawCr3OptionsReset(float backX, float backY, int w, int h, const Mat4& view) {
    constexpr float kSize = 0.66f;
    float cx, cy, ex, ey;
    cr3UiScale = kSize;
    UiFig& probe = cr3UiPose("combtnspanel", "", 0, 0, 0);
    if (!cr3UiCall(probe, "tapAreaCP01", cx, cy) || !cr3UiCall(probe, "tapAreaAP01", ex, ey)) return (void)(cr3UiScale = 1);
    const float r = std::hypot(ex - cx, ey - cy), px = backX - 0.15f - r - cx, py = backY - cy;
    UiFig& panel = cr3UiPut("combtnspanel", "", 0, px, py, view);
    float ix, iy;
    if (cr3UiCall(panel, "call_comBtnBg_fr00", ix, iy)) cr3UiPut("combtnbg_fr00", "", 0, ix, iy, view);
    if (cr3UiCall(panel, "call_comBtnIconS_fr00", ix, iy)) {
        cr3UiScale = kSize * 0.82f;
        cr3UiPut("combtnicon_fr00", "", 1.0f / 30, ix, iy + r * 0.2f, view);
    }
    cr3UiScale = 1;
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    drawUiText("Reset", 11, (px + cx) / 10, (py + cy - r * 0.5f) / 10, w, h, false, 1.0f, r * 1.3f / 10);
    cr3CampTaps.push_back(circleTap(px + cx, py + cy, px + cx + r, py + cy, kTapOptionsReset));
}

std::string Viewer::cr3OptionsResetAsk() const {
    static const char* const kTab[kOptionTabs] = {"sound", "control", "other", "graphics"};
    return std::string("Put the ") + kTab[std::clamp(campOptions.tab, 0, kOptionTabs - 1)] + " settings back to their defaults?";
}

void Viewer::cr3OptionsReset() {
    Cr3CampOptions& co = campOptions;
    if (co.tab == 0) {
        for (int& v : optionVolume) v = 100;
        audio.setVolumes(1, 1, 1);
    } else if (co.tab == 1) co.hands = 0;
    else if (co.tab == 2) optionQuality = 0;
    else { // (the window's type and resolution kept)
        const int display = settings.display, resolution = settings.resolution;
        settings = Settings();
        settings.display = display, settings.resolution = resolution;
        applySettings();
        saveSettings();
    }
    SDL_Log("options: tab %d reset", co.tab);
}

void Viewer::saveImportAnswer(bool yes) {
    const bool finished = saveImport.state == vitaimport::Session::Result && saveImport.succeeded;
    saveImport.answer(yes, std::filesystem::path(savePath()).parent_path().u8string());
    if (finished) {
        systemRead = systemFound = false;
        systemFlags.clear();
        cr3Camp.stage = 0;
        campOptions.from = 0;
        startCr3Title();
    }
}

void Viewer::drawCampOptions(int w, int h, const Mat4& view) {
    saveImport.poll();
    const float t = cr3Camp.since;
    Cr3CampOptions& co = campOptions;
    float x, y, cx, cy;
    // (the tabs and their settings scaled about the tabs' height: they stay under the header)
    const float s = cr3OptionsScale(w, h);
    constexpr float kPivot = 2.1f;
    cr3UiScale = s, uiTextScale = s;
    UiFig& top = cr3UiPut("opttoppositon", "opttoppositon_io", t, 0, kPivot * (1 - s), view);
    if (cr3UiCall(top, "call_optTab_fr00", x, y, w, h)) drawCr3OptionTabs(co.tab, x, y, w, h, view);
    if (co.tab == 0 && cr3UiCall(top, "call_optSltMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("optsltmenu", "", 0, x, y, view);
        for (int k = 0; k < 3; ++k) {
            float px, py;
            if (!cr3UiCall(menu, "call_optSltPanel_00" + std::to_string(k + 1), px, py)) continue;
            if (vitaButtons() && !co.footer && k == optionFocus) cr3UiTintMaterial = "mat_opt_btn_01", cr3UiMaterialTint[0] = 0.5f, cr3UiMaterialTint[1] = 1, cr3UiMaterialTint[2] = 1.5f;
            UiFig& panel = cr3UiPut("optsltpanel", "", 0, px, py, view);
            cr3UiTintMaterial.clear();
            cr3UiLabel(panel, "typeM_L00", menuAnnounce(0x198 + k), w, h, 0.26f);
            if (!cr3UiCall(panel, "call_optSltVolume", cx, cy)) continue;
            UiFig& vol = cr3UiPut("optsltvolume", "", 0, cx, cy, view);
            const int v = std::clamp(optionVolume[k], 0, 100);
            float bx, by, ux, uy, lx, ly;
            if (cr3UiCall(vol, "call_optSltBar_fr00", bx, by)) cr3UiPut("optsltbar_fr00", "", float(v) / 30, bx, by, view);
            if (cr3UiCall(vol, "barLimitUp", ux, uy) && cr3UiCall(vol, "barLimitLow", lx, ly)) {
                const float from = ux + 0.23f * s, to = lx - 0.23f * s;
                float kx, ky;
                if (cr3UiCall(vol, "call_optSltBarCursor", kx, ky)) cr3UiPut("optsltbarcursor", "", 0, kx + (to - from) * float(v) / 100.0f, ky, view);
                for (int step = 0; step <= 10; ++step) { // (a tap along the bar: its tenth)
                    const float sx = from + (to - from) * float(step) / 10.0f;
                    cr3CampTaps.push_back({sx - (to - from) / 20.0f, uy, sx + (to - from) / 20.0f, ly, 800 + k * 20 + step});
                }
            }
        }
    }
    if (co.tab == 1 && cr3UiCall(top, "call_optHndMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("opthndmenu", "", 0, x, y, view);
        for (int k = 0; k < 2; ++k) {
            if (!cr3UiCall(menu, "call_optHnd_fr00_00" + std::to_string(k + 1), cx, cy)) continue;
            UiFig& hnd = cr3UiPut("opthnd_fr00", "", float(k * 2 + (k == co.hands ? 1 : 0)) / 30, cx, cy, view);
            float tx, ty;
            if (cr3UiCall(hnd, "typeM_C01", tx, ty)) {
                dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                drawUiText(menuAnnounce(0x19b + k), 24, tx / 10, (ty - 0.12f * s) / 10, w, h, false, 0.26f);
            }
            float ax, ay, bx, by;
            if (cr3UiCall(hnd, "tapAreaS01", ax, ay) && cr3UiCall(hnd, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 500 + k});
        }
    }
    if (co.tab == 2 && cr3UiCall(top, "call_optSltMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("optsltmenu", "", 0, x, y, view);
        float px, py;
        if (cr3UiCall(menu, "call_optSltPanel_001", px, py)) {
            UiFig& panel = cr3UiPut("optsltlrpanel", "", 0, px, py, view);
            cr3UiLabel(panel, "typeM_L00", menuAnnounce(0x230), w, h, 0.26f);
            float tx, ty;
            if (cr3UiCall(panel, "typeM_C01", tx, ty)) {
                dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                drawUiText(menuAnnounce(0x22b + std::clamp(optionQuality, 0, 2)), 24, tx / 10, (ty - 0.12f * s) / 10, w, h, false, 0.26f);
            }
            if (cr3UiCall(panel, "call_optSltLbtn", cx, cy)) {
                UiFig& l = cr3UiPut("optsltlbtn", "", 0, cx, cy, view);
                float ax, ay, bx, by;
                if (cr3UiCall(l, "tapAreaS01", ax, ay) && cr3UiCall(l, "tapAreaE01", bx, by)) {
                    cr3CampTaps.push_back({ax, ay, ax + 3.0f * s, by, 601}); // (the box between the arrows: on)
                    cr3CampTaps.push_back({ax, ay, bx, by, 600});
                }
                // (its right button over the other end: optSltRbtn, as wide)
                UiFig& r = cr3UiPut("optsltrbtn", "", 0, cx + 3.0f * s, cy, view);
                if (cr3UiCall(r, "tapAreaS01", ax, ay) && cr3UiCall(r, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 601});
            }
            // CR3-U's Language (the Vita's options had none): the copy's languages - its own, each
            // fan translation over it -, each in its own words, the arrows as Resolution's
            float below = 0.85f * s;
            if (languages.size() > 1) {
                UiFig& lang = cr3UiPut("optsltlrpanel", "", 0, px, py - below, view);
                cr3UiLabel(lang, "typeM_L00", menuAnnounce(102), w, h, 0.26f); // ("Language", "言語", "Idioma")
                if (cr3UiCall(lang, "typeM_C01", tx, ty)) {
                    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                    const cr3::Language id = languages[languageIndex].id;
                    const char* name = id == cr3::Language::Japanese ? "日本語" : id == cr3::Language::FanSpanish ? "Español" : "English";
                    drawUiText(name, 24, tx / 10, (ty - 0.12f * s) / 10, w, h, false, 0.26f);
                }
                if (cr3UiCall(lang, "call_optSltLbtn", cx, cy)) {
                    UiFig& l = cr3UiPut("optsltlbtn", "", 0, cx, cy, view);
                    float ax, ay, bx, by;
                    if (cr3UiCall(l, "tapAreaS01", ax, ay) && cr3UiCall(l, "tapAreaE01", bx, by)) {
                        cr3CampTaps.push_back({ax, ay, ax + 3.0f * s, by, 1401}); // (the box between the arrows: on)
                        cr3CampTaps.push_back({ax, ay, bx, by, 1400});
                    }
                    UiFig& r = cr3UiPut("optsltrbtn", "", 0, cx + 3.0f * s, cy, view);
                    if (cr3UiCall(r, "tapAreaS01", ax, ay) && cr3UiCall(r, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 1401});
                }
                below += 0.85f * s;
            }
            UiFig& imp = cr3UiPut("optsltlrpanel", "", 0, px, py - below, view);
            cr3UiLabel(imp, "typeM_L00", "Vita Saves", w, h, 0.26f);
            if (cr3UiCall(imp, "typeM_C01", tx, ty)) {
                dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                const std::string label = co.from != 1 ? "Title only" : co.otherRow == 2 ? "> Import <" : "Import";
                drawUiText(label, 24, tx / 10, (ty - 0.12f * s) / 10, w, h, false, 0.26f);
            }
            if (cr3UiCall(imp, "call_optSltLbtn", cx, cy)) {
                UiFig& r = cr3UiPut("optsltrbtn", "", 0, cx + 3.0f * s, cy, view);
                float ax, ay, bx, by;
                if (co.from == 1 && cr3UiCall(r, "tapAreaS01", ax, ay) && cr3UiCall(r, "tapAreaE01", bx, by))
                    cr3CampTaps.push_back({ax - 3.0f * s, ay, bx, by, 1300});
            }
        }
    }
    if (co.tab == 3 && cr3UiCall(top, "call_optSltMenu", x, y, w, h)) drawCr3GraphicsTab(cr3UiPut("optsltmenu", "", 0, x, y, view), w, h, view);
    cr3UiScale = 1, uiTextScale = 1;
    // (the back, and CR3-U's Reset left of it)
    UiFig& foot = cr3UiPut("comftrposition", "", 0, 0, 0, view);
    if (cr3UiCall(foot, "call_comBackMenu_001", x, y, w, h)) {
        UiFig& bm = cr3UiPut("combackmenu", "", 0, x, y, view);
        float ax, ay, ex, ey;
        if (cr3UiCall(bm, "call_comBack_fr00", cx, cy)) cr3UiPut("comback_fr00", "", 0, cx, cy, view);
        if (cr3UiCall(bm, "tapAreaS01", ax, ay) && cr3UiCall(bm, "tapAreaE01", ex, ey)) {
            cr3CampTaps.push_back({ax, ay, ex, ey, -1});
            drawCr3OptionsReset(std::min(ax, ex), (ay + ey) / 2, w, h, view);
        }
    }
    std::string line = co.tab < 3 ? menuAnnounce(0x19f + co.tab) : std::string("Change graphics settings.");
    if (co.tab == 3) { // (Graphics: the focused setting's help)
        const std::vector<SettingRow> rows = settingRows();
        if (settingsCursor >= 0 && settingsCursor < int(rows.size())) line = rows[size_t(settingsCursor)].help;
    }
    if (co.tab == 2 && co.otherRow == 2) line = co.from == 1 ? "Import decrypted Vita saves (I). Existing saves are backed up." : "Import saves from the title screen's Options.";
    if (co.tab == 2 && co.otherRow == 1) line = "The language of the game's text: its own, or a fan translation over it.";
    drawCr3Header(t, 7, line, w, h, view);
    if (co.asking) { // (the Reset's question over all; its Yes / No the only taps)
        cr3CampTaps.clear();
        drawCr3Asked(cr3OptionsResetAsk(), co.askSince, co.askFocus, true, kTapResetYes, kTapResetNo, w, h, view);
    }
    if (saveImport.state == vitaimport::Session::Confirm || saveImport.state == vitaimport::Session::Result) {
        cr3CampTaps.clear();
        std::string message = saveImport.message;
        for (size_t at = 0; (at = message.find('\n', at)) != std::string::npos; at += 2) message.replace(at, 1, "\\n");
        if (saveImport.state == vitaimport::Session::Confirm)
            drawCr3Asked(message, 1.0f, saveImport.focus, true, 1301, 1302, w, h, view);
        else drawCr3Told(message, 1.0f, 1303, w, h, view);
    }
}

void Viewer::chooseCampOptions(int id) {
    if (saveImport.active()) {
        if (saveImport.state == vitaimport::Session::Picking) {
            if (id == -1) saveImport.cancel();
        } else if (id == 1301) saveImportAnswer(true);
        else if (id == 1302 || id == 1303 || id == -1) saveImportAnswer(false);
        return;
    }
    if (id == 1300) {
        if (campOptions.from == 1) saveImport.begin("cr3", SDL_GetKeyboardFocus());
        return;
    }

    Cr3CampOptions& co = campOptions;
    if (co.asking) { // (the Reset's question: Yes puts the tab's settings back)
        if (id != kTapResetYes && id != kTapResetNo && id != -1) return;
        co.asking = false;
        audio.playSe(id == kTapResetYes ? "com_003" : "com_004", false);
        if (id == kTapResetYes) cr3OptionsReset();
        return;
    }
    if (id == kTapOptionsReset) {
        co.asking = true, co.askSince = 0, co.askFocus = 1; // (No first)
        audio.playSe("com_003", false);
        return;
    }
    const int settingCount = int(settingRows().size());
    if (id == kTapGraphicsUp || id == kTapGraphicsDown) {
        settingsCursor = std::clamp(settingsCursor + (id == kTapGraphicsUp ? -1 : 1), 0, settingCount - 1);
        audio.playSe("com_002", false);
        return;
    }
    if (id >= kTapGraphicsRow && id < kTapGraphicsRow + settingCount * 2) { // (Graphics: a row's left or right arrow)
        settingsCursor = (id - kTapGraphicsRow) / 2;
        settingChange(settingsCursor, (id - kTapGraphicsRow) % 2 ? 1 : -1, false);
        audio.playSe("com_002", false);
        return;
    }
    if (id == -1) {
        audio.playSe("com_004", false);
        cr3Camp.stage = co.from ? 0 : 1, cr3Camp.since = 0; // (from the title or a battle: back to it)
        co.from = 0;
        if (scriptSaves) writeSystem(); // (Cr3Util_Save_SystemData)
        return;
    }
    if (id >= 400 && id < 400 + kOptionTabs && id - 400 != co.tab) co.tab = id - 400, co.footer = false, audio.playSe("com_002", false);
    if (id >= 500 && id < 502 && id - 500 != co.hands) co.hands = id - 500, audio.playSe("com_003", false);
    if (id == 600 || id == 601) optionQuality = (optionQuality + (id == 600 ? 2 : 1)) % 3, audio.playSe("com_002", false);
    if (id == 1400 || id == 1401) stepLanguage(id == 1400 ? -1 : 1), co.otherRow = 1, audio.playSe("com_002", false);
    if (id >= 800 && id < 860) {
        optionVolume[(id - 800) / 20] = (id - 800) % 20 * 10;
        audio.setVolumes(float(optionVolume[0]) / 100.0f, float(optionVolume[1]) / 100.0f, float(optionVolume[2]) / 100.0f);
        audio.playSe("com_002", false);
    }
}

void Viewer::campOptionsKey(SDL_Keycode key) {
    if (saveImport.active()) {
        if (saveImport.state == vitaimport::Session::Picking) {
            if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) saveImport.cancel();
        } else if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN) saveImport.focus ^= 1;
        else if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) saveImportAnswer(false);
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) saveImportAnswer(saveImport.focus == 0);
        return;
    }

    Cr3CampOptions& co = campOptions;
    if (co.asking) { // (the Reset's question: left / right Yes or No, Return that, Escape No)
        if (key == SDLK_LEFT || key == SDLK_RIGHT) co.askFocus = key == SDLK_LEFT ? 0 : 1, audio.playSe("com_002", false);
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) chooseCampOptions(co.askFocus == 0 ? kTapResetYes : kTapResetNo);
        else if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) chooseCampOptions(kTapResetNo);
        return;
    }
    if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) return chooseCampOptions(-1);
    if (key == SDLK_TAB) return chooseCampOptions(400 + (co.tab + 1) % kOptionTabs);
    if (key == SDLK_R) return chooseCampOptions(kTapOptionsReset);
    if (vitaButtons() && co.footer) {
        if (key == SDLK_UP) co.footer = false, audio.playSe("com_002", false);
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) chooseCampOptions(-1);
        return;
    }
    if (vitaButtons() && co.tab == 0 && (key == SDLK_UP || key == SDLK_DOWN)) {
        if (key == SDLK_DOWN && optionFocus == 2) co.footer = true;
        else optionFocus = std::clamp(optionFocus + (key == SDLK_DOWN ? 1 : -1), 0, 2);
        audio.playSe("com_002", false);
        SDL_Log("options: sound row %d%s", optionFocus, co.footer ? " footer" : "");
        return;
    }
    if (vitaButtons() && co.tab == 1 && key == SDLK_DOWN) { co.footer = true; audio.playSe("com_002", false); return; }
    if (co.tab == 3) { // (Graphics: up / down a row, left / right / Return change it)
        if (vitaButtons() && key == SDLK_DOWN && settingsCursor + 1 >= int(settingRows().size())) { co.footer = true; audio.playSe("com_002", false); return; }
        if (key == SDLK_UP || key == SDLK_DOWN) return chooseCampOptions(key == SDLK_UP ? kTapGraphicsUp : kTapGraphicsDown);
        if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_RETURN || key == SDLK_KP_ENTER)
            return chooseCampOptions(kTapGraphicsRow + settingsCursor * 2 + (key == SDLK_LEFT ? 0 : 1));
        return;
    }
    if (co.tab == 0 && (key == SDLK_LEFT || key == SDLK_RIGHT)) {
        chooseCampOptions(800 + optionFocus * 20 + std::clamp(optionVolume[optionFocus] / 10 + (key == SDLK_RIGHT ? 1 : -1), 0, 10));
        if (vitaButtons() && optionFocus == 1) audio.playSe("com_003", false);
        else if (vitaButtons() && optionFocus == 2) audio.playVoice("j002_110");
        return;
    }
    if (co.tab == 0 && key >= SDLK_1 && key <= SDLK_3) optionFocus = int(key - SDLK_1);
    if (co.tab == 1 && (key == SDLK_LEFT || key == SDLK_RIGHT)) chooseCampOptions(500 + (key == SDLK_RIGHT ? 1 : 0));
    if (co.tab == 2) {
        if (key == SDLK_UP || key == SDLK_DOWN) { // (the Language row only where there are languages to pick)
            const int rows = languages.size() > 1 ? 3 : 2, at = co.otherRow == 2 ? rows - 1 : co.otherRow;
            if (vitaButtons() && key == SDLK_DOWN && at == rows - 1) { co.footer = true; audio.playSe("com_002", false); return; }
            const int next = vitaButtons() ? std::clamp(at + (key == SDLK_DOWN ? 1 : -1), 0, rows - 1) : (at + (key == SDLK_DOWN ? 1 : rows - 1)) % rows;
            co.otherRow = next == rows - 1 ? 2 : next;
            if (vitaButtons()) audio.playSe("com_002", false);
        } else if (key == SDLK_I || (co.otherRow == 2 && (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE))) chooseCampOptions(1300);
        else if (co.otherRow == 0 && (key == SDLK_LEFT || key == SDLK_RIGHT)) chooseCampOptions(key == SDLK_LEFT ? 600 : 601);
        else if (co.otherRow == 1 && (key == SDLK_LEFT || key == SDLK_RIGHT)) chooseCampOptions(key == SDLK_LEFT ? 1400 : 1401);
    }
}

} // namespace cr3_viewer
