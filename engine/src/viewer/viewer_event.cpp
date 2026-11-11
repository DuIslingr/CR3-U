#include "viewer/viewer.hpp"

namespace cr3_viewer {

// The events' runtime the screen shows: the vista's, else the field's
cr3::EventRuntime* Viewer::activeEvents() {
    if (titleMenu.stage && titleEvents) return titleEvents.get(); // (title.nut's)
    if ((battleMode || campOpen) && manualStage == 2 && manualEvents) return manualEvents.get(); // (the manual's tutorial)
    if (battleMode && battleScript) return &battleScript->events(); // (the battle's talks)
    if (cr3BattleMode && cr3BattleEvents) return cr3BattleEvents.get(); // (CR3's battle: its script's)
    if (!eventMode && areaMode && vs.screen != 0 && vsEvents && (vsEvents->running() || vsEvents->messageShown())) return vsEvents.get(); // (the VS menu's script)
    if (!eventMode && areaMode && areaEvents) return areaEvents.get(); // (CR3's field: its area script's)
    return eventMode ? event.get() : script ? &script->events() : nullptr;
}

// FldMain::BranchAfterEvent's order: an area change, a battle, a vista, then the world select,
// the camp, the shop, the party, the pub, the chapter select, the library, the title
void Viewer::branchAfterField() {
    cr3::GameState::Next& n = game.next;
    if (!n.popPoint.empty()) {
        const std::string pp = n.popPoint;
        n.popPoint.clear();
        SDL_Log("play: to %s", pp.c_str());
        enterMap(pp.substr(0, 5), pp);
        return;
    }
    if (n.changeDungeon >= 0 && n.changeArea >= 0) { // (ChangeDungeon / ChangeArea: the area's own entry place)
        char map[16]; // (the town dungeon 0: t01NN; else dDDNN - the area from 0)
        if (n.changeDungeon == 0) std::snprintf(map, sizeof map, "t01%02d", n.changeArea + 1);
        else std::snprintf(map, sizeof map, "d%02d%02d", n.changeDungeon, n.changeArea + 1);
        n.changeDungeon = n.changeArea = -1;
        SDL_Log("play: to %s", map);
        enterMap(map, "");
        return;
    }
    if (n.battle >= 0) {
        returnMap = field.info().name, std::memcpy(returnPlayer, player, sizeof player), returnHeading = heading;
        return skipBattle();
    }
    if (n.vista) {
        returnMap = field.info().name, std::memcpy(returnPlayer, player, sizeof player), returnHeading = heading;
        n.vista = false;
        SDL_Log("play: vista %s in %s", n.vistaFunction.c_str(), n.vistaMap.c_str());
        if (!startVista(n.vistaFunction, n.vistaMap)) returnToField(-1);
        return;
    }
    if (n.worldSelect) { // (FldMain step 10: the world select, then WarpTask)
        n.worldSelect = false;
        openWorldSelect();
        return;
    }
    if (n.camp) { // (FldMain step 5: the camp)
        n.camp = false;
        return openCamp();
    }
    if (n.shop) { // (FldMain step 0xb: the shop)
        n.shop = false;
        openShop();
        return;
    }
    if (n.party) { // (FldMain step 0xc: the party)
        n.party = false;
        party = PartyMenu(), party.stage = 1, party.at = uiClock;
        SDL_Log("party: open");
        return;
    }
    if (n.pub) { // (FldMain step 0xd: the pub)
        n.pub = false;
        return openPub();
    }
    if (n.chapter) { // (FldMain step 0xe: the chapter select)
        n.chapter = false;
        openChapterSelect();
        return;
    }
    if (n.library) { // (FldMain step 0xf: the library)
        n.library = false;
        openLibrary();
        return;
    }
    if (n.title) { // (FldMain 0x1d, 2: the title - none here: the options' Title Screen's way)
        n.title = false;
        toTitle = true;
    }
}

void Viewer::skipBattle() {
    cr3::GameState::Next& n = game.next;
    if (!skipBattles) {
        int party[2];
        for (int k = 0; k < 2; ++k) {
            const int c = n.battleParty[k] >= 0 ? n.battleParty[k] : game.party[k];
            party[k] = c >= 0 ? c + 1 : 0; // (characters from 1 in the battle's data)
        }
        SDL_Log("play: battle %d (characters %d, %d)", n.battle, party[0], party[1]);
        const int id = n.battle;
        n.battle = -1, n.battleParty[0] = n.battleParty[1] = -1;
        if (startBattle(id, party)) {
            startFight(2, 1u + uint32_t(battlesSkipped));
            fightingInFlow = true;
            return;
        }
    }
    SDL_Log("play: battle %d - taken as won", n.battle);
    n.battle = -1;
    ++battlesSkipped;
    afterBattle();
}

// the battle dropped (the options' Title Screen)
void Viewer::leaveBattle() {
    cancelSceneAudio();
    pendingBgm = 0, pendingBgmAt = 0;
    battleMode = fightingInFlow = choosing = optionsOpen = optionsAsk = false;
    battleScript.reset(), battleRules.reset();
    battleFight = 0, battleStep = BattleStep::Over;
    showActions.clear(), showIndex = 0, regenPending.clear(), regenStarted = false;
    announceQueue.clear(), commandShown.clear();
}

// BtlMain::Exit: a vista reserved goes on to it, else the field (evt100)
void Viewer::afterBattle() {
    cr3::GameState::Next& n = game.next;
    battleMode = false;
    if (n.vista) { // (BtlMain::Exit: a vista reserved goes on to it)
        n.vista = false;
        if (startVista(n.vistaFunction, n.vistaMap)) return;
    }
    returnToField(1);
}

void Viewer::branchAfterVista() {
    cr3::GameState::Next& n = game.next;
    if (n.battle >= 0) skipBattle();
    else if (n.vista) {
        n.vista = false;
        SDL_Log("play: vista %s in %s", n.vistaFunction.c_str(), n.vistaMap.c_str());
        if (!startVista(n.vistaFunction, n.vistaMap)) returnToField(n.fieldMode);
    } else if (n.credit >= 0) { // (VistaMain::BranchAfterVista: 3, the credits)
        eventMode = false;
        startCredits(n.credit);
        n.credit = -1;
    } else returnToField(n.fieldMode);
}

void Viewer::startCredits(int n) {
    if (!staffTried) {
        staffTried = true;
        if (!cr3::releaseStaffRoll(dataArg, binaryArg, staffLines, staffFigures)) SDL_Log("credits: the staff roll's table not found in the executable");
    }
    credits = Credits();
    credits.stage = 1, credits.number = std::clamp(n, 0, 3);
    static const int kMusic[4] = {-1, 17, 28, 29};
    if (kMusic[credits.number] >= 0) audio.playBgm(kMusic[credits.number], 20);
    SDL_Log("play: credits %d (%zu lines)", n, staffLines.size());
}

std::string Viewer::creditFigure() const {
    static const char* const kDefault[4] = {"end2_BG", "end1_BG", "end3_BG", "end4_BG"};
    std::string f = staffFigures[credits.number].empty() ? kDefault[credits.number] : staffFigures[credits.number];
    std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return f;
}

float Viewer::creditScale() const {
    const float aspect = float(screenW) / float(std::max(screenH, 1));
    return aspect < 1.5f ? 1.0f : aspect / 1.5f;
}

bool Viewer::creditsLineShown(size_t i) const {
    return i < staffLines.size() && !staffLines[i].empty() && (i < 44 || i > 49 || game.flag(201));
}

void Viewer::updateCredits(float dt) {
    if (!inputScript.empty() && inputScript.front() == "fast" && uiClock >= fastUntil) inputScript.pop_front(), fastUntil = uiClock + 2.0f;
    credits.stageTime += dt;
    if (credits.stage == 1) {
        credits.animTime += dt * (fastHeld || uiClock < fastUntil ? 5.0f : 1.0f); // (TouchSimpleMove: the figure's speed 5)
        UiFig& f = uiFig(creditFigure());
        const float dur = f.fig.animLoaded ? f.fig.anim.duration() : 0;
        float x, y;
        const float lineH = creditScale() * 0.032205883f;
        poseUi(f, creditFigure(), credits.animTime);
        if (uiJoint(f, "typeS_L01", x, y)) {
            if (!credits.started) credits.startY = y, credits.started = true;
            const bool allIn = (y - credits.startY) / lineH >= float(staffLines.empty() ? 0 : 543);
            if (allIn && credits.animTime >= dur) credits.stage = 2, credits.stageTime = 0;
        } else if (credits.animTime >= dur) credits.stage = 2, credits.stageTime = 0;
    } else if (credits.stage == 2 && credits.stageTime >= 120 * cr3::EventRuntime::kTick) {
        credits.stage = 3, credits.stageTime = 0;
        audio.stopBgm(60);
    } else if (credits.stage == 3 && credits.stageTime >= 62 * cr3::EventRuntime::kTick) {
        credits.stage = 0;
        SDL_Log("play: credits over");
        cr3::GameState::Next& n = game.next;
        const std::string pp = n.popPoint;
        n.popPoint.clear(), n.fieldMode = -1;
        if (!pp.empty()) enterMap(pp.substr(0, 5), pp, "credit"); // (FldMain::Init mode 4: CallEvent("credit"))
        else enterMap(returnMap, "", "credit");
    }
}

void Viewer::drawCredits() {
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f;
    const Mat4 view = cr3::mat4Multiply(uiProjection(screenW, screenH), camera);
    const float k = creditScale();
    UiFig& f = uiFig(creditFigure());
    f.fig.place = cr3::Mat34();
    f.fig.place.m[0] = f.fig.place.m[5] = k;
    poseUi(f, creditFigure(), credits.animTime);
    drawFigure(f.fig, view.data());
    float jx, jy;
    if (uiJoint(f, "typeS_L01", jx, jy)) {
        const float lineH = k * 0.032205883f;
        const float risen = credits.started ? (jy - credits.startY) / lineH : 0;
        for (size_t i = 0; i < staffLines.size(); ++i) {
            if (float(i) > risen) break;
            const float y = jy - float(i) * lineH;
            if (y >= 0.4f || !creditsLineShown(i)) continue;
            drawUiText(staffLines[i], i >= 438 && i < 534 ? 18.0f : 28.0f, jx, y, screenW, screenH, true); // (SetTextRender: the size + 4, or - 6, drawn 4 less; typeS_L: from the joint)
        }
    }
    fastRect.id = 0;
    if (credits.stage == 1) {
        UiFig& ff = uiFig("speedup");
        ff.fig.place = cr3::Mat34();
        poseUi(ff, "speedup", uiClock, true);
        drawFigure(ff.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(ff, "tapAreaS01", sx, sy) && uiJoint(ff, "tapAreaE01", ex, ey))
            fastRect.id = 1, fastRect.x0 = sx, fastRect.y0 = sy, fastRect.x1 = ex, fastRect.y1 = ey;
    }
    // the fades: in over 60 frames, out over 60 after the wait
    const float a = credits.stage == 1 ? std::max(0.0f, 1.0f - credits.stageTime / (60 * cr3::EventRuntime::kTick))
                    : credits.stage == 3 ? std::min(1.0f, credits.stageTime / (60 * cr3::EventRuntime::kTick)) : 0.0f;
    if (a > 0 && white.id) {
        static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
        const Mat4 id = cr3::mat4Identity();
        const float rgba[4] = {0, 0, 0, a};
        fullFrameQuad(white, kScreen, rgba);
    }
}

void Viewer::returnToField(int mode) {
    cr3::GameState::Next& n = game.next;
    const std::string entry = mode == 1 ? "evt100" : mode == 2 ? "evt200" : "evt000";
    n.fieldMode = -1;
    eventMode = false;
    dev->resetHistory();
    if (!n.popPoint.empty()) {
        const std::string pp = n.popPoint;
        n.popPoint.clear();
        SDL_Log("play: to %s (%s)", pp.c_str(), entry.c_str());
        enterMap(pp.substr(0, 5), pp, entry);
    } else {
        SDL_Log("play: back to %s (%s)", returnMap.c_str(), entry.c_str());
        std::memcpy(player, returnPlayer, sizeof player), heading = returnHeading;
        enterMap(returnMap, "", entry);
    }
}

void Viewer::readSystem() {
    systemRead = true;
    std::FILE* f = std::fopen(savePath("system.txt").c_str(), "rb");
    if (!f) return;
    std::string text;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != "CR3-U system 1") return;
    systemFound = true;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string key;
        l >> key;
        if (key == "flags")
            for (int n; l >> n;)
                if (n >= 0 && n < kSystemFlags) systemFlags.push_back(n); // (all this file holds)
        else if (key == "options")
            for (int& o : systemOptions) l >> o;
    }
}

void Viewer::applySystem(bool importFlags) {
    if (!systemRead) readSystem();
    if (!systemFound) return; // (none yet: the save's own as they were)
    if (importFlags) {
        for (int n = 0; n < kSystemFlags; ++n) game.setFlag(n, false);
        for (int n : systemFlags) game.setFlag(n, true);
    }
    if (systemOptions[0] >= 0)
        optionLanguage = systemOptions[0], optionQuality = systemOptions[1], optionVolume[0] = systemOptions[2],
        optionVolume[1] = systemOptions[3], optionVolume[2] = systemOptions[4], languageFromSlot();
    if (systemOptions[5] == 0 || systemOptions[5] == 1) campOptions.hands = systemOptions[5];
}

void Viewer::writeSystem() {
    systemRead = systemFound = true;
    systemFlags.clear();
    for (int n = 0; n < kSystemFlags; ++n)
        if (game.flag(n)) systemFlags.push_back(n);
    const int o[6] = {optionLanguage, optionQuality, optionVolume[0], optionVolume[1], optionVolume[2], campOptions.hands};
    std::memcpy(systemOptions, o, sizeof o);
    std::string text = "CR3-U system 1\nflags";
    for (int n : systemFlags) text += " " + std::to_string(n);
    text += "\noptions";
    for (int v : o) text += " " + std::to_string(v);
    text += "\n";
    std::string error;
    if (!crstorage::replaceFile(savePath("system.txt"), text, &error)) SDL_Log("system save failed: %s", error.c_str());
}

// Fresh game / retry boundary. System settings are applied by newGame;
// save loading or explicit carryOver runs afterward and supplies its own progress.
void Viewer::resetForNewSession() {
    resetSessionUi();
    cancelSceneAudio();
    pendingBgm = 0, pendingBgmAt = 0;
    titleEvents.reset();
    titleMenu = TitleMenu();
    resetGame();
    playSeconds = 0;
}

void Viewer::newGame() {
    resetForNewSession();
    applySystem();
    std::vector<uint8_t> d;
    if (!archive.read("data\\battledata.dat", d) || !battleData.parse(d)) return;
    game.initSopia(battleData, cr3::sopiaExp), game.initItems(battleData), game.initEquip(battleData, cr3::levelExpTable());
}

// VistaMain: the map's figure, the event's script (its scene's figures follow it)
bool Viewer::startVista(const std::string& function, const std::string& map) {
    vistaControlsUntil = -1;
    std::string err;
    unload(vistaMap);
    vistaMap = ViewFigure();
    for (ViewFigure& f : eventFigures) unload(f);
    for (ViewFigure& f : eventHeads) unload(f);
    for (auto& slots : eventAttachments)
        for (auto& [slot, att] : slots) unload(att.fig);
    eventFigures.clear(), eventHeads.clear(), eventAttachments.clear(), eventFigureNames.clear(), eventModelArmors.clear();
    eventEnded = false, eventClock = 0, messageLogged = false;
    vistaMapName = map;
    if (map != "null" && !loadFigure(map + ".geom", vistaMap, &err)) SDL_Log("vista %s: %s", map.c_str(), err.c_str());
    // (VistaMain::LoadMapData: CrxSimpleModel::Build - the map figure's own lights, where it has
    // any: e0009f's dark space, not the field's left on)
    if (map != "null" && loadLights(map)) SDL_Log("lights: %s", map.c_str());
    vistaMap.absolute = true; // (VistaMain::LoadMapData: slot 0)
    vistaMapTime = 0;
    event = std::make_unique<cr3::EventRuntime>(archive, game);
    event->autoPage = pageSeconds, event->askYesNo = true;
    event->extraNatives = [this](cr3::ScriptVM& vm) { bindQuakeNatives(vm), bindCr3FieldUiNatives(vm, false), bindVsNatives(vm); };
    event->language = optionLanguage; // (the release's language's slot: the Vita's Japanese is 0)
    // CR3: an area script's event, "<script>:<function>" (t01:s000_t01_00101_evt)
    // - a bare one (the next vista's: BranchAfterVista) in the script eventScriptOf finds
    const size_t colon = function.find(':');
    eventScript = colon != std::string::npos ? function.substr(0, colon) : eventScriptOf(function);
    const std::string fn = colon != std::string::npos ? function.substr(colon + 1) : function;
    if (!eventScript.empty() ? !event->start(eventScript, fn) : !event->start(fn)) {
        SDL_Log("event %s: %s", function.c_str(), event->error().c_str());
        // (VistaMain::LoadScript: a function its script lacks - q071_t01_00102_evt, a quest
        // battle's SetBattleToVista - calls nothing; nothing running, the vista is over at its
        // next step and goes on as any ends, BranchAfterVista)
        if (event->error().find(": no function ") == std::string::npos) return false;
    }
    eventMode = true;
    dev->resetHistory();
    status = function + " in " + map;
    SDL_Log("event %s: vista %s, %zu scene models", function.c_str(), map.c_str(), event->scene().models().size());
    stepEvent(); // (the update the game starts it in)
    return true;
}

void Viewer::startLunch(const std::string& function, const std::string& map) {
    SDL_Log("lunch: %s in %s", function.c_str(), map.c_str());
    const std::string script = eventScriptOf(function);
    if (!startVista(script.empty() ? function : script + ":" + function, map.empty() ? "null" : map)) return;
    lunchActive = true, lunchWaitLogged = false, lunchFunction = function, lunchFocus.clear();
}

// a balloon's place on the screen (UI units, the screen 9.6 x 6.4 at least): false behind the camera
bool Viewer::lunchBalloonAt(const cr3::EventRuntime::LunchBalloon& b, float& x, float& y) {
    if (!event || !lastEventVPSet) return false;
    for (const cr3::EventModel& m : event->scene().models()) {
        if (m.name != b.model) continue;
        const cr3::Mat34 pl = event->scene().locatorAtStart(m.name); // (EvtScene::GetLocatorPos(model, 0): ev_<model> at the scene's start)
        const float q[3] = {pl.m[3] + b.offset[0], pl.m[7] + b.offset[1], pl.m[11] + b.offset[2]};
        Mat4 vpStart = lastEventVP; // (Cr3UtilSetBalloonPos through the scene camera as posed at 0 - see cameraAtStart)
        cr3::Mat34 startCam;
        float fov = 45, zn = 0.1f, zf = 100;
        if (event->scene().cameraAtStart(startCam, fov, zn, zf))
            vpStart = cr3::mat4Multiply(perspective(fov * 3.14159265f / 180, float(frameW) / float(std::max(frameH, 1)), zn, zf),
                                        cr3::mat4RigidInverse(cr3::mat4FromMat34(startCam)));
        const float* v = vpStart.data(); // (column-major)
        const float cw = v[3] * q[0] + v[7] * q[1] + v[11] * q[2] + v[15];
        if (cw <= 0) return false;
        const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
        x = (v[0] * q[0] + v[4] * q[1] + v[8] * q[2] + v[12]) / cw * float(frameW) / 2 / k; // (across the frame the scene fills)
        y = (v[1] * q[0] + v[5] * q[1] + v[9] * q[2] + v[13]) / cw * float(frameH) / 2 / k;
        return true;
    }
    return false;
}

bool Viewer::lunchIdle() const { return lunchActive && event && eventMode && !event->running() && !event->lunch().shutdown; }

bool Viewer::lunchKey(SDL_Keycode key) {
    if (!vitaButtons() || !lunchIdle()) return false;
    std::vector<std::string> shown;
    for (const auto& b : event->lunch().balloons)
        if (b.shown && b.delay <= 0) shown.push_back(b.model);
    if (shown.empty()) return true;
    auto it = std::find(shown.begin(), shown.end(), lunchFocus);
    int at = it == shown.end() ? 0 : int(it - shown.begin());
    if (key == SDLK_UP || key == SDLK_RIGHT) at = (at + 1) % int(shown.size());
    else if (key == SDLK_DOWN || key == SDLK_LEFT) at = (at + int(shown.size()) - 1) % int(shown.size());
    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        lunchFocus = shown[size_t(at)];
        chooseLunchBalloon(lunchFocus);
        return true;
    } else return true;
    lunchFocus = shown[size_t(at)];
    SDL_Log("lunch: the cursor on %s", lunchFocus.c_str());
    return true;
}

void Viewer::chooseLunchBalloon(const std::string& model) {
    const std::string base = lunchFunction.size() > 4 && lunchFunction.compare(lunchFunction.size() - 4, 4, "_evt") == 0
                                 ? lunchFunction.substr(0, lunchFunction.size() - 4) : lunchFunction;
    SDL_Log("lunch: %s", (base + "_" + model).c_str());
    lunchWaitLogged = false;
    if (!event->call(base + "_" + model)) SDL_Log("lunch: %s", event->error().c_str());
}

bool Viewer::lunchClick(float fx, float fy) {
    if (!lunchIdle()) return false;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    for (const cr3::EventRuntime::LunchBalloon& b : event->lunch().balloons) {
        float x, y;
        if (!b.shown || b.delay > 0 || !lunchBalloonAt(b, x, y)) continue;
        if ((x - ux) * (x - ux) + (y + 0.5f - uy) * (y + 0.5f - uy) < 0.3f) {
            chooseLunchBalloon(b.model);
            return true;
        }
    }
    return false;
}

void Viewer::drawLunchBalloons(int w, int h) {
    if (!lunchActive || !event || event->running()) return; // (LunchBalloon: shown while no talk runs - ProcessBalloonTouch's)
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    if (vitaButtons()) {
        const auto& balloons = event->lunch().balloons;
        const auto focused = std::find_if(balloons.begin(), balloons.end(), [&](const auto& b) { return b.model == lunchFocus && b.shown && b.delay <= 0; });
        if (focused == balloons.end()) {
            lunchFocus.clear();
            for (const auto& b : balloons) if (b.shown && b.delay <= 0) { lunchFocus = b.model; break; }
        }
    }
    for (const cr3::EventRuntime::LunchBalloon& b : event->lunch().balloons) {
        float x, y;
        if (vitaButtons() && b.model == lunchFocus) cr3UiScale = 1.3f + 0.2f * std::sin(uiClock * 6);
        if (b.shown && lunchBalloonAt(b, x, y)) cr3UiPut("feflag_fr00", "", float(b.frame) / 30, x, y, view);
        cr3UiScale = 1;
    }
}

// one of the game's updates, the figures following the scene's models
void Viewer::stepEvent() {
    if (eventEnded) return;
    if (!event->tick()) {
        SDL_Log("event: %s", event->error().c_str());
        eventEnded = true;
    }
    if (!event->running() && !eventEnded && lunchActive && !event->lunch().shutdown) { // (LunchMain 200: the balloons' taps)
        for (cr3::EventRuntime::LunchBalloon& b : event->lunch().balloons)
            if (b.shown && b.delay > 0) --b.delay;
        if (!lunchWaitLogged) {
            lunchWaitLogged = true;
            std::string shown;
            for (const cr3::EventRuntime::LunchBalloon& b : event->lunch().balloons)
                if (b.shown) shown += " " + b.model + (b.frame == 1 ? " (marked)" : b.frame == 2 ? " (the end)" : "");
            SDL_Log("lunch: the balloons, at frame %d:%s", event->frame(), shown.c_str());
        }
        syncEventFigures(*event);
        return;
    }
    if (!event->running() && !eventEnded) {
        if (lunchActive) lunchActive = false, SDL_Log("lunch: over"); // (LunchMain 10000: then as a vista ends)
        eventEnded = true;
        SDL_Log("event ended at frame %d (%.2f s)%s", event->frame(), event->frame() * cr3::EventRuntime::kTick,
                event->battle() >= 0 ? (": battle " + std::to_string(event->battle())).c_str() : "");
        if (playMode && eventScript.empty()) {
            branchAfterVista();
            return;
        }
        // (VistaMain::BranchAfterVista / SetNextModeAfterVista: a vista VS reserved - the VS battle it
        // reserved (VsBattleIn: mode 3, back to 0x19 after), else VSMain anew)
        if (vsFromVista && !eventScript.empty()) {
            vsFromVista = false;
            game.next = cr3::GameState::Next();
            eventMode = false;
            if (!savedArea.empty() && !loadArea(savedArea, "")) SDL_Log("area %s: not entered", savedArea.c_str());
            if (vsPendingTournament >= 0 && game.cr3Db && game.vsSelected >= 0) {
                const int k = game.vsSelected;
                const bool begun = vs.chosen == k && vs.battle == 0; // (VS_BattleIn's: begun from the menu)
                if (!begun) {
                    vs = Cr3VsMode();
                    vs.chosen = k;
                    game.cr3Db->pickWildcard();
                }
                vs.battle = 0, vs.lost = false;
                std::fill(std::begin(vs.wildChosen), std::end(vs.wildChosen), -1);
                vsPendingTournament = -1;
                SDL_Log("vs: the reserved tournament %d's battles", game.cr3Db->vsTournaments()[size_t(k)].id);
                vsNextBattle();
            } else {
                vsEnter();
            }
            return;
        }
        // (CR3, VistaMain::BranchAfterVista: the battle it reserved - BattleIn2 -, its
        // SetBattleToVista kept for after)
        if (game.next.battle >= 0 && !eventScript.empty()) {
            const int id = game.next.battle, fieldId = game.next.battleField;
            game.next.battle = -1;
            SDL_Log("event: battle %d on field %d", id, fieldId);
            requestCr3Battle(id, fieldId);
            return;
        }
        // (VistaMain::BranchAfterVista 0xf: a lunch reserved - LunchEventStart - its scene next)
        if (game.next.lunch && !game.next.vistaFunction.empty() && !eventScript.empty()) {
            const cr3::GameState::Next n = game.next;
            game.next = cr3::GameState::Next();
            startLunch(n.vistaFunction, n.vistaMap);
            return;
        }
        // (--event: an event that hands over to the next vista - SetVistaEvent at its end, as the
        // prologue's s000_t01_00101_evt to s000_t01_00103_evt - goes on into it, as the game does)
        if (game.next.vista && !game.next.vistaFunction.empty() && !eventScript.empty()) {
            const cr3::GameState::Next n = game.next;
            game.next = cr3::GameState::Next();
            SDL_Log("event: on to %s in %s", n.vistaFunction.c_str(), n.vistaMap.c_str());
            startVista(eventScriptOf(n.vistaFunction) + ":" + n.vistaFunction, n.vistaMap.empty() ? "null" : n.vistaMap);
            return;
        }
        // (CR3: SetVistaToField - Cr3System.SetComeFromVistaToField - back to the field it's in:
        // the area its map is of, d0101f's d0101)
        // - or where SetPopPoint2 put it (Fld2Main::SM_Init: the pop point first)
        // (nothing else reserved - VistaMain::BranchAfterVista's field, SM_Init - the same)
        const bool toField = game.next.credit < 0 && game.next.popPoint.empty() && !game.next.title;
        if ((game.next.fieldMode == 2 || toField) && !eventScript.empty() && !game.popPoint2.empty()) {
            const std::string pop = game.popPoint2;
            game.next = cr3::GameState::Next();
            SDL_Log("event: back to the field, at %s", pop.c_str());
            eventMode = false;
            if (!enterPopPoint(pop)) SDL_Log("area %s: not entered", pop.c_str());
            return;
        }
        // (a vista from no field - the prologue's, from the title -: its map's area; else the field
        // it was left from - Fld2Main::SM_Init: Cr3FieldData, as Fld2SaveData kept it)
        if (game.next.fieldMode == 2 && !eventScript.empty() && vistaMapName.size() == 6 && vistaMapName.back() == 'f' && savedArea.empty()) {
            const std::string a = vistaMapName.substr(0, 5);
            game.next = cr3::GameState::Next();
            SDL_Log("event: back to the field, %s", a.c_str());
            eventMode = false;
            if (!loadArea(a, "")) SDL_Log("area %s: not entered", a.c_str());
            return;
        }
        if (playMode && (toField || game.next.fieldMode == 2) && !eventScript.empty() && !savedArea.empty()) { // (the field the vista was left from, where the player stood)
            const std::string a = savedArea;
            game.next = cr3::GameState::Next();
            SDL_Log("event: back to the field, %s", a.c_str());
            eventMode = false;
            if (!loadArea(a, "")) SDL_Log("area %s: not entered", a.c_str());
            return;
        }
        if (playMode) branchAfterVista(); // (the rest: the credits, a ChangeArea's place)
    }
    if (event->messageShown() != messageLogged) {
        messageLogged = event->messageShown();
        if (messageLogged) SDL_Log("frame %d: a message, %zu pages (window %d, %s)", event->frame(), event->message().pages.size(), event->messageWindow(),
                                   event->windowOpen(event->messageWindow()) ? "open" : "not open");
    }
    syncEventFigures(*event);
}

// a figure for each of the scene's models, made again when they change (an EvtScene.Init or ChangeArmor)
void Viewer::syncEventFigures(cr3::EventRuntime& ev) {
    std::vector<std::string> names;
    std::vector<int> armors;
    for (const cr3::EventModel& m : ev.scene().models()) names.push_back(m.figure), armors.push_back(m.temporaryArmor);
    if (names != eventFigureNames || armors != eventModelArmors) {
        for (ViewFigure& f : eventFigures) unload(f);
        for (ViewFigure& f : eventHeads) unload(f);
        for (auto& slots : eventAttachments)
            for (auto& [slot, att] : slots) unload(att.fig);
        eventFigures.clear(), eventHeads.clear(), eventAttachments.clear();
        for (size_t idx = 0; idx < names.size(); ++idx) {
            const std::string& n = names[idx];
            const int tempArmor = armors[idx];
            ViewFigure f, head;
            std::string err;
            // (CrxModel::Build: a parts model - the heroes' j00N - is <name>b01 and <name>h01)
            const bool parts = !archive.contains(n + ".geom") && archive.contains(n + "b01.geom");
            std::string body = parts ? (tempArmor >= 0 && cr3TablesReady() ? cr3BattleTables.body(n, tempArmor) : cr3PartsBody(n)) : n;
            if (parts && !archive.contains(body + ".geom")) body = n + "b01";
            if (game.cr3NewGamePlus > 0 && n == "j001" && tempArmor < 0) SDL_Log("New Game Plus: protagonist outfit %s", body.c_str());
            if (!archive.contains(body + ".geom") || !loadFigure(body + ".geom", f, &err))
                SDL_Log("event model %s: %s", n.c_str(), err.empty() ? "no figure" : err.c_str());
            if (parts && archive.contains(n + "h01.geom") && !loadFigure(n + "h01.geom", head, &err))
                SDL_Log("event model %s head: %s", n.c_str(), err.c_str());
            eventFigures.push_back(std::move(f));
            eventHeads.push_back(std::move(head));
            eventAttachments.emplace_back();
        }
        eventFigureNames = names, eventModelArmors = armors;
    }
    // Sync each model's joint-constrained attachments / equipped weapon (0x00845348..0x008461b8).
    const std::vector<cr3::EventModel>& models = ev.scene().models();
    for (size_t i = 0; i < models.size() && i < eventAttachments.size(); ++i) {
        const cr3::EventModel& m = models[i];
        std::map<int, EventAttachmentView>& slots = eventAttachments[i];
        std::map<int, std::pair<std::string, std::string>> wanted;
        for (const auto& [slot, att] : m.attachments)
            if (!att.figure.empty()) wanted[slot] = {att.figure, ""};
        if (m.weaponVisible && !wanted.count(0)) {
            const int ch = m.figure.size() == 4 && m.figure[0] == 'j' ? std::atoi(m.figure.c_str() + 1) : 0;
            std::string wfig, wvar;
            if (ch > 0 && cr3TablesReady() && cr3BattleTables.weaponModel(cr3BattleTables.weaponOf(ch, &game.cr3Weapon), wfig, wvar))
                wanted[0] = {wfig, wvar};
        }
        for (auto it = slots.begin(); it != slots.end();) {
            const auto w = wanted.find(it->first);
            if (w == wanted.end() || w->second.first != it->second.figure || w->second.second != it->second.variant) {
                unload(it->second.fig);
                it = slots.erase(it);
            } else ++it;
        }
        for (const auto& [slot, fv] : wanted) {
            if (slots.count(slot)) continue;
            EventAttachmentView av;
            av.figure = fv.first, av.variant = fv.second;
            std::string err;
            figureVariant = av.variant;
            if (!loadFigure(av.figure + ".geom", av.fig, &err))
                SDL_Log("event attachment %s (%s): %s", m.name.c_str(), av.figure.c_str(), err.c_str());
            figureVariant.clear();
            slots[slot] = std::move(av);
        }
    }
}

// a frame between the event's updates: how far past the last one it is (none once it ended)
float Viewer::eventLead() const {
    if (titleMenu.stage && titleEvents) return 0.0f;
    if ((battleMode || campOpen) && manualStage == 2 && manualEvents) return manualClock;
    if (battleMode && battleScript) return battleScriptClock;
    if (cr3BattleMode && cr3BattleEvents) return cr3BattleEventClock;
    if (!eventMode && areaMode && areaEvents) return areaEventClock;
    return eventMode ? (eventEnded ? 0.0f : eventClock) : fieldClock;
}

void Viewer::updateEvent(float dt) {
    if (event && !event->mapAnimPaused) vistaMapTime += dt;
    eventClock = std::min(eventClock + dt, 0.25f);
    for (; eventClock >= cr3::EventRuntime::kTick && eventMode; eventClock -= cr3::EventRuntime::kTick) stepEvent();
    if (eventMode) stepScriptCollect(dt); // (a scene's CollectListDisp - s101_d02_00601's gift - as the field's)
}

// the models where the scene has them, playing what the script chose (ChangeAnimator: their
// <figure><suffix>; else the figure's own), their faces as ChangeModelFace left them
void Viewer::poseEvent() {
    pose(vistaMap, vistaMapTime);
    if (event->secondMap != secondMapName) { // (VistaEvent.LoadSecondMap: built as the first is)
        unload(secondMap);
        secondMap = ViewFigure();
        secondMapName = event->secondMap;
        std::string err;
        if (!secondMapName.empty() && !loadFigure(secondMapName + ".geom", secondMap, &err)) SDL_Log("second map %s: %s", secondMapName.c_str(), err.c_str());
        secondMap.absolute = true;
    }
    pose(secondMap, vistaMapTime);
    poseEventModels(*event);
}

void Viewer::poseEventModels(cr3::EventRuntime& ev) {
    std::vector<cr3::EventModel>& models = ev.scene().models();
    for (size_t i = 0; i < models.size() && i < eventFigures.size(); ++i) {
        const cr3::EventModel& m = models[i];
        if (!m.visible) continue;
        poseEventFigure(ev, m, eventFigures[i]);
        if (i < eventHeads.size() && !eventHeads[i].meshes.empty()) poseEventFigure(ev, m, eventHeads[i]);
    }
}

void Viewer::drawEvent(int w, int h) {
    cr3::Mat34 cw;
    float fov = 45, zn = 0.1f, zf = 100;
    if (event->sceneShown() && event->scene().camera(cw, fov, zn, zf, eventLead())) {
        // Cr3System.SetViewportEnable/Position/Size (0x0092fb94..0x0092fbe8): e.g. Staffroll 3D sub-viewport.
        int subW = w, subH = h;
        if (game.viewportEnable && game.viewportSize[0] > 0.01f && game.viewportSize[1] > 0.01f) {
            int rw = frameW, rh = frameH;
            dev->sceneSize(frameW, frameH, rw, rh);
            const float kx = float(rw) / float(std::max(frameW, 1)), ky = float(rh) / float(std::max(frameH, 1));
            const float vx = float(frameX) + game.viewportPos[0] * float(frameW);
            const float vy = float(frameY) + (1.0f - game.viewportPos[1] - game.viewportSize[1]) * float(frameH);
            const float vw = game.viewportSize[0] * float(frameW), vh = game.viewportSize[1] * float(frameH);
            dev->setViewport(vx * kx, vy * ky, vw * kx, vh * ky);
            subW = std::max(int(vw + 0.5f), 1), subH = std::max(int(vh + 0.5f), 1);
        }
        // a cut (the camera jumps between frames): the upscaler's history says nothing of it
        const float* lp = lastCamera.m;
        const float moved = std::hypot(cw.m[3] - lp[3], cw.m[7] - lp[7], cw.m[11] - lp[11]);
        const float turned = cw.m[2] * lp[2] + cw.m[6] * lp[6] + cw.m[10] * lp[10]; // the view axes' cosine
        if (moved > 0.05f * std::max(zn * 20, 0.1f) || turned < 0.97f) dev->resetHistory();
        lastCamera = cw;
        dev->setCamera(zn, zf, fov * 3.14159265f / 180);
        const Mat4 world = cr3::mat4FromMat34(cw);
        Mat4 proj = perspective(fov * 3.14159265f / 180, float(subW) / float(std::max(subH, 1)), zn, zf);
        applyQuake(proj);
        const Mat4 vp = cr3::mat4Multiply(proj, cr3::mat4RigidInverse(world));
        lastEventVP = vp, lastEventVPSet = true; // (a lunch's balloons: placed through it)
        // VistaMain::Draw: the vista light figure's lights for the map and the models
        cr3::SceneLighting l = lighting;
        event->scene().lights(l, eventLead());
        l.camera[0] = world[12], l.camera[1] = world[13], l.camera[2] = world[14];
        l.time = time;
        dev->setLighting(l);
        drawFigure(event->mapChanged ? secondMap : vistaMap, vp.data()); // (VistaMain::OnDraw: the first, or after ChangeMap the second)
        // the scene's models by their draw layer (CR3's EvtScene.SetModelLayer), each layer's
        // screen sprites over it (EvtScene.CreateSprite / CreateSpriteWithTexture)
        const std::vector<cr3::EventModel>& models = event->scene().models();
        const std::vector<cr3::EventSprite>& sprites = event->scene().sprites;
        int top = 0;
        for (const cr3::EventModel& m : models) top = std::max(top, m.layer);
        for (const cr3::EventSprite& sp : sprites) top = std::max(top, sp.layer);
        const float lead = eventLead();
        const float fit = std::min(float(std::max(w, 1)) / 1920.0f, float(std::max(h, 1)) / 1280.0f);
        const float vw = float(std::max(w, 1)) / std::max(fit, 1e-4f), vh = float(std::max(h, 1)) / std::max(fit, 1e-4f);
        for (int layer = 0; layer <= top; ++layer) {
            for (size_t i = 0; i < models.size() && i < eventFigures.size(); ++i)
                if (models[i].visible && models[i].layer == layer) {
                    if (models[i].hasShadow() && models[i].shadow) drawShadow(eventFigures[i].place, vp.data());
                    drawFigure(eventFigures[i], vp.data());
                    if (i < eventHeads.size() && !eventHeads[i].meshes.empty()) drawFigure(eventHeads[i], vp.data());
                    if (i < eventAttachments.size()) {
                        for (auto& [slot, av] : eventAttachments[i]) {
                            if (av.fig.meshes.empty()) continue;
                            const auto ait = models[i].attachments.find(slot);
                            const std::string joint = ait != models[i].attachments.end() && !ait->second.joint.empty() ? ait->second.joint : std::string("DummyWP");
                            cr3::Mat34 jp;
                            if (!jointWorld(eventFigures[i], joint, jp) && !(i < eventHeads.size() && jointWorld(eventHeads[i], joint, jp))) continue;
                            if (ait != models[i].attachments.end()) {
                                constexpr float kDeg = 3.14159265f / 180.0f;
                                const float cx = std::cos(ait->second.rotDeg[0] * kDeg), sx = std::sin(ait->second.rotDeg[0] * kDeg);
                                const float cy = std::cos(ait->second.rotDeg[1] * kDeg), sy = std::sin(ait->second.rotDeg[1] * kDeg);
                                const float cz = std::cos(ait->second.rotDeg[2] * kDeg), sz = std::sin(ait->second.rotDeg[2] * kDeg);
                                cr3::Mat34 local;
                                local.m[0] = cy * cz, local.m[1] = -cy * sz, local.m[2] = sy;
                                local.m[4] = sx * sy * cz + cx * sz, local.m[5] = -sx * sy * sz + cx * cz, local.m[6] = -sx * cy;
                                local.m[8] = -cx * sy * cz + sx * sz, local.m[9] = cx * sy * sz + sx * cz, local.m[10] = cx * cy;
                                local.m[3] = ait->second.offset[0], local.m[7] = ait->second.offset[1], local.m[11] = ait->second.offset[2];
                                jp = jp * local;
                                if (!ait->second.motion.empty()) {
                                    const std::string want = ait->second.motion + ".anim";
                                    const auto mit = std::find(av.fig.anims.begin(), av.fig.anims.end(), want);
                                    if (mit != av.fig.anims.end() && (!av.fig.animLoaded || av.fig.anims[av.fig.animIndex] != want)) {
                                        const float keep = time;
                                        loadAnim(av.fig, size_t(mit - av.fig.anims.begin()));
                                        time = keep;
                                    }
                                    av.fig.absolute = false;
                                }
                            }
                            av.fig.place = jp;
                            for (int k = 0; k < 4; ++k) av.fig.tint[k] = eventFigures[i].tint[k];
                            const float at = ait != models[i].attachments.end() && !ait->second.motion.empty() ? ait->second.animTime + lead : time;
                            const bool aloop = ait == models[i].attachments.end() || ait->second.motion.empty() || ait->second.loop;
                            pose(av.fig, at, aloop);
                            drawFigure(av.fig, vp.data());
                        }
                    }
                }
            for (const cr3::EventSprite& sp : sprites)
                if (sp.layer == layer) {
                    const float a = sp.alpha(lead);
                    if (a <= 0) continue;
                    const cr3::Texture* tex = sp.textured ? texture(sp.texture) : (white.id ? &white : nullptr);
                    if (!tex || !tex->id) continue;
                    const float sw = sp.explicitSize ? sp.size[0] / 1920.0f * vw : sp.textured ? 1920.0f : vw;
                    const float sh = sp.explicitSize ? sp.size[1] / 1280.0f * vh : sp.textured ? 1920.0f : vh;
                    const float cover = std::max(vw / 1920.0f, vh / 1280.0f) + 0.01f;
                    const float sx = sp.explicitScale ? sp.scale[0] : sp.textured && !sp.explicitSize ? cover : sp.scale[0];
                    const float sy = sp.explicitScale ? sp.scale[1] : sp.textured && !sp.explicitSize ? cover : sp.scale[1];
                    const float cx = sp.pos[0] / (vw * 0.5f), cy = -sp.pos[1] / (vh * 0.5f);
                    const float hw = (sw * sx) / vw, hh = (sh * sy) / vh;
                    const float x0 = cx - hw, x1 = cx + hw, y0 = cy - hh, y1 = cy + hh;
                    const float quad[24] = {x0, y0, 0, 0, x1, y0, 1, 0, x1, y1, 1, 1, x0, y0, 0, 0, x1, y1, 1, 1, x0, y1, 0, 1};
                    const Mat4 id = cr3::mat4Identity();
                    const float rgba[4] = {sp.colourAt(0, lead), sp.colourAt(1, lead), sp.colourAt(2, lead), std::min(a, 1.0f)};
                    dev->drawQuad(*tex, quad, id.data(), rgba);
                }
        }
        if (game.viewportEnable) {
            int rw = frameW, rh = frameH;
            dev->sceneSize(frameW, frameH, rw, rh);
            const float kx = float(rw) / float(std::max(frameW, 1)), ky = float(rh) / float(std::max(frameH, 1));
            dev->setViewport(frameX * kx, frameY * ky, frameW * kx, frameH * ky);
        }
    }
}

void Viewer::loadWindow(WindowLayout& w, const char* figure) {
    std::string err;
    if (!loadFigure(std::string(figure) + ".geom", w.fig, &err)) SDL_Log("window %s: %s", figure, err.c_str());
    std::vector<uint8_t> d;
    if (archive.read(std::string(figure) + ".name", d)) w.joints = cr3::parseJointNames(d);
    for (const std::string& j : w.joints)
        if (j.compare(0, 17, "call_sendCsr_loop") == 0 || j.compare(0, 16, "call_msgCsr_loop") == 0) w.cursorJoint = j;
}

// a window's Position figure (CR3) and the joint it places the window at
void Viewer::loadPosition(WindowLayout& w, const char* figure, const char* call, float y) {
    std::string err;
    if (!loadFigure(std::string(figure) + ".geom", w.positionFig, &err)) {
        SDL_Log("window position %s: %s", figure, err.c_str());
        return;
    }
    std::vector<uint8_t> d;
    if (archive.read(std::string(figure) + ".name", d)) w.positionJoints = cr3::parseJointNames(d);
    w.positionCall = call, w.positionY = y;
}

void Viewer::loadMessageWindow(const std::string& fontPath, std::vector<uint8_t> fontBytes, const std::string& fontName, int fontFace) {
    if (archive.contains("comMsgVMenu.geom")) {
        // CR3's vista window (EvtVistaWindow: VistaWindowMenu) - comMsgVMenu, two centred lines
        // at typeM_C01 / C02, placed by comMsgVPosition; the speaker's name in comMsgVNameMenu
        // (typeM_L01) by comMsgVNamePosition, shown while someone speaks
        loadWindow(vistaWindow, "commsgvmenu");
        vistaWindow.slots = {"01", "02"};
        vistaWindow.uiUnit = vistaName.uiUnit = 10; // (its panel spans 12 units: wider than 16:9's, not stretched there)
        // (a phone wider than 1.875:1 - 19.5:9, 20:9 - would leave its ends short of the screen's
        // while the speaker's name keeps to the edge: the panel stretched to the screen there)
        vistaWindow.span = 1.25f;
        loadPosition(vistaWindow, "commsgvposition", "call_comMsgVMenu", -0.03f);
        vistaWindow.lineSize = 32, vistaWindow.blockLines = true; // (VistaWindowMenu::SetStringData: lines 32, the name 28)
        loadWindow(vistaName, "commsgvnamemenu");
        vistaName.slots = {"01"}, vistaName.speakerOnly = true;
        vistaName.nameSize = vistaName.lineSize = 28, vistaName.nameDx = -0.01f, vistaName.nameDy = 0.01f; // (its border's 0.01: SetStringData's 0.07 the Pose after it drops)
        loadPosition(vistaName, "commsgvnameposition", "call_comMsgVNameMenu", -0.03f);
    } else {
        loadWindow(vistaWindow, "vstwin");
        vistaWindow.stretch = vistaWindow.nameTag = true;
        vistaWindow.slots = {"01", "11", "12", "21", "22"};
    }
    // the cut-ins' message window (ConversationWindowMenu: msgWin, msgWin_in / msgWin_out; its
    // slots 01 the name, 02..04 a page's three lines - EvtCutinWindow::GetLinePerPage - and
    // 05..07 centred ones)
    if (archive.contains("comMsgMenu.geom")) {
        // CR3's (EvtCutinWindow: MessageWindowMenu) - comMsgMenu at comMsgPosition's
        // call_comMsgMenu_fit2: the speaker's name at typeM_L01, a page's three lines at L02..L04
        loadWindow(conversationWindow, "commsgmenu");
        conversationWindow.nameTag = true, conversationWindow.linesPerPage = 3, conversationWindow.uiUnit = 10;
        conversationWindow.slots = {"01", "02", "03", "04"};
        conversationWindow.window = cr3::EventRuntime::CutIn;
        conversationWindow.nameSize = 30, conversationWindow.lineSize = 32; // (MessageWindowMenu::SetStringData)
        loadPosition(conversationWindow, "commsgposition", "call_comMsgMenu", 0);
        // (its own animation places the panel's parts - absolute, slot 0 -, played as it opens:
        // SetParameterDataBase(.., 0, 1/6 s, play); closing it back)
        conversationWindow.inAnim = "commsgmenu.anim", conversationWindow.outBackwards = true;
        conversationWindow.lineRgb[0] = 76 / 255.0f, conversationWindow.lineRgb[1] = 83 / 255.0f, conversationWindow.lineRgb[2] = 91 / 255.0f;
        conversationWindow.fig.absolute = true;
    } else {
        loadWindow(conversationWindow, "msgwin");
        conversationWindow.stretch = conversationWindow.nameTag = conversationWindow.lowerBelow3x2 = true;
        conversationWindow.linesPerPage = 3;
        conversationWindow.slots = {"01", "02", "03", "04", "05", "06", "07"};
        conversationWindow.window = cr3::EventRuntime::CutIn;
        conversationWindow.inAnim = "msgwin_in.anim", conversationWindow.outAnim = "msgwin_out.anim";
    }
    loadCutIns();
    if (archive.contains("ttlMnlMenu.geom")) {
        loadWindow(tutorialMenuWindow, "ttlmnlmenu");
        tutorialMenuWindow.slots = {"01", "02", "03", "04"}, tutorialMenuWindow.linesPerPage = 4, tutorialMenuWindow.uiUnit = 10;
        tutorialMenuWindow.nameSize = tutorialMenuWindow.lineSize = 30, tutorialMenuWindow.window = cr3::EventRuntime::TutorialMenu;
        loadPosition(tutorialMenuWindow, "ttlmnlpositon", "call_ttlMnlMenu", 0);
        tutorialMenuWindow.fig.absolute = true;
        loadCr3Part(tutorialMenuPicture, "ttlmnlpositon", "ttlmnlimg_fr00", "call_ttlMnlImg_fr00", "");
    }
    if (archive.contains("comMsgAMenu.geom")) {
        // CR3's FieldAnnouncementMenu (0x00a64e14..0x00a659fc): comMsgAMenu at comMsgAPosition's
        // call_comMsgAMenu_fit8, slots typeM_L01..03 and typeM_C04..06, size 32, warm ivory tint.
        // Top (2, 4, 8): y = 0, fit8; Middle (3, 9): y = -2.1, fit cancelled; Bottom (10): y = -4.2, fit2.
        for (auto [wl, window, py, fit] : {std::tuple<WindowLayout*, int, float, int>{&announceWindow, cr3::EventRuntime::AnnounceA, 0.0f, 0},
                                           {&annoWindow, cr3::EventRuntime::Tutorial, 0.0f, 0},
                                           {&announceTopWindow, cr3::EventRuntime::AnnounceTop, 0.0f, 0},
                                           {&helpWindow, cr3::EventRuntime::AnnounceB, -2.1f, -1},
                                           {&announceMidWindow, cr3::EventRuntime::AnnounceMid, -2.1f, -1},
                                           {&announceBotWindow, cr3::EventRuntime::AnnounceBot, -4.2f, 2}}) {
            loadWindow(*wl, "commsgamenu");
            wl->linesPerPage = 3, wl->centredSlots = true, wl->uiUnit = 10, wl->lineSize = 32;
            wl->slots = {"01", "02", "03", "04", "05", "06"};
            wl->window = window, wl->fitOverride = fit;
            loadPosition(*wl, "commsgaposition", "call_comMsgAMenu", py);
            wl->lineRgb[0] = 1.0f, wl->lineRgb[1] = 0.898039f, wl->lineRgb[2] = 0.886275f;
            wl->inAnim = "commsgamenu.anim", wl->outBackwards = true, wl->fig.absolute = true;
        }
        loadCr3Part(announcePageCursor, "commsgpbtnaposition", "commsgpbtnmenu", "call_comMsgPBtnMenu", "commsgpbtnmenu_loop");
    } else {
        for (auto [wl, name, window] : {std::tuple<WindowLayout*, const char*, int>{&helpWindow, "helpwin", cr3::EventRuntime::AnnounceB},
                                         {&annoWindow, "annowin", cr3::EventRuntime::Tutorial},
                                         {&announceWindow, "annowin", cr3::EventRuntime::AnnounceA}}) {
            loadWindow(*wl, name);
            wl->linesPerPage = 3, wl->centredSlots = wl->stretch = true;
            wl->slots = {"01", "02", "03", "11", "12", "13"};
            wl->window = window;
            wl->inAnim = std::string(name) + "_in.anim", wl->outAnim = std::string(name) + "_out.anim";
        }
    }
    if (archive.contains("comMsgSMenu.geom")) {
        // CR3's system window (EvtSystemWindow, window 5: SysWindowMenu, interface 0xb) - comMsgSMenu at
        // comMsgSPosition's call_comMsgSMenu_fit2 (its move offset 2 is depth), three lines at
        // typeM_L05..L07 (SetStringData: size 32, (0.282, 0.306, 0.306), left)
        loadWindow(systemWindow, "commsgsmenu");
        systemWindow.linesPerPage = 3, systemWindow.slots = {"05", "06", "07"}, systemWindow.window = cr3::EventRuntime::System;
        systemWindow.uiUnit = 10, systemWindow.lineSize = 32;
        loadPosition(systemWindow, "commsgsposition", "call_comMsgSMenu", 0);
        systemWindow.inAnim = "commsgsmenu.anim", systemWindow.outBackwards = true;
        systemWindow.lineRgb[0] = 0.282f, systemWindow.lineRgb[1] = systemWindow.lineRgb[2] = 0.306f;
        systemWindow.fig.absolute = true;
    } else {
        loadWindow(systemWindow, "syswin_io");
        systemWindow.linesPerPage = 3, systemWindow.slots = {"01", "02", "03"}, systemWindow.window = cr3::EventRuntime::System;
        systemWindow.inAnim = "syswin_io.anim", systemWindow.outBackwards = true;
        systemWindow.fig.absolute = true; // (the figure's own animator: SetParameterDataBase's)
    }
    if (archive.contains("getitem.geom")) {
        loadWindow(itemWindow, "getitem");
        itemWindow.stretch = true, itemWindow.linesPerPage = 1, itemWindow.slots = {"01"}, itemWindow.window = 7;
        itemWindow.inAnim = "getitem_in.anim", itemWindow.outAnim = "getitem_out.anim";
    }
    if (archive.contains("comMsgNMenu.geom")) {
        // CR3's NarrationMenu (0x00aea9c0..0x00aeb720): comMsgNMenu at comMsgNPosition's
        // call_comMsgNMenu_fit0, 10 centred lines typeM_C01..10, size 30, white with near-black shadow.
        loadWindow(narrationWindow, "commsgnmenu");
        narrationWindow.uiUnit = 10, narrationWindow.lineSize = 30, narrationWindow.window = cr3::EventRuntime::Narration;
        narrationWindow.lineShadow = true;
        narrationWindow.lineShadowRgb[0] = narrationWindow.lineShadowRgb[1] = narrationWindow.lineShadowRgb[2] = 1.0f / 255.0f;
        for (int k = 1; k <= 10; ++k) {
            char n[4];
            std::snprintf(n, sizeof n, "%02d", k);
            narrationWindow.slots.push_back(n);
        }
        loadPosition(narrationWindow, "commsgnposition", "call_comMsgNMenu", 0);
        narrationWindow.inAnim = "commsgnmenu.anim", narrationWindow.outBackwards = true, narrationWindow.fig.absolute = true;
    } else {
        loadWindow(narrationWindow, "narrationpos");
        for (int k = 1; k <= 30; ++k) {
            char n[4];
            std::snprintf(n, sizeof n, "%02d", k);
            narrationWindow.slots.push_back(n);
        }
    }
    if (archive.contains("comMsgFBPosition.geom")) {
        loadCr3Part(vistaFastButton, "commsgfbposition", "commsgfbmenu", "call_comMsgFBMenu", "");
        loadCr3Part(vistaStopButton, "commsgfbposition", "commsgstmenu", "call_comMsgSTMenu", "");
    }
    std::string err;
    if (!loadFigure("sendcsr_loop.geom", pageCursor, &err)) SDL_Log("page cursor: %s", err.c_str());
    if (!loadFigure("msgcsr_loop.geom", conversationCursor, &err)) SDL_Log("page cursor: %s", err.c_str());
    if (!fontPath.empty() && !font.load(fontPath, &err)) SDL_Log("font: %s", err.c_str());
    else if (fontPath.empty() && !fontBytes.empty() && !font.loadMemory(std::move(fontBytes), fontName, &err, fontFace)) SDL_Log("font: %s", err.c_str());
    // (the code points the game's font lacks - Hangul, simplified Chinese - from the system's, as
    // Android's: CR3_FONT_FALLBACK, else a Noto CJK where the distributions put it, else Windows'
    // or macOS's own - one for each script there)
    const char* fb = std::getenv("CR3_FONT_FALLBACK");
    const char* windir = std::getenv("WINDIR");
    const std::string winFonts = std::string(windir ? windir : "C:/Windows") + "/Fonts/";
    if (!font.loaded()) {
        for (const std::string& path : std::vector<std::string>{
                 "/system/fonts/Roboto-Regular.ttf", "/system/fonts/Roboto.ttf", "/system/fonts/NotoSans-Regular.ttf",
                 "/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                 "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", winFonts + "segoeui.ttf",
                 winFonts + "arial.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Helvetica.ttc"}) {
            if (font.load(path)) break;
        }
    }
    std::vector<std::string> fallbacks{cr3::TextRasterizer::firstFont({fb ? fb : "", "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
                                                                       "/system/fonts/NotoSansCJK-Regular.ttc",
                                                                       "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
                                                                       "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Bold.ttc",
                                                                       "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                                                                       "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"})};
    if (fallbacks[0].empty())
        fallbacks = {cr3::TextRasterizer::firstFont({winFonts + "malgunbd.ttf", winFonts + "malgun.ttf",
                                                     "/System/Library/Fonts/AppleSDGothicNeo.ttc"}),
                     cr3::TextRasterizer::firstFont({winFonts + "msyhbd.ttc", winFonts + "msyh.ttc",
                                                     "/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/Hiragino Sans GB.ttc"})};
    std::string fallback;
    for (const std::string& f : fallbacks) {
        if (f.empty() || !font.loaded()) continue;
        if (!font.addFallback(f, &err)) SDL_Log("font: %s", err.c_str());
        else fallback += (fallback.empty() ? "" : ", ") + f;
    }
    SDL_Log("message windows: vista %zu meshes, narration %zu joints; font %s%s%s", vistaWindow.fig.meshes.size(),
            narrationWindow.joints.size(), font.loaded() ? font.path().c_str() : "none (no text)", fallback.empty() ? "" : ", then ",
            fallback.c_str());
}

// the interface's projection over a w x h screen: UI units to clip space (z: the camera's
// near / far, 2.56 either way)
Mat4 Viewer::uiProjection(int w, int h, float unit) {
    const float k = std::min(float(w) / (0.96f * unit), float(h) / (0.64f * unit)); // pixels a unit
    Mat4 m = cr3::mat4Identity();
    m[0] = k / (float(w) * 0.5f);
    m[5] = k / (float(h) * 0.5f);
    m[10] = -1.0f / (2.56f * unit);
    return m;
}

// CR3's fit (a placement joint's _fitN, CRXPartsBase::SearchOffsetJointPositionAnyString): a
// numpad anchor - 1..3 the foot, 7..9 the top, 1 4 7 left, 3 6 9 right - moved by the screen's
// room beyond 9.6 x 6.4 (GetFitX/YOffset's tables: 16:9 +-0.88 across, 4:3 +-0.4 up and down)
void Viewer::fitOffset(const std::string& joint, int w, int h, float unit, float& dx, float& dy) {
    dx = dy = 0;
    const size_t at = joint.rfind("_fit");
    if (at == std::string::npos || at + 4 >= joint.size() || !std::isdigit(static_cast<unsigned char>(joint[at + 4]))) return;
    const int n = joint[at + 4] - '0';
    if (n < 1 || n > 9) return;
    const float k = std::min(float(w) / (0.96f * unit), float(h) / (0.64f * unit));
    const float roomX = (float(w) / k - 0.96f * unit) / 2, roomY = (float(h) / k - 0.64f * unit) / 2;
    dx = float((n - 1) % 3 - 1) * roomX;
    dy = float((n - 1) / 3 - 1) * roomY;
}

// a joint of a window's figure, posed, in UI units
bool Viewer::windowJoint(const WindowLayout& wl, const std::string& name, float& x, float& y) {
    const auto it = std::find(wl.joints.begin(), wl.joints.end(), name);
    if (it == wl.joints.end() || !wl.fig.skinned) return false;
    std::vector<cr3::JointTransform> p = wl.fig.skeleton.basePose;
    if (wl.fig.animLoaded)
        p = wl.fig.absolute ? wl.fig.anim.poseAbsolute(wl.fig.skeleton.basePose, wl.frame) : wl.fig.anim.poseOver(wl.fig.skeleton.basePose, wl.frame);
    const std::vector<cr3::Mat34> w = wl.fig.skeleton.world(p, wl.fig.place);
    const size_t j = size_t(it - wl.joints.begin());
    if (j >= w.size()) return false;
    x = w[j].m[3], y = w[j].m[7];
    return true;
}

// a slot's text joint (type?_?NN, NN the slot's number)
std::string Viewer::slotJoint(const WindowLayout& wl, size_t slot) {
    if (slot >= wl.slots.size()) return {};
    for (const std::string& j : wl.joints)
        if (j.size() == 9 && j.compare(0, 4, "type") == 0 && j.compare(7, 2, wl.slots[slot]) == 0) return j;
    return {};
}

void Viewer::drawWindow(WindowLayout& wl, bool showsMessage, int w, int h) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev) return;
    const float aspect = float(w) / float(std::max(h, 1));
    wl.fig.place = cr3::Mat34();
    if (wl.stretch && aspect >= 1.5f) wl.fig.place.m[0] = aspect / 1.5f;
    else if (wl.span > 0 && aspect / 1.5f > wl.span) wl.fig.place.m[0] = aspect / 1.5f / wl.span;
    else if (wl.lowerBelow3x2) wl.fig.place.m[7] = -0.0403125f;
    if (!wl.positionCall.empty() && wl.positionFig.skinned) { // (CR3: at its Position figure's joint, fitted)
        const std::vector<cr3::Mat34> pw = wl.positionFig.skeleton.world(wl.positionFig.skeleton.basePose);
        for (size_t j = 0; j < wl.positionJoints.size() && j < pw.size(); ++j)
            if (wl.positionJoints[j].compare(0, wl.positionCall.size(), wl.positionCall) == 0) {
                float fx = 0, fy = 0;
                const std::string fitJoint = wl.fitOverride < 0 ? std::string()
                                           : wl.fitOverride > 0 ? "_fit" + std::to_string(wl.fitOverride)
                                                                : wl.positionJoints[j];
                fitOffset(fitJoint, w, h, wl.uiUnit, fx, fy);
                wl.fig.place.m[3] += pw[j].m[3] + fx, wl.fig.place.m[7] += pw[j].m[7] + wl.positionY + fy;
                break;
            }
    }
    const bool closing = wl.window >= 0 && ev->windowClosing(wl.window);
    if (closing) showsMessage = false;
    if (wl.speakerOnly && (ev->speakerName().empty() || !showsMessage)) return; // (CR3's name window: only while someone speaks)
    struct Swap { // (its own text cache while it draws)
        Viewer* v;
        bool on;
        void swap() { std::swap(v->textQuads, v->nameQuads), std::swap(v->shownText, v->nameText), std::swap(v->shownPx, v->namePx); }
        Swap(Viewer* v_, bool on_) : v(v_), on(on_) { if (on) swap(); }
        ~Swap() { if (on) swap(); }
    } own(this, wl.speakerOnly);
    const std::string speaker = showsMessage && (wl.nameTag || wl.speakerOnly) ? ev->speakerName() : std::string();
    float at = 0; // (where in its animation)
    if (!wl.inAnim.empty()) { // (opening, then closing)
        const std::string want = closing && !wl.outBackwards ? wl.outAnim : wl.inAnim;
        const auto anim = std::find(wl.fig.anims.begin(), wl.fig.anims.end(), want);
        if (anim != wl.fig.anims.end() && (!wl.fig.animLoaded || wl.fig.anims[wl.fig.animIndex] != want)) {
            const float keep = time;
            loadAnim(wl.fig, size_t(anim - wl.fig.anims.begin()));
            time = keep;
        }
        at = std::max(ev->windowAge(wl.window), 0.0f) + eventLead();
        const float dur = wl.lineSize > 0 && wl.outBackwards ? 5 * cr3::EventRuntime::kTick : (wl.fig.animLoaded ? wl.fig.anim.duration() : 0.0f);
        if (closing && dur > 0 && at >= dur) return;
        if (closing && wl.outBackwards && wl.fig.animLoaded) at = std::max(dur - at, 0.0f);
        else if (dur > 0) at = std::min(at, dur);
    } else if (wl.nameTag) { // (the vista's: the name line in while someone speaks)
        const std::string want = speaker.empty() ? "vstwin_io.anim" : "vstwin_talk.anim";
        const auto anim = std::find(wl.fig.anims.begin(), wl.fig.anims.end(), want);
        if (anim != wl.fig.anims.end() && (!wl.fig.animLoaded || wl.fig.anims[wl.fig.animIndex] != want)) {
            const float keep = time;
            loadAnim(wl.fig, size_t(anim - wl.fig.anims.begin()));
            time = keep;
        }
    }
    pose(wl.fig, at, false);
    wl.frame = wl.fig.animLoaded ? std::min(at * wl.fig.anim.sampleRate(), float(std::max(wl.fig.anim.frames() - 1, 1))) : 0;
    const Mat4 proj = uiProjection(w, h, wl.uiUnit);
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * wl.uiUnit; // (CAM_hud2D's z)
    const Mat4 view = cr3::mat4Multiply(proj, camera);
    cr3::SceneLighting l;
    dev->setLighting(l);
    if (!wl.fig.meshes.empty()) drawFigure(wl.fig, view.data());
    if (!showsMessage) return;
    // the page's text: made again when it changes (or the screen's scale does); sizes in pixels
    // of the interface's 960 x 640 (CR2's 1000 a unit, CR3's 100)
    const float k = std::min(float(w) / (0.96f * wl.uiUnit), float(h) / (0.64f * wl.uiUnit));
    const float pxUnit = 1000.0f / wl.uiUnit;
    const int px = int(k + 0.5f);
    std::string key = std::to_string(ev->messageWindow()) + "\n" + speaker + "\n" + std::to_string(ev->page()) + "\n";
    for (const cr3::TextLine& ln : ev->message().pages[size_t(ev->page())].lines)
        key += std::string(ln.centred ? "C:" : "L:") + std::to_string(ln.colour) + ":" + ln.text + "\n";
    if (key != shownText || px != shownPx) {
        for (TextQuad& q : textQuads) dev->destroy(q.tex);
        textQuads.clear();
        shownText = key, shownPx = px;
        int typedBefore = 0; // (the page's characters before the line: it types after them)
        auto add = [&](const std::string& text, size_t slot, int colour, bool whole) {
            int chars = 0;
            for (unsigned char c : text) chars += (c & 0xc0) != 0x80;
            const int from = whole ? -1 : typedBefore;
            if (!whole) typedBefore += chars;
            const std::string joint = slotJoint(wl, slot);
            float jx, jy;
            if (text.empty() || joint.empty() || !font.loaded() || !windowJoint(wl, joint, jx, jy)) return;
            // (CR2: M 32, S 28; CR3's CRXPartsBase::SearchTextPosition: M 30, S 28; else 64)
            const int size = joint[4] == 'M' ? (wl.uiUnit > 1 ? 30 : 32) : joint[4] == 'S' ? 28 : 64;
            const bool centred = joint[6] == 'C', right = joint[6] == 'R';
            const int drawn = size - 4; // (SetTextRender: the size less 4)
            const int rasterPx = std::max(1, int(float(drawn) / pxUnit * k + 0.5f));
            int tw = 0, th = 0;
            std::vector<float> pen;
            cr3::Image img = font.render(text, rasterPx, tw, th, &pen);
            if (img.rgba.empty()) return;
            TextQuad q;
            q.tex = dev->upload(img);
            q.from = from;
            const float unitsPerPx = float(drawn) / pxUnit / float(rasterPx);
            for (float p : pen) q.pen.push_back(p * unitsPerPx);
            q.w = float(tw) * unitsPerPx, q.h = float(th) * unitsPerPx, q.below = float(img.height - th) * unitsPerPx;
            if (wl.lineSize) {
                // CR3's (64-bit ProvisionalMenuText; s the size its window asks for, a unit 200
                // half-pixels): the window's Pose moves the joint (s / 2) / 200 right and down
                // (the vista's name its border's 0.01 - Initialize type 2 - back); SetTextOffsetData / SetPosition centre a quad W = width + s
                // wide, its text at its left: L at the joint + (W - s) / 200 + 3s / 1600, C on
                // it, R at (s - W) / 200; the middle (s - height) / 200 - 3s / 1600 off it
                const float s = float(whole ? wl.nameSize : wl.lineSize) / pxUnit;
                const int type = joint[6] == 'L' ? 0 : joint[6] == 'C' ? 1 : joint[6] == 'R' ? 2 : 3;
                const float px = jx + s / 4 + (whole ? wl.nameDx : 0), py = jy - s / 4 + (whole ? wl.nameDy : 0);
                const float W = q.w + s;
                const float cx = type == 1 ? px : type == 2 ? px + (s - W) / 2 : px + (W - s) / 2 + (type == 0 ? 3 * s / 16 : 0);
                q.x = cx - W / 2;
                q.y = py + (s - q.h) / 2 - 3 * s / 16;
            } else {
                // SetTextOffsetData: left - the joint less half the size; centred - on it; right -
                // ending there; the line's middle (size - height) / 2 from the joint
                q.x = centred ? jx - q.w / 2 : right ? jx - q.w + float(drawn) / (2 * pxUnit) : jx - float(drawn) / (2 * pxUnit);
                q.y = jy + (float(drawn) / pxUnit - q.h) / 2;
            }
            q.joint = joint, q.dx = q.x - jx, q.dy = q.y - jy;
            if (whole && wl.speakerOnly) q.border = 2 / pxUnit; // (the vista's name: Initialize type 2 - bordered black, 2 pixels where the UI isn't the iPhone's)
            if (whole && wl.nameTag && wl.lineSize) // (CR3's conversation name: MessageWindowMenu::SetStringData's type 3, (0.361, 0.349, 0.380) behind)
                q.shadow = true, q.shadowRgb[0] = 0.361f, q.shadowRgb[1] = 0.349f, q.shadowRgb[2] = 0.380f;
            if (!whole && wl.lineShadow)
                q.shadow = true, q.shadowRgb[0] = wl.lineShadowRgb[0], q.shadowRgb[1] = wl.lineShadowRgb[1], q.shadowRgb[2] = wl.lineShadowRgb[2];
            cr3::textColour(colour, q.rgb);
            if (colour == 0 && !whole)
                for (int c = 0; c < 3; ++c) q.rgb[c] = wl.lineRgb[c]; // (the name stays white)
            textQuads.push_back(q);
        };
        const std::vector<cr3::TextLine>& lines = ev->message().pages[size_t(ev->page())].lines;
        if (wl.speakerOnly) {
            add(speaker, 0, 0, true);
        } else if (wl.nameTag) {
            add(speaker, 0, 0, true);
            for (size_t i = 0; i < lines.size() && i < size_t(wl.linesPerPage); ++i) {
                const size_t base = lines[i].centred && size_t(1 + wl.linesPerPage) + i < wl.slots.size() ? size_t(1 + wl.linesPerPage) : 1;
                add(lines[i].text, base + i, lines[i].colour, false);
            }
        } else if (wl.centredSlots) {
            for (size_t i = 0; i < lines.size() && i < size_t(wl.linesPerPage); ++i) {
                const size_t base = lines[i].centred && size_t(wl.linesPerPage) + i < wl.slots.size() ? size_t(wl.linesPerPage) : 0;
                add(lines[i].text, base + i, lines[i].colour, false);
            }
        } else {
            for (size_t i = 0; i < lines.size(); ++i) add(lines[i].text, i, lines[i].colour, false);
        }
        if (wl.blockLines) { // (VistaWindowMenu::SetStringData: the narrower line's +0x4c8 half the difference back - one block)
            float widest = 0;
            for (const TextQuad& q : textQuads)
                if (q.from >= 0) widest = std::max(widest, q.w);
            for (TextQuad& q : textQuads)
                if (q.from >= 0) q.x -= (widest - q.w) / 2, q.dx -= (widest - q.w) / 2;
        }
    }
    for (TextQuad& q : textQuads) {
        float jx, jy; // (following its joint: a window sliding in)
        if (!q.joint.empty() && windowJoint(wl, q.joint, jx, jy)) q.x = jx + q.dx, q.y = jy + q.dy;
        // as far as it has typed (MenuText: the characters shown so far, the rest cut off)
        float tw = q.w;
        if (q.from >= 0 && !q.pen.empty()) {
            const int n = std::clamp(ev->revealed() - q.from, 0, int(q.pen.size()) - 1);
            if (n == 0) continue;
            tw = std::min(q.w, q.pen[size_t(n)]);
        }
        const float u1 = q.w > 0 ? tw / q.w : 1;
        const float x0 = q.x, x1 = q.x + tw, y0 = q.y - q.h / 2 - q.below, y1 = q.y + q.h / 2; // (rows top first: v 0 at the top)
        const float v[24] = {x0, y0, 0, 1, x1, y0, u1, 1, x1, y1, u1, 0, x0, y0, 0, 1, x1, y1, u1, 0, x0, y1, 0, 0};
        if (q.border > 0) { // (its outline: the text in black, around it)
            static const float kRound[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7071f, 0.7071f}, {-0.7071f, 0.7071f}, {0.7071f, -0.7071f}, {-0.7071f, -0.7071f}};
            const float black[4] = {0, 0, 0, 1};
            for (const auto& d : kRound) {
                float o[24];
                for (int i = 0; i < 24; i += 4) o[i] = v[i] + d[0] * q.border, o[i + 1] = v[i + 1] + d[1] * q.border, o[i + 2] = v[i + 2], o[i + 3] = v[i + 3];
                dev->drawQuad(q.tex, o, proj.data(), black);
            }
        }
        if (q.shadow) {
            const float dx = 1 / pxUnit, dy = -1.72f / pxUnit;
            float o[24];
            for (int i = 0; i < 24; i += 4) o[i] = v[i] + dx, o[i + 1] = v[i + 1] + dy, o[i + 2] = v[i + 2], o[i + 3] = v[i + 3];
            const float back[4] = {q.shadowRgb[0], q.shadowRgb[1], q.shadowRgb[2], 1};
            dev->drawQuad(q.tex, o, proj.data(), back);
        }
        const float rgba[4] = {q.rgb[0], q.rgb[1], q.rgb[2], 1};
        dev->drawQuad(q.tex, v, proj.data(), rgba);
    }
    // the page cursor, looping, once the page is typed
    float cx, cy;
    ViewFigure& cursor = wl.cursorJoint.compare(0, 16, "call_msgCsr_loop") == 0 ? conversationCursor : pageCursor;
    if (ev->awaitingTap() && !cursor.meshes.empty() && !wl.cursorJoint.empty() && windowJoint(wl, wl.cursorJoint, cx, cy)) {
        cursor.place = cr3::Mat34();
        cursor.place.m[3] = cx, cursor.place.m[7] = cy;
        pose(cursor, time);
        drawFigure(cursor, view.data());
    }
}

void Viewer::loadCr3CutIns() {
    static const char* kSide[3] = {"R", "", "L"};
    const float keep = time;
    for (int k : {0, 2}) {
        Cr3CutIn& c = cr3CutIns[size_t(k)];
        const std::string side = kSide[k], pos = "commsgcut" + std::string(k == 0 ? "r" : "l") + "position";
        std::string err;
        if (!loadFigure(pos + ".geom", c.position, &err) || !loadFigure("commsgcut" + std::string(k == 0 ? "r" : "l") + "_id00.geom", c.portrait, &err)) {
            SDL_Log("cut-in %s: %s", side.c_str(), err.c_str());
            continue;
        }
        std::vector<uint8_t> d;
        if (archive.read(pos + ".name", d)) c.joints = cr3::parseJointNames(d);
        c.call = "call_comMsgCut" + side + "Menu";
        const auto io = std::find(c.position.anims.begin(), c.position.anims.end(), pos + "_io.anim");
        if (io != c.position.anims.end()) loadAnim(c.position, size_t(io - c.position.anims.begin()));
        c.position.absolute = false; // (slot 1: over its base)
        c.portrait.absolute = true;  // (its own, slot 0)
        c.loaded = true;
    }
    Cr3CutIn& c = cr3CutIns[1];
    std::string err;
    if (loadFigure("commsgcenterposition.geom", c.position, &err) && loadFigure("commsgcentermenu.geom", c.menu, &err) &&
        loadFigure("commsgcenter_id00.geom", c.portrait, &err) && loadFigure("commsgeff_01.geom", c.effect, &err)) {
        std::vector<uint8_t> d;
        if (archive.read("commsgcenterposition.name", d)) c.joints = cr3::parseJointNames(d);
        if (archive.read("commsgcentermenu.name", d)) c.menuJoints = cr3::parseJointNames(d);
        c.call = "call_comMsgCenterMenu";
        for (auto [f, anim] : {std::pair<ViewFigure*, const char*>{&c.menu, "commsgcentermenu_in.anim"}, {&c.portrait, "commsgcenter_id00.anim"},
                               {&c.effect, "commsgeff_01.anim"}}) {
            const auto a = std::find(f->anims.begin(), f->anims.end(), anim);
            if (a != f->anims.end()) loadAnim(*f, size_t(a - f->anims.begin()));
            f->absolute = true;
        }
        c.loaded = true;
    } else SDL_Log("cut-in centre: %s", err.c_str());
    time = keep;
    cr3CutInsLoaded = cr3CutIns[0].loaded || cr3CutIns[2].loaded;
}

void Viewer::drawCr3CentreCutIn(Cr3CutIn& c, const cr3::EventRuntime::Portrait& p, cr3::EventRuntime& ev, int w, int h, const float* view) {
    const float t = p.age + eventLead();
    // (closing: a quarter of its alpha off each update - InterfaceCloseAnimeSet's 4)
    const float fade = p.closing ? std::clamp((p.time - eventLead()) / cr3::EventRuntime::kCentreCloseSeconds, 0.0f, 1.0f) : 1.0f;
    const std::vector<cr3::Mat34> pw = c.position.skeleton.world(c.position.skeleton.basePose);
    c.menu.place = cr3::Mat34();
    for (size_t j = 0; j < c.joints.size() && j < pw.size(); ++j)
        if (c.joints[j].compare(0, c.call.size(), c.call) == 0) {
            float fx, fy;
            fitOffset(c.joints[j], w, h, 10, fx, fy);
            c.menu.place.m[3] = pw[j].m[3] + fx, c.menu.place.m[7] = pw[j].m[7] + fy;
        }
    pose(c.menu, t, false);
    c.menu.tint[3] = fade;
    cr3::SceneLighting l;
    dev->setLighting(l);
    if (fade > 0) drawFigure(c.menu, view);
    // the portrait at call_comMsgCenter_id00, its scale the joint's
    const std::vector<cr3::Mat34> mw = c.menu.skeleton.world(c.menu.posed.empty() ? c.menu.skeleton.basePose : c.menu.posed, c.menu.place);
    c.portrait.place = cr3::Mat34();
    for (size_t j = 0; j < c.menuJoints.size() && j < mw.size(); ++j)
        if (c.menuJoints[j] == "call_comMsgCenter_id00") {
            const cr3::Mat34& m = mw[j];
            for (int r = 0; r < 3; ++r) {
                const float sc = std::sqrt(m.m[r] * m.m[r] + m.m[4 + r] * m.m[4 + r] + m.m[8 + r] * m.m[8 + r]); // (the column's length)
                c.portrait.place.m[r * 5] = sc;
            }
            c.portrait.place.m[3] = m.m[3], c.portrait.place.m[7] = m.m[7];
        }
    const float alpha = (t < 1.0f / 6 ? 0.0f : std::min((t - 1.0f / 6) * 6, 1.0f)) * fade;
    if (alpha > 0) {
        pose(c.portrait, 0, false);
        cr3::Texture* face = texture(ev.cutInImage(p.texture));
        for (ViewMesh& vm : c.portrait.meshes) {
            vm.texture = face ? face : texture(vm.baseMaterial.diffuse());
            std::vector<float>& dc = vm.material.params[0x33];
            if (dc.size() < 4) dc.resize(4, 1.0f);
            dc[0] = dc[1] = dc[2] = p.bright;
        }
        c.portrait.tint[3] = alpha;
        drawFigure(c.portrait, view);
    }
    // the burst: from 0.367 s (its +0x48 timer) till its animation ends
    const float et = t - 0.36666667f;
    if (et >= 0 && c.effect.animLoaded && et <= c.effect.anim.duration() && !p.closing) {
        c.effect.place = cr3::Mat34();
        c.effect.place.m[0] = c.effect.place.m[5] = 10;
        c.effect.place.m[3] = c.portrait.place.m[3], c.effect.place.m[7] = c.portrait.place.m[7];
        pose(c.effect, et, false);
        drawFigure(c.effect, view);
    }
}

void Viewer::drawCr3CutIns(int w, int h) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev) return;
    const float unit = 10;
    const Mat4 proj = uiProjection(w, h, unit);
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * unit;
    const Mat4 view = cr3::mat4Multiply(proj, camera);
    for (int k : {0, 2}) {
        Cr3CutIn& c = cr3CutIns[size_t(k)];
        const cr3::EventRuntime::Portrait& p = ev->cutIn(k);
        if (!c.loaded || !p.open) continue;
        // the slide: in over 0.2 s (CutInMenu::Initialize), out the same backwards; alpha in
        // over 8/30 s, out over 4/30 s (CutInMenu::Pose)
        const float dur = c.position.animLoaded ? std::min(c.position.anim.duration(), cr3::EventRuntime::kCutInSeconds) : 0.0f;
        const float age = p.closing ? std::max(p.time - eventLead(), 0.0f) : p.time + eventLead();
        const float t = std::clamp(age / cr3::EventRuntime::kCutInSeconds * dur, 0.0f, dur);
        pose(c.position, t, false);
        float ax = 0, ay = 0;
        std::string joint;
        for (size_t j = 0; j < c.joints.size() && j < c.position.posed.size(); ++j)
            if (c.joints[j].compare(0, c.call.size(), c.call) == 0) joint = c.joints[j];
        {
            std::vector<cr3::JointTransform> pz = c.position.skeleton.basePose;
            if (c.position.animLoaded) pz = c.position.anim.poseOver(c.position.skeleton.basePose, std::min(t * c.position.anim.sampleRate(), float(std::max(c.position.anim.frames() - 1, 1))));
            const std::vector<cr3::Mat34> wm = c.position.skeleton.world(pz);
            for (size_t j = 0; j < c.joints.size() && j < wm.size(); ++j)
                if (c.joints[j] == joint) ax = wm[j].m[3], ay = wm[j].m[7];
        }
        float fx, fy;
        fitOffset(joint, w, h, unit, fx, fy);
        // (a taller screen's: CutInMenu takes 0.4 off for screen type 1 - the room above and below)
        const float roomY = std::max(float(h) / std::min(float(w) / (0.96f * unit), float(h) / (0.64f * unit)) - 0.64f * unit, 0.0f) / 2;
        c.portrait.place = cr3::Mat34();
        c.portrait.place.m[3] = ax + fx, c.portrait.place.m[7] = ay + fy - roomY - (p.shop ? 0.87f : 0.0f); // (CutInMenu::Pose: a shop's clerk lower)
        const float alpha = p.closing ? std::clamp(age / (4 * cr3::EventRuntime::kTick), 0.0f, 1.0f)
                                      : std::clamp((p.age + eventLead()) / (8 * cr3::EventRuntime::kTick), 0.0f, 1.0f);
        if (alpha <= 0) continue;
        pose(c.portrait, 0, false);
        cr3::Texture* face = texture(ev->cutInImage(p.texture));
        for (ViewMesh& vm : c.portrait.meshes) {
            vm.texture = face ? face : texture(vm.baseMaterial.diffuse());
            std::vector<float>& dc = vm.material.params[0x33];
            if (dc.size() < 4) dc.resize(4, 1.0f);
            dc[0] = dc[1] = dc[2] = p.bright;
        }
        c.portrait.tint[3] = alpha;
        cr3::SceneLighting l;
        dev->setLighting(l);
        drawFigure(c.portrait, view.data());
    }
    if (cr3CutIns[1].loaded && ev->cutIn(1).open) drawCr3CentreCutIn(cr3CutIns[1], ev->cutIn(1), *ev, w, h, view.data());
}

bool Viewer::loadCr3Part(Cr3Part& p, const std::string& position, const std::string& figure, const std::string& call, const std::string& loop) {
    std::string err;
    if (!loadFigure(position + ".geom", p.position, &err) || !loadFigure(figure + ".geom", p.fig, &err)) {
        SDL_Log("%s: %s", figure.c_str(), err.c_str());
        return false;
    }
    std::vector<uint8_t> d;
    if (archive.read(position + ".name", d)) p.positionJoints = cr3::parseJointNames(d);
    p.call = call;
    const float keep = time;
    const std::string want = (loop.empty() ? figure : loop) + ".anim";
    const auto a = std::find(p.fig.anims.begin(), p.fig.anims.end(), want);
    if (a != p.fig.anims.end()) loadAnim(p.fig, size_t(a - p.fig.anims.begin()));
    p.fig.absolute = loop.empty();
    time = keep;
    return p.loaded = true;
}

void Viewer::drawCr3Part(Cr3Part& p, int w, int h, float t, float alpha, float dy, int fitOverride) {
    if (!p.loaded || alpha <= 0) return;
    const float unit = 10;
    const std::vector<cr3::Mat34> pw = p.position.skeleton.world(p.position.skeleton.basePose);
    p.fig.place = cr3::Mat34();
    for (size_t j = 0; j < p.positionJoints.size() && j < pw.size(); ++j)
        if (p.positionJoints[j].compare(0, p.call.size(), p.call) == 0) {
            float fx = 0, fy = 0;
            const std::string fitJoint = fitOverride < 0 ? std::string()
                                       : fitOverride > 0 ? "_fit" + std::to_string(fitOverride)
                                                         : p.positionJoints[j];
            fitOffset(fitJoint, w, h, unit, fx, fy);
            p.fig.place.m[3] = pw[j].m[3] + fx, p.fig.place.m[7] = pw[j].m[7] + dy + fy;
            break;
        }
    pose(p.fig, t, true);
    p.fig.tint[3] = std::min(alpha, 1.0f);
    const Mat4 proj = uiProjection(w, h, unit);
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * unit;
    const Mat4 view = cr3::mat4Multiply(proj, camera);
    cr3::SceneLighting l;
    dev->setLighting(l);
    drawFigure(p.fig, view.data());
}

bool Viewer::startCr3Title() {
    Cr3Title& t = cr3Title;
    std::string err;
    for (int k = 0; k < 4; ++k) {
        unload(t.maps[size_t(k)]);
        t.maps[size_t(k)] = ViewFigure();
        if (!loadFigure(std::string(kDemoMaps[k]) + ".geom", t.maps[size_t(k)], &err)) SDL_Log("title: %s", err.c_str());
        t.maps[size_t(k)].absolute = true;
    }
    for (int k = 0; k < 6; ++k) {
        char n[16];
        std::snprintf(n, sizeof n, "f0000title%02d", k + 1);
        if (!t.cameras[size_t(k)].load(archive, n)) SDL_Log("title: camera %s not in the release", n);
    }
    if (!loadCr3Part(t.logo, "ttllogopositon", "ttllogomenu", "call_ttlLogoMenu", "")) return false;
    resetSessionUi();
    titleEvents.reset();
    titleMenu.stage = 0;
    cancelSceneAudio();
    pendingBgm = 0, pendingBgmAt = 0;
    initEventDrawing();
    t.stage = 1, t.clock = t.stageClock = 0, t.shot = 0, t.shotClock = 0, t.lightsOf = -1;
    fieldMode = eventMode = areaMode = false;
    audio.playBgm(33, 10); // (CrxSound::PlayBgm(0x21, 10, loop))
    SDL_Log("title: CR3's");
    return true;
}

void Viewer::updateCr3Title(float dt) {
    Cr3Title& t = cr3Title;
    t.clock += dt, t.stageClock += dt, t.shotClock += dt;
    if (t.shotClock >= kDemoShots[t.shot].seconds) t.shotClock -= kDemoShots[t.shot].seconds, t.shot = (t.shot + 1) % 6;
    if (t.stage == 1 && t.stageClock >= 1.0f) t.stage = 2, t.stageClock = 0; // (Cr3FadeIn(1, 30): in over 30 frames)
    if (t.stage == 2 && t.stageClock > 0.5f && !inputScript.empty() && inputScript.front() == "title:tap") inputScript.pop_front(), cr3TitleTap(); // (tests)
    if (t.stage == 2 && t.stageClock > 0.5f && !inputScript.empty() && inputScript.front() == "title:options") inputScript.pop_front(), openCr3OptionsFrom(1); // (tests)
    if (t.stage == 3 && t.stageClock >= 0.5f) t.stage = 4, t.stageClock = 0, openSaveMenu(); // (TitleMenuMain 3: CreateSaveFileMenu)
    if (t.stage == 4) stepSaveMenu(dt);
    if (t.stage == 5 && t.stageClock >= 0.4f) { // (out - Cr3FadeOut(1, 10) -, then the file: its field, or a new game's prologue)
        t.stage = 0;
        for (ViewFigure& f : t.maps) unload(f);
        unload(t.logo.position), unload(t.logo.fig);
        std::error_code saveError;
        const bool occupied = std::filesystem::exists(std::filesystem::u8path(savePath(cr3SaveFile().c_str())), saveError) || bool(saveError);
        resetForNewSession();
        cr3Playing = true;
        if (occupied) {
            if (loadCr3Save()) return;
            // A failed existing load is never an empty slot. In particular,
            // do not let the prologue's autosave overwrite an unreadable file.
            cr3Playing = cr3SavePending = false;
            cr3SaveRetryAt = 0;
            if (startCr3Title()) {
                cr3Title.stage = 4;
                openSaveMenu((cr3SaveSlot - 1) / kCr3SavePageSize);
                saveMenu.notice = "Could not load this save.\\nThe original file has been kept.";
            }
            return;
        }
        SDL_Log("title: file %d - a new game", cr3SaveSlot);
        SDL_Log("title: a new game");
        // (TitleMain::Init: Cr3FieldData 0x700000000 - town 0, area 7: the field t0108, whose evt000
        // finds flag 2001 clear there - s000_t01_00101_set: flags 33, 702, 2001, 7, 8, 12 - and
        // calls the prologue's first vista; CrxGameData::NewGame sets story flag 1)
        game.setFlag(1, true);
        playMode = true;
        if (!loadArea("t0108", "")) startEvent("t01:s000_t01_00101_evt", "null", pageSeconds);
    }
}

bool Viewer::cr3TitleTap() {
    Cr3Title& t = cr3Title;
    if (t.stage != 2) return true;
    audio.playSe("com_001", false); // (TitleMenuMain 2: the SE alone - the music plays on over the save files)
    t.stage = 3, t.stageClock = 0;
    SDL_Log("title: tapped");
    return true;
}

void Viewer::drawCr3TitleScene(int w, int h) {
    Cr3Title& t = cr3Title;
    const DemoShot& sh = kDemoShots[t.shot];
    cr3::Mat34 cw;
    float fov = 45, zn = 0.1f, zf = 5000;
    if (!t.cameras[size_t(sh.camera)].at(t.shotClock, cw, fov, zn, zf)) return;
    if (t.lightsOf != sh.map) t.lightsOf = sh.map, loadLights(kDemoMaps[sh.map]);
    dev->setCamera(zn, zf, fov * 3.14159265f / 180);
    const Mat4 vp = cr3::mat4Multiply(perspective(fov * 3.14159265f / 180, float(w) / float(std::max(h, 1)), zn, zf),
                                      cr3::mat4RigidInverse(cr3::mat4FromMat34(cw)));
    cr3::SceneLighting l = lighting;
    l.camera[0] = cw.m[3], l.camera[1] = cw.m[7], l.camera[2] = cw.m[11];
    l.time = time;
    dev->setLighting(l);
    ViewFigure& f = t.maps[size_t(sh.map)];
    f.place = cr3::Mat34();
    pose(f, t.shotClock, false);
    drawFigure(f, vp.data());
}

void Viewer::drawCr3TitleUi() {
    Cr3Title& t = cr3Title;
    if (t.stage <= 2) drawCr3Part(t.logo, screenW, screenH, t.clock, 1);
    else if (t.stage == 3) drawCr3Part(t.logo, screenW, screenH, t.clock, 1 - std::min(t.stageClock * 2, 1.0f)); // (ttlLogoPositon_out)
    drawCr3TitleOptions();
    if (t.stage == 4 || t.stage == 5) drawSaveMenu(screenW, screenH);
    // the shots' fades, then the title's own (in from black; out to white)
    const DemoShot& sh = kDemoShots[t.shot];
    float a = 0, c = 0;
    if (t.shotClock < 0.5f && sh.in >= 0) a = 1 - 2 * t.shotClock, c = sh.in;
    else if (t.shotClock > sh.seconds - 0.5f && sh.out >= 0) a = 2 * (t.shotClock - (sh.seconds - 0.5f)), c = sh.out;
    if (t.stage == 1) { // (behind the title's own fade from black: that one over it)
        const float b = 1 - std::min(t.stageClock, 1.0f);
        a = std::max(a, b), c = a > b ? c : 0;
    }
    if (t.stage == 5) a = std::min(t.stageClock * 3, 1.0f), c = 1;
    if (a > 0 && white.id) {
        static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
        const Mat4 id = cr3::mat4Identity();
        const float rgba[4] = {c, c, c, std::min(a, 1.0f)};
        fullFrameQuad(white, kScreen, rgba);
    }
    drawCr3OptionsOver(screenW, screenH);
}

// (CR3-U's: an Options button on the title - the camp's, comBtnSPanel with comBtnIconS_fr00's
// Options frame, where the camp's own sits - while it waits for the tap; O / G too)
void Viewer::drawCr3TitleOptions() {
    if (cr3Title.stage != 2 || cr3Camp.stage != 0) return;
    cr3CampTaps.clear();
    const int w = screenW, h = screenH;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    float cx, cy;
    // (at the screen's lower right, clear of the logo and the copyright)
    const float k = std::min(float(w) / 9.6f, float(h) / 6.4f);
    const float px = float(w) / k / 2 - 1.2f, py = -3.2f + 0.75f;
    UiFig& panel = cr3UiPut("combtnspanel", "", 0, px, py, view);
    if (cr3UiCall(panel, "call_comBtnBg_fr00", cx, cy)) cr3UiPut("combtnbg_fr00", "", 0, cx, cy, view);
    if (cr3UiCall(panel, "call_comBtnIconS_fr00", cx, cy)) cr3UiPut("combtnicons_fr00", "", 6.0f / 30, cx, cy, view);
    float ax, ay, ex, ey;
    if (cr3UiCall(panel, "tapAreaCP01", ax, ay) && cr3UiCall(panel, "tapAreaAP01", ex, ey)) cr3CampTaps.push_back(circleTap(ax, ay, ex, ey, kTapTitleOptions));
}

void Viewer::loadMapNameParts() {
    MapNameParts& m = mapNameParts;
    std::string err;
    if (!loadFigure("fenameposition.geom", m.position, &err) || !loadFigure("fenamemenu.geom", m.menu, &err) ||
        !loadFigure("fenamelist_fr00.geom", m.name, &err))
        return;
    for (ViewFigure& d : m.digits)
        if (!loadFigure("fenamenum_fr00.geom", d, &err)) return;
    std::vector<uint8_t> data;
    if (archive.read("fenameposition.name", data)) m.positionJoints = cr3::parseJointNames(data);
    if (archive.read("fenamemenu.name", data)) m.menuJoints = cr3::parseJointNames(data);
    const float keep = time;
    const auto in = std::find(m.menu.anims.begin(), m.menu.anims.end(), "fenamemenu_in.anim");
    if (in != m.menu.anims.end()) loadAnim(m.menu, size_t(in - m.menu.anims.begin()));
    m.menu.absolute = false; // (slot 1: over its base)
    m.name.absolute = true;
    for (ViewFigure& d : m.digits) d.absolute = true;
    time = keep;
    m.loaded = true;
}

void Viewer::drawMapName(const cr3::EventRuntime& ev, int w, int h) {
    MapNameParts& m = mapNameParts;
    if (ev.mapName() < 0) return;
    if (!m.loaded) loadMapNameParts();
    if (!m.loaded) return;
    const float age = ev.mapNameAge() + eventLead(), unit = 10;
    const float in = 0.96666664f, hold = 2.0f;
    const float t = age < in ? age : age < in + hold ? in : std::min(in + (age - in - hold), 1.3333334f);
    const float alpha = age < in + hold ? std::min(age / 0.6666667f, 1.0f) : std::max(1.0f - (age - in - hold) / 0.1f, 0.0f);
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, unit), [&] { Mat4 c = cr3::mat4Identity(); c[14] = -0.881f * unit; return c; }());
    // the menu at the position's call joint, fitted
    const std::vector<cr3::Mat34> pw = m.position.skeleton.world(m.position.skeleton.basePose);
    m.menu.place = cr3::Mat34();
    for (size_t j = 0; j < m.positionJoints.size() && j < pw.size(); ++j)
        if (m.positionJoints[j].compare(0, 15, "call_feNameMenu") == 0) {
            float fx, fy;
            fitOffset(m.positionJoints[j], w, h, unit, fx, fy);
            m.menu.place.m[3] = pw[j].m[3] + fx, m.menu.place.m[7] = pw[j].m[7] + fy;
        }
    pose(m.menu, t, false);
    m.menu.tint[3] = alpha;
    cr3::SceneLighting l;
    dev->setLighting(l);
    drawFigure(m.menu, view.data());
    const std::vector<cr3::Mat34> mw = m.menu.skeleton.world(m.menu.posed.empty() ? m.menu.skeleton.basePose : m.menu.posed, m.menu.place);
    // MapNameMenu::Pose (0x00ad6564): Quat(0, 0, 0.261909, 0.965092) = 30.37 deg about Z -> cos = w^2 - z^2, sin = 2*w*z.
    const float c30 = 0.862807f, s30 = 0.505533f;
    auto placeAt = [&](ViewFigure& f, const cr3::Mat34& at, float frame) {
        f.place = cr3::Mat34();
        f.place.m[0] = c30, f.place.m[1] = -s30, f.place.m[4] = s30, f.place.m[5] = c30;
        f.place.m[3] = at.m[3], f.place.m[7] = at.m[7], f.place.m[11] = at.m[11];
        pose(f, frame / 30.0f, false); // (ChangeAnimeTime: its frame)
        f.tint[3] = alpha;
        drawFigure(f, view.data());
    };
    const int lv = std::clamp(ev.mapNameLevel(), 0, 20);
    const int lo = lv > 0 ? (lv - 1) * 10 + 1 : 0, hi = lv > 0 ? lv * 10 : 0;
    const int digits[6] = {lo % 1000 / 100, lo % 100 / 10, lo % 10, hi % 1000 / 100, hi % 100 / 10, hi % 10};
    int k = 0;
    for (size_t j = 0; j < m.menuJoints.size() && j < mw.size(); ++j) {
        if (m.menuJoints[j] == "call_feNameList_fr00") placeAt(m.name, mw[j], float(ev.mapName()));
        else if (lv > 0 && m.menuJoints[j].compare(0, 19, "call_feNameNum_fr00") == 0 && k < 6) placeAt(m.digits[size_t(k)], mw[j], float(digits[k])), ++k;
    }
}

void Viewer::loadCutIns() {
    if (archive.contains("comMsgCutRPosition.geom")) {
        loadCr3CutIns();
        loadCr3Part(messageButton, "commsgposition", "commsgbtn", "call_comMsgBtn", "");
        loadCr3Part(vistaPageCursor, "commsgpbtnvposition", "commsgpbtnmenu", "call_comMsgPBtnMenu", "commsgpbtnmenu_loop");
        return;
    }
    static const char* kAnim[cr3::EventRuntime::kCutIns] = {"cutinr_io.anim", "cutinlr_io.anim", "cutinl_io.anim",
                                                            "cutinpty_io.anim", "cutinsgl_io.anim"};
    static const char* kHidden[cr3::EventRuntime::kCutIns] = {"_L", "", "_R", "_L", "_R"};
    std::vector<uint8_t> d;
    const std::vector<std::string> joints = archive.read("cutin.name", d) ? cr3::parseJointNames(d) : std::vector<std::string>();
    const float keep = time;
    for (int k = 0; k < cr3::EventRuntime::kCutIns; ++k) {
        ViewFigure& f = cutInFigures[size_t(k)];
        std::string err;
        if (!loadFigure("cutin.geom", f, &err)) {
            SDL_Log("cut-in: %s", err.c_str());
            continue;
        }
        const auto anim = std::find(f.anims.begin(), f.anims.end(), kAnim[k]);
        if (anim != f.anims.end()) loadAnim(f, size_t(anim - f.anims.begin()));
        for (ViewMesh& vm : f.meshes) {
            const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
            const std::string& name = j < joints.size() ? joints[j] : std::string();
            vm.hidden = *kHidden[k] && name.size() > 2 && name.compare(name.size() - 2, 2, kHidden[k]) == 0;
        }
    }
    time = keep;
}

void Viewer::drawCutIns(int w, int h) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev) return;
    static const float kSide[cr3::EventRuntime::kCutIns] = {1, 0, -1, 1, -1};
    const float aspect = float(w) / float(std::max(h, 1));
    const Mat4 proj = uiProjection(w, h);
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f;
    const Mat4 view = cr3::mat4Multiply(proj, camera);
    for (int k = 0; k < cr3::EventRuntime::kCutIns; ++k) {
        const cr3::EventRuntime::Portrait& c = ev->cutIn(k);
        ViewFigure& f = cutInFigures[size_t(k)];
        if (!c.open || f.meshes.empty()) continue;
        const float t = c.closing ? std::max(c.time - eventLead(), 0.0f) : std::min(c.time + eventLead(), cr3::EventRuntime::kCutInSeconds);
        f.place = cr3::Mat34();
        if (aspect < 1.5f) f.place.m[7] = -0.0403125f;
        else {
            const float s = std::min(float(w) / 960.0f, float(h) / 640.0f);
            f.place.m[3] = kSide[k] * float(int((float(w) - s * 960.0f) * 0.5f)) / (s * 960.0f);
        }
        pose(f, t, false);
        cr3::Texture* face = texture(ev->cutInImage(c.texture));
        for (ViewMesh& vm : f.meshes) {
            vm.texture = face ? face : texture(vm.baseMaterial.diffuse());
            std::vector<float>& dc = vm.material.params[0x33];
            if (dc.size() < 4) dc.resize(4, 1.0f);
            dc[0] = dc[1] = dc[2] = c.bright;
        }
        cr3::SceneLighting l;
        dev->setLighting(l);
        drawFigure(f, view.data());
    }
}

bool Viewer::saveExists(const char* file) { // (an empty one: DeleteCont's - none)
    std::FILE* f = std::fopen(savePath(file).c_str(), "rb");
    if (!f) return false;
    const bool some = std::fgetc(f) != EOF;
    std::fclose(f);
    return some;
}

auto Viewer::saveInfo(const char* file) -> SaveInfo {
    SaveInfo s;
    if (!saveExists(file)) return s;
    std::FILE* f = std::fopen(savePath(file).c_str(), "rb");
    if (!f) return s;
    std::string text;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    s.exists = true;
    size_t p = 0;
    while (p < text.size()) {
        const size_t e = text.find('\n', p);
        const std::string line = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? text.size() : e + 1;
        if (line.compare(0, 6, "flags ") == 0) {
            std::istringstream in(line.substr(6));
            for (int v; in >> v;) s.flags.insert(v);
        } else if (line.compare(0, 6, "saved ") == 0) {
            s.saved = std::time_t(std::atoll(line.c_str() + 6));
        } else if (line.compare(0, 9, "playtime ") == 0) {
            s.playFrames = std::atol(line.c_str() + 9);
        }
    }
    return s;
}

// A title transition abandons menus and their callbacks; it never confirms them.
// Keep game progress and preferences until the player chooses a save or new game.
void Viewer::resetSessionUi() {
    audio.pauseSceneSound(false);
    endMovie();
    geneInspectOpen = scriptGeneStatus = false;
    geneDisposal = ScriptGeneDisposal();
    cr3CampEvent = false;
    goodsFocus = -1;
    lunchFocus.clear();
    lunchActive = lunchWaitLogged = false;
    lunchFunction.clear();
    vistaControlsUntil = -1;
    campOpen = mapOpen = settingsOpen = optionsOpen = optionsAsk = false;
    saveStage = manualStage = 0;
    saveFailed = false;
    world.stage = shop.stage = party.stage = library.stage = pub.stage = credits.stage = 0;
    paused = fieldTabOpen = false;
    script.reset();
    event.reset();
    manualEvents.reset();
    cancelPointerInput();
    keyLock.stage = iai.stage = aim.stage = vslash.stage = 0;
    tapRects.clear();
    keyFocus = -1;
    pendingBranch = false;
    changeStage = 0;
    toTitle = false;
    fieldMode = eventMode = battleMode = false;
    saveImport.cancel();
    cr3Camp.stage = 0;
    inputDialog.open = false;
    areaMode = cr3BattleMode = cr3Playing = cr3SavePending = false;
    cr3SaveRetryAt = 0;
    areaEvents.reset();
    cr3BattleEvents.reset();
    geneLabWasOpen = false;
    town.stage = 0;
    worldSel.wasOpen = false;
    campOptions.asking = false;
    campOptions.from = 0;
    saveMenu.ask = 0;
    saveMenu.footerFocus = -1;
    saveMenu.source = -1;
    saveTaps.clear();
}

void Viewer::startTitle() {
    resetSessionUi();
    cancelSceneAudio();
    pendingBgm = 0, pendingBgmAt = 0;
    titleMenu = TitleMenu();
    titleMenu.stage = 1, titleMenu.start = uiClock;
    titleEvents.reset();
    fieldMode = eventMode = false;
    audio.stopBgm(10);
    SDL_Log("title: the logo");
}

bool Viewer::titleRunning() const { return titleEvents && titleEvents->running(); }

void Viewer::titleScript(const char* function) {
    titleEvents = std::make_unique<cr3::EventRuntime>(archive, game);
    titleEvents->language = optionLanguage, titleEvents->autoPage = pageSeconds;
    if (!titleEvents->start("title", function)) {
        SDL_Log("title: %s - %s", function, titleEvents->error().c_str());
        titleEvents.reset();
    }
}

float Viewer::titleAnimLength(const std::string& fig, const std::string& anim) {
    UiFig& u = uiFig(fig);
    poseUi(u, anim, 0);
    return u.fig.animLoaded ? u.fig.anim.duration() : 0.0f;
}

// Cr3UtilResetWork(true) / Cr3InitWorkFromBattleData(.., true) - a new game with the clear data:
// the common data kept (the items, the Oz, the sopia's levels and EXP), each character's weapon
// and armour (cr3_player_data +0x3c, +0x3e) and seeds, the flags past 3000 and the global work;
// the levels, the rest of the equipment and the story's flags (0 .. 3000) a new game's
void Viewer::carryOver() {
    std::FILE* f = std::fopen(savePath("save1.txt").c_str(), "rb");
    if (!f) return;
    std::string text;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    cr3::GameState old;
    if (!old.load(text)) return;
    std::memcpy(game.itemCount, old.itemCount, sizeof game.itemCount);
    game.oz = old.oz;
    std::memcpy(game.sopiaLevel, old.sopiaLevel, sizeof game.sopiaLevel);
    std::memcpy(game.sopiaExp, old.sopiaExp, sizeof game.sopiaExp);
    for (int c = 0; c < 9; ++c) {
        game.equip[c][0] = old.equip[c][0], game.equip[c][1] = old.equip[c][1];
        std::memcpy(game.seed[c], old.seed[c], sizeof game.seed[c]);
    }
    for (size_t k = 3001; k < old.flags.size(); ++k) game.setFlag(int(k), old.flags[k]);
    std::memcpy(game.globalWork, old.globalWork, sizeof game.globalWork);
    SDL_Log("title: the clear data carried over");
}

void Viewer::titleAsk(int id) {
    titleMenu.ask = id, titleMenu.stage = 7, titleMenu.start = uiClock;
    SDL_Log("title: ask %d", id);
}

void Viewer::titleButton(int id) {
    TitleMenu& t = titleMenu;
    if (t.stage == 5) {
        if (id == 8) { // (Start: CloseTitleMenu - then the continue data, else the panel)
            audio.playSe(0xb4, false);
            t.stage = 10, t.start = uiClock, t.next = 6;
        } else if (id == 20) { // (Options: interface 0x3d)
            audio.playSe(0xb4, false);
            campOpen = true, optionsOpen = true, optionsStart = campStart = uiClock;
            t.stage = 9;
        } else if (id == 24) { // (Scenarios: ScenarioFlowMenu, interface 0x73 - the save's progress, Cr3Util_Load_GameData)
            audio.playSe(0xb4, false);
            if (std::FILE* f = std::fopen(savePath("save1.txt").c_str(), "rb")) {
                std::string text;
                char buf[4096];
                for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
                std::fclose(f);
                game.load(text);
            }
            campOpen = true, campLink = 201, campStart = uiClock, scenarioScroll = 0;
            t.stage = 9;
        }
    } else if (t.stage == 6) {
        if (id == 9) { // (New Game: NewGame's question - with clear data 102)
            audio.playSe(0xb4, false);
            titleAsk(saveInfo("save1.txt").flags.count(5) ? 102 : 101);
        } else if (id == 10 && saveExists("save1.txt")) {
            audio.playSe(0xb4, false);
            titleAsk(107);
        } else if (id == 100) { // (CreateTitleMenu(true): the buttons back)
            audio.playSe(0xb5, false);
            t.stage = 4, t.start = uiClock;
        }
    }
}

void Viewer::titleAnswer(bool yes) {
    TitleMenu& t = titleMenu;
    if (t.stage != 7) return;
    const int ask = t.ask;
    audio.playSe(yes ? 0xbe : 0xb4, false);
    if (ask == 104) { // (the autosave: resumed, else the panel)
        if (yes) t.action = 3, t.stage = 8, t.ask = 0, t.doneAt = uiClock + 1.0f;
        else t.stage = 6, t.start = uiClock;
    } else if (ask == 101 || ask == 103) {
        if (yes) t.action = 1, t.stage = 8, t.ask = 0, t.doneAt = uiClock + 1.0f;
        else t.stage = 6, t.start = uiClock;
    } else if (ask == 102) { // (with the clear data: Cr3UtilResetWork(true) - what it carries over)
        if (yes) t.action = 4, t.stage = 8, t.ask = 0, t.doneAt = uiClock + 1.0f;
        else titleAsk(103);
    } else if (ask == 107) {
        if (yes) t.action = 2, t.stage = 8, t.ask = 108, t.doneAt = uiClock + 2.0f; // ("Load complete." a second, then the fade)
        else t.stage = 6, t.start = uiClock;
    }
}

void Viewer::titleToken(const std::string& tok) {
    SDL_Log("title: input %s (stage %d)", tok.c_str(), titleMenu.stage);
    static const std::pair<const char*, int> kButtons[] = {{"start", 8}, {"options", 20}, {"scenarios", 24}, {"new", 9}, {"load", 10}, {"back", 100}};
    if (titleMenu.stage == 3) titleMenu.tapped = true;
    else if (titleMenu.stage == 7 && (tok == "yes" || tok == "no")) titleAnswer(tok == "yes");
    else if (titleMenu.stage == 9) {
        if (tok == "back") optionsOpen = false, campLink = 0;
    } else
        for (const auto& [name, id] : kButtons)
            if (tok == name) titleButton(id);
}

void Viewer::titleTap() {
    if (titleRunning()) return titleEvents->advance();
    if (titleMenu.stage == 3) titleMenu.tapped = true;
}

bool Viewer::titleClick(float fx, float fy) {
    if (titleRunning()) return titleEvents->advance(), true;
    if (titleMenu.stage == 9) return fieldClick(fx, fy);
    if (titleMenu.stage == 3) return titleMenu.tapped = true;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    for (const TapRect& r : tapRects)
        if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
            if (titleMenu.stage == 7 && (r.id == 7401 || r.id == 7402)) titleAnswer(r.id == 7401);
            else titleButton(r.id);
            return true;
        }
    return true;
}

void Viewer::updateTitle(float dt) {
    TitleMenu& t = titleMenu;
    if (titleEvents) {
        static float clock = 0;
        clock = std::min(clock + dt, 0.25f);
        for (; clock >= cr3::EventRuntime::kTick; clock -= cr3::EventRuntime::kTick)
            if (!titleEvents->tick()) SDL_Log("title: %s", titleEvents->error().c_str());
        if (!titleEvents->running()) titleEvents.reset();
    }
    if (!inputScript.empty() && uiClock >= inputNext && !titleRunning() && (t.stage == 3 || (t.stage >= 5 && t.stage <= 9)) && t.stage != 8 &&
        !(t.stage == 7 && uiClock - t.start < 0.5f)) {
        titleToken(inputScript.front());
        inputScript.pop_front();
        inputNext = uiClock + 0.6f;
    }
    const float age = uiClock - t.start;
    if (t.stage == 1 && age >= std::max(titleAnimLength("logoinc", "logoinc"), 0.1f)) {
        t.stage = 2, t.start = uiClock;
        audio.playBgm(1, 10);
        SDL_Log("title: the title");
    } else if (t.stage == 2 && age >= titleAnimLength("title", "title_in")) {
        t.stage = 3, t.start = uiClock;
    } else if (t.stage == 3 && t.tapped) {
        t.tapped = false;
        audio.playSe(0xb4, false);
        t.stage = 4, t.start = uiClock, t.next = 1;
    } else if (t.stage == 4 && age >= titleAnimLength("title", "title_wait")) {
        if (t.next == 1) titleScript("title"); // (the first time: Versionup_Title; Tutorial_Credit with flag 5)
        t.stage = 5, t.start = uiClock, t.next = 0;
        SDL_Log("title: the menu");
    } else if (t.stage == 10 && age >= titleAnimLength("title", "title_in")) {
        t.start = uiClock;
        if (saveExists("cont.txt")) { // (the continue data: Tutorial_ContData, its calendar and question)
            titleScript("Tutorial_ContData");
            titleAsk(104);
        } else {
            t.stage = 6;
        }
    } else if (t.stage == 8 && uiClock >= t.doneAt) {
        t.stage = 0; // (the main loop takes it from here: action)
        audio.stopBgm(10);
    } else if (t.stage == 9 && !optionsOpen && !optionsAsk && campLink != 201 && manualStage == 0) {
        campOpen = false; // (the window closed: the title's buttons again)
        t.stage = 5, t.start = uiClock;
    }
    if (t.stage == 9) updateManual(dt);
}

void Viewer::drawCalendar(const SaveInfo& s, const Mat4& view, const Mat4& proj, float t) {
    UiFig& cal = uiFig("calendar_io");
    cal.fig.place = cr3::Mat34();
    poseUi(cal, "calendar_io", t);
    drawFigure(cal.fig, view.data());
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &s.saved);
#else
    localtime_r(&s.saved, &lt);
#endif
    const long hours = std::min(s.playFrames / 72000, 99L), minutes = std::min(s.playFrames / 1200 - hours * 60, 99L);
    float x, y;
    auto num = [&](const char* j, int v, int digits) { if (uiJoint(cal, j, x, y)) drawDigits(v, 90, digits, x, y, proj, 1, 1, true); };
    auto glyph = [&](const char* j, int g) { if (uiJoint(cal, j, x, y)) drawDigits(0, g, 1, x, y, proj); };
    if (uiJoint(cal, "call_dotNum_id00", x, y)) drawStatusSign(21, x, y, proj);
    glyph("call_dotNum_id01", lt.tm_mon + 1); // (the month's own: glyphs 1 .. 12)
    num("call_dotNum_id03", lt.tm_mday, 2);
    glyph("call_dotNum_id04", 166);
    num("call_dotNum_id08", lt.tm_year + 1900, 4);
    num("call_dotNum_id10", lt.tm_hour, 2);
    glyph("call_dotNum_id11", 163);
    num("call_dotNum_id13", lt.tm_min, 2);
    if (uiJoint(cal, "call_dotNum_id14", x, y)) drawStatusSign(22, x, y, proj);
    num("call_dotNum_id16", int(hours), 2);
    glyph("call_dotNum_id17", 163);
    num("call_dotNum_id19", int(minutes), 2);
}

void Viewer::drawTitleButtons(UiFig& parts, const Mat4& view, int skip) {
    for (const std::string& j : parts.joints) {
        if (j.compare(0, 18, "call_circleBtn_fr") != 0 && j.compare(0, 17, "call_circleBtn_fr") != 0) continue;
        if (skip > 0) { // (CreateCircleButton: the first `skip` left out)
            --skip;
            continue;
        }
        const int id = std::atoi(j.c_str() + 17);
        float x, y;
        if (!uiJoint(parts, j, x, y) || x >= 0.67f) continue; // (GameTitleMenu::Draw: those short of 0.67)
        UiFig& c = uiFig("circlebtn_fr");
        c.fig.place = cr3::Mat34();
        c.fig.place.m[3] = x, c.fig.place.m[7] = y;
        poseUi(c, "circlebtn_fr", float(id) / 20.0f);
        focusLook(c, id);
        drawFigure(c.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) tapRects.push_back({id, sx, sy, ex, ey});
    }
}

void Viewer::drawTitle() {
    TitleMenu& tm = titleMenu;
    tapRects.clear();
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f;
    const Mat4 proj = uiProjection(screenW, screenH), view = cr3::mat4Multiply(proj, camera);
    cr3::SceneLighting l;
    dev->setLighting(l);
    const float age = uiClock - tm.start;
    static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
    const Mat4 id = cr3::mat4Identity();
    if (tm.stage == 1) { // (logoInc, from black over 20 frames)
        UiFig& logo = uiFig("logoinc");
        logo.fig.place = cr3::Mat34();
        poseUi(logo, "logoinc", age);
        drawFigure(logo.fig, view.data());
        const float a = std::max(0.0f, 1.0f - age / (20 * cr3::EventRuntime::kTick));
        const float rgba[4] = {0, 0, 0, a};
        if (a > 0 && white.id) fullFrameQuad(white, kScreen, rgba);
        return;
    }
    if (tm.stage == 9) return drawCamp(view, proj, screenW, screenH);
    // the title over its painting (title_bg: wider than 3:2 stretched)
    const float aspect = float(screenW) / float(std::max(screenH, 1));
    UiFig& bg = uiFig("title_bg");
    bg.fig.place = cr3::Mat34();
    if (aspect >= 1.5f) bg.fig.place.m[0] = aspect / 1.5f;
    poseUi(bg, "title_bg", uiClock, true);
    drawFigure(bg.fig, view.data());
    UiFig& ti = uiFig("title");
    ti.fig.place = cr3::Mat34();
    for (ViewMesh& vm : ti.fig.meshes) vm.hidden = false;
    if (tm.stage == 2) poseUi(ti, "title_in", age);
    else if (tm.stage == 3) poseUi(ti, "title_loop", age, true);
    else if (tm.stage == 4) poseUi(ti, "title_wait", age);
    else if (tm.stage == 5) poseUi(ti, "title_wait", 1e6f); // (its end: the buttons' places)
    else poseUi(ti, "title_in", std::max(titleAnimLength("title", "title_in") - (tm.stage == 10 ? age : 1e6f), 0.0f)); // (backwards: gone)
    hideJoint(ti, "GEO_titleBG");
    drawFigure(ti.fig, view.data());
    if (tm.stage == 2 || tm.stage == 3) {
        UiFig& diva = uiFig("divabtn_io");
        diva.fig.place = cr3::Mat34();
        poseUi(diva, "divabtn_io", uiClock - (tm.stage == 2 ? tm.start : 0), true);
        drawFigure(diva.fig, view.data());
    }
    if (tm.stage == 2) { // (from white over 20 frames)
        const float a = std::max(0.0f, 1.0f - age / (20 * cr3::EventRuntime::kTick));
        const float rgba[4] = {1, 1, 1, a};
        if (a > 0 && white.id) fullFrameQuad(white, kScreen, rgba);
    }
    if (titleRunning()) return;
    if (tm.stage == 5 || tm.stage == 4) drawTitleButtons(ti, view, 0); // (CircleButtonPose: following their joints)
    const bool panel = tm.stage == 6; // (a question: the panel gone - DeleteCircleButton, its parts, the calendar)
    if (tm.stage == 7 && tm.ask == 107) { // (Load: CreateLoadTitle - menu with menuLoad_io)
        UiFig& mn = uiFig("menu");
        mn.fig.place = cr3::Mat34();
        poseUi(mn, "menuload_io", age);
        drawFigure(mn.fig, view.data());
    }
    if (panel) {
        const SaveInfo s = saveInfo("save1.txt");
        if (s.exists) {
            UiFig& bi = uiFig("bossicon_io");
            bi.fig.place = cr3::Mat34();
            poseUi(bi, "bossicon_io", age);
            for (const char* j : {"GEO_boss1", "GEO_boss2", "GEO_boss3"}) showJoint(bi, j, true);
            if (!s.flags.count(1)) showJoint(bi, "GEO_boss1", false);
            if (!s.flags.count(3)) showJoint(bi, "GEO_boss2", false);
            if (!s.flags.count(4)) showJoint(bi, "GEO_boss3", false);
            drawFigure(bi.fig, view.data());
            drawCalendar(s, view, proj, age);
        }
        UiFig& nb = uiFig("newlybtn_io");
        nb.fig.place = cr3::Mat34();
        poseUi(nb, "newlybtn_io", age);
        drawFigure(nb.fig, view.data());
        if (tm.stage == 6) drawTitleButtons(nb, view, s.exists ? 0 : 1);
        UiFig& bk = uiFig("btlback");
        bk.fig.place = cr3::Mat34();
        poseUi(bk, "btlback_in", age);
        drawFigure(bk.fig, view.data());
        float sx, sy, ex, ey;
        if (tm.stage == 6 && uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
    } else if (tm.stage == 7 && tm.ask == 104) {
        drawCalendar(saveInfo("cont.txt"), view, proj, age);
    }
    if (tm.stage == 7) drawAsk(announce(tm.ask), age, view, screenW, screenH, 7401, 7402);
    else if (tm.stage == 8 && tm.ask == 108 && uiClock < tm.doneAt - 1.0f) drawAsk(announce(108), age, view, screenW, screenH, 0, 0);
    if (tm.stage == 8) { // (Cr3FadeOut(0, 20): to black over the last second)
        const float a = std::clamp(1.0f - (tm.doneAt - uiClock), 0.0f, 1.0f);
        const float rgba[4] = {0, 0, 0, a};
        if (a > 0 && white.id) fullFrameQuad(white, kScreen, rgba);
    }
}

std::string Viewer::savePath(const char* file) {
    char* dir = SDL_GetPrefPath("DuIslingr", "CR3-U");
    std::string p = dir ? std::string(dir) + file : std::string("cr3u-") + file;
    SDL_free(dir);
    return p;
}

void Viewer::scriptSave(int kind) {
    if (!scriptSaves) return;
    if (kind == 0) writeSave();
    else if (kind == 1) writeSave("cont.txt");
    else if (!crstorage::replaceFile(savePath("cont.txt"), "")) SDL_Log("could not clear continue data"); // (DeleteCont: none to continue)
}

bool Viewer::writeSave(const char* file) {
    std::map<std::string, std::string> extra;
    char pos[96];
    std::snprintf(pos, sizeof pos, "%.6f %.6f %.6f %.6f", player[0], player[1], player[2], heading);
    extra["map"] = field.info().name, extra["place"] = pos;
    extra["saved"] = std::to_string(static_cast<long long>(std::time(nullptr))); // (the calendar's: cr3_savedata's time)
    extra["playtime"] = std::to_string(static_cast<long>(playSeconds * 20.0)); // (+0x10: frames of 0.05 s)
    extra["options"] = std::to_string(optionLanguage) + " " + std::to_string(optionQuality) + " " + std::to_string(optionVolume[0]) + " " +
                       std::to_string(optionVolume[1]) + " " + std::to_string(optionVolume[2]);
    const std::string text = game.save(extra), path = savePath(file);
    std::string error;
    const bool ok = crstorage::replaceFile(path, text, &error);
    if (!ok) SDL_Log("save failed: %s", error.c_str());
    SDL_Log("save: %s %s", path.c_str(), ok ? "written" : "not written");
    if (ok && std::strcmp(file, "save1.txt") == 0) writeSystem(); // (SaveConfirmationMenu: Cr3Util_Save_SystemData too)
    return ok;
}

std::string Viewer::cr3SaveFile(int slot) const { return "save" + std::to_string(slot > 0 ? slot : cr3SaveSlot) + ".txt"; }

bool Viewer::writeCr3Save() {
    if (!areaMode || !areaWalker) return false;
    std::map<std::string, std::string> extra;
    const cr3::Vec3 p = areaWalker->position();
    char pos[96];
    std::snprintf(pos, sizeof pos, "%.6f %.6f %.6f %.6f", p[0], p[1], p[2], areaYaw);
    extra["area"] = area.name, extra["place"] = pos;
    extra["saved"] = std::to_string(static_cast<long long>(std::time(nullptr)));
    extra["playtime"] = std::to_string(static_cast<long>(playSeconds * 20.0));
    extra["options"] = std::to_string(optionLanguage) + " " + std::to_string(optionQuality) + " " + std::to_string(optionVolume[0]) + " " +
                       std::to_string(optionVolume[1]) + " " + std::to_string(optionVolume[2]);
    const std::string text = game.save(extra), path = savePath(cr3SaveFile().c_str());
    std::string error;
    const bool ok = crstorage::replaceFile(path, text, &error);
    if (!ok) SDL_Log("save failed: %s", error.c_str());
    SDL_Log("save: %s %s (%s)", path.c_str(), ok ? "written" : "not written", area.name.c_str());
    if (cr3Playing) {
        cr3SavePending = !ok;
        cr3SaveRetryAt = ok ? 0 : uiClock + 5.0f;
    }
    if (ok) writeSystem();
    return ok;
}

std::string Viewer::readSaveText(int slot) {
    std::string text;
    if (std::FILE* f = std::fopen(savePath(cr3SaveFile(slot).c_str()).c_str(), "rb")) {
        char buf[4096];
        for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
        std::fclose(f);
    }
    return text;
}

// the save's area, if there is one (a CR3 save: one with "area")
std::string Viewer::cr3SavedArea() {
    const std::string text = readSaveText();
    const size_t at = text.find("\narea ");
    if (at == std::string::npos) return {};
    const size_t end = text.find('\n', at + 6);
    return text.substr(at + 6, end == std::string::npos ? std::string::npos : end - at - 6);
}

bool Viewer::loadCr3Save() {
    std::map<std::string, std::string> extra;
    std::string err;
    if (!game.load(readSaveText(), &extra, &err) || extra["area"].empty())
        return SDL_Log("continue: %s (%s)", err.empty() ? "no area saved" : err.c_str(), savePath(cr3SaveFile().c_str()).c_str()), false;
    int o[5] = {optionLanguage, optionQuality, optionVolume[0], optionVolume[1], optionVolume[2]};
    if (std::sscanf(extra["options"].c_str(), "%d %d %d %d %d", &o[0], &o[1], &o[2], &o[3], &o[4]) == 5)
        optionLanguage = o[0], optionQuality = o[1], optionVolume[0] = o[2], optionVolume[1] = o[3], optionVolume[2] = o[4], languageFromSlot();
    // CR3 keeps story flags in each slot; system.txt may belong to a different slot.
    applySystem(false);
    playSeconds = double(std::atol(extra["playtime"].c_str())) / 20.0;
    savedArea.clear(); // a New Game Plus save uses the prologue's default spawn
    float q[4];
    if (std::sscanf(extra["place"].c_str(), "%f %f %f %f", &q[0], &q[1], &q[2], &q[3]) == 4)
        savedArea = extra["area"], savedPos = {q[0], q[1], q[2]}, savedYaw = q[3]; // (loadArea puts the player back there)
    game.next = cr3::GameState::Next();
    playMode = true;
    cr3Playing = true;
    if (!game.cr3Boxes.empty()) lastAreaDungeon = cr3::cr3DungeonNum(extra["area"]);
    else lastAreaDungeon = -1;
    if (!loadArea(extra["area"], "")) return false;
    SDL_Log("continue: %s", extra["area"].c_str());
    return true;
}

// --continue: the save read back, its map entered there
bool Viewer::continueSave(const std::string& binary, const std::string& dataPath, const char* only) {
    std::string text;
    for (const char* file : {"cont.txt", "save1.txt"}) { // (the continue data, else the save)
        if (only && std::strcmp(file, only) != 0) continue;
        if (std::FILE* f = std::fopen(savePath(file).c_str(), "rb")) {
            char buf[4096];
            for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
            std::fclose(f);
        }
        if (!text.empty()) {
            SDL_Log("continue: from %s", file);
            break;
        }
    }
    std::map<std::string, std::string> extra;
    std::string err;
    if (!game.load(text, &extra, &err)) return SDL_Log("continue: %s (%s)", err.c_str(), savePath().c_str()), false;
    int o[5] = {optionLanguage, optionQuality, optionVolume[0], optionVolume[1], optionVolume[2]};
    if (std::sscanf(extra["options"].c_str(), "%d %d %d %d %d", &o[0], &o[1], &o[2], &o[3], &o[4]) == 5)
        optionLanguage = o[0], optionQuality = o[1], optionVolume[0] = o[2], optionVolume[1] = o[3], optionVolume[2] = o[4], languageFromSlot();
    applySystem(); // (the system data's flags and options over the save's)
    const std::string map = extra["map"].empty() ? "d1020" : extra["map"];
    playSeconds = double(std::atol(extra["playtime"].c_str())) / 20.0;
    if (!loadField(map, "", binary, dataPath)) return false;
    float p[4];
    if (std::sscanf(extra["place"].c_str(), "%f %f %f %f", &p[0], &p[1], &p[2], &p[3]) == 4) {
        restoreFieldPosition(p, p[3]);
    }
    SDL_Log("continue: %s at %.3f %.3f %.3f", map.c_str(), player[0], player[1], player[2]);
    return true;
}

// --game-save: the game's own game.dat / cont.dat (script/gamesave.hpp) - and its system.dat
// beside it, the flags 0..991 - played from its map and place
bool Viewer::loadGameSave(const std::string& path, const std::string& binary, const std::string& dataPath) {
    auto read = [](const std::string& p, std::vector<uint8_t>& out) {
        std::FILE* f = std::fopen(p.c_str(), "rb");
        if (!f) return false;
        out.clear();
        uint8_t buf[4096];
        for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
        std::fclose(f);
        return true;
    };
    std::vector<uint8_t> d;
    cr3::GameSavePlace place;
    bool crc = false;
    std::string err;
    if (!read(path, d) || !cr3::readGameSave(d, game, place, &crc, &err)) return SDL_Log("game save %s: %s", path.c_str(), err.empty() ? "can't read" : err.c_str()), false;
    const std::string sys = (std::filesystem::path(path).parent_path() / "system.dat").string();
    uint32_t options[6];
    bool sysCrc = false;
    const bool system = read(sys, d) && cr3::readSystemSave(d, game, options, &sysCrc);
    SDL_Log("game save %s%s%s: %s at %.3f %.3f %.3f", path.c_str(), crc ? "" : " (its CRC doesn't hold)", system ? ", with its system.dat" : "",
            place.map.c_str(), place.position[0], place.position[1], place.position[2]);
    playSeconds = double(place.playFrames) / 20.0;
    if (!loadField(place.map, "", binary, dataPath)) return false;
    restoreFieldPosition(place.position, place.heading);
    return true;
}

void Viewer::saveAnswer(bool yes) {
    if (saveStage != 1) return;
    if (yes) {
        audio.playSe(0xbd, false);
        saveFailed = !writeSave();
        saveStage = 2, saveStart = uiClock;
    } else {
        audio.playSe(0xb4, false);
        saveStage = 0;
    }
}

void Viewer::drawSave(const Mat4& view, const Mat4& proj, int w, int h) {
    const float t = uiClock - saveStart;
    UiFig& title = uiFig("menu");
    title.fig.place = cr3::Mat34();
    poseUi(title, "menusave_io", t);
    drawFigure(title.fig, view.data());
    StatusShown rows[2];
    int count = 0;
    for (int k = 0; k < 2; ++k)
        if (game.party[k] >= 0 && game.party[k] < 9) rows[count++] = statusOf(game.party[k]);
    drawStatusWindows(rows, count, t, view, proj);
    if (saveStage == 1) drawAsk(announce(0x69), t, view, w, h, 7401, 7402);
    else {
        UiFig& win = uiFig("syswin_io");
        win.fig.place = cr3::Mat34();
        poseUi(win, "syswin_io", t);
        drawFigure(win.fig, view.data());
        float x, y;
        if (uiJoint(win, "typeM_L01", x, y)) drawUiText(saveFailed ? "Could not save. Please try again." : announce(0x6a), 26, x, y, w, h, true);
    }
}

bool Viewer::inputDialogShown() const { return inputDialog.open; }

size_t Viewer::codePoints(const std::string& t) {
    size_t n = 0;
    for (unsigned char c : t) n += (c & 0xc0) != 0x80;
    return n;
}

void Viewer::syncInputDialog() {
    cr3::EventRuntime* ev = activeEvents();
    const bool want = ev && ev->inputShown();
    if (want && !inputDialog.open) {
        inputDialog.open = true, inputDialog.kind = ev->inputKind(), inputDialog.text = inputDialog.was = ev->inputText(), inputDialog.since = uiClock;
        SDL_Log("text entry: kind %d", inputDialog.kind);
    } else if (!want) inputDialog.open = false;
}

void Viewer::closeInputDialog(bool ok) {
    cr3::EventRuntime* ev = activeEvents();
    if (!inputDialog.open || !ev) return;
    if (inputDialog.text.empty()) ok = false; // (nothing typed: as it was)
    audio.playSe(ok ? "com_003" : "com_004", false);
    ev->answerInput(ok ? inputDialog.text : inputDialog.was, ok);
    inputDialog.open = false;
    SDL_Log("text entry: %s", ok ? "OK" : "cancelled");
}

void Viewer::typeInputDialog(const char* utf8) {
    if (!inputDialog.open || !utf8) return;
    const size_t most = inputDialog.kind == 0 ? 16 : 24;
    for (const char* c = utf8; *c;) { // (a code point at a time, control characters left out)
        size_t n = 1;
        while (c[n] && (static_cast<unsigned char>(c[n]) & 0xc0) == 0x80) ++n;
        if (static_cast<unsigned char>(*c) >= 0x20 && codePoints(inputDialog.text) < most) inputDialog.text.append(c, n);
        c += n;
    }
}

bool Viewer::inputDialogKey(SDL_Keycode k) {
    if (!inputDialog.open) return false;
    std::string& t = inputDialog.text;
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) closeInputDialog(true);
    else if (k == SDLK_ESCAPE) closeInputDialog(false);
    else if (k == SDLK_DELETE) t.clear();
    else if (k == SDLK_BACKSPACE && !t.empty()) { // (the last code point)
        size_t at = t.size() - 1;
        while (at > 0 && (static_cast<unsigned char>(t[at]) & 0xc0) == 0x80) --at;
        t.erase(at);
    }
    return true; // (every key its own while it's up)
}

bool Viewer::inputDialogClick(float fx, float fy) {
    if (!inputDialog.open) return false;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    auto in = [&](const YesNoRectBox& r) { return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1; };
    if (in(inputDialog.ok)) closeInputDialog(true);
    else if (in(inputDialog.cancel)) closeInputDialog(false);
    else if (window) SDL_StartTextInput(window); // (the field: a phone's keyboard back)
    return true; // (the box takes every tap)
}

void Viewer::drawInputDialog(int w, int h) {
    if (!inputDialog.open || !white.id) return;
    static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
    const Mat4 id = cr3::mat4Identity(), proj = uiProjection(w, h);
    const float a = std::min((uiClock - inputDialog.since) * 10.0f, 1.0f);
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    dev->clearDepth();
    const float dim[4] = {0, 0, 0, 0.5f * a};
    fullFrameQuad(white, kScreen, dim);
    auto box = [&](float x0, float y0, float x1, float y1, float r, float g, float b, float al) {
        const float v[24] = {x0, y0, 0, 1, x1, y0, 1, 1, x1, y1, 1, 0, x0, y0, 0, 1, x1, y1, 1, 0, x0, y1, 0, 0};
        const float rgba[4] = {r, g, b, al * a};
        dev->drawQuad(white, v, proj.data(), rgba);
    };
    const float X = 0.27f, top = 0.25f, bottom = 0.03f; // (UI units: the screen 0.96 x 0.64 about its centre)
    box(-X - 0.003f, bottom - 0.003f, X + 0.003f, top + 0.003f, 0.45f, 0.45f, 0.48f, 1);
    box(-X, bottom, X, top, 0.13f, 0.13f, 0.15f, 0.97f);
    static const char* const kHints[2][4] = {{"Name", "Team name", "Pet name", "Food name"}, {"名前", "チーム名", "ペットの名前", "食べ物の名前"}};
    const int kind = inputDialog.kind == 0 ? 0 : inputDialog.kind == 1 ? 1 : inputDialog.kind == 3 ? 2 : 3;
    const char* hint = kHints[japaneseText ? 1 : 0][kind];
    drawUiText(hint, 20, -X + 0.02f, 0.215f, w, h, true, 0.65f);
    box(-X + 0.02f, 0.125f, X - 0.02f, 0.185f, 0.92f, 0.92f, 0.94f, 1); // (the field)
    box(-X + 0.02f, 0.122f, X - 0.02f, 0.126f, 0.2f, 0.6f, 0.9f, 1);  // (its underline)
    const float tw = drawUiText(inputDialog.text, 26, -X + 0.035f, 0.155f, w, h, true, 0.08f, 2 * X - 0.07f);
    if (std::fmod(uiClock - inputDialog.since, 1.0f) < 0.55f) box(-X + 0.037f + tw, 0.135f, -X + 0.040f + tw, 0.175f, 0.1f, 0.1f, 0.1f, 1); // (the cursor)
    inputDialog.cancel = {-X + 0.02f, bottom + 0.012f, -0.01f, bottom + 0.072f};
    inputDialog.ok = {0.01f, bottom + 0.012f, X - 0.02f, bottom + 0.072f};
    for (const YesNoRectBox* r : {&inputDialog.cancel, &inputDialog.ok}) box(r->x0, r->y0, r->x1, r->y1, 0.22f, 0.22f, 0.25f, 1);
    drawUiText("CANCEL", 22, (inputDialog.cancel.x0 + inputDialog.cancel.x1) / 2, bottom + 0.042f, w, h, false, 0.9f);
    drawUiText("OK", 22, (inputDialog.ok.x0 + inputDialog.ok.x1) / 2, bottom + 0.042f, w, h, false, 0.9f);
}

void Viewer::drawYesNo(cr3::EventRuntime& ev, int w, int h) {
    yesNoRects.clear();
    const bool closing = !ev.yesNoShown() && ev.yesNoClosing() >= 0;
    if (!ev.yesNoShown() && !closing) return;
    // CR3 (Cr3EvtWindowShowYesNoButton: SetYNChoiceTWindowParameter - YNChoiceButton, interface
    // 0xe): the screen dimmed (its CustomFigure, black at half the buttons' alpha), comBtnMenu at
    // comBtnPosition's call_comBtnMenu with comBtnMenu_io, fading in over 0.1 s (and back out on
    // InterfaceCloseAnimeSet), its buttons from joint 10 on - slots 005 and 006 -, Yes (icon and
    // text 10) and No (11). CR2's choice_io isn't in CR3's data
    const float t = closing ? std::max(0.1f - (ev.yesNoClosing() + eventLead()), 0.0f) : ev.yesNoAge() + eventLead();
    const float a = std::min(t * 10.0f, 1.0f);
    if (!closing) {
        if (ev.yesNoAge() < yesNoSeenAge) yesNoCursor = 0;
        yesNoSeenAge = ev.yesNoAge();
    }
    if (white.id && a > 0) {
        static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
        const Mat4 id = cr3::mat4Identity();
        const float rgba[4] = {0, 0, 0, 0.5f * a};
        dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
        fullFrameQuad(white, kScreen, rgba);
    }
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    dev->setLighting(cr3::SceneLighting());
    dev->clearDepth();
    float x, y;
    UiFig& pos = cr3UiPut("combtnposition", "", 0, 0, 0, view);
    if (!cr3UiCall(pos, "call_comBtnMenu", x, y, w, h)) return;
    // (comBtnMenu posed for its buttons' places; its two arc lines not shown - the reference's
    // yes / no have none, its battle commands do)
    UiFig& menu = cr3UiPut("combtnmenu", "combtnmenu_io", t, x, y, view, 0);
    for (int b = 0; b < 2; ++b) {
        float px, py, cx, cy;
        if (!cr3UiCall(menu, "call_comBtnPanel_00" + std::to_string(5 + b), px, py)) continue;
        const int id = 10 + b;
        if (vitaButtons() && b == yesNoCursor) {
            cr3UiTintMaterial = "mat_com_menuBtn_02";
            cr3UiMaterialTint[0] = 0.5f, cr3UiMaterialTint[1] = 1, cr3UiMaterialTint[2] = 1.5f;
        }
        UiFig& panel = cr3UiPut("combtnpanel", "", 0, px, py, view, a);
        if (cr3UiCall(panel, "call_comBtnBg_fr00", cx, cy)) cr3UiPut("combtnbg_fr00", "", float(b) / 30, cx, cy, view, a);
        if (cr3UiCall(panel, "call_comBtnIcon_fr00", cx, cy)) cr3UiPut("combtnicon_fr00", "", float(id) / 30, cx, cy, view, a);
        if (cr3UiCall(panel, "call_comBtnText_fr00", cx, cy)) cr3UiPut("combtntext_fr00", "", float(id) / 30, cx, cy, view, a);
        if (cr3UiCall(panel, "call_comBtnR", cx, cy)) cr3UiPut("combtnr", "", 0, cx, cy, view, a);
        float ax, ay, bx, by;
        if (!closing && cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) yesNoRects.push_back({b == 0, ax / 10, ay / 10, bx / 10, by / 10});
        cr3UiTintMaterial.clear();
        cr3UiMaterialTint[0] = cr3UiMaterialTint[1] = cr3UiMaterialTint[2] = 1;
    }
}

void Viewer::drawChoice(cr3::EventRuntime& ev, int w, int h) {
    choiceRects.clear();
    const int n = ev.choiceShown();
    if (n < 2) return;
    const std::string c = "C" + std::to_string(n), lc = "c" + std::to_string(n);
    const bool closing = ev.choiceClosing() >= 0;
    const float a = closing ? 1.0f : ev.choiceAnswer() >= 0 ? 1.0f : std::min(ev.choiceAge() / 0.13333334f, 1.0f);
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    dev->setLighting(cr3::SceneLighting());
    dev->clearDepth();
    float x, y;
    UiFig& pos = cr3UiPut("commsg" + lc + "position", "", 0, 0, 0, view, a);
    if (!cr3UiCall(pos, "call_comMsg" + c + "Menu", x, y, w, h)) return;
    const std::string menuName = "commsg" + lc + "menu";
    UiFig& menu = cr3UiPose(menuName, "", 0, x, y); // (its joints first: the bubble widened, then drawn)
    const std::vector<std::vector<std::string>> lines = ev.choiceLines();
    const float size = n == 2 ? 32.0f : 28.0f, off = size / 2 / 200; // (CreateText: SetTextRender 0x20 / 0x1c, (s / 2) / 200 right and down)
    struct Placed { std::string text; float x, y, width; };
    struct Option { bool row = false; float left = 0, right = 0, top = 0, bottom = 0; std::vector<Placed> placed; };
    std::vector<Option> options(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k) {
        Option& o = options[size_t(k)];
        const std::vector<std::string> two = k < int(lines.size()) ? lines[size_t(k)] : std::vector<std::string>();
        float ax, ay, bx, by; // (the menu's tapAreaS0k / E0k: each option's row - the panels' own the same)
        const std::string kk = std::to_string(k + 1);
        o.row = cr3UiCall(menu, "tapAreaS0" + kk, ax, ay) && cr3UiCall(menu, "tapAreaE0" + kk, bx, by);
        if (o.row) o.left = std::min(ax, bx) / 10, o.right = std::max(ax, bx) / 10, o.top = ay / 10, o.bottom = by / 10;
        for (int l = 0; l < (n == 2 ? 2 : 1); ++l) {
            std::string line = l < int(two.size()) ? two[size_t(l)] : std::string();
            if (n == 3)
                for (size_t m = 1; m < two.size(); ++m) line += " " + two[m];
            if (line.empty()) continue;
            char num[4];
            std::snprintf(num, sizeof num, "%02d", n == 2 ? 2 * k + l + 1 : k + 1);
            float tx, ty;
            bool found = false;
            for (const std::string& j : menu.joints)
                if (j.size() >= 2 && j.compare(j.size() - 2, 2, num) == 0 && j.compare(0, 4, "type") == 0 && uiJoint(menu, j, tx, ty)) {
                    found = true;
                    break;
                }
            if (!found) continue;
            if (n == 2 && two.size() == 1) ty -= 0.24f; // (SetTextHeadPosition: the line alone towards the middle)
            o.placed.push_back({line, (tx + off) / 10, (ty - off) / 10, uiTextWidth(line, size - 4, w, h)}); // (typeM_L: from the left)
        }
    }
    // The original draws a line whole from its joint, and a long one - the fan translations' -
    // runs past its bubble. Its room: the row less as much at the right end as the text starts
    // from the left. A menu with a line wider than that is widened: the bubble cut in three, its
    // ends moved out as far as the widest line needs (its text with its left end), only as far
    // as the screen leaves (0.02 clear of its edges). What is still too wide - a 3:2 or 4:3
    // screen's - has its option's lines centred, 4% of the row clear at each end, and narrowed
    // to that
    float need = 0, extent = 0;
    for (const Option& o : options) {
        if (!o.row || o.placed.empty()) continue;
        const float room = o.right - o.placed[0].x - (o.placed[0].x - o.left);
        for (const Placed& q : o.placed) need = std::max(need, q.width - room);
        extent = std::max({extent, o.right, -o.left});
    }
    const float k = std::min(float(w) / 0.96f, float(h) / 0.64f), half = float(w) / (2 * k);
    float widen = std::clamp(std::min(need / 2, half - 0.02f - extent), 0.0f, 1.0f);
    const float quadWidth = sliceUiQuad(menuName, menu.fig, 0.12f);
    float rowWidth = 0;
    for (const Option& o : options)
        if (o.row) rowWidth = std::max(rowWidth, o.right - o.left);
    if (quadWidth <= 0 || rowWidth <= 0) widen = 0;
    widenUiQuad(menuName, menu.fig, quadWidth > 0 && rowWidth > 0 ? widen * quadWidth / rowWidth : 0);
    cr3UiPut(menuName, "", 0, x, y, view, a);
    for (int kk = 0; kk < n; ++kk) {
        Option& o = options[size_t(kk)];
        if (o.row) {
            o.left -= widen, o.right += widen;
            for (Placed& q : o.placed) q.x -= widen;
            if (!o.placed.empty()) {
                const float room = o.right - o.placed[0].x - (o.placed[0].x - o.left);
                float widest = 0;
                for (const Placed& q : o.placed) widest = std::max(widest, q.width);
                if (widest > room + 0.0005f) {
                    const float inner = (o.right - o.left) * 0.92f, middle = (o.left + o.right) / 2;
                    for (Placed& q : o.placed) q.width = std::min(q.width, inner), q.x = middle - q.width / 2;
                }
            }
        }
        // (the Vita's cursor - FUN_8123f2f4 / FUN_8123e184: the option under it its comMsgC<n>Panel
        // shown, its text at 0.227, the others' at 0.88)
        const bool cursor = vitaButtons() && ev.choiceAnswer() < 0, lit = cursor && kk == choiceCursor;
        float px, py;
        if (lit && cr3UiCallNth(menu, "call_comMsg" + c + "Panel", kk, px, py)) cr3UiPut("commsg" + lc + "panel", "", 0, px, py, view, a);
        for (const Placed& q : o.placed) drawUiText(q.text, size - 4, q.x, q.y, w, h, true, cursor ? a * (lit ? 0.227f : 0.88f) : a, q.width);
        if (!cr3UiCallNth(menu, "call_comMsg" + c + "Panel", kk, px, py)) continue;
        UiFig& panel = uiFig("commsg" + lc + "panel");
        panel.fig.place = cr3::Mat34();
        panel.fig.place.m[0] = panel.fig.place.m[5] = panel.fig.place.m[10] = cr3UiScale;
        panel.fig.place.m[3] = px, panel.fig.place.m[7] = py, panel.fig.place.m[11] = cr3UiZ;
        poseUi(panel, "commsg" + lc + "panel", 0);
        if (o.row) choiceRects.push_back({false, o.left, o.top, o.right, o.bottom});
        float ex, ey;
        if (kk == ev.choiceAnswer() && cr3UiCall(panel, "call_comBtnEffe", ex, ey)) cr3UiPut("combtneffe", "", std::max(ev.choiceClosing(), 0.0f) + (closing ? 0.0f : ev.choiceAge()), ex, ey, view);
    }
}

void Viewer::answerChoice(int index) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || ev->choiceShown() < 2 || ev->choiceAnswer() >= 0 || index < 0 || index >= ev->choiceShown()) return;
    audio.playSe("com_003", false); // (comBtn's tap: decide)
    ev->answerChoice(index);
    choiceCursor = 0;
    SDL_Log("choice: %d of %d", index + 1, ev->choiceShown());
}

bool Viewer::choiceShown() {
    cr3::EventRuntime* ev = activeEvents();
    return ev && ev->choiceShown() >= 2;
}

bool Viewer::choiceKey(SDL_Keycode k) {
    cr3::EventRuntime* ev = activeEvents();
    if (!vitaButtons() || !ev || ev->choiceShown() < 2 || ev->choiceAnswer() >= 0) return false;
    const int n = ev->choiceShown();
    if (k == SDLK_UP || k == SDLK_DOWN) {
        choiceCursor = (std::clamp(choiceCursor, 0, n - 1) + (k == SDLK_DOWN ? 1 : n - 1)) % n;
        audio.playSe("com_002", false);
        SDL_Log("choice: the cursor on %d", choiceCursor + 1);
        return true;
    }
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) return answerChoice(std::clamp(choiceCursor, 0, n - 1)), true;
    return false;
}

bool Viewer::choiceClick(float fx, float fy) {
    if (!choiceShown()) return false;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    for (size_t i = 0; i < choiceRects.size(); ++i) {
        const YesNoRect& r = choiceRects[i];
        if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
            answerChoice(int(i));
            break;
        }
    }
    return true; // (the menu takes every tap)
}

void Viewer::answerYesNo(bool yes) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || !ev->yesNoShown()) return;
    audio.playSe(yes ? "com_003" : "com_004", false); // (comBtn's tap: decide, cancel)
    ev->answerYesNo(yes);
    yesNoCursor = 0, yesNoSeenAge = -1;
    SDL_Log("yes / no: %s", yes ? "yes" : "no");
}

bool Viewer::yesNoShown() {
    cr3::EventRuntime* ev = activeEvents();
    return ev && ev->yesNoShown();
}

bool Viewer::yesNoKey(SDL_Keycode k) {
    if (!yesNoShown()) return false;
    cr3::EventRuntime* ev = activeEvents();
    if (ev->yesNoAge() < yesNoSeenAge) yesNoCursor = 0;
    yesNoSeenAge = ev->yesNoAge();
    if (vitaButtons() && (k == SDLK_UP || k == SDLK_DOWN)) {
        yesNoCursor ^= 1;
        audio.playSe("com_002", false);
        SDL_Log("yes / no: cursor %s", yesNoCursor == 0 ? "yes" : "no");
    } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) answerYesNo(!vitaButtons() || yesNoCursor == 0);
    else if (k == SDLK_Y) answerYesNo(true);
    else if (k == SDLK_BACKSPACE || k == SDLK_N) answerYesNo(false);
    return true;
}

bool Viewer::tutorialShoulder(bool right) {
    if (manualStage == 2 && manualToList && !manualEntries.empty()) {
        const int next = manualCursor + (right ? 1 : -1);
        if (next < 0 || size_t(next) >= manualEntries.size()) {
            manualEvents.reset(), manualStage = 1, manualStart = uiClock;
            SDL_Log("tutorial: back to the list");
        } else playManual(next);
        return true;
    }
    cr3::EventRuntime* ev = manualStage == 2 && manualEvents ? manualEvents.get() : activeEvents();
    if (!ev || !ev->tutorialPage(right ? 1 : -1)) return false;
    SDL_Log("tutorial: page %d", ev->page() + 1);
    return true;
}

bool Viewer::yesNoClick(float fx, float fy) {
    if (!yesNoShown()) return false;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    for (const YesNoRect& r : yesNoRects)
        if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
            answerYesNo(r.yes);
            break;
        }
    return true; // (the buttons take every tap)
}

void Viewer::drawAsk(const std::string& message, float t, const Mat4& view, int w, int h, int yes, int no) {
    UiFig& win = uiFig("syswin_io");
    win.fig.place = cr3::Mat34();
    poseUi(win, "syswin_io", t);
    drawFigure(win.fig, view.data());
    float x, y;
    size_t from = 0;
    for (int line = 1; line <= 3 && from <= message.size(); ++line) { // (Cr3UtilNPrint: a line each "\n", typeM_L01 ..)
        const size_t at = message.find("\\n", from);
        char jn[16];
        std::snprintf(jn, sizeof jn, "typeM_L%02d", line);
        if (uiJoint(win, jn, x, y)) drawUiText(message.substr(from, at == std::string::npos ? std::string::npos : at - from), 26, x, y, w, h, true); // (an L joint: from the left)
        if (at == std::string::npos) break;
        from = at + 2;
    }
    UiFig& ch = uiFig("choice_io");
    ch.fig.place = cr3::Mat34();
    poseUi(ch, "choice_io", t);
    drawFigure(ch.fig, view.data());
    for (int n : {11, 12}) {
        char jn[24];
        std::snprintf(jn, sizeof jn, "call_cBtn_fr%d", n);
        if (!uiJoint(ch, jn, x, y)) continue;
        UiFig& c = uiFig("circlebtn_fr");
        c.fig.place = cr3::Mat34();
        c.fig.place.m[3] = x, c.fig.place.m[7] = y;
        poseUi(c, "circlebtn_fr", float(n) / 20.0f);
        focusLook(c, n == 11 ? yes : no);
        drawFigure(c.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) tapRects.push_back({n == 11 ? yes : no, sx, sy, ex, ey});
    }
}

void Viewer::updateVistaSlash() {
    cr3::EventRuntime* ev = activeEvents();
    const bool awaited = ev && ev->awaitingSlash();
    if (!awaited) {
        vslash = VistaSlash();
        return;
    }
    if (vslash.stage == 0) {
        vslash = VistaSlash(), vslash.stage = 1, vslash.end = ev->finishingSlash(), vslash.at = uiClock;
        SDL_Log("vista: the slash awaited%s (%.1f s)", vslash.end ? " (the finale)" : "", uiClock);
    }
    if (vslash.stage == 1 && uiClock - vslash.at > 0.5f && !inputScript.empty() && (inputScript.front() == "slash" || inputScript.front() == "slashfail")) {
        const bool hit = inputScript.front() == "slash";
        inputScript.pop_front();
        vslashEnd(hit);
    }
    const float t = uiClock - vslash.at;
    if (vslash.stage == 2 && t > uiDuration(vslash.end ? "catastrophe" : "execution")) vslash.stage = 3, vslash.at = uiClock;
    else if (vslash.stage == 3 && t > uiDuration(vslash.end ? "finale_io" : "kill_io", vslash.end ? "finale" : "kill")) {
        if (vslash.hit) {
            SDL_Log("vista: the slash done");
            ev->advance(); // (CheckExecution: true)
            vslash = VistaSlash();
        } else {
            vslash = VistaSlash(), vslash.stage = 1, vslash.end = ev->finishingSlash(), vslash.at = uiClock; // (SetEnforecementEffectParameter again)
        }
    }
}

void Viewer::vslashEnd(bool hit) {
    if (vslash.stage != 1) return;
    vslash.hit = hit, vslash.at = uiClock;
    if (hit) audio.playSe(0x15c, false), vslash.stage = 2;
    else vslash.stage = 3;
    SDL_Log("vista: the slash %s (%.1f s)", hit ? "lands" : "misses", uiClock);
}

// the mark's place (its figure's own, the interface's units)
bool Viewer::vslashPlace(float& x, float& y) {
    UiFig& f = uiFig(vslash.end ? "finale" : "kill");
    return uiJoint(f, "tapAreaS01", x, y) ? true : (x = 0, y = 0, true);
}

void Viewer::vslashPointer(float fx, float fy, int phase) { // (0 pressed, 2 let go)
    if (vslash.stage != 1) return;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    if (phase == 0) {
        vslash.pressed = true, vslash.x0 = x, vslash.y0 = y;
        return;
    }
    if (!vslash.pressed) return;
    vslash.pressed = false;
    float mx = 0, my = 0;
    vslashPlace(mx, my);
    const float dx = x - vslash.x0, dy = y - vslash.y0, l2 = dx * dx + dy * dy;
    if (l2 <= 0) return; // (a tap: nothing)
    const float u = ((mx - vslash.x0) * dx + (my - vslash.y0) * dy) / l2;
    vslashEnd(u >= 0 && u <= 1 && std::hypot(vslash.x0 + u * dx - mx, vslash.y0 + u * dy - my) * 2048.0f < 192.0f);
}

float Viewer::uiDuration(const std::string& anim, const std::string& fig) {
    UiFig& f = uiFig(fig.empty() ? anim : fig);
    poseUi(f, anim, 0);
    return f.fig.animLoaded ? f.fig.anim.duration() : 0.5f;
}

void Viewer::drawVistaSlash() {
    if (vslash.stage == 0) return;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f;
    const Mat4 view = cr3::mat4Multiply(uiProjection(screenW, screenH), camera);
    cr3::SceneLighting l;
    dev->setLighting(l);
    const std::string name = vslash.end ? "finale" : "kill";
    UiFig& f = uiFig(name);
    f.fig.place = cr3::Mat34();
    const float t = uiClock - vslash.at, in = uiDuration(name + "_io", name);
    if (vslash.stage == 1) {
        if (t < in) poseUi(f, name + "_io", t);
        else poseUi(f, name + "_wait", t - in, true);
    } else if (vslash.stage == 2) {
        poseUi(f, name + "_wait", t, true);
    } else {
        poseUi(f, name + "_io", std::max(in - t, 0.0f)); // (+0x5c 2: backwards)
    }
    drawFigure(f.fig, view.data());
    float mx = 0, my = 0;
    vslashPlace(mx, my);
    if (vslash.stage == 1) { // (the cursor)
        const std::string cn = vslash.end ? "bluecsr_loop" : "redcsr_loop";
        UiFig& c = uiFig(cn);
        c.fig.place = cr3::Mat34();
        poseUi(c, cn, uiClock, true);
        drawFigure(c.fig, view.data());
    } else if (vslash.stage == 2) { // (the hit's)
        const std::string hn = vslash.end ? "catastrophe" : "execution";
        UiFig& h = uiFig(hn);
        h.fig.place = cr3::Mat34();
        poseUi(h, hn, t);
        drawFigure(h.fig, view.data());
    }
}

void Viewer::loadSaveSlots() {
    for (int k = 0; k < kCr3SaveSlots; ++k) {
        Cr3SaveSlotInfo& i = saveMenu.slots[size_t(k)];
        i = Cr3SaveSlotInfo();
        std::error_code ec;
        i.occupied = std::filesystem::exists(savePath(cr3SaveFile(k + 1).c_str()), ec) || bool(ec);
        std::map<std::string, std::string> extra;
        i.exists = i.state.load(readSaveText(k + 1), &extra) && !extra["area"].empty();
        i.seconds = std::atol(extra["playtime"].c_str()) / 20;
        if (!i.exists) continue;
        i.state.cr3Db = game.cr3Db;
        std::swap(game, i.state); // (its party's numbers, as the game's own)
        for (int j = 0; j < 3; ++j) {
            cr3::Cr3Fighter f;
            if (game.cr3Party[j] > 0 && cr3CharaStats(game.cr3Party[j], f)) i.hp[j] = f.maxHp, i.mp[j] = cr3CharaMp(game.cr3Party[j], f.maxMp);
        }
        std::swap(game, i.state);
    }
}

void Viewer::openSaveMenu(int page) {
    loadSaveSlots();
    saveMenu.page = std::clamp(page, 0, kCr3SavePages - 1);
    saveMenu.mode = 0, saveMenu.focus = kCr3SaveSlots, saveMenu.source = -1, saveMenu.ask = 0, saveMenu.since = 0;
    saveMenu.askFocus = 0, saveMenu.notice.clear();
    SDL_Log("title: %d save slots, page %d/%d", kCr3SaveSlots, saveMenu.page + 1, kCr3SavePages);
}

// (SaveFilePanel::Enable: those to be picked - all for playing, those with a game to copy or
// delete, the empty ones to copy into)
bool Viewer::saveSlotOn(int k) const {
    const Cr3SaveMenu& m = saveMenu;
    if (k < 0 || k >= kCr3SaveSlots) return false;
    const auto& slot = m.slots[size_t(k)];
    if (m.mode == 2 || m.mode == 5) return !slot.occupied;
    if (m.mode == 3) return slot.occupied; // unreadable files may still be explicitly deleted
    return m.mode == 0 || slot.exists;
}

void Viewer::saveChapter(const cr3::GameState& st, int& title, int& stars) { // (MbSaveFileInfo: flag, chapter, stars)
    title = 0, stars = 0;
    for (int k = 0; k < 33; ++k)
        if (st.flag(701 + k)) title = k, stars = k < 9 ? 0 : k < 17 ? 1 : k < 22 ? 2 : k < 32 ? 3 : 4;
    if (st.flag(6458)) title = 32, stars = 5;
}

bool Viewer::savePlusAvailable() const {
    return std::any_of(saveMenu.slots.begin(), saveMenu.slots.end(), [](const Cr3SaveSlotInfo& i) {
               return i.exists && i.state.canNewGamePlus();
           }) && std::any_of(saveMenu.slots.begin(), saveMenu.slots.end(), [](const Cr3SaveSlotInfo& i) {
               return !i.occupied;
           });
}

bool Viewer::createSavePlus(int source, int destination) {
    try {
        if (source == destination || source < 0 || source >= kCr3SaveSlots || destination < 0 || destination >= kCr3SaveSlots)
            throw vitaimport::Error("Invalid New Game Plus slots.");
        if (std::filesystem::exists(savePath(cr3SaveFile(destination + 1).c_str())))
            throw vitaimport::Error("The destination slot is occupied.");
        cr3::GameState previous, fresh;
        std::map<std::string, std::string> oldExtra;
        std::string error;
        if (!previous.load(readSaveText(source + 1), &oldExtra, &error)) throw vitaimport::Error(error);
        previous.cr3Db = offlineDb.loaded() ? &offlineDb : nullptr;
        if (!previous.makeNewGamePlus(fresh, &error)) throw vitaimport::Error(error);
        if (fresh.cr3Db) offlineDb.login(fresh, std::time(nullptr));
        std::map<std::string, std::string> extra;
        extra["area"] = "t0108"; // no old place: the prologue's normal spawn
        extra["saved"] = std::to_string(static_cast<long long>(std::time(nullptr)));
        extra["playtime"] = oldExtra.count("playtime") ? oldExtra["playtime"] : "0";
        extra["options"] = oldExtra.count("options") ? oldExtra["options"] :
            std::to_string(optionLanguage) + " " + std::to_string(optionQuality) + " " +
            std::to_string(optionVolume[0]) + " " + std::to_string(optionVolume[1]) + " " + std::to_string(optionVolume[2]);
        vitaimport::Plan plan;
        plan.slots = 1;
        plan.files[cr3SaveFile(destination + 1)] = fresh.save(extra);
        vitaimport::install(plan, std::filesystem::path(savePath()).parent_path().u8string());
        SDL_Log("title: New Game Plus %d from file %d into file %d", fresh.cr3NewGamePlus, source + 1, destination + 1);
        return true;
    } catch (const std::exception& e) {
        SDL_Log("New Game Plus: %s", e.what());
        saveMenu.notice = "Could not create the new save.\\nCheck the destination is empty and writable.\\nYour original save was kept.";
        return false;
    }
}

void Viewer::chooseSave(int id) {
    Cr3SaveMenu& m = saveMenu;
    if (!m.notice.empty()) {
        if (id == kTapSaveYes || id == kTapSaveNo || id == -1) m.notice.clear();
        return;
    }
    if (m.ask) {
        if (m.ask == 6) {
            if (id == kTapSavePlus) {
                if (!savePlusAvailable() || !m.slots[size_t(m.askSlot)].state.canNewGamePlus()) return;
                m.source = m.askSlot, m.mode = 5, m.ask = 0, m.focus = kCr3SaveSlots;
                audio.playSe("com_003", false);
                return;
            }
            if (id == kTapSaveNo || id == -1) {
                m.ask = 0;
                audio.playSe("com_004", false);
                return;
            }
            if (id != kTapSaveYes) return;
            m.ask = 1; // Continue uses the ordinary load path below.
        }
        if (id == kTapSaveYes) {
            if (m.ask == 5) {
                if (!createSavePlus(m.source, m.askSlot)) return;
                // The ordinary title load path now reads the new slot and starts its prologue.
                m.ask = 1;
            }
            if (m.ask == 1) { // (5: utils::LoadData - its field, or a new game in it)
                audio.playSe("com_001", false);
                m.ask = 0, cr3SaveSlot = m.askSlot + 1;
                cr3Title.stage = 5, cr3Title.stageClock = 0;
                audio.stopBgm(15); // (TitleMain: StopBgm 0.5 s - the title's music out as the game starts)
                SDL_Log("title: file %d", cr3SaveSlot);
                return;
            }
            if (m.ask == 3) return audio.playSe("com_003", false), (void)(m.ask = 4), (void)(m.askSince = 0);
            if (m.ask == 2) { // (utils::CopyData)
                audio.playSe("com_011", false);
                const std::string text = readSaveText(m.source + 1);
                std::string error;
                if (text.empty() || !crstorage::replaceFile(savePath(cr3SaveFile(m.askSlot + 1).c_str()), text, &error)) {
                    SDL_Log("copy failed: %s", text.empty() ? "source could not be read" : error.c_str());
                    m.notice = "Could not copy the file. Please try again.";
                    return;
                }
                SDL_Log("title: file %d copied to %d", m.source + 1, m.askSlot + 1);
            } else { // (utils::DeleteData)
                audio.playSe("com_012", false);
                if (!SDL_RemovePath(savePath(cr3SaveFile(m.askSlot + 1).c_str()).c_str())) {
                    SDL_Log("delete failed: %s", SDL_GetError());
                    m.notice = "Could not delete the file. Please try again.";
                    return;
                }
                SDL_Log("title: file %d deleted", m.askSlot + 1);
            }
            return openSaveMenu(m.askSlot / kCr3SavePageSize);
        }
        if (id == kTapSaveNo || id == -1) {
            audio.playSe("com_004", false);
            const int was = m.ask;
            m.ask = 0;
            if (was == 5) m.mode = 5;
            else if (was >= 3) m.mode = 3, m.focus = kCr3SaveSlots; // (OnSelectDelete)
            return;
        }
        return;
    }
    if (id == -1) { // (the back: the title again - or, choosing, the files)
        audio.playSe("com_004", false);
        if (m.mode != 0) return void((m.mode = 0, m.focus = kCr3SaveSlots, m.source = -1));
        cr3Title.stage = 2, cr3Title.stageClock = 0;
        audio.playBgm(33, 10);
        return;
    }
    const int files = int(std::count_if(m.slots.begin(), m.slots.end(), [](const Cr3SaveSlotInfo& i) { return i.exists; }));
    const int occupied = int(std::count_if(m.slots.begin(), m.slots.end(), [](const Cr3SaveSlotInfo& i) { return i.occupied; }));
    if (id == kTapSaveCopy) {
        if (m.mode == 1 || m.mode == 2 || files == 0 || occupied == kCr3SaveSlots) return;
        return audio.playSe("com_003", false), (void)(m.mode = 1), (void)(m.focus = kCr3SaveSlots);
    }
    if (id == kTapSaveDelete) {
        if (m.mode == 3 || occupied == 0) return;
        return audio.playSe("com_003", false), (void)(m.mode = 3), (void)(m.focus = kCr3SaveSlots), (void)(m.source = -1);
    }
    if (id == kTapSavePrev || id == kTapSaveNext) {
        m.page = (m.page + (id == kTapSaveNext ? 1 : kCr3SavePages - 1)) % kCr3SavePages;
        m.focus = kCr3SaveSlots;
        audio.playSe("com_002", false);
        SDL_Log("title: save page %d/%d", m.page + 1, kCr3SavePages);
        return;
    }
    if (id < kTapSaveFile || id >= kTapSaveFile + kCr3SaveSlots) return;
    const int k = id - kTapSaveFile;
    if (!saveSlotOn(k)) return;
    m.page = k / kCr3SavePageSize;
    if (m.mode == 2 || m.mode == 5) { // copy / New Game Plus destination
        if (k == m.source) return;
        audio.playSe("com_003", false);
        m.focus = k;
        m.ask = m.mode == 5 ? 5 : 2, m.askSlot = k, m.askSince = 0;
        m.askFocus = m.ask == 5 ? 1 : 0;
        return;
    }
    if (k != m.focus) return audio.playSe("com_002", false), (void)(m.focus = k);
    audio.playSe("com_003", false);
    if (m.mode == 0 && m.slots[size_t(k)].occupied && !m.slots[size_t(k)].exists) {
        m.notice = "This save could not be read.\\nThe original file has been kept.";
        return;
    }
    if (m.mode == 0) m.ask = m.slots[size_t(k)].exists && m.slots[size_t(k)].state.canNewGamePlus() ? 6 : 1,
        m.askSlot = k, m.askSince = 0, m.askFocus = 0;
    else if (m.mode == 1) m.source = k, m.mode = 2, m.focus = k; // (OnDecideSource)
    else m.ask = 3, m.askSlot = k, m.askSince = 0;
}

void Viewer::stepSaveMenu(float dt) {
    Cr3SaveMenu& m = saveMenu;
    m.since += dt, m.askSince += dt;
    if (!inputScript.empty() && inputScript.front().compare(0, 5, "save:") != 0 && inputScript.front().compare(0, 5, "wait:") != 0 &&
        inputScript.front().compare(0, 6, "click:") != 0 && !m.ask && m.mode == 0 && m.notice.empty()) {
        m.askSlot = 0, m.ask = 1; // (--input going on past the title without save: tokens - file 1 played, as before the save files)
        return chooseSave(kTapSaveYes);
    }
    if (inputScript.empty() || inputScript.front().compare(0, 5, "save:") != 0 || m.since < 0.6f || uiClock < inputNext) return;
    const std::string t = inputScript.front().substr(5); // (--input save:<1 .. 12> / next / prev / copy / delete / yes / no / back)
    inputScript.pop_front();
    inputNext = uiClock + 0.4f;
    if (t == "plus") chooseSave(kTapSavePlus);
    else if (t == "copy") chooseSave(kTapSaveCopy);
    else if (t == "delete") chooseSave(kTapSaveDelete);
    else if (t == "yes") chooseSave(kTapSaveYes);
    else if (t == "no") chooseSave(kTapSaveNo);
    else if (t == "back") chooseSave(-1);
    else if (t == "next") chooseSave(kTapSaveNext);
    else if (t == "prev") chooseSave(kTapSavePrev);
    else if (t == "up") saveMenuKey(SDLK_UP);
    else if (t == "down") saveMenuKey(SDLK_DOWN);
    else if (t == "enter") saveMenuKey(SDLK_RETURN);
    else {
        char* end = nullptr;
        const long slot = std::strtol(t.c_str(), &end, 10);
        if (end != t.c_str() && !*end && slot >= 1 && slot <= kCr3SaveSlots) chooseSave(kTapSaveFile + int(slot) - 1);
    }
}

bool Viewer::saveMenuKey(SDL_Keycode k) {
    if (cr3Title.stage != 4) return false;
    Cr3SaveMenu& m = saveMenu;
    if (!m.notice.empty()) {
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE || k == SDLK_ESCAPE || k == SDLK_BACKSPACE) chooseSave(kTapSaveNo);
        return true;
    }
    if (m.ask) {
        if (m.ask == 6) {
            if (k == SDLK_UP || k == SDLK_LEFT) m.askFocus = (m.askFocus + 2) % 3;
            else if (k == SDLK_DOWN || k == SDLK_RIGHT) m.askFocus = (m.askFocus + 1) % 3;
            else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE)
                chooseSave(m.askFocus == 0 ? kTapSaveYes : m.askFocus == 1 ? kTapSavePlus : kTapSaveNo);
            else if (k == SDLK_Y) chooseSave(kTapSaveYes);
            else if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) chooseSave(kTapSaveNo);
            return true;
        }
        if ((vitaButtons() || m.ask == 5) && (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN)) m.askFocus ^= 1, audio.playSe("com_002", false);
        else if (k == SDLK_Y) chooseSave(kTapSaveYes);
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) chooseSave((vitaButtons() || m.ask == 5) && m.askFocus == 1 ? kTapSaveNo : kTapSaveYes);
        else if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE || k == SDLK_N) chooseSave(kTapSaveNo);
        return true;
    }
    if (vitaButtons() && m.footerFocus >= 0) {
        if (k == SDLK_LEFT || k == SDLK_RIGHT) m.footerFocus ^= 1, audio.playSe("com_002", false), SDL_Log("title: save footer %s", m.footerFocus ? "delete" : "copy");
        else if (k == SDLK_DOWN) {
            m.footerFocus = -1, m.focus = kCr3SaveSlots;
            saveMenuKey(SDLK_DOWN);
        } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) {
            const int mode = m.mode;
            chooseSave(m.footerFocus ? kTapSaveDelete : kTapSaveCopy);
            if (mode == m.mode) audio.playSe("com_005", false);
            else m.footerFocus = -1;
        } else if (k == SDLK_BACKSPACE || k == SDLK_ESCAPE) m.footerFocus = -1, chooseSave(-1);
        else if (k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) m.footerFocus = -1, chooseSave(k == SDLK_PAGEUP ? kTapSavePrev : kTapSaveNext);
        return true;
    }
    if (vitaButtons() && k == SDLK_UP && (m.focus == kCr3SaveSlots || m.focus % kCr3SavePageSize == 0) && m.mode != 2 && m.mode != 5) {
        m.footerFocus = 0;
        audio.playSe("com_002", false);
        SDL_Log("title: save footer copy");
        return true;
    }
    if (vitaButtons() && k == SDLK_DOWN && m.focus < kCr3SaveSlots && m.focus % kCr3SavePageSize == kCr3SavePageSize - 1) return true;
    if (k == SDLK_UP || k == SDLK_DOWN) { // (the next file that can be picked)
        for (int step = 1; step <= kCr3SaveSlots; ++step) {
            const int from = m.focus < kCr3SaveSlots ? m.focus
                : m.page * kCr3SavePageSize + (k == SDLK_DOWN ? -1 : kCr3SavePageSize);
            const int at = (from + (k == SDLK_DOWN ? step : kCr3SaveSlots - step)) % kCr3SaveSlots;
            if (saveSlotOn(at) && !((m.mode == 2 || m.mode == 5) && at == m.source)) {
                if (m.mode == 2 || m.mode == 5) m.askSlot = at;
                m.focus = at, m.page = at / kCr3SavePageSize;
                audio.playSe("com_002", false);
                SDL_Log("title: save focus %d, page %d/%d", at + 1, m.page + 1, kCr3SavePages);
                break;
            }
        }
    } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) {
        if (m.focus < kCr3SaveSlots) chooseSave(kTapSaveFile + m.focus);
    } else if (k == SDLK_LEFT || k == SDLK_PAGEUP) chooseSave(kTapSavePrev);
    else if (k == SDLK_RIGHT || k == SDLK_PAGEDOWN) chooseSave(kTapSaveNext);
    else if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) chooseSave(-1);
    else if (k == SDLK_C) chooseSave(kTapSaveCopy);
    else if (k == SDLK_DELETE || k == SDLK_D) chooseSave(kTapSaveDelete);
    return true;
}

bool Viewer::saveMenuClick(float fx, float fy) {
    if (cr3Title.stage != 4) return false;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    for (auto it = saveTaps.rbegin(); it != saveTaps.rend(); ++it)
        if (ux >= std::min(it->x0, it->x1) && ux <= std::max(it->x0, it->x1) && uy >= std::min(it->y0, it->y1) && uy <= std::max(it->y0, it->y1)) {
            chooseSave(it->id);
            break;
        }
    return true;
}

bool Viewer::drawNumberedEmptySavePanel(const UiFig& panel, const Mat4& view) {
    if (panel.fig.meshes.size() != 1) return false;
    const ViewMesh& original = panel.fig.meshes[0];
    const auto& uv = original.source.uv;
    if (uv.size() != 8 || original.vertices.size() != 4 * cr3::kVertexStride) return false;
    float u0 = uv[0], u1 = uv[0], v0 = uv[1], v1 = uv[1];
    for (int k = 0; k < 4; ++k) {
        u0 = std::min(u0, uv[k * 2]), u1 = std::max(u1, uv[k * 2]);
        v0 = std::min(v0, uv[k * 2 + 1]), v1 = std::max(v1, uv[k * 2 + 1]);
    }
    if (u1 <= u0 || v1 <= v0) return false;
    const bool fresh = saveNumberPatch.meshes.empty();
    if (fresh) saveNumberPatch.meshes.resize(1);
    ViewMesh& patch = saveNumberPatch.meshes[0];
    patch.vertices.resize(36 * cr3::kVertexStride);
    const float columns[] = {0, 0.32f, 0.69f, 1}, rows[] = {0, 0.58f, 0.84f, 1};
    std::vector<uint32_t> indices;
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) {
        const float corners[4][2] = {{columns[col], rows[row]}, {columns[col + 1], rows[row]},
            {columns[col + 1], rows[row + 1]}, {columns[col], rows[row + 1]}};
        const uint32_t first = uint32_t((row * 3 + col) * 4);
        for (int corner = 0; corner < 4; ++corner) {
            uint8_t* out = &patch.vertices[size_t(first + corner) * cr3::kVertexStride];
            std::memcpy(out, original.vertices.data(), cr3::kVertexStride);
            float pos[3] = {};
            for (int k = 0; k < 4; ++k) {
                const bool right = uv[k * 2] > (u0 + u1) / 2, top = uv[k * 2 + 1] > (v0 + v1) / 2;
                const float weight = (right ? corners[corner][0] : 1 - corners[corner][0]) *
                                     (top ? corners[corner][1] : 1 - corners[corner][1]);
                float point[3];
                std::memcpy(point, &original.vertices[size_t(k) * cr3::kVertexStride], sizeof point);
                for (int c = 0; c < 3; ++c) pos[c] += weight * point[c];
            }
            // Split the native quad so its translucent background is composed once;
            // only the header's central strip takes its UVs from the blank silver.
            const float u = row == 1 && col == 1 ? (corner == 1 || corner == 2 ? 0.27f : 0.24f) : corners[corner][0];
            const float sample[2] = {u0 + (u1 - u0) * u, v0 + (v1 - v0) * corners[corner][1]};
            std::memcpy(out, pos, sizeof pos);
            std::memcpy(out + 12, sample, sizeof sample);
        }
        for (uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u}) indices.push_back(first + index);
    }
    if (fresh) patch.gpu = dev->upload(patch.vertices, indices, -1);
    else dev->updateVertices(patch.gpu, patch.vertices);
    patch.material = original.material;
    patch.material.states[0xa6] = {0, 0, 0, 0}; // the native flat panel, no back-face culling
    patch.texture = original.texture, patch.extraMap = original.extraMap;
    for (int k = 0; k < 4; ++k) saveNumberPatch.tint[k] = panel.fig.tint[k];
    drawFigure(saveNumberPatch, view.data());
    return true;
}

// A cleared file has the native load choices plus New Game Plus. The source
// is the file being opened, so there is no separate global restart control.
void Viewer::drawClearedSaveChoices(int w, int h, const Mat4& view) {
    Cr3SaveMenu& m = saveMenu;
    drawCr3Told("File " + std::to_string(m.askSlot + 1) + " is cleared.\\nContinue playing or start New Game Plus?\\nNew Game Plus keeps your completed save.",
                1.0f, kTapSaveNo, w, h, view, &saveTaps);
    saveTaps.clear();
    float x, y;
    UiFig& pos = cr3UiPut("combtnposition", "", 0, 0, 0, view);
    if (!cr3UiCall(pos, "call_comBtnMenu", x, y, w, h)) return;
    UiFig& menu = cr3UiPut("combtnmenu", "combtnmenu_io", m.askSince, x, y, view, 0);
    const char* labels[] = {"Continue", "New Game Plus", "Back"};
    const int ids[] = {kTapSaveYes, kTapSavePlus, kTapSaveNo};
    for (int b = 0; b < 3; ++b) {
        float px, py, cx, cy, ax, ay, bx, by;
        if (!cr3UiCall(menu, "call_comBtnPanel_00" + std::to_string(4 + b), px, py)) continue;
        const bool enabled = b != 1 || savePlusAvailable();
        cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = enabled ? 1.0f : 0.4f;
        UiFig& panel = cr3UiPut("combtnpanel", "", 0, px, py, view);
        // The panel contains the blank text pill; its own posed mesh gives
        // the text bounds without the circular icon to its right.
        float left = 1e9f, right = -1e9f, bottom = 1e9f, top = -1e9f;
        for (const ViewMesh& mesh : panel.fig.meshes) if (!mesh.hidden)
            for (size_t at = 0; at + 12 <= mesh.vertices.size(); at += cr3::kVertexStride) {
                float p[3]; std::memcpy(p, &mesh.vertices[at], sizeof p);
                left = std::min(left, p[0]), right = std::max(right, p[0]);
                bottom = std::min(bottom, p[1]), top = std::max(top, p[1]);
            }
        float labelRight = right;
        if (cr3UiCall(panel, "call_comBtnBg_fr00", cx, cy)) {
            UiFig& circle = cr3UiPut("combtnbg_fr00", "", float(b) / 30, cx, cy, view);
            for (const ViewMesh& mesh : circle.fig.meshes) if (!mesh.hidden)
                for (size_t at = 0; at + 12 <= mesh.vertices.size(); at += cr3::kVertexStride) {
                    float x; std::memcpy(&x, &mesh.vertices[at], sizeof x);
                    labelRight = std::min(labelRight, x);
                }
        }
        // Use the native confirm/back icons; the new option is named in full.
        if (cr3UiCall(panel, "call_comBtnIcon_fr00", cx, cy)) cr3UiPut("combtnicon_fr00", "", float(b == 2 ? 11 : 10) / 30, cx, cy, view);
        if (cr3UiCall(panel, "call_comBtnR", cx, cy)) cr3UiPut("combtnr", "", 0, cx, cy, view);
        cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
        dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
        if (right > left && top > bottom)
            drawUiText(labels[b], 22, (left + labelRight + 0.08f) / 20, (bottom + top) / 20, w, h, false,
                       enabled ? (b == m.askFocus ? 0.0f : 0.08f) : 0.6f, (labelRight - left - 0.24f) / 10);
        if (enabled && cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by))
            saveTaps.push_back({ax, ay, bx, by, ids[b]});
    }
}

void Viewer::drawSaveMenu(int w, int h) {
    saveTaps.clear();
    Cr3SaveMenu& m = saveMenu;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    float x, y, cx, cy, ax, ay, bx, by;
    float saveCenterX = 0;
    UiFig& pos = cr3UiPut("ttldatapositon", "ttldatapositon_in", m.since, 0, 0, view);
    Cr3SaveSlotInfo* picked = m.focus < kCr3SaveSlots && m.slots[size_t(m.focus)].exists ? &m.slots[size_t(m.focus)] : nullptr;
    if (cr3UiCall(pos, "call_ttlDataParty", x, y)) {
        UiFig& party = cr3UiPut("ttldataparty", "", 0, x, y, view);
        if (picked) {
            int title = 0, stars = 0;
            saveChapter(picked->state, title, stars);
            if (cr3UiCall(party, "call_ttlDataTitle", cx, cy))
                for (int k = 1; k <= 5; ++k) cr3UiPut("ttldatatitle0" + std::to_string(k), "", float(title) / 30, cx, cy, view);
            for (int k = 0; k < 3; ++k) { // (VsModeCharaPanel: the gene's card, its level, HP, MP)
                if (!cr3UiCall(party, "call_VsMainParty_0" + std::to_string(k + 1), cx, cy)) continue;
                const int ch = picked->state.cr3Party[k];
                if (ch <= 0) continue;
                UiFig& card = cr3UiPut("vsmainparty", "", 0, cx, cy, view);
                float gx, gy;
                // (VsModeCharaPanel::Pose: the gene's CardModel - 4.6 x - at call_VsMainPC, the face's
                // VsMainPC at call_GeneCard)
                const int gk = picked->state.wornGene(ch);
                if (gk >= 0 && game.cr3Db && cr3UiCall(card, "call_VsMainPC", gx, gy)) {
                    dev->clearDepth();
                    drawCr3Card(picked->state.cr3Genes[size_t(gk)].master, gx, gy, 1.0f, 4.6f, view);
                }
                if (cr3UiCall(card, "call_GeneCard", gx, gy)) cr3UiPut("vsmainpc", "", float(ch - 1) / 30, gx, gy, view); // (SetCharacterFace)
                if (gk < 0 || !game.cr3Db) continue;
                const cr3::Cr3Gene& g = picked->state.cr3Genes[size_t(gk)];
                for (const auto& [joint, fig, value, places] :
                     {std::tuple<const char*, const char*, int, int>{"call_camTopLv_num00", "camtoplv_num00", game.cr3Db->geneLevel(g), 3},
                      {"call_camTopHp_num00", "camtophp_num00", picked->hp[k], 5}, {"call_camTopMp_num00", "camtopmp_num00", picked->mp[k], 4}}) {
                    if (!cr3UiCall(card, joint, gx, gy)) continue;
                    UiFig& num = cr3UiPut(fig, "", 0, gx, gy, view);
                    const bool lv = std::strcmp(fig, "camtoplv_num00") == 0;
                    cr3UiNumber(num, lv ? "call_camTopNumLv_fr00" : "call_camTopNum_fr00", lv ? "camtopnumlv_fr00" : "camtopnum_fr00", value, places, 0, view);
                }
            }
        }
    }
    if (cr3UiCall(pos, "call_ttlDataFile", x, y)) {
        UiFig& file = cr3UiPut("ttldatafile", "", 0, x, y, view);
        for (int row = 0; row < kCr3SavePageSize; ++row) {
            const int k = m.page * kCr3SavePageSize + row;
            const std::string n = "0" + std::to_string(row + 1);
            if (!cr3UiCall(file, "call_ttlDataSave" + n, cx, cy)) continue;
            const Cr3SaveSlotInfo& i = m.slots[size_t(k)];
            const bool active = k == m.focus || ((m.mode == 2 || m.mode == 5) && k == m.source) || ((m.ask == 2 || m.ask == 5) && k == m.askSlot);
            const float alpha = saveSlotOn(k) || active ? 1.0f : 0.5f;
            // SaveFilePanel::Deactivate/Activate: silver for an empty idle slot,
            // blue for an existing save, cyan when selected (8123a1d4 / 8123a276).
            UiFig& panel = cr3UiPose("ttldatasave" + n, "", float(active ? 1 : i.occupied ? 0 : 2) / 30, cx, cy);
            panel.fig.tint[0] = panel.fig.tint[1] = panel.fig.tint[2] = alpha;
            panel.fig.tint[3] = 1;
            const bool emptyNumber = !i.occupied && !active && k >= kCr3SavePageSize && drawNumberedEmptySavePanel(panel, view);
            if (!emptyNumber) drawFigure(panel.fig, view.data());
            if (active && cr3UiCall(panel, "call_ttlDataOk", ax, ay))
                cr3UiPut("ttldataok", "", float((m.mode == 2 || m.mode == 5) && k == m.source ? 1 : (m.ask == 2 || m.ask == 5) && k == m.askSlot ? 2 : 0) / 30, ax, ay, view, alpha);
            if (cr3UiCall(panel, "call_ttlDataTxt", ax, ay)) {
                UiFig& txt = cr3UiPut("ttldatatxt", "", 0, ax, ay, view, alpha);
                std::string head = menuAnnounce(0x312);
                char num[8];
                std::snprintf(num, sizeof num, "%02d", k + 1);
                if (const size_t at = head.find("%02d"); at != std::string::npos) head.replace(at, 4, num);
                else if (const size_t at2 = head.find("%d"); at2 != std::string::npos) head.replace(at2, 2, std::to_string(k + 1));
                // SaveFileText::Set uses ProvisionalMenuText type 1: white fill,
                // black outline, 24 for the header/time and 32 for the name.
                auto label = [&](const char* joint, const std::string& text, float size = 24, bool centred = false,
                                 float maxWidth = 0) {
                    float tx, ty;
                    if (text.empty() || !cr3UiCall(txt, joint, tx, ty)) return;
                    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                    drawUiText(text, size, (tx + (centred ? 0 : 0.063f)) / 10, ty / 10, w, h,
                               !centred, alpha, maxWidth, 0.001f);
                };
                if (i.occupied || active) label("typeM_L01", head);
                else if (emptyNumber) {
                    float tx, ty;
                    if (cr3UiCall(txt, "typeM_C04", tx, ty) && cr3UiCall(txt, "typeM_L01", ax, ay))
                        drawUiText(head, 20, tx / 10, (ay + 0.05f) / 10, w, h, false, alpha);
                }
                if (i.exists) {
                    int title = 0, stars = 0;
                    saveChapter(i.state, title, stars);
                    if (cr3UiCall(txt, "call_ttlDataStar_fr00", bx, by)) cr3UiPut("ttldatastar_fr00", "", float(stars) / 30, bx, by, view, alpha);
                    if (i.state.canNewGamePlus()) {
                        float tx, ty;
                        if (cr3UiCall(txt, "typeM_R03", tx, ty)) {
                            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                            const std::string cleared = "CLEARED";
                            drawUiText(cleared, 18, (tx - 0.063f) / 10 - uiTextWidth(cleared, 18, w, h),
                                       (ty + 0.36f) / 10, w, h, true, alpha, 0, 0.001f);
                        }
                    }
                    char tm[32];
                    std::snprintf(tm, sizeof tm, "%ld:%02ld", i.seconds / 3600, i.seconds % 3600 / 60);
                    float tx, ty;
                    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                    if (cr3UiCall(txt, "typeM_R03", tx, ty)) {
                        const float timeX = (tx - 0.063f) / 10 - uiTextWidth(tm, 24, w, h);
                        float lx, ly;
                        if (cr3UiCall(txt, "typeM_L02", lx, ly))
                            label("typeM_L02", menuAnnounce(0x2c7), 24, false,
                                  std::max(0.01f, timeX - (lx + 0.063f) / 10 - 0.006f));
                        drawUiText(tm, 24, timeX, ty / 10, w, h, true, alpha, 0, 0.001f);
                    }
                    const auto nm = i.state.cr3Names.find("name");
                    if (nm != i.state.cr3Names.end()) label("typeM_C04", nm->second, 32, true, 0.34f);
                } else if (i.occupied) label("typeM_C04", "Unreadable save", 24, true, 0.34f);
            }
            if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) {
                saveTaps.push_back({ax, ay, bx, by, kTapSaveFile + k});
                saveCenterX = (ax + bx) / 2;
            }
        }
        for (const auto& [joint, fig, id, on] : {std::tuple<const char*, const char*, int, bool>{"call_ttlDataCopy", "ttldatacopy", kTapSaveCopy, m.mode == 1 || m.mode == 2},
                                                 {"call_ttlDatadelete", "ttldatadelete", kTapSaveDelete, m.mode == 3}}) {
            if (!cr3UiCall(file, joint, cx, cy)) continue;
            if (vitaButtons() && m.footerFocus == (id == kTapSaveCopy ? 0 : 1)) cr3UiTint[0] = 0.5f, cr3UiTint[1] = 1, cr3UiTint[2] = 1.5f;
            UiFig& b = cr3UiPut(fig, "", float(on ? 1 : 0) / 30, cx, cy, view);
            cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
            if (cr3UiCall(b, "tapAreaS01", ax, ay) && cr3UiCall(b, "tapAreaE01", bx, by)) saveTaps.push_back({ax, ay, bx, by, id});
        }
    }
    // (the header's coins and ML, the footer's team level and name: the picked file's - SetHeaderCOIN ..)
    const auto keepCoins = game.cr3Coins;
    const auto keepMoney = game.cr3Money;
    const int keepLevel = game.cr3TeamLevel;
    const std::string keepTeam = game.cr3Names["team"];
    game.cr3Coins = picked ? picked->state.cr3Coins : 0, game.cr3Money = picked ? picked->state.cr3Money : 0;
    game.cr3TeamLevel = picked ? picked->state.cr3TeamLevel : 0;
    const auto tn = picked ? picked->state.cr3Names.find("team") : game.cr3Names.end();
    game.cr3Names["team"] = picked && tn != picked->state.cr3Names.end() ? tn->second : std::string();
    const std::string help = m.mode == 5 ? "Choose an empty destination slot. Your completed save will be kept." :
        menuAnnounce(m.mode == 0 ? 0x311 : m.mode == 1 ? 0x309 : m.mode == 2 ? 0x30a : 0x30c);
    drawCr3Header(m.since, 0x26, help, w, h, view);
    std::vector<Cr3GeneLabTap> keep;
    keep.swap(cr3CampTaps);
    drawCr3CampFooter(w, h, view);
    for (const Cr3GeneLabTap& t : cr3CampTaps) saveTaps.push_back(t);
    keep.swap(cr3CampTaps);
    game.cr3Coins = keepCoins, game.cr3Money = keepMoney, game.cr3TeamLevel = keepLevel, game.cr3Names["team"] = keepTeam;
    // The original three file panels are reused on each page. Native option arrows
    // are centered below the save panels; file ids and operations remain global.
    dev->clearDepth();
    float pageX = 0, pageY = 0;
    cr3UiScale = 0.5f;
    for (int dir : {-1, 1}) {
        UiFig& arrow = cr3UiPut(dir < 0 ? "optsltlbtn" : "optsltrbtn", "", 0,
                                saveCenterX + (dir < 0 ? -1.365f : 1.035f), -2.005f, view);
        if (cr3UiCall(arrow, "tapAreaS01", ax, ay) && cr3UiCall(arrow, "tapAreaE01", bx, by)) {
            saveTaps.push_back({std::min(ax, bx) - 0.05f, std::min(ay, by) - 0.05f,
                                std::max(ax, bx) + 0.05f, std::max(ay, by) + 0.05f,
                                dir < 0 ? kTapSavePrev : kTapSaveNext});
            pageX += (ax + bx) / 4, pageY += (ay + by) / 4;
        }
    }
    cr3UiScale = 1;
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    const std::string pageLabel = "Page " + std::to_string(m.page + 1) + " / " + std::to_string(kCr3SavePages);
    drawUiText(pageLabel, 22, pageX / 10 + 0.0015f, pageY / 10 - 0.0015f, w, h, false, 0);
    drawUiText(pageLabel, 22, pageX / 10, pageY / 10, w, h, false);
    if (!m.notice.empty()) {
        saveTaps.clear();
        drawCr3Told(m.notice, 1.0f, kTapSaveNo, w, h, view, &saveTaps);
        return;
    }
    if (m.ask == 6) {
        saveTaps.clear();
        drawClearedSaveChoices(w, h, view);
        return;
    }
    if (m.ask) { // (SetBasicYNChoiceWindow: 0x308 / 0x30b / 0x30d / 0x30e, "{number}" the file's two digits)
        std::string ask = menuAnnounce(m.ask == 1 ? 0x308 : m.ask == 2 ? 0x30b : m.ask == 3 ? 0x30d : 0x30e);
        if (m.ask == 5) ask = "Start New Game Plus in File " + std::to_string(m.askSlot + 1) +
            "?\\nKeep levels, genes, items and money.\\nStory, quests and VS progress restart.";
        char nb[8];
        std::snprintf(nb, sizeof nb, "%02d", m.askSlot + 1);
        if (const size_t at = ask.find("{number}"); at != std::string::npos) ask.replace(at, 8, nb);
        saveTaps.clear();
        drawCr3Asked(ask, m.askSince, vitaButtons() || m.ask == 5 ? m.askFocus : 0, true, kTapSaveYes, kTapSaveNo, w, h, view, &saveTaps);
    }
}

bool Viewer::movieBytes(const std::string& file, std::vector<uint8_t>& out) {
    if (archive.read(file, out)) return true;
    if (!moviesDir.empty()) {
        for (const std::string& f : {moviesDir + "/" + file, moviesDir + "/movie/" + file}) {
            std::FILE* fp = std::fopen(f.c_str(), "rb");
            if (!fp) continue;
            std::fseek(fp, 0, SEEK_END);
            out.resize(size_t(std::max(0L, std::ftell(fp))));
            std::fseek(fp, 0, SEEK_SET);
            const bool ok = std::fread(out.data(), 1, out.size(), fp) == out.size();
            std::fclose(fp);
            if (ok) return true;
        }
    }
    return false;
}

void Viewer::startMovie(const std::string& name) {
    endMovie();
    std::vector<uint8_t> bytes;
    if (!movieBytes(name + ".mp4", bytes)) {
        SDL_Log("movie %s: not in the release (movies/ in the game folder, or CR3_MOVIES, may hold another release's)", name.c_str());
        game.moviePlaying = false;
        return;
    }
    auto mv = std::make_unique<cr3::Movie>();
    std::string err;
    if (!mv->open(std::move(bytes), &err)) {
        SDL_Log("movie %s: %s", name.c_str(), err.c_str());
        game.moviePlaying = false;
        return;
    }
    movie.mv = std::move(mv), movie.name = name, movie.clock = 0, movie.shown = 0, movie.ended = false, movie.skipShown = -10;
    { // (the fan translation's English is in its _jp files)
        std::vector<uint8_t> t;
        if (movieBytes(name + (optionLanguage == 0 || archive.overlayActive() ? "_jp" : "_en") + ".srt", t))
            movie.subs = cr3::parseSrt(std::string(t.begin(), t.end()));
    }
    SDL_Log("movie %s: %d x %d, %d pictures at %.0f a second (%.1f s), sound %s, %zu subtitles", name.c_str(), movie.mv->width(),
            movie.mv->height(), movie.mv->frames(), double(movie.mv->fps()), double(movie.mv->duration()),
            movie.mv->pcm.empty() ? "none" : "on", movie.subs.size());
    if (!movie.mv->pcm.empty()) audio.playMovieSound(std::move(movie.mv->pcm), movie.mv->rate, movie.mv->channels);
}

void Viewer::endMovie() {
    if (!movie.mv) return;
    SDL_Log("movie %s: %s at %.1f s", movie.name.c_str(), movie.ended ? "ended" : "skipped", double(movie.clock));
    audio.stopMovieSound();
    if (movie.tex.id) dev->destroy(movie.tex);
    movie = MovieShow();
    game.moviePlaying = false;
}

void Viewer::stepMovie(float dt) {
    if (!game.movieRequest.empty()) {
        const std::string name = game.movieRequest;
        game.movieRequest.clear();
        startMovie(name);
    }
    if (!movie.mv) return;
    if (!inputScript.empty() && inputScript.front() == "movie:skip" && movie.clock > 1.0f) {
        inputScript.pop_front();
        return endMovie();
    }
    if (movie.paused) return;
    movie.clock += dt;
    // the pictures due by now (the movie's clock; the last one decoded shown)
    bool fresh = false;
    while (float(movie.shown) < movie.clock * movie.mv->fps()) {
        if (!movie.mv->next(movie.rgba)) {
            movie.ended = true;
            return endMovie();
        }
        ++movie.shown, fresh = true;
    }
    if (fresh) {
        cr3::Image img;
        img.width = movie.mv->width(), img.height = movie.mv->height(), img.rgba = movie.rgba;
        cr3::Texture old = movie.tex;
        movie.tex = dev->upload(img);
        if (old.id) dev->destroy(old);
    }
}

bool Viewer::movieTap() {
    if (!movie.mv) return false;
    if (uiClock - movie.skipShown < 3.0f) endMovie();
    else movie.skipShown = uiClock;
    return true;
}

bool Viewer::moviePad(SDL_GamepadButton button) {
    if (!vitaButtons() || !movie.mv) return false;
    if (button == SDL_GAMEPAD_BUTTON_NORTH) {
        movie.paused = !movie.paused;
        audio.pauseMovieSound(movie.paused);
        SDL_Log("movie: %s at %.2f", movie.paused ? "paused" : "resumed", double(movie.clock));
    } else if (button == SDL_GAMEPAD_BUTTON_WEST) endMovie();
    return true; // movie owns the pad; Circle is not a touch on Vita
}

bool Viewer::vistaPad(SDL_GamepadButton button) {
    if (!vitaButtons() || !eventMode || !event) return false;
    if (event->vistaPaused) {
        if (button == SDL_GAMEPAD_BUTTON_NORTH) {
            event->vistaPaused = false;
            audio.pauseSceneSound(false);
            SDL_Log("vista: resumed at frame %d", event->frame());
        }
        return true;
    }
    if (button != SDL_GAMEPAD_BUTTON_NORTH || !event->windowOpen(cr3::EventRuntime::Vista)) return false;
    // FastForwardButton's first hit reveals the controls for three seconds; a later
    // triangle hit while they are shown toggles GameSystem's pause (8125f47e).
    if (event->vistaPauseEnabled || uiClock < vistaControlsUntil) {
        event->vistaPaused = true;
        audio.pauseSceneSound(true);
        SDL_Log("vista: paused at frame %d", event->frame());
    } else {
        vistaControlsUntil = uiClock + 3;
        SDL_Log("vista: controls shown");
    }
    return true;
}

void Viewer::drawMovie(int w, int h) {
    if (!movie.mv) return;
    static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
    const Mat4 id = cr3::mat4Identity();
    static const float kBlack[4] = {0, 0, 0, 1}, kWhite[4] = {1, 1, 1, 1};
    dev->clearDepth();
    fullFrameQuad(white, kScreen, kBlack);
    if (movie.tex.id && movie.mv->height() > 0) { // (letterboxed in the frame: its shape kept)
        const float ma = float(movie.mv->width()) / float(movie.mv->height()), sa = float(frameW) / float(std::max(frameH, 1));
        const float sx = ma < sa ? ma / sa : 1.0f, sy = ma < sa ? 1.0f : sa / ma;
        const float v[24] = {-sx, -sy, 0, 1, sx, -sy, 1, 1, sx, sy, 1, 0, -sx, -sy, 0, 1, sx, sy, 1, 0, -sx, sy, 0, 0};
        fullFrameQuad(movie.tex, v, kWhite);
    }
    for (const cr3::Subtitle& t : movie.subs) {
        if (movie.clock < t.from || movie.clock >= t.to) continue;
        std::vector<std::string> rows;
        for (size_t p = 0, q; p <= t.text.size(); p = q + 1) {
            q = t.text.find('\n', p);
            if (q == std::string::npos) q = t.text.size();
            rows.push_back(t.text.substr(p, q - p));
        }
        for (size_t r = 0; r < rows.size(); ++r)
            drawUiText(rows[r], 26, 0, -0.26f + 0.04f * float(rows.size() - 1 - r), w, h, false);
    }
    if (uiClock - movie.skipShown < 3.0f) drawUiText("Skip", 28, 0.40f, 0.27f, w, h, false);
}

void Viewer::drawStaffroll(const cr3::EventRuntime& ev, int w, int h) {
    const auto& sr = ev.staffroll();
    if (!sr.open) return;
    if (sr.backAlpha > 0 && white.id) {
        static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
        const float rgba[4] = {0, 0, 0, std::min(sr.backAlpha, 1.0f)};
        fullFrameQuad(white, kScreen, rgba);
    }
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    if (sr.graphicAlpha > 0 && sr.graphicIndex > 0) {
        char tex[24];
        std::snprintf(tex, sizeof tex, "cutin100_c%02d", sr.graphicIndex);
        UiFig& bg = uiFig("ttlendmenu");
        cr3UiTexture(bg, tex);
        cr3UiPut("ttlendmenu", "", 0, 0, 0, view, sr.graphicAlpha);
    }
    // StaffrollMenu::SetStaffText (0x00b3ed78): shifts credit columns -0.85 UI units on 16:9+ so
    // they clear the right-hand 3D sub-viewport [0.52, 0.27, 0.46, 0.46].
    const float xShift = float(w) / float(std::max(h, 1)) >= 1.5f ? -0.85f : 0.0f;
    const float leftX = sr.leftX + xShift, rightX = sr.rightX + xShift;
    const float fontSz = float(std::max(sr.fontSize, 8));
    const float shade = std::clamp((sr.textRgb[0] + sr.textRgb[1] + sr.textRgb[2]) / 3.0f, 0.0f, 1.0f);
    const float border = sr.shadowMode == 2 ? 0.0012f : 0.0f;
    for (const auto& ln : sr.lines) {
        if (ln.y < -4.8f || ln.y > 4.8f) continue;
        if (!ln.left.empty() && ln.left != " ") {
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            const float tw = uiTextWidth(ln.left, fontSz, w, h);
            if (sr.shadowMode == 1) drawUiText(ln.left, fontSz, leftX / 10.0f - tw + 0.001f, ln.y / 10.0f - 0.0017f, w, h, true, 0.0f);
            drawUiText(ln.left, fontSz, leftX / 10.0f - tw, ln.y / 10.0f, w, h, true, shade, 0, border);
        }
        if (ln.logoFrame >= 0) {
            cr3UiPut("ttlendrogo_fr00", "", float(ln.logoFrame) / 30.0f, rightX, ln.y, view);
        } else if (!ln.right.empty() && ln.right != " ") {
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            if (sr.shadowMode == 1) drawUiText(ln.right, fontSz, rightX / 10.0f + 0.001f, ln.y / 10.0f - 0.0017f, w, h, true, 0.0f);
            drawUiText(ln.right, fontSz, rightX / 10.0f, ln.y / 10.0f, w, h, true, shade, 0, border);
        }
    }
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
}

void Viewer::drawEventUi() {
    dev->clearDepth(); // (the scene's depths behind all of it: a near camera's ground too)
    if (credits.stage) return drawCredits();
    if (cr3Title.stage) return drawCr3TitleUi();
    if (titleMenu.stage) drawTitle();
    else drawIntro(screenW, screenH);
    drawBalloons(screenW, screenH);
    drawKeyMark(screenW, screenH);
    drawFieldUi(screenW, screenH);
    drawBattleHud(screenW, screenH);
    drawVistaSlash();
    if (battleMode && white.id) { // (a special scene's: Cr3Fade over the battle)
        const float alpha = battleFade.alpha.at(battleClock);
        if (alpha > 0) {
            static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
            const Mat4 id = cr3::mat4Identity();
            const float* c = fadeColours[std::clamp(battleFade.colour, 0, 3)];
            const float rgba[4] = {c[0], c[1], c[2], alpha};
            fullFrameQuad(white, kScreen, rgba);
        }
    }
    if (cr3BattleMode && white.id) { // (a Last Attack's white: Cr3FadeOut2 / Cr3FadeIn2 colour 1 over the battle)
        const float alpha = cr3White.at(cr3BattleClock);
        if (alpha > 0) {
            static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
            const float rgba[4] = {1, 1, 1, alpha};
            fullFrameQuad(white, kScreen, rgba);
        }
    }
    drawCr3BattleHud(screenW, screenH);
    if (cr3BattleMode && cr3BattleStage == Cr3BattleStage::Action && cr3ActionPlay.spellCardAge >= 0)
        drawCr3SpellCards(screenW, screenH, cr3ActionPlay.spellCardAge, cr3ActionPlay.spellCards);
    drawCr3OptionsOver(screenW, screenH); // (the Options over a battle)
    if (cr3BattleMode && cr3Ctn.stage != 0) drawCr3Continue(screenW, screenH);
    if (cr3BattleMode && cr3GameOverAt >= 0) drawCr3GameOver(screenW, screenH);
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || !eventDrawingReady) return;
    if (vitaButtons() && eventMode && (ev->vistaPaused || (ev->windowOpen(cr3::EventRuntime::Vista) && (ev->vistaPauseEnabled || uiClock < vistaControlsUntil))))
        drawUiText(ev->vistaPaused ? "Paused" : "Triangle: Pause", 22, 0.33f, 0.27f, screenW, screenH, false);
    struct YesNoLast { // (the buttons over everything else the event shows)
        Viewer* v; cr3::EventRuntime* ev;
        ~YesNoLast() { v->dev->clearDepth(), v->drawClearQuest(*ev, v->screenW, v->screenH), v->drawLevelUp(*ev, v->screenW, v->screenH), v->drawKeyBuy(v->screenW, v->screenH), v->drawYesNo(*ev, v->screenW, v->screenH), v->drawChoice(*ev, v->screenW, v->screenH), v->drawInputDialog(v->screenW, v->screenH); }
    } yesNoLast{this, ev};
    if (ev->effectLine) { // (BattleEffectTypeBMenu: radiate_loop, scaled aspect / 1.5 - or 1.5 / aspect - across)
        Mat4 camera = cr3::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr3::mat4Multiply(uiProjection(screenW, screenH), camera);
        UiFig& rl = uiFig("radiate_loop");
        const float aspect = float(screenW) / float(std::max(screenH, 1));
        const float k = aspect > 1.5f ? aspect / 1.5f : 1.5f / aspect;
        rl.fig.place = cr3::Mat34();
        rl.fig.place.m[0] = rl.fig.place.m[5] = k;
        poseUi(rl, "radiate_loop", uiClock, true);
        drawFigure(rl.fig, view.data());
    }
    auto drawEventFade = [&](const cr3::EventFade& fd) {
        const float alpha = fd.at(eventLead() / cr3::EventRuntime::kTick);
        if (alpha > 0 && white.id) {
            static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
            const Mat4 id = cr3::mat4Identity();
            const float* c = fadeColours[std::clamp(fd.colour, 0, 3)];
            const float rgba[4] = {c[0], c[1], c[2], alpha};
            fullFrameQuad(white, kScreen, rgba);
        }
    };
    if (ev == vsEvents.get()) { // (the VS menu under its script's windows, OverLayMain's mask between)
        drawVs(screenW, screenH);
        if (vsOverlay && white.id) {
            static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
            const float rgba[4] = {0, 0, 0, 0.7f};
            fullFrameQuad(white, kScreen, rgba);
        }
        dev->clearDepth();
    }
    drawEventFade(ev->backFade()); // (BackFadeWindowMenu: passes 0xd..0xf, behind cut-ins and menus)
    const bool message = ev->messageShown() && ev->page() < int(ev->message().pages.size());
    const bool onVista = message && ev->messageWindow() == cr3::EventRuntime::Vista;
    if (ev->windowOpen(cr3::EventRuntime::Vista)) {
        drawWindow(vistaWindow, onVista, screenW, screenH);
        // (CR3: the page cursor once the page is typed, fading in)
        const bool typed = onVista && ev->awaitingTap() && !ev->windowClosing(cr3::EventRuntime::Vista);
        if (!typed) vistaTypedFrame = -1;
        else if (vistaTypedFrame < 0) vistaTypedFrame = ev->frame();
        if (typed) {
            const float age = float(ev->frame() - vistaTypedFrame) * cr3::EventRuntime::kTick + eventLead();
            drawCr3Part(vistaPageCursor, screenW, screenH, age, age * 6.0f);
        }
    }
    if (!vistaName.fig.meshes.empty()) drawWindow(vistaName, onVista, screenW, screenH);
    drawCr3GeneLab(*ev, screenW, screenH, false); // (its backdrop behind the clerk)
    drawCr3Shop(*ev, screenW, screenH, false);    // (the shop's likewise)
    if (cr3CutInsLoaded) drawCr3CutIns(screenW, screenH);
    else drawCutIns(screenW, screenH);
    drawCr3GeneLab(*ev, screenW, screenH, true);
    drawCr3SpacePort(*ev, screenW, screenH);
    drawCr3Shop(*ev, screenW, screenH, true);
    if (ev == event.get()) drawLunchBalloons(screenW, screenH); // (a lunch's)
    drawCr3WorldSelect(*ev, screenW, screenH);
    if (ev == areaEvents.get() || (cr3CampEvent && ev == event.get())) drawCr3Field(screenW, screenH); // (the field's buttons, the camp - a vista's too)
    if (ev == areaEvents.get()) drawCr3TownMove(screenW, screenH); // (the town map)
    if (ev == areaEvents.get()) drawAreaDrag(screenW, screenH);  // (the drag's controller)
    if (ev == areaEvents.get()) drawVs(screenW, screenH);      // (the VS mode's menus)
    drawCr3ScriptCollect(screenW, screenH);                     // (a script's collection)
    drawCr3ScriptGenes(screenW, screenH);                       // (an event's gene status, its gene disposal)
    drawGeneSelector(screenW, screenH);                         // (the VS tutorial's gene cards)
    drawCr3GeneGet(screenW, screenH);                           // (over them all)
    if (ev->windowOpen(cr3::EventRuntime::CutIn) || ev->windowClosing(cr3::EventRuntime::CutIn)) {
        const bool onConversation = message && ev->messageWindow() == cr3::EventRuntime::CutIn;
        drawWindow(conversationWindow, onConversation, screenW, screenH);
        if (onConversation && !ev->windowClosing(cr3::EventRuntime::CutIn)) drawCr3Part(messageButton, screenW, screenH, 0, 1);
    }
    if (ev->mapName() >= 0) drawMapName(*ev, screenW, screenH); // (CR3's map name)
    if (ev->staffroll().open) drawStaffroll(*ev, screenW, screenH); // (CR3's StaffrollMenu)
    if (ev->tutorialMenuOpen() && !tutorialMenuWindow.fig.meshes.empty()) { // (CR3's tutorial menu)
        drawWindow(tutorialMenuWindow, message && ev->messageWindow() == cr3::EventRuntime::TutorialMenu, screenW, screenH);
        if (const int id = ev->tutorialMenuPicture(); id > 0 && tutorialMenuPicture.loaded) {
            char tex[16];
            std::snprintf(tex, sizeof tex, "tuto_ss%03d", (id - 1) / 8 + 1);
            cr3::Texture* t = texture(tex);
            for (ViewMesh& vm : tutorialMenuPicture.fig.meshes) vm.texture = t ? t : texture(vm.baseMaterial.diffuse());
            drawCr3Part(tutorialMenuPicture, screenW, screenH, float((id - 1) % 8) / 30.0f, 1); // (ChangeAnimeTime: the frame)
        }
    }
    auto drawNarration = [&]() {
        if (ev->windowOpen(cr3::EventRuntime::Narration) || ev->windowClosing(cr3::EventRuntime::Narration)) {
            const bool onNarration = message && ev->messageWindow() == cr3::EventRuntime::Narration;
            drawWindow(narrationWindow, onNarration, screenW, screenH);
            const bool typed = onNarration && ev->awaitingTap() && !ev->windowClosing(cr3::EventRuntime::Narration);
            if (!typed) narrationTypedFrame = -1;
            else if (narrationTypedFrame < 0) narrationTypedFrame = ev->frame();
            if (typed) {
                const float age = float(ev->frame() - narrationTypedFrame) * cr3::EventRuntime::kTick + eventLead();
                drawCr3Part(vistaPageCursor, screenW, screenH, age, age * 6.0f);
            }
        } else {
            narrationTypedFrame = -1;
        }
    };
    if (!ev->narrationAboveFade()) drawNarration();
    dev->clearDepth();
    drawEventFade(ev->fade()); // (FadeWindowMenu: passes 0x13..0x15, over Vista/CutIn/menus; OnFadeNarretionMenu and SysWindow render over it)
    if (ev->narrationAboveFade()) drawNarration();
    // the tutorials': the picture (tutorialPic, its PRG_rep the image shown), the windows over it
    const cr3::EventRuntime::TutorialPicture& tp = ev->tutorialPicture();
    if (tp.visible && tp.shown >= 0 && tp.shown < int(tp.images.size())) {
        UiFig& pic = uiFig("tutorialpic");
        pic.fig.place = cr3::Mat34();
        poseUi(pic, "tutorialpic", 0);
        if (cr3::Texture* tex = texture(tp.images[size_t(tp.shown)]))
            for (ViewMesh& vm : pic.fig.meshes) vm.texture = tex;
        Mat4 camera = cr3::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr3::mat4Multiply(uiProjection(screenW, screenH), camera);
        drawFigure(pic.fig, view.data());
        dev->clearDepth();
    }
    bool anyAnnounceCursor = false;
    for (WindowLayout* wl : {&annoWindow, &announceWindow, &helpWindow, &announceTopWindow, &announceMidWindow, &announceBotWindow, &systemWindow})
        if (ev->windowOpen(wl->window) || ev->windowClosing(wl->window)) {
            const bool onThis = message && ev->messageWindow() == wl->window;
            drawWindow(*wl, onThis, screenW, screenH);
            if (wl != &systemWindow && onThis && ev->awaitingTap() && !ev->windowClosing(wl->window) && announcePageCursor.loaded) {
                anyAnnounceCursor = true;
                if (announceTypedFrame < 0) announceTypedFrame = ev->frame();
                const float age = float(ev->frame() - announceTypedFrame) * cr3::EventRuntime::kTick + eventLead();
                const int cfit = wl->fitOverride < 0 ? -1 : wl->fitOverride == 2 ? 3 : 0; // (fit9 top-right, -1 mid-right, fit3 bot-right)
                drawCr3Part(announcePageCursor, screenW, screenH, age, age * 6.0f, wl->positionY, cfit);
            }
        }
    if (!anyAnnounceCursor) announceTypedFrame = -1;
    // the fast-forward button (CR3: comMsgFBMenu at comMsgFBPosition's call_comMsgFBMenu_fit6;
    // CR2: speedUp, its loop, at its own place - over the message's window while allowed)
    fastRect.id = 0;
    if ((message && ev->fastForwardShown() && ev->windowOpen(ev->messageWindow())) || ev->skipButtonShown()) {
        if (vistaFastButton.loaded) {
            drawCr3Part(vistaFastButton, screenW, screenH, 0, 1);
            float sx, sy, ex, ey;
            const std::vector<cr3::Mat34> fw = vistaFastButton.fig.skeleton.world(
                vistaFastButton.fig.posed.empty() ? vistaFastButton.fig.skeleton.basePose : vistaFastButton.fig.posed, vistaFastButton.fig.place);
            std::vector<uint8_t> nd;
            const std::vector<std::string> jn = archive.read("commsgfbmenu.name", nd) ? cr3::parseJointNames(nd) : std::vector<std::string>();
            bool haveS = false, haveE = false;
            for (size_t j = 0; j < jn.size() && j < fw.size(); ++j) {
                if (jn[j] == "tapAreaS02") sx = fw[j].m[3] / 10.0f, sy = fw[j].m[7] / 10.0f, haveS = true;
                else if (jn[j] == "tapAreaE02") ex = fw[j].m[3] / 10.0f, ey = fw[j].m[7] / 10.0f, haveE = true;
            }
            if (haveS && haveE) fastRect.id = 1, fastRect.x0 = sx, fastRect.y0 = sy, fastRect.x1 = ex, fastRect.y1 = ey;
        } else {
            Mat4 camera = cr3::mat4Identity();
            camera[14] = -0.881f;
            const Mat4 view = cr3::mat4Multiply(uiProjection(screenW, screenH), camera);
            UiFig& ff = uiFig("speedup");
            ff.fig.place = cr3::Mat34();
            poseUi(ff, "speedup", uiClock, true);
            drawFigure(ff.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(ff, "tapAreaS01", sx, sy) && uiJoint(ff, "tapAreaE01", ex, ey)) fastRect.id = 1, fastRect.x0 = sx, fastRect.y0 = sy, fastRect.x1 = ex, fastRect.y1 = ey;
        }
    }
    if ((ev->windowOpen(7) || ev->windowClosing(7)) && itemWindow.fig.meshes.empty()) drawCr3GetItem(*ev, message && ev->messageWindow() == 7);
    else if (ev->windowOpen(7) || ev->windowClosing(7)) {
        drawWindow(itemWindow, message && ev->messageWindow() == 7, screenW, screenH);
        const cr3::BattleData::Item* it = ev->windowOpen(7) && ev->itemShown() > 0 ? battleData.item(ev->itemShown()) : nullptr;
        float x, y;
        if (it && it->icon > 0 && windowJoint(itemWindow, "call_mainIcon_id00", x, y)) { // (GetItemWindowMenu::SetItemIcon)
            Mat4 camera = cr3::mat4Identity();
            camera[14] = -0.881f;
            const Mat4 view = cr3::mat4Multiply(uiProjection(screenW, screenH), camera);
            UiFig& ic = uiFig("mainicon_id");
            ic.fig.place = cr3::Mat34();
            // (left of the centred text: the joint less half its width, less 0.032)
            const float textW = !textQuads.empty() && shownText.compare(0, 2, "7\n") == 0 ? textQuads[0].w : 0;
            ic.fig.place.m[3] = x - (textW / 2 + 0.032f), ic.fig.place.m[7] = y;
            poseUi(ic, "mainicon_id", it->icon == 0x1c ? 1.45f : it->icon == 0x1d ? 1.4f : float(it->icon) / 20.0f);
            drawFigure(ic.fig, view.data());
        }
    }
}

} // namespace cr3_viewer
