#include "viewer/viewer.hpp"

namespace cr3_viewer {

void Viewer::touchEvents(const cr3::Vec3& p) {
    if (!script) return;
    std::set<std::string> now;
    for (const cr3::Gimmick& g : field.gimmicks()) {
        if (g.kind != cr3::Gimmick::Event || g.check) continue;
        const float at[3] = {p[0], p[1], p[2]};
        if (!g.touches(at, controller->radius)) continue;
        now.insert(g.name);
        const auto st = script->gimmicks().find(g.id);
        const bool enabled = st == script->gimmicks().end() || st->second.event;
        if (insideEvents.count(g.name) || !enabled || script->running()) continue;
        SDL_Log("%s: touched %s - %s", field.info().name.c_str(), g.name.c_str(), g.function.c_str());
        if (!script->call(g.function)) SDL_Log("%s", script->error().c_str());
        else if (!script->running() && playMode) pendingBranch = true; // (acted on at the next update: not mid-walk)
    }
    insideEvents.swap(now);
}

// an event's scene putting the leader at its p000 (FldUtilSetFieldMapPlayerPos / Rot)
void Viewer::placePlayer(const cr3::FieldScript::Placement& pl) {
    if (pl.heading) heading = pl.yaw;
    if (!pl.position || !controller) return;
    controller->setPosition({pl.at[0], pl.at[1] + 0.17f, pl.at[2]}); // (dropped onto the floor, as on arriving)
    for (int k = 0; k < 10; ++k) controller->update({0, 0, 0});
    const cr3::Vec3& p = controller->position();
    player[0] = p[0], player[1] = p[1] - 0.02f, player[2] = p[2];
    std::memcpy(playerPrev, player, sizeof player);
    std::memcpy(playerCur, player, sizeof player);
    stepClock = 0, substep = 0;
}

uint32_t Viewer::boxRandom() { // (lrand48's part: a simple generator of the viewer's own)
    static uint32_t x = 0x2545f491u;
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    return x;
}

void Viewer::checkBalloons() {
    balloons.clear(), nearestBalloon = -1;
    if (!script || script->running()) return;
    float best = 30000;
    const std::vector<cr3::Gimmick>& gs = field.gimmicks();
    for (size_t i = 0; i < gs.size(); ++i) {
        const cr3::Gimmick& g = gs[i];
        const float reach = g.checkLength();
        if (reach <= 0 || !script->visible(g)) continue;
        if (g.kind == cr3::Gimmick::Box && game.boxes.isOpen(g.boxKind, g.boxNumber)) continue; // (an opened box: none)
        const auto st = script->gimmicks().find(g.id);
        if (st != script->gimmicks().end() && !st->second.event) continue;
        const float d = std::hypot(g.position[0] - playerCur[0], g.position[1] - playerCur[1], g.position[2] - playerCur[2]); // (as of the update)
        if (d > reach) continue;
        balloons.push_back(i);
        if (d < best) best = d, nearestBalloon = int(i);
    }
}

bool Viewer::areaDragFree() const { return areaMode && !eventMode && !(areaEvents && areaEvents->running()) && cr3Camp.stage == 0; }

void Viewer::areaDragStart(float fx, float fy, bool own, SDL_FingerID finger) {
    areaDrag = Cr3AreaDrag(), areaDrag.on = true, areaDrag.x0 = areaDrag.x = fx, areaDrag.y0 = areaDrag.y = fy;
    areaDrag.own = own, areaDrag.finger = finger;
}

void Viewer::areaDragMove(float fx, float fy) {
    if (!areaDrag.on) return;
    areaDrag.x = fx, areaDrag.y = fy;
    if (std::hypot(fx - areaDrag.x0, fy - areaDrag.y0) >= 0.02f) areaDrag.moved = true;
}

void Viewer::areaDragEnd() {
    if (!areaDrag.on) return;
    const bool tapped = !areaDrag.moved;
    areaDrag.on = false;
    if (tapped) tap(); // (a touch without a move: the field's tap)
}

bool Viewer::areaCamFree() const { return areaDragFree() && areaCamera.turnable(); }

bool Viewer::cameraHalf(float fx, float fy) const { return campOptions.hands == 1 ? fy < 0.5f : fx > 0.5f; }

void Viewer::uiUnits(float fx, float fy, float& ux, float& uy) const { // (the UI's units: 100 pixels of the screen's 960 x 640, y up)
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
}

void Viewer::areaCamStart(float fx, float fy, bool mouse, bool own, SDL_FingerID finger) {
    areaCam = Cr3AreaCam(), areaCam.on = true, areaCam.mouse = mouse, areaCam.own = own, areaCam.finger = finger;
    areaCam.x0 = areaCam.x = fx, areaCam.y0 = areaCam.y = fy;
}

void Viewer::areaCamMove(float fx, float fy) {
    if (!areaCam.on) return;
    areaCam.x = fx, areaCam.y = fy;
    float ax, ay, bx, by;
    uiUnits(areaCam.x0, areaCam.y0, ax, ay), uiUnits(fx, fy, bx, by);
    if (std::hypot(bx - ax, by - ay) * 100 > 20) areaCam.moved = true; // (Fld2TaskTouch: over 400 squared)
}

void Viewer::areaCamEnd() {
    if (!areaCam.on) return;
    const bool tapped = !areaCam.moved && !areaCam.mouse;
    areaCam.on = false;
    if (tapped) tap();
}

void Viewer::areaCamStick(float rx, float ryDown, float dt) {
    if (areaCamSticking && rx == 0 && ryDown == 0)
        areaCamSticking = false, SDL_Log("camera: the right stick let go - yaw %.2f, pitch %.2f", areaCamera.yaw, areaCamera.pitch);
    if (!vitaButtons() || areaCam.on || !areaMode || !areaCamFree() || (rx == 0 && ryDown == 0)) return;
    areaCamSticking = true;
    auto rate = [](float d) { const float a = std::fabs(d); return a < 20 ? 0.0f : std::copysign(a < 60 ? 75.0f : 250.0f, d); };
    areaCamera.drag(rate(-rx * 120) * dt * 60 * 0.0001f, rate(ryDown * 120) * dt * 60 * 0.0001f);
}

void Viewer::areaCamStep(float dt) {
    if (!areaCam.on || !areaCamFree()) return;
    float ax, ay, bx, by;
    uiUnits(areaCam.x0, areaCam.y0, ax, ay), uiUnits(areaCam.x, areaCam.y, bx, by);
    auto rate = [](float d) { const float a = std::fabs(d); return a < 20 ? 0.0f : std::copysign(a < 60 ? 75.0f : 250.0f, d); };
    // (GetTouchDiff: where it pressed less where it is; yaw += x x -0.0001 x -1, pitch += y x -1 x -0.0001)
    areaCamera.drag(rate((ax - bx) * 100) * dt * 60 * 0.0001f, rate((ay - by) * 100) * dt * 60 * 0.0001f);
}

void Viewer::fingerEvent(Uint32 type, SDL_FingerID id, float fx, float fy) {
    const bool mine = mouseFingerOn && id == mouseFinger;
    if (type == SDL_EVENT_FINGER_DOWN) {
        if (!mouseFingerOn) return void((mouseFingerOn = true, mouseFinger = id)); // (its mouse button's: pointerDown)
        if (!areaDragFree()) return;
        if (areaCamFree() && cameraHalf(fx, fy)) {
            if (!areaCam.on) areaCamStart(fx, fy, false, true, id);
        } else if (!areaDrag.on) areaDragStart(fx, fy, true, id);
    } else if (type == SDL_EVENT_FINGER_MOTION) {
        if (mine) return;
        if (areaCam.on && areaCam.own && areaCam.finger == id) areaCamMove(fx, fy);
        if (areaDrag.on && areaDrag.own && areaDrag.finger == id) areaDragMove(fx, fy);
    } else if (type == SDL_EVENT_FINGER_UP || type == SDL_EVENT_FINGER_CANCELED) {
        if (mine) return void(mouseFingerOn = false);
        const bool up = type == SDL_EVENT_FINGER_UP;
        if (areaCam.on && areaCam.own && areaCam.finger == id) up ? areaCamEnd() : void(areaCam = Cr3AreaCam());
        if (areaDrag.on && areaDrag.own && areaDrag.finger == id) up ? areaDragEnd() : void(areaDrag = Cr3AreaDrag());
    }
}

// (its push as the stick's: sx across, sy away from the camera - the finger up the screen)
bool Viewer::areaDragStick(float& sx, float& sy, bool& run) const {
    if (!areaDrag.on || !areaDragFree()) return false;
    const float dx = areaDrag.x - areaDrag.x0, dy = areaDrag.y - areaDrag.y0, m = std::hypot(dx, dy);
    if (m < 0.02f) sx = sy = 0;
    else sx = dx / m, sy = -dy / m;
    run = m >= 0.05f;
    return true;
}

// A tap (Return, a click): the vista's page; the field's - its event's page, else the nearest
// balloon's function (FldMain::CheckTouchEventExec_Tap, EventExecute), the leader turned to it
void Viewer::tap() {
    if (eventMode && event && event->vistaPaused) return;
    if (movieTap()) return;
    if (cr3BattleMode) return cr3BattleTap();
    if (areaMode && !eventMode && areaEvents) { // (CR3's field: the script's page, else a check object's function)
        if (areaEvents->running()) areaEvents->touch();
        else if (areaCheckNear >= 0) {
            const AreaGimmick& g = areaGimmicks[size_t(areaCheckNear)];
            if (g.box >= 0) game.cr3OpenBox = g.box; // (Fld2Box::ExecOpen: Fld2TBoxList::SetOpenBox)
            runAreaFunction(g.function);
        }
        return;
    }
    if (cr3Title.stage) {
        cr3TitleTap();
        return;
    }
    if (titleMenu.stage) return titleTap();
    if (vslash.stage) return; // (the slash: a swipe, not a tap)
    if (battleMode) {
        if (skipScene()) return;
        if (battleScript && battleScript->running()) battleScript->events().advance();
        else if (analyze.stage == 1 || analyze.stage == 2) analyze.tapped = true;
        else if (result.stage > 0) result.tapped = true;
        else if (gameOver.stage > 0) gameOver.tapped = true;
        return;
    }
    if (eventMode) {
        if (event) event->touch();
        return;
    }
    if (!fieldMode || !script) return;
    if (script->running()) {
        script->events().touch();
        return;
    }
    if (const int s = sprintAt(); s >= 0) return startSprint(s); // (FldMain: a sprint's before the balloons)
    if (actionMark.gimmick >= 0 && actionMark.startedAt < 0) return actionTapped(); // (then a touch action's)
    if (nearestBalloon < 0 || keyLock.stage != 0) return;
    const cr3::Gimmick& g = field.gimmicks()[size_t(nearestBalloon)];
    heading = std::atan2(g.position[0] - player[0], g.position[2] - player[2]);
    SDL_Log("%s: tapped %s - %s", field.info().name.c_str(), g.name.c_str(), g.function.c_str());
    std::string function = g.function;
    if (g.kind == cr3::Gimmick::Box) { // (FldMain's balloon of a box: opened, then its function - box_msg)
        if (g.boxKind == 1 && script->leader() != 1) function = "Message_obox"; // (a key box: only Orlando opens it)
        else if (g.boxKind == 1) return startUnlock(nearestBalloon); // (FldUnlock first)
        else openBox(g);
    }
    callGimmick(function);
}

void Viewer::callGimmick(const std::string& function) {
    if (!script->call(function)) SDL_Log("%s", script->error().c_str());
    else if (!script->running() && playMode) pendingBranch = true;
    checkBalloons();
}

// FldBox::Open: the item one more (Cr3UtilGetItem), the box opening (fa01, then fn02), SE 0x153
void Viewer::openBox(const cr3::Gimmick& g) {
    const int item = game.boxes.item(g.boxKind, g.boxNumber, game.party);
    game.boxes.markOpen(g.boxKind, g.boxNumber);
    if (item > 0 && item < 300) game.itemCount[item] = static_cast<short>(std::min(game.itemCount[item] + 1, 99));
    audio.playSe(0x153, false);
    boxOpening[g.name] = uiClock;
    script->boxItem = item;
    SDL_Log("%s: box %s - item %d (%d now)", field.info().name.c_str(), g.id.c_str(), item, item > 0 && item < 300 ? game.itemCount[item] : 0);
}

void Viewer::startUnlock(int gimmick) {
    keyLock.gimmick = gimmick, keyLock.orlando = true;
    keyLock.stage = 1, keyLock.stageAt = uiClock, keyLock.spread = 0, keyLock.pressing = false;
    SDL_Log("%s: the lock of %s", field.info().name.c_str(), field.gimmicks()[size_t(gimmick)].id.c_str());
}

void Viewer::unlockEnd(bool success) {
    if (keyLock.stage != 1) return;
    keyLock.pressing = false;
    if (success) {
        audio.playSe(0x164, false);
        keyLock.stage = 2, keyLock.stageAt = uiClock, keyLock.countdown = 20;
    } else {
        audio.playSe(0x165, false);
        keyLock.stage = 0, keyLock.markAt = uiClock, keyLock.spread = 0;
    }
    SDL_Log("%s: unlock %s", field.info().name.c_str(), success ? "succeeded" : "failed");
}

void Viewer::updateKeyLock() {
    KeyLock& k = keyLock;
    if (k.stage == 2) {
        if (--k.countdown > 0) return;
        k.stage = 0;
        if (k.gimmick < 0 || size_t(k.gimmick) >= field.gimmicks().size()) return;
        const cr3::Gimmick& g = field.gimmicks()[size_t(k.gimmick)];
        k.gimmick = -1;
        openBox(g);
        audio.playSe(0x154, false);
        callGimmick(g.function);
        return;
    }
    if (k.stage == 1) return;
    int at = -1; // (the key box the leader is at)
    if (!script->running())
        for (size_t i : balloons) {
            const cr3::Gimmick& g = field.gimmicks()[i];
            if (g.kind == cr3::Gimmick::Box && g.boxKind == 1) at = int(i);
        }
    if (at != k.gimmick) k.gimmick = at, k.markAt = uiClock;
    k.orlando = script->leader() == 1;
}

// the mark's place in the interface's units (the box's, 0.125 above, through the field's camera)
bool Viewer::keyMarkPlace(float& x, float& y) {
    if (keyLock.gimmick < 0 || size_t(keyLock.gimmick) >= field.gimmicks().size()) return false;
    const cr3::Gimmick& g = field.gimmicks()[size_t(keyLock.gimmick)];
    const float p[3] = {g.position[0], g.position[1] + 0.125f, g.position[2]};
    return uiPlace(p, x, y);
}

// a field point's place in the interface's units (Cr3UtilSetBalloonPos, / 2048)
bool Viewer::uiPlace(const float p[3], float& x, float& y) {
    const float* m = fieldScene.data();
    const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
    if (cw <= 0) return false;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    x = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw * float(frameW) * 0.5f / k;
    y = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw * float(frameH) * 0.5f / k;
    return true;
}

// the mouse on the lock (fx, fy: the screen's fractions): pressed, dragged, let go
void Viewer::unlockPointer(float fx, float fy, int phase) {
    if (keyLock.stage != 1) return;
    if (phase == 2) {
        if (keyLock.pressing) unlockEnd(false);
        return;
    }
    float mx, my;
    if (!keyMarkPlace(mx, my)) return;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    const float d = std::hypot(x - mx, y - my) * 2048.0f;
    if (phase == 0) {
        if (d <= kUnlockReach) keyLock.pressing = true, keyLock.spread = 2 * d;
        else unlockEnd(false);
    } else if (keyLock.pressing) {
        keyLock.spread = 2 * d;
        if (keyLock.spread >= kUnlockSpread) unlockEnd(true);
    }
}

int Viewer::actionKind(const std::string& n) {
    return n.find("hjump") != std::string::npos ? 1 : n.find("mystic") != std::string::npos ? 3 : n.find("iai") != std::string::npos ? 4
         : n.find("aim") != std::string::npos ? 5 : n.find("marts") != std::string::npos ? 6 : 0;
}

int Viewer::actionCharacter(int kind) { return kind == 1 ? 0 : kind >= 3 && kind <= 6 ? kind - 1 : -1; }

bool Viewer::actionRunning() const { return sprint.stage != 0 || iai.stage != 0 || aim.stage != 0 || hakkei.stage != 0 || mystic.stage != 0; }

void Viewer::updateActionMark() {
    if (actionRunning()) return;
    if (actionMark.startedAt >= 0 && (actionMark.endedAt < 0 || uiClock - actionMark.endedAt > 0.5f)) actionMark = ActionMark();
    if (actionMark.endedAt >= 0) return;
    int at = -1;
    if (controller && fieldTabShown()) {
        const std::vector<cr3::Gimmick>& gs = field.gimmicks();
        for (size_t i = 0; i < gs.size() && at < 0; ++i) {
            const cr3::Gimmick& g = gs[i];
            if (g.kind != cr3::Gimmick::Ability || actionKind(g.name) == 0 || !script->visible(g)) continue;
            const cr3::FieldScript::TouchAction* a = script->touchAction(g.name);
            if (a && a->enabled && !a->spots.empty() && g.touches(controller->position().data(), controller->radius)) at = int(i);
        }
    }
    if (at != actionMark.gimmick) {
        actionMark = ActionMark(), actionMark.gimmick = at, actionMark.at = uiClock;
        if (at >= 0) actionMark.kind = actionKind(field.gimmicks()[size_t(at)].name);
    }
    actionMark.able = actionMark.kind && script->leader() == actionCharacter(actionMark.kind);
}

bool Viewer::actionMarkPlace(float& x, float& y) {
    if (actionMark.gimmick < 0) return false;
    const cr3::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name);
    if (!a || a->spots.empty()) return false;
    return uiPlace(a->spots[a->markSpot()].data(), x, y);
}

void Viewer::actionTapped() {
    if (actionMark.gimmick < 0 || actionMark.startedAt >= 0) return;
    if (!actionMark.able) {
        audio.playSe(0xb6, false);
        return;
    }
    const cr3::Gimmick& g = field.gimmicks()[size_t(actionMark.gimmick)];
    const std::vector<std::array<float, 3>>& spots = script->touchAction(g.name)->spots;
    const size_t ms = script->touchAction(g.name)->markSpot();
    heading = std::atan2(spots[ms][0] - player[0], spots[ms][2] - player[2]); // (FldPlayer::SetDirection to its mark)
    if (actionMark.kind == 1) {
        sprint = Sprint();
        sprint.to = spots;
        sprint.stage = 1, sprint.jump = true;
        playMotion(fig, "fj01"), time = 0;
    } else if (actionMark.kind == 4) {
        iai = Iai();
        iai.stage = 1;
    } else if (actionMark.kind == 5) {
        aim = Aim();
        aim.stage = 1;
    } else if (actionMark.kind == 3) {
        mystic = Mystic();
        mystic.stage = 1;
    } else if (actionMark.kind == 6) {
        hakkei = Hakkei();
        hakkei.stage = 1, hakkei.wanted = std::min(script->touchAction(g.name)->touches, 8);
    } else {
        SDL_Log("%s: %s - its game isn't in yet", field.info().name.c_str(), g.name.c_str());
        return;
    }
    actionMark.startedAt = uiClock;
    SDL_Log("%s: %s started - %zu spots", field.info().name.c_str(), g.name.c_str(), spots.size());
}

bool Viewer::actionTap(float fx, float fy) { // (a click: on the mark)
    float mx, my;
    if (actionMark.gimmick < 0 || actionMark.startedAt >= 0 || !fieldTabShown() || !actionMarkPlace(mx, my)) return false;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    if (std::hypot(x - mx, y - my) * 2048.0f > 128.0f) return false;
    actionTapped();
    return true;
}

void Viewer::iaiEnd(bool success) {
    if (!iai.stage && !aim.stage && !hakkei.stage && !mystic.stage) return;
    const char* what = iai.stage ? "iai" : aim.stage ? "aim" : hakkei.stage ? "hakkei" : "mystic";
    iai = Iai(), aim = Aim(), hakkei = Hakkei(), mystic.stage = 0;
    const std::string function = field.gimmicks()[size_t(actionMark.gimmick)].name;
    SDL_Log("%s: %s %s", field.info().name.c_str(), what, success ? "succeeded" : "failed");
    if (!success) {
        audio.playSe(0x165, false);
        actionMark.startedAt = -1, actionMark.at = uiClock; // (the mark anew)
        return;
    }
    audio.playSe(0x164, false);
    actionMark.endedAt = uiClock;
    callGimmick(function);
}

// the mouse in an iai (fx, fy: the screen's fractions; phase 0 pressed, 1 dragged)
void Viewer::iaiPointer(float fx, float fy, int phase) {
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    if (iai.stage == 1 && phase == 0) {
        iai.stage = 2, iai.x0 = x, iai.y0 = y, iai.pressedAt = uiClock;
        return;
    }
    float mx, my;
    if (iai.stage != 2 || phase != 1 || !actionMarkPlace(mx, my)) return;
    const float dx = x - iai.x0, dy = y - iai.y0, l2 = dx * dx + dy * dy;
    if (l2 <= 0) return;
    const float t = ((mx - iai.x0) * dx + (my - iai.y0) * dy) / l2;
    if (t < 0 || t > 1) return;
    if (std::hypot(iai.x0 + t * dx - mx, iai.y0 + t * dy - my) * 2048.0f < 128.0f) iaiEnd(true);
}

// the leader's place in the interface's units (FldUtilGet2DPos)
bool Viewer::leaderPlace(float& x, float& y) { return uiPlace(player, x, y); }

// the mouse in an aim (phase 0 pressed, 1 dragged, 2 let go)
void Viewer::aimPointer(float fx, float fy, int phase) {
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    float mx, my, lx, ly;
    if (!actionMarkPlace(mx, my) || !leaderPlace(lx, ly)) return;
    if (aim.stage == 1 && phase == 0) {
        if (std::hypot(x - mx, y - my) * 2048.0f <= 128.0f) aim.stage = 2, aim.x = x, aim.y = y;
        else iaiEnd(false);
    } else if (aim.stage == 2 && phase == 1) {
        aim.x = x, aim.y = y;
    } else if (aim.stage == 2 && phase == 2) {
        iaiEnd(std::hypot(x - lx, y - ly) * 2048.0f <= 128.0f);
    }
}

// a hakkei's tap (on the mark, or not)
void Viewer::hakkeiTap(bool onMark) {
    if (!hakkei.stage) return;
    if (!onMark) return iaiEnd(false);
    const int n = int(hakkei.tappedAt.size());
    hakkei.tappedAt.push_back(uiClock);
    playMotion(fig, ("fp0" + std::to_string(2 + std::min(n, 4))).c_str()), time = 0;
    audio.playSe(uiClock * 997.0f - std::floor(uiClock * 997.0f) < 0.5f ? 0x160 : 0x161, false); // (lrand48 % 2)
    if (n + 1 >= hakkei.wanted) iaiEnd(true);
}

void Viewer::hakkeiPointer(float fx, float fy) {
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    float mx, my;
    if (actionMarkPlace(mx, my)) hakkeiTap(std::hypot(x - mx, y - my) * 2048.0f <= 128.0f);
}

// a mystic's tap: on one of its other spots (its number), or not (-1)
void Viewer::mysticTap(int spot) {
    if (!mystic.stage) return;
    const cr3::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name);
    if (spot < 0 || !a || size_t(spot) >= a->spotNames.size()) return iaiEnd(false);
    script->mysticTapSpot = a->spotNames[size_t(spot)];
    mystic.chose = spot, mystic.choseAt = uiClock;
    SDL_Log("%s: the mystic's tap - %s", field.info().name.c_str(), script->mysticTapSpot.c_str());
    iaiEnd(true);
}

void Viewer::mysticPointer(float fx, float fy) {
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    const cr3::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name);
    if (!a) return;
    for (size_t i = 0; i < a->spots.size(); ++i) {
        float sx, sy;
        if (i != a->markSpot() && uiPlace(a->spots[i].data(), sx, sy) && std::hypot(x - sx, y - sy) * 2048.0f <= 128.0f) return mysticTap(int(i));
    }
    mysticTap(-1);
}

void Viewer::updateIai() {
    if (hakkei.stage && !hakkei.tappedAt.empty() && uiClock - hakkei.tappedAt[0] > 60.0f / 60.0f) iaiEnd(false); // (60 frames)
    if (iai.stage == 2 && uiClock - iai.pressedAt > 10.0f / 60.0f) iaiEnd(false); // (10 frames)
}

void Viewer::drawActionMark(const Mat4& view) {
    float x, y;
    if (actionMark.gimmick < 0 || !actionMarkPlace(x, y)) return;
    auto at = [&](UiFig& u) {
        u.fig.place = cr3::Mat34();
        u.fig.place.m[3] = x, u.fig.place.m[7] = y, u.fig.place.m[11] = -0.2f;
        drawFigure(u.fig, view.data());
    };
    auto duration = [&](UiFig& u, const std::string& anim) {
        poseUi(u, anim, 0);
        return u.fig.animLoaded ? u.fig.anim.duration() : 0.0f;
    };
    const float t = uiClock - actionMark.at, ts = uiClock - actionMark.startedAt, te = uiClock - actionMark.endedAt;
    if (!actionMark.able) {
        UiFig& m = uiFig("gmk0rng_io");
        poseUi(m, "gmk0rng_io", t);
        return at(m);
    }
    if (actionMark.kind == 1) {
        UiFig& m = uiFig("gmk1rng");
        if (actionMark.startedAt >= 0) poseUi(m, "gmk1rng_out", ts);
        else poseUi(m, "gmk1rng_io", t);
        return at(m);
    }
    const std::string ring = "gmk" + std::to_string(actionMark.kind) + "rng";
    UiFig& r = uiFig(ring);
    const float io = duration(r, ring + "_io");
    if (actionMark.kind == 5 && aim.stage == 2) { // (dragged)
        const float keepX = x, keepY = y;
        x = aim.x, y = aim.y;
        poseUi(r, ring + "_wait", t - io, true);
        at(r);
        x = keepX, y = keepY;
    } else if (actionMark.endedAt >= 0) poseUi(r, ring + "_out", te);
    else if (t < io) poseUi(r, ring + "_io", t);
    else poseUi(r, ring + "_wait", t - io, true);
    if (!(actionMark.kind == 5 && aim.stage == 2)) at(r);
    if (actionMark.endedAt >= 0 && actionMark.kind != 3) return;
    UiFig& b = uiFig("gmkbtn");
    if (actionMark.startedAt < 0) {
        poseUi(b, "gmkbtn_io", t);
        return at(b);
    }
    const float out = duration(b, "gmkbtn_out");
    if (ts < out) {
        poseUi(b, "gmkbtn_out", ts);
        at(b);
    } else if (actionMark.kind == 4) { // (then the iai's guide, looping)
        UiFig& c = uiFig("gmk4csr_loop");
        poseUi(c, "gmk4csr_loop", ts - out, true);
        at(c);
    } else if (actionMark.kind == 3 && (mystic.stage || mystic.chose >= 0)) { // (the mystic's other spots' rings)
        const cr3::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name);
        UiFig& o = uiFig("gmk3rng");
        for (size_t i = 0; a && i < a->spots.size(); ++i) {
            float sx, sy;
            if (i == a->markSpot() || !uiPlace(a->spots[i].data(), sx, sy)) continue;
            const float to = ts - out, ioO = duration(o, "gmk3rng_io");
            if (!mystic.stage && int(i) != mystic.chose) continue;
            if (!mystic.stage) poseUi(o, "gmk3rng_out", uiClock - mystic.choseAt);
            else if (to < ioO) poseUi(o, "gmk3rng_io", to);
            else poseUi(o, "gmk3rng_wait", to - ioO, true);
            o.fig.place = cr3::Mat34();
            o.fig.place.m[3] = sx, o.fig.place.m[7] = sy, o.fig.place.m[11] = -0.2f;
            drawFigure(o.fig, view.data());
        }
    } else if (actionMark.kind == 6 && hakkei.stage) { // (the hakkei's balls)
        static const char* const kTriangle[3] = {"call_gmk5ball01", "call_gmk5ball02", "call_gmk5ball03"};
        static const char* const kPentagon[5] = {"call_gmk5ball01_cpy", "call_gmk5ball02_cpy", "call_gmk5ball03_cpy", "call_gmk5ball04", "call_gmk5ball05"};
        const int n = hakkei.wanted;
        UiFig& ball = uiFig("gmk5ball");
        const float d = duration(ball, "gmk5ball");
        for (int i = 0; i < n && i < (n < 4 ? 3 : 5); ++i) {
            float bx, by;
            if (!uiJoint(r, n < 4 ? kTriangle[i] : kPentagon[i], bx, by)) continue;
            const bool tapped = size_t(i) < hakkei.tappedAt.size();
            const float bt = tapped ? uiClock - hakkei.tappedAt[size_t(i)] : std::max(d - (ts - out), 0.0f); // (appearing: played back; tapped: on)
            if (tapped && bt >= d) continue;
            poseUi(ball, "gmk5ball", bt);
            ball.fig.place = cr3::Mat34();
            ball.fig.place.m[3] = bx, ball.fig.place.m[7] = by, ball.fig.place.m[11] = -0.2f;
            drawFigure(ball.fig, view.data());
        }
    } else if (actionMark.kind == 5 && aim.stage) { // (the aim's: the mark, the goal, the line)
        float lx, ly;
        const float ta = ts - out;
        UiFig& mk = uiFig("gmk5mark_io");
        poseUi(mk, "gmk5mark_io", ta);
        at(mk);
        if (leaderPlace(lx, ly)) {
            UiFig& g = uiFig("gmk5goal_io");
            poseUi(g, "gmk5goal_io", ta);
            g.fig.place = cr3::Mat34();
            g.fig.place.m[3] = lx, g.fig.place.m[7] = ly, g.fig.place.m[11] = -0.2f;
            drawFigure(g.fig, view.data());
        }
        const float tx = aim.stage == 2 ? aim.x : x, ty = aim.stage == 2 ? aim.y : y;
        const float len = std::hypot(tx - x, ty - y), ang = std::atan2(tx - x, ty - y);
        UiFig& ln = uiFig("gmk5line");
        poseUi(ln, "gmk5line", ta);
        const float c = std::cos(ang), sn = std::sin(ang), sy = std::max(len / 0.112f, 0.1f);
        ln.fig.place = cr3::Mat34(); // (turned to the touch about z, its y scaled)
        ln.fig.place.m[0] = c, ln.fig.place.m[1] = sn * sy, ln.fig.place.m[4] = -sn, ln.fig.place.m[5] = c * sy;
        ln.fig.place.m[3] = x, ln.fig.place.m[7] = y, ln.fig.place.m[11] = -0.2f;
        drawFigure(ln.fig, view.data());
    }
}

int Viewer::sprintAt() const {
    if (!fieldTabShown() || sprint.stage || !script) return -1;
    const std::vector<cr3::Gimmick>& gs = field.gimmicks();
    for (size_t i = 0; i < gs.size(); ++i) {
        const cr3::Gimmick& g = gs[i];
        if (g.kind != cr3::Gimmick::Ability || g.name.find("sprint") == std::string::npos || !script->visible(g)) continue;
        const cr3::FieldScript::TouchAction* a = script->touchAction(g.name);
        if (!a || !a->enabled || a->spots.empty()) continue;
        if (std::hypot(g.position[0] - playerCur[0], g.position[1] - playerCur[1], g.position[2] - playerCur[2]) <= 0.15f) return int(i);
    }
    return -1;
}

bool Viewer::sprintTap(float fx, float fy) { // (a click: on the marker)
    const int i = sprintAt();
    if (i < 0) return false;
    float mx, my;
    if (!uiPlace(field.gimmicks()[size_t(i)].position, mx, my)) return false;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    if (std::hypot(x - mx, y - my) * 2048.0f > 128.0f) return false;
    startSprint(i);
    return true;
}

void Viewer::startSprint(int i) {
    const cr3::Gimmick& g = field.gimmicks()[size_t(i)];
    sprint = Sprint();
    sprint.to = script->touchAction(g.name)->spots;
    sprint.stage = 1;
    heading = std::atan2(g.position[0] - player[0], g.position[2] - player[2]); // (FldPlayer::SetDirection to it)
    playMotion(fig, "fj01"), time = 0;
    SDL_Log("%s: sprint %s - %zu spots", field.info().name.c_str(), g.name.c_str(), sprint.to.size());
}

void Viewer::sprintHop() {
    Sprint& s = sprint;
    const std::array<float, 3>& to = s.to[s.k];
    std::memcpy(s.from, player, sizeof s.from);
    const float h = to[1] >= s.from[1] ? to[1] - s.from[1] + 0.04f : 0.04f, dy = to[1] - s.from[1];
    s.v = std::sqrt(1.96f * h);
    const float r = std::sqrt(std::max(s.v * s.v - 1.96f * dy, 0.0f));
    s.T = std::max((r - s.v) / -0.98f, (-s.v - r) / -0.98f);
    if (s.T <= 0) s.T = 0.1f;
    s.vx = (to[0] - s.from[0]) / s.T, s.vz = (to[2] - s.from[2]) / s.T;
    heading = std::atan2(to[0] - s.from[0], to[2] - s.from[2]);
    s.t = 0, s.air = 0, s.stage = 2;
    playMotion(fig, "fj02"), time = 0;
    audio.playSe(0x155, false);
    if (s.jump) audio.stopAllVoices(0), audio.playVoice(0x8fa);
}

void Viewer::updateSprint(float dt) {
    Sprint& s = sprint;
    if (!s.stage) return;
    s.t += dt;
    if (s.stage == 1) {
        if (s.t >= 0.15f) sprintHop(); // (the crouch's blend)
        return;
    }
    const float t = std::min(s.t, s.T);
    player[0] = s.from[0] + s.vx * t, player[1] = s.from[1] + s.v * t - 0.49f * t * t, player[2] = s.from[2] + s.vz * t;
    if (s.air == 0 && s.t >= 0.15f) { // (fj03, ending as it lands)
        s.air = 1;
        playMotion(fig, "fj03");
        const float d = fig.animLoaded ? fig.anim.duration() : 0.0f;
        time = std::max(d - (s.T - s.t - 0.2f), 0.0f);
    } else if (s.air == 1 && s.t >= s.T - 0.2f) {
        s.air = 2;
        playMotion(fig, "fj01"), time = 0;
    }
    if (s.t < s.T) {
        std::memcpy(playerPrev, player, sizeof player), std::memcpy(playerCur, player, sizeof player);
        return;
    }
    const std::array<float, 3>& to = s.to[s.k];
    player[0] = to[0], player[1] = to[1], player[2] = to[2];
    std::memcpy(playerPrev, player, sizeof player), std::memcpy(playerCur, player, sizeof player);
    audio.playSe(0x156, false);
    if (++s.k < s.to.size()) {
        sprintHop();
        return;
    }
    s.stage = 0; // (on the last: idle, the controller there)
    playMotion(fig, "fn01");
    controller->setPosition({player[0], player[1] + 0.02f, player[2]});
    for (int k = 0; k < 4; ++k) controller->update({0, 0, 0});
    const cr3::Vec3& p = controller->position();
    player[0] = p[0], player[1] = p[1] - 0.02f, player[2] = p[2];
    std::memcpy(playerPrev, player, sizeof player), std::memcpy(playerCur, player, sizeof player);
    checkBalloons();
}

void Viewer::drawKeyMark(int w, int h) {
    if (script && !script->boxVisible()) return; // (SetBoxVisible: GimmickAllViewOff)
    if (fieldMode && !eventMode && actionMark.gimmick >= 0) {
        const Mat4 proj = uiProjection(w, h);
        Mat4 camera = cr3::mat4Identity();
        camera[14] = -0.881f;
        cr3::SceneLighting l;
        dev->setLighting(l);
        drawActionMark(cr3::mat4Multiply(proj, camera));
    }
    if (!fieldMode || eventMode || keyLock.gimmick < 0) return;
    const cr3::Gimmick& g = field.gimmicks()[size_t(keyLock.gimmick)];
    if (keyLock.stage == 0 && game.boxes.isOpen(g.boxKind, g.boxNumber)) return;
    float x, y;
    if (!keyMarkPlace(x, y)) return;
    const Mat4 proj = uiProjection(w, h);
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f;
    const Mat4 view = cr3::mat4Multiply(proj, camera);
    cr3::SceneLighting l;
    dev->setLighting(l);
    auto at = [&](UiFig& u, float scale) {
        u.fig.place = cr3::Mat34();
        u.fig.place.m[0] = u.fig.place.m[5] = u.fig.place.m[10] = scale;
        u.fig.place.m[3] = x, u.fig.place.m[7] = y, u.fig.place.m[11] = -0.2f; // (SetParamNumber 4: z -0.2)
        drawFigure(u.fig, view.data());
    };
    auto duration = [&](UiFig& u, const char* anim) {
        poseUi(u, anim, 0);
        return u.fig.animLoaded ? u.fig.anim.duration() : 0.0f;
    };
    const float t = uiClock - keyLock.markAt, ts = uiClock - keyLock.stageAt;
    if (!keyLock.orlando) {
        UiFig& m = uiFig("gmk0rng_io");
        poseUi(m, "gmk0rng_io", t);
        at(m, 1);
        return;
    }
    UiFig& ring = uiFig("gmk2ring");
    const float io = duration(ring, "gmk2ring_io");
    if (keyLock.stage == 2) poseUi(ring, "gmk2ring_out", ts);
    else if (t < io) poseUi(ring, "gmk2ring_io", t);
    else poseUi(ring, "gmk2ring_wait", t - io, true);
    at(ring, keyLock.stage == 1 ? 1 + keyLock.spread / kUnlockSpread : 1);
    if (keyLock.stage == 0) {
        UiFig& b = uiFig("gmkbtn");
        poseUi(b, "gmkbtn_io", t);
        at(b, 1);
    } else if (keyLock.stage == 1) {
        UiFig& b = uiFig("gmkbtn");
        const float out = duration(b, "gmkbtn_out");
        if (ts < out) {
            poseUi(b, "gmkbtn_out", ts);
            at(b, 1);
        } else { // (then the guide, looping: gmk2csr_loop)
            UiFig& c = uiFig("gmk2csr_loop");
            poseUi(c, "gmk2csr_loop", ts - out, true);
            at(c, 1 + keyLock.spread / kUnlockSpread);
        }
    }
}

void Viewer::walk(float dt, float sx, float sy, bool run) {
    if (!controller) return;
    cr3::Vec3 right, forward;
    field.groundAxes(right, forward);
    cr3::Vec3 dir{right[0] * sx + forward[0] * sy, 0, right[2] * sx + forward[2] * sy};
    const float l = std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
    const float speed = l > 0 ? (run ? 0.48f : 0.09f) : 0.0f;
    if (l > 0) dir = {dir[0] / l * speed, 0, dir[2] / l * speed}, heading = std::atan2(dir[0], dir[2]);
    constexpr float kSub = cr3::CharacterController::kUpdate / cr3::CharacterController::kSubsteps; // (1/60 s)
    stepClock = std::min(stepClock + dt, 15 * kSub);
    std::string exitTo;
    for (; stepClock >= kSub && exitTo.empty(); stepClock -= kSub) {
        std::memcpy(playerPrev, playerCur, sizeof playerCur);
        controller->move(dir);
        controller->substep();
        const cr3::Vec3& p = controller->position();
        playerCur[0] = p[0], playerCur[1] = p[1] - 0.02f, playerCur[2] = p[2];
        if (++substep < cr3::CharacterController::kSubsteps) continue;
        substep = 0;
        // an exit: the area changes to the map its pop point names (FldMain::ChangeAreaExecute)
        const std::string exit = field.exitTouched(p, controller->radius);
        if (exit.empty()) exitsArmed = true;
        else if (exitsArmed && exit.size() >= 5) exitTo = exit;
        touchEvents(p);
        checkBalloons();
    }
    if (script && script->playerState().motionSerial != leaderMotionSerial) // (Cr3FieldMapCharacter.SetMotion)
        leaderMotionSerial = script->playerState().motionSerial, leaderMotion = script->playerState().motion,
        SDL_Log("leader: motion %s", leaderMotion.c_str());
    if (speed != 0) leaderMotion.clear(); // (moving again: its own motions)
    playMotion(fig, !leaderMotion.empty() ? leaderMotion.c_str() : speed == 0 ? "fn01" : run ? "ff02" : "ff01");
    if (!exitTo.empty()) {
        SDL_Log("exit %s", exitTo.c_str());
        enterMap(exitTo.substr(0, 5), exitTo);
        return;
    }
    const float a = std::clamp(stepClock / kSub, 0.0f, 1.0f);
    for (int c = 0; c < 3; ++c) player[c] = playerPrev[c] + (playerCur[c] - playerPrev[c]) * a;
}

bool Viewer::loadTable(const std::string& binary, const std::string& dataPath) {
    dataArg = dataPath, binaryArg = binary;
    if (cr3::releaseMapTable(dataPath, binary, table, &tableFrom)) return true;
    SDL_Log("the maps' table: not found beside %s (the iPad's executable, the Vita's eboot.bin, the Android "
            "release's APK) - give the game's executable with --binary", dataPath.c_str());
    return false;
}

void Viewer::drawResidents(const float* mvp) {
    if (!script) return;
    for (const auto& [n, r] : script->residents()) {
        if (r.tick < 0) continue;
        const char* name = cr3::FieldScript::residentFigure(n);
        ViewFigure& f = residentFigs[n];
        std::string err;
        if (f.meshes.empty() && f.name.empty() && !loadFigure(std::string(name) + ".geom", f, &err)) {
            SDL_Log("resident %s: %s", name, err.c_str());
            f.name = "-";
            continue;
        }
        if (f.meshes.empty()) continue;
        const float t = float(script->ticks() - r.tick) * cr3::EventRuntime::kTick + fieldClock;
        if (f.animLoaded && t > f.anim.duration()) continue; // (played out)
        float p[3] = {player[0], player[1], player[2]}, yaw = heading;
        for (const cr3::Gimmick& g : field.gimmicks())
            if (r.target != "player" && (g.id == r.target || g.name == r.target)) {
                std::memcpy(p, g.position, sizeof p);
                yaw = 2 * std::atan2(g.rotation[1], g.rotation[3]); // (its turn about y)
                break;
            }
        const float c = std::cos(yaw), sn = std::sin(yaw);
        f.place = cr3::Mat34();
        f.place.m[0] = c * r.scale[0], f.place.m[2] = sn * r.scale[2], f.place.m[5] = r.scale[1];
        f.place.m[8] = -sn * r.scale[0], f.place.m[10] = c * r.scale[2];
        f.place.m[3] = p[0], f.place.m[7] = p[1], f.place.m[11] = p[2];
        pose(f, t, false);
        drawFigure(f, mvp);
    }
}

// where a turn's target is ("player", else a gimmick's id or name)
bool Viewer::gimmickTarget(const std::string& target, float& x, float& z) const {
    if (target == "player") return x = player[0], z = player[2], true;
    for (const cr3::Gimmick& g : field.gimmicks())
        if (g.id == target || g.name == target) return x = g.position[0], z = g.position[2], true;
    return false;
}

float Viewer::turnBetween(float from, float to, float a) { // (the shorter way round)
    float d = std::remainder(to - from, 6.28318531f);
    return from + d * a;
}

void Viewer::refreshGimmicks() {
    std::vector<std::string> want;
    for (const cr3::Gimmick& gm : field.gimmicks())
        if (!gm.figure.empty() && archive.contains(gm.figure + ".geom") && script->visible(gm)) want.push_back(gm.name);
    if (want == shownGimmicks) return;
    shownGimmicks = want;
    std::string err;
    for (ViewFigure& g : gimmicks) unload(g);
    gimmicks.clear(), gimmickIds.clear(), gimmickSerial.clear(), gimmickFace.clear(), gimmickColor.clear();
    for (const cr3::Gimmick& gm : field.gimmicks()) {
        if (gm.figure.empty() || !archive.contains(gm.figure + ".geom") || !script->visible(gm)) continue;
        ViewFigure g;
        cr3::JointTransform t;
        std::memcpy(t.rotation, gm.rotation.data(), sizeof t.rotation);
        std::memcpy(t.translation, gm.position, sizeof t.translation);
        t.scale[0] = t.scale[1] = t.scale[2] = gm.scale;
        g.place = t.matrix();
        if (!loadFigure(gm.figure + ".geom", g, &err)) continue;
        if (gm.kind == cr3::Gimmick::Npc) playMotion(g, "fn01");
        if (gm.kind == cr3::Gimmick::Box) playMotion(g, game.boxes.isOpen(gm.boxKind, gm.boxNumber) ? "fn02" : "fn01"); // (FldBox: closed fn01, open fn02)
        gimmicks.push_back(std::move(g));
        gimmickIds.push_back(gm.id), gimmickSerial.push_back(0), gimmickColor.push_back(-1);
        Facing fc; // (its joint's yaw)
        fc.yaw = std::atan2(g.place.m[2], g.place.m[0]);
        gimmickFace.push_back(fc);
    }
}

// A map: its table row, layers and figures, the character dropped at the point `at` (empty:
// where it stood, the field re-entered after a vista or a battle), the area's `entry` started
bool Viewer::enterMap(const std::string& map, const std::string& at, const std::string& entry) {
    const auto row = std::find_if(table.begin(), table.end(), [&](const cr3::MapInfo& m) { return m.name == map; });
    if (row == table.end()) {
        SDL_Log("%s: not one of the %zu maps (%s)", map.c_str(), table.size(), tableFrom.c_str());
        return false;
    }
    std::string err;
    if (!field.load(archive, *row, &err)) {
        SDL_Log("%s: %s", map.c_str(), err.c_str());
        return false;
    }
    unload(mapFigure);
    mapFigure = ViewFigure();
    if (!field.figureName().empty() && !loadFigure(field.figureName() + ".geom", mapFigure, &err))
        SDL_Log("%s: %s", field.figureName().c_str(), err.c_str());
    mapFigure.absolute = true; // (FldMap::ChangeArea: BindAnimator(figure, 0, ...))
    // a dungeon entered anew: its treasure boxes drawn (FldMain::ChangeDungeonFunc; not with flag 0x7fb)
    const std::string dungeon = map.substr(0, 3);
    if (dungeon != boxesDungeon && !game.flag(0x7fb)) {
        game.boxes.choose(game.difficulty, game.party, [] { return uint32_t(boxRandom()); });
        int placed = 0;
        for (int k = 0; k < 3; ++k)
            for (int n = 0; n < cr3::TreasureBoxes::count(k); ++n) placed += game.boxes.isSet(k, n);
        SDL_Log("%s: the treasure boxes drawn - table %d, %d placed", dungeon.c_str(), game.boxes.table, placed);
    }
    boxesDungeon = dungeon;
    keyLock = KeyLock();
    actionMark = ActionMark(), sprint = Sprint(), iai = Iai(), aim = Aim(), hakkei = Hakkei(), mystic = Mystic();
    // the dungeon's script sets the area up (evt000: which gimmicks show, which collide)
    if (!script) script = std::make_unique<cr3::FieldScript>(archive, game), script->onSave = [this](int k) { scriptSave(k); };
    script->events().language = optionLanguage; // (the release's language's slot)
    script->events().autoPage = pageSeconds, script->events().askYesNo = true;
    if (!script->enter(map, field.gimmicks(), entry)) SDL_Log("%s's script: %s", map.c_str(), script->error().c_str());
    if (!playMode && script->running() && !script->runToEnd()) // (--field: the area as its set-up leaves it; --play: as it runs)
        SDL_Log("%s's %s: %s", map.c_str(), entry.c_str(), script->error().empty() ? "still running" : script->error().c_str());
    SDL_Log("%s: %s %s", map.c_str(), entry.c_str(), script->running() ? "running" : "done");
    if (playMode && !script->running()) pendingBranch = true; // (ended at once: what it reserved, as after any event)
    if (!script->flagMarks().empty()) {
        std::string marks;
        for (const std::string& f : script->flagMarks()) marks += " " + f;
        SDL_Log("%s: flag marks%s", map.c_str(), marks.c_str());
    }
    fieldClock = 0;
    field.setSolidGimmicks([this](const cr3::Gimmick& g) {
        const auto it = script->gimmicks().find(g.id);
        return script->visible(g) && (it == script->gimmicks().end() || it->second.collision);
    });
    shownGimmicks.clear();
    refreshGimmicks();
    if (at.empty()) {
        // (where the leader stood)
    } else if (!field.point(at, player)) {
        SDL_Log("%s: no point %s; the points:", map.c_str(), at.c_str());
        for (const std::string& n : field.pointNames()) SDL_Log("  %s", n.c_str());
    }
    fieldMode = true;
    exitsArmed = false;
    dev->resetHistory();
    controller = std::make_unique<cr3::CharacterController>(field.collision());
    controller->setPosition({player[0], player[1] + 0.17f, player[2]}); // (dropped onto the floor)
    for (int k = 0; k < 10; ++k) controller->update({0, 0, 0});
    const cr3::Vec3& p = controller->position();
    player[0] = p[0], player[1] = p[1] - 0.02f, player[2] = p[2];
    std::memcpy(playerPrev, player, sizeof player);
    std::memcpy(playerCur, player, sizeof player);
    stepClock = 0, substep = 0;
    insideEvents.clear();
    { // (where the leader stands as the area starts: those boxes don't fire till it leaves them)
        for (const cr3::Gimmick& g : field.gimmicks())
            if (g.kind == cr3::Gimmick::Event && !g.check && g.touches(player, 0.02f)) insideEvents.insert(g.name);
    }
    checkBalloons();
    std::string exits;
    for (const std::string& e : field.exitNames()) exits += " " + e;
    SDL_Log("%zu gimmicks, %zu with figures; layers bg [%s] fg [%s]", field.gimmicks().size(), gimmicks.size(), field.backgroundImage().c_str(), field.foregroundImage().c_str());
    SDL_Log("field %s (table from %s): variant %d, layers %s%s%s%s, %zu collision triangles, exits%s, at %s (%.3f %.3f %.3f)",
            map.c_str(), tableFrom.c_str(), row->variant, field.backgroundImage().empty() ? "" : "bg ",
            field.midgroundImage().empty() ? "" : "mg ", field.foregroundImage().empty() ? "" : "fg ",
            field.figureName().empty() ? "" : "+ 3D figure", field.collision().triangles(), exits.empty() ? " none" : exits.c_str(),
            at.c_str(), player[0], player[1], player[2]);
    return true;
}

bool Viewer::loadField(const std::string& map, const std::string& at, const std::string& binary, const std::string& dataPath,
               const std::string& entry) {
    return loadTable(binary, dataPath) && enterMap(map, at, entry);
}

bool Viewer::fieldScriptRunning() const { return script && script->running(); }

// --input's tokens the field takes (fieldToken: the tab, the camp), one a 0.4 s while it is idle
void Viewer::fieldInput() {
    if (!inputScript.empty() && mystic.stage && uiClock - actionMark.startedAt > 1.0f && uiClock >= inputNext &&
        (inputScript.front() == "tapspot" || inputScript.front() == "tapspotfail")) { // (a mystic's: the first other spot, or none)
        const cr3::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name);
        const int other = a && a->spots.size() > 1 ? (a->markSpot() == 0 ? 1 : 0) : -1;
        mysticTap(inputScript.front() == "tapspot" ? other : -1);
        inputScript.pop_front();
        inputNext = uiClock + 0.4f;
        return;
    }
    if (!inputScript.empty() && hakkei.stage && uiClock - actionMark.startedAt > 1.0f && uiClock >= inputNext &&
        (inputScript.front() == "punch" || inputScript.front() == "punchfail")) { // (a hakkei's tap: on the mark, or away)
        hakkeiTap(inputScript.front() == "punch");
        inputScript.pop_front();
        inputNext = uiClock + 0.1f;
        return;
    }
    if (!inputScript.empty() && uiClock >= inputNext && keyLock.stage == 1 && uiClock - keyLock.stageAt > 1.0f &&
        (inputScript.front() == "unlock" || inputScript.front() == "unlockfail")) { // (the lock: spread, or let go)
        unlockEnd(inputScript.front() == "unlock");
        inputScript.pop_front();
        inputNext = uiClock + 0.4f;
        return;
    }
    if (!inputScript.empty() && uiClock >= inputNext && (iai.stage || aim.stage) && uiClock - actionMark.startedAt > 1.0f &&
        (inputScript.front() == "slash" || inputScript.front() == "slashfail" || inputScript.front() == "aim" ||
         inputScript.front() == "aimfail" || inputScript.front() == "drag")) { // (the iai: a slash past the mark, or not; the aim: drawn back to the leader, or not)
        if (inputScript.front() == "drag") { // (an aim's press on the mark, dragged halfway: tests)
            float mx, my, lx, ly;
            if (aim.stage && actionMarkPlace(mx, my) && leaderPlace(lx, ly)) aim.stage = 2, aim.x = (mx + lx) / 2, aim.y = (my + ly) / 2;
            inputScript.pop_front();
            inputNext = uiClock + 0.4f;
            return;
        }
        iaiEnd(inputScript.front() == "slash" || inputScript.front() == "aim");
        inputScript.pop_front();
        inputNext = uiClock + 0.4f;
        return;
    }
    if (!inputScript.empty() && uiClock >= inputNext && party.stage && uiClock - party.at > 0.5f) { // (the party: organize, talk, first, second, left, right, pick, back)
        const std::string t = inputScript.front();
        static const std::map<std::string, int> kIds = {{"organize", 25}, {"talk", 13}, {"first", 1}, {"second", 2}, {"back", 100}};
        if (auto i = kIds.find(t); i != kIds.end()) partyTap(i->second);
        else if (t == "left" || t == "right") partyKey(t == "left" ? SDLK_LEFT : SDLK_RIGHT);
        else if (t == "pick") partyKey(SDLK_RETURN);
        inputScript.pop_front();
        inputNext = uiClock + 0.4f;
        return;
    }
    if (!inputScript.empty() && uiClock >= inputNext && shop.stage && uiClock - shop.at > 0.5f) { // (the shop: its buttons by name, the list's, how many, yes / no)
        shopToken(inputScript.front());
        inputScript.pop_front();
        inputNext = uiClock + 0.3f;
        return;
    }
    if (!inputScript.empty() && uiClock >= inputNext && pub.stage && uiClock - pub.at > 0.5f) { // (the pub: next, pick, accept, back, yes, no)
        const std::string t = inputScript.front();
        if (t == "next") pubKey(SDLK_RIGHT);
        else if (t == "list") pubTap(35);
        else if (t == "pick" || t == "accept") pubKey(SDLK_RETURN);
        else if (t == "back") pubKey(SDLK_BACKSPACE);
        else if (t == "yes" || t == "no") pubKey(t == "yes" ? SDLK_Y : SDLK_N);
        inputScript.pop_front();
        inputNext = uiClock + 0.4f;
        return;
    }
    if (!inputScript.empty() && uiClock >= inputNext && library.stage == 1 && uiClock - library.at > 0.5f) { // (the library: up, down, pick, back)
        const std::string t = inputScript.front();
        libraryKey(t == "up" ? SDLK_UP : t == "down" ? SDLK_DOWN : t == "pick" ? SDLK_RETURN : t == "back" ? SDLK_BACKSPACE : SDLK_UNKNOWN);
        inputScript.pop_front();
        inputNext = uiClock + 0.4f;
        return;
    }
    if (!inputScript.empty() && uiClock >= inputNext && world.stage && uiClock - world.at > 0.5f) { // (the world select: up, down, pick, back, yes, no)
        const std::string t = inputScript.front();
        if (t == "up" || t == "down") worldKey(t == "up" ? SDLK_UP : SDLK_DOWN);
        else if (t == "pick") worldKey(SDLK_RETURN);
        else if (t == "back") worldKey(SDLK_BACKSPACE);
        else if (t == "yes" || t == "no") worldKey(t == "yes" ? SDLK_Y : SDLK_N);
        inputScript.pop_front();
        inputNext = uiClock + 0.4f;
        return;
    }
    if (inputScript.empty() || uiClock < inputNext || (!campOpen && !fieldTabShown() && !mapOpen && !saveStage) || manualStage == 2) return;
    if (!fieldToken(inputScript.front())) return; // (not the field's: left for a battle)
    inputScript.pop_front();
    inputNext = uiClock + 0.4f;
}

void Viewer::fieldUpdate(float dt) {
    if (!script) return;
    fieldClock = std::min(fieldClock + dt, 0.25f);
    for (; fieldClock >= cr3::EventRuntime::kTick; fieldClock -= cr3::EventRuntime::kTick) {
        if (pendingBranch) {
            pendingBranch = false;
            branchAfterField();
            if (!fieldMode) return;
            continue;
        }
        // (the windows, the fade, the scene go on updating once the script has ended: GameMain's
        // update runs them whether or not a script waits)
        const bool wasRunning = script->running();
        if (!script->tick()) SDL_Log("%s's script: %s", field.info().name.c_str(), script->error().c_str());
        updateKeyLock();
        updateActionMark();
        refreshLeader(); // (ChangeModel, ExeQuick)
        if (!wasRunning) continue;
        refreshGimmicks();
        if (script->events().scene().loaded()) syncEventFigures(script->events());
        placePlayer(script->takePlacement());
        checkBalloons();
        if (!script->running()) {
            SDL_Log("%s: the script ended", field.info().name.c_str());
            if (library.stage == 2) library.stage = 1, library.at = uiClock; // (an entry read: the shelf again)
            else if (playMode) branchAfterField();
            if (!fieldMode) return; // (gone to a vista)
        }
    }
}

// the script an event is in: its name's second part where there's such a script
// (s000_d01_00301_evt: d01), else the one the last was in
std::string Viewer::eventScriptOf(const std::string& fn) const {
    const size_t a = fn.find('_'), b = a == std::string::npos ? a : fn.find('_', a + 1);
    if (b != std::string::npos) {
        const std::string s = fn.substr(a + 1, b - a - 1);
        if (archive.contains("script\\" + s + ".nut")) return s;
    }
    return eventScript;
}

bool Viewer::startEvent(const std::string& function, std::string map, float page) {
    initEventDrawing();
    pageSeconds = page;
    // (CR3: a vista event's map is named by its caller - "<script>:X" calls SetVistaEvent("X_evt",
    // "<map>"): Cr3System.SetVistaFuncName / SetVistaMapName. Without --vista, X run first as the
    // game does, its map taken)
    const size_t colon = function.find(':');
    if (map == "null" && colon != std::string::npos && function.size() > 4 && function.compare(function.size() - 4, 4, "_evt") == 0) {
        const std::string script = function.substr(0, colon), fn = function.substr(colon + 1);
        game.next = cr3::GameState::Next();
        cr3::EventRuntime caller(archive, game);
        if (caller.start(script, fn.substr(0, fn.size() - 4)) && game.next.vistaFunction == fn) map = game.next.vistaMap;
        game.next = cr3::GameState::Next();
        if (map.empty()) map = "null"; // (the prologue's: "" - its scene is all the event's own)
    }
    return startVista(function, map);
}

void Viewer::initEventDrawing() {
    if (eventDrawingReady) return;
    eventDrawingReady = true;
    // the fades' colours (InterfaceMain's fade windows: the figures fadeb, fadew, fadef, their
    // material's DiffuseColor); the encounter's wipe black
    const char* fades[3] = {"fadeb", "fadew", "fadef"};
    for (int k = 0; k < 3; ++k) {
        std::vector<uint8_t> d;
        cr3::Geom g;
        if (archive.read(std::string(fades[k]) + ".geom", d) && cr3::parseGeom(d, g) && !g.materials.empty())
            g.materials[0].param(0x33, fadeColours[k], 3);
    }
    cr3::Image px;
    px.width = px.height = 1;
    px.rgba = {255, 255, 255, 255};
    white = dev->upload(px);
}

std::array<float, 3> Viewer::fieldCameraLD(const std::string& name, bool& found) {
    found = false;
    const int dungeon = cr3::cr3DungeonNum(name);
    if (dungeon < 0 || name.size() < 5) return {};
    const int areaNo = std::atoi(name.substr(3, 2).c_str());
    std::vector<uint8_t> d;
    cr3::MbeFile f;
    if (!archive.read("data\\fieldcamerald.mbe", d) || !f.parse(d)) return {};
    const cr3::MbeFile::Table* t = f.table("FieldCameraLD");
    if (!t) return {};
    for (size_t r = 0; r < t->rows.size(); ++r) { // (GetFieldCameraPositionLD: the first of the dungeon and area)
        cr3::MbeFile::Row row;
        if (!f.row(*t, r, "iiifff", row) || row.i(1) != dungeon || row.i(2) != areaNo) continue;
        found = true;
        return {row.f(3), row.f(4), row.f(5)};
    }
    return {};
}

bool Viewer::setupRail(const std::string& name) {
    rail = Cr3RailCam();
    std::vector<uint8_t> d;
    cr3::Skeleton sk;
    cr3::Animation an;
    if (!archive.read(name + "_cam.skel", d) || !cr3::parseSkeleton(d, sk) || !archive.read(name + "_cam.anim", d) || !an.parse(d)) return false;
    std::vector<std::string> names;
    if (archive.read(name + "_cam.name", d)) names = cr3::parseJointNames(d);
    const int frames = int(float(int(an.duration() * 30.0f)) - 0.1f);
    std::vector<std::pair<int, int>> groups; // (aim joint, camera joint)
    for (size_t j = 0; j < names.size(); ++j)
        if (names[j].size() > 4 && names[j].compare(names[j].size() - 4, 4, "_aim") == 0) {
            const auto c = std::find(names.begin(), names.end(), names[j].substr(0, names[j].size() - 4));
            if (c != names.end()) groups.push_back({int(j), int(c - names.begin())});
        }
    if (groups.empty() || frames < 2) return false;
    rail.aim.assign(groups.size(), {}), rail.cam.assign(groups.size(), {});
    for (int i = 0; i < frames; ++i) {
        const std::vector<cr3::Mat34> w = sk.world(an.poseAbsolute(sk.basePose, std::min(float(i) / 30.0f * an.sampleRate(), float(an.frames() - 1))));
        for (size_t g = 0; g < groups.size(); ++g) {
            const cr3::Mat34& a = w[size_t(groups[g].first)];
            const cr3::Mat34& c = w[size_t(groups[g].second)];
            rail.aim[g].push_back({a.m[3], a.m[7], a.m[11]}), rail.cam[g].push_back({c.m[3], c.m[7], c.m[11]});
        }
    }
    const std::vector<cr3::Mat34> w0 = sk.world(an.poseAbsolute(sk.basePose, 0));
    for (size_t j = 0; j < names.size() && j < w0.size(); ++j)
        if (names[j].compare(0, 7, "cam_box") == 0 && names[j].size() >= 9) {
            Cr3RailCam::Box b;
            b.world = w0[j], b.group = std::atoi(names[j].substr(7, 2).c_str()) - 1;
            rail.boxes.push_back(b);
        }
    bool found = false;
    rail.offset = fieldCameraLD(name, found);
    rail.on = true;
    SDL_Log("area %s: the rail camera, %zu groups of %d frames, %zu boxes, offset (%.1f %.1f %.1f)", name.c_str(), groups.size(), frames,
            rail.boxes.size(), rail.offset[0], rail.offset[1], rail.offset[2]);
    return true;
}

float Viewer::railLen(const cr3::Vec3& a, const cr3::Vec3& b) { return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }

cr3::Vec3 Viewer::railCamAt(float f) const {
    const std::vector<cr3::Vec3>& p = rail.cam[size_t(rail.group)];
    const int i = std::clamp(int(f), 0, int(p.size()) - 1), j = std::min(i + 1, int(p.size()) - 1);
    const float t = std::fmod(std::max(f, 0.0f), 1.0f);
    return {p[size_t(i)][0] + (p[size_t(j)][0] - p[size_t(i)][0]) * t, p[size_t(i)][1] + (p[size_t(j)][1] - p[size_t(i)][1]) * t,
            p[size_t(i)][2] + (p[size_t(j)][2] - p[size_t(i)][2]) * t};
}

void Viewer::railSearch(const cr3::Vec3& target) { // (SearchBestPath2)
    const std::vector<cr3::Vec3>& p = rail.aim[size_t(rail.group)];
    float bestSum = 65535;
    int k = -1;
    for (int i = 0; i + 1 < int(p.size()); ++i) {
        const float sum = railLen(target, p[size_t(i)]) + railLen(target, p[size_t(i) + 1]);
        if (sum < bestSum) bestSum = sum, k = i;
    }
    if (k > 0) { // (NearestPointSegment: the fraction along it)
        const cr3::Vec3 &a = p[size_t(k)], &b = p[size_t(k) + 1];
        const float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float l2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
        const float t = l2 > 1e-9f ? std::clamp(((target[0] - a[0]) * ab[0] + (target[1] - a[1]) * ab[1] + (target[2] - a[2]) * ab[2]) / l2, 0.0f, 1.0f) : 0.0f;
        rail.best = float(k) + t;
    }
}

void Viewer::railReset(const cr3::Vec3& feet) { // (Reset: StartCameraGroup, the eye at once)
    float nearest = 1e30f;
    for (size_t g = 0; g < rail.aim.size(); ++g)
        for (size_t i = 0; i < rail.aim[g].size(); ++i)
            if (const float l = railLen(feet, rail.aim[g][i]); l < nearest) nearest = l, rail.group = int(g), rail.best = float(i);
    const cr3::Vec3 target{feet[0] + rail.offset[0], feet[1] + rail.offset[1], feet[2] + rail.offset[2]};
    railSearch(target);
    rail.shown = rail.best, rail.easing = rail.switching = false, rail.blend = 0, rail.started = true;
    rail.eye = railCamAt(rail.best);
    for (Cr3RailCam::Box& b : rail.boxes) b.in = false;
    areaCamera.eye = rail.eye, areaCamera.at = target;
}

bool Viewer::railBoxHolds(const cr3::Mat34& w, const cr3::Vec3& p) const {
    const float d[3] = {p[0] - w.m[3], p[1] - w.m[7], p[2] - w.m[11]};
    for (int a = 0; a < 3; ++a) {
        const float ax[3] = {w.m[a], w.m[4 + a], w.m[8 + a]};
        const float len = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
        if (len < 1e-6f) return false;
        if (std::fabs((d[0] * ax[0] + d[1] * ax[1] + d[2] * ax[2]) / len) > len * 0.05f + 0.2f) return false;
    }
    return true;
}

void Viewer::railStep(const cr3::Vec3& feet, const cr3::Vec3& centre, float dt) {
    if (!rail.started) return railReset(feet);
    for (Cr3RailCam::Box& b : rail.boxes) { // (CallbackCollisionEvent: entering one)
        const bool in = railBoxHolds(b.world, centre);
        if (in && !b.in && b.group >= 0 && b.group < int(rail.aim.size()) && b.group != rail.group)
            rail.switching = rail.easing = true, rail.from = rail.eye, rail.blend = 0, rail.group = b.group;
        b.in = in;
    }
    const cr3::Vec3 target{feet[0] + rail.offset[0], feet[1] + rail.offset[1], feet[2] + rail.offset[2]};
    rail.tick += dt;
    for (; rail.tick >= 1.0f / 30; rail.tick -= 1.0f / 30) { // (RailAction, an update each 30th)
        railSearch(target);
        if (rail.switching && rail.easing) {
            rail.blend += 1.0f / 30;
            if (rail.blend > 0.3f) {
                rail.blend = 0, rail.switching = rail.easing = false;
                continue;
            }
            rail.shown = rail.best;
            const cr3::Vec3 to = railCamAt(rail.best);
            const float k = rail.blend / 0.3f;
            for (int c = 0; c < 3; ++c) rail.eye[c] = to[c] * k + rail.from[c] * (1 - k);
            continue;
        }
        if (!rail.switching && rail.easing) {
            if (rail.shown < rail.best) rail.shown = std::min(rail.shown + 2.5f, rail.best);
            else if (rail.shown > rail.best) rail.shown = std::max(rail.shown - 2.5f, rail.best);
            if (std::fabs(rail.best - rail.shown) < 2.5f) rail.easing = false;
        } else if (!rail.easing) {
            const cr3::Vec3 next = railCamAt(rail.best);
            if (railLen(next, rail.eye) <= 1.0f) rail.shown = rail.best;
            else rail.easing = true;
        }
        rail.eye = railCamAt(rail.shown);
    }
    areaCamera.eye = rail.eye, areaCamera.at = target;
}

void Viewer::setupAreaCamera(const std::string& name) {
    areaCamera.setLookDown(false), areaCamera.setExternal(false), rail = Cr3RailCam();
    if (std::find_if(std::begin(kRailMaps), std::end(kRailMaps), [&](const char* m) { return name == m; }) != std::end(kRailMaps) && setupRail(name)) {
        areaCamera.setExternal(true);
        return;
    }
    if (std::find_if(std::begin(kLookDownMaps), std::end(kLookDownMaps), [&](const char* m) { return name == m; }) == std::end(kLookDownMaps)) return;
    bool found = false; // (towns alone: a dungeon's own number not known yet)
    const std::array<float, 3> offset = fieldCameraLD(name, found);
    if (!found) return;
    areaCamera.setLookDown(true, offset);
    SDL_Log("area %s: the look-down camera, (%.1f %.1f %.1f)", name.c_str(), offset[0], offset[1], offset[2]);
}

bool Viewer::fieldTargetAt(const std::string& name, cr3::Vec3& out) {
    if (name.empty()) return out = {0, 0, 0}, true; // (an environment effect: where its model is)
    if (name == "player" && areaWalker) {
        out = areaWalker->position(), out[1] -= areaWalker->radius;
        return true;
    }
    for (const AreaGimmick& g : areaGimmicks)
        if (g.function == name) return out = {g.world.m[3], g.world.m[7], g.world.m[11]}, true;
    for (const AreaNpc& npc : areaNpcs)
        if (npc.name == name) return out = {npc.place.m[3], npc.place.m[7], npc.place.m[11]}, true;
    if (areaJointsOf != area.name) { // (any other joint of <area>p's: a pop point - t0102_p1)
        areaJointsOf = area.name, areaJoints.clear();
        std::vector<uint8_t> d;
        cr3::Skeleton sk;
        if (archive.read(area.name + "p.skel", d) && cr3::parseSkeleton(d, sk) && archive.read(area.name + "p.name", d)) {
            const std::vector<std::string> names = cr3::parseJointNames(d);
            const std::vector<cr3::Mat34> w = sk.world(sk.basePose);
            for (size_t j = 0; j < names.size() && j < w.size(); ++j) areaJoints[names[j]] = {w[j].m[3], w[j].m[7], w[j].m[11]};
        }
    }
    const auto it = areaJoints.find(name);
    return it != areaJoints.end() && (out = it->second, true);
}

const cr3::SparkAsset* Viewer::sparkAsset(const std::string& model) {
    auto it = sparkAssets.find(model);
    if (it == sparkAssets.end()) {
        std::unique_ptr<cr3::SparkAsset> a;
        std::vector<uint8_t> d;
        std::string err;
        if (archive.read(model + ".sprk", d)) {
            a = std::make_unique<cr3::SparkAsset>();
            if (!cr3::parseSpark(d, *a, &err)) SDL_Log("effect %s: %s", model.c_str(), err.c_str()), a.reset();
        }
        it = sparkAssets.emplace(model, std::move(a)).first;
    }
    return it->second.get();
}

void Viewer::dropSpark(ViewSpark& v) {
    for (auto& [m, mesh] : v.meshes) dev->destroy(mesh.first);
    v.meshes.clear();
}

void Viewer::stepFieldEffects(float dt) {
    for (cr3::GameState::FieldEffect& e : game.fieldEffects) {
        if (e.age == 0) SDL_Log("area %s: effect %s on %s", area.name.c_str(), e.model.c_str(), e.target.empty() ? "the map" : e.target.c_str());
        e.age += dt;
        auto it = fieldSparks.find(e.serial);
        if (it == fieldSparks.end()) {
            ViewSpark v;
            if (const cr3::SparkAsset* a = sparkAsset(e.model)) v.fx = std::make_unique<cr3::SparkEffect>(a, e.serial * 2654435761u);
            it = fieldSparks.emplace(e.serial, std::move(v)).first;
        }
        if (it->second.fx) it->second.fx->step(dt);
    }
    game.fieldEffects.erase(std::remove_if(game.fieldEffects.begin(), game.fieldEffects.end(),
                                           [this](const cr3::GameState::FieldEffect& e) {
                                               const auto it = fieldEffectFigs.find(e.model);
                                               const float d = it != fieldEffectFigs.end() && it->second.animLoaded ? it->second.anim.duration() : 1.0f;
                                               return !e.loop && e.age > d;
                                           }),
                            game.fieldEffects.end());
    for (auto it = fieldSparks.begin(); it != fieldSparks.end();) {
        const uint32_t serial = it->first;
        if (std::none_of(game.fieldEffects.begin(), game.fieldEffects.end(), [&](const cr3::GameState::FieldEffect& e) { return e.serial == serial; }))
            dropSpark(it->second), it = fieldSparks.erase(it);
        else
            ++it;
    }
}

// `camera`: the camera's place (the billboards')
void Viewer::drawFieldEffects(const float* vp, const cr3::Mat34& camera) {
    for (const cr3::GameState::FieldEffect& e : game.fieldEffects) {
        cr3::Vec3 at;
        if (!fieldTargetAt(e.target, at)) continue;
        auto it = fieldEffectFigs.find(e.model);
        if (it == fieldEffectFigs.end()) {
            ViewFigure f;
            std::string err;
            if (!loadFigure(e.model + ".geom", f, &err)) SDL_Log("effect %s: %s", e.model.c_str(), err.c_str());
            f.absolute = true;
            it = fieldEffectFigs.emplace(e.model, std::move(f)).first;
        }
        cr3::Mat34 place;
        for (int k = 0; k < 3; ++k) place.m[k * 5] = e.scale[size_t(k)], place.m[3 + 4 * k] = at[size_t(k)] + e.offset[size_t(k)];
        if (auto sp = fieldSparks.find(e.serial); sp != fieldSparks.end() && sp->second.fx) drawSpark(sp->second, place, camera, vp);
        ViewFigure& f = it->second;
        if (f.meshes.empty()) continue;
        f.place = place;
        const float d = f.animLoaded ? f.anim.duration() : 0.0f;
        pose(f, d > 0 ? (e.loop ? std::fmod(e.age, d) : std::min(e.age, d)) : 0.0f, false);
        drawFigure(f, vp);
    }
}

void Viewer::drawSpark(ViewSpark& v, const cr3::Mat34& root, const cr3::Mat34& camera, const float* vp) {
    sparkQuads.clear();
    v.fx->quads(root, camera, sparkQuads);
    std::map<const cr3::SparkMaterial*, std::vector<const cr3::SparkEffect::Quad*>> byMaterial;
    for (const auto& q : sparkQuads) byMaterial[q.material].push_back(&q);
    if (SDL_getenv("CR3_SPARK") && !sparkQuads.empty() && int(time * 2) != int((time - 1.0f / 60.0f) * 2)) { // (tests: twice a second)
        const auto& q = sparkQuads.back();
        const float wx = q.corner[1][0] - q.corner[0][0], wy = q.corner[1][1] - q.corner[0][1], wz = q.corner[1][2] - q.corner[0][2];
        SDL_Log("spark: %zu quads, last %s rgba %.2f %.2f %.2f %.2f, %.3f wide at (%.1f %.1f %.1f)", sparkQuads.size(),
                q.material ? q.material->name.c_str() : "-", q.color[0][0], q.color[0][1], q.color[0][2], q.color[0][3],
                std::sqrt(wx * wx + wy * wy + wz * wz), q.corner[0][0], q.corner[0][1], q.corner[0][2]);
    }
    for (auto& [mat, list] : byMaterial) {
        auto& slot = v.meshes[mat];
        if (slot.second < list.size()) { // (room for twice as many)
            dev->destroy(slot.first);
            slot.second = std::max<size_t>(list.size() * 2, 64);
            std::vector<uint32_t> idx;
            for (uint32_t k = 0; k < slot.second; ++k)
                for (const uint32_t i : {0u, 2u, 1u, 1u, 2u, 3u}) idx.push_back(k * 4 + i);
            slot.first = dev->upload(std::vector<uint8_t>(slot.second * 4 * cr3::kVertexStride, 0), idx, -1);
        }
        std::vector<uint8_t> verts(slot.second * 4 * cr3::kVertexStride, 0); // (the rest: nothing)
        static const float kUv[4][2] = {{0, 1}, {1, 1}, {0, 0}, {1, 0}};
        static const float kNormal[3] = {0, 0, 1}, kTangent[3] = {1, 0, 0}, kBinormal[3] = {0, 1, 0};
        for (size_t k = 0; k < list.size(); ++k)
            for (int c = 0; c < 4; ++c)
                packVertex(&verts[(k * 4 + size_t(c)) * cr3::kVertexStride], list[k]->corner[c], kNormal, kUv[c], list[k]->color[c], kTangent,
                           kBinormal, kUv[c]);
        dev->updateVertices(slot.first, verts);
        cr3::DrawState st;
        cr3::MeshShading sh;
        cr3::Texture* tex = mat ? texture(mat->texture) : nullptr;
        sh.texMode = tex ? 4 : -1;
        st.cull = 0;
        st.depthTest = mat ? mat->depthTest : true;
        st.depthWrite = mat ? mat->depthWrite : false;
        st.blend = !mat || mat->blend;
        const int mode = mat ? mat->blendMode : 0;
        st.blendSrc = cr3::blendCode(kGlSrcAlpha, false);
        st.blendDst = cr3::blendCode(mode == 2 ? kGlOneMinusSrcAlpha : 1u, true);
        st.blendOp = mode == 1 ? 2 : 0;
        if (mat && mat->alphaTest) sh.alphaRef = mat->alphaRef;
        dev->drawMesh(slot.first, tex, vp, sh, st);
    }
}

void Viewer::placeMover(AreaNpc& npc) {
    if (!areaP.animLoaded) return;
    areaP.place = cr3::Mat34();
    pose(areaP, npc.moveTime, false);
    cr3::Mat34 r;
    if (jointWorld(areaP, "ragdoll_" + npc.name, r)) npc.ragdoll = r;
    if (!jointWorld(areaP, npc.name, r)) return; // (its model's joint - the ragdoll's plus the first offset)
    for (int c = 0; c < 3; ++c) { // (its rotation, not the joint's scale)
        const float l = std::sqrt(r.m[c] * r.m[c] + r.m[4 + c] * r.m[4 + c] + r.m[8 + c] * r.m[8 + c]);
        if (l > 1e-6f) r.m[c] /= l, r.m[4 + c] /= l, r.m[8 + c] /= l;
    }
    npc.place = r;
}

void Viewer::stepAreaMovers(float dt) {
    for (const auto& [name, req] : game.moveRequests)
        for (AreaNpc& npc : areaNpcs)
            if (npc.mover && npc.name == name) {
                npc.moveTime = 0, npc.moveEnd = req[0] / 30.0f;
                npc.moveSpeed = req[0] != 0 && req[1] > 0 ? npc.moveEnd / (req[1] / 30.0f) : 0;
                if (req[1] <= 0) npc.moveTime = npc.moveEnd;
                npc.moving = true;
                SDL_Log("area %s: %s moves to frame %.0f over %.0f", area.name.c_str(), name.c_str(), req[0], req[1]);
            }
    game.moveRequests.clear();
    for (AreaNpc& npc : areaNpcs) {
        if (!npc.mover || !npc.moving) continue;
        npc.moveTime += npc.moveSpeed * dt;
        if (npc.moveTime >= npc.moveEnd) npc.moveTime = npc.moveEnd, npc.moving = false;
        placeMover(npc);
        if (!npc.moving) SDL_Log("area %s: %s at (%.2f %.2f %.2f)", area.name.c_str(), npc.name.c_str(), npc.place.m[3], npc.place.m[7], npc.place.m[11]);
    }
}

// the step switches (Fld2StepSwitch::CheckOnSwitch: the player's feet within 0.5 - the field
// free -: down - its _n01 played, SE 0x24e -, and at the motion's end its function, once; a
// SwitchOn from the area's script has it down already)
void Viewer::stepStepSwitches() {
    const cr3::Vec3 p = areaWalker->position();
    const float feet[3] = {p[0], p[1] - areaWalker->radius, p[2]};
    for (AreaNpc& npc : areaNpcs) {
        if (!npc.stepSwitch || !game.gimmickVisible(npc.name)) continue;
        if (npc.stepState == 0 && game.switchesOn.count(npc.name)) npc.stepState = 2;
        if (npc.stepState == 0 && !(areaEvents && areaEvents->running())) {
            const float dx = feet[0] - npc.place.m[3], dy = feet[1] - npc.place.m[7], dz = feet[2] - npc.place.m[11];
            if (std::sqrt(dx * dx + dy * dy + dz * dz) <= 0.5f) {
                npc.stepState = 1, npc.stepAt = areaClock;
                audio.playSe(0x24e, false);
                SDL_Log("area %s: %s stood on", area.name.c_str(), npc.name.c_str());
            }
        }
        const float dur = npc.body.animLoaded ? npc.body.anim.duration() : 0.0f;
        if (npc.stepState == 1 && areaClock - npc.stepAt >= dur) {
            npc.stepState = 2;
            game.switchesOn.insert(npc.name);
            fieldPendingFunctions.push_back(npc.name); // (SetScript)
        }
    }
}

void Viewer::updateGimmickWalls() {
    std::string key;
    for (const AreaNpc& npc : areaNpcs)
        if (npc.solid && !npc.broken && game.gimmickVisible(npc.name)) key += npc.name + ";";
    if (key == areaWallKey) return;
    areaWallKey = key;
    area.walls = areaBaseWalls;
    for (const AreaNpc& npc : areaNpcs) {
        if (!npc.solid || npc.broken || !game.gimmickVisible(npc.name)) continue;
        for (const cr3::PhysBody& b : areaPhysP.bodies)
            if (b.name == npc.name) {
                cr3::PhysScene one = areaPhysP;
                one.bodies = {b};
                one.bodies[0].contactResponse = true;
                area.walls.addScene(one);
            }
    }
}

float Viewer::fieldAnimDuration(const std::string& model) {
    auto it = fieldEffectFigs.find(model);
    if (it == fieldEffectFigs.end()) {
        ViewFigure f;
        std::string err;
        if (!loadFigure(model + ".geom", f, &err)) SDL_Log("field: %s: %s", model.c_str(), err.c_str());
        f.absolute = true;
        it = fieldEffectFigs.emplace(model, std::move(f)).first;
    }
    return it->second.animLoaded ? it->second.anim.duration() : 0.0f;
}

void Viewer::putBomb() {
    for (FieldBomb& b : fieldBombs) {
        if (b.on) continue;
        const cr3::Vec3 p = areaWalker->position();
        b = FieldBomb();
        b.on = true, b.stage = 1, b.pos = {p[0], p[1] - areaWalker->radius, p[2]};
        const bool first = std::none_of(fieldBombs.begin(), fieldBombs.end(), [&](const FieldBomb& o) { return &o != &b && o.on && o.stage == 1; });
        audio.playSe("fe_011", false);
        if (first) audio.playSe("fe_012", true);
        SDL_Log("area %s: a bomb put at (%.2f %.2f %.2f)", area.name.c_str(), b.pos[0], b.pos[1], b.pos[2]);
        return;
    }
}

float Viewer::bombDist(const cr3::Vec3& a, const cr3::Vec3& b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

void Viewer::stepFieldBombs(float dt) {
    const float fuseEnd = std::min(fieldAnimDuration("fe002bom01"), 1.5f), blastEnd = fieldAnimDuration("fe002bom02");
    for (FieldBomb& b : fieldBombs) {
        if (!b.on) continue;
        b.t += dt;
        if (b.stage == 1) {
            if (b.chained) b.chainT += dt;
            if ((fuseEnd > 0 && b.t >= fuseEnd) || b.t >= 1.5f || (b.chained && b.chainT >= b.chainDelay)) {
                b.stage = 2, b.t = 0, b.blasting = true, b.fresh = true;
                b.yaw = float(std::rand()) / float(RAND_MAX) * 3.14159f;
                if (std::none_of(fieldBombs.begin(), fieldBombs.end(), [](const FieldBomb& o) { return o.on && o.stage == 1; })) audio.stopSe("fe_012", 0);
                audio.playSe("fe_013", false);
                SDL_Log("area %s: a bomb goes off", area.name.c_str());
            }
        } else if (b.stage == 2) {
            if (int(b.t / (1.0f / 30)) > 14) b.stage = 3, b.blasting = false;
        } else if (b.stage == 3 && b.t >= std::max(blastEnd, 0.5f)) {
            b = FieldBomb();
        }
    }
    for (FieldBomb& b : fieldBombs) // (CheckBlastChain: a blast sets off the bombs within 1, one after another)
        if (b.on && b.blasting)
            for (FieldBomb& o : fieldBombs)
                if (&o != &b && o.on && o.stage == 1 && !o.chained && bombDist(o.pos, b.pos) < 1.0f) {
                    int n = 1;
                    for (const FieldBomb& c : fieldBombs) n += c.chained ? 1 : 0;
                    o.chained = true, o.chainT = 0, o.chainDelay = float(n) * 0.4f;
                }
    // (Fld2Rock::UpdateBlast: a blast within its radius - the quake, its function, its body gone, its chips)
    for (AreaNpc& npc : areaNpcs) {
        if (!npc.rock || npc.broken || !game.gimmickVisible(npc.name)) continue;
        const cr3::Vec3 at{npc.place.m[3], npc.place.m[7], npc.place.m[11]};
        const bool hit = std::any_of(fieldBombs.begin(), fieldBombs.end(), [&](const FieldBomb& b) { return b.on && b.blasting && bombDist(b.pos, at) <= npc.blastRadius; });
        if (!hit) continue;
        npc.broken = true, npc.brokenAt = areaClock;
        game.brokenRocks.insert(area.name + ":" + npc.name);
        startQuake(20, 1, 0, 30); // (Fld2Rock::UpdateBlast: StartQuake(20, 1, 0, 30))
        auto rnd = []() { return float(std::rand()) / float(RAND_MAX); };
        for (int k = 0; k < 30; ++k) {
            AreaNpc::Chip c;
            c.big = k < 10;
            c.pos = {at[0] + rnd() * 0.6f - 0.3f, at[1] + rnd() * 0.4f + 0.1f, at[2] + rnd() * 0.6f - 0.3f};
            const float sp = (c.big ? rnd() * 0.4f + 0.6f : rnd() * 0.8f + 0.2f) * 20;
            c.vel = {(rnd() * 0.4f - 0.2f) * sp, (rnd() * 0.2f + 0.5f) * sp, (rnd() * 0.4f - 0.2f) * sp};
            c.rx = rnd() * 3.14159f, c.ry = rnd() * 3.14159f;
            c.vrx = (rnd() * 0.5f + 0.5f) * (rnd() < 0.51f ? 1 : -1) * 10, c.vry = (rnd() * 0.5f + 0.5f) * (rnd() < 0.51f ? 1 : -1) * 10;
            c.scale = c.big ? rnd() * 0.3f + 0.5f : rnd() * 0.4f + 0.1f;
            npc.chips.push_back(c);
        }
        SDL_Log("area %s: %s broken", area.name.c_str(), npc.name.c_str());
        fieldPendingFunctions.push_back(npc.name); // (SetScript: its function - krock_11: flag 4034, SE fe_014 - once the area's script is free)
    }
    // (Fld2PhysicalSwitch::Update: a bomb going off within the row's +0x34 of it -
    // CheckBlastObjectwithSwitchTiming - turns it: Exchange, the other model, SE 0x24f, its function)
    for (AreaNpc& npc : areaNpcs) {
        if (!npc.bombSwitch || !game.gimmickVisible(npc.name)) continue;
        const cr3::Vec3 at{npc.place.m[3], npc.place.m[7], npc.place.m[11]};
        if (std::none_of(fieldBombs.begin(), fieldBombs.end(), [&](const FieldBomb& b) { return b.on && b.fresh && bombDist(b.pos, at) <= npc.blastRadius; })) continue;
        if (!game.switchesOn.erase(npc.name)) game.switchesOn.insert(npc.name);
        audio.playSe(0x24f, false);
        SDL_Log("area %s: %s turned %s", area.name.c_str(), npc.name.c_str(), game.switchesOn.count(npc.name) ? "on" : "off");
        fieldPendingFunctions.push_back(npc.name); // (SetScript)
    }
    for (FieldBomb& b : fieldBombs) b.fresh = false;
    for (AreaNpc& npc : areaNpcs) { // (the chips: 0.98 down an update - 30 a second -, one bounce at the rock's foot)
        if (npc.chips.empty()) continue;
        const float age = areaClock - npc.brokenAt;
        if (age > 1.3f) {
            npc.chips.clear();
            continue;
        }
        for (AreaNpc::Chip& c : npc.chips) {
            c.vel[1] -= 0.98f * dt * 30;
            for (int a = 0; a < 3; ++a) c.pos[size_t(a)] += c.vel[size_t(a)] * dt;
            c.rx += c.vrx * dt, c.ry += c.vry * dt;
            if (!c.bounced && c.vel[1] < 0 && c.pos[1] < npc.place.m[7]) c.bounced = true, c.vel = {c.vel[0] * 0.7f, -c.vel[1] * 0.7f, c.vel[2] * 0.7f};
        }
    }
}

void Viewer::drawFieldBombs(const float* vp) {
    for (const FieldBomb& b : fieldBombs) {
        if (!b.on) continue;
        const std::string model = b.stage == 1 ? "fe002bom01" : "fe002bom02";
        fieldAnimDuration(model);
        ViewFigure& f = fieldEffectFigs[model];
        if (f.meshes.empty()) continue;
        f.place = cr3::Mat34();
        const float c = std::cos(b.yaw), sn = std::sin(b.yaw);
        f.place.m[0] = c, f.place.m[2] = sn, f.place.m[8] = -sn, f.place.m[10] = c;
        f.place.m[3] = b.pos[0], f.place.m[7] = b.pos[1], f.place.m[11] = b.pos[2];
        const float d = f.animLoaded ? f.anim.duration() : 0.0f;
        pose(f, d > 0 ? std::min(b.t, d) : 0.0f, false);
        drawFigure(f, vp);
    }
    for (const AreaNpc& npc : areaNpcs) {
        if (npc.chips.empty()) continue;
        const std::string base = npc.model.size() > 2 ? npc.model.substr(0, npc.model.size() - 2) : npc.model;
        const float alpha = std::clamp(1.2f - (areaClock - npc.brokenAt), 0.0f, 1.0f);
        for (const AreaNpc::Chip& c : npc.chips) {
            const std::string model = base + (c.big ? "01" : "02");
            fieldAnimDuration(model);
            ViewFigure& f = fieldEffectFigs[model];
            if (f.meshes.empty()) continue;
            const float cx = std::cos(c.rx), sx = std::sin(c.rx), cy = std::cos(c.ry), sy = std::sin(c.ry);
            f.place = cr3::Mat34(); // (turned about x, then y; scaled)
            const float r[9] = {cy, sx * sy, cx * sy, 0, cx, -sx, -sy, sx * cy, cx * cy};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) f.place.m[i * 4 + j] = r[i * 3 + j] * c.scale;
            f.place.m[3] = c.pos[0], f.place.m[7] = c.pos[1], f.place.m[11] = c.pos[2];
            f.tint[3] = alpha;
            pose(f, 0, false);
            drawFigure(f, vp);
            f.tint[3] = 1;
        }
    }
}

void Viewer::setupAreaGimmicks(const std::string& name) {
    for (AreaNpc& npc : areaNpcs) unload(npc.body), unload(npc.head), unload(npc.alt);
    areaNpcs.clear();
    areaGimmicks.clear(), areaCheckNear = -1;
    std::vector<uint8_t> d;
    cr3::Skeleton sk;
    if (!archive.read(name + "p.skel", d) || !cr3::parseSkeleton(d, sk)) return;
    std::vector<std::string> names;
    if (archive.read(name + "p.name", d)) names = cr3::parseJointNames(d);
    const std::vector<cr3::Mat34> w = sk.world(sk.basePose);
    for (size_t j = 0; j < names.size() && j < w.size(); ++j)
        if (names[j].compare(0, 11, "ragdoll_evt") == 0 || names[j].compare(0, 12, "ragdoll_cobj") == 0 ||
            names[j].compare(0, 11, "ragdoll_chk") == 0 ||
            names[j].compare(0, 12, "ragdoll_kbox") == 0 || // (a key box: Fld2Box kind 2 - its function, SetScript)
            (names[j].size() > 9 && names[j].compare(0, 8, "ragdoll_") == 0 && std::strchr("cjm", names[j][8]) &&
             std::isdigit(static_cast<unsigned char>(names[j][9]))))
            // (event boxes, check objects, and the people: ragdoll_c<id>_<nn> - c115_00 -, the
            // party's j<id>_00, the monsters' m<id>_<nn>, each's function named so, tapped like
            // a check object)
            areaGimmicks.push_back({names[j].substr(8), names[j].compare(0, 11, "ragdoll_evt") == 0, w[j], false, {}});
        else if (names[j].compare(0, 11, "ragdoll_ac_") == 0) // (the way on to another area: ac_t0202_p1)
            areaGimmicks.push_back({names[j].substr(8), false, w[j], false, names[j].substr(11)});
        else if (const int b = drawnBox(names[j]); b >= 0) { // (a treasure / gold box the draw filled: Fld2Box, its event OpenTBox / OpenGBox)
            AreaGimmick g{game.cr3Boxes[size_t(b)].type == 1 ? "OpenGBox" : "OpenTBox", false, w[j], false, {}};
            g.box = b, g.boxName = names[j].substr(8);
            areaGimmicks.push_back(g);
        }
    loadAreaNpcs(name, names, w);
    loadMinimap(name, names, w);
    unload(areaP), areaP = ViewFigure();
    std::string err;
    if (std::any_of(areaNpcs.begin(), areaNpcs.end(), [](const AreaNpc& n) { return n.mover || n.wander == 2 || n.wander == 3; }) && loadFigure(name + "p.geom", areaP, &err)) {
        areaP.absolute = true; // (CrxSimpleModel: its own animation)
        for (size_t k = 0; k < areaP.anims.size(); ++k)
            if (areaP.anims[k] == name + "p.anim") {
                const float keep = time;
                loadAnim(areaP, k);
                time = keep;
            }
    }
    // (the solid gimmicks' bodies - rocks, boxes, check objects: their <area>p.phys bodies,
    // walls while they stand - updateGimmickWalls)
    areaPhysP = cr3::PhysScene(), areaWallKey = "~";
    if (archive.read(name + "p.phys", d)) cr3::parsePhys(d, areaPhysP);
    cr3::PhysScene phys; // (the area changes' boxes: their bodies, named as they are)
    if (std::any_of(areaGimmicks.begin(), areaGimmicks.end(), [](const AreaGimmick& g) { return !g.areaChange.empty(); }) &&
        archive.read(name + "p.phys", d) && cr3::parsePhys(d, phys))
        for (AreaGimmick& g : areaGimmicks)
            for (const cr3::PhysBody& b : phys.bodies)
                if (!g.areaChange.empty() && b.name == g.function && b.shape < phys.shapes.size()) {
                    const cr3::PhysShape& sh = phys.shapes[b.shape];
                    if (sh.kind == cr3::PhysShape::Box) {
                        for (int a = 0; a < 3; ++a) g.boxLo[size_t(a)] = -sh.halfExtents[size_t(a)], g.boxHi[size_t(a)] = sh.halfExtents[size_t(a)];
                    } else if (sh.kind == cr3::PhysShape::Mesh && !sh.vertices.empty()) {
                        g.boxLo = g.boxHi = sh.vertices[0];
                        for (const cr3::Vec3& v : sh.vertices)
                            for (int a = 0; a < 3; ++a)
                                g.boxLo[size_t(a)] = std::min(g.boxLo[size_t(a)], v[size_t(a)]), g.boxHi[size_t(a)] = std::max(g.boxHi[size_t(a)], v[size_t(a)]);
                    } else continue;
                    g.shaped = true, g.boxAt = b.position, g.boxTurn = b.rotation;
                }
    for (const AreaGimmick& g : areaGimmicks) // (where: for --walk tests)
        SDL_Log("area %s: %s %s at (%.2f %.2f %.2f)", name.c_str(),
                !g.areaChange.empty() ? "area change" : g.eventBox ? "event box" : "check object", g.function.c_str(),
                g.world.m[3], g.world.m[7], g.world.m[11]);
    for (const AreaGimmick& g : areaGimmicks)
        if (g.shaped) SDL_Log("area %s: %s's bounds (%.2f %.2f %.2f) .. (%.2f %.2f %.2f) from (%.2f %.2f %.2f)", name.c_str(), g.function.c_str(), g.boxLo[0],
                              g.boxLo[1], g.boxLo[2], g.boxHi[0], g.boxHi[1], g.boxHi[2], g.boxAt[0], g.boxAt[1], g.boxAt[2]);
}

// a box opened by its script (Fld2Box::Open: _a01 rewound, not looped; SE 0x251; fe101get01 over
// it) and let go (Update: 0.1 less alpha each update, then gone)
void Viewer::stepAreaBox(AreaNpc& npc, float dt) {
    const auto it = game.fieldBoxes.find(npc.name);
    const int state = it == game.fieldBoxes.end() ? 0 : it->second;
    if (state >= 1 && npc.openAt < 0) {
        npc.openAt = areaClock;
        const std::string want = npc.model + "_a01.anim";
        const auto at = std::find(npc.body.anims.begin(), npc.body.anims.end(), want);
        if (at != npc.body.anims.end()) {
            const float keep = time;
            loadAnim(npc.body, size_t(at - npc.body.anims.begin()));
            time = keep;
        }
        audio.playSe(0x251, false);
        if (!boxGetTried) {
            boxGetTried = true;
            std::string err;
            if (!loadFigure("fe101get01.geom", boxGet, &err)) SDL_Log("box: fe101get01: %s", err.c_str());
        }
        SDL_Log("area %s: %s opened", area.name.c_str(), npc.name.c_str());
    }
    if (state == 2 && npc.fade > 0) npc.fade = std::max(npc.fade - 3.0f * dt, 0.0f); // (0.1 an update, 30 a second)
}

void Viewer::loadAreaNpcs(const std::string& name, const std::vector<std::string>& joints, const std::vector<cr3::Mat34>& world) {
    std::vector<uint8_t> d;
    cr3::MbeFile f;
    if (!archive.read("data\\field_" + name.substr(0, 3) + ".mbe", d) || !f.parse(d)) return;
    const cr3::MbeFile::Table* t = f.table("Field");
    if (!t) return;
    const int areaNo = std::atoi(name.substr(3, 2).c_str()) - 1;
    auto questActive = [&](int q) { return q >= 0 && game.cr3Quests.begun.count(q) && !game.cr3Quests.ended.count(q); };
    // (GetFieldParamByName: an active quest's Field row for a gimmick overrides the -1 default row)
    std::vector<cr3::MbeFile::Row> rows;
    std::set<std::string> seenGimmicks;
    for (int pass = 0; pass < 2; ++pass)
        for (size_t r = 0; r < t->rows.size(); ++r) {
            cr3::MbeFile::Row row;
            if (!f.row(*t, r, "iiiiiippiiif", row) || row.i(0) != areaNo) continue;
            const int q = row.i(4);
            if (pass == 0 ? !questActive(q) : q != -1) continue;
            if (seenGimmicks.insert(row.s(6)).second) rows.push_back(row);
        }
    for (const cr3::MbeFile::Row& row : rows) {
        const std::string gimmick = row.s(6), model = row.s(7);
        if (gimmick.compare(0, 8, "ragdoll_") != 0) continue;
        if (row.f(11) > 0)
            for (AreaGimmick& g : areaGimmicks)
                if (g.function == gimmick.substr(8)) g.checkRange = row.f(11);
        if (model.size() < 4) continue;
        // (a treasure or gift box only where its dungeon's draw gave it something - Fld2Box::Create:
        // GetTBoxData - the draws come with a level from the world select: not yet)
        const bool drawn = drawnBox(gimmick) >= 0;
        if ((gimmick.compare(0, 12, "ragdoll_tbox") == 0 || gimmick.compare(0, 12, "ragdoll_gbox") == 0) && !drawn) continue;
        AreaNpc npc;
        npc.name = gimmick.substr(8), npc.model = model;
        const auto it = std::find(joints.begin(), joints.end(), npc.name);
        if (it == joints.end() || size_t(it - joints.begin()) >= world.size()) continue;
        npc.place = world[size_t(it - joints.begin())];
        const bool whole = archive.contains(model + ".geom"); // (an object's: t0100sign00)
        const std::string base = whole ? model : model.substr(0, 4);
        std::string body = base, head;
        if (!whole && !archive.contains(base + ".geom") && archive.contains(base + "b01.geom")) {
            body = base[0] == 'j' ? cr3PartsBody(base) : base + "b01";
            if (!archive.contains(body + ".geom")) body = base + "b01";
            if (archive.contains(base + "h01.geom")) head = base + "h01";
        }
        std::string err;
        figureVariant = !whole && model.size() > 4 ? model.substr(4, 1) : std::string();
        const bool ok = loadFigure(body + ".geom", npc.body, &err);
        if (ok && !head.empty()) loadFigure(head + ".geom", npc.head, &err);
        figureVariant.clear();
        if (!ok) {
            SDL_Log("area %s: person %s (%s): %s", name.c_str(), npc.name.c_str(), model.c_str(), err.c_str());
            continue;
        }
        for (ViewFigure* fp : {&npc.body, &npc.head}) { // (fn01, looped)
            ViewFigure& fig = *fp;
            if (fig.meshes.empty()) continue;
            const std::string want = motionBase(fig.name.substr(0, fig.name.size() - 5)) + "_fn01.anim";
            const auto at = std::find(fig.anims.begin(), fig.anims.end(), want);
            if (at == fig.anims.end()) continue;
            const float keep = time;
            loadAnim(fig, size_t(at - fig.anims.begin()));
            time = keep;
        }
        npc.box = gimmick.compare(0, 12, "ragdoll_kbox") == 0 || drawn;
        npc.solid = row.i(9) != 0;
        npc.rock = gimmick.compare(0, 13, "ragdoll_krock") == 0;
        npc.bombSwitch = gimmick.compare(0, 12, "ragdoll_bswc") == 0;
        npc.stepSwitch = gimmick.compare(0, 12, "ragdoll_fswc") == 0;
        npc.mover = gimmick.compare(0, 13, "ragdoll_fmove") == 0;
        if (npc.mover) {
            const auto rj = std::find(joints.begin(), joints.end(), gimmick);
            if (rj != joints.end() && size_t(rj - joints.begin()) < world.size()) npc.ragdoll = world[size_t(rj - joints.begin())];
            else npc.ragdoll = npc.place;
        }
        if (npc.stepSwitch) // (its going down: <model>_n01, slot 1)
            for (size_t k = 0; k < npc.body.anims.size(); ++k)
                if (npc.body.anims[k] == model + "_n01.anim") {
                    const float keep = time;
                    loadAnim(npc.body, k);
                    time = keep;
                }
        if (npc.bombSwitch && model.size() > 2) loadFigure(model.substr(0, model.size() - 2) + "03.geom", npc.alt, &err);
        npc.blastRadius = npc.talkRange = row.f(11);
        npc.wander = row.i(10);
        for (const char* m : {"j001", "j002", "j003", "j004", "j005", "c112", "c127"}) // (Fld2NPCIdle2Name - Fld2NPC::Build)
            if (model.compare(0, 4, m) == 0) npc.idle2 = true;
        npc.idle2Left = 10.0f + 5.0f * float(std::rand()) / float(RAND_MAX);
        if (npc.wander >= 4 && npc.wander <= 5) {
            const auto rj = std::find(joints.begin(), joints.end(), "ragdoll_npc_" + npc.name);
            if (rj != joints.end() && size_t(rj - joints.begin()) < world.size()) {
                cr3::Mat34 r = world[size_t(rj - joints.begin())];
                float sc[3];
                for (int c = 0; c < 3; ++c) { // (GetScaleFromFigure x 0.1: the sides; the turn alone kept)
                    sc[c] = std::sqrt(r.m[c] * r.m[c] + r.m[4 + c] * r.m[4 + c] + r.m[8 + c] * r.m[8 + c]);
                    if (sc[c] > 1e-6f) r.m[c] /= sc[c], r.m[4 + c] /= sc[c], r.m[8 + c] /= sc[c];
                }
                npc.rect = r, npc.rectW = sc[0] * 0.1f, npc.rectD = sc[2] * 0.1f;
            }
            npc.walkSpeed = (npc.wander == 5 ? 4.5f : 0.75f) * (model.compare(0, 4, "c553") == 0 ? 2.0f / 3.0f : 1.0f);
        }
        npc.broken = npc.rock && game.brokenRocks.count(name + ":" + npc.name);
        npc.phase = float(areaNpcs.size()) * 0.37f;
        areaNpcs.push_back(std::move(npc));
    }
    if (!areaNpcs.empty()) SDL_Log("area %s: %zu people and objects", name.c_str(), areaNpcs.size());
}

void Viewer::areaScriptCamera(const std::array<float, 3>& eye, const std::array<float, 3>& at, float frames) {
    if (!areaScriptCam.on) areaScriptCam.on = true, areaScriptCam.eye = areaCamera.eye, areaScriptCam.at = areaCamera.at;
    areaScriptCam.toEye = eye, areaScriptCam.toAt = at, areaScriptCam.left = std::max(frames, 0.0f) / 30.0f;
    if (areaScriptCam.left <= 0) areaScriptCam.eye = eye, areaScriptCam.at = at;
}

void Viewer::stepAreaScriptCam(float dt) {
    AreaScriptCam& c = areaScriptCam;
    if (!c.on || c.left <= 0) return;
    const float k = std::min(dt / c.left, 1.0f);
    for (int a = 0; a < 3; ++a) c.eye[a] += (c.toEye[a] - c.eye[a]) * k, c.at[a] += (c.toAt[a] - c.at[a]) * k;
    c.left -= dt;
}

// (StartQuake / StopQuake: the field's, and a vista's - QuakeControlTask shakes whatever's drawn)
// (QuakeControlTask::Start 0x00944188 from utils::StartQuake 0x0092d33c: width, from, to, and
// frames x GameSystem +0x148 - 1/30 s -, the time and the way reset)
void Viewer::startQuake(float width, float from, float to, float frames) {
    const float sinceUpdate = fieldQuake.sinceUpdate;
    fieldQuake = FieldQuake(); // (+0xc4 time, +0xcc way and +0xd0 drawn cleared)
    fieldQuake.on = true, fieldQuake.width = width, fieldQuake.from = from, fieldQuake.to = to;
    fieldQuake.seconds = frames / 30.0f, fieldQuake.sinceUpdate = sinceUpdate;
    SDL_Log("quake: %g wide, %g -> %g over %g frames", double(width), double(from), double(to), double(frames));
}

// (QuakeControlTask::OnUpdate 0x00944084, at each of the game's 60-Hz ChildUpdates - the time
// only at every other's 1/30 s: the time on, off once it reaches seconds - unless those are
// below 0 -, held at |seconds|; the way flipped when a frame was drawn since the last)
void Viewer::stepQuake(float dt) {
    if (!fieldQuake.on) return;
    fieldQuake.time += dt;
    if (fieldQuake.seconds >= 0 && fieldQuake.time >= fieldQuake.seconds) fieldQuake.on = false;
    fieldQuake.time = std::min(fieldQuake.time, std::fabs(fieldQuake.seconds));
    for (fieldQuake.sinceUpdate += dt; fieldQuake.sinceUpdate >= 1.0f / 60.0f; fieldQuake.sinceUpdate -= 1.0f / 60.0f)
        if (fieldQuake.drawn) fieldQuake.drawn = false, fieldQuake.parity = (fieldQuake.parity + 1) % 2;
    fieldQuake.sinceUpdate = std::min(fieldQuake.sinceUpdate, 1.0f / 60.0f);
}

void Viewer::bindQuakeNatives(cr3::ScriptVM& vm) {
    vm.setNative("Cr3System", "StartQuake", [this](cr3::NativeCall& c) {
        startQuake(float(c.number(0)), float(c.number(1)), float(c.number(2)), float(c.number(3)));
    });
    vm.setNative("Cr3System", "StopQuake", [this](cr3::NativeCall&) { fieldQuake.on = false; }); // (QuakeControlTask::Stop)
}

// (QuakeControlTask::Render 0x009440f8 on layers 5..9 and 0 - GameMain's draw -: the layer's
// RenderLayerScreen y offset (+0x88, then the camera's +0x24c) = width x (from + (to - from) x
// time / |seconds|), down on the way's 0, up on its 1; MVGL Camera::GetScreenMatrix 0x00b8f184
// moves clip space by that / DisplayInfo's height. That height the reference playthrough's: its
// boss deaths' StartQuake(10, 0.2, 1.0, 143) measure 4.95 px of 720 at the end - 10 / 727)
void Viewer::applyQuake(Mat4& proj) {
    if (!fieldQuake.on) return;
    constexpr float kQuakeDisplayHeight = 720.0f;
    const float span = std::fabs(fieldQuake.seconds);
    const float k = fieldQuake.from + (fieldQuake.to - fieldQuake.from) * (span > 0 ? fieldQuake.time / span : 0.0f);
    Mat4 shift{};
    shift[0] = shift[5] = shift[10] = shift[15] = 1;
    shift[13] = (fieldQuake.parity ? 1.0f : -1.0f) * fieldQuake.width * k / kQuakeDisplayHeight;
    proj = cr3::mat4Multiply(shift, proj);
    fieldQuake.drawn = true;
}

void Viewer::bindAreaNatives(cr3::ScriptVM& vm) {
    vm.setNative("Cr3FieldMapCamera", "SetScriptCamera", [this](cr3::NativeCall& c) {
        areaScriptCamera({float(c.number(0)), float(c.number(1)), float(c.number(2))}, {float(c.number(3)), float(c.number(4)), float(c.number(5))},
                         float(c.number(6)));
    });
    vm.setNative("Cr3FieldMapCamera", "SetScriptCameraPosition", [this](cr3::NativeCall& c) {
        areaScriptCamera({float(c.number(0)), float(c.number(1)), float(c.number(2))}, areaScriptCam.on ? areaScriptCam.toAt : areaCamera.at,
                         float(c.number(3)));
    });
    vm.setNative("Cr3FieldMapCamera", "SetScriptCameraTarget", [this](cr3::NativeCall& c) {
        areaScriptCamera(areaScriptCam.on ? areaScriptCam.toEye : areaCamera.eye, {float(c.number(0)), float(c.number(1)), float(c.number(2))},
                         float(c.number(3)));
    });
    vm.setNative("Cr3FieldMapCamera", "ResetScriptCamera", [this](cr3::NativeCall&) { areaScriptCam = AreaScriptCam(); });
    bindQuakeNatives(vm);
    // (RecoverBonusMPPreparation / RecoverBonusMP - InterfaceMain's: the camp's members' most MP
    // kept, then each whose most grew - the team level's - has its MP that much more, saved)
    vm.setNative("InterfaceWindow", "RecoverBonusMPPreparation", [this](cr3::NativeCall&) {
        bonusMpBefore.clear();
        cr3::Cr3Fighter cf;
        for (int ch : cr3PartyMembers())
            if (cr3CharaStats(ch, cf)) bonusMpBefore[ch] = cf.maxMp;
    });
    vm.setNative("InterfaceWindow", "RecoverBonusMP", [this](cr3::NativeCall&) {
        cr3::Cr3Fighter cf;
        for (const auto& [ch, before] : bonusMpBefore)
            if (cr3CharaStats(ch, cf) && cf.maxMp > before) {
                const int mp = std::min(cr3CharaMp(ch, before) + cf.maxMp - before, cf.maxMp);
                if (mp >= cf.maxMp) game.cr3Mp.erase(ch);
                else game.cr3Mp[ch] = mp;
                SDL_Log("bonus MP: %d %d -> %d / %d", ch, before, mp, cf.maxMp);
            }
        bonusMpBefore.clear();
    });
    vm.setNative("Cr3GimmickData", "SetMotion", [this](cr3::NativeCall& c) { areaMotions[c.string(0)] = c.string(1); });
    vm.setNative("Cr3GimmickData", "NPCIdle2Enable", [this](cr3::NativeCall& c) { // (Fld2SetIdle2Enable)
        for (AreaNpc& npc : areaNpcs)
            if (npc.name == c.string(0)) npc.idle2On = c.boolean(1);
    });
    vm.setNative("Cr3GimmickData", "NPCMove", [this](cr3::NativeCall& c) { // (Fld2SetNPCMove - Fld2NPC::SetMove: false holds it where it is)
        for (AreaNpc& npc : areaNpcs)
            if (npc.name == c.string(0)) npc.held = !c.boolean(1), npc.wait = 0;
    });
    // (ChangeDirection(name, target, frames) - Fld2ChangeNPCDirection, Fld2DirectionManager::
    // SetDirection: the person turned about y toward "player" or another person - Cr3GetDirY,
    // atan2(dx, dz) -, slerped over that many updates)
    vm.setNative("Cr3GimmickData", "ChangeDirection", [this](cr3::NativeCall& c) {
        const std::string name = c.string(0), target = c.string(1);
        std::array<float, 3> to{};
        bool found = false;
        if (target == "player" && areaWalker) {
            const cr3::Vec3 p = areaWalker->position();
            to = {p[0], p[1], p[2]}, found = true;
        } else
            for (const AreaNpc& o : areaNpcs)
                if (o.name == target) to = {o.place.m[3], o.place.m[7], o.place.m[11]}, found = true;
        if (!found) return;
        for (AreaNpc& npc : areaNpcs) {
            if (npc.name != name) continue;
            npc.turnFrom = std::atan2(npc.place.m[2], npc.place.m[0]);
            npc.turnTo = std::atan2(to[0] - npc.place.m[3], to[2] - npc.place.m[11]);
            npc.turnFrames = std::max(c.integer(2, 1), 1), npc.turnStep = 0;
        }
    });
    vm.setNative("Cr3GimmickData", "SetMapScrollSpeed", [this](cr3::NativeCall& c) {
        const uint32_t hash = cr3::nameHash(c.string(0).c_str());
        const std::vector<float> speed = {float(c.number(1)), float(c.number(2))};
        int n = 0;
        for (ViewFigure& f : areaFigures)
            for (ViewMesh& vm : f.meshes)
                if (vm.baseMaterial.hash == hash) vm.baseMaterial.params[0x55] = speed, vm.material.params[0x55] = speed, ++n;
        SDL_Log("area %s: %s scrolls (%.2f %.2f) on %d meshes", area.name.c_str(), c.string(0).c_str(), double(speed[0]), double(speed[1]), n);
    });
}

void Viewer::stepAreaTurns(float dt) {
    areaTurnClock += dt;
    for (; areaTurnClock >= cr3::EventRuntime::kTick; areaTurnClock -= cr3::EventRuntime::kTick)
        for (AreaNpc& npc : areaNpcs) {
            if (npc.turnStep >= npc.turnFrames) continue;
            ++npc.turnStep;
            const float k = std::min(float(npc.turnStep) / float(npc.turnFrames), 1.0f);
            const float yaw = npc.turnFrom + std::remainder(npc.turnTo - npc.turnFrom, 6.2831853f) * k;
            const float sc = std::hypot(npc.place.m[0], npc.place.m[8]) > 1e-6f ? std::hypot(npc.place.m[0], npc.place.m[8]) : 1.0f;
            const float c = std::cos(yaw) * sc, sn = std::sin(yaw) * sc;
            cr3::Mat34& m = npc.place;
            m.m[0] = c, m.m[1] = 0, m.m[2] = sn;
            m.m[4] = 0, m.m[5] = sc, m.m[6] = 0;
            m.m[8] = -sn, m.m[9] = 0, m.m[10] = c;
        }
}

// a person's or object's animation by its motion's name (its own, <model>_, <base>_), from now
bool Viewer::setNpcAnim(AreaNpc& npc, const std::string& motion) {
    bool found = false;
    for (ViewFigure* fp : {&npc.body, &npc.head}) {
        ViewFigure& f = *fp;
        if (f.meshes.empty()) continue;
        const std::string base = motionBase(f.name.substr(0, f.name.size() - 5));
        for (const std::string& want : {motion + ".anim", npc.model + "_" + motion + ".anim", base + "_" + motion + ".anim"}) {
            const auto at = std::find(f.anims.begin(), f.anims.end(), want);
            if (at == f.anims.end()) continue;
            const float keep = time;
            loadAnim(f, size_t(at - f.anims.begin()));
            time = keep;
            found = true;
            break;
        }
    }
    npc.motionAt = areaClock, npc.eventMotion = found;
    return found;
}

// The people who walk about (Fld2NPC: the Field row's +0x30 - 4 walking, 5 running, within the
// rectangle of their ragdoll_npc_<name> joint - its place, turn and scale x 0.1 the sides -;
// MoveRect / CheckMoveRect): a point drawn in it at random, turned to over 4 updates
// (Fld2DirectionManager) with ff01 / ff02 (Fld2NPCAnimeNameTbl 2 / 3), walked to straight ahead
// at 0.75 / 4.5 a second (0.5 / 3 for c553) - SetMoveSpeed's table -, set on the ground; there
// (within 0.4) or after 10 s: fn01 and a wait of 3 .. 7 s (5 + 0, -1, -2, 1, 2, 2 by rand % 6).
// The player within the row's +0x34 (Check: +0x1f0) or Cr3GimmickData.NPCMove(name, false)
// (SetMove: +0x1f1) stops them where they are
// (+0x30 2 / 3 - MovePass: on its own joint of <area>p - Cr3GetPositionFromFigure -, the
// animation run so it goes 0.75 / 4.5 a second - SetMoveSpeed: the joint's way over the
// animation's first second -, looped; set on the ground, turned to the way it goes; the player
// near, or NPCMove false: fn01, held at that time - CheckMovePass goes on from there)
bool Viewer::areaPassJoint(AreaNpc& npc, float t, std::array<float, 3>& at) {
    if (!areaP.animLoaded) return false;
    areaP.place = cr3::Mat34();
    pose(areaP, t, true);
    cr3::Mat34 r;
    if (!jointWorld(areaP, npc.name, r) && !jointWorld(areaP, "ragdoll_" + npc.name, r)) return false;
    at = {r.m[3], r.m[7], r.m[11]};
    return true;
}

void Viewer::stepAreaPassers(float dt, const cr3::Vec3& pp) {
    for (AreaNpc& npc : areaNpcs) {
        if (npc.wander < 2 || npc.wander > 3) continue;
        if (npc.walkSpeed <= 0) { // (SetMoveSpeed, once)
            std::array<float, 3> a, b;
            if (!areaPassJoint(npc, 0, a) || !areaPassJoint(npc, 1, b)) {
                npc.wander = 0;
                continue;
            }
            const float d = std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));
            const float speed = (npc.wander == 3 ? 4.5f : 0.75f) * (npc.model.compare(0, 4, "c553") == 0 ? 2.0f / 3.0f : 1.0f);
            npc.walkSpeed = d > 1e-4f ? speed / d : 1.0f;
        }
        const float px = pp[0] - npc.place.m[3], py = pp[1] - areaWalker->radius - npc.place.m[7], pz = pp[2] - npc.place.m[11];
        const bool stop = std::sqrt(px * px + py * py + pz * pz) <= npc.talkRange || npc.held;
        if (stop) {
            if (npc.walking) npc.walking = false, setNpcAnim(npc, "fn01");
            continue;
        }
        if (!npc.walking) npc.walking = true, setNpcAnim(npc, npc.wander == 3 ? "ff02" : "ff01");
        npc.walkTime += npc.walkSpeed * dt;
        std::array<float, 3> at;
        if (!areaPassJoint(npc, npc.walkTime, at)) continue;
        cr3::CollisionWorld::Hit hit;
        if (area.walls.sweep({at[0], at[1] + 1.0f, at[2]}, {at[0], at[1] - 1.0f, at[2]}, 0.01f, {0, 1, 0}, 0.0f, hit)) at[1] = at[1] + 1.0f - 2.0f * hit.fraction;
        const float dx = at[0] - npc.place.m[3], dz = at[2] - npc.place.m[11];
        if (std::hypot(dx, dz) > 1e-4f) {
            const float yaw = std::atan2(dx, dz), sc = std::max(std::hypot(npc.place.m[0], npc.place.m[8]), 1e-6f);
            cr3::Mat34& m = npc.place;
            m.m[0] = std::cos(yaw) * sc, m.m[2] = std::sin(yaw) * sc, m.m[8] = -std::sin(yaw) * sc, m.m[10] = std::cos(yaw) * sc;
        }
        npc.place.m[3] = at[0], npc.place.m[7] = at[1], npc.place.m[11] = at[2];
    }
}

// (Fld2NPC::Update: one of Fld2NPCIdle2Name's standing - not walking, no talk of its own going -
// with NPCIdle2Enable on: after 10 .. 15 s fn02 once - at its end, or 10 s on, fn01 again for
// another 10 .. 15)
void Viewer::stepAreaIdle2(float dt) {
    const bool talking = areaEvents && areaEvents->running();
    for (AreaNpc& npc : areaNpcs) {
        if (!npc.idle2 || !npc.idle2On || npc.walking || talking) continue;
        npc.idle2Left -= dt;
        const float dur = npc.body.animLoaded ? npc.body.anim.duration() : 0.0f;
        const bool ended = npc.idle2State == 1 && areaClock - npc.motionAt >= dur;
        if (npc.idle2Left > 0 && !ended) continue;
        npc.idle2State = npc.idle2State == 0 ? 1 : 0;
        npc.idle2Left = 10.0f + 5.0f * float(std::rand()) / float(RAND_MAX);
        if (!setNpcAnim(npc, npc.idle2State == 1 ? "fn02" : "fn01") && npc.idle2State == 1) setNpcAnim(npc, "fn01"), npc.idle2State = 0;
    }
}

void Viewer::stepAreaWanderers(float dt) {
    if (!areaWalker) return;
    stepAreaIdle2(dt);
    const cr3::Vec3 pp = areaWalker->position();
    stepAreaPassers(dt, pp);
    for (AreaNpc& npc : areaNpcs) {
        if (npc.wander < 4 || npc.wander > 5 || npc.rectW <= 0) continue;
        const float px = pp[0] - npc.place.m[3], py = pp[1] - areaWalker->radius - npc.place.m[7], pz = pp[2] - npc.place.m[11];
        const bool near = std::sqrt(px * px + py * py + pz * pz) <= npc.talkRange;
        if (npc.walking) {
            if (near || npc.held) {
                npc.walking = false;
                setNpcAnim(npc, "fn01");
                continue;
            }
            npc.walkTime += dt;
            const float step = npc.walkSpeed * dt;
            const float sc = std::max(std::hypot(npc.place.m[0], npc.place.m[8]), 1e-6f);
            float x = npc.place.m[3] + step * npc.place.m[2] / sc, y = npc.place.m[7], z = npc.place.m[11] + step * npc.place.m[10] / sc;
            cr3::CollisionWorld::Hit hit; // (HitTestFast down from 1 above to 1 below, the floor's group)
            if (area.walls.sweep({x, y + 1.0f, z}, {x, y - 1.0f, z}, 0.01f, {0, 1, 0}, 0.0f, hit)) y = y + 1.0f - 2.0f * hit.fraction;
            npc.place.m[3] = x, npc.place.m[7] = y, npc.place.m[11] = z;
            if (std::hypot(npc.target[0] - x, npc.target[2] - z) <= 0.4f || npc.walkTime > 10.0f) {
                npc.walking = false;
                setNpcAnim(npc, "fn01");
                static const float kWait[6] = {5, 4, 3, 6, 7, 7};
                npc.wait = kWait[std::rand() % 6], npc.walkTime = 0;
            }
            continue;
        }
        if (npc.wait > 0) {
            npc.wait = std::max(npc.wait - dt, 0.0f);
            continue;
        }
        if (near || npc.held) continue;
        const float u = (float(std::rand()) / float(RAND_MAX) - 0.5f) * npc.rectW, v = (float(std::rand()) / float(RAND_MAX) - 0.5f) * npc.rectD;
        const cr3::Mat34& r = npc.rect;
        float tx = r.m[3] + r.m[0] * u + r.m[2] * v, ty = npc.place.m[7], tz = r.m[11] + r.m[8] * u + r.m[10] * v;
        cr3::CollisionWorld::Hit hit;
        if (area.walls.sweep({tx, ty + 1.0f, tz}, {tx, ty - 1.0f, tz}, 0.01f, {0, 1, 0}, 0.0f, hit)) ty = ty + 1.0f - 2.0f * hit.fraction;
        npc.target = {tx, ty, tz};
        npc.turnFrom = std::atan2(npc.place.m[2], npc.place.m[0]);
        npc.turnTo = std::atan2(tx - npc.place.m[3], tz - npc.place.m[11]);
        npc.turnFrames = 4, npc.turnStep = 0;
        setNpcAnim(npc, npc.wander == 5 ? "ff02" : "ff01");
        npc.walking = true, npc.walkTime = 0;
    }
}

void Viewer::stepAreaMotions() {
    for (const auto& [name, motion] : areaMotions) {
        for (AreaNpc& npc : areaNpcs) {
            if (npc.name != name) continue;
            bool found = false;
            for (ViewFigure* fp : {&npc.body, &npc.head}) {
                ViewFigure& f = *fp;
                if (f.meshes.empty()) continue;
                const std::string base = motionBase(f.name.substr(0, f.name.size() - 5));
                for (const std::string& want : {motion + ".anim", npc.model + "_" + motion + ".anim", base + "_" + motion + ".anim"}) {
                    const auto at = std::find(f.anims.begin(), f.anims.end(), want);
                    if (at == f.anims.end()) continue;
                    const float keep = time;
                    loadAnim(f, size_t(at - f.anims.begin()));
                    time = keep;
                    found = true;
                    break;
                }
            }
            npc.motionAt = areaClock, npc.eventMotion = found;
            SDL_Log("area %s: %s's motion %s%s", area.name.c_str(), name.c_str(), motion.c_str(), found ? "" : " (none)");
        }
    }
    areaMotions.clear();
}

bool Viewer::loadArea(std::string name, const std::string& player) { // (a copy: callers pass members it resets)
    // (a script's map name may be upper case - q058_d07_00109's "D0712f": the map table's own is lower)
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
    std::string err;
    if (!area.load(archive, name, &err)) {
        SDL_Log("area %s: %s", name.c_str(), err.c_str());
        return false;
    }
    for (ViewFigure& f : areaFigures) unload(f);
    areaFigures.clear();
    for (AreaNpc& npc : areaNpcs) unload(npc.body), unload(npc.head), unload(npc.alt);
    areaNpcs.clear();
    game.gimmickShown.clear(), game.fieldBoxes.clear(), game.switchesOn.clear(); // (Fld2TaskGimmick::CreateGimmick: all shown, evt000 hides what it will)
    // (Fld2Main::ChangeArea: the tab closed and made again - CampFieldMenu closed -, the minimap back on)
    cr3Camp.panelOpen = false, cr3Camp.panelAt = -10, game.minimapEnable = true;
    minimapMarks.clear(), minimapLayers.clear(), minimapLayer = -1;
    game.fieldEffects.clear();
    if (name.compare(0, 3, areaPrefix) != 0) game.brokenRocks.clear(); // (another dungeon or a town: its rocks whole again)
    areaPrefix = name.substr(0, 3);
    for (FieldBomb& b : fieldBombs) b = FieldBomb();
    fieldPendingFunctions.clear();
    audio.stopSe("fe_012", 0);
    fieldQuake = FieldQuake();
    areaBaseWalls = area.walls, areaPhysP = cr3::PhysScene(), areaWallKey = "~";
    for (const std::string& n : area.figures) {
        ViewFigure f;
        if (!loadFigure(n + ".geom", f, &err)) {
            SDL_Log("area %s: %s", n.c_str(), err.c_str());
            f = ViewFigure(), f.tint[3] = 0; // (kept in its place: Cr3Area::places by index)
        }
        f.absolute = true; // (CrxSimpleModel: its own animation, looped)
        areaFigures.push_back(std::move(f));
    }
    // the lights: Fld2TaskMap::EntryLight's, the <map>f model's figure (Fld2CameraBase::SetLight: else <map>c)
    // (a random area's: its d0600_00 piece's figure - CreateAreaRandom's +0x378 -, d0600f)
    const bool randomArea = name.compare(0, 3, "d06") == 0 || name.compare(0, 3, "d18") == 0;
    if (randomArea && loadLights(name.substr(0, 3) + "00f")) SDL_Log("lights: %s00f", name.substr(0, 3).c_str());
    else if (loadLights(name + "f")) SDL_Log("lights: %sf", name.c_str());
    else if (loadLights(name + "c")) SDL_Log("lights: %sc", name.c_str());
    else lighting = cr3::SceneLighting();
    initEventDrawing(); // (the area's script's windows and fades)
    // the player: Fld2TaskPlayer::CreatePlayer (Fld2CharaNameTbl's model; a hero's body b01)
    areaPlayerArgName = player;
    // (Fld2TaskPlayer: Fld2CharaNameTbl[the party's first - 1], 15 its last)
    static const char* const kCharaNames[6] = {"j001", "j002", "j003", "j004", "j005", "c101"};
    const int lead = game.cr3Party[0] == 15 ? 6 : game.cr3Party[0];
    areaPlayerName = player.empty() ? (lead >= 1 && lead <= 6 ? kCharaNames[lead - 1] : "c101") : player;
    if (areaPlayerName[0] == 'j' && areaPlayerName.size() == 4) areaPlayerName = cr3PartsBody(areaPlayerName);
    unload(areaPlayer), unload(areaPlayerHead);
    areaPlayer = ViewFigure(), areaPlayerHead = ViewFigure();
    if (!loadFigure(areaPlayerName + ".geom", areaPlayer, &err)) SDL_Log("player %s: %s", areaPlayerName.c_str(), err.c_str());
    if (areaPlayerName.size() >= 7 && areaPlayerName[4] == 'b' && archive.contains(areaPlayerName.substr(0, 4) + "h01.geom") &&
        !loadFigure(areaPlayerName.substr(0, 4) + "h01.geom", areaPlayerHead, &err))
        SDL_Log("player head: %s", err.c_str());
    areaWalker = std::make_unique<cr3::CharacterController>(area.walls);
    areaWalker->radius = 0.2f, areaWalker->stepHeight = 0.1f; // (Fld2TaskPlayer::CreateGhost: a 0.2 sphere; Setup(0.1); SetMaxSlope 45 degrees)
    areaWalker->setPosition({area.spawn[0], area.spawn[1] + 0.2f, area.spawn[2]});
    for (int k = 0; k < 60; ++k) areaWalker->setVelocity({0, 0, 0}), areaWalker->step(); // (down onto the floor)
    areaYaw = area.spawnYaw;
    areaMotionSet = false;
    setAreaMotion(cr3::Cr3Motion::Idle);
    const cr3::Vec3 p = areaWalker->position();
    setupAreaCamera(name);
    areaCamera.reset({p[0], p[1] - areaWalker->radius, p[2]}, areaYaw), rail.started = false;
    areaCamera.step({p[0], p[1] - areaWalker->radius, p[2]}, areaYaw, false, 0, &area.cameraWalls, &area.walls);
    areaMode = true, areaClock = 0;
    // (Cr3System.GetDungeonNum / GetAreaNum: the dungeon - the Field_ table's, t01 0, d01 5 - and the area from 0)
    game.dungeon = cr3::cr3DungeonNum(name);
    game.area = std::atoi(name.substr(3, 2).c_str()) - 1;
    if (game.dungeon != lastAreaDungeon) { // (Fld2Main SM_Init / the dungeon changed: Fld2TBoxList::Drawing)
        game.cr3Boxes.clear(), game.cr3OpenBox = -1;
        if (game.dungeon > 4) drawCr3Boxes(name);
    }
    lastAreaDungeon = game.dungeon;
    areaEvents = std::make_unique<cr3::EventRuntime>(archive, game);
    areaEvents->language = optionLanguage, areaEvents->autoPage = pageSeconds, areaEvents->askYesNo = true;
    areaEvents->extraNatives = [this](cr3::ScriptVM& vm) { bindAreaNatives(vm); };
    areaScriptCam = AreaScriptCam(), areaMotions.clear();
    areaEventClock = 0;
    game.gimmickSetupRequested = false;
    areaBaseWalls = area.walls;
    setupAreaGimmicks(name);
    // (standing where it was left for a vista, coming back to it)
    const bool restored = savedArea == name;
    if (restored) {
        areaWalker->setPosition({savedPos[0], savedPos[1], savedPos[2]});
        areaYaw = savedYaw;
        const cr3::Vec3 q = areaWalker->position();
        areaCamera.reset({q[0], q[1] - areaWalker->radius, q[2]}, areaYaw), rail.started = false;
        for (AreaGimmick& g : areaGimmicks) g.inside = gimmickHolds(g); // (no box set off by standing in it)
    }
    savedArea.clear();
    // (Fld2Main::SM_FirstEvent: evt000 - evt002 back from a battle lost, Cr3FieldData's come-from 1
    // - SetComeFromBattleToField, Fld2SaveData(1) - with BtlSystem::s_ResultParam 1)
    const std::string entry = game.next.fieldMode == 1 && cr3BattleLost ? "evt002" : "evt000";
    game.next.fieldMode = -1;
    if (!areaEvents->start(name.substr(0, 3), entry) && (entry == "evt000" || !areaEvents->start(name.substr(0, 3), "evt000")))
        SDL_Log("area %s: %s - %s", name.c_str(), entry.c_str(), areaEvents->error().c_str());
    else {
        SDL_Log("area %s: %s running, %zu gimmicks", name.c_str(), entry.c_str(), areaGimmicks.size());
        areaScriptOver = !areaEvents->running(); // (over without a wait: what it reserved still goes next)
    }
    setupAreaEncounters(name);
    cr3SavePending = cr3SavePending || cr3Playing; // (Fld2Main::SM_Init / SM_ChangeArea: DBSystem::Sync)
    const cr3::Vec3 at = areaWalker->position();
    SDL_Log("area %s: %zu figures, player %s at %s (%.2f %.2f %.2f)", name.c_str(), areaFigures.size(), areaPlayerName.c_str(),
            restored ? "where it left" : "defaultPos", at[0], at[1], at[2]);
    return true;
}

// Fld2TaskPlayer::SetMotion: the motion, blended in over 1/6 s
void Viewer::setAreaMotion(cr3::Cr3Motion m) {
    if (areaMotionSet && m == areaMotion) return;
    const std::string want = motionBase(areaPlayerName) + "_" + cr3::cr3MotionName(m) + ".anim";
    if (std::find(areaPlayer.anims.begin(), areaPlayer.anims.end(), want) == areaPlayer.anims.end()) return;
    for (ViewFigure* fp : {&areaPlayer, &areaPlayerHead}) { // (the head: its own animator, the same motion)
        ViewFigure& f = *fp;
        const std::string name = motionBase(f.name.substr(0, f.name.size() - 5)) + "_" + cr3::cr3MotionName(m) + ".anim";
        const auto it = std::find(f.anims.begin(), f.anims.end(), name);
        if (it == f.anims.end()) continue;
        if (f.animLoaded) f.blendAnim = f.anim, f.blendLoaded = true, f.blendAbsolute = f.absolute, f.blendTime = areaMotionAge, f.blendLoop = true;
        const float keep = time;
        loadAnim(f, size_t(it - f.anims.begin()));
        time = keep;
    }
    areaMotion = m, areaMotionSet = true, areaMotionAge = 0;
}

// the player within a gimmick's box (its joint's axes; the box 0.05 of each, the player's
// sphere counted)
bool Viewer::gimmickHolds(const AreaGimmick& g) const {
    const cr3::Vec3 p = areaWalker->position();
    if (g.shaped) { // (the player's sphere against its box, in the box's own axes)
        const float d[3] = {p[0] - g.boxAt[0], p[1] - g.boxAt[1], p[2] - g.boxAt[2]};
        const float qx = -g.boxTurn[0], qy = -g.boxTurn[1], qz = -g.boxTurn[2], qw = g.boxTurn[3];
        const float tx = 2 * (qy * d[2] - qz * d[1]), ty = 2 * (qz * d[0] - qx * d[2]), tz = 2 * (qx * d[1] - qy * d[0]);
        const float l[3] = {d[0] + qw * tx + (qy * tz - qz * ty), d[1] + qw * ty + (qz * tx - qx * tz), d[2] + qw * tz + (qx * ty - qy * tx)};
        for (int a = 0; a < 3; ++a)
            if (l[a] < g.boxLo[size_t(a)] - areaWalker->radius || l[a] > g.boxHi[size_t(a)] + areaWalker->radius) return false;
        return true;
    }
    const float d[3] = {p[0] - g.world.m[3], p[1] - g.world.m[7], p[2] - g.world.m[11]};
    for (int a = 0; a < 3; ++a) {
        const float ax[3] = {g.world.m[a], g.world.m[4 + a], g.world.m[8 + a]};
        const float len = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
        if (len < 1e-6f) return false;
        const float along = (d[0] * ax[0] + d[1] * ax[1] + d[2] * ax[2]) / len;
        if (std::fabs(along) > len * 0.05f + areaWalker->radius) return false;
    }
    return true;
}

// another area entered at a pop point (Fld2Main::ChangeArea(pop point): its map's - t0202_p1's
// t0202 -, the player on the point's joint, turned its way)
bool Viewer::enterPopPoint(const std::string& pop) {
    if (pop.size() <= 5) return false;
    const std::string map = pop.substr(0, 5);
    game.popPoint2.clear(); // (Fld2ClearPopPoint)
    SDL_Log("area %s: to %s at %s", area.name.c_str(), map.c_str(), pop.c_str());
    if (!loadArea(map, areaPlayerArgName)) return false;
    std::vector<uint8_t> d;
    cr3::Skeleton sk;
    if (archive.read(map + "p.skel", d) && cr3::parseSkeleton(d, sk)) {
        std::vector<std::string> names;
        if (archive.read(map + "p.name", d)) names = cr3::parseJointNames(d);
        const std::vector<cr3::Mat34> w = sk.world(sk.basePose);
        for (size_t j = 0; j < names.size() && j < w.size(); ++j)
            if (names[j] == pop) {
                areaWalker->setPosition({w[j].m[3], w[j].m[7] + areaWalker->radius, w[j].m[11]});
                for (int k = 0; k < 30; ++k) areaWalker->setVelocity({0, 0, 0}), areaWalker->step();
                areaYaw = std::atan2(w[j].m[2], w[j].m[10]);
                const cr3::Vec3 q = areaWalker->position();
                areaCamera.reset({q[0], q[1] - areaWalker->radius, q[2]}, areaYaw), rail.started = false;
                SDL_Log("area %s: player at %s (%.2f %.2f %.2f)", map.c_str(), pop.c_str(), q[0], q[1], q[2]);
            }
    }
    for (AreaGimmick& g : areaGimmicks) g.inside = gimmickHolds(g);
    return true;
}

int Viewer::drawnBox(const std::string& joint) const {
    if (joint.compare(0, 8, "ragdoll_") != 0) return -1;
    for (size_t k = 0; k < game.cr3Boxes.size(); ++k)
        if (game.cr3Boxes[k].area == game.area && game.cr3Boxes[k].name == joint.substr(8) && !game.cr3Boxes[k].opened) return int(k);
    return -1;
}

void Viewer::drawCr3Boxes(const std::string& areaName) {
    static bool seeded = false;
    if (!seeded) {
        const char* fixedSeed = SDL_getenv("CR3_BATTLE_SEED");
        boxRng.seed(fixedSeed ? uint32_t(std::strtoul(fixedSeed, nullptr, 10)) : uint32_t(SDL_GetTicks()));
        seeded = true;
    }
    std::vector<uint8_t> d;
    cr3::MbeFile field, boxes, items;
    if (!archive.read("data\\field_" + areaName.substr(0, 3) + ".mbe", d) || !field.parse(d)) return;
    std::vector<std::pair<int, std::string>> tboxes, gboxes; // (area, name)
    if (const cr3::MbeFile::Table* t = field.table("Field"))
        for (size_t r = 0; r < t->rows.size(); ++r) {
            cr3::MbeFile::Row row;
            if (!field.row(*t, r, "iiiiiippiiif", row)) continue;
            const std::string g = row.s(6);
            if (g.compare(0, 12, "ragdoll_tbox") == 0) tboxes.push_back({row.i(0), g.substr(8)});
            else if (g.compare(0, 12, "ragdoll_gbox") == 0) gboxes.push_back({row.i(0), g.substr(8)});
        }
    auto byArea = [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) { return a.first < b.first; };
    std::stable_sort(tboxes.begin(), tboxes.end(), byArea), std::stable_sort(gboxes.begin(), gboxes.end(), byArea);
    const int levelNo = (std::max(encountDifficulty(), 1) - 1) * 10 + 1; // (MbDungeonLevelNo)
    cr3::MbeFile::Row kinds;
    bool found = false;
    if (archive.read("data\\tbox_table.mbe", d) && boxes.parse(d))
        if (const cr3::MbeFile::Table* t = boxes.table("TBoxTable"))
            for (size_t r = 0; r < t->rows.size() && !found; ++r)
                if (boxes.row(*t, r, "iiiiiiiiiiiiiiiiiii", kinds) && kinds.i(2) == game.dungeon && kinds.i(3) == levelNo && (kinds.i(1) == -1 || game.flag(kinds.i(1))))
                    found = true;
    std::vector<cr3::MbeFile::Row> contents;
    if (archive.read("data\\tbox_item_table.mbe", d) && items.parse(d))
        if (const cr3::MbeFile::Table* t = items.table("TBoxItemTable"))
            for (size_t r = 0; r < t->rows.size(); ++r) {
                cr3::MbeFile::Row row;
                if (items.row(*t, r, "iiiiiii", row)) contents.push_back(row);
            }
    const int n = int(tboxes.size());
    if (found && n > 0) {
        std::vector<int> order(static_cast<size_t>(n));
        for (int k = 0; k < n; ++k) order[size_t(k)] = k;
        for (int k = 0; k < 100; ++k) std::swap(order[boxRng.next() % uint32_t(n)], order[boxRng.next() % uint32_t(n)]);
        for (int i = 0; i < 14 && i < n; ++i) { // (DrawingOne)
            const int kind = kinds.i(size_t(4 + i));
            std::vector<const cr3::MbeFile::Row*> pool;
            int total = 0;
            for (const cr3::MbeFile::Row& c : contents) {
                if (c.i(1) != kind) continue;
                if (c.i(6) != 0 && std::any_of(game.cr3Boxes.begin(), game.cr3Boxes.end(), [&](const cr3::GameState::Cr3Box& b) {
                        return b.type == 0 && b.kind == kind && b.category == c.i(3) && b.id == c.i(4);
                    }))
                    continue;
                pool.push_back(&c), total += c.i(5);
            }
            if (total <= 0) continue;
            int roll = int(boxRng.next() % uint32_t(total)), at = 0;
            const cr3::MbeFile::Row* pick = pool.back();
            for (const cr3::MbeFile::Row* c : pool)
                if (roll < (at += c->i(5))) {
                    pick = c;
                    break;
                }
            cr3::GameState::Cr3Box b;
            b.area = tboxes[size_t(order[size_t(i)])].first, b.name = tboxes[size_t(order[size_t(i)])].second;
            b.kind = kind, b.type = 0, b.category = pick->i(3), b.id = pick->i(4);
            game.cr3Boxes.push_back(b);
        }
    }
    int gold = 0; // (GBOX_PROBS: a chance in a hundred each)
    if (const cr3::BsonValue* c = game.cr3Db ? game.cr3Db->constants() : nullptr)
            if (const cr3::BsonValue* probs = c->get("GBOX_PROBS"); probs && probs->isArray())
                for (const cr3::BsonValue& p : probs->items)
                    if (int(boxRng.unit() * 100.0f) <= p.asInt()) ++gold;
                    else break; // (Fld2TBoxList::Drawing: stops at the first failed tier)
    for (int i = 0; i < gold && i < int(gboxes.size()); ++i) {
        cr3::GameState::Cr3Box b;
        b.area = gboxes[size_t(i)].first, b.name = gboxes[size_t(i)].second, b.type = 1;
        game.cr3Boxes.push_back(b);
    }
    SDL_Log("%s: boxes drawn for level %d - %zu (%d treasure places, %d gold of %zu)", areaName.c_str(), levelNo, game.cr3Boxes.size(), n, gold, gboxes.size());
    for (const cr3::GameState::Cr3Box& b : game.cr3Boxes) SDL_Log("  box %s in area %d: kind %d, type %d, %d:%d", b.name.c_str(), b.area + 1, b.kind, b.type, b.category, b.id);
}

void Viewer::runAreaFunction(const std::string& fn) {
    if (!areaEvents) return;
    SDL_Log("area %s: %s", area.name.c_str(), fn.c_str());
    if (!areaEvents->start(area.name.substr(0, 3), fn)) SDL_Log("area %s: %s - %s", area.name.c_str(), fn.c_str(), areaEvents->error().c_str());
    else if (!areaEvents->running()) {
        if (!areaEvents->error().empty()) SDL_Log("area %s: %s - %s", area.name.c_str(), fn.c_str(), areaEvents->error().c_str());
        afterAreaScript(); // (over at once: no wait in it)
    }
}

// a script over: what it reserved (Fld2Main::BranchAfterEvent) - a vista (the field left, the
// player's place kept), another area at a pop point (SetPopPoint: the pop point's map its
// first five letters, the player at that joint of its <map>p), a battle
void Viewer::afterAreaScript() {
    cr3::GameState::Next& n = game.next;
    // (Fld2Main::BranchAfterEvent: area changes first, then town move / lunch / vista / title / battle)
    if (!n.popPoint.empty() && n.popPoint.size() > 5) {
        const std::string pop = n.popPoint;
        n.popPoint.clear();
        enterPopPoint(pop);
        return;
    }
    if (n.changeDungeon >= 0 && n.changeArea >= 0) {
        const int d = n.changeDungeon, a = n.changeArea;
        n.changeDungeon = n.changeArea = -1;
        if (const char* pfx = cr3::cr3DungeonPrefix(d)) {
            char map[16];
            std::snprintf(map, sizeof map, "%s%02d", pfx, a + 1);
            loadArea(map, areaPlayerArgName);
            return;
        }
    }
    if (n.townMove) { // (Fld2GetTownMove: Fld2Main::SM_TownMove)
        n.townMove = false;
        openTownMove(false);
        return;
    }
    if (n.lunch && !n.vistaFunction.empty()) { // (Fld2Main: Cr3ScriptWork 0xf - LunchMain)
        const cr3::GameState::Next keep = n;
        n = cr3::GameState::Next();
        const cr3::Vec3 p = areaWalker->position();
        savedArea = area.name, savedPos = {p[0], p[1], p[2]}, savedYaw = areaYaw;
        initEventDrawing();
        startLunch(keep.vistaFunction, keep.vistaMap);
        return;
    }
    if (n.vista && !n.vistaFunction.empty()) {
        const cr3::GameState::Next keep = n;
        n = cr3::GameState::Next();
        const cr3::Vec3 p = areaWalker->position();
        savedArea = area.name, savedPos = {p[0], p[1], p[2]}, savedYaw = areaYaw;
        SDL_Log("area %s: vista %s in %s", area.name.c_str(), keep.vistaFunction.c_str(), keep.vistaMap.c_str());
        startEvent(eventScriptOf(keep.vistaFunction).empty() ? area.name.substr(0, 3) + ":" + keep.vistaFunction
                                                             : eventScriptOf(keep.vistaFunction) + ":" + keep.vistaFunction,
                   keep.vistaMap.empty() ? "null" : keep.vistaMap, pageSeconds);
        return;
    }
    if (n.title) {
        n = cr3::GameState::Next();
        startCr3Title();
        return;
    }
    if (n.battle >= 0) { // (FldMain::BranchAfterEvent: the battle - the field kept to come back to)
        const int id = n.battle, fieldId = n.battleField;
        n.battle = -1, n.battleField = -1;
        savedArea = area.name;
        const cr3::Vec3 p = areaWalker->position();
        savedPos = {p[0], p[1], p[2]}, savedYaw = areaYaw;
        SDL_Log("area %s: battle %d on field %d", area.name.c_str(), id, fieldId);
        requestCr3Battle(id, fieldId);
    }
}

// (the difficulty, DAT_014bde04 - Cr3System.SetDungeonLevel, else the world select's level)
int Viewer::encountDifficulty() const {
    if (game.dungeonLevel > 0) return game.dungeonLevel;
    return game.selectLevel >= 0 ? game.selectLevel + 1 : 0;
}

void Viewer::setupAreaEncounters(const std::string& name) {
    areaEnc = AreaEncounters();
    static bool seeded = false; // (rand(): seeded by the clock - CR3_BATTLE_SEED for tests)
    if (!seeded) {
        const char* fixedSeed = SDL_getenv("CR3_BATTLE_SEED");
        encountRng.seed(fixedSeed ? uint32_t(std::strtoul(fixedSeed, nullptr, 10)) : uint32_t(SDL_GetTicks()));
        seeded = true;
    }
    if (name.size() < 5 || name[0] != 'd') return;
    static cr3::MbeFile enc, mon, ace;
    static int readIn = -1; // (the language it was read in: textGeneration)
    if (readIn != textGeneration) enc = cr3::MbeFile(), mon = cr3::MbeFile(), ace = cr3::MbeFile(), readIn = textGeneration;
    if (!enc.table("Encounter")) {
        std::vector<uint8_t> d;
        if (archive.read("data\\enc.mbe", d)) enc.parse(d);
        if (archive.read("data\\mon_enc.mbe", d)) mon.parse(d);
        if (archive.read("data\\ace_coupling.mbe", d)) ace.parse(d);
    }
    const int dungeon = std::atoi(name.substr(1, 2).c_str()), areaNo = std::atoi(name.substr(3, 2).c_str());
    areaEnc.dungeon = dungeon;
    if (const cr3::MbeFile::Table* t = ace.table("AceCoupling")) // (CreateAceCoupling + LoadAceCoupling)
        for (size_t k = 0; k < t->rows.size(); ++k) {
            cr3::MbeFile::Row r;
            if (!ace.row(*t, k, "iiiiiiiiiiiiii", r) || r.i(3) <= 0) continue;
            const int aceId = r.i(0), calc = r.i(4) & 0xff;
            int lv = r.i(5);
            if (calc == 0) {
                const auto saved = game.cr3AceLevels.find(aceId);
                if (saved != game.cr3AceLevels.end() && saved->second > 0) lv = saved->second;
            }
            areaEnc.aces.push_back({aceId, r.i(3), calc, lv, r.i(6), r.i(7), r.i(8), r.i(9), r.i(10), r.i(12), int16_t(r.i(13)), int16_t(uint32_t(r.i(13)) >> 16)});
        }
    cr3::MbeFile::Row r;
    bool found = false;
    if (const cr3::MbeFile::Table* t = enc.table("Encounter"))
        for (size_t k = 0; k < t->rows.size() && !found; ++k)
            if (enc.row(*t, k, "iii", r) && r.i(0) == dungeon && int8_t(r.i(1)) == areaNo) { // (u32 dungeon; chars area, field, steps, chance; u32 music)
                const uint32_t b = uint32_t(r.i(1));
                areaEnc.field = int8_t(b >> 8), areaEnc.steps = int8_t(b >> 16), areaEnc.rate = int8_t(b >> 24), areaEnc.bgm = r.i(2);
                found = true;
            }
    if (!found) return;
    const int d = std::max(encountDifficulty(), 1), band = (d - 1) * 10 + 1;
    if (const cr3::MbeFile::Table* t = mon.table("MonsterEncounter"))
        for (size_t k = 0; k < t->rows.size(); ++k) {
            // (ints: dungeon, index, coupling, base level, max level, -, event flag; shorts: the
            // first strikes, the player's and the enemy's; then a char weight for each area)
            if (!mon.row(*t, k, "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii", r) || r.i(0) != dungeon) continue;
            if (r.i(3) > band || (r.i(4) != -1 && band > r.i(4))) continue;
            if (r.i(6) != -1 && !game.flag(r.i(6))) continue;
            if (areaNo < 1 || areaNo > 100) continue;
            const int weight = int8_t(uint32_t(r.i(8 + (areaNo - 1) / 4)) >> ((areaNo - 1) % 4 * 8));
            if (weight <= 0) continue;
            areaEnc.couplings.push_back({weight, r.i(1), r.i(2), int16_t(r.i(7)), int16_t(uint32_t(r.i(7)) >> 16)});
            areaEnc.total += weight;
        }
    SDL_Log("area %s: encounters every %d steps from %d%%, %zu couplings (and %zu aces) on field %d", name.c_str(),
            std::max(areaEnc.steps / encounterUp(), 1), areaEnc.rate, areaEnc.couplings.size(), areaEnc.aces.size(), areaEnc.field);
}

// the coupling met (Pickout / ForceEncount: rand() % the weights' sum, the first whose running
// sum reaches it) - its index in areaEnc.couplings; -1 none
int Viewer::pickEncounter() {
    if (areaEnc.couplings.empty()) return -1;
    const int r = areaEnc.total > 0 ? int(encountRng.next() % uint32_t(areaEnc.total)) : 0;
    int sum = 0;
    for (size_t k = 0; k < areaEnc.couplings.size(); ++k)
        if ((sum += areaEnc.couplings[k].weight) >= r) return int(k);
    return int(areaEnc.couplings.size()) - 1;
}

// DrawingAceCoupling: the aces the difficulty's band holds, their event flag set, each passing
// its own chance - one of them at random; -1 none
int Viewer::drawAce() {
    const int band = (std::max(encountDifficulty(), 1) - 1) * 10 + 1;
    std::vector<int> drawn;
    for (size_t k = 0; k < areaEnc.aces.size(); ++k) {
        const AreaEncounters::Ace& a = areaEnc.aces[k];
        if (a.base > band || (a.max != -1 && band > a.max) || (a.flag != -1 && !game.flag(a.flag))) continue;
        if (int(encountRng.next() % 100) < a.chance) drawn.push_back(int(k));
    }
    return drawn.empty() ? -1 : drawn[encountRng.next() % uint32_t(drawn.size())];
}

// the monsters' level (CalcEnemyLevel @ 0x00867728): average equipped Gene levels of cr3Party[0..2]
// (CalcAveLevel @ 0x008677d8) + (rand & 7) - 5, held to the difficulty's band
int Viewer::encounterLevel() {
    int sum = 0, count = 0;
    for (int slot = 0; slot < 3; ++slot) {
        const int ch = game.cr3Party[slot];
        if (ch <= 0) continue;
        const int gi = game.wornGene(ch);
        if (gi >= 0 && game.cr3Db) {
            sum += game.cr3Db->geneLevel(game.cr3Genes[size_t(gi)]);
            ++count;
        } else if (cr3TablesReady()) {
            cr3::MbeFile::Row cr, gr;
            if (cr3BattleTables.character.find("Char", ch, "iiiiiiii", cr) && cr.i(7) > 0 && cr3BattleTables.gene.find("GuestGene", cr.i(7), "iii", gr)) {
                sum += gr.i(2);
                ++count;
            }
        }
    }
    const int ave = count > 0 ? std::max(sum / count, 1) : 1;
    const int d = encountDifficulty();
    const uint32_t u = encountRng.next();
    int level = ave + int(u & 7) - 5;
    if (d >= 11) level = d * 10 - int(u % 10);
    else if (d > 0) level = std::clamp(level, d * 10 - 9, d * 10);
    return std::max(level, 1);
}

// SetupAceBattle: its coupling, its level (CalcEnemyLevel's, or its own), its first strikes
void Viewer::startAceEncounter(const AreaEncounters::Ace& a) {
    AreaEncounters::Coupling c;
    c.id = a.coupling, c.playerFirst = a.playerFirst, c.enemyFirst = a.enemyFirst;
    SDL_Log("area %s: an ace (id %d, lv %d)", area.name.c_str(), a.id, a.level);
    startEncounter(c, a.calcLevel ? encounterLevel() : std::max(a.level, 1));
    activeAceId = a.id;
}

// a battle met (SetupBattle): the monsters' level from the party's equipped gene average
// (CalcAveLevel) and the difficulty's band (CalcEnemyLevel), the field kept to come back to
void Viewer::startEncounter(const AreaEncounters::Coupling& met, int level) {
    activeAceId = -1;
    const int coupling = met.id;
    if (level < 0) level = encounterLevel();
    cr3::GameState::Next& n = game.next;
    n.battleLevel = level, n.battleMusic = areaEnc.bgm, n.battleMusicAfter = 32, n.battleNext = 1;
    n.battleFirstStrike[0] = met.playerFirst, n.battleFirstStrike[1] = met.enemyFirst;
    savedArea = area.name;
    const cr3::Vec3 p = areaWalker->position();
    savedPos = {p[0], p[1], p[2]}, savedYaw = areaYaw;
    SDL_Log("area %s: encounter - coupling %d on field %d at level %d", area.name.c_str(), coupling, areaEnc.field, level);
    audio.stopBgm(30); // (SetupBattle: the field's music out over a second - 30 updates)
    requestCr3Battle(coupling, areaEnc.field);
}

// ForceBattleIn (the field's Battle button @ 0x00866d44): an Ace first outside d01, else a coupling
bool Viewer::forceEncounter() {
    if (!areaWalker) return false;
    if (const int a = areaEnc.dungeon != 1 ? drawAce() : -1; a >= 0) {
        startAceEncounter(areaEnc.aces[size_t(a)]);
        return true;
    }
    const int c = pickEncounter();
    if (c < 0) return false;
    startEncounter(areaEnc.couplings[size_t(c)]);
    return true;
}

// CrxGetEncounterUp: the party's first three's worn genes - their skills' kind 124 (0x7c)
// constants, the highest; 1 at least (the steps between rolls divided by it)
int Viewer::encounterUp() {
    if (!cr3BattleTablesLoaded && !(cr3BattleTablesLoaded = cr3BattleTables.load(archive, nullptr))) return 1;
    cr3Rules.useTables(cr3BattleTables);
    int up = 1;
    for (int slot = 0; slot < 3; ++slot) {
        const int gi = game.wornGene(game.cr3Party[slot]);
        if (game.cr3Party[slot] <= 0 || gi < 0) continue;
        for (const auto& sl : game.cr3Genes[size_t(gi)].slots)
            if (sl[0] == 1 && sl[1] > 0) up = std::max(up, cr3Rules.skillKindMax(sl[1], 0x7c));
    }
    return up;
}

// SM_Main: each 1.7 walked across a step; at every steps' (over the genes' EncounterUp) a roll
// under the chance meets a battle, else the chance up 5. Not while they're off (the field's
// button, the scripts' SetEncountEnable), flag 1040 or 4105 set, unless 1037 says count anyway
void Viewer::countEncounter() {
    // (the distance from the last place counted, taken 0.05 at a time: the controller's
    // shiver against a corner - 0.01 there and back each frame - not walked)
    const cr3::Vec3 p = areaWalker->position();
    const float dx = p[0] - areaEnc.last[0], dz = p[2] - areaEnc.last[1], moved = std::sqrt(dx * dx + dz * dz);
    if (!areaEnc.lastSet || moved < 0.05f) {
        if (!areaEnc.lastSet) areaEnc.last[0] = p[0], areaEnc.last[1] = p[2], areaEnc.lastSet = true;
        return;
    }
    areaEnc.last[0] = p[0], areaEnc.last[1] = p[2];
    if (areaEnc.rate == 0 || areaEnc.couplings.empty()) return;
    const bool on = game.encountEnable && !encountersOff && !game.flag(0x410) && !game.flag(4105);
    if (!on && !game.flag(0x40d)) return;
    areaEnc.walked += moved;
    if (areaEnc.walked < 1.7f) return;
    areaEnc.walked = 0;
    if (++areaEnc.counted < std::max(areaEnc.steps / encounterUp(), 1)) return;
    areaEnc.counted = 0;
    if (int(encountRng.next() % 100) <= areaEnc.rate) { // (Pickout: an ace first - not in dungeon 1 -, else the area's)
        if (const int a = areaEnc.dungeon != 1 ? drawAce() : -1; a >= 0) return startAceEncounter(areaEnc.aces[size_t(a)]);
        if (const int c = pickEncounter(); c >= 0) return startEncounter(areaEnc.couplings[size_t(c)]);
    }
    areaEnc.rate += 5;
}

void Viewer::updateArea(float dt, float sx, float sy, bool run) {
    takeAutoBattleToken();
    areaClock += dt, areaMotionAge += dt;
    stepFieldEffects(dt);
    stepAreaScriptCam(dt);
    stepAreaMotions();
    stepAreaTurns(dt);
    stepAreaWanderers(dt);
    if (areaEvents) { // (the area's script at the game's updates - its fades and windows on after it)
        const bool wasRunning = areaEvents->running();
        areaEventClock = std::min(areaEventClock + dt, 0.25f);
        for (; areaEventClock >= cr3::EventRuntime::kTick; areaEventClock -= cr3::EventRuntime::kTick)
            if (!areaEvents->tick()) {
                SDL_Log("area script: %s", areaEvents->error().c_str());
                break;
            }
        areaScriptRun = areaEvents->running() ? areaScriptRun + dt : 0;
        if (areaScriptRun >= 60 && areaScriptRun - dt < 60) // (a minute in: a script that waits on what never comes)
            SDL_Log("area script: still running after a minute, in %s", areaEvents->where().c_str());
        if ((wasRunning || areaScriptOver) && !areaEvents->running()) {
            areaScriptOver = false;
            SDL_Log("area script: ended at frame %d", areaEvents->frame());
            afterAreaScript();
            if (eventMode || !areaMode || cr3Title.stage) return;
        }
        if (game.gimmickSetupRequested) {
            game.gimmickSetupRequested = false;
            setupAreaGimmicks(area.name);
        }
    }
    const bool scripted = areaEvents && areaEvents->running();
    if (scripted) sx = sy = 0; // (PauseMove: the player held while the area's script runs)
    // (the gene lab: its clock since it opened, the screen's; --input lab:talk / combine / sell /
    // repository / back / normal / enhance - its buttons -, lab:card<n> / row<n> / tab<n> / mix /
    // ok / yes / no / down / up, for tests)
    if (areaEvents && areaEvents->geneLab().open) {
        if (!geneLabWasOpen) geneLabClock = 0, geneLabFocus = 0, geneMenu = Cr3GeneMenu();
        geneLabClock += dt;
        geneMenu.since += dt;
        geneMenu.alertSince += dt;
        if (!geneMenu.alert.empty() && geneMenu.alertSince > 2.0f) geneMenu.alert.clear(); // (SetAutoEndAlertWindowMenu)
        if (geneMenu.screen == 5 && geneMenu.since >= geneMenu.effectEnd) geneMenu.screen = 6, geneMenu.since = 0; // (the effect over: its card)
        if (!inputScript.empty() && inputScript.front().compare(0, 4, "lab:") == 0 && geneLabClock > 1.0f && geneMenu.since > 0.5f &&
            uiClock >= inputNext && !areaEvents->geneLab().talking) {
            static const std::map<std::string, int> kLab = {{"talk", 17},  {"combine", 28},  {"sell", 21},    {"repository", 29}, {"back", -1},
                                                            {"normal", 30}, {"enhance", 31}, {"mix", kTapMix}, {"yes", kTapYes},   {"no", kTapNo},
                                                            {"ok", kTapOk},   {"store", 33},     {"takeout", 34},   {"expand", 35}};
            const std::string want = inputScript.front().substr(4);
            inputScript.pop_front();
            inputNext = uiClock + 0.5f;
            if (const auto it = kLab.find(want); it != kLab.end()) chooseGeneLab(*areaEvents, it->second);
            else if (want == "down" || want == "up") geneLabKey(want == "down" ? SDLK_DOWN : SDLK_UP);
            else if (want.compare(0, 4, "card") == 0) chooseGeneLab(*areaEvents, kTapCard + std::atoi(want.c_str() + 4));
            else if (want.compare(0, 3, "row") == 0) chooseGeneLab(*areaEvents, kTapRow + std::atoi(want.c_str() + 3));
            else if (want.compare(0, 3, "tab") == 0) chooseGeneLab(*areaEvents, kTapTab + std::atoi(want.c_str() + 3));
        }
    }
    geneLabWasOpen = areaEvents && areaEvents->geneLab().open;
    // (the Space Port: its clocks; --input port:talk / quests / explore / back / yes / no / ok /
    // left / right / told, port:row<n> / cat<n>, for tests)
    stepSpacePort(dt);
    stepWorldSelect(dt);
    if (stepTownMove(dt)) return; // (the town map holds the field)
    stepScriptCollect(dt);
    stepShop(dt);
    // (--input shop:talk / buy / sell / back / yes / no / second<n> / row<n> / cat<n> / tab / up / down, for tests)
    if (areaEvents && areaEvents->shop().open && !areaEvents->shop().talking && !inputScript.empty() && inputScript.front().compare(0, 5, "shop:") == 0 &&
        shopMenu.clock > 0.8f && shopMenu.since > 0.5f && uiClock >= inputNext) {
        const std::string want = inputScript.front().substr(5);
        inputScript.pop_front();
        inputNext = uiClock + 0.5f;
        if (want == "talk") chooseShop(*areaEvents, 0x11);
        else if (want == "buy") chooseShop(*areaEvents, 0x14);
        else if (want == "alter") chooseShop(*areaEvents, 0x29);
        else if (want == "sell") chooseShop(*areaEvents, 0x15);
        else if (want == "back") chooseShop(*areaEvents, -1);
        else if (want == "yes") chooseShop(*areaEvents, kTapShopYes);
        else if (want == "no") chooseShop(*areaEvents, kTapShopNo);
        else if (want == "tab") chooseShop(*areaEvents, shopMenu.buy ? kTapShopSellTab : kTapShopBuyTab);
        else if (want == "up") chooseShop(*areaEvents, kTapShopReel + 2);
        else if (want.compare(0, 6, "second") == 0) chooseShop(*areaEvents, kTapShopSecond + std::atoi(want.c_str() + 6));
        else if (want.compare(0, 3, "row") == 0) chooseShop(*areaEvents, kTapShopRow + std::atoi(want.c_str() + 3));
        else if (want.compare(0, 3, "cat") == 0) chooseShop(*areaEvents, kTapShopCat + std::atoi(want.c_str() + 3));
        else if (want.compare(0, 5, "chara") == 0) chooseShop(*areaEvents, kTapShopChara + std::atoi(want.c_str() + 5));
    }
    stepCr3GeneGet(dt);
    // (--input gene:<id>[:level[:new]] - the gene obtained window as the scripts open it -, gene:tap, gene:collect:<dungeon>:<category>:<id> - a script's collection - and gene:close, for tests)
    if (areaEvents && !inputScript.empty() && inputScript.front().compare(0, 5, "gene:") == 0 && uiClock >= inputNext) {
        const std::string want = inputScript.front().substr(5);
        inputScript.pop_front();
        inputNext = uiClock + 0.5f;
        if (want == "tap") geneGetTap();
        else if (want.compare(0, 8, "collect:") == 0) { // (gene:collect:<dungeon>:<category>:<id> - CollectListDisp's)
            int d = 2, c = -1, i = -1;
            std::sscanf(want.c_str() + 8, "%d:%d:%d", &d, &c, &i);
            areaEvents->collectListOpen(d, c, i);
        } else if (want == "close") chooseCollectList(-1);
        else {
            int id = 0, level = 1, fresh = 0;
            std::sscanf(want.c_str(), "%d:%d:%d", &id, &level, &fresh);
            areaEvents->openGeneGet(id, level, fresh != 0, true);
        }
    }
    // (--input world:point<n> / level<n> / list / row<n> / gift<n> / help<kind x 10 + slot> / close / yes / no / back, for tests)
    if (areaEvents && areaEvents->worldSelect().open && !inputScript.empty() && inputScript.front().compare(0, 6, "world:") == 0 &&
        worldSel.clock > 1.0f && worldSel.since > 0.5f && uiClock >= inputNext &&
        (worldSel.stage != 5 || (collectList.open && collectList.phase == 0 && collectList.since > 0.5f && collectList.cgl % 3 != 1 && collectList.cgl != 4 &&
                                 (collectList.cgl != 0 || collectList.since > collectShown() + 0.1f)))) {
        const std::string want = inputScript.front().substr(6);
        inputScript.pop_front();
        inputNext = uiClock + 0.5f;
        if (want == "yes") chooseWorldSelect(*areaEvents, kTapWorldYes);
        else if (want == "no") chooseWorldSelect(*areaEvents, kTapWorldNo);
        else if (want == "back") chooseWorldSelect(*areaEvents, -1);
        else if (want == "list") chooseWorldSelect(*areaEvents, kTapWorldList);
        else if (want == "close") chooseWorldSelect(*areaEvents, kTapCollectClose);
        else if (want.compare(0, 4, "gift") == 0) chooseWorldSelect(*areaEvents, kTapWorldGift + std::atoi(want.c_str() + 4));
        else if (want.compare(0, 4, "help") == 0) chooseWorldSelect(*areaEvents, kTapCollect + std::atoi(want.c_str() + 4));
        else if (want.compare(0, 3, "row") == 0) chooseWorldSelect(*areaEvents, kTapWorldRow + std::atoi(want.c_str() + 3));
        else if (want.compare(0, 5, "point") == 0) chooseWorldSelect(*areaEvents, kTapWorldPoint + std::atoi(want.c_str() + 5));
        else if (want.compare(0, 5, "level") == 0) chooseWorldSelect(*areaEvents, kTapWorldLevel + std::atoi(want.c_str() + 5));
    }
    if (areaEvents && areaEvents->spacePort().open && !areaEvents->spacePort().talk && !inputScript.empty() &&
        inputScript.front().compare(0, 5, "port:") == 0 && spacePortMenu.clock > 0.8f && spacePortMenu.since > 0.5f && uiClock >= inputNext) {
        static const std::map<std::string, int> kPort = {{"talk", 0x11}, {"quests", 0x12}, {"explore", 0x13}, {"back", -1}, {"yes", kTapPortYes},
                                                         {"no", kTapPortNo}, {"ok", 500}, {"left", 501}, {"right", 502}, {"told", kTapPortTold}, {"shuffle", kTapPortShuffle}};
        const std::string want = inputScript.front().substr(5);
        inputScript.pop_front();
        inputNext = uiClock + 0.5f;
        if (const auto it = kPort.find(want); it != kPort.end()) chooseSpacePort(*areaEvents, it->second);
        else if (want.compare(0, 3, "row") == 0) chooseSpacePort(*areaEvents, 300 + std::atoi(want.c_str() + 3));
        else if (want.compare(0, 3, "cat") == 0) chooseSpacePort(*areaEvents, 400 + std::atoi(want.c_str() + 3));
    }
    // (CR3's camp: its clock; --input camp:open / genes / row<n> / card<n> / back / field<n> - a
    // field button, 0 Open .. 6 Shop)
    cr3Camp.since += dt;
    campOptions.askSince += dt;
    campQuests.detailSince += dt;
    campEquip.modelSince += dt;
    myPage.toldSince += dt;
    myPage.screenSince += dt;
    myPage.profileSince += dt;
    campItems.toldSince += dt;
    campGifts.toldSince += dt;
    vs.since += dt;
    vs.toldSince += dt;
    // (--input vs:open / battle / normal / special / row<n> / ok / next / back: the VS mode's, for tests)
    if (!inputScript.empty() && inputScript.front().compare(0, 3, "vs:") == 0 && uiClock >= inputNext && ((!cr3BattleMode && vs.screen != 5) || vs.wildOpen)) {
        const std::string want = inputScript.front().substr(3);
        inputScript.pop_front();
        inputNext = uiClock + 0.5f;
        if (want == "open") openVs();
        else if (want == "battle") chooseVs(1);
        else if (want == "normal" || want == "special") chooseVs(want == "normal" ? 3 : 4);
        else if (want.compare(0, 3, "row") == 0) chooseVs(100 + std::atoi(want.c_str() + 3));
        else if (want == "ok") chooseVs(200);
        else if (want == "next") chooseVs(300);
        else if (want == "back") chooseVs(-1);
        else if (want == "shop") chooseVs(2);
        else if (want == "box") chooseVs(1000); // (the Sp Shop's: the boxes, the goods)
        else if (want == "goods") chooseVs(1001);
        else if (want == "vip") chooseVs(1002);
        else if (want.compare(0, 4, "item") == 0) chooseVs(700 + std::atoi(want.c_str() + 4)); // (the goods' row n: focused, then asked)
        else if (want == "boxprev" || want == "boxpick" || want == "boxnext") chooseVs(want == "boxprev" ? 1100 : want == "boxpick" ? 1101 : 1102);
        else if (want.compare(0, 3, "buy") == 0) chooseVs(1110 + std::atoi(want.c_str() + 3));
        else if (want == "yes" || want == "no") chooseVs(want == "yes" ? 600 : 601);
        else if (want == "told") chooseVs(602);
        else if (want == "results") chooseVs(301);
        else if (want == "prizes") chooseVs(300);
        else if (want.compare(0, 5, "boost") == 0) chooseVs(500 + std::atoi(want.c_str() + 5));
        else if (want.compare(0, 7, "unboost") == 0) chooseVs(520 + std::atoi(want.c_str() + 7));
        else if (want.compare(0, 4, "wild") == 0) chooseVsWildCard(std::atoi(want.c_str() + 4));
        else if (want.compare(0, 4, "side") == 0) chooseVs(10 + std::atoi(want.c_str() + 4)); // (Battle's four small buttons)
        else if (want.compare(0, 5, "goods") == 0) chooseVs(700 + std::atoi(want.c_str() + 5));
    }
    if (!inputScript.empty() && inputScript.front().compare(0, 5, "camp:") == 0 && uiClock >= inputNext && (cr3Camp.stage > 0 || cr3FieldIdle())) {
        const std::string want = inputScript.front().substr(5);
        inputScript.pop_front();
        inputNext = uiClock + 0.5f;
        if (want == "open") openCr3Camp();
        else if (want == "genes") chooseCr3Camp(202);
        else if (want == "back") chooseCr3Camp(-1);
        else if (want.compare(0, 3, "row") == 0) chooseCr3Camp(300 + std::atoi(want.c_str() + 3));
        else if (want.compare(0, 4, "card") == 0) chooseCr3Camp(kTapCard + std::atoi(want.c_str() + 4));
        else if (want.compare(0, 5, "field") == 0) chooseCr3Camp(100 + std::atoi(want.c_str() + 5));
        else if (want.compare(0, 3, "pad") == 0) { // (camp:padup .. padtri: a controller's press as the Vita takes it - captures)
            static const std::map<std::string, PadPress> kPress = {{"up", PadPress::Up},         {"down", PadPress::Down},     {"left", PadPress::Left},
                                                                 {"right", PadPress::Right},   {"ok", PadPress::Confirm},    {"back", PadPress::Cancel},
                                                                 {"tri", PadPress::Triangle}};
            if (const auto p = kPress.find(want.substr(3)); p != kPress.end()) cr3BarPad(p->second);
        }
        else if (want.compare(0, 6, "button") == 0) chooseCr3Camp(200 + std::atoi(want.c_str() + 6));
        else if (want.compare(0, 3, "cat") == 0) chooseCr3Camp(400 + std::atoi(want.c_str() + 3));
        else if (want.compare(0, 2, "id") == 0) chooseCr3Camp(std::atoi(want.c_str() + 2)); // (a tap's id: id403 the Options' Graphics tab)
        else if (want.compare(0, 4, "reel") == 0) chooseCr3Camp(600 + std::atoi(want.c_str() + 4));
        else if (want.compare(0, 5, "chara") == 0) chooseCr3Camp(700 + std::atoi(want.c_str() + 5));
        else if (want == "yes" || want == "no" || want == "ok") chooseCr3Camp(want == "yes" ? 500 : want == "no" ? 501 : 502);
        else if (want == "model") chooseCr3Camp(800);
        else if (want == "mypage") openMyPage();
        else if (want.compare(0, 6, "scroll") == 0) myPageScroll(float(std::atof(want.c_str() + 6)));
        else if (want.compare(0, 4, "turn") == 0) cr3CampDrag(float(std::atof(want.c_str() + 4)), 0);
        else if (want.compare(0, 4, "zoom") == 0) cr3CampDrag(0, float(std::atof(want.c_str() + 4)));
    }
    // (--input call:<function>: tests - an area function run as if its gimmick were tapped)
    if (!scripted && !inputScript.empty() && inputScript.front().compare(0, 5, "call:") == 0) {
        const std::string want = inputScript.front().substr(5);
        inputScript.pop_front();
        SDL_Log("area %s: calls %s", area.name.c_str(), want.c_str());
        const bool here = std::any_of(areaGimmicks.begin(), areaGimmicks.end(), [&](const AreaGimmick& g) { return g.function == want; }) ||
                          std::any_of(areaNpcs.begin(), areaNpcs.end(), [&](const AreaNpc& n) { return n.name == want; });
        const size_t us = want.rfind('_'); // (a gimmick's name - cobj_05, c121_00 - only where it is; a script's own function anywhere)
        const bool gimmickLike = us != std::string::npos && want.size() - us == 3 && std::isdigit(static_cast<unsigned char>(want[us + 1])) && std::isdigit(static_cast<unsigned char>(want[us + 2]));
        if (here || !gimmickLike) runAreaFunction(want);
        else SDL_Log("area %s: %s not here", area.name.c_str(), want.c_str());
    }
    // (--input drag:<x0>/<y0>/<x1>/<y1> - a finger touched at the first screen fraction, moved to
    // the second, held - and drag:end, let go: tests)
    if (!(areaEvents && areaEvents->running()) && !inputScript.empty() && inputScript.front().compare(0, 5, "drag:") == 0) { // (live: a call: just before may have started one)
        const std::string want = inputScript.front().substr(5);
        inputScript.pop_front();
        float a = 0.5f, b = 0.5f, c = 0.5f, d = 0.5f;
        if (want == "end") areaDragEnd();
        else if (std::sscanf(want.c_str(), "%f/%f/%f/%f", &a, &b, &c, &d) == 4) areaDragStart(a, b), areaDragMove(c, d);
        SDL_Log("area %s: drag %s", area.name.c_str(), want.c_str());
    }
    // (--input touch:<x0>/<y0>/<x1>/<y1> - a finger as SDL gives it: pressed, the camera's half or
    // the walk's by the Options' Controls, moved and held; touch:end lets go; mouse2:... the right
    // button's drag: tests)
    if (!(areaEvents && areaEvents->running()) && !inputScript.empty() &&
        (inputScript.front().compare(0, 6, "touch:") == 0 || inputScript.front().compare(0, 7, "mouse2:") == 0)) {
        const bool mouse = inputScript.front()[0] == 'm';
        const std::string want = inputScript.front().substr(mouse ? 7 : 6);
        inputScript.pop_front();
        float a = 0.5f, b = 0.5f, c = 0.5f, d = 0.5f;
        if (want == "end") {
            if (areaCam.on) areaCamEnd();
            if (areaDrag.on) areaDragEnd();
        } else if (std::sscanf(want.c_str(), "%f/%f/%f/%f", &a, &b, &c, &d) == 4) {
            if (mouse) areaCamStart(a, b, true);
            else pointerTouch = true, pointerDown(a, b), pointerTouch = false;
            if (areaCam.on) areaCamMove(c, d);
            else if (areaDrag.on) areaDragMove(c, d);
        }
        SDL_Log("area %s: %s %s - camera %s, walk %s", area.name.c_str(), mouse ? "mouse2" : "touch", want.c_str(), areaCam.on ? "held" : "free",
                areaDrag.on ? "held" : "free");
    }
    // (--input near:<gimmick>[:<distance>]: tests - the player stood 1.5 (or that) from it on its side, facing it)
    if (!(areaEvents && areaEvents->running()) && !inputScript.empty() && inputScript.front().compare(0, 5, "near:") == 0) {
        std::string want = inputScript.front().substr(5);
        float away = 1.5f;
        if (const size_t colon = want.find(':'); colon != std::string::npos) away = float(std::atof(want.c_str() + colon + 1)), want.resize(colon);
        inputScript.pop_front();
        std::vector<AreaGimmick> near = areaGimmicks;
        for (const AreaNpc& npc : areaNpcs) // (a rock, a box: by its place)
            if (npc.name == want) near.push_back({npc.name, false, npc.place, false, {}});
        for (const AreaGimmick& g : near)
            if (g.function == want || (!g.boxName.empty() && g.boxName == want)) {
                const cr3::Vec3 p = areaWalker->position();
                float dx = p[0] - g.world.m[3], dz = p[2] - g.world.m[11];
                const float l = std::max(std::hypot(dx, dz), 1e-3f);
                dx /= l, dz /= l;
                // (the side toward the player first, then round it: the first that lands at the
                // object's height - not dropped off a walkway into what's below)
                const float from = std::atan2(dz, dx);
                for (int t = 0; t < 8; ++t) {
                    const float a = from + float(t) * 0.785398f;
                    const float ex = std::cos(a), ez = std::sin(a);
                    areaWalker->setPosition({g.world.m[3] + ex * away, g.world.m[7] + 0.5f, g.world.m[11] + ez * away});
                    for (int k = 0; k < 60; ++k) areaWalker->setVelocity({0, 0, 0}), areaWalker->step();
                    if (std::fabs(areaWalker->position()[1] - g.world.m[7]) < 2.5f || t == 7) {
                        if (std::fabs(areaWalker->position()[1] - g.world.m[7]) >= 2.5f) // (none: the player's side)
                            areaWalker->setPosition({g.world.m[3] + dx * away, g.world.m[7] + 0.5f, g.world.m[11] + dz * away});
                        else dx = ex, dz = ez;
                        break;
                    }
                }
                for (int k = 0; k < 60; ++k) areaWalker->setVelocity({0, 0, 0}), areaWalker->step();
                areaYaw = std::atan2(-dx, -dz);
                const cr3::Vec3 q = areaWalker->position();
                areaCamera.reset({q[0], q[1] - areaWalker->radius, q[2]}, areaYaw), rail.started = false;
                SDL_Log("area %s: near %s at (%.2f %.2f %.2f)", area.name.c_str(), want.c_str(), q[0], q[1], q[2]);
            }
    }
    // (--input check: tests - a tap on the check object the player is by, as the field takes it)
    if (!(areaEvents && areaEvents->running()) && !inputScript.empty() && inputScript.front() == "check" && cr3FieldIdle()) {
        inputScript.pop_front();
        SDL_Log("area %s: check %s", area.name.c_str(), areaCheckNear >= 0 ? areaGimmicks[size_t(areaCheckNear)].function.c_str() : "(none near)");
        tap();
    }
    // (--input box:<event box>: tests - the player put in that box, set off as if walked in)
    if (!(areaEvents && areaEvents->running()) && !inputScript.empty() && inputScript.front().compare(0, 4, "box:") == 0) {
        const std::string want = inputScript.front().substr(4);
        inputScript.pop_front();
        for (const AreaGimmick& g : areaGimmicks)
            if (g.function == want) {
                if (g.shaped) areaWalker->setPosition({g.boxAt[0] + (g.boxLo[0] + g.boxHi[0]) / 2, g.boxAt[1] + (g.boxLo[1] + g.boxHi[1]) / 2, g.boxAt[2] + (g.boxLo[2] + g.boxHi[2]) / 2});
                else areaWalker->setPosition({g.world.m[3], g.world.m[7], g.world.m[11]}); // (held in it - a drop may miss a small one, or a floor's edge)
                SDL_Log("area %s: put in %s", area.name.c_str(), want.c_str());
            }
    }
    // (--input wander:<seconds>: tests - the player runs round in a small circle that long, the
    // encounters' steps counted)
    if (!(areaEvents && areaEvents->running()) && !inputScript.empty() && inputScript.front().compare(0, 7, "wander:") == 0) {
        wanderLeft = std::max(float(std::atof(inputScript.front().c_str() + 7)), 0.0f);
        inputScript.pop_front();
        SDL_Log("area %s: wander %.0f s", area.name.c_str(), wanderLeft);
    }
    if (wanderLeft > 0 && !(areaEvents && areaEvents->running())) {
        wanderLeft -= dt, wanderTurn += dt * 2.0f;
        sx = std::cos(wanderTurn), sy = std::sin(wanderTurn), run = true;
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys) areaCamera.turn(float(keys[SDL_SCANCODE_E] - keys[SDL_SCANCODE_Q]) * 1.5f * dt, float(keys[SDL_SCANCODE_R] - keys[SDL_SCANCODE_V]) * 0.8f * dt);
    areaCamStep(dt); // (the camera's half held: Fld2CameraGom::UpdateControl)
    const float fx = areaCamera.at[0] - areaCamera.eye[0], fz = areaCamera.at[2] - areaCamera.eye[2];
    const float fl = std::max(std::hypot(fx, fz), 1e-4f);
    const float forward[2] = {fx / fl, fz / fl}, right[2] = {-forward[1], forward[0]};
    float dx = forward[0] * sy + right[0] * sx, dz = forward[1] * sy + right[1] * sx;
    const float push = std::min(std::hypot(sx, sy), 1.0f);
    const bool moving = push > 0.01f && std::hypot(dx, dz) > 1e-6f; // (no way across: the camera straight above)
    const float speed = moving ? (run ? 4.8f : 1.2f) : 0.0f; // (SM_Walk / SM_Run: fixed 1.2 / 4.8, never scaled by stick push)
    float moveAngle = areaYaw;
    if (moving) {
        const float dl = std::hypot(dx, dz);
        dx /= dl, dz /= dl;
        // (RotateToMoveDir: turning to face the way it goes)
        moveAngle = std::atan2(dx, dz);
        const float diff = std::remainder(moveAngle - areaYaw, 6.2831853f);
        areaYaw += std::copysign(std::min(std::fabs(diff), 12.0f * dt), diff);
    }
    const int steps = std::max(1, int(std::ceil(dt * 60 - 1e-3f)));
    for (int k = 0; k < steps; ++k) {
        areaWalker->setVelocity({dx * speed, 0, dz * speed});
        areaWalker->step(dt / float(steps));
    }
    setAreaMotion(!moving ? cr3::Cr3Motion::Idle : run ? cr3::Cr3Motion::Run : cr3::Cr3Motion::Walk);
    const cr3::Vec3 p = areaWalker->position();
    const bool touchMove = moving && areaDrag.on && areaDragFree() && !areaCam.on && !areaCamSticking;
    areaCamera.step({p[0], p[1] - areaWalker->radius, p[2]}, moveAngle, moving, dt, &area.cameraWalls, &area.walls, touchMove);
    if (rail.on) railStep({p[0], p[1] - areaWalker->radius, p[2]}, p, dt);
    // the gimmicks: an event box walked into sets off its function; the nearest check object
    areaCheckNear = -1;
    float bestCheckDist = 1e30f;
    for (size_t k = 0; k < areaGimmicks.size(); ++k) {
        AreaGimmick& g = areaGimmicks[k];
        const bool in = gimmickHolds(g);
        if (!g.areaChange.empty()) { // (Fld2AreaChange::CollisionEventF: SetNextEvent, then SM_ChangeArea to its point - a SetPopPoint2's first)
            if (in && !g.inside && !scripted) {
                const std::string pop = game.popPoint2.empty() ? g.areaChange : game.popPoint2;
                enterPopPoint(pop);
                return;
            }
            g.inside = in;
            continue;
        }
        if (g.eventBox && in && !g.inside && !scripted) runAreaFunction(g.function);
        // (a check object: near it across - Fld2TaskPlayer's gimmick ghost, a sphere of 1.0,
        // against its box; the nearest within range taken)
        if (!g.eventBox && game.gimmickVisible(g.boxName.empty() ? g.function : g.boxName) &&
            !(g.box >= 0 && size_t(g.box) < game.cr3Boxes.size() && game.cr3Boxes[size_t(g.box)].opened)) {
            const float across = std::hypot(p[0] - g.world.m[3], p[2] - g.world.m[11]);
            const float half = std::sqrt(g.world.m[0] * g.world.m[0] + g.world.m[4] * g.world.m[4] + g.world.m[8] * g.world.m[8]) * 0.05f;
            const float range = g.checkRange > 0 ? g.checkRange : 1.0f + half + 0.3f;
            if (across <= range && across < bestCheckDist && std::fabs(p[1] - g.world.m[7]) < 2.5f) bestCheckDist = across, areaCheckNear = int(k);
        }
        g.inside = in;
    }
    // (--input encount:force - the field's Battle button in a dungeon -: tests)
    if (!scripted && !inputScript.empty() && inputScript.front() == "encount:force") {
        inputScript.pop_front();
        forceEncounter();
        return;
    }
    for (AreaNpc& npc : areaNpcs)
        if (npc.box) stepAreaBox(npc, dt);
    stepFieldBombs(dt);
    stepAreaMovers(dt);
    stepStepSwitches();
    updateGimmickWalls();
    if (!fieldPendingFunctions.empty() && areaEvents && !areaEvents->running()) {
        const std::string fn = fieldPendingFunctions.front();
        fieldPendingFunctions.erase(fieldPendingFunctions.begin());
        runAreaFunction(fn);
    }
    // (--input goods:<n> - the field's goods button n: tests)
    if (!scripted && !inputScript.empty() && inputScript.front().compare(0, 6, "goods:") == 0 && cr3FieldIdle()) {
        const bool show = inputScript.front().rfind("goods:show", 0) == 0;
        const int n = std::atoi(inputScript.front().c_str() + (show ? 10 : 6));
        inputScript.pop_front();
        if (show) game.goodsButtons.insert(n); // fixture: same visibility as the script's goods button
        else useGoods(n);
    }
    // (Fld2Main::SM_Camp 5: the camp closed, the area script's evt001 - the story's steps that
    // wait on the camp: s101_d02_00606_set, a gift merged)
    if (cr3CampWasOpen && cr3Camp.stage == 0 && !scripted) runAreaFunction("evt001");
    cr3CampWasOpen = cr3Camp.stage != 0;
    if (game.cr3BoxBattle >= 0 && areaEvents && !areaEvents->running() && !cr3BattleRequest.active && areaWalker) { // (a box's monsters: Fld2TaskEncount, its coupling)
        AreaEncounters::Coupling met;
        met.id = game.cr3BoxBattle;
        game.cr3BoxBattle = -1;
        startEncounter(met);
    }
    if (game.cr3GBoxShow && areaEvents && !areaEvents->running()) { // (Fld2Main::SM_OpenGBox: the dungeon's sheet, the prize marked)
        game.cr3GBoxShow = false;
        const cr3::Cr3ItemSheet::Prize* won = game.cr3Db ? game.cr3Db->openedPrize() : nullptr;
        areaEvents->collectListOpen(game.dungeon - 4, won && won->gene ? 1 : 0, won ? won->item : -1);
    }
    if (cr3FieldIdle() && !cr3BattleRequest.active) countEncounter();
    if (cr3SavePending && uiClock >= cr3SaveRetryAt && cr3FieldIdle() && !cr3BattleRequest.active)
        writeCr3Save(); // a failed save stays pending, with a five-second retry interval
}

void Viewer::drawArea(int w, int h) {
    // the camera: at its eye, looking at the player (its z axis back from the target); the
    // script's while it has one
    const auto& e = areaScriptCam.on ? areaScriptCam.eye : areaCamera.eye;
    const auto& t = areaScriptCam.on ? areaScriptCam.at : areaCamera.at;
    float z[3] = {e[0] - t[0], e[1] - t[1], e[2] - t[2]};
    const float zl = std::max(std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]), 1e-5f);
    for (float& c : z) c /= zl;
    float x[3] = {z[2], 0, -z[0]}; // (up x z)
    const float xl = std::max(std::hypot(x[0], x[2]), 1e-5f);
    x[0] /= xl, x[2] /= xl;
    const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    cr3::Mat34 cw;
    for (int r = 0; r < 3; ++r) cw.m[r * 4 + 0] = x[r], cw.m[r * 4 + 1] = y[r], cw.m[r * 4 + 2] = z[r], cw.m[r * 4 + 3] = e[r];
    const float fov = areaCamera.fovY * 3.14159265f / 180;
    dev->setCamera(areaCamera.zNear, areaCamera.zFar, fov);
    const Mat4 world = cr3::mat4FromMat34(cw);
    Mat4 proj = perspective(fov, float(w) / float(std::max(h, 1)), areaCamera.zNear, areaCamera.zFar);
    applyQuake(proj);
    const Mat4 vp = cr3::mat4Multiply(proj, cr3::mat4RigidInverse(world));
    cr3::SceneLighting l = lighting;
    l.camera[0] = e[0], l.camera[1] = e[1], l.camera[2] = e[2];
    l.time = time;
    dev->setLighting(l);
    // the area (Fld2TaskMap::OnDraw: <area>f, <area>s 0.01 up, <area>m1), its animations looping
    for (size_t k = 0; k < areaFigures.size(); ++k) {
        ViewFigure& f = areaFigures[k];
        f.place = k < area.places.size() ? area.places[k] : cr3::Mat34();
        if (k < area.figures.size() && area.figures[k] == area.name + "s") f.place.m[7] = 0.01f;
        pose(f, f.animLoaded && f.anim.duration() > 0 ? std::fmod(areaClock, f.anim.duration()) : 0.0f, false);
        drawFigure(f, vp.data());
    }
    drawFieldEffects(vp.data(), cw);
    drawFieldBombs(vp.data());
    for (AreaNpc& npc : areaNpcs) { // (Fld2NPC::Draw, Fld2Box::Draw)
        if (!game.gimmickVisible(npc.name) || npc.broken) continue;
        if (npc.box) {
            if (npc.fade <= 0) continue;
            ViewFigure& f = npc.body;
            f.place = npc.place;
            f.tint[3] = npc.fade;
            pose(f, npc.openAt >= 0 ? areaClock - npc.openAt : 0.0f, false);
            drawFigure(f, vp.data());
            if (npc.openAt >= 0 && boxGet.animLoaded && areaClock - npc.openAt < boxGet.anim.duration()) { // (+0x170: 0.1 up)
                boxGet.place = npc.place, boxGet.place.m[7] += 0.1f;
                pose(boxGet, areaClock - npc.openAt, false);
                drawFigure(boxGet, vp.data());
            }
            continue;
        }
        if (npc.stepSwitch) { // (up, going down, or down - its _n01's end)
            const float dur = npc.body.animLoaded ? npc.body.anim.duration() : 0.0f;
            npc.body.place = npc.place;
            pose(npc.body, npc.stepState == 0 ? 0.0f : npc.stepState == 1 ? std::min(areaClock - npc.stepAt, dur) : dur, false);
            drawFigure(npc.body, vp.data());
            continue;
        }
        if (npc.bombSwitch && game.switchesOn.count(npc.name) && !npc.alt.meshes.empty()) { // (Exchange: its other model)
            npc.alt.place = npc.place;
            pose(npc.alt, 0, false);
            drawFigure(npc.alt, vp.data());
            continue;
        }
        for (ViewFigure* fp : {&npc.body, &npc.head}) {
            if (fp->meshes.empty()) continue;
            fp->place = npc.place;
            pose(*fp, npc.eventMotion ? areaClock - npc.motionAt : areaClock + npc.phase, true);
            drawFigure(*fp, vp.data());
        }
    }
    // the player: where it walks, facing its way
    for (ViewFigure* fp : {&areaPlayer, &areaPlayerHead}) {
        ViewFigure& f = *fp;
        if (f.meshes.empty()) continue;
        const cr3::Vec3 p = areaWalker->position();
        f.place = cr3::Mat34();
        const float c = std::cos(areaYaw), s = std::sin(areaYaw);
        f.place.m[0] = c, f.place.m[2] = s, f.place.m[8] = -s, f.place.m[10] = c;
        f.place.m[3] = p[0], f.place.m[7] = p[1] - areaWalker->radius, f.place.m[11] = p[2];
        f.tint[3] = areaCamera.playerAlpha; // (Fld2TaskPlayer::SetAlpha: the camera too near)
        f.blendWeight = f.blendLoaded ? std::min(areaMotionAge * 6.0f, 1.0f) : 1.0f; // (SetAnimationBlend 1/6 s)
        pose(f, areaMotionAge, true);
        drawFigure(f, vp.data());
    }
}

// the dungeon and area the map is in (cr3_field_data +0 / +4: FldMap's table maps them to the
// maps - t01MM dungeon 0, dNNMM dungeon NN, the area MM - 1)
int Viewer::dungeonNum() const {
    const std::string& n = field.info().name;
    return n.size() >= 5 && n[0] == 'd' ? std::atoi(n.substr(1, 2).c_str()) : 0;
}

int Viewer::areaNum() const {
    const std::string& n = field.info().name;
    return n.size() >= 5 ? std::max(std::atoi(n.substr(3, 2).c_str()) - 1, 0) : 0;
}

void Viewer::drawDungeonMap(const Mat4& view) {
    static const short kStart[15] = {0, 14, 21, 38, 53, 69, 83, 98, 114, 129, 144, 164, 212, 213, 220};
    static const short kGoalFlags[222] = {
        2151, 2152, 2153, 2154, 2155, 2156, 2157, 2158, 2159, 2160, 2161, 2162, 2163, 2164, 2165, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 2166, 2167, 0, 0, 0, 0, 0, 2168, 0, 0, 0, 2169, 0, 0, 0, 0, 2170, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 2171, 2172, 0, 0, 0, 2173, 0, 0, 0, 0, 0, 2174, 0, 0, 0, 2175, 0, 0, 0, 0, 0, 0, 2176, 0, 0, 0, 0, 0, 0, 0, 2177, 0, 0,
        0, 0, 0, 0, 0, 0, 2178, 0, 0, 0, 0, 0, 0, 2179, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2180, 0, 0, 0, 0, 2181, 0, 0, 0, 0, 0, 0, 2182,
        0, 0, 0, 0, 0, 0, 0, 2183, 2184, 0, 0, 2185, 0, 0, 2186, 0, 2187, 0, 0, 0, 0, 0, 0, 0, 0, 2188, 0, 2189, 0, 0, 0, 0, 0, 2190,
        0, 0, 0, 0, 2191, 2192, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2193, 0, 0, 0, 2194, 0, 0, 0, 0, 0, 2195, 0, 0, 0, 2196, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2197, 0}; // (the library's, 0x535794)
    const float t = uiClock - mapStart;
    const int d = std::clamp(dungeonNum(), 0, 13);
    char suffix[8];
    std::snprintf(suffix, sizeof suffix, d == 0 ? "t01" : "d%02d", d);
    UiFig& guide = uiFig("mapguide_io");
    guide.fig.place = cr3::Mat34();
    poseUi(guide, "mapguide_io", t);
    if (cr3::Texture* tex = texture(std::string("guide_") + suffix))
        for (ViewMesh& vm : guide.fig.meshes) vm.texture = tex;
    drawFigure(guide.fig, view.data());
    UiFig& info = uiFig("mapinfo");
    info.fig.place = cr3::Mat34();
    const std::string anim = std::string("mapinfo_") + suffix;
    auto mark = [&](float frame, const char* joint, const char* fig, bool loop) {
        poseUi(info, anim, frame / 20.0f);
        float x, y;
        if (!uiJoint(info, joint, x, y)) return;
        UiFig& m = uiFig(fig);
        m.fig.place = cr3::Mat34();
        m.fig.place.m[3] = x, m.fig.place.m[7] = y;
        poseUi(m, fig, t, loop);
        drawFigure(m.fig, view.data());
    };
    if (d < 14)
        for (int k = kStart[d]; k < (d + 1 < 15 ? kStart[d + 1] : 222) && k < 222; ++k)
            if (kGoalFlags[k] && game.flag(kGoalFlags[k])) mark(float(k - kStart[d] + 1), "call_mapGoal", "mapgoal", false);
    mark(float(areaNum() + 1), "call_mapSymbol", "mapsymbol", true);
}

void Viewer::restoreFieldPosition(const float* position, float yaw) {
    player[0] = position[0], player[1] = position[1], player[2] = position[2], heading = yaw;
    // The save stores the rendered feet position, 0.02 below the controller.
    // Restore both physics and interpolation before the next displayed frame.
    if (controller) {
        controller->setPosition({player[0], player[1] + 0.02f, player[2]});
        controller->setVelocity({0, 0, 0});
    }
    std::memcpy(playerPrev, player, sizeof player);
    std::memcpy(playerCur, player, sizeof player);
    stepClock = 0, substep = 0;
    exitsArmed = false;
    insideEvents.clear();
    const float centre[3] = {player[0], player[1] + 0.02f, player[2]};
    for (const cr3::Gimmick& g : field.gimmicks())
        if (g.kind == cr3::Gimmick::Event && !g.check && g.touches(centre, controller ? controller->radius : 0.02f))
            insideEvents.insert(g.name);
    checkBalloons();
    fig.place = cr3::Mat34();
    fig.place.m[3] = player[0], fig.place.m[7] = player[1], fig.place.m[11] = player[2];
}

bool Viewer::fieldTabShown() const {
    return fieldMode && !eventMode && !battleMode && script && !script->running() && !campOpen && !mapOpen && !saveStage &&
           keyLock.stage == 0 && changeStage == 0 && !actionRunning() && world.stage == 0 && shop.stage == 0 && party.stage == 0;
}

float Viewer::fieldTabPose(UiFig& tab) {
    const float d = tab.fig.animLoaded ? tab.fig.anim.duration() : 0.0f;
    const float e = std::clamp(uiClock - fieldTabAt, 0.0f, d);
    return fieldTabOpen ? e : d - e;
}

void Viewer::drawFieldTab(const Mat4& view) {
    UiFig& tab = uiFig("fldmenu_tab");
    tab.fig.place = cr3::Mat34();
    poseUi(tab, "fldmenu_tab", 0);
    poseUi(tab, "fldmenu_tab", fieldTabPose(tab));
    // (the encounters' sign: GEO_encOn / GEO_encOff by flag 0x40e - not had, both away)
    for (ViewMesh& vm : tab.fig.meshes) {
        const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
        const std::string jn = j < tab.joints.size() ? tab.joints[j] : "";
        vm.hidden = ((jn == "GEO_encOn" || jn == "GEO_encOff") && !game.flag(0x40e)) || (jn == "GEO_encOn" && encountersOff) || (jn == "GEO_encOff" && !encountersOff);
    }
    // (the buttons off at half: the map in dungeon 12, saving with flag 0x3ea, the encounters' without 0x40f)
    for (ViewMesh& vm : tab.fig.meshes) {
        const uint32_t mi = vm.source.material;
        const std::string mn = mi < tab.materials.size() ? tab.materials[mi] : "";
        const bool off = (mn == "PRG_map" && dungeonNum() == 12) || (mn == "PRG_save" && game.flag(0x3ea)) || (mn == "PRG_enc" && !game.flag(0x40f));
        const bool lit = fieldTabOpen && ((tabFocus == 2 && mn == "PRG_camp") || (tabFocus == 5 && mn == "PRG_enc") ||
                                          (tabFocus == 4 && mn == "PRG_save") || (tabFocus == 3 && mn == "PRG_map"));
        vm.material.params[0x33] = off ? std::vector<float>{0.5f, 0.5f, 0.5f, 1.0f} : lit ? std::vector<float>{1.0f, 2.0f, 3.0f, 1.0f} : vm.baseMaterial.params[0x33];
        if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
    }
    drawFigure(tab.fig, view.data());
    for (int k = 1; k <= 5; ++k) {
        char sa[16], ea[16];
        std::snprintf(sa, sizeof sa, "tapAreaS%02d", k), std::snprintf(ea, sizeof ea, "tapAreaE%02d", k);
        float sx, sy, ex, ey;
        if (uiJoint(tab, sa, sx, sy) && uiJoint(tab, ea, ex, ey)) tapRects.push_back({6000 + k, sx, sy, ex, ey});
    }
}

void Viewer::fieldTabTap(int area) {
    if (!game.tabEnable) return;
    UiFig& tab = uiFig("fldmenu_tab");
    const bool opened = fieldTabOpen && uiClock - fieldTabAt >= (tab.fig.animLoaded ? tab.fig.anim.duration() : 0.0f);
    if (area == 1) { // (the tab: open or closed, from where its animation is)
        const float d = tab.fig.animLoaded ? tab.fig.anim.duration() : 0.0f;
        const float now = fieldTabPose(tab);
        fieldTabOpen = !fieldTabOpen;
        fieldTabAt = uiClock - (fieldTabOpen ? now : d - now);
        audio.playSe(fieldTabOpen ? 0xba : 0xbb, false);
        SDL_Log("field: the tab %s", fieldTabOpen ? "opened" : "closed");
        return;
    }
    if (!opened) return;
    if (area == 2) {
        audio.playSe(0xb4, false);
        openCamp();
    } else if (area == 3 && dungeonNum() != 12) { // (FldDungeonMap)
        audio.playSe(0xb4, false);
        fieldTabOpen = false, fieldTabAt = -10;
        mapOpen = true, mapStart = uiClock;
        SDL_Log("field: map of dungeon %d, area %d", dungeonNum(), areaNum());
    } else if (area == 4 && !game.flag(0x3ea)) { // (FldSaveLoad)
        audio.playSe(0xb4, false);
        fieldTabOpen = false, fieldTabAt = -10;
        saveStage = 1, saveStart = uiClock;
    } else if (area == 5 && game.flag(0x40e) && game.flag(0x40f)) {
        audio.playSe(0xb4, false);
        encountersOff = !encountersOff;
        SDL_Log("field: encounters %s", encountersOff ? "off" : "on");
    }
}

// a click on the field's UI (the tab, the camp): false - none of it there (the field's own tap)
bool Viewer::fieldClick(float fx, float fy) {
    if (mapOpen) { // (any tap: closed)
        audio.playSe(0xb5, false), mapOpen = false;
        return true;
    }
    if (saveStage == 2) return true;
    if (party.stage) { // (the party's buttons, faces)
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        for (const TapRect& r : tapRects)
            if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
                partyTap(r.id);
                break;
            }
        return true;
    }
    if (shop.stage) { // (the shop's buttons, rows, question)
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        for (const TapRect& r : tapRects)
            if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
                shopTap(r.id);
                break;
            }
        return true;
    }
    if (world.stage || library.stage == 1 || pub.stage) { // (the world select's rows, its question; the library's rows; the pub's)
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        for (const TapRect& r : tapRects)
            if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
                world.stage ? worldTap(r.id) : pub.stage ? pubTap(r.id) : libraryTap(r.id);
                break;
            }
        return true;
    }
    if (!campOpen && !fieldTabShown() && !saveStage) return false;
    if (campOpen && manualStage == 2) {
        if (manualEvents) manualEvents->advance();
        return true;
    }
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    for (const TapRect& r : tapRects)
        if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
            if (r.id > 6000 && r.id <= 6005) fieldTabTap(r.id - 6000);
            else if (r.id == 6100) startChange();
            else if (saveStage == 1 && (r.id == 7401 || r.id == 7402)) saveAnswer(r.id == 7401);
            else if (campOpen && optionsTapped(r, x)) {
            } else if (campOpen && campUse) campUseTap(r.id);
            else if (campOpen && campList == 17 && r.id >= 1000 && r.id < 5000) campEquipCursor = r.id - 1000;
            else if (campOpen && campList && r.id >= 1000 && r.id < 5000) campCursor = r.id - 1000; // (a row: the focus)
            else if (campOpen && campList == 16 && r.id > 0 && r.id < 1000) campPickItem(r.id); // (the focused row: picked)
            else if (campOpen && r.id >= 0) campPress(r.id);
            return true;
        }
    return campOpen; // (the camp takes every tap)
}

// --input in the field: "tab", "camp" (the tab opened, then Camp), "back", "items", "sopia",
// "equip", "options", "category"; the options' own (as in battle); false - not one of these
bool Viewer::fieldToken(const std::string& t) {
    if (t == "tap" && fieldTabShown()) tap(); // (the leader's tap: the nearest balloon's)
    else if (t == "change" && fieldTabShown()) startChange(); // (the character change's button)
    else if (t == "sprint" && sprintAt() >= 0) startSprint(sprintAt());
    else if (t == "action" && fieldTabShown() && actionMark.gimmick >= 0) actionTapped(); // (its mark tapped)
    else if (t == "tab" && fieldTabShown()) fieldTabTap(1);
    else if (t == "camp" && fieldTabShown()) fieldTabOpen = true, fieldTabAt = -10, fieldTabTap(2);
    else if (t == "map" && fieldTabShown()) fieldTabOpen = true, fieldTabAt = -10, fieldTabTap(3);
    else if (t == "save" && fieldTabShown()) fieldTabOpen = true, fieldTabAt = -10, fieldTabTap(4);
    else if (t == "close" && mapOpen) audio.playSe(0xb5, false), mapOpen = false;
    else if (saveStage == 1 && (t == "yes" || t == "no")) saveAnswer(t == "yes");
    else if (!campOpen) return false;
    else if (settingsOpen && (t == "up" || t == "down" || t == "left" || t == "right" || t == "enter"))
        settingsKey(t == "up" ? SDLK_UP : t == "down" ? SDLK_DOWN : t == "left" ? SDLK_LEFT : t == "right" ? SDLK_RIGHT : SDLK_RETURN);
    else if (t == "back" && settingsOpen) closeSettings();
    else if (t == "back") manualStage == 1 ? closeManual() : optionsAsk ? optionTap(3202) : optionsOpen ? void(optionsOpen = false) : campPress(100);
    else if (t == "items") campPress(16);
    else if (t == "scenarios") campPress(201);
    else if (campLink == 201 && (t == "down" || t == "up")) scrollScenarios(t == "down" ? 0.3f : -0.3f);
    else if (t == "pub") campPress(202);
    else if (t == "equip") campPress(17);
    else if (t == "sopia") campPress(18);
    else if (t == "options") campPress(20);
    else if (t == "category") campCategory = 1 - campCategory, campCursor = 0;
    else if (t.size() > 1 && t[0] == 'u' && std::isdigit(static_cast<unsigned char>(t[1]))) campPickItem(std::atoi(t.c_str() + 1)); // (an item used: uNNN)
    else if ((campUse || campList == 17) && (t == "left" || t == "right")) campKey(t == "left" ? SDLK_LEFT : SDLK_RIGHT);
    else if (campTakeSopia && (t == "yes" || t == "no")) campTakeAnswer(t == "yes");
    else if (campUse && (t == "yes" || t == "no")) campUseTap(t == "yes" ? 7201 : 7202);
    else if (t == "down") campKey(SDLK_DOWN);
    else if (t == "up") campKey(SDLK_UP);
    else if (t == "enter") campKey(SDLK_RETURN);
    else {
        static const std::pair<const char*, int> kOptionTaps[] = {{"title", 3101}, {"manual", 3102}, {"quality-", 3103}, {"quality+", 3104},
                                                                  {"language-", 3105}, {"language+", 3106}, {"yes", 3201}, {"no", 3202},
                                                              {"graphics", 3110}};
        for (const auto& [n, id] : kOptionTaps)
            if (t == n && (optionsOpen || optionsAsk)) {
                optionTap(id);
                SDL_Log("input %s", t.c_str());
                return true;
            }
        return false;
    }
    SDL_Log("input %s", t.c_str());
    return true;
}

void Viewer::refreshLeader() {
    if (!leaderFollows || !fieldMode || !script) return;
    const int c = script->leader();
    if (c < 0 || c > 6) return;
    char want[16];
    std::snprintf(want, sizeof want, "c%03d.geom", c + 1);
    if (fig.name == want) return;
    const auto it = std::find(models.begin(), models.end(), want);
    if (it == models.end()) return;
    const float keep = time;
    load(size_t(it - models.begin()));
    time = keep;
    playMotion(fig, "fn01");
}

bool Viewer::changeButtonShown() const {
    return fieldTabShown() && game.changeEnable && game.flag(0x3fb) && script && script->fieldCharacter(1) >= 0;
}

void Viewer::startChange() {
    if (changeStage || !changeButtonShown()) return;
    audio.playSe(0xb4, false);
    changeStage = 1, changeFrames = 0;
}

void Viewer::updateChange(float dt) {
    if (!changeStage) return;
    changeFrames += dt * 60;
    if (changeStage == 1) {
        const float k = std::max(1 - changeFrames / 16, 0.0f);
        for (int c = 0; c < 3; ++c) fig.tint[c] = k;
        if (changeFrames < 16) return;
        game.active = game.active == 0 ? 1 : 0;
        SDL_Log("%s: the leader now character %d", field.info().name.c_str(), script->leader());
        refreshLeader();
        changeStage = 2, changeFrames = 0;
        for (int c = 0; c < 3; ++c) fig.tint[c] = 0;
        return;
    }
    const float k = std::min(changeFrames / 32, 1.0f);
    for (int c = 0; c < 3; ++c) fig.tint[c] = k;
    if (changeFrames >= 32) changeStage = 0;
}

void Viewer::drawChangeButton(const Mat4& view) {
    if (!changeButtonShown()) return;
    UiFig& b = uiFig("chrtrade");
    b.fig.place = cr3::Mat34();
    poseUi(b, "chrtrade", 0);
    drawFigure(b.fig, view.data());
    float sx, sy, ex, ey;
    for (const std::string& j : b.joints)
        if (j.compare(0, 8, "tapAreaS") == 0 && uiJoint(b, j, sx, sy) && uiJoint(b, "tapAreaE" + j.substr(8), ex, ey))
            tapRects.push_back({6100, sx, sy, ex, ey});
}

void Viewer::drawFieldUi(int w, int h) {
    if (!fieldMode || battleMode || eventMode) return;
    const bool tabShown = fieldTabShown();
    if (!campOpen && !tabShown && !mapOpen && !saveStage && !world.stage && !shop.stage && !party.stage) return;
    const Mat4 proj = uiProjection(w, h);
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f;
    const Mat4 view = cr3::mat4Multiply(proj, camera);
    cr3::SceneLighting l;
    dev->setLighting(l);
    dev->clearDepth();
    if (campOpen) drawCamp(view, proj, w, h);
    else if (mapOpen) tapRects.clear(), drawDungeonMap(view);
    else if (saveStage) tapRects.clear(), drawSave(view, proj, w, h);
    else if (world.stage) tapRects.clear(), drawWorldSelect(view, w, h);
    else if (library.stage == 1) tapRects.clear(), drawLibrary(view, w, h);
    else if (shop.stage) tapRects.clear(), drawShop(view, proj, w, h);
    else if (pub.stage) tapRects.clear(), drawPub(view, w, h);
    else if (party.stage) tapRects.clear(), drawParty(view, proj, w, h);
    else {
        tapRects.clear();
        if (game.tabEnable) drawFieldTab(view); // (Cr3TabMenu.SetEnable)
        drawChangeButton(view);
    }
    // the battery (BatteryWindowMenu: with the camp, the shop, the pub - Set*Parameter's
    // SetBatteryWindowParameter -, and in the field once its level is under 20%, looked at
    // every 10 s - InterfaceMain::Update)
    if (campOpen || shop.stage || pub.stage) drawBattery(view);
    else if (tabShown && batteryLow) drawBattery(view);
}

void Viewer::updateBattery() {
    if (uiClock - batteryFrameAt < 1.0f) return;
    batteryFrameAt = uiClock;
    int pct = -1;
    const SDL_PowerState st = SDL_GetPowerInfo(nullptr, &pct);
    const float level = pct < 0 ? 1.0f : float(pct) / 100.0f;
    if (st == SDL_POWERSTATE_CHARGING) batteryFrame = level >= 1.0f ? 0.35f : level < 0.3f ? 0.3f : level < 0.6f ? 0.25f : 0.2f;
    else if (st == SDL_POWERSTATE_CHARGED || st == SDL_POWERSTATE_NO_BATTERY) batteryFrame = 0.2f;
    else if (st == SDL_POWERSTATE_ON_BATTERY) batteryFrame = level < 0.3f ? 0.15f : level < 0.6f ? 0.1f : 0.05f;
    else batteryFrame = 0;
    if (uiClock - batteryCheckedAt >= 10.0f) { // (InterfaceMain::Update: each 10 s)
        batteryCheckedAt = uiClock;
        batteryLow = st == SDL_POWERSTATE_ON_BATTERY && level < 0.2f;
    }
}

void Viewer::drawBattery(const Mat4& view) {
    updateBattery();
    UiFig& b = uiFig("battery_id");
    b.fig.place = cr3::Mat34();
    poseUi(b, "battery_id", batteryFrame);
    drawFigure(b.fig, view.data());
}

void Viewer::drawBalloons(int w, int h) {
    if (!fieldMode || eventMode || !script || !script->boxVisible()) return;
    drawFlagMarks(w, h);
    if (balloons.empty()) {
        attentionShown = false; // (EndCheckArea)
        return;
    }
    if (!balloonLoaded) {
        balloonLoaded = true;
        std::string err;
        const float keep = time;
        if (!loadFigure("balloon.geom", balloonFigure, &err)) SDL_Log("balloon: %s", err.c_str());
        time = keep;
    }
    if (balloonFigure.meshes.empty()) return;
    const float k = std::min(float(w) / 0.96f, float(h) / 0.64f);
    const Mat4 proj = uiProjection(w, h);
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f;
    const Mat4 view = cr3::mat4Multiply(proj, camera);
    cr3::SceneLighting l;
    dev->setLighting(l);
    for (size_t i : balloons) {
        const cr3::Gimmick& g = field.gimmicks()[i];
        const float p[3] = {g.position[0], g.position[1] + g.balloonHeight(), g.position[2]};
        const float* m = fieldScene.data();
        const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
        if (cw <= 0) continue;
        const float nx = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw;
        const float ny = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw;
        balloonFigure.place = cr3::Mat34();
        balloonFigure.place.m[3] = nx * float(frameW) * 0.5f / k, balloonFigure.place.m[7] = ny * float(frameH) * 0.5f / k; // (the field's camera across the frame)
        pose(balloonFigure, time);
        drawFigure(balloonFigure, view.data());
    }
    // the attention mark (FldMain::SetAttentionMarkVisible: while any balloon is up -
    // InterfaceMain::SetCheckAreaParameter, CheckAreaExclamationMenu's `checkArea`, looping)
    if (!attentionShown) attentionShown = true, attentionAt = uiClock;
    UiFig& mark = uiFig("checkarea");
    mark.fig.place = cr3::Mat34();
    poseUi(mark, "checkarea", uiClock - attentionAt, true);
    drawFigure(mark.fig, view.data());
}

// the flag marks (FldFlagMark::Update: the `flag` figure at a flagged gimmick's balloon place,
// FldUtilGetBalloonPosOffset - hidden while it has a balloon: CheckEntryFlagName)
void Viewer::drawFlagMarks(int w, int h) {
    if (script->flagMarks().empty() || script->running()) return;
    const float k = std::min(float(w) / 0.96f, float(h) / 0.64f);
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h), camera);
    cr3::SceneLighting l;
    dev->setLighting(l);
    const std::vector<cr3::Gimmick>& gs = field.gimmicks();
    for (size_t i = 0; i < gs.size(); ++i) {
        const cr3::Gimmick& g = gs[i];
        if (!script->flagMarks().count(g.id) || !script->visible(g) || std::find(balloons.begin(), balloons.end(), i) != balloons.end()) continue;
        const float p[3] = {g.position[0], g.position[1] + g.balloonHeight(), g.position[2]};
        const float* m = fieldScene.data();
        const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
        if (cw <= 0) continue;
        UiFig& f = uiFig("flag");
        f.fig.place = cr3::Mat34();
        f.fig.place.m[3] = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw * float(frameW) * 0.5f / k;
        f.fig.place.m[7] = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw * float(frameH) * 0.5f / k;
        poseUi(f, "flag", time, true);
        drawFigure(f.fig, view.data());
    }
}

// the event's UI, over the resolved scene: the fade, then the windows over it (their depths
// in front of the fade windows')
// the dungeon's name as it is entered (EvtIntroduction: MapTitleMenu - the language's mapName
// figure on its mapNameDNN animator, played once)
void Viewer::drawIntro(int w, int h) {
    if (!fieldMode || !script || script->introDungeon() <= 0) return;
    static const char* const kFigures[8] = {"mapname", "mapname", "mapnamefre", "mapnameger", "mapnamespa", "mapnamechs", "mapnamecht", "mapnamekor"};
    const std::string figure = kFigures[std::clamp(optionLanguage, 0, 7)];
    char anim[32];
    std::snprintf(anim, sizeof anim, "%sd%02d", figure.c_str(), script->introDungeon());
    UiFig& u = uiFig(figure);
    u.fig.place = cr3::Mat34();
    poseUi(u, anim, float(script->ticks() - script->introTick()) * cr3::EventRuntime::kTick + fieldClock);
    if (u.fig.animLoaded) script->introSeconds = u.fig.anim.duration();
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f;
    cr3::SceneLighting l;
    dev->setLighting(l);
    drawFigure(u.fig, cr3::mat4Multiply(uiProjection(w, h), camera).data());
}

bool Viewer::cr3FieldIdle() {
    return areaMode && !eventMode && !battleMode && !cr3BattleMode && vs.screen == 0 && areaEvents && !areaEvents->running() && !areaEvents->geneLab().open &&
           !titleMenu.stage && shop.stage == 0 && world.stage == 0 && party.stage == 0 && !mapOpen && !saveStage && town.stage == 0;
}

void Viewer::drawCr3Field(int w, int h) {
    cr3CampTaps.clear();
    if (cr3Camp.stage == 0 && !cr3FieldIdle()) return;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    if (cr3Camp.stage == 0) return drawCr3FieldButtons(w, h, view);
    drawCr3Camp(w, h, view);
}

// (CampFieldMenu, as Fld2TabMenu::Open makes it with each area: closed - feBtnPanel at its
// closing's end, only feBtnOpen and feBtnEncount of the panel's showing. feBtnOpen opens it
// (InterfaceOpenAnimeSet, com_008) or closes it (com_009); open, the others take taps
// (TouchGestureTap2). Pose fades them with the panel: its feBtnPanel_in time t / 0.133
// opening, (0.3 - t) / 0.133 closing. feBtnTown twice: at feMapPositon's feMapMenu's
// call_feMapBtn, its frame 0 - the town map (tab 8: SetDirectTownMove), a town's while flag
// 0x3f1 is off (SetMapPermitFieldTabMenu), open or not -, and at call_feBtnTown its frame 1 -
// back to town (tab 10: CreateTownMoveDungeonWindow) -, with the panel, unless flag 1001)
bool Viewer::cr3InDungeon() const { return area.name.size() > 1 && (area.name[0] == 'd' || area.name[0] == 'e'); } // (GetDungeonNo 5 .. 22)

void Viewer::drawCr3FieldButtons(int w, int h, const Mat4& view) {
    float x, y, bx, by;
    const bool open = cr3Camp.panelOpen, dungeon = cr3InDungeon();
    // (Fld2TabMenu::Open: in a dungeon, not riding - flag 4105 -, the encounters' switch by the
    // tab's bit 0 and Battle by bit 1; in a town neither - its VS mode is My Page's)
    const bool tabs = dungeon && !game.flag(4105);
    const float age = std::max(uiClock - cr3Camp.panelAt, 0.0f);
    const float pt = open ? std::min(age, 0.1333f) : std::min(0.1333f + age, 0.3f);
    const float fade = std::clamp(pt < 0.1333f ? pt / 0.1333f : (0.3f - pt) / 0.1333f, 0.0f, 1.0f);
    const auto tapOf = [&](const UiFig& u, int id) {
        float ax, ay, ex, ey;
        if (cr3UiCall(u, "tapAreaCP00", ax, ay) && cr3UiCall(u, "tapAreaAP00", ex, ey)) cr3CampTaps.push_back(circleTap(ax, ay, ex, ey, id));
    };
    // (the Vita's cursor: the place it lights - the whole part, or its mat_fe_parts_01)
    static constexpr float kLit[3] = {0.5f, 1.0f, 1.5f}, kWhite[3] = {1, 1, 1};
    const auto setTint = [](float* to, const float* from) { to[0] = from[0], to[1] = from[1], to[2] = from[2]; };
    int barLit = 0;
    if (cr3BarCursorOn()) cr3BarSettle(-1), barLit = kCr3BarIds[cr3Camp.barCursor];
    UiFig& pos = cr3UiPut("febtnposition", "", 0, 0, 0, view);
    if (cr3UiCall(pos, "call_feBtnMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("febtnmenu", "", 0, x, y, view);
        if (cr3UiCall(menu, "call_feBtnPanel", x, y)) {
            UiFig& panel = cr3UiPut("febtnpanel", "febtnpanel_in", pt, x, y, view);
            struct Btn {
                const char* joint;
                const char* fig;
                int id;
                bool shown, fades;
            };
            const Btn btns[] = {{"call_feBtnMypage", "febtnmypage", 5, game.flag(27), true},
                                {"call_feBtnCamp", "febtncamp", 4, game.flag(27), true},
                                {"call_feBtnOpen", "febtnopen", 0, true, false},
                                {"call_feBtnSave", "febtnsave", 1, true, true},
                                {"call_feBtnEncount", "febtnencount", 2, tabs && (game.tabButtons & 1) != 0, false},
                                {"call_feBtnBattle", "febtnbattle", 3, tabs && (game.tabButtons & 2) != 0, true},
                                {"call_feBtnShop", "febtnshop", 6, game.flag(26), true}};
            for (const Btn& b : btns) {
                if (!b.shown || !cr3UiCall(panel, b.joint, bx, by)) continue;
                const bool lit = barLit == 100 + b.id;
                if (lit && b.id != 2) cr3UiTintMaterial = "mat_fe_parts_01", setTint(cr3UiMaterialTint, kLit);
                UiFig& u = cr3UiPut(b.fig, "", 0, bx, by, view, b.fades ? fade : 1);
                cr3UiTintMaterial.clear(), setTint(cr3UiMaterialTint, kWhite);
                float ix, iy;
                if (b.id == 0 && cr3UiCall(u, "call_feIconOpen_fr", ix, iy)) cr3UiPut("febtniconopen_fr00", "", open ? 1.0f / 30 : 0.0f, ix, iy, view);
                if (b.id == 2 && cr3UiCall(u, "call_feIconEncount_fr", ix, iy)) { // (frame 0 on, 1 off; the Vita's cursor lights the icon)
                    if (lit) setTint(cr3UiTint, kLit);
                    cr3UiPut("febtniconencount_fr00", "", encountersOff ? 1.0f / 30 : 0.0f, ix, iy, view, fade);
                    setTint(cr3UiTint, kWhite);
                }
                if (b.id == 0 || open) tapOf(u, 100 + b.id);
            }
        }
    }
    UiFig& mapPos = cr3UiPut("femappositon", "", 0, 0, 0, view);
    if (cr3UiCall(mapPos, "call_feMapMenu", x, y, w, h)) {
        UiFig& mapMenu = cr3UiPut("femapmenu", "", 0, x, y, view);
        if (!dungeon && !game.flag(0x3f1) && cr3UiCall(mapMenu, "call_feMapBtn", bx, by)) {
            if (barLit == 108) setTint(cr3UiTint, kLit);
            tapOf(cr3UiPut("febtntown", "", 0, bx, by, view), 108);
            setTint(cr3UiTint, kWhite);
        }
    }
    if (!game.flag(1001) && cr3UiCall(pos, "call_feBtnTown", x, y, w, h)) {
        if (barLit == 110) setTint(cr3UiTint, kLit);
        UiFig& u = cr3UiPut("febtntown", "", 1.0f / 30, x, y, view, fade);
        setTint(cr3UiTint, kWhite);
        if (open) tapOf(u, 110);
    }
    if (dungeon && game.minimapEnable) drawCr3Minimap(w, h, view);
    drawCr3Goods(w, h, view);
}

void Viewer::loadMinimap(const std::string& name, const std::vector<std::string>& joints, const std::vector<cr3::Mat34>& world) {
    minimapMarks.clear(), minimapLayers.clear(), minimapLayer = -1, minimapAt = uiClock;
    for (size_t j = 0; j < joints.size() && j < world.size(); ++j) {
        if (joints[j].compare(0, 16, "ragdoll_minimap_") != 0) continue;
        const cr3::Mat34& m = world[j];
        MinimapLayer l;
        l.name = joints[j].substr(16);
        for (int a = 0; a < 3; ++a) {
            const float half = std::sqrt(m.m[a] * m.m[a] + m.m[4 + a] * m.m[4 + a] + m.m[8 + a] * m.m[8 + a]) * 0.1f * 0.5f; // (its scale x 0.1 x 0.5 each way)
            // GetMinimapLayer expands each box to include [-1, 1] on every axis.
            l.lo[a] = std::min(-1.0f, m.m[a * 4 + 3] - half);
            l.hi[a] = std::max(1.0f, m.m[a * 4 + 3] + half);
        }
        minimapLayers.push_back(l);
    }
    std::vector<uint8_t> d;
    cr3::MbeFile f;
    if (!archive.read("data\\field_" + name.substr(0, 3) + ".mbe", d) || !f.parse(d)) return;
    const cr3::MbeFile::Table* t = f.table("Field");
    if (!t) return;
    const int areaNo = std::atoi(name.substr(3, 2).c_str()) - 1;
    auto questActive = [&](int q) { return q >= 0 && game.cr3Quests.begun.count(q) && !game.cr3Quests.ended.count(q); };
    std::vector<cr3::MbeFile::Row> rows;
    std::set<std::string> seenGimmicks;
    for (int pass = 0; pass < 2; ++pass)
        for (size_t r = 0; r < t->rows.size(); ++r) {
            cr3::MbeFile::Row row;
            if (!f.row(*t, r, "iiiiiippiiif", row) || row.i(0) != areaNo) continue;
            const int q = row.i(4);
            if (pass == 0 ? !questActive(q) : q != -1) continue;
            if (seenGimmicks.insert(row.s(6)).second) rows.push_back(row);
        }
    for (const cr3::MbeFile::Row& row : rows) {
        if (row.i(8) != 1 && row.i(8) != 2) continue;
        const std::string gimmick = row.s(6);
        if ((gimmick.compare(0, 12, "ragdoll_tbox") == 0 || gimmick.compare(0, 12, "ragdoll_gbox") == 0) && drawnBox(gimmick) < 0) continue;
        auto it = std::find(joints.begin(), joints.end(), gimmick);
        if (it == joints.end() && gimmick.size() > 8) it = std::find(joints.begin(), joints.end(), gimmick.substr(8));
        if (it == joints.end() || size_t(it - joints.begin()) >= world.size()) continue;
        const cr3::Mat34& m = world[size_t(it - joints.begin())];
        MinimapMark mk;
        mk.kind = row.i(8), mk.x = m.m[3], mk.z = m.m[11];
        mk.layer = minimapLayerAt({m.m[3], m.m[7], m.m[11]});
        mk.name = gimmick.compare(0, 8, "ragdoll_") == 0 ? gimmick.substr(8) : gimmick;
        minimapMarks.push_back(mk);
    }
    SDL_Log("area %s: minimap %zu marks, %zu layers", name.c_str(), minimapMarks.size(), minimapLayers.size());
}

int Viewer::minimapLayerAt(const cr3::Vec3& p) const { // (Fld2TaskGimmick::GetMinimapLayer)
    for (size_t i = 0; i < minimapLayers.size(); ++i) {
        const MinimapLayer& l = minimapLayers[i];
        if (p[0] >= l.lo[0] && p[0] <= l.hi[0] && p[1] >= l.lo[1] && p[1] <= l.hi[1] && p[2] >= l.lo[2] && p[2] <= l.hi[2]) return int(i);
    }
    return -1;
}

auto Viewer::cr3UiPutTurned(const std::string& name, float x, float y, float turn, const Mat4& view) -> UiFig& {
    UiFig& u = cr3UiPose(name, "", 0, x, y, false);
    const float c = std::cos(turn), s = std::sin(turn);
    u.fig.place.m[0] = c * cr3UiScale, u.fig.place.m[1] = -s * cr3UiScale, u.fig.place.m[4] = s * cr3UiScale, u.fig.place.m[5] = c * cr3UiScale;
    u.fig.tint[0] = u.fig.tint[1] = u.fig.tint[2] = u.fig.tint[3] = 1;
    drawFigure(u.fig, view.data());
    return u;
}

void Viewer::drawCr3Minimap(int w, int h, const Mat4& view) {
    if (!areaWalker) return;
    cr3::Vec3 p = areaWalker->position();
    p[1] -= areaWalker->radius; // GetMinimapLayer takes the model's position, at its feet.
    if (const int l = minimapLayerAt(p); l >= 0) minimapLayer = l; // (UpdateMinimap: kept where none says)
    float x, y, cx, cy;
    UiFig& pos = cr3UiPut("femappositon", "", std::min(uiClock - minimapAt, 5.0f), 0, 0, view);
    if (!cr3UiCall(pos, "call_feMapMenu", cx, cy, w, h)) return;
    UiFig& menu = cr3UiPut("femapmenu", "", 0, cx, cy, view);
    const bool hasLayerTex = minimapLayer >= 0 && minimapLayer < int(minimapLayers.size());
    const bool skipBaseMap = !hasLayerTex && (area.name == "d0904" || area.name == "d0908" || area.name == "d0912"); // (ChangeMapTexture @ 0x00ad838c)
    if (!skipBaseMap && cr3UiCall(menu, "call_feMap_id", x, y)) {
        UiFig& map = cr3UiPose("femap_id00", "", 0, x, y, false);
        const std::string tex = (hasLayerTex ? minimapLayers[size_t(minimapLayer)].name : area.name) + "m_map";
        if (cr3::Texture* t = texture(tex))
            for (ViewMesh& vm : map.fig.meshes)
                if (vm.source.material == 0) vm.texture = t;
        map.fig.tint[0] = map.fig.tint[1] = map.fig.tint[2] = map.fig.tint[3] = 1;
        drawFigure(map.fig, view.data());
    }
    auto markVisible = [&](const MinimapMark& mk) { // (MiniMapMenu::SetGimickPointDataView @ 0x00ad8654)
        if (!mk.name.empty() && !game.gimmickVisible(mk.name)) return false;
        if (const auto fb = game.fieldBoxes.find(mk.name); fb != game.fieldBoxes.end() && fb->second >= 1) return false;
        for (const AreaNpc& npc : areaNpcs)
            if (npc.name == mk.name && (npc.broken || npc.openAt >= 0)) return false;
        return true;
    };
    for (int kind : {1, 2}) // (the warps, then the objects)
        for (const MinimapMark& mk : minimapMarks)
            if (mk.kind == kind && (minimapLayers.empty() || mk.layer == minimapLayer) && markVisible(mk))
                cr3UiPut(kind == 1 ? "femapwarp" : "femapobject", "", 0, cx + mk.x / 30, cy - mk.z / 30, view);
    UiFig& panel = cr3UiPose("femapplayerpanel", "", 0, cx + p[0] / 30, cy - p[2] / 30, false);
    float px, py;
    if (cr3UiCall(panel, "call_feMapCamera", px, py)) {
        const float toPlayer = std::atan2(p[0] - areaCamera.eye[0], p[2] - areaCamera.eye[2]);
        cr3UiPutTurned("femapcamera", px, py, toPlayer, view);
    }
    panel.fig.tint[0] = panel.fig.tint[1] = panel.fig.tint[2] = panel.fig.tint[3] = 1;
    drawFigure(panel.fig, view.data());
    if (cr3UiCall(panel, "call_feMapPlayer", px, py)) cr3UiPutTurned("femapplayer", px, py, std::atan2(-std::sin(areaYaw), -std::cos(areaYaw)), view);
}

int Viewer::focusedGoods() {
    if (game.goodsButtons.empty()) return goodsFocus = -1;
    if (!game.goodsButtons.count(goodsFocus)) goodsFocus = *game.goodsButtons.begin();
    return goodsFocus;
}

bool Viewer::goodsPad(bool cycle) {
    if (!vitaButtons() || !areaMode || !cr3FieldIdle() || cr3Camp.stage != 0) return false;
    const int goods = focusedGoods();
    if (goods < 0) return false;
    if (cycle) {
        auto it = game.goodsButtons.find(goods);
        if (++it == game.goodsButtons.end()) it = game.goodsButtons.begin();
        goodsFocus = *it;
        SDL_Log("field: goods cursor %d", goodsFocus);
    } else {
        SDL_Log("field: goods uses %d", goods);
        useGoods(goods);
    }
    return true;
}

void Viewer::drawCr3Goods(int w, int h, const Mat4& view) {
    if (game.goodsButtons.empty()) return;
    float x, y, px, py, ix, iy;
    UiFig& pos = cr3UiPut("fegoodsposition", "", 0, 0, 0, view);
    if (!cr3UiCall(pos, "call_feGoodsMenu", x, y, w, h)) return;
    UiFig& menu = cr3UiPut("fegoodsmenu", "", 0, x, y, view);
    const int goods = focusedGoods();
    if (!cr3UiCall(menu, "call_feGoodsPanel_03", px, py)) return; // (the arc's middle - _01 .. _05 down from the corner)
    UiFig& panel = cr3UiPut("fegoodspanel", "fegoodspanel_in", 1.0f, px, py, view);
    if (cr3UiCall(panel, "call_feGoodsIcon_fr00", ix, iy)) cr3UiPut("fegoodsicon_fr00", "", float(goods) / 30, ix, iy, view);
    float ax, ay, ex, ey;
    if (cr3UiCall(panel, "tapAreaCP00", ax, ay) && cr3UiCall(panel, "tapAreaAP00", ex, ey)) cr3CampTaps.push_back(circleTap(ax, ay, ex, ey, 120 + goods));
    else cr3CampTaps.push_back({px - 0.5f, py - 0.5f, px + 0.5f, py + 0.5f, 120 + goods});
}

void Viewer::useGoods(int goods) {
    if (!game.goodsButtons.count(goods)) return;
    if (goods == 0) putBomb(); // (Fld2BombManager::PutBomb)
    else SDL_Log("field: goods %d (not in yet)", goods);
}

bool Viewer::vitaButtons() const { return archive.isOpen(); }

bool Viewer::cr3BarShown(int id) const { // (drawCr3FieldButtons' conditions)
    const bool dungeon = cr3InDungeon(), tabs = dungeon && !game.flag(4105);
    switch (id) {
    case 110: return !game.flag(1001);
    case 108: return !dungeon && !game.flag(0x3f1);
    case 103: return tabs && (game.tabButtons & 2) != 0;
    case 102: return tabs && (game.tabButtons & 1) != 0;
    case 101: return true;
    case 106: return game.flag(26);
    case 104: case 105: return game.flag(27);
    default: return false;
    }
}

bool Viewer::cr3BarCursorOn() { return vitaButtons() && cr3Camp.stage == 0 && cr3Camp.panelOpen && cr3FieldIdle(); }

// the cursor on a shown place (from where it is: on the way it went - past either end, Camp's)
void Viewer::cr3BarSettle(int way) {
    int at = cr3Camp.barCursor;
    for (int n = 0; n < 9; ++n) {
        if (at < 0 || at > 7) at = 5;
        if (cr3BarShown(kCr3BarIds[at])) break;
        at += way > 0 ? 1 : -1;
    }
    cr3Camp.barCursor = at;
}

bool Viewer::cr3BarPad(PadPress p) {
    if (!vitaButtons() || cr3Camp.stage != 0 || !cr3FieldIdle()) return false;
    if (p == PadPress::Triangle || (p == PadPress::Cancel && cr3Camp.panelOpen)) {
        chooseCr3Camp(100);
        return true;
    }
    if (!cr3Camp.panelOpen) return false;
    const int was = cr3Camp.barCursor;
    if (p == PadPress::Up || p == PadPress::Left) {
        cr3Camp.barCursor = was <= 1 ? 7 : was - 1;
        cr3BarSettle(-1);
    } else if (p == PadPress::Down || p == PadPress::Right) {
        cr3Camp.barCursor = was != 7 ? was + 1 : cr3BarShown(kCr3BarIds[1]) ? 1 : cr3BarShown(kCr3BarIds[0]) ? 0 : 7;
        cr3BarSettle(1);
    } else if (p == PadPress::Confirm) {
        cr3BarSettle(-1);
        SDL_Log("field: the cursor presses %d", kCr3BarIds[cr3Camp.barCursor]);
        chooseCr3Camp(kCr3BarIds[cr3Camp.barCursor]);
        return true;
    } else return false;
    if (cr3Camp.barCursor != was) audio.playSe("com_002", false), SDL_Log("field: the cursor on %d", kCr3BarIds[cr3Camp.barCursor]);
    return true;
}

// ---- The Vita's L / R (masks 0x200 / 0x400) over CR3's menus, each as the port's own keys do the
// same there: the gene lab's tabs (GeneSynthesisFutter FUN_811690b6: L the one before, R the
// one after), the shop's Buy / Sell (ShopMenuMain::Update 0x812432aa, either), the gifts'
// categories (GiftSynthesizeListMenu::Update 0x81189cf2), the quests' categories and a quest's
// neighbours (comListMenu::Update 0x81137862, QuestDitailMenu::Update 0x81246a8e), the
// Options' tabs (OptionMenu FUN_81203e54: not round, com_003), the Gene Index's pages and
// profiles (PictureBookMenu FUN_81211910), the Lucky Box's boxes (LuckyBoxMenu::Update
// 0x811ea496). With the Vita copy as the game
bool Viewer::cr3Shoulder(bool right) {
    if (!vitaButtons()) return false;
    if (geneInspectOpen) return true;
    if (tutorialShoulder(right) || battleShoulder(right) || vsShoulder(right) || campGeneShoulder(right)) return true;
    const SDL_Keycode lr = right ? SDLK_RIGHT : SDLK_LEFT;
    cr3::EventRuntime* ev = activeEvents();
    if (ev && ev->geneLab().open && !ev->geneLab().talking && geneMenu.alert.empty() && (geneMenu.screen == 3 || geneMenu.screen == 7))
        return chooseGeneLab(*ev, kTapTab + (geneMenu.tab + (right ? 1 : 3)) % 4), true;
    if (ev && ev->shop().open && !ev->shop().talking && shopMenu.told.empty() && (shopMenu.screen == 1 || shopMenu.screen == 2))
        return cr3ShopKey(SDLK_TAB), true;
    if (vs.screen == 11 && !vs.boxCheckFocus && !vs.ask && vs.told.empty()) return chooseVs(right ? 1102 : 1100), true;
    if (cr3Camp.stage == 6 && campGifts.step == 0 && campGifts.told.empty()) return campGiftsKey(lr), true;
    if (cr3Camp.stage == 7) return campQuestsKey(lr), true;
    if (cr3Camp.stage == 9 && !saveImport.active() && !campOptions.asking) {
        const int to = std::clamp(campOptions.tab + (right ? 1 : -1), 0, kOptionTabs - 1);
        if (to != campOptions.tab) campOptions.tab = to, campOptions.footer = false, audio.playSe("com_003", false), SDL_Log("options: tab %d", to);
        return true;
    }
    if (cr3Camp.stage == 10 && myPage.screen == 3 && !cardView.gene) return myPageKey(lr), true;
    if (areaMode && cr3Camp.stage == 0 && cr3FieldIdle()) {
        if (right) return goodsPad(true);
        if (areaCamFree()) {
            areaCamera.behind(areaYaw);
            SDL_Log("camera: behind the player");
            return true;
        }
    }
    return false;
}

void Viewer::drawAreaDrag(int w, int h) {
    drawAreaCam(w, h);
    if (!areaDrag.on || !areaDragFree() || (!areaDrag.moved && areaDrag.held < 0.5f)) return;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    dev->setLighting(cr3::SceneLighting());
    dev->clearDepth();
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const auto ui = [&](float fx, float fy, float& ux, float& uy) { ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k; };
    float ax, ay, bx, by;
    ui(areaDrag.x0, areaDrag.y0, ax, ay);
    ui(areaDrag.x, areaDrag.y, bx, by);
    float dx = bx - ax, dy = by - ay;
    const float len = std::hypot(dx, dy), most = 1.28f; // (128 of 960: 1.28 units)
    if (len > most) dx *= most / len, dy *= most / len;
    cr3UiScale = 2;
    cr3UiPut("feconmove01", "", areaDrag.held, ax, ay, view, 1, true);
    cr3UiPut("feconmove02", "", areaDrag.held, ax + dx * 0.5f, ay + dy * 0.5f, view, 1, true);
    cr3UiPut("feconmove03", "", areaDrag.held, ax + dx, ay + dy, view, 1, true);
    cr3UiScale = 1;
}

// the camera's pad (Fld2Controller(1): feConCamera where it pressed, feConMove03 at the finger -
// 128 of the screen's 960 at most)
void Viewer::drawAreaCam(int w, int h) {
    if (!areaCam.on || !areaCamFree() || (!areaCam.moved && areaCam.held < 0.5f)) return;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    dev->setLighting(cr3::SceneLighting());
    dev->clearDepth();
    float ax, ay, bx, by;
    uiUnits(areaCam.x0, areaCam.y0, ax, ay);
    uiUnits(areaCam.x, areaCam.y, bx, by);
    float dx = bx - ax, dy = by - ay;
    const float len = std::hypot(dx, dy), most = 1.28f;
    if (len > most) dx *= most / len, dy *= most / len;
    cr3UiScale = 2;
    cr3UiPut("feconcamera", "", areaCam.held, ax, ay, view, 1, true);
    cr3UiPut("feconmove03", "", areaCam.held, ax + dx, ay + dy, view, 1, true);
    cr3UiScale = 1;
}

} // namespace cr3_viewer
