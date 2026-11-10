#include "viewer/viewer.hpp"

namespace cr3_viewer {

auto Viewer::worldRows() -> std::vector<ListRow> {
    static const int kFlags[13] = {1041, 1049, 1046, 1047, 1048, 1044, 1043, 1045, 1042, 1050, 1051, 1051, 1052};
    static const int kDungeons[13] = {1, 9, 6, 7, 8, 4, 3, 5, 2, 10, 11, 12, 13};
    static const int kLevelFlags[11] = {0, 1081, 1082, 1083, 1084, 1085, 1086, 1086, 1087, 1087, 1088};
    std::vector<ListRow> rows;
    if (world.chapters) {
        static const int kChapterFlags[4] = {11, 18, 19, 20}, kChapters[4] = {1, 5, 7, 8};
        for (int i = 0; i < 4; ++i)
            if (game.flag(kChapterFlags[i])) {
                ListRow r;
                r.id = kChapters[i], r.name = menuName(14 + i), r.number = -1;
                rows.push_back(r);
            }
    } else if (world.stage == 1) {
        for (int i = 0; i < 13; ++i)
            if (game.flag(kFlags[i])) {
                ListRow r;
                r.id = kDungeons[i], r.name = menuName(kDungeons[i]), r.number = -1;
                rows.push_back(r);
            }
    } else {
        for (int k = 1; k <= 11; ++k)
            if (kLevelFlags[k - 1] == 0 || game.flag(kLevelFlags[k - 1])) {
                ListRow r;
                r.id = k, r.name = menuName(0x14 + k), r.number = -1;
                rows.push_back(r);
            }
    }
    return rows;
}

void Viewer::openWorldSelect() {
    world = WorldSelect();
    world.stage = 1, world.at = uiClock;
    SDL_Log("world select: %zu dungeons", worldRows().size());
    if (worldRows().empty()) world.stage = 0, pendingBranch = true;
}

void Viewer::openChapterSelect() {
    world = WorldSelect();
    world.chapters = true, world.stage = 1, world.at = uiClock;
    SDL_Log("chapter select: %zu chapters", worldRows().size());
    if (worldRows().empty()) world.stage = 0, pendingBranch = true;
}

// MapSelectMenu::CheckCoupling: a dungeon whose flag (0x534578, by its number) is set, and of
// its rows (0x5345d8: the dungeon, a flag, the two characters from 0) one whose flag is set,
// wants that pair as the party (cr3_common_data +3, +4); the pair it wants, or none
bool Viewer::worldPair(const cr3::GameState& g, int dungeon, int pair[2]) {
    static const int kFlag[10] = {0, 1079, 1076, 1077, 1078, 1074, 1073, 1075, 1072, 1080};
    static const int kRows[13][4] = {{4, 1061, 0, 2}, {3, 1062, 0, 5}, {5, 1063, 0, 3}, {6, 1066, 0, 5}, {7, 1067, 0, 3},
                                     {4, 1064, 0, 4}, {2, 1065, 2, 3}, {6, 1066, 0, 5}, {7, 1067, 0, 3}, {8, 1068, 0, 4},
                                     {6, 1066, 0, 5}, {7, 1067, 0, 3}, {8, 1068, 0, 4}};
    if (dungeon < 1 || dungeon > 9 || !g.flag(kFlag[dungeon])) return false;
    for (const auto& r : kRows)
        if (r[0] == dungeon && g.flag(r[1]) && (g.party[0] != r[2] || g.party[1] != r[3])) {
            pair[0] = r[2], pair[1] = r[3];
            return true;
        }
    return false;
}

void Viewer::worldPick(const ListRow& r) {
    audio.playSe(0xb4, false);
    int pair[2];
    if (world.stage == 1 && world.chapters) world.dungeon = r.id, world.dungeonFocus = world.focus, world.stage = 3;
    else if (world.stage == 1 && worldPair(game, r.id, pair)) world.dungeon = r.id, world.dungeonFocus = world.focus, world.stage = 4; // (the System window: announce 0x81, yes / no)
    else if (world.stage == 1) world.dungeon = r.id, world.dungeonFocus = world.focus, world.stage = 2, world.focus = 0;
    else if (world.stage == 2) world.level = r.id, world.stage = 3;
    world.at = uiClock;
}

void Viewer::worldBack() {
    audio.playSe(0xb5, false);
    if (world.stage == 2 || (world.stage == 3 && world.chapters)) world.stage = 1, world.focus = world.dungeonFocus, world.at = uiClock;
    else if (world.stage == 3) world.stage = 2, world.at = uiClock;
    else world.stage = 0, pendingBranch = true; // (back to the field: what else was reserved)
}

void Viewer::worldAnswer(bool yes) {
    if (world.stage == 4) { // (yes: ChangeCoupling - the pair the party, cr3_common_data +1..+4 - then the levels; no: the dungeons again)
        audio.playSe(yes ? 0xb4 : 0xb5, false);
        int pair[2];
        if (yes && worldPair(game, world.dungeon, pair)) {
            game.party[0] = pair[0], game.party[1] = pair[1];
            SDL_Log("world select: the party %d and %d", pair[0], pair[1]);
        }
        if (yes) world.stage = 2, world.focus = 0;
        else world.stage = 1, world.focus = world.dungeonFocus;
        world.at = uiClock;
        return;
    }
    if (world.stage != 3) return;
    if (!yes) return worldBack();
    audio.playSe(0xb4, false);
    if (world.chapters) { // (FldMain step 0xe: CallEvent("chapter_select"))
        game.selectChapter = world.dungeon;
        SDL_Log("chapter select: chapter %d", world.dungeon);
        world.stage = 0;
        return callGimmick("chapter_select");
    }
    game.selectDungeon = world.dungeon, game.difficulty = world.level - 1;
    SDL_Log("world select: dungeon %d, level %d", world.dungeon, world.level);
    world.stage = 0;
    callGimmick("WarpTask"); // (FldMain step 10: CallEvent("WarpTask"))
}

void Viewer::worldTap(int id) {
    if (world.stage >= 3) {
        if (id == 7601 || id == 7602) worldAnswer(id == 7601);
        return;
    }
    const std::vector<ListRow> rows = worldRows();
    if (id >= 1000 && id < 1000 + int(rows.size())) world.focus = id - 1000;
    else
        for (const ListRow& r : rows)
            if (r.id == id) return worldPick(r);
}

void Viewer::worldKey(SDL_Keycode k) {
    if (world.stage >= 3) {
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) worldAnswer(true);
        else if (k == SDLK_BACKSPACE || k == SDLK_N) worldAnswer(false);
        return;
    }
    const std::vector<ListRow> rows = worldRows();
    const int n = std::max(int(rows.size()), 1);
    if (k == SDLK_UP) world.focus = (world.focus + n - 1) % n;
    else if (k == SDLK_DOWN) world.focus = (world.focus + 1) % n;
    else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && world.focus < int(rows.size())) worldPick(rows[size_t(world.focus)]);
    else if (k == SDLK_BACKSPACE) worldBack();
}

void Viewer::drawWorldSelect(const Mat4& view, int w, int h) {
    const float t = uiClock - world.at;
    UiFig& title = uiFig("menu"); // (MapSelectTitleMenu)
    title.fig.place = cr3::Mat34();
    poseUi(title, world.chapters ? "menucpt_io" : "menuwld_io", uiClock - world.at + (world.stage > 1 ? 10.0f : 0.0f));
    drawFigure(title.fig, view.data());
    if (world.stage == 3 && world.chapters) {
        const std::string q = menuName(14 + std::clamp(world.dungeonFocus, 0, 3)) + "\\n" + announce(0x8b);
        return drawAsk(q, t, view, w, h, 7601, 7602);
    }
    if (world.stage == 4) return drawAsk(announce(0x81), t, view, w, h, 7601, 7602);
    if (world.stage == 3) {
        const std::string q = menuName(world.dungeon) + "\\n" + menuName(0x14 + world.level) + "\\n" + announce(0x80);
        return drawAsk(q, t, view, w, h, 7601, 7602);
    }
    const std::vector<ListRow> rows = worldRows();
    if (rows.empty()) return;
    world.focus = std::clamp(world.focus, 0, int(rows.size()) - 1);
    const int shown = world.chapters ? world.focus + 1 : world.stage == 1 ? rows[size_t(world.focus)].id : world.dungeon;
    UiFig& pic = uiFig("mappic"); // (MapSelectPicMenu: the dungeon's picture; ChapterSelectPicMenu the chapter's)
    char tex[24];
    std::snprintf(tex, sizeof tex, world.chapters ? "mapPic_c%02d" : "mapPic_d%02d", shown);
    if (cr3::Texture* tx = texture(tex))
        for (ViewMesh& vm : pic.fig.meshes) vm.texture = tx;
    pic.fig.place = cr3::Mat34();
    poseUi(pic, "mappic_in", t);
    drawFigure(pic.fig, view.data());
    drawSkillList(rows, rows[size_t(world.focus)].id, t, view, w, h, -1, -1);
}

std::vector<int> Viewer::pubPosters() const {
    std::vector<int> v;
    for (int i = 1; i <= 50 && v.size() < 9; ++i)
        if (game.flag(1200 + i)) v.push_back(i);
    return v;
}

int Viewer::pubListCount() const { return !game.flag(201) ? 28 : !game.flag(202) ? 42 : 50; } // (PubListMenu::Initialize)

// the list's sign of a request (PosterSId::SetSign - hud_posterS's quarters): 1 posted, 2 taken,
// 3 done (flag 1300 + id), 4 not yet
int Viewer::pubSign(int id) const {
    int sign = game.flag(1200 + id) ? 1 : 4;
    if (game.globalWork[7] == id) sign = 2;
    if (game.flag(1300 + id)) sign = 3;
    return sign;
}

void Viewer::openPub() {
    pub = Pub();
    pub.stage = 1, pub.at = uiClock;
    SDL_Log("pub: %zu requests posted", pubPosters().size());
}

void Viewer::pubEnd(int result) { // (PubMainMenu: ResultPubParameter - FldMain's CallEvent / CallAtMenuFinished)
    pub.stage = 0;
    SDL_Log("pub: %d", result);
    if (result == 1) callGimmick("pub_checkA");
    else if (result == 2) callGimmick("pub_checkG");
    else pendingBranch = true;
}

void Viewer::pubTap(int id) {
    const std::vector<int> posters = pubPosters();
    if (pub.stage == 1) {
        if (id == 100) return audio.playSe(0xb5, false), pubEnd(3);
        if (id == 35) return audio.playSe(0xb4, false), void((pub.stage = 4, pub.at = uiClock, pub.listFocus = 0)); // (Check: PubTopMenu 200)
        if (id >= 1000 && id < 1000 + int(posters.size())) {
            const int k = id - 1000;
            if (k != pub.focus) return audio.playSe(0xb3, false), void(pub.focus = k);
            audio.playSe(0xb4, false);
            pub.request = posters[size_t(k)];
            game.setFlag(1250 + pub.request, false);
            pub.stage = 2, pub.at = uiClock;
            SDL_Log("pub: request %d", pub.request);
        }
    } else if (pub.stage == 2) {
        if (id == 100) return audio.playSe(0xb5, false), void((pub.stage = 1, pub.at = uiClock));
        if (id == 38 || id == 39) {
            audio.playSe(0xb4, false);
            if (game.globalWork[7] == pub.request) pub.stage = 3, pub.at = uiClock; // (taken already: give it up?)
            else game.globalWork[7] = pub.request, pubEnd(1);
        }
    } else if (pub.stage == 4) { // (PubListMenu: a tap shows a request's title; Back the board)
        if (id == 100) audio.playSe(0xb5, false), pub.stage = 1, pub.at = uiClock;
        else if (id >= 2000 && id <= 2050) audio.playSe(0xb4, false), pub.listFocus = id - 2000;
    } else if (pub.stage == 3) {
        if (id == 7601) audio.playSe(0xb4, false), game.globalWork[7] = 0, pubEnd(2);
        else if (id == 7602) audio.playSe(0xb5, false), pub.stage = 2, pub.at = uiClock;
    }
}

void Viewer::pubKey(SDL_Keycode k) {
    const int n = std::max(int(pubPosters().size()), 1);
    if (pub.stage == 4 && (k == SDLK_LEFT || k == SDLK_RIGHT)) {
        const int count = pubListCount();
        pubTap(2000 + (pub.listFocus + (k == SDLK_LEFT ? count - 2 : 0)) % count + 1);
    } else if (pub.stage == 1 && (k == SDLK_LEFT || k == SDLK_RIGHT)) pubTap(1000 + (pub.focus + (k == SDLK_LEFT ? n - 1 : 1)) % n);
    else if (pub.stage == 1 && (k == SDLK_RETURN || k == SDLK_KP_ENTER)) pubTap(1000 + pub.focus);
    else if (pub.stage == 2 && (k == SDLK_RETURN || k == SDLK_KP_ENTER)) pubTap(game.globalWork[7] == pub.request ? 39 : 38);
    else if (pub.stage == 3 && (k == SDLK_Y || k == SDLK_RETURN)) pubTap(7601);
    else if (pub.stage == 3 && k == SDLK_N) pubTap(7602);
    else if (k == SDLK_BACKSPACE) pub.stage == 3 ? pubTap(7602) : pubTap(100);
}

void Viewer::drawPub(const Mat4& view, int w, int h) {
    const float t = uiClock - pub.at;
    auto part = [&](const char* fig, const char* anim, float x = 0, float y = 0, float tt = -1) -> UiFig& {
        UiFig& u = uiFig(fig);
        u.fig.place = cr3::Mat34();
        u.fig.place.m[3] = x, u.fig.place.m[7] = y;
        poseUi(u, anim, tt >= 0 ? tt : t);
        drawFigure(u.fig, view.data());
        return u;
    };
    part("pubgirls_io", "pubgirls_io");
    part("menu", "menupub_io");
    float x, y, sx, sy, ex, ey;
    auto back = [&]() {
        UiFig& bk = part("btlback", "btlback_in");
        if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
    };
    auto help = [&](int id) {
        UiFig& hp = part("camphelp_io", "camphelp_io");
        if (uiJoint(hp, "typeM_L01", x, y)) drawUiText(messageText("pubtxt", id), 24, x, y, w, h, true);
    };
    const std::vector<int> posters = pubPosters();
    if (pub.stage == 1) {
        UiFig& board = part("posterbtn", "posterbtn");
        pub.focus = std::clamp(pub.focus, 0, std::max(int(posters.size()) - 1, 0));
        for (size_t k = 0; k < posters.size(); ++k) {
            char jn[32];
            std::snprintf(jn, sizeof jn, "call_pubPoster_io%02zu", k + 1);
            if (!uiJoint(board, jn, x, y)) continue;
            UiFig& p = uiFig("pubposter_io");
            p.fig.place = cr3::Mat34();
            p.fig.place.m[3] = x, p.fig.place.m[7] = y;
            poseUi(p, "pubposter_io", t);
            for (size_t mi = 0; mi < p.fig.meshes.size(); ++mi) { // (SetCursorPosition: the focused one's MTR_posterL at 1.5)
                ViewMesh& vm = p.fig.meshes[mi];
                const uint32_t m = vm.source.material;
                const bool lit = int(k) == pub.focus && m < p.materials.size() && p.materials[m] == "MTR_posterL";
                vm.material.params[0x33] = lit ? std::vector<float>{1.5f, 1.5f, 1.5f, 1.0f} : vm.baseMaterial.params[0x33];
                if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
            }
            drawFigure(p.fig, view.data());
            if (uiJoint(p, "tapAreaS01", sx, sy) && uiJoint(p, "tapAreaE01", ex, ey)) tapRects.push_back({1000 + int(k), sx, sy, ex, ey});
            const int id = posters[k];
            float mx, my;
            if (id == game.globalWork[7] && uiJoint(p, "call_checkMark", mx, my)) part("checkmark_io", "checkmark_io", mx, my);
            if (game.flag(1250 + id) && uiJoint(p, "call_seal_id", mx, my)) part("seal_id", "seal_id", mx, my, 0);
            if (int(k) == pub.focus && uiJoint(p, "call_faceSlct", mx, my)) part("faceslct", "faceslct", mx, my);
        }
        if (!posters.empty()) help(posters[size_t(pub.focus)]);
        drawCommandMenu("checkbtn_io", t, view, {}, {}, 0, 0, 0, "checkbtn_io"); // (the list: circle 35 - PubTopMenu's 200)
        back();
    } else if (pub.stage == 4) { // (PubListMenu: posterIcon, each request's sign at call_posterS_idNN, the share done)
        UiFig& icons = part("postericon", "postericon");
        cr3::Texture* tex = texture("hud_posters");
        const Mat4 proj = uiProjection(w, h);
        static const float kUv[5][4] = {{0, 0, 0, 0}, {0, 0, 0.5f, 0.5f}, {0.5f, 0, 1, 0.5f}, {0, 0.5f, 0.5f, 1}, {0.5f, 0.5f, 1, 1}};
        for (int id = 1; id <= pubListCount(); ++id) {
            char jn[32];
            std::snprintf(jn, sizeof jn, "call_posterS_id%02d", id);
            if (!uiJoint(icons, jn, x, y) || !tex) continue;
            const float* uv = kUv[pubSign(id)];
            const float h2 = 0.032f; // (a 0.064 square)
            const float v[24] = {x - h2, y - h2, uv[0], uv[3], x + h2, y - h2, uv[2], uv[3], x + h2, y + h2, uv[2], uv[1],
                                 x - h2, y - h2, uv[0], uv[3], x + h2, y + h2, uv[2], uv[1], x - h2, y + h2, uv[0], uv[1]};
            const float k = id == pub.listFocus ? 2.0f : 1.0f; // (the focused one's diffuse 2)
            const float rgba[4] = {k, k, k, 1};
            dev->drawQuad(*tex, v, proj.data(), rgba);
            tapRects.push_back({2000 + id, x - h2, y - h2, x + h2, y + h2});
            if (id == pub.listFocus) {
                UiFig& fs = uiFig("faceslct");
                fs.fig.place = cr3::Mat34();
                fs.fig.place.m[0] = fs.fig.place.m[5] = fs.fig.place.m[10] = 0.5f; // (scaled half)
                fs.fig.place.m[3] = x, fs.fig.place.m[7] = y;
                poseUi(fs, "faceslct", t);
                drawFigure(fs.fig, view.data());
            }
        }
        int done = 0;
        for (int f = 1301; f <= 1350; ++f) done += game.flag(f);
        UiFig& pc = part("percent_io", "percent_io");
        if (uiJoint(pc, "call_dotNum_id1", x, y)) drawDigits(done * 100 / 50, 80, 3, x, y, proj); // (GetPercentInteger(done, 50))
        if (pub.listFocus > 0) help(pub.listFocus);
        back();
    } else if (pub.stage == 3) {
        drawAsk(announce(0x83), t, view, w, h, 7601, 7602); // (PubMainMenu 9 / 10: the request shut, then give it up?)
    } else {
        UiFig& win = part("pubwin_io", "pubwin_io");
        const std::string text = messageText("pubtxt", pub.request + 60);
        size_t from = 0;
        for (int line = 1; line <= 8 && from <= text.size(); ++line) {
            const size_t at = text.find("\\n", from);
            if (uiJoint(win, "typeS_L0" + std::to_string(line), x, y))
                drawUiText(text.substr(from, at == std::string::npos ? std::string::npos : at - from), 22, x, y, w, h, true);
            if (at == std::string::npos) break;
            from = at + 2;
        }
        help(pub.request);
        const bool taken = game.globalWork[7] == pub.request;
        drawCommandMenu(taken ? "giveup_io" : "accept_io", t, view, {}, {}, 0, 0, 0, taken ? "giveup_io" : "accept_io");
        back();
    }
}

void Viewer::openShop() {
    shop = Shop();
    shop.stage = 1, shop.at = uiClock;
    SDL_Log("shop: open");
}

int Viewer::shopRank() const {
    int r = 0;
    for (int f = 1081; f <= 1085; ++f) r += game.flag(f);
    return r;
}

std::vector<int> Viewer::shopChars() const { // (the characters an equipment list is for: ShopWepon..ListMenu::CharaChange - every one joined, Cr3UtilValidCharacter)
    std::vector<int> c = validChars();
    if (c.empty()) c.push_back(0);
    return c;
}

int Viewer::spareCount(int id) const { // (Cr3UtilGetNotEquippedItemNumber: held, less those worn)
    int worn = 0;
    for (int c = 0; c < 9; ++c)
        for (int s = 0; s < 3; ++s) worn += game.equip[c][s] == id;
    return std::max(int(game.itemCount[id]) - worn, 0);
}

auto Viewer::shopRows() -> std::vector<ListRow> {
    std::vector<ListRow> rows;
    const int rank = shopRank();
    const std::vector<int> chars = shopChars();
    const int ch = chars[size_t(std::clamp(shop.who, 0, int(chars.size()) - 1))];
    for (int id = 1; id < 300; ++id) {
        const cr3::BattleData::Item* it = battleData.item(id);
        if (!it || it->type != shop.kind) continue;
        if (shop.kind > 0 && !(it->wearers >> ch & 1)) continue;
        ListRow r;
        r.id = id, r.name = it->name[1], r.icon = it->icon, r.help = it->text[1]; // (ChangeHelpText: Cr3UtilGetItemHelpText)
        if (shop.buy) {
            if (it->shopRank > rank) continue;
            r.number = it->price, r.selectable = game.oz >= it->price && game.itemCount[id] < 99;
        } else {
            if (game.itemCount[id] <= 0) continue;
            r.number = it->sellPrice, r.selectable = shop.kind == 0 || spareCount(id) > 0;
        }
        rows.push_back(r);
    }
    return rows;
}

int Viewer::shopLimit() const {
    const cr3::BattleData::Item* it = battleData.item(shop.item);
    if (!it) return 0;
    if (!shop.buy) return shop.kind == 0 ? game.itemCount[shop.item] : spareCount(shop.item);
    const int room = 99 - game.itemCount[shop.item];
    return it->price <= 0 ? room : std::min(room, game.oz / it->price);
}

void Viewer::shopClose(const char* call) {
    shop.stage = 0;
    if (call) callGimmick(call); // (FldMain: CallEvent)
    else pendingBranch = true;   // (back to the field)
}

void Viewer::shopTap(int id) {
    auto stage = [&](int s) { shop.stage = s, shop.at = uiClock, shop.focus = 0; };
    if (shop.stage == 1) {
        if (id == 14 || id == 15) audio.playSe(0xb4, false), shop.buy = id == 14, stage(2); // (ShopFirstMenu: 4 buy, 5 sell)
        else if (id == 13) audio.playSe(0xb4, false), shopClose("shop_talk"); // (3: FldShop's 3 - CallEvent shop_talk)
        else if (id == 100) audio.playSe(0xb5, false), shopClose();
    } else if (shop.stage == 2) {
        static const std::pair<int, int> kKinds[4] = {{16, 0}, {21, 1}, {22, 2}, {23, 3}};
        for (const auto& [b, k] : kKinds)
            if (id == b) audio.playSe(0xb4, false), shop.kind = k, shop.who = 0, stage(3);
        if (id == 100) audio.playSe(0xb5, false), stage(1);
    } else if (shop.stage == 3) {
        const std::vector<ListRow> rows = shopRows();
        if (id >= 1000 && id < 1000 + int(rows.size())) shop.focus = id - 1000;
        else if (id == 100) audio.playSe(0xb5, false), stage(2);
        else
            for (const ListRow& r : rows)
                if (r.id == id && r.selectable && shopLimit() >= 0) {
                    shop.item = id;
                    if (shopLimit() < 1) return (void)audio.playSe(0xb6, false);
                    audio.playSe(0xb4, false);
                    shop.count = 1, shop.stage = 4, shop.at = uiClock;
                }
    } else if (shop.stage == 4) {
        if (id == 7101 || id == 7102) { // (CampChooseQuantityMenu::SetViewNumber: one less / more, within 1 .. its limit)
            const int n = shop.count + (id == 7102 ? 1 : -1);
            if (n < 1 || n > shopLimit()) audio.playSe(0xb6, false);
            else shop.count = n, audio.playSe(0xb4, false);
        } else if (id == 7801 || id == 7802) {
            const cr3::BattleData::Item* it = battleData.item(shop.item);
            if (id == 7801 && it) { // (ShopCutInPiupiuMenu::Update 10 / 12)
                if (shop.buy) game.itemCount[shop.item] = static_cast<short>(std::min(game.itemCount[shop.item] + shop.count, 99)), game.oz -= it->price * shop.count;
                else game.itemCount[shop.item] = static_cast<short>(std::max(game.itemCount[shop.item] - shop.count, 0)), game.oz += it->sellPrice * shop.count;
                game.oz = std::clamp(game.oz, 0, 9999999);
                audio.playSe(200, false);
                SDL_Log("shop: %s %d of %s - %d Oz now", shop.buy ? "bought" : "sold", shop.count, it->name[1].c_str(), game.oz);
            } else audio.playSe(0xb4, false);
            shop.stage = 3, shop.at = uiClock;
        }
    }
}

void Viewer::shopKey(SDL_Keycode k) {
    if (shop.stage == 1 || shop.stage == 2) {
        static const int kFirst[3] = {14, 15, 13}, kKinds[4] = {16, 21, 22, 23}; // (1 buy, 2 sell, 3 talk)
        if (k >= SDLK_1 && k <= SDLK_4) {
            const int i = int(k - SDLK_1);
            if (shop.stage == 1 && i < 3) shopTap(kFirst[i]);
            else if (shop.stage == 2) shopTap(kKinds[i]);
        } else if (k == SDLK_BACKSPACE) shopTap(100);
        return;
    }
    if (shop.stage == 4) {
        if (k == SDLK_LEFT || k == SDLK_DOWN) shopTap(7101);
        else if (k == SDLK_RIGHT || k == SDLK_UP) shopTap(7102);
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) shopTap(7801);
        else if (k == SDLK_BACKSPACE || k == SDLK_N) shopTap(7802);
        return;
    }
    const std::vector<ListRow> rows = shopRows();
    const int n = std::max(int(rows.size()), 1);
    if (k == SDLK_UP) shop.focus = (shop.focus + n - 1) % n;
    else if (k == SDLK_DOWN) shop.focus = (shop.focus + 1) % n;
    else if ((k == SDLK_LEFT || k == SDLK_RIGHT) && shop.kind > 0) { // (ShopWepon..ListMenu::CharaChange)
        const int c = int(shopChars().size());
        shop.who = (shop.who + (k == SDLK_RIGHT ? 1 : c - 1)) % c, shop.focus = 0;
        audio.playSe(0xb4, false);
    } else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && shop.focus < int(rows.size())) shopTap(rows[size_t(shop.focus)].id);
    else if (k == SDLK_BACKSPACE) shopTap(100);
}

void Viewer::shopToken(const std::string& t) {
    static const std::map<std::string, int> kButtons = {{"talk", 13}, {"buy", 14}, {"sell", 15}, {"items", 16}, {"weapons", 21},
                                                        {"armour", 22}, {"accessories", 23}, {"back", 100}};
    if (auto b = kButtons.find(t); b != kButtons.end()) return shopTap(b->second);
    static const std::map<std::string, SDL_Keycode> kKeys = {{"up", SDLK_UP}, {"down", SDLK_DOWN}, {"pick", SDLK_RETURN}, {"more", SDLK_UP},
                                                             {"less", SDLK_DOWN}, {"yes", SDLK_Y}, {"no", SDLK_N}, {"char", SDLK_RIGHT}};
    if (auto k = kKeys.find(t); k != kKeys.end()) shopKey(k->second);
}

void Viewer::drawShop(const Mat4& view, const Mat4& proj, int w, int h) {
    const float t = uiClock - shop.at;
    auto keepLook = buttonLook;
    auto keepLevel = buttonLevel;
    auto keepFighter = buttonFighter;
    buttonLook = nullptr, buttonLevel = nullptr, buttonFighter = nullptr;
    UiFig& title = uiFig("menu"); // (ShopTitleMenu)
    title.fig.place = cr3::Mat34();
    poseUi(title, "menushop_io", uiClock - shop.at + (shop.stage > 1 ? 10.0f : 0.0f));
    drawFigure(title.fig, view.data());
    UiFig& cut = uiFig("cutin"); // (ShopCutInPiupiuMenu: Piupiu)
    cut.fig.place = cr3::Mat34();
    poseUi(cut, "cutinsgl_io", 10.0f);
    cr3::Texture* face = texture("cutIn102_L02");
    for (ViewMesh& vm : cut.fig.meshes) {
        const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
        const std::string jn = j < cut.joints.size() ? cut.joints[j] : "";
        vm.hidden = jn == "GEO_faceUp_R" || jn == "GEO_faceLow_R";
        if (face) vm.texture = face;
    }
    if (shop.stage <= 2) drawFigure(cut.fig, view.data());
    { // (OzViewWindowMenu)
        float x, y;
        UiFig& oz = uiFig("ozwin_io");
        oz.fig.place = cr3::Mat34();
        poseUi(oz, "ozwin_io", t);
        drawFigure(oz.fig, view.data());
        if (uiJoint(oz, "call_status_fr13", x, y)) drawStatusSign(13, x, y, proj);
        if (uiJoint(oz, "call_dotNum_id07", x, y)) drawDigits(std::clamp(game.oz, 0, 9999999), 110, 7, x, y, proj);
    }
    auto back = [&] {
        UiFig& bk = uiFig("btlback");
        bk.fig.place = cr3::Mat34();
        poseUi(bk, "btlback_in", t);
        drawFigure(bk.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
    };
    if (shop.stage == 1) drawCommandMenu("shopbtn_io", t, view, {}, {}, w, h, 0, "shopbtn_io"), back();
    else if (shop.stage == 2) { // (ShopSecondMenu::Initialize: ChangeAnimeTime - shopCmd_io to half its length, its in; to sell, items / accessories greyed with none to sell - Cr3UtilNumberAvailableForSale)
        UiFig& cmd = uiFig("shopcmd_io");
        poseUi(cmd, "shopcmd_io", 0);
        const float half = cmd.fig.animLoaded ? cmd.fig.anim.duration() * 0.5f : t;
        auto sellable = [&](int kind) {
            for (int id = 1; id < 300; ++id)
                if (const cr3::BattleData::Item* it = game.itemCount[id] > 0 ? battleData.item(id) : nullptr)
                    if (it->type == kind && (kind == 0 || spareCount(id) > 0)) return true;
            return false;
        };
        buttonLook = [&, this](int n) { return !shop.buy && ((n == 16 && !sellable(0)) || (n == 23 && !sellable(3))) ? 0 : 1; };
        drawCommandMenu("shopcmd_io", std::min(t, half), view, {}, {}, w, h, 0, "shopcmd_io");
        buttonLook = nullptr;
        // the buy / sell sign (shopMode_io; SetSellBuySelected: the one chosen's material at
        // 1, the other's at 0.3)
        UiFig& mode = uiFig("shopmode_io");
        mode.fig.place = cr3::Mat34();
        poseUi(mode, "shopmode_io", t);
        for (ViewMesh& vm : mode.fig.meshes) {
            const uint32_t m = vm.source.material;
            const std::string mn = m < mode.materials.size() ? mode.materials[m] : "";
            const float k = mn == "MTR_buy" ? (shop.buy ? 1.0f : 0.3f) : mn == "MTR_sell" ? (shop.buy ? 0.3f : 1.0f) : -1.0f;
            vm.material.params[0x33] = k >= 0 ? std::vector<float>{k, k, k, 1.0f} : vm.baseMaterial.params[0x33];
            if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
        }
        drawFigure(mode.fig, view.data());
        back();
    }
    else if (shop.stage == 3) {
        const std::vector<ListRow> rows = shopRows();
        if (rows.empty()) back();
        else { // (its own back: scrlBack, 100)
            shop.focus = std::clamp(shop.focus, 0, int(rows.size()) - 1);
            if (shop.kind > 0) { // (SetCharaStatusParamerter / SetItemStatusParamerter: the character's windows, the change the focused one makes worn)
                const std::vector<int> chars = shopChars();
                const int ch = chars[size_t(std::clamp(shop.who, 0, int(chars.size()) - 1))];
                int eq[8], eq2[8], change[5] = {0, 0, 0, 0, 0};
                for (int e = 0; e < 8; ++e) eq[e] = eq2[e] = game.equip[ch][e];
                eq2[shop.kind - 1] = rows[size_t(shop.focus)].id;
                const int lv = std::max(game.level[ch], 1);
                const auto a = cr3::BattleRules::playerStats(battleData, ch + 1, lv, eq, game.sopiaLevel, game.seed[ch], game.itemCount);
                const auto b = cr3::BattleRules::playerStats(battleData, ch + 1, lv, eq2, game.sopiaLevel, game.seed[ch], game.itemCount);
                for (int k = 0; k < 5; ++k) change[k] = b.param[k] - a.param[k];
                drawStatusB(ch, b.maxHp - a.maxHp, b.maxMp - a.maxMp, t, view, proj);
                drawCapacity(ch, change, t, view, proj, w, h);
            }
            drawSkillList(rows, rows[size_t(shop.focus)].id, t, view, w, h, -1, -1);
        }
    } else if (shop.stage == 4) {
        const cr3::BattleData::Item* it = battleData.item(shop.item);
        UiFig& num = uiFig("shopnum_io");
        num.fig.place = cr3::Mat34();
        poseUi(num, "shopnum_io", t);
        drawFigure(num.fig, view.data());
        float x, y, sx, sy, ex, ey;
        if (uiJoint(num, "call_dotNum_id01", x, y)) drawDigits(shop.count, 70, 2, x, y, proj); // (DotNumId2 set 0x46, the zeros shown)
        for (int k = 1; k <= 2; ++k) {
            char sa[16], ea[16];
            std::snprintf(sa, sizeof sa, "tapAreaS%02d", k), std::snprintf(ea, sizeof ea, "tapAreaE%02d", k);
            if (uiJoint(num, sa, sx, sy) && uiJoint(num, ea, ex, ey)) tapRects.push_back({7100 + k, sx, sy, ex, ey});
        }
        UiFig& plate = uiFig("plicenum_io"); // (the price: count x the item's)
        plate.fig.place = cr3::Mat34();
        poseUi(plate, "plicenum_io", t);
        drawFigure(plate.fig, view.data());
        bool sign = false, digits = false; // (the first of each: Cr3UtilGetCallAnyStringParameter / GetCallDotNumParameter 0)
        for (const std::string& j : plate.joints) {
            if (!sign && j.rfind("call_status_fr", 0) == 0 && uiJoint(plate, j, x, y)) drawStatusSign(std::atoi(j.c_str() + 14), x, y, proj), sign = true;
            if (!digits && j.rfind("call_dotNum_id", 0) == 0 && uiJoint(plate, j, x, y) && it)
                drawDigits(shop.count * (shop.buy ? it->price : it->sellPrice), 90, 7, x, y, proj), digits = true;
        }
        if (it) { // (Cr3UtilSPrint: the name for %s; DPrint: the count held for %d)
            std::string q = announce(shop.buy ? 0x70 : 0x71);
            if (size_t at = q.find("%s"); at != std::string::npos) q.replace(at, 2, it->name[1]);
            if (size_t at = q.find("%d"); at != std::string::npos) q.replace(at, 2, std::to_string(game.itemCount[shop.item]));
            drawAsk(q, t, view, w, h, 7801, 7802);
        }
    }
    buttonLook = keepLook, buttonLevel = keepLevel, buttonFighter = keepFighter;
}

void Viewer::inspectGene(int master, int held) {
    inspectedGene = held >= 0 && held < int(game.cr3Genes.size()) ? game.cr3Genes[size_t(held)] : cr3::Cr3Gene(master);
    if (held < 0 && game.cr3Db) game.cr3Db->addGeneLevels(inspectedGene, 0);
    geneInspectOpen = true;
    audio.playSe("com_003", false);
    SDL_Log("gene: status %d", master);
}

bool Viewer::geneInspectionKey(SDL_Keycode key) {
    if (!geneInspectOpen) return false;
    if (key == SDLK_BACKSPACE || key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_SPACE || key == SDLK_TAB)
        geneInspectOpen = false, audio.playSe("com_004", false);
    return true;
}

void Viewer::geneMenuScreen(int screen) { geneMenu.screen = screen, geneMenu.since = 0, geneLabFocus = 0, geneMenu.leftFocus = false, geneMenu.materialFocus = 0; }

std::vector<int> Viewer::geneLabButtons() const {
    if (geneMenu.screen == 1) return {30, 31};
    if (geneMenu.screen == 9) return {33, 34, 35}; // (SetGeneSynthesisArsenalSecond: Store, Take Out, Expand)
    return {17, 28, 21, 29};
}

int Viewer::geneLabHeld() const {
    return int(std::count_if(game.cr3Genes.begin(), game.cr3Genes.end(), [](const cr3::Cr3Gene& g) { return !g.stocked; }));
}

bool Viewer::geneLabButtonOn(int id) const {
    if (id == 17) return !geneLabVs; // (SetGeneSynthesisTopButton(GeneSynthesisMain +0x2c8): Talk only from the field)
    if (id == 28) return !game.flag(3613);
    if (id == 21) return !game.flag(3614);
    if (id == 29) return !game.flag(3615);
    // (the repository's: Store with room in the stock; Take Out with genes stocked and fewer
    // than 50 held; Expand under 250 with GENE_STOCK_PRICE coins)
    const int stocked = int(game.cr3Genes.size()) - geneLabHeld();
    if (id == 33) return stocked < game.cr3GeneStock;
    if (id == 34) return stocked > 0 && geneLabHeld() < 50;
    if (id == 35) return game.cr3GeneStock < 250 && game.cr3Db && game.cr3Coins >= game.cr3Db->constant("GENE_STOCK_PRICE");
    return true;
}

// AnnounceData (message\AnnounceMenuName.dat - Cr3UtilGetMnlCmpAnnounceData's): u32 the
// records' offset, the texts', the count; a record two u32s - the Japanese text's, the
// English's - by number from 1; "\r\n" in them a line break
std::string Viewer::menuAnnounce(int id) { return announceText("AnnounceMenuName", id); }

// an AnnounceData file's text (message\<file>.dat: 8-byte records - Japanese, English)
std::string Viewer::announceText(const std::string& file, int id) {
    std::vector<uint8_t>& d = messageFiles[file];
    if (d.empty() && !archive.read("message\\" + file + ".dat", d)) return {};
    auto u32 = [&](size_t o) { return o + 4 <= d.size() ? uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24) : 0u; };
    if (id < 1 || uint32_t(id) > u32(8)) return {};
    const size_t o = size_t(u32(4)) + u32(size_t(u32(0)) + size_t(id - 1) * 8 + (optionLanguage == 0 ? 0 : 4));
    std::string s;
    for (size_t k = o; k < d.size() && d[k]; ++k) s += char(d[k]);
    for (size_t p; (p = s.find('\r')) != std::string::npos;) s.erase(p, 1);
    return s;
}

// the help line (SetHeaderSentence: comHeMenu's typeM_L01)
void Viewer::drawGeneLabSentence(const UiFig& menu, const std::string& text, int w, int h) {
    float tx, ty;
    if (text.empty() || !cr3UiCall(menu, "typeM_L01", tx, ty)) return;
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    const size_t nl = text.find("\\n");
    // (dark grey on the bar, as the reference video's - about 60, 60, 65 -, at 23: its line
    // "Weight: 6/Power: 34/..." as wide as the reference's, from where it starts)
    if (nl == std::string::npos) { // (one too long for the bar - to the battery, 0.9 from the right -: smaller, to 18, then narrower)
        const float k = std::min(float(w) / 9.6f, float(h) / 6.4f);
        const float room = std::min(0.97f, (float(w) / k / 2 - 0.9f - (tx - 0.03f)) / 10);
        const float wide = uiTextWidth(text, 23, w, h), size = wide > room ? std::max(23 * room / wide, 18.0f) : 23;
        return (void)drawUiText(text, size, (tx - 0.03f) / 10, (ty - 0.1f + (23 - size) * 0.004f) / 10, w, h, true, 0.24f, room);
    }
    // (two lines: the bar's height between them)
    drawUiText(text.substr(0, nl), 18, (tx + 0.075f) / 10, (ty - 0.04f) / 10, w, h, true, 0.24f);
    drawUiText(text.substr(nl + 2), 18, (tx + 0.075f) / 10, (ty - 0.27f) / 10, w, h, true, 0.24f);
}

void Viewer::drawCr3GeneLab(cr3::EventRuntime& ev, int w, int h, bool front) {
    const cr3::EventRuntime::GeneLab& lab = ev.geneLab();
    if (!lab.open || lab.talking) return;
    geneLabVs = lab.vs;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    float x, y;
    if (!front) {
        dev->clearDepth();
        UiFig& bg = cr3UiPut("shbgpositon", "", 0, 0, 0, view);
        if (cr3UiCall(bg, "call_shBgMenu", x, y, w, h)) cr3UiPut("shbgmenu", "", 0, x, y, view);
        return;
    }
    dev->clearDepth();
    geneLabTaps.clear();
    const int screen = geneMenu.screen;
    if (screen == 5 || screen == 6) return drawGeneMerge(w, h, view);
    const float t = geneLabClock;
    // the header (SetHeaderTitle: 30 the lab, 31 combining, 32 enhancing, 34 selling, 35 the repository)
    std::string sentence;
    if (screen <= 1) sentence = menuAnnounce(0x26e); // (SetGeneSynthesisTopButton / SecondButton: "Select command.")
    if (screen == 2) sentence = menuAnnounce(geneMenu.mode == 1 ? 0x176 : 0x18e);
    if (screen == 3 || (screen == 7 && geneMenu.mode == 1)) sentence = menuAnnounce(0x177); // (asked: " ")
    if (screen == 7 && geneMenu.mode == 2) sentence = menuAnnounce(0x273);
    if (screen == 7 && geneMenu.mode >= 3) sentence = menuAnnounce(geneMenu.mode == 3 ? 0x17c : 0x17d);
    drawCr3Header(t, screen == 9 || geneMenu.mode >= 3 ? 35 : screen < 2 ? (geneLabVs ? 37 : 30) : geneMenu.mode == 1 ? 32 : geneMenu.mode == 2 ? 34 : 31, sentence, w, h,
                  view); // (CreateGeneSynthesisMenu: 0x25 while GameMain's VS flag is on)
    // the buttons
    if (screen <= 1 || screen == 9) {
        const std::vector<int> ids = geneLabButtons();
        UiFig& pos = cr3UiPut("combtnposition", "", 0, 0, 0, view);
        if (cr3UiCall(pos, "call_comBtnMenu", x, y, w, h)) {
            UiFig& menu = cr3UiPut("combtnmenu", "combtnmenu_io", screen == 0 ? t : geneMenu.since, x, y, view);
            for (int b = 0; b < int(ids.size()); ++b) {
                float px, py;
                if (!cr3UiCall(menu, "call_comBtnPanel_00" + std::to_string(4 + b), px, py)) continue;
                const int id = ids[size_t(b)];
                const bool off = !geneLabButtonOn(id);
                cr3UiTint[0] = cr3UiTint[1] = off ? 0.549f : 1.0f, cr3UiTint[2] = off ? 0.655f : 1.0f; // (cr3UiPut's tint)
                UiFig& panel = cr3UiPut("combtnpanel", "", 0, px, py, view);
                float cx, cy;
                if (cr3UiCall(panel, "call_comBtnBg_fr00", cx, cy)) cr3UiPut("combtnbg_fr00", "", float(b) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnIcon_fr00", cx, cy)) cr3UiPut("combtnicon_fr00", "", float(id) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnText_fr00", cx, cy))
                    cr3UiPut(b == geneLabFocus ? "combtntexttap_fr00" : "combtntext_fr00", "", float(id) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnR", cx, cy)) cr3UiPut("combtnr", "", 0, cx, cy, view);
                float ax, ay, bx, by;
                if (!off && cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) geneLabTaps.push_back({ax, ay, bx, by, id});
            }
            {
                cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1.0f;
            }
        }
    }
    if (screen == 2) drawGeneBase(w, h, view);
    if (screen == 3 || screen == 4) drawGeneMaterials(w, h, view);
    if (screen == 7) drawGenePicks(w, h, view);
    // the footer (GeneSynthesisFutter 0xa0: comFtrMixMenu at comFtrPosition's fit4 - the genes
    // held of 50 (comFtrMixGenesPanel), the stock of its room (comFtrMixBoxPanel), each a
    // comListNum_fr00 digit 0.19 apart from its call_comListNum_fr00_001 / _002 (SetNumberOfGene:
    // leading zeros hidden) - or, with a list (+0xd4 1), the tabs comFtrMixTabTap_fr00 on
    // comFtrMixTabPanel: frames 0 .. 3 ALL, N, R, SR, + 6 the one chosen - a radio, ALL to begin with); none while the base
    // is chosen (EndGeneSynthesisFooter - GeneChangeMenu's own footer has only its back)
    // and its back button (ComBackButton: comBackMenu at comFtrPosition's call_comBackMenu_001_fit6)
    UiFig& foot = cr3UiPut("comftrposition", "", 0, 0, 0, view);
    if (screen != 2 && screen != 4 && screen != 8 && cr3UiCall(foot, "call_comFtrMixMenu", x, y, w, h)) {
        UiFig& mix = cr3UiPut("comftrmixmenu", "", 0, x, y, view);
        const int held = geneLabHeld();
        const int stocked = int(game.cr3Genes.size()) - held;
        const int capacity = game.cr3Db ? game.cr3Db->constant("GENE_CAPACITY", 50) : 50;
        auto number = [&](const UiFig& panel, const std::string& joint, int value, int places) {
            float nx, ny;
            if (!cr3UiCall(panel, joint, nx, ny)) return;
            int most = 1;
            for (int k = 0; k < places; ++k) most *= 10;
            value = std::clamp(value, 0, most - 1);
            bool shown = false;
            for (int k = 0; k < places; ++k, nx += 0.19f) {
                most /= 10;
                const int d = value / most % 10;
                if (d > 0 || k == places - 1) shown = true;
                if (shown) cr3UiPut("comlistnum_fr00", "", float(d) / 30, nx, ny, view);
            }
        };
        float px, py;
        const bool tabs = screen == 3 || (screen == 7 && geneMenu.mode < 3);
        if (!tabs && cr3UiCall(mix, "call_comFtrMixGenesPanel", px, py)) {
            UiFig& genes = cr3UiPut("comftrmixgenespanel", "", 0, px, py, view);
            number(genes, "call_comListNum_fr00_001", held, 2);
            number(genes, "call_comListNum_fr00_002", capacity, 2);
        }
        if (!tabs && cr3UiCall(mix, "call_comFtrMixBoxPanel", px, py)) {
            UiFig& box = cr3UiPut("comftrmixboxpanel", "", 0, px, py, view);
            number(box, "call_comListNum_fr00_001", stocked, 3);
            number(box, "call_comListNum_fr00_002", game.cr3GeneStock, 3);
        }
        if (tabs && cr3UiCall(mix, "call_comFtrMixTabPanel", px, py)) {
            UiFig& panel = cr3UiPut("comftrmixtabpanel", "", 0, px, py, view);
            for (int k = 0; k < 4; ++k) {
                float tx, ty;
                if (cr3UiCall(panel, "call_comFtrMixTabTap_fr00_00" + std::to_string(k + 1), tx, ty))
                    cr3UiPut("comftrmixtabtap_fr00", "", float(k + (k == geneMenu.tab ? 6 : 0)) / 30, tx, ty, view); // (TouchGestureTap: the chosen one's frame + 6)
                float ax, ay, bx, by;
                if (cr3UiCall(panel, "tapAreaS0" + std::to_string(k + 1), ax, ay) && cr3UiCall(panel, "tapAreaE0" + std::to_string(k + 1), bx, by))
                    geneLabTaps.push_back({ax, ay, bx, by, kTapTab + k});
            }
        }
    }
    if (screen != 4 && screen != 8 && cr3UiCall(foot, "call_comBackMenu_001", x, y, w, h)) {
        UiFig& menu = cr3UiPut("combackmenu", "", 0, x, y, view);
        float cx, cy, ax, ay, bx, by;
        if (cr3UiCall(menu, "call_comBack_fr00", cx, cy)) cr3UiPut("comback_fr00", "", 0, cx, cy, view);
        if (cr3UiCall(menu, "tapAreaS01", ax, ay) && cr3UiCall(menu, "tapAreaE01", bx, by)) geneLabTaps.push_back({ax, ay, bx, by, -1});
    }
    // (the picks' OK: GeneSynthesisFutter's second ComBackButton - comBackMenu at
    // call_comBackMenu_002_fit6, comBack_fr00 frame 1; dim with none picked - SetBackButtonReaction)
    if (screen == 7 && cr3UiCall(foot, "call_comBackMenu_002", x, y, w, h)) {
        const float a = geneMenu.picked.empty() ? 0.5f : 1.0f;
        UiFig& menu = cr3UiPut("combackmenu", "", 0, x, y, view, a);
        float cx, cy, ax, ay, bx, by;
        if (cr3UiCall(menu, "call_comBack_fr00", cx, cy)) cr3UiPut("comback_fr00", "", 1.0f / 30, cx, cy, view, a);
        if (!geneMenu.picked.empty() && cr3UiCall(menu, "tapAreaS01", ax, ay) && cr3UiCall(menu, "tapAreaE01", bx, by))
            geneLabTaps.push_back({ax, ay, bx, by, kTapOk});
    }
    if (screen == 4 || screen == 8) drawGeneAsk(w, h, view);
    if (!geneMenu.alert.empty()) drawGeneAlert(w, h, view);
    if (geneInspectOpen) {
        dev->clearDepth();
        drawGeneBase(w, h, view, &inspectedGene);
        geneLabTaps.clear();
        geneLabTaps.push_back({-100, -100, 100, 100, kTapAlert});
    }
}

// the base gene (GeneChangeMenu 0xa5, CreateBaseGeneSelectMenu): its status at the left -
// geneCngStsMenu at geneCngStsPositon's call_geneCngStsMenu: the eight commands and skills at
// typeM_L01 .. L08 (those to come dim), the element (btStAtbIcon_fr00, frame the kind), the lock
// (geneCnglockBtn_fr00: 1 locked), camEquJenePanel at call_ccamEquJenePanel - the name at its
// typeM_L01, the level (camEquNumLv_num00: comNum04_fr00's three - 10, 11, 12 "MAX"), the six
// numbers (geneCngStsNum_num00 at call_camEquNumSts_num00_000 .. 005: comNum00_fr00's five), the
// HP and MP bars full; the cards at the right - geneCngListMenu at geneCngListPosition's fit6,
// its nine call_geneCngListPanel joints the places (x, y + 0.25, z x 10), the one in focus the
// fifth at 4 x, the rest 3 x
void Viewer::drawGeneBase(int w, int h, const Mat4& view, const cr3::Cr3Gene* inspected) {
    const float t = inspected ? 1 : geneMenu.since, alpha = std::min(t / 0.2f, 1.0f);
    const std::vector<cr3::Cr3Gene>& genes = game.cr3Genes;
    geneMenu.focus = std::clamp(geneMenu.focus, 0, std::max(int(geneMenu.genes.size()) - 1, 0));
    float x, y;
    UiFig& lp = cr3UiPut("genecnglistposition", "genecnglistposition_io", t, 0, 0, view, alpha);
    if (!inspected && cr3UiCall(lp, "call_geneCngListMenu", x, y, w, h)) {
        UiFig& list = cr3UiPut("genecnglistmenu", "genecnglistmenu_io", t, x, y, view, alpha);
        float px[9], py[9], pz[9];
        int slots = 0;
        for (const std::string& j : list.joints) {
            if (j.compare(0, 21, "call_geneCngListPanel") != 0 || slots >= 9) continue;
            if (!uiJoint(list, j, px[slots], py[slots])) continue;
            pz[slots] = 0; // (the joints' depth: drawn back to front instead)
            ++slots;
        }
        // (the list's count: the one in focus, of all - geneCngListNum_num00's comNum00 digits)
        for (int k = 0; k < 2; ++k) {
            float nx, ny;
            if (!cr3UiCall(list, "call_geneCngListNum_num00_00" + std::to_string(k + 1), nx, ny)) continue;
            UiFig& num = cr3UiPut("genecnglistnum_num00", "", 0, nx, ny, view, alpha);
            cr3UiNumber(num, "call_geneCngNum_fr00", "genecngnum_fr00", k == 0 ? geneMenu.focus + 1 : int(geneMenu.genes.size()), 3, 0, view, alpha);
        }
        if (slots == 9) {
            dev->clearDepth();
            // (back to front: the far ones first, the one in focus last)
            std::vector<int> order;
            for (int s = 1; s <= 8; ++s) order.push_back(s);
            std::sort(order.begin(), order.end(), [](int a, int b) { return std::abs(a - 5) > std::abs(b - 5); });
            for (const int s : order) {
                const int g = geneMenu.focus + s - 5;
                if (g < 0 || g >= int(geneMenu.genes.size())) continue;
                const cr3::Cr3Gene& gene = genes[size_t(geneMenu.genes[size_t(g)])];
                const float scale = s == 5 ? 4.0f : 3.0f;
                drawCr3Card(gene.master, px[s], py[s] + 0.25f, pz[s] + float(8 - std::abs(s - 5)) * 0.1f, scale, view,
                            alpha * (vitaButtons() && cr3Camp.stage == 3 && cr3Camp.geneButton ? 0.6f : 1), geneBaseSelectable(gene));
                const float hw = 0.15f * scale, hh = 0.22f * scale; // (its tap: the card's face)
                geneLabTaps.push_back({px[s] - hw, py[s] + 0.25f - hh, px[s] + hw, py[s] + 0.25f + hh, kTapCard + g});
            }
        }
    }
    if (!inspected && geneMenu.genes.empty()) return;
    const cr3::Cr3Gene& gene = inspected ? *inspected : genes[size_t(geneMenu.genes[size_t(geneMenu.focus)])];
    if (inspected) drawCr3Card(gene.master, 2.4f, 0, 1, 7, view);
    const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db ? game.cr3Db->geneInfo(gene.master) : nullptr;
    if (!gi) return;
    dev->clearDepth();
    UiFig& sp = cr3UiPut("genecngstspositon", "", t, 0, 0, view, alpha); // (its own animation: in from the right)
    if (!cr3UiCall(sp, "call_geneCngStsMenu", x, y)) return;
    UiFig& sts = cr3UiPut("genecngstsmenu", "", 0, x, y, view, alpha);
    float cx, cy;
    if (cr3UiCall(sts, "call_btStAtbIcon_fr00", cx, cy)) cr3UiPut("btstatbicon_fr00", "", float(gi->kind) / 30, cx, cy, view, alpha);
    if (cr3UiCall(sts, "call_geneCnglockBtn_fr00", cx, cy)) {
        if (vitaButtons() && cr3Camp.stage == 3 && cr3Camp.geneButton && cr3Camp.geneSkill < 0)
            cr3UiTintMaterial = "mat_gene_change_01", cr3UiMaterialTint[0] = 0.5f, cr3UiMaterialTint[1] = 1, cr3UiMaterialTint[2] = 1.5f;
        cr3UiPut("genecnglockbtn_fr00", "", gene.locked ? 1.0f / 30 : 0.0f, cx, cy, view, alpha);
        cr3UiTintMaterial.clear();
    }
    const int level = game.cr3Db->geneLevel(gene);
    const std::array<int, 6> stats = game.cr3Db->geneStats(gene);
    if (cr3UiCall(sts, "call_ccamEquJenePanel", cx, cy)) {
        UiFig& panel = cr3UiPut("camequjenepanel", "", 0, cx, cy, view, alpha);
        float nx, ny;
        if (cr3UiCall(panel, "call_camEquHp_per", nx, ny)) cr3UiPut("camequhp_per", "", 100.0f / 30, nx, ny, view, alpha);
        if (cr3UiCall(panel, "call_camEquMp_per", nx, ny)) cr3UiPut("cameqump_per", "", 100.0f / 30, nx, ny, view, alpha);
        if (cr3UiCall(panel, "call_camEquNumLv_num00", nx, ny)) {
            UiFig& lv = cr3UiPut("camequnumlv_num00", "", 0, nx, ny, view, alpha);
            cr3UiNumber(lv, "call_comNum04_fr00", "comnum04_fr00", level, 3, 0, view, alpha, level >= gi->maxLevel ? 10 : -1);
        }
        for (int k = 0; k < 6; ++k) {
            char joint[40];
            std::snprintf(joint, sizeof joint, "call_camEquNumSts_num00_%03d", k);
            if (!cr3UiCall(panel, joint, nx, ny)) continue;
            UiFig& num = cr3UiPut("genecngstsnum_num00", "", 0, nx, ny, view, alpha);
            cr3UiNumber(num, "call_comNum00_fr00", "comnum00_fr00", stats[size_t(k)], 5, 0, view, alpha);
        }
        if (cr3UiCall(panel, "typeM_L01", nx, ny)) {
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            drawUiText(cr3NameOf("gene_name.mbe", "GeneName", gene.master), 26, (nx + 0.075f) / 10, (ny - 0.15f) / 10, w, h, true, 0.26f); // (dark on the light panel)
        }
    }
    // the eight: its slots' commands and skills, then (dim) those its levels still to come teach
    std::vector<std::pair<std::string, bool>> lines;
    for (int s = 0; s < 8; ++s) {
        const int type = gene.slots[s][0], id = gene.slots[s][1];
        if (id < 0) continue;
        lines.push_back({type == 1 ? cr3NameOf("skill_name.mbe", "SkillName", id) : cr3NameOf("command_name.mbe", "CommandName", id), true});
    }
    for (const auto& l : gi->learns)
        if (l[0] > level && lines.size() < 8) lines.push_back({l[1] == 1 ? cr3NameOf("skill_name.mbe", "SkillName", l[2]) : cr3NameOf("command_name.mbe", "CommandName", l[2]), false});
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    for (size_t k = 0; k < lines.size() && k < 8; ++k) {
        char joint[16];
        std::snprintf(joint, sizeof joint, "typeM_L%02zu", k + 1);
        const bool selected = vitaButtons() && cr3Camp.stage == 3 && cr3Camp.geneButton && int(k) == cr3Camp.geneSkill;
        if (cr3UiCall(sts, joint, cx, cy)) drawUiText((selected ? "> " : "") + lines[k].first, 24, (cx + 0.075f) / 10, (cy - 0.15f) / 10, w, h, true, lines[k].second ? 0.26f : 0.6f);
    }
}

// the materials (GeneSynthesisListMenu 0xa2: comListMenu at comListPosition's call_comListMenu,
// its io animator in; three rows 1.24 apart from call_comListPanel - comListLPanel, at its
// call_comListLMixing comListLMixing: the result's picture (comListGeneIcon_id00's mat_card_001),
// element, grade (comListRare_fr00), stars (mixStar_fr00: stars - 1), name (typeM_L01, dark),
// level (comListLv_txt's comListNumUn - comListNum_fr00 + 10; "MAX" 40 ..) and the gold (comListNumUn
// at _001: comListUnIcon_fr00 2, 7 digits + 30), the cursor comListLCursor on the one in focus,
// those not affordable dimmed) beside NormalSynthesisMenu (0xa1: mixMtrPositon - the base and
// the material in mixMtrMenu at call_mixMtrMenu_001 / _002, titled mixMtrText_fr00 0 / 1, each
// its picture, element, grade, name, stars and level (mixMtrLvPanel); the mix button
// mixBtn_fr00 at call_mixBtn_fr00 - the result's card (9 x, at call_GeneCard - 0.25, - 0.02) in
// their place)
// (`style` 0: the level as text and the gold - Combine's, Sell's; 1: the level's bar and number -
// Enhance's, InitializeL 2: comListLv_per at its share of the top level, comListNumL_num00's
// digits + 10)
void Viewer::drawGeneRowParts(const UiFig& mixing, int master, int level, int maxLevel, int gold, const Mat4& view, float alpha, int w, int h, int style) {
    const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db ? game.cr3Db->geneInfo(master) : nullptr;
    if (!gi) return;
    float cx, cy;
    if (cr3UiCall(mixing, "call_comListGeneIcon_id00", cx, cy)) {
        UiFig& icon = uiFig("comlistgeneicon_id00");
        char pic[16];
        std::snprintf(pic, sizeof pic, "card_%03d", gi->series);
        cr3UiMaterial(icon, "mat_card_001", pic);
        cr3UiPut("comlistgeneicon_id00", "", 0, cx, cy, view, alpha);
    }
    if (cr3UiCall(mixing, "call_btStAtbIcon_fr00", cx, cy)) cr3UiPut("btstatbicon_fr00", "", float(gi->kind) / 30, cx, cy, view, alpha);
    if (cr3UiCall(mixing, "call_comListRare_fr00", cx, cy)) cr3UiPut("comlistrare_fr00", "", float(gi->grade) / 30, cx, cy, view, alpha);
    if (cr3UiCall(mixing, "call_mixStar_fr00", cx, cy)) cr3UiPut("mixstar_fr00", "", float(gi->stars - 1) / 30, cx, cy, view, alpha);
    if (style == 1) {
        if (cr3UiCall(mixing, "call_comListLv_per", cx, cy)) cr3UiPut("comlistlv_per", "", float(level) * 100.0f / float(std::max(maxLevel, 1)) / 30, cx, cy, view, alpha);
        if (cr3UiCall(mixing, "call_comListNumL_num00", cx, cy)) {
            UiFig& num = cr3UiPut("comlistnuml_num00", "", 0, cx, cy, view, alpha);
            cr3UiNumber(num, "call_comListNum_fr00", "comlistnum_fr00", level, 3, 10, view, alpha, level >= maxLevel ? 40 : -1);
        }
    } else if (cr3UiCall(mixing, "call_comListLv_txt", cx, cy)) {
        UiFig& lv = cr3UiPut("comlistlv_txt", "", 0, cx, cy, view, alpha);
        float nx, ny;
        if (cr3UiCall(lv, "call_comListNumUn_num00_001", nx, ny)) {
            UiFig& num = cr3UiPut("comlistnumun_num00", "", 0, nx, ny, view, alpha);
            const bool top = level >= maxLevel;
            // (its seven places: the level in the last three)
            for (int k = 0; k < 3; ++k) {
                float dx, dy;
                const int p = k == 0 ? 100 : k == 1 ? 10 : 1;
                if ((!top && level < p && k < 2) || !cr3UiCall(num, "call_comListNum_fr00_00" + std::to_string(5 + k), dx, dy)) continue;
                cr3UiPut("comlistnum_fr00", "", float(top ? 40 + k : level / p % 10 + 10) / 30, dx, dy, view, alpha);
            }
        }
    }
    if (gold >= 0 && cr3UiCall(mixing, "call_comListNumUn_num00_001", cx, cy)) {
        UiFig& num = cr3UiPut("comlistnumun_num00", "", 0, cx, cy, view, alpha);
        float ix, iy;
        if (cr3UiCall(num, "call_comListUnIcon_fr00", ix, iy)) cr3UiPut("comlistunicon_fr00", "", 2.0f / 30, ix, iy, view, alpha);
        cr3UiNumber(num, "call_comListNum_fr00", "comlistnum_fr00", gold, 7, 30, view, alpha);
    }
    if (cr3UiCall(mixing, "typeM_L01", cx, cy)) {
        dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
        drawUiText(cr3NameOf("gene_name.mbe", "GeneName", master), 26, (cx + 0.075f) / 10, (cy - 0.15f) / 10, w, h, true, 0.26f);
    }
}

void Viewer::geneMenuRows() {
    geneMenu.rows.clear();
    if (!game.cr3Db) return;
    const auto& previews = game.cr3Db->mergePreviews();
    for (size_t k = 0; k < previews.size(); ++k) {
        const cr3::Cr3Gene& partner = game.cr3Genes[size_t(previews[k].partner)];
        if (partner.stocked || partner.locked || game.geneWearer(previews[k].partner)) continue; // (the 50 alone; a locked or worn one never given up - SetEquipLockSynthesisListMenu)
        const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db->geneInfo(previews[k].result.master);
        if (geneMenu.tab > 0 && (!gi || gi->grade != geneMenu.tab - 1)) continue;
        geneMenu.rows.push_back(int(k));
    }
    geneMenu.row = std::clamp(geneMenu.row, 0, std::max(int(geneMenu.rows.size()) - 1, 0));
}

void Viewer::drawGeneMaterials(int w, int h, const Mat4& view) {
    if (!game.cr3Db || geneMenu.base < 0 || size_t(geneMenu.base) >= game.cr3Genes.size()) return;
    const float t = geneMenu.since;
    const auto& previews = game.cr3Db->mergePreviews();
    const cr3::Cr3Gene& base = game.cr3Genes[size_t(geneMenu.base)];
    const cr3::Cr3OfflineDb::MergePreview* chosen =
        geneMenu.row < int(geneMenu.rows.size()) ? &previews[size_t(geneMenu.rows[size_t(geneMenu.row)])] : nullptr;
    float x, y;
    // the two and the mix button
    // (asked: SetConfirmationNormalSynthesisMenu - the two at mixOkMtrPositon's, the list and
    // the footer gone)
    const bool asked = geneMenu.screen == 4;
    UiFig& mp = cr3UiPut(asked ? "mixokmtrpositon" : "mixmtrpositon", "", asked ? t : 0, 0, 0, view, std::min(t / 0.2f, 1.0f));
    auto panel = [&](int k, const cr3::Cr3Gene& g) {
        const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db->geneInfo(g.master);
        float px, py;
        if (!gi || !cr3UiCall(mp, "call_mixMtrMenu_00" + std::to_string(k + 1), px, py)) return;
        if (vitaButtons() && geneMenu.leftFocus && geneMenu.materialFocus == k) cr3UiTint[0] = 0.5f, cr3UiTint[1] = 1, cr3UiTint[2] = 1.5f;
        UiFig& menu = cr3UiPut("mixmtrmenu", "", 0, px, py, view);
        cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
        float cx, cy;
        if (cr3UiCall(menu, "call_mixMtrText_fr00", cx, cy)) cr3UiPut("mixmtrtext_fr00", "", float(k) / 30, cx, cy, view);
        if (cr3UiCall(menu, "call_mixStar_fr00", cx, cy)) cr3UiPut("mixstar_fr00", "", float(gi->stars - 1) / 30, cx, cy, view);
        if (cr3UiCall(menu, "call_mixMtrPanel", cx, cy)) {
            UiFig& p = cr3UiPut("mixmtrpanel", "", 0, cx, cy, view);
            float ix, iy;
            if (cr3UiCall(p, "call_comListGeneIcon_id00", ix, iy)) {
                UiFig& icon = uiFig("comlistgeneicon_id00");
                char pic[16];
                std::snprintf(pic, sizeof pic, "card_%03d", gi->series);
                cr3UiMaterial(icon, "mat_card_001", pic);
                cr3UiPut("comlistgeneicon_id00", "", 0, ix, iy, view);
            }
            if (cr3UiCall(p, "call_btStAtbIcon_fr00", ix, iy)) cr3UiPut("btstatbicon_fr00", "", float(gi->kind) / 30, ix, iy, view);
            if (cr3UiCall(p, "call_comListRare_fr00", ix, iy)) cr3UiPut("comlistrare_fr00", "", float(gi->grade) / 30, ix, iy, view);
            if (cr3UiCall(p, "typeM_L00", ix, iy)) {
                dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                drawUiText(cr3NameOf("gene_name.mbe", "GeneName", g.master), 26, (ix + 0.075f) / 10, (iy - 0.15f) / 10, w, h, true, 0.26f);
            }
        }
        if (cr3UiCall(menu, "call_mixMtrLvPanel", cx, cy)) {
            UiFig& lv = cr3UiPut("mixmtrlvpanel", "", 0, cx, cy, view);
            float nx, ny;
            const int level = game.cr3Db->geneLevel(g);
            if (cr3UiCall(lv, "call_comListNum_fr00_001", nx, ny)) {
                const bool top = level >= gi->maxLevel;
                bool shown = top;
                for (int d = 0, p = 100; d < 3; ++d, p /= 10, nx += 0.19f) {
                    if (level / p % 10 > 0 || d == 2) shown = true;
                    if (shown) cr3UiPut("comlistnum_fr00", "", float(top ? 40 + d : level / p % 10) / 30, nx, ny, view);
                }
            }
        }
    };
    if (geneMenu.result && chosen && !asked) {
        float gx, gy;
        if (cr3UiCall(mp, "call_GeneCard", gx, gy)) {
            dev->clearDepth();
            drawCr3Card(chosen->result.master, gx - 0.25f, gy - 0.02f, 1.0f, 9.0f, view);
        }
    } else {
        panel(0, base);
        if (chosen) panel(1, game.cr3Genes[size_t(chosen->partner)]);
    }
    if (asked) return;
    if (chosen && cr3UiCall(mp, "call_mixBtn_fr00", x, y)) {
        if (vitaButtons() && geneMenu.leftFocus && geneMenu.materialFocus == 2) cr3UiTint[0] = 0.5f, cr3UiTint[1] = 1, cr3UiTint[2] = 1.5f;
        UiFig& b = cr3UiPut("mixbtn_fr00", "", geneMenu.result ? 1.0f / 30 : 0.0f, x, y, view);
        cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
        float ax, ay, bx, by;
        if (cr3UiCall(b, "tapAreaCP01", ax, ay) && cr3UiCall(b, "tapAreaAP01", bx, by)) geneLabTaps.push_back(circleTap(ax, ay, bx, by, kTapMix));
    }
    // the list
    UiFig& pos = cr3UiPut("comlistposition", "comlistposition_io", t, 0, 0, view);
    if (!cr3UiCall(pos, "call_comListMenu", x, y, w, h)) return;
    UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
    float lx, ly;
    if (!cr3UiCall(menu, "call_comListPanel", lx, ly)) return;
    const int top = std::clamp(geneMenu.row - 1, 0, std::max(int(geneMenu.rows.size()) - 3, 0));
    for (int k = top; k < int(geneMenu.rows.size()) && k < top + 3; ++k) {
        const cr3::Cr3OfflineDb::MergePreview& p = previews[size_t(geneMenu.rows[size_t(k)])];
        const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db->geneInfo(p.result.master);
        if (!gi) continue;
        const bool affordable = game.cr3Money >= p.gold;
        const float alpha = affordable ? 1.0f : 0.5f;
        const float ry = ly - 1.24f * float(k - top);
        UiFig& row = cr3UiPut("comlistlpanel", "", 0, lx, ry, view, alpha);
        float cx, cy;
        if (k == geneMenu.row && cr3UiCall(row, "call_comListLCursor", cx, cy)) cr3UiPut("comlistlcursor", "", 0, cx, cy, view);
        if (cr3UiCall(row, "call_comListLMixing", cx, cy)) {
            UiFig& mixing = cr3UiPut("comlistlmixing", "", 0, cx, cy, view, alpha);
            drawGeneRowParts(mixing, p.result.master, game.cr3Db->geneLevel(p.result), gi->maxLevel, p.gold, view, alpha, w, h);
        }
        float ax, ay, bx, by;
        if (affordable && cr3UiCall(row, "tapAreaS01", ax, ay) && cr3UiCall(row, "tapAreaE01", bx, by)) geneLabTaps.push_back({ax, ay, bx, by, kTapRow + k});
    }
}

// a card the base may be (Combine: not locked or worn - 0x00a9388c; Enhance: not at its top level - SetLevelLockSynthesisListMenu)
bool Viewer::geneBaseSelectable(const cr3::Cr3Gene& g) const {
    if (cr3Camp.stage == 3) return true;
    if (cr3Camp.stage == 4 && campItems.step == 2) return campItemCanTargetGene(g);
    if (geneMenu.mode == 0) {
        if (g.locked) return false;
        if (g.uid > 0)
            for (const auto& [ch, uid] : game.cr3GeneOf)
                if (uid == g.uid) return false;
        return true;
    }
    if (geneMenu.mode != 1 || !game.cr3Db) return true;
    const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db->geneInfo(g.master);
    return gi && game.cr3Db->geneLevel(g) < gi->maxLevel;
}

// the genes to pick from (CreateGeneSynthesisIntensificationListItem / SellListItem): the 50
// held but the base and the locked, by the footer's tab (their grade)
void Viewer::geneMenuPickRows() {
    geneMenu.pickRows.clear();
    for (size_t k = 0; k < game.cr3Genes.size(); ++k) {
        const cr3::Cr3Gene& g = game.cr3Genes[k];
        if (g.stocked != (geneMenu.mode == 4) || int(k) == geneMenu.base) continue;
        if ((g.locked || game.geneWearer(int(k))) && geneMenu.mode != 4) continue; // (locked or worn: kept)
        const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db ? game.cr3Db->geneInfo(g.master) : nullptr;
        if (!gi || (geneMenu.tab > 0 && gi->grade != geneMenu.tab - 1)) continue;
        geneMenu.pickRows.push_back(int(k));
    }
    geneMenu.row = std::clamp(geneMenu.row, 0, std::max(int(geneMenu.pickRows.size()) - 1, 0));
}

// the picks: Enhance's (IntensificationSynthesisMenu 0xaa - mixUpMenu at mixMtrPositon's
// call_mixUpMenu: Components, Additional EXP, Total Cost at typeM_L01 .. L03 (AnnounceMenuName
// 0x184 .. 0x186), the count and its 20 in slots 1 and 2, the EXP in 3, the gold in 4; the base's
// mixMtrPanel, mixMtrLvwPanel - its level now and after - and stars; the mix button the base's
// card) or Sell's (GeneSellPanel - mixSellMenu at call_mixSelMenu: Genes to Sell and Additional
// ML at typeM_L01 / L02 (0x17a, 0x17b), the count / 20 in slots 1 and 2, the gold in 3, + 30);
// the rows as the materials' (comListCheck on those picked)
void Viewer::drawGenePicks(int w, int h, const Mat4& view) {
    if (!game.cr3Db) return;
    const float t = geneMenu.since;
    const bool sell = geneMenu.mode == 2;
    float x, y, cx, cy;
    UiFig& mp = cr3UiPut("mixmtrpositon", "", 0, 0, 0, view, std::min(t / 0.2f, 1.0f));
    if (geneMenu.mode >= 3) { // (SetSynthesisParameterCardView: the row in focus's card)
        if (geneMenu.row < int(geneMenu.pickRows.size()) && cr3UiCall(mp, "call_GeneCard", x, y)) {
            dev->clearDepth();
            drawCr3Card(game.cr3Genes[size_t(geneMenu.pickRows[size_t(geneMenu.row)])].master, x - 0.25f, y - 0.02f, 1.0f, 9.0f, view);
        }
    } else if (sell) {
        if (cr3UiCall(mp, "call_mixSelMenu", x, y)) {
            UiFig& m = cr3UiPut("mixsellmenu", "", 0, x, y, view);
            cr3UiLabel(m, "typeM_L01", menuAnnounce(0x17a), w, h, 0.26f);
            cr3UiLabel(m, "typeM_L02", menuAnnounce(0x17b), w, h, 0.26f);
            long gold = 0;
            for (const int g : geneMenu.picked) gold += game.cr3Db->genePrice(game.cr3Genes[size_t(g)]);
            cr3UiSlotNumber(m, 1, long(geneMenu.picked.size()), 2, 0, view);
            cr3UiSlotNumber(m, 2, 20, 2, 0, view);
            cr3UiSlotNumber(m, 3, gold, 7, 30, view);
        }
    } else if (geneMenu.base >= 0 && size_t(geneMenu.base) < game.cr3Genes.size()) {
        const cr3::Cr3Gene& base = game.cr3Genes[size_t(geneMenu.base)];
        const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db->geneInfo(base.master);
        const cr3::Cr3OfflineDb::Enhance gain = game.cr3Db->enhanceGain(game, geneMenu.base, geneMenu.picked);
        cr3::Cr3Gene after = base;
        game.cr3Db->addGeneExp(after, gain.exp);
        if (gi && geneMenu.result && cr3UiCall(mp, "call_GeneCard", x, y)) {
            dev->clearDepth();
            drawCr3Card(base.master, x - 0.25f, y - 0.02f, 1.0f, 9.0f, view);
        } else if (gi && cr3UiCall(mp, "call_mixUpMenu", x, y)) {
            UiFig& m = cr3UiPut("mixupmenu", "", 0, x, y, view);
            cr3UiLabel(m, "typeM_L01", menuAnnounce(0x184), w, h, 0.26f);
            cr3UiLabel(m, "typeM_L02", menuAnnounce(0x185), w, h, 0.26f);
            cr3UiLabel(m, "typeM_L03", menuAnnounce(0x186), w, h, 0.26f);
            cr3UiSlotNumber(m, 1, long(geneMenu.picked.size()), 2, 0, view);
            cr3UiSlotNumber(m, 2, 20, 2, 0, view);
            cr3UiSlotNumber(m, 3, gain.exp, 7, 0, view);
            cr3UiSlotNumber(m, 4, gain.gold, 7, 0, view);
            if (cr3UiCall(m, "call_mixStar_fr00", cx, cy)) cr3UiPut("mixstar_fr00", "", float(gi->stars - 1) / 30, cx, cy, view);
            if (cr3UiCall(m, "call_mixMtrPanel", cx, cy)) {
                UiFig& p = cr3UiPut("mixmtrpanel", "", 0, cx, cy, view);
                float ix, iy;
                if (cr3UiCall(p, "call_comListGeneIcon_id00", ix, iy)) {
                    UiFig& icon = uiFig("comlistgeneicon_id00");
                    char pic[16];
                    std::snprintf(pic, sizeof pic, "card_%03d", gi->series);
                    cr3UiMaterial(icon, "mat_card_001", pic);
                    cr3UiPut("comlistgeneicon_id00", "", 0, ix, iy, view);
                }
                if (cr3UiCall(p, "call_btStAtbIcon_fr00", ix, iy)) cr3UiPut("btstatbicon_fr00", "", float(gi->kind) / 30, ix, iy, view);
                if (cr3UiCall(p, "call_comListRare_fr00", ix, iy)) cr3UiPut("comlistrare_fr00", "", float(gi->grade) / 30, ix, iy, view);
                cr3UiLabel(p, "typeM_L00", cr3NameOf("gene_name.mbe", "GeneName", base.master), w, h, 0.26f);
            }
            if (cr3UiCall(m, "call_mixMtrLvwPanel", cx, cy)) {
                UiFig& lv = cr3UiPut("mixmtrlvwpanel", "", 0, cx, cy, view);
                const int now = game.cr3Db->geneLevel(base), then = game.cr3Db->geneLevel(after);
                cr3UiSlotNumber(lv, 1, now, 3, 0, view, now >= gi->maxLevel);
                cr3UiSlotNumber(lv, 2, then, 3, 0, view, then >= gi->maxLevel);
            }
        }
        if (cr3UiCall(mp, "call_mixBtn_fr00", x, y)) {
            UiFig& b = cr3UiPut("mixbtn_fr00", "", geneMenu.result ? 1.0f / 30 : 0.0f, x, y, view);
            float ax, ay, bx, by;
            if (cr3UiCall(b, "tapAreaCP01", ax, ay) && cr3UiCall(b, "tapAreaAP01", bx, by)) geneLabTaps.push_back(circleTap(ax, ay, bx, by, kTapMix));
        }
    }
    UiFig& pos = cr3UiPut("comlistposition", "comlistposition_io", t, 0, 0, view);
    if (!cr3UiCall(pos, "call_comListMenu", x, y, w, h)) return;
    UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
    float lx, ly;
    if (!cr3UiCall(menu, "call_comListPanel", lx, ly)) return;
    const int top = std::clamp(geneMenu.row - 1, 0, std::max(int(geneMenu.pickRows.size()) - 3, 0));
    for (int k = top; k < int(geneMenu.pickRows.size()) && k < top + 3; ++k) {
        const int gk = geneMenu.pickRows[size_t(k)];
        const cr3::Cr3Gene& g = game.cr3Genes[size_t(gk)];
        const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db->geneInfo(g.master);
        if (!gi) continue;
        const float ry = ly - 1.24f * float(k - top);
        UiFig& row = cr3UiPut("comlistlpanel", "", 0, lx, ry, view);
        if (k == geneMenu.row && cr3UiCall(row, "call_comListLCursor", cx, cy)) cr3UiPut("comlistlcursor", "", 0, cx, cy, view);
        if (cr3UiCall(row, "call_comListLMixing", cx, cy)) {
            UiFig& mixing = cr3UiPut("comlistlmixing", "", 0, cx, cy, view);
            drawGeneRowParts(mixing, g.master, game.cr3Db->geneLevel(g), gi->maxLevel, sell ? game.cr3Db->genePrice(g) : -1, view, 1, w, h, sell ? 0 : 1);
            float kx, ky;
            if (std::find(geneMenu.picked.begin(), geneMenu.picked.end(), gk) != geneMenu.picked.end() && cr3UiCall(mixing, "call_comListCheck", kx, ky))
                cr3UiPut("comlistcheck", "", 0, kx, ky, view);
        }
        float ax, ay, bx, by;
        if (cr3UiCall(row, "tapAreaS01", ax, ay) && cr3UiCall(row, "tapAreaE01", bx, by)) geneLabTaps.push_back({ax, ay, bx, by, kTapRow + k});
    }
}

// an alert (AlertwindowMenu: comMsgSMenu at comMsgSPosition, its text at typeM_L05 ..; a tap or
// two seconds - SetAutoEndAlertWindowMenu - and it is gone)
void Viewer::drawGeneAlert(int w, int h, const Mat4& view) {
    dev->clearDepth();
    float x, y;
    UiFig& sp = cr3UiPut("commsgsposition", "", 0, 0, 0, view);
    if (!cr3UiCall(sp, "call_comMsgSMenu", x, y, w, h)) return;
    UiFig& win = cr3UiPut("commsgsmenu", "", 0, x, y, view, std::min(geneMenu.alertSince / 0.13f, 1.0f));
    size_t from = 0;
    for (int line = 5; line <= 7 && from <= geneMenu.alert.size(); ++line) {
        const size_t at = geneMenu.alert.find("\\n", from);
        cr3UiLabel(win, ("typeM_L0" + std::to_string(line)).c_str(), geneMenu.alert.substr(from, at == std::string::npos ? std::string::npos : at - from), w, h, 0.3f);
        if (at == std::string::npos) break;
        from = at + 2;
    }
    geneLabTaps.push_back({-6, -4, 6, 4, kTapAlert});
}

void Viewer::geneLabAlert(int id) {
    audio.playSe("com_005", false);
    geneMenu.alert = menuAnnounce(id), geneMenu.alertSince = 0;
    SDL_Log("gene lab: alert %#x", id);
}

// asked (SetBasicYNChoice2Window: the system window - comMsgSMenu at comMsgSPosition's fit2,
// the question at typeM_L05 .. L07 (SetSystemMessageWindowParameter), AnnounceMenuName 0x175 -
// and YNChoiceButton, interface 0xf: comBtnMenu's buttons from its joint 10 on - slots 005 and
// 006 -, Yes (icon and text 10) and No (11))
void Viewer::drawGeneAsk(int w, int h, const Mat4& view) {
    const float t = geneMenu.since;
    dev->clearDepth();
    float x, y;
    // (the picks asked about: SynthesisCardListMenu - mixOkCardMenu at mixOkCardPositon's
    // call_mixOkCardMenu, the cards at 3.2 x, 1.1 apart from its call_GeneCard - SetCardModelPosition:
    // + the scroll - 6, the scroll 6 to begin with -, five shown)
    if (geneMenu.screen == 8) {
        UiFig& cp = cr3UiPut("mixokcardpositon", "", t, 0, 0, view);
        float gx, gy;
        if (cr3UiCall(cp, "call_mixOkCardMenu", x, y, w, h)) {
            UiFig& cm = cr3UiPut("mixokcardmenu", "", 0, x, y, view);
            if (cr3UiCall(cm, "call_GeneCard", gx, gy)) {
                dev->clearDepth();
                for (size_t k = 0; k < geneMenu.picked.size() && k < 5; ++k)
                    drawCr3Card(game.cr3Genes[size_t(geneMenu.picked[k])].master, gx + 1.1f * float(k), gy, 1.0f, 3.2f, view);
            }
        }
        dev->clearDepth();
    }
    UiFig& sp = cr3UiPut("commsgsposition", "", 0, 0, 0, view);
    if (cr3UiCall(sp, "call_comMsgSMenu", x, y, w, h)) {
        UiFig& win = cr3UiPut("commsgsmenu", "", 0, x, y, view, std::min(t / 0.2f, 1.0f));
        std::string text = geneMenu.message.empty() ? menuAnnounce(geneMenu.screen == 8 ? (geneMenu.mode == 2 ? 0x182 : 0x178) : 0x175) : geneMenu.message;
        if (const size_t at = text.find("{basegene}"); at != std::string::npos && geneMenu.base >= 0 && size_t(geneMenu.base) < game.cr3Genes.size())
            text.replace(at, 10, cr3NameOf("gene_name.mbe", "GeneName", game.cr3Genes[size_t(geneMenu.base)].master));
        dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
        size_t from = 0;
        for (int line = 5; line <= 7 && from <= text.size(); ++line) {
            const size_t at = text.find("\\n", from);
            float tx, ty;
            if (cr3UiCall(win, "typeM_L0" + std::to_string(line), tx, ty))
                drawUiText(text.substr(from, at == std::string::npos ? std::string::npos : at - from), 26, (tx + 0.075f) / 10, (ty - 0.15f) / 10, w, h, true, 0.3f);
            if (at == std::string::npos) break;
            from = at + 2;
        }
    }
    UiFig& pos = cr3UiPut("combtnposition", "", 0, 0, 0, view);
    if (!cr3UiCall(pos, "call_comBtnMenu", x, y, w, h)) return;
    UiFig& menu = cr3UiPut("combtnmenu", "combtnmenu_io", t, x, y, view, 0); // (YNChoiceButton: no arc lines - as the reference)
    for (int b = 0; b < 2; ++b) {
        float px, py, cx, cy;
        if (!cr3UiCall(menu, "call_comBtnPanel_00" + std::to_string(5 + b), px, py)) continue;
        const int id = 10 + b;
        UiFig& panel = cr3UiPut("combtnpanel", "", 0, px, py, view);
        if (cr3UiCall(panel, "call_comBtnBg_fr00", cx, cy)) cr3UiPut("combtnbg_fr00", "", float(b) / 30, cx, cy, view);
        if (cr3UiCall(panel, "call_comBtnIcon_fr00", cx, cy)) cr3UiPut("combtnicon_fr00", "", float(id) / 30, cx, cy, view);
        if (cr3UiCall(panel, "call_comBtnText_fr00", cx, cy)) cr3UiPut(b == geneLabFocus ? "combtntexttap_fr00" : "combtntext_fr00", "", float(id) / 30, cx, cy, view);
        if (cr3UiCall(panel, "call_comBtnR", cx, cy)) cr3UiPut("combtnr", "", 0, cx, cy, view);
        float ax, ay, bx, by;
        if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) geneLabTaps.push_back({ax, ay, bx, by, b == 0 ? kTapYes : kTapNo});
    }
}

float Viewer::geneEffectLength() {
    if (!geneEffectCamLoaded) geneEffectCamLoaded = geneEffectCam.load(archive, "Cam_ue002jene01");
    return geneEffectCamLoaded && geneEffectCam.duration() > 0 ? geneEffectCam.duration() : 4.0f;
}

void Viewer::drawGeneEffect(int w, int h, const cr3::Cr3Gene& made) {
    const float t = geneMenu.since;
    static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
    const Mat4 id = cr3::mat4Identity();
    const float black[4] = {0, 0, 0, 1};
    if (white.id) fullFrameQuad(white, kScreen, black); // (Fld2SetVisible(false))
    dev->clearDepth();
    geneEffectLength();
    cr3::Mat34 cw;
    float fovDeg = 45, zn = 0.1f, zf = 1000;
    if (!geneEffectCamLoaded || !geneEffectCam.at("EV1000", std::min(t, geneMenu.effectEnd), cw, fovDeg, zn, zf)) cw.m[11] = 10;
    const float fov = fovDeg * 3.14159265f / 180;
    dev->setCamera(std::max(zn, 0.01f), zf, fov);
    const Mat4 vp = cr3::mat4Multiply(perspective(fov, float(w) / float(std::max(h, 1)), std::max(zn, 0.01f), zf), cr3::mat4RigidInverse(cr3::mat4FromMat34(cw)));
    cr3::SceneLighting l = lighting;
    l.camera[0] = cw.m[3], l.camera[1] = cw.m[7], l.camera[2] = cw.m[11];
    l.time = time;
    dev->setLighting(l);
    UiFig& stage = uiFig("ue002jene01");
    stage.fig.place = cr3::Mat34();
    poseUi(stage, "ue002jene01", t);
    drawFigure(stage.fig, vp.data());
    UiFig& clerk = uiFig("c115_ue002jene01");
    clerk.fig.place = cr3::Mat34();
    poseUi(clerk, "c115_ue002jene01", t);
    const float clerkEnd = clerk.fig.animLoaded ? clerk.fig.anim.duration() : 0.0f;
    drawFigure(clerk.fig, vp.data());
    // (the lights: their own animation each, at their joints)
    std::vector<std::string> at;
    if (geneMenu.mode == 1) {
        for (int k = 1; k <= std::min(geneMenu.lights, 20); ++k) at.push_back("g_" + std::to_string(k));
        at.push_back("g_d");
    } else
        at = {"g_4", "g_5"};
    for (const std::string& j : at) {
        cr3::Mat34 m;
        if (!cr3JointMatrix(stage, j, m)) continue;
        UiFig& light = uiFig("ue002jene02");
        light.fig.place = m;
        poseUi(light, "ue002jene02", t);
        drawFigure(light.fig, vp.data());
    }
    // (the card: from the clerk's end - 2.4 s, at g_result)
    cr3::Mat34 m;
    const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db ? game.cr3Db->geneInfo(made.master) : nullptr;
    if (gi && t >= clerkEnd - 2.4f && cr3JointMatrix(stage, "g_result", m)) {
        UiFig& u = uiFig("card_default");
        u.fig.place = m;
        u.fig.place.m[3] -= 0.087f; // (SetCardModel: its move offset, (-0.087, 0, 0))
        poseUi(u, "card_default", 0);
        char name[32];
        std::snprintf(name, sizeof name, "card_%03d", gi->series);
        cr3UiMaterial(u, "picture", name);
        std::snprintf(name, sizeof name, "card_rar_%02d", gi->grade);
        cr3UiMaterial(u, "rarity", name);
        std::snprintf(name, sizeof name, "card_att_%02d", gi->kind);
        cr3UiMaterial(u, "attribute", name);
        u.fig.tint[3] = 1;
        drawFigure(u.fig, vp.data());
    }
    cr3::SceneLighting none;
    dev->setLighting(none);
}

void Viewer::drawGeneMerge(int w, int h, const Mat4& view) {
    const float t = geneMenu.since;
    if (geneMenu.made < 0 || size_t(geneMenu.made) >= game.cr3Genes.size()) return;
    const cr3::Cr3Gene& g = game.cr3Genes[size_t(geneMenu.made)];
    if (geneMenu.screen == 5) {
        drawGeneEffect(w, h, g);
        geneLabTaps.push_back({-6, -4, 6, 4, kTapAlert}); // (a tap ends it)
        return;
    }
    dev->clearDepth();
    drawCr3Card(g.master, 0, 0.3f, 1.0f, 9.0f, view, std::min(t / 0.3f, 1.0f));
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db ? game.cr3Db->geneInfo(g.master) : nullptr;
    char lv[32];
    std::snprintf(lv, sizeof lv, "Lv %d", game.cr3Db ? game.cr3Db->geneLevel(g) : 1);
    drawUiText(cr3NameOf("gene_name.mbe", "GeneName", g.master), 30, 0, -2.3f / 10, w, h, false, 1.0f);
    if (gi) drawUiText(lv, 26, 0, -2.7f / 10, w, h, false, 1.0f);
    geneLabTaps.push_back({-4.8f, -3.2f, 4.8f, 3.2f, -1});
}

// a choice in the lab: Talk (the clerk's script's), Combine (its own two - Combine, Enhance),
// Sell; Back a screen back (the lab closed from its buttons). Combine and Sell want more than
// five genes held (CheckSynthesisGeneCount(0): else the alert AnnounceMenuName 0x31e), as does
// each pick (CheckSynthesisGeneCount(picked): five left at least)
void Viewer::geneMenuOpenBase(cr3::EventRuntime& ev, int mode) {
    ev.geneLabClerk(false);
    geneMenu.mode = mode;
    geneMenu.genes.clear();
    for (size_t k = 0; k < game.cr3Genes.size(); ++k)
        if (!game.cr3Genes[k].stocked) geneMenu.genes.push_back(int(k));
    geneMenu.focus = 0;
    geneMenuScreen(2);
    SDL_Log("gene lab: the base gene (%zu cards)", geneMenu.genes.size());
}

void Viewer::geneMenuOpenPicks(int mode) {
    geneMenu.mode = mode;
    geneMenu.picked.clear();
    geneMenu.tab = 0, geneMenu.row = 0, geneMenu.result = false;
    geneMenuPickRows();
    geneMenuScreen(7);
    static const char* kNames[5] = {"combine", "enhance", "sell", "store", "take out"};
    SDL_Log("gene lab: %s - %zu genes", kNames[std::clamp(mode, 0, 4)], geneMenu.pickRows.size());
}

void Viewer::chooseGeneLab(cr3::EventRuntime& ev, int id) {
    if (geneInspectOpen) { geneInspectOpen = false; audio.playSe("com_004", false); return; }
    const int screen = geneMenu.screen;
    if (screen == 5) { // (a tap ends the effect - TouchGestureTap)
        if (id == kTapAlert) geneMenu.screen = 6, geneMenu.since = 0;
        return;
    }
    if (!geneMenu.alert.empty()) { // (an alert takes the tap)
        if (id == kTapAlert || id == -1) geneMenu.alert.clear();
        return;
    }
    if (id == -1) {
        audio.playSe("com_004", false);
        if (screen == 0) {
            ev.geneLabEnd();
            SDL_Log("gene lab: closed");
        } else if (screen == 1 || screen >= 5 && screen <= 6) {
            if (screen >= 5) ev.geneLabClerk(true);
            geneMenuScreen(0);
        } else if (screen == 2) {
            ev.geneLabClerk(true);
            geneMenuScreen(1);
        } else if (screen == 3 || (screen == 7 && geneMenu.mode == 1)) {
            geneMenuScreen(2);
        } else if (screen == 7) {
            ev.geneLabClerk(true);
            geneMenuScreen(geneMenu.mode >= 3 ? 9 : 0);
        } else if (screen == 4) {
            geneMenuScreen(3);
        } else if (screen == 8) {
            geneMenuScreen(geneMenu.mode == 5 ? 9 : 7);
        } else if (screen == 9) {
            geneMenuScreen(0);
        }
        return;
    }
    const int held = geneLabHeld();
    if (screen == 0) {
        if ((id == 28 || id == 21) && held <= 5) return geneLabAlert(0x31e);
        audio.playSe("com_003", false);
        if (id == 17) {
            ev.geneLabTalk();
            SDL_Log("gene lab: talk");
        } else if (id == 28) {
            geneMenuScreen(1);
            SDL_Log("gene lab: combine");
        } else if (id == 21) {
            ev.geneLabClerk(false);
            geneMenu.base = -1;
            geneMenuOpenPicks(2);
        } else if (id == 29) {
            geneMenuScreen(9);
            SDL_Log("gene lab: repository");
        } else
            SDL_Log("gene lab: button %d (its menu not in yet)", id);
        return;
    }
    if (screen == 1) {
        audio.playSe("com_003", false);
        if (id == 30 || id == 31) geneMenuOpenBase(ev, id == 31 ? 1 : 0);
        return;
    }
    if (screen == 9) { // (the repository: Store - five genes kept -, Take Out, Expand)
        if (id == 33 && held <= 5) return geneLabAlert(0x31e);
        audio.playSe("com_003", false);
        geneMenu.base = -1;
        if (id == 33 || id == 34) {
            ev.geneLabClerk(false);
            geneMenuOpenPicks(id == 33 ? 3 : 4);
        } else if (id == 35 && game.cr3Db) { // (AnnounceMenuName 0x17e: {price}, {storehouse_max}, {stocksize})
            geneMenu.mode = 5;
            std::string t = menuAnnounce(0x17e);
            for (const auto& [key, value] : {std::pair<const char*, int>{"{price}", game.cr3Db->constant("GENE_STOCK_PRICE")},
                                             {"{storehouse_max}", game.cr3Db->constant("GENE_STOCK_CAPACITY_ADD")},
                                             {"{stocksize}", game.cr3GeneStock}})
                if (const size_t at = t.find(key); at != std::string::npos) t.replace(at, std::strlen(key), std::to_string(value));
            geneMenuScreen(8);
            geneMenu.message = t;
        }
        return;
    }
    if (screen == 2 && id >= kTapCard && id < kTapRow) {
        const int g = id - kTapCard;
        if (g != geneMenu.focus) {
            geneMenu.focus = g;
            audio.playSe("com_002", false);
            return;
        }
        if (!geneBaseSelectable(game.cr3Genes[size_t(geneMenu.genes[size_t(g)])])) {
            audio.playSe("com_005", false);
            return;
        }
        audio.playSe("com_003", false);
        geneMenu.base = geneMenu.genes[size_t(g)];
        if (geneMenu.mode == 1) return geneMenuOpenPicks(1);
        if (game.cr3Db) game.cr3Db->previewMergeGene(game, geneMenu.base);
        geneMenu.tab = 0, geneMenu.row = 0, geneMenu.result = false;
        geneMenuRows();
        geneMenuScreen(3);
        SDL_Log("gene lab: base %d (gene %d) - %zu merges", geneMenu.base, game.cr3Genes[size_t(geneMenu.base)].master, geneMenu.rows.size());
        return;
    }
    if ((screen == 3 || screen == 7) && id >= kTapTab && id < kTapTab + 4) { // (a radio: the chosen one stays chosen)
        if (geneMenu.tab == id - kTapTab) return;
        audio.playSe("com_002", false);
        geneMenu.tab = id - kTapTab, geneMenu.row = 0;
        if (screen == 3) geneMenuRows();
        else geneMenuPickRows();
        return;
    }
    if ((screen == 3 || screen == 7) && id == kTapMix) {
        audio.playSe("com_003", false);
        geneMenu.result = !geneMenu.result;
        return;
    }
    if (screen == 3 && id >= kTapRow && id < kTapTab) {
        const int r = id - kTapRow;
        if (r != geneMenu.row) {
            geneMenu.row = r;
            audio.playSe("com_002", false);
            return;
        }
        audio.playSe("com_003", false);
        geneMenu.message.clear();
        geneMenuScreen(4);
        return;
    }
    if (screen == 7 && id >= kTapRow && id < kTapTab) { // (SetSynthesisListCheckMark: a tap checks it, or not)
        const int r = id - kTapRow;
        if (r < 0 || r >= int(geneMenu.pickRows.size())) return;
        geneMenu.row = r;
        const int g = geneMenu.pickRows[size_t(r)];
        auto it = std::find(geneMenu.picked.begin(), geneMenu.picked.end(), g);
        if (it != geneMenu.picked.end()) {
            geneMenu.picked.erase(it);
            audio.playSe("com_003", false);
        } else if (geneMenu.mode == 4 || (geneMenu.mode == 3 && int(geneMenu.picked.size()) + 5 < held)) {
            // (the repository's: room where they go - the stock's, the 50)
            const int stocked = int(game.cr3Genes.size()) - held, n = int(geneMenu.picked.size());
            if (geneMenu.mode == 3 ? stocked + n >= game.cr3GeneStock : held + n >= 50) audio.playSe("com_005", false);
            else geneMenu.picked.push_back(g), audio.playSe("com_003", false);
        } else if (geneMenu.picked.size() >= 20 || int(geneMenu.picked.size()) + 5 >= held) {
            if (int(geneMenu.picked.size()) + 5 >= held) geneLabAlert(0x31e);
            else audio.playSe("com_005", false);
        } else {
            geneMenu.picked.push_back(g);
            audio.playSe("com_003", false);
        }
        return;
    }
    if (screen == 7 && id == kTapOk && !geneMenu.picked.empty() && geneMenu.mode >= 3) { // (DBEmu MoveGene, each)
        audio.playSe("com_003", false);
        int moved = 0;
        for (const int g : geneMenu.picked) moved += game.cr3Db && game.cr3Db->moveGene(game, g, geneMenu.mode == 3) == cr3::kDbOk;
        SDL_Log("gene lab: %s %d genes", geneMenu.mode == 3 ? "stored" : "took out", moved);
        geneMenu.picked.clear();
        ev.geneLabClerk(true);
        geneMenuScreen(9);
        return;
    }
    if (screen == 7 && id == kTapOk && !geneMenu.picked.empty()) {
        audio.playSe("com_003", false);
        geneMenu.message.clear();
        geneMenuScreen(8);
        return;
    }
    if ((screen == 4 || screen == 8) && (id == kTapYes || id == kTapNo)) {
        if (id == kTapNo) {
            audio.playSe("com_004", false);
            geneMenu.message.clear();
            geneMenuScreen(geneMenu.mode == 5 ? 9 : screen - 1);
            return;
        }
        audio.playSe("com_003", false);
        if (screen == 8 && geneMenu.mode == 5) { // (DBEmu BuyGeneStock - request 0x5d)
            const int r = game.cr3Db ? game.cr3Db->buyGeneStock(game) : 500;
            SDL_Log("gene lab: repository expanded %d -> %d (coins %d)", r, game.cr3GeneStock, game.cr3Coins);
            geneMenu.message.clear();
            geneMenuScreen(9);
            return;
        }
        if (screen == 8 && geneMenu.mode == 2) { // (StartGeneSellSynthesis: DBEmu SellGenes, then the lab again)
            const int r = game.cr3Db ? game.cr3Db->sellGenes(game, geneMenu.picked) : 500;
            SDL_Log("gene lab: sold %zu -> %d (gold %d)", geneMenu.picked.size(), r, game.cr3Money);
            audio.playSe("com_024", false);
            geneMenu.picked.clear();
            ev.geneLabClerk(true);
            geneMenuScreen(0);
            return;
        }
        if (screen == 8) { // (StartIntensificationSynthesis: DBEmu EnhanceGene)
            const int base = geneMenu.base;
            const int r = game.cr3Db ? game.cr3Db->enhanceGene(game, base, geneMenu.picked) : 500;
            SDL_Log("gene lab: enhance %d with %zu -> %d", base, geneMenu.picked.size(), r);
            if (r != cr3::kDbOk) {
                geneMenuScreen(7);
                return;
            }
            geneMenu.made = base - int(std::count_if(geneMenu.picked.begin(), geneMenu.picked.end(), [base](int g) { return g < base; }));
            geneMenu.lights = int(geneMenu.picked.size());
            geneMenu.picked.clear();
            audio.playSe("com_031", false);
            geneMenuScreen(5);
            geneMenu.effectEnd = geneEffectLength();
            return;
        }
        const int preview = geneMenu.rows[size_t(geneMenu.row)];
        const int r = game.cr3Db ? game.cr3Db->mergeGene(game, preview, false) : 500;
        SDL_Log("gene lab: merge %d -> %d", preview, r);
        if (r != cr3::kDbOk) {
            geneMenuScreen(3);
            return;
        }
        geneMenu.made = int(game.cr3Genes.size()) - 1;
        geneMenu.lights = 2;
        audio.playSe("com_031", false); // (the effect's: SoundPlaySE com_031, stopped at its end)
        geneMenuScreen(5);
        geneMenu.effectEnd = geneEffectLength();
        SDL_Log("gene lab: the effect, %.2f s (the clerk's %.2f)", geneMenu.effectEnd, titleAnimLength("c115_ue002jene01", "c115_ue002jene01"));
        SDL_Log("gene lab: made gene %d", game.cr3Genes.back().master);
    }
}

bool Viewer::geneLabClick(float fx, float fy) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || !ev->geneLab().open || ev->geneLab().talking) return false;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    // (the last drawn on top first: the cards in focus, the question)
    for (auto it = geneLabTaps.rbegin(); it != geneLabTaps.rend(); ++it) {
        const Cr3GeneLabTap& t = *it;
        if (ux >= std::min(t.x0, t.x1) && ux <= std::max(t.x0, t.x1) && uy >= std::min(t.y0, t.y1) && uy <= std::max(t.y0, t.y1)) {
            chooseGeneLab(*ev, t.id);
            return true;
        }
    }
    return true; // (the lab holds the screen)
}

// the keys: up / down the buttons, the cards, the rows; left / right the tabs; Tab the mix
// button; Enter chooses (picks a row; O the picks' OK; Y / N asked), Escape / Backspace goes back
bool Viewer::geneLabKey(SDL_Keycode key) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || !ev->geneLab().open || ev->geneLab().talking) return false;
    if (geneInspectionKey(key)) return true;
    const int screen = geneMenu.screen;
    const bool up = key == SDLK_UP, down = key == SDLK_DOWN, enter = key == SDLK_RETURN || key == SDLK_SPACE;
    if (!geneMenu.alert.empty()) {
        if (enter || key == SDLK_ESCAPE || key == SDLK_BACKSPACE) geneMenu.alert.clear();
        return true;
    }
    if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) {
        chooseGeneLab(*ev, -1);
        return true;
    }
    if (screen <= 1 || screen == 9) {
        const std::vector<int> ids = geneLabButtons();
        const int n = int(ids.size());
        if (up || down) {
            for (int step = 0; step < n; ++step) {
                geneLabFocus = (geneLabFocus + (down ? 1 : n - 1)) % n;
                if (geneLabButtonOn(ids[size_t(geneLabFocus)])) break;
            }
            audio.playSe("com_002", false);
        } else if (enter && geneLabFocus < n && geneLabButtonOn(ids[size_t(geneLabFocus)]))
            chooseGeneLab(*ev, ids[size_t(geneLabFocus)]);
    } else if (screen == 2) {
        if ((up || down) && !geneMenu.genes.empty()) {
            const int f = std::clamp(geneMenu.focus + (down ? 1 : -1), 0, int(geneMenu.genes.size()) - 1);
            if (f != geneMenu.focus) chooseGeneLab(*ev, kTapCard + f);
        } else if (enter && !geneMenu.genes.empty())
            chooseGeneLab(*ev, kTapCard + geneMenu.focus);
    } else if (screen == 3 || screen == 7) {
        if (vitaButtons() && key == SDLK_X) return chooseGeneLab(*ev, kTapTab + (geneMenu.tab + 1) % 4), true;
        if (vitaButtons() && key == SDLK_TAB) {
            int g = -1, master = 0;
            if (screen == 7 && geneMenu.row < int(geneMenu.pickRows.size())) g = geneMenu.pickRows[size_t(geneMenu.row)];
            else if (screen == 3 && game.cr3Db && geneMenu.row < int(geneMenu.rows.size())) {
                const auto& preview = game.cr3Db->mergePreviews()[size_t(geneMenu.rows[size_t(geneMenu.row)])];
                master = preview.result.master;
            }
            if (g >= 0) master = game.cr3Genes[size_t(g)].master;
            if (master) inspectGene(master, g);
            return true;
        }
        if (vitaButtons() && key == SDLK_LEFT && !geneMenu.leftFocus && (screen == 3 || geneMenu.mode == 1))
            return geneMenu.leftFocus = true, geneMenu.materialFocus = 0, true;
        if (vitaButtons() && screen == 7 && geneMenu.mode == 1 && geneMenu.leftFocus) {
            if (key == SDLK_RIGHT && geneMenu.materialFocus == 0) geneMenu.materialFocus = 1, audio.playSe("com_002", false);
            else if (key == SDLK_RIGHT) geneMenu.leftFocus = false;
            else if (key == SDLK_LEFT) geneMenu.materialFocus = 0, audio.playSe("com_002", false);
            else if (enter) chooseGeneLab(*ev, geneMenu.materialFocus == 0 ? kTapMix : kTapOk);
            SDL_Log("gene lab: enhance button %d", geneMenu.materialFocus);
            return true;
        }
        if (vitaButtons() && screen == 3 && geneMenu.leftFocus) {
            if (key == SDLK_RIGHT) geneMenu.leftFocus = false;
            else if (up || down) geneMenu.materialFocus = std::clamp(geneMenu.materialFocus + (down ? 1 : -1), 0, 2), audio.playSe("com_002", false);
            else if (enter && geneMenu.materialFocus == 2 && !geneMenu.rows.empty()) chooseGeneLab(*ev, kTapRow + geneMenu.row);
            SDL_Log("gene lab: material focus %d", geneMenu.materialFocus);
            return true;
        }
        const int n = int(screen == 3 ? geneMenu.rows.size() : geneMenu.pickRows.size());
        if ((up || down) && n > 0) {
            const int r = std::clamp(geneMenu.row + (down ? 1 : -1), 0, n - 1);
            if (r != geneMenu.row) geneMenu.row = r, audio.playSe("com_002", false);
        } else if (key == SDLK_LEFT || key == SDLK_RIGHT)
            chooseGeneLab(*ev, kTapTab + (geneMenu.tab + (key == SDLK_RIGHT ? 1 : 3)) % 4);
        else if (key == SDLK_TAB)
            chooseGeneLab(*ev, kTapMix);
        else if (key == SDLK_O && screen == 7)
            chooseGeneLab(*ev, kTapOk);
        else if (enter && n > 0) {
            if (screen == 7) chooseGeneLab(*ev, kTapRow + geneMenu.row);
            else if (game.cr3Money >= game.cr3Db->mergePreviews()[size_t(geneMenu.rows[size_t(geneMenu.row)])].gold) {
                geneMenu.message.clear();
                audio.playSe("com_003", false);
                geneMenuScreen(4);
            }
        }
    } else if (screen == 4 || screen == 8) {
        if (up || down || key == SDLK_LEFT || key == SDLK_RIGHT) geneLabFocus ^= 1, audio.playSe("com_002", false);
        else if (enter) chooseGeneLab(*ev, geneLabFocus == 0 ? kTapYes : kTapNo);
        else if (key == SDLK_Y) chooseGeneLab(*ev, kTapYes);
        else if (key == SDLK_N) chooseGeneLab(*ev, kTapNo);
    } else if (screen == 6 && enter)
        chooseGeneLab(*ev, -1);
    return true;
}

bool Viewer::townOpen(int pt) const { return game.flag(kTownFlag[pt]); }

std::string Viewer::townPlace(int id) { // (AnnounceMenuName 0x91 + id, utils::FilterMessage over it: the hero's house its {name})
    const std::string s = menuAnnounce(0x91 + id);
    return areaEvents ? areaEvents->filterMessage(s) : s;
}

bool Viewer::townMerged() const { return game.flag(1043) && game.flag(1044); } // (0x28d: the first two one point)

int Viewer::townPointOf(int id) const {
    if (townMerged() && (id == 1 || id == 2)) return 1;
    for (int pt = 0; pt < kTownPoints; ++pt)
        if (kTownId[pt] == id) return pt;
    return -1;
}

// a place's point on the painting, the scroll left out (UI units)
bool Viewer::townPointAt(int pt, float& x, float& y) {
    if (pt < 0 || !kTownJoint[pt]) return false;
    UiFig& u = uiFig("t0100town00");
    poseUi(u, "t0100town00", 0);
    if (!uiJoint(u, kTownJoint[pt], x, y)) return false;
    x *= 2.5929f * 3.8f * 890.0f / 1100.0f, y *= 10.0f * 890.0f / 1100.0f;
    return true;
}

void Viewer::townScrollClamp(float& x, float& y) const {
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float hw = float(screenW) / 2 / k, hh = float(screenH) / 2 / k;
    const float mx = std::max(8.68f - hw, 0.0f);
    x = std::clamp(x, -mx, mx), y = std::clamp(y, std::min(-(7.36f - hh), 0.0f), std::max(6.49f - hh, 0.0f));
}

void Viewer::townAim(int id) { // (Update 4: the focused place's point to (-3, 0))
    float x, y;
    if (!townPointAt(townPointOf(id), x, y)) return;
    town.aimX = -3 - x, town.aimY = -y, town.aiming = true;
    townScrollClamp(town.aimX, town.aimY);
}

void Viewer::openTownMove(bool direct) {
    if (!areaEvents || town.stage) return;
    town = Cr3TownMove();
    town.direct = direct;
    SDL_Log("town map: open%s", direct ? "" : " (a script's)");
    if (areaEvents->start(area.name.substr(0, 3), "TownQuestClearNEXT")) town.stage = 1; // (SM_TownMove 0)
    else townMoveShow();
}

void Viewer::townListOpen(int focus) {
    town.rows.clear(), town.images.clear();
    for (int k = 0; k < kTownPoints; ++k)
        if (townOpen(kTownOrder[k])) town.rows.push_back(kTownOrder[k]), town.images.push_back(kTownImage[k]);
    town.row = std::clamp(focus, 0, std::max(int(town.rows.size()) - 1, 0));
    town.stage = 2, town.since = 0;
    if (!town.rows.empty()) townAim(town.rows[size_t(town.row)] + 1);
}

void Viewer::townMoveShow() {
    town.stage = 2;
    townListOpen(0);
    std::string maps;
    for (int id : town.rows) maps += std::string(" ") + (game.flag(2401) && !game.flag(2500) ? kTownMapAlt : kTownMap)[id + 1];
    SDL_Log("town map: %zu places:%s", town.rows.size(), maps.c_str());
}

void Viewer::townAsk(int id, bool fromList) {
    town.asked = id, town.fromList = fromList;
    town.stage = 4, town.since = 0;
    if (townPointOf(id) >= 0) townAim(id);
    SDL_Log("town map: place %d asked", id);
}

void Viewer::townLeave(int id) { // (Update 8: a 30-frame fade, then two seconds)
    town.asked = id;
    town.stage = 5, town.since = 0;
    if (areaEvents) areaEvents->menuFade(true, 30);
}

void Viewer::townFinish() {
    const int id = town.asked;
    const bool direct = town.direct;
    town = Cr3TownMove();
    if (id > 0 && id < 12) {
        const char* const* maps = game.flag(2401) && !game.flag(2500) ? kTownMapAlt : kTownMap;
        const std::string map = maps[id];
        game.popPoint2.clear(); // (Fld2ClearPopPoint)
        SDL_Log("town map: to %s", map.c_str());
        if (map == "t0203") runAreaFunction("MonorailSound"); // (Fld2Main 4: index 25 - SetNextEvent("MonorailSound"))
        else if (!loadArea(map, "")) SDL_Log("area %s: not entered", map.c_str());
        return;
    }
    SDL_Log("town map: left");
    if (!game.popPoint2.empty()) { // (Fld2Main 5: the pop point, if any)
        const std::string pop = game.popPoint2;
        enterPopPoint(pop);
        return;
    }
    if (areaEvents) areaEvents->menuFade(false, direct ? 10 : 0);
}

void Viewer::chooseTownMove(int id) {
    if (town.stage == 2) {
        if (id == -1) { // (Update 5: SetReturnTownMap - the map alone)
            audio.playSe("com_004", false);
            town.stage = 3, town.since = 0, town.aiming = false;
            return;
        }
        if (id >= kTapTownRow && id < kTapTownRow + kTownPoints) {
            const int r = id - kTapTownRow;
            if (r >= int(town.rows.size())) return;
            if (r != town.row) return audio.playSe("com_002", false), (void)(town.row = r), townAim(town.rows[size_t(r)] + 1);
            audio.playSe("com_003", false);
            townAsk(town.rows[size_t(r)] + 1, true);
        }
        return;
    }
    if (town.stage == 3) {
        if (id == -1) return audio.playSe("com_004", false), townLeave(-1); // (the back button: 8 with none chosen)
        if (id == kTapTownList) return audio.playSe("com_003", false), townListOpen(0); // (spListBtn: Update 3)
        if (id >= kTapTownPoint && id < kTapTownPoint + kTownPoints) {
            int pt = id - kTapTownPoint;
            if (townMerged() && pt == 0) pt = 1;
            audio.playSe("com_003", false);
            if (kTownId[id - kTapTownPoint] < 100) townAsk(kTownId[id - kTapTownPoint], false);
        }
        return;
    }
    if (town.stage == 4) {
        if (id == kTapTownYes) return audio.playSe("com_003", false), townLeave(town.asked);
        if (id == kTapTownNo || id == -1) {
            audio.playSe("com_004", false);
            if (town.fromList) {
                int r = 0;
                for (size_t k = 0; k < town.rows.size(); ++k)
                    if (town.rows[k] + 1 == town.asked) r = int(k);
                townListOpen(r);
            } else town.stage = 3, town.since = 0, town.aiming = false;
            town.asked = -1;
        }
    }
}

// (each frame: the scroll toward its aim, half the way at each of 30 updates a second; the
// script's end; --input town:row<n> / pt<n> / list / back / yes / no)
bool Viewer::stepTownMove(float dt) {
    if (!town.stage) return false;
    town.clock += dt, town.since += dt, town.tick += dt;
    if (town.stage == 1) {
        if (areaEvents && !areaEvents->running()) townMoveShow();
        return true;
    }
    for (; town.tick >= 1.0f / 30; town.tick -= 1.0f / 30)
        if (town.aiming && !town.dragging) {
            town.scrollX += (town.aimX - town.scrollX) / 2, town.scrollY += (town.aimY - town.scrollY) / 2;
            if (std::fabs(town.aimX - town.scrollX) < 0.001f && std::fabs(town.aimY - town.scrollY) < 0.001f) town.aiming = false;
        }
    if (town.stage == 5) {
        if (town.since >= 2.0f) townFinish();
        return true;
    }
    if (!inputScript.empty() && inputScript.front().compare(0, 5, "town:") == 0 && town.since > 0.6f && uiClock >= inputNext) {
        const std::string want = inputScript.front().substr(5);
        inputScript.pop_front();
        inputNext = uiClock + 0.5f;
        if (want == "back") chooseTownMove(-1);
        else if (want == "list") chooseTownMove(kTapTownList);
        else if (want == "yes") chooseTownMove(kTapTownYes);
        else if (want == "no") chooseTownMove(kTapTownNo);
        else if (want.compare(0, 3, "row") == 0) {
            const int r = std::atoi(want.c_str() + 3);
            chooseTownMove(kTapTownRow + r);
            if (town.stage == 2) chooseTownMove(kTapTownRow + r); // (focused, then chosen)
        } else if (want.compare(0, 2, "pt") == 0) chooseTownMove(kTapTownPoint + std::atoi(want.c_str() + 2));
    }
    return true;
}

void Viewer::drawCr3TownMove(int w, int h) {
    townTaps.clear();
    if (town.stage < 2) return;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    // the painting (CustomFigure +0x248: t0100plane)
    if (cr3::Texture* t = texture("t0100plane")) {
        const float half = 5.945f * 344.5f / 118.0f / 2, x0 = town.scrollX - half, x1 = town.scrollX + half, y0 = town.scrollY - half, y1 = town.scrollY + half;
        const float v[24] = {x0, y0, 0, 0, x1, y0, 1, 0, x1, y1, 1, 1, x0, y0, 0, 0, x1, y1, 1, 1, x0, y1, 0, 1};
        const Mat4 proj = uiProjection(w, h, 10);
        const float rgba[4] = {1, 1, 1, 1};
        dev->drawQuad(*t, v, proj.data(), rgba);
        dev->clearDepth();
    }
    std::vector<Cr3GeneLabTap> keep;
    keep.swap(cr3CampTaps);
    float x, y, cx, cy, ax, ay, bx, by;
    const bool map = town.stage == 3;
    const int focus = town.stage == 2 && !town.rows.empty() ? townPointOf(town.rows[size_t(town.row)] + 1)
                      : town.stage == 4                     ? townPointOf(town.asked)
                                                            : -1;
    for (int pt = 0; pt < kTownPoints; ++pt) { // (ProvisionalDraw)
        if (!townOpen(pt) || (townMerged() && pt == 0) || !townPointAt(pt, x, y)) continue;
        x += town.scrollX, y += town.scrollY;
        UiFig& menu = cr3UiPut("sppointmenu", "", 0, x, y, view);
        const bool on = pt == focus;
        float rx = x, ry = y;
        if (cr3UiCall(menu, on ? "call_spPointOn" : "call_spPointOff", cx, cy)) {
            cr3UiPut(on ? "sppointon" : "sppointoff", "", uiClock, cx, cy, view, 1, true);
            rx = cx, ry = cy;
        }
        UiFig& icon = cr3UiPut("spiconmenu", "", 0, rx, ry, view);
        if (cr3UiCall(icon, "call_spIcon_fr", cx, cy)) cr3UiPut("spicon_fr00", "", float(kTownIcon[pt]) / 30, cx, cy, view);
        if ((game.flag(kTownHit[pt]) || game.flag(kTownHit2[pt])) && cr3UiCall(icon, "call_comListHit", cx, cy)) cr3UiPut("comlisthit", "", 0, cx, cy, view);
        if (map) { // (TouchGestureTap: the point's or its icon's)
            if (cr3UiCall(menu, "tapAreaCP01", ax, ay) && cr3UiCall(menu, "tapAreaAP01", bx, by)) cr3CampTaps.push_back(circleTap(ax, ay, bx, by, kTapTownPoint + pt));
            if (cr3UiCall(icon, "tapAreaS01", ax, ay) && cr3UiCall(icon, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, kTapTownPoint + pt});
        }
    }
    UiFig& btnPos = cr3UiPut("spbtnpositon", "", 0, 0, 0, view);
    if (map && cr3UiCall(btnPos, "call_spBtnMenu", x, y, w, h)) { // (spListBtn)
        UiFig& bm = cr3UiPut("spbtnmenu", "", 0, x, y, view);
        if (cr3UiCall(bm, "call_spListBtn", cx, cy)) {
            UiFig& lb = cr3UiPut("splistbtn", "", 0, cx, cy, view);
            if (cr3UiCall(lb, "tapAreaCP01", ax, ay) && cr3UiCall(lb, "tapAreaAP01", bx, by)) cr3CampTaps.push_back(circleTap(ax, ay, bx, by, kTapTownList));
        }
    }
    if (town.stage == 2) {
        std::vector<Cr3PlaceRow> rows;
        for (size_t k = 0; k < town.rows.size(); ++k) {
            const int pt = town.rows[k];
            Cr3PlaceRow r;
            r.image = town.images[k], r.name = townPlace(pt + 1);
            r.hit = game.flag(kTownHit[pt]) || game.flag(kTownHit2[pt]);
            r.fresh = !r.hit && game.flag(kTownNew[pt]);
            rows.push_back(r);
        }
        drawCr3PlaceRows(rows, town.row, town.since, kTapTownRow, 0x0f, w, h, view);
    }
    UiFig& foot = cr3UiPut("comftrposition", "", 0, 0, 0, view);
    if (town.stage != 4 && cr3UiCall(foot, "call_comBackMenu_001", x, y, w, h)) { // (ComBackButton)
        UiFig& menu = cr3UiPut("combackmenu", "", 0, x, y, view);
        if (cr3UiCall(menu, "call_comBack_fr00", cx, cy)) cr3UiPut("comback_fr00", "", 0, cx, cy, view);
        if (cr3UiCall(menu, "tapAreaS01", ax, ay) && cr3UiCall(menu, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, -1});
    }
    dev->clearDepth();
    drawCr3Header(town.clock, 0x12, menuAnnounce(0xa5), w, h, view); // (HeaderMenu 0x42: 0x12, MnlCmp 0xa5)
    if (town.stage == 4) {
        std::string ask = menuAnnounce(0x9f); // (SetSystemMessage: Cr3UtilSPrint - the place for its %s)
        if (const size_t at = ask.find("%s"); at != std::string::npos) ask.replace(at, 2, townPlace(town.asked));
        cr3CampTaps.clear();
        drawCr3Asked(ask, town.since, 0, true, kTapTownYes, kTapTownNo, w, h, view);
    }
    townTaps.swap(cr3CampTaps);
    cr3CampTaps.swap(keep);
}

bool Viewer::townMoveClick(float fx, float fy) {
    if (town.stage < 2) return town.stage != 0;
    if (town.stage == 5) return true;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    for (auto it = townTaps.rbegin(); it != townTaps.rend(); ++it)
        if (ux >= std::min(it->x0, it->x1) && ux <= std::max(it->x0, it->x1) && uy >= std::min(it->y0, it->y1) && uy <= std::max(it->y0, it->y1)) {
            chooseTownMove(it->id);
            return true;
        }
    town.dragging = town.stage == 3; // (the map held: dragged about)
    return true;
}

// a drag (TouchSimpleMove: in the screen's 960 x 640, the plane 344.5 a unit - 118 to a UI unit)
bool Viewer::townMoveDrag(float dx, float dy) {
    if (town.stage != 3 || !town.dragging) return false;
    town.scrollX += dx / 118, town.scrollY -= dy / 118;
    townScrollClamp(town.scrollX, town.scrollY);
    return true;
}

bool Viewer::townMoveKey(SDL_Keycode key) {
    if (town.stage < 2) return town.stage != 0;
    if (town.stage == 5) return true;
    const bool enter = key == SDLK_RETURN || key == SDLK_SPACE, back = key == SDLK_ESCAPE || key == SDLK_BACKSPACE;
    if (town.stage == 2) {
        const int n = int(town.rows.size());
        if ((key == SDLK_UP || key == SDLK_DOWN) && n > 0) chooseTownMove(kTapTownRow + std::clamp(town.row + (key == SDLK_DOWN ? 1 : -1), 0, n - 1));
        else if (enter && n > 0) chooseTownMove(kTapTownRow + town.row);
        else if (back) chooseTownMove(-1);
    } else if (town.stage == 3) {
        const float step = 0.5f;
        if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN) {
            town.scrollX += key == SDLK_LEFT ? step : key == SDLK_RIGHT ? -step : 0;
            town.scrollY += key == SDLK_DOWN ? step : key == SDLK_UP ? -step : 0;
            townScrollClamp(town.scrollX, town.scrollY);
        } else if (key == SDLK_L || enter) chooseTownMove(kTapTownList);
        else if (back) chooseTownMove(-1);
    } else if (town.stage == 4) {
        if (enter || key == SDLK_Y) chooseTownMove(kTapTownYes);
        else if (back || key == SDLK_N) chooseTownMove(kTapTownNo);
    }
    return true;
}

bool Viewer::worldOpen(int k) const { return game.flag(1050 + k); }

std::string Viewer::worldName(int k) { return menuAnnounce(k == 3 && game.flag(6004) ? 0xfa : 0xe9 + k); } // (DAT_014cfbde bit 4)

// the planet's turn (S 2.3 x zoom, then the pitch about X, after the yaw about Y - the game's
// matrices: its Y turn the other way about)
cr3::Mat34 Viewer::worldTurn(float scale, float pitch, float yaw) {
    cr3::Mat34 m;
    const float cp = std::cos(pitch), sp = std::sin(pitch), cy = std::cos(yaw), sy = std::sin(yaw);
    // Rx(pitch) * Ry(-yaw): rows (cy, 0, -sy), (-sp sy, cp, -sp cy), (cp sy, sp, cp cy)
    const float a[9] = {cy, 0, -sy, -sp * sy, cp, -sp * cy, cp * sy, sp, cp * cy};
    for (int row = 0; row < 3; ++row)
        for (int c = 0; c < 3; ++c) m.m[row * 4 + c] = a[row * 3 + c] * scale;
    m.m[3] = m.m[7] = m.m[11] = 0;
    return m;
}

// star01's points (StarNameTable) as posed, the figure's place not over them
bool Viewer::worldPointLocal(const UiFig& star, int k, float* out) {
    char name[16];
    std::snprintf(name, sizeof name, "star01_D%02d", kWorldStar[k]);
    const auto it = std::find(star.joints.begin(), star.joints.end(), name);
    if (it == star.joints.end() || !star.fig.skinned) return false;
    std::vector<cr3::JointTransform> p = star.fig.posed.empty() ? star.fig.skeleton.basePose : star.fig.posed;
    const std::vector<cr3::Mat34> w = star.fig.skeleton.world(p);
    const size_t j = size_t(it - star.joints.begin());
    if (j >= w.size()) return false;
    out[0] = w[j].m[3], out[1] = w[j].m[7], out[2] = w[j].m[11];
    return true;
}

void Viewer::worldApply(const cr3::Mat34& m, const float* p, float* q) {
    for (int r = 0; r < 3; ++r) q[r] = m.m[r * 4] * p[0] + m.m[r * 4 + 1] * p[1] + m.m[r * 4 + 2] * p[2] + m.m[r * 4 + 3];
}

// the planet drawn (its three figures, star01's camera and lights) - the view the points are
// seen through
auto Viewer::drawWorldPlanet(int w, int h) -> Cr3WorldView {
    Cr3WorldView out;
    Cr3WorldSelect& ws = worldSel;
    if (!worldStarTried) {
        worldStarTried = true;
        std::vector<uint8_t> d;
        if (!archive.read("t0100star01.geom", d) || !cr3::parseGeom(d, worldStarGeom)) SDL_Log("world select: no t0100star01");
    }
    UiFig& s0 = uiFig("t0100star00");
    UiFig& s1 = uiFig("t0100star01");
    UiFig& s2 = uiFig("t0100star02");
    if (!s1.fig.skinned || worldStarGeom.cameras.empty()) return out;
    // star01: placed (0, 0.03) and moved by the shift; the earth, its cloud, earth_eff set outright
    poseUi(s1, "t0100star01", 0);
    s1.fig.place = cr3::Mat34();
    s1.fig.place.m[3] = ws.shift, s1.fig.place.m[7] = 0.03f;
    out.planet = worldTurn(2.3f * ws.zoom, ws.pitch, ws.yaw);
    s1.fig.jointWorld.clear();
    cr3::Mat34 eff;
    eff.m[0] = eff.m[5] = eff.m[10] = 2.3f * ws.zoom;
    for (const auto& [name, m] : {std::pair<const char*, cr3::Mat34>{"t0100star01", out.planet}, {"t0100star01_cloud", worldTurn(2.3f * ws.zoom, ws.pitch, ws.cloudYaw)}, {"earth_eff", eff}}) {
        const auto it = std::find(s1.joints.begin(), s1.joints.end(), name);
        if (it != s1.joints.end()) s1.fig.jointWorld.push_back({size_t(it - s1.joints.begin()), m});
    }
    pose(s1.fig, 0, false);
    // its camera: the joint it names, where star01 is
    const cr3::GeomCamera& gc = worldStarGeom.cameras[0];
    const int cj = s1.fig.skeleton.joint(gc.hash);
    if (cj < 0) return out;
    const std::vector<cr3::Mat34> wj = s1.fig.skeleton.world(s1.fig.posed.empty() ? s1.fig.skeleton.basePose : s1.fig.posed, s1.fig.place);
    if (size_t(cj) >= wj.size()) return out;
    const float fov = gc.fovY * 3.14159265f / 180, zn = std::max(gc.near, 0.001f), zf = std::max(gc.far, zn * 2);
    dev->setCamera(zn, zf, fov);
    out.vp = cr3::mat4Multiply(perspective(fov, float(w) / float(std::max(h, 1)), zn, zf), cr3::mat4RigidInverse(cr3::mat4FromMat34(wj[size_t(cj)])));
    out.ok = true;
    cr3::SceneLighting l;
    if (!figureLights("t0100star01", l)) l = cr3::SceneLighting();
    l.camera[0] = wj[size_t(cj)].m[3], l.camera[1] = wj[size_t(cj)].m[7], l.camera[2] = wj[size_t(cj)].m[11];
    l.time = uiClock;
    dev->setLighting(l);
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    dev->clearDepth();
    // star00 at the shift (-0.15 placed, moved 0.15 + the shift); star02 0.28 nearer, 1.5 x the zoom, turning
    if (s0.fig.skinned) {
        poseUi(s0, "t0100star00", 0);
        s0.fig.place = cr3::Mat34();
        if (float(w) > float(h) * 1.55f) s0.fig.place.m[0] = s0.fig.place.m[5] = 1.5f; // (Cr3UtilGetScreenType 2 - not an iPad's 4:3 / 3:2: 1.5 x across and up)
        s0.fig.place.m[3] = ws.shift;
        pose(s0.fig, 0, false);
        drawFigure(s0.fig, out.vp.data());
    }
    if (s2.fig.skinned) {
        poseUi(s2, "t0100star02", 0);
        const float k = 1.5f * ws.zoom, c = std::cos(ws.starsYaw), sn = std::sin(ws.starsYaw);
        s2.fig.place = cr3::Mat34();
        s2.fig.place.m[0] = c * k, s2.fig.place.m[2] = sn * k, s2.fig.place.m[5] = k, s2.fig.place.m[8] = -sn * k, s2.fig.place.m[10] = c * k;
        s2.fig.place.m[3] = ws.shift, s2.fig.place.m[11] = 0.28f;
        pose(s2.fig, 0, false);
        drawFigure(s2.fig, out.vp.data());
    }
    drawFigure(s1.fig, out.vp.data());
    dev->setLighting(cr3::SceneLighting());
    return out;
}

// GetDRotation: the yaw turned until the point faces the camera, in its middle
void Viewer::worldAim(int k) {
    Cr3WorldSelect& ws = worldSel;
    UiFig& s1 = uiFig("t0100star01");
    float p[3], q[3];
    if (!worldPointLocal(s1, k, p)) return;
    for (int n = 0; n < 2000; ++n) {
        worldApply(worldTurn(2.3f, ws.pitch, ws.yaw), p, q);
        if (q[2] < 0) ws.yaw += 1.0f;
        else if (q[0] < 0.01f && q[0] > -0.01f) break;
        else ws.yaw += q[0] / 10;
    }
}

void Viewer::drawCr3WorldSelect(cr3::EventRuntime& ev, int w, int h) {
    worldTaps.clear();
    if (!ev.worldSelect().open) return;
    Cr3WorldSelect& ws = worldSel;
    const Cr3WorldView wv = drawWorldPlanet(w, h);
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    std::vector<Cr3GeneLabTap> keep;
    keep.swap(cr3CampTaps);
    const bool planet = ws.stage == 0;
    float x, y, cx, cy, ax, ay, bx, by;
    // the points (Pose: the facing ones, the nearest last; the last at its button's place)
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float hw = float(screenW) / 2 / k, hh = float(screenH) / 2 / k;
    struct Mark {
        int point;
        float x, y, depth;
    };
    std::vector<Mark> marks;
    UiFig& s1 = uiFig("t0100star01");
    for (int pt = 0; pt < kWorldPoints - 1 && wv.ok; ++pt) {
        float p[3], q[3];
        if (!worldOpen(pt) || !worldPointLocal(s1, pt, p)) continue;
        worldApply(wv.planet, p, q);
        if (q[2] < 0) continue;
        const float* m = wv.vp.data(); // (column-major)
        const float cw = m[3] * q[0] + m[7] * q[1] + m[11] * q[2] + m[15];
        if (cw <= 0) continue;
        const float nx = (m[0] * q[0] + m[4] * q[1] + m[8] * q[2] + m[12]) / cw, ny = (m[1] * q[0] + m[5] * q[1] + m[9] * q[2] + m[13]) / cw;
        marks.push_back({pt, nx * hw, ny * hh, q[2]});
    }
    UiFig& btnPos = cr3UiPut("spbtnpositon", "", 0, 0, 0, view);
    if (planet && worldOpen(kWorldPoints - 1) && cr3UiCall(btnPos, "call_spIconMenu", x, y)) marks.push_back({kWorldPoints - 1, x, y, 1e9f});
    std::stable_sort(marks.begin(), marks.end(), [](const Mark& a, const Mark& b) { return a.depth < b.depth; });
    for (const Mark& mk : marks) {
        UiFig& menu = cr3UiPut("sppointmenu", "", 0, mk.x, mk.y, view);
        const bool on = mk.point == ws.point;
        float rx = mk.x, ry = mk.y;
        if (cr3UiCall(menu, on ? "call_spPointOn" : "call_spPointOff", cx, cy)) {
            cr3UiPut(on ? "sppointon" : "sppointoff", "", uiClock, cx, cy, view, 1, true); // (ChangeAnime(0), SetAnimeLoop(true, 0): its own animation, round)
            rx = cx, ry = cy;
        }
        UiFig& icon = cr3UiPut("spiconmenu", "", 0, rx, ry, view);
        if (cr3UiCall(icon, "call_spIcon_fr", cx, cy)) cr3UiPut("spicon_fr00", "", float(mk.point + 1) / 30, cx, cy, view);
        if (cr3UiCall(icon, "call_spCheckBtn", cx, cy)) {
            UiFig& gift = cr3UiPut("spcheckbtn", "", 0, cx, cy, view);
            if (planet && cr3UiCall(gift, "tapAreaS01", ax, ay) && cr3UiCall(gift, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, kTapWorldGift + mk.point});
        }
        if (game.flag(1080 + mk.point) && cr3UiCall(icon, "call_comListHit", cx, cy)) cr3UiPut("comlisthit", "", 0, cx, cy, view);
        if (planet) { // (the point's or its icon's tap areas)
            for (const UiFig* u : {&menu, &icon})
                if (cr3UiCall(*u, "tapAreaS01", ax, ay) && cr3UiCall(*u, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, kTapWorldPoint + mk.point});
        }
    }
    // the list button (spBtnMenu's spListBtn), the dungeon's name
    if (planet && cr3UiCall(btnPos, "call_spBtnMenu", x, y)) {
        UiFig& bm = cr3UiPut("spbtnmenu", "", 0, x, y, view);
        if (cr3UiCall(bm, "call_spListBtn", cx, cy)) {
            if (vitaButtons()) cr3UiTint[0] = 0.5f, cr3UiTint[1] = 1, cr3UiTint[2] = 1.5f;
            UiFig& lb = cr3UiPut("splistbtn", "", 0, cx, cy, view);
            cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
            if (cr3UiCall(lb, "tapAreaS01", ax, ay) && cr3UiCall(lb, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, kTapWorldList});
        }
    }
    if (!planet && ws.stage != 4 && ws.stage != 5 && ws.point >= 0 && cr3UiCall(btnPos, "call_spMapName", x, y)) { // (Update 3: hidden by the list)
        UiFig& nm = cr3UiPut("spmapname", "", 0, x, y, view);
        float tx, ty;
        if (cr3UiCall(nm, "typeM_L01", tx, ty) || cr3UiCall(nm, "typeS_L01", tx, ty)) {
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            drawUiText(worldName(ws.point), 26, (tx + 0.075f) / 10, (ty - 0.15f) / 10, w, h, true, 1.0f);
        }
    }
    // the levels (CreateListMenu: a comListMenu's rows - the icon, the name)
    if (ws.stage == 1) {
        UiFig& lp = cr3UiPut("comlistposition", "comlistposition_io", ws.since, 0, 0, view);
        float lx, ly;
        if (cr3UiCall(lp, "call_comListMenu", x, y, w, h)) {
            UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
            if (cr3UiCall(menu, "call_comListPanel", lx, ly)) {
                const int n = int(ws.levels.size());
                const int top = ws.row - 2;
                drawCr3ListEnds(lx, ly, n, ws.row, view); // (no bar - as the reference)
                for (int r = std::max(top, 0); r < n && r <= ws.row + 2; ++r) {
                    const int lv = ws.levels[size_t(r)];
                    Cr3FocusRow focusRow(*this, r == ws.row);
                    UiFig& panel = cr3UiPut("comlistpanel", "", 0, cr3ListRowX(lx, r, ws.row), cr3ListRowY(ly, r, top, ws.row), view);
                    if (r == ws.row && cr3UiCall(panel, "call_comListCursor", cx, cy)) cr3UiPut("comlistcursor", "", 0, cx, cy, view);
                    if (cr3UiCall(panel, "call_comListIcon_fr00", cx, cy))
                        cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), kWorldLevelIcon[lv]), cx, cy, view);
                    cr3UiLabel(panel, "typeM_L01", menuAnnounce(0x15 + lv), w, h, 0.3f);
                    if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, kTapWorldLevel + r});
                }
            }
        }
    }
    if (ws.stage == 4) drawWorldList(w, h, view);
    // the back button
    UiFig& foot = cr3UiPut("comftrposition", "", 0, 0, 0, view);
    if ((ws.stage <= 1 || ws.stage == 4) && cr3UiCall(foot, "call_comBackMenu_001", x, y, w, h)) {
        UiFig& menu = cr3UiPut("combackmenu", "", 0, x, y, view);
        if (cr3UiCall(menu, "call_comBack_fr00", cx, cy)) cr3UiPut("comback_fr00", "", 0, cx, cy, view);
        if (cr3UiCall(menu, "tapAreaS01", ax, ay) && cr3UiCall(menu, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, -1});
    }
    if (ws.stage == 4) dev->clearDepth(); // (the header over the list's help - its ends nearer)
    if (ws.stage != 5) drawCr3Header(ws.clock, 0x15, menuAnnounce(ws.stage == 0 || ws.stage == 4 ? 0x128 : ws.stage == 1 ? 0x129 : 0x12a), w, h, view); // (0xd: SetViewHeaderMenu false)
    if (ws.stage == 2) {
        std::string ask = menuAnnounce(0xa4); // (SetSystemMessage: the dungeon's, the level's - Cr3UtilSPrint twice)
        for (const std::string& part : {worldName(ws.point), menuAnnounce(0x15 + ws.level)})
            if (const size_t at = ask.find("%s"); at != std::string::npos) ask.replace(at, 2, part);
        cr3CampTaps.clear();
        drawCr3Asked(ask, ws.since, 0, true, kTapWorldYes, kTapWorldNo, w, h, view);
    }
    if (ws.stage == 5) {
        cr3CampTaps.clear();
        if (collectList.open) drawCr3CollectList(w, h, view);
    }
    worldTaps.swap(cr3CampTaps);
    cr3CampTaps.swap(keep);
}

// The collection (CollectListMenu, from the world select's gift - Update 0xd: Cr3FadeOut 5
// frames, the header hidden, half a second, CreateCollectListMain(World_Dungeon_Tbl[point]) -
// its sheet DAT_00f63ad4[n - 2], its board cltList_d<DAT_00f63f98[sheet]>). mpListPositon slid
// down from 7 over a sixth of a second; mpListMenu at its call_mpListMenu_fit0, its
// mpListBgPanel with mpListBg_id00 (the board) and the close button. The sheet's entries in
// order, each kind to its own places (CollectListMain::Update 1 - their panels are anchors, not
// drawn): an item (below 100000 or 4xxxxx) at call_mpListItemPanel_001 .. - comListIcon its
// icon -, a gift (6xxxxx) at call_mpListGiftPanel_00n - mpListGiftIcon -, a gene at
// call_mpListGenePanel_00n - its card, 2.5 x turned 15 degrees (CollectPanel::SetCardModel) - and
// the whole sheet's prize, once won, at call_mpListGiftCmpPanel (2.9 x); only those won,
// fading in over 0.133 s (Step), dimmed - (0.549, 0.549, 0.655), a card half lit - with none
// held, a number plate (mpListNumPlate, mpListNum_fr00 tens and ones) with more than one. Those
// new since (the entry's +0x34) a second later (Update 3: their mark cleared, com_026), each
// with mpListNew (mpListGetEffe only for those a dungeon just added - none from the world select),
// mpListGetBgPanel's io behind them till then. mpListCollected
// (in, then its loop) with those won (mpListColNumL) of the sheet's (mpListColNumS) - or, all
// won, mpListCmpMenu. A panel tapped: its name over it (HelpSet: mpListHelp, size 22 at
// typeM_C00); the close button (or back): Cr3FadeOut 10 frames, a second, Cr3FadeIn - the planet
void Viewer::drawCr3CollectList(int w, int h, const Mat4& view) {
    Cr3CollectList& cl = collectList;
    if (!game.cr3Db || cl.sheet < 0 || size_t(cl.sheet) >= game.cr3Db->itemSheets().size()) return;
    const cr3::Cr3ItemSheet& sheet = game.cr3Db->itemSheets()[size_t(cl.sheet)];
    const auto pit = game.cr3Sheets.find(sheet.id);
    const cr3::Cr3SheetProgress* prog = pit == game.cr3Sheets.end() ? nullptr : &pit->second;
    const float t = cl.since;
    const bool anyFresh = std::find(cl.fresh.begin(), cl.fresh.end(), true) != cl.fresh.end();
    const float reveal = anyFresh ? 1.0f : 0.0f; // (Update 2: a second, then the new ones)
    float x, y, cx, cy, ax, ay, bx, by;
    // (a figure posed but not drawn: the panels, anchors)
    auto at = [&](const std::string& name, const std::string& anim, float tt, float px, float py) -> UiFig& {
        UiFig& u = uiFig(name);
        u.fig.place = cr3::Mat34();
        u.fig.place.m[3] = px, u.fig.place.m[7] = py;
        poseUi(u, anim.empty() ? name : anim, tt);
        return u;
    };
    const float slide = cl.phase == 0 ? 7.0f * std::max(0.0f, 1.0f - t / 0.1666667f) : cl.scripted ? 7.0f * std::min(cl.closing / 0.1666667f, 1.0f) : 0.0f;
    UiFig& pos = at("mplistpositon", "", 0, 0, slide);
    if (!cr3UiCall(pos, "call_mpListMenu", x, y, w, h)) return;
    UiFig& menu = at("mplistmenu", "", t, x, y); // (its own animation: the board in)
    float px, py;
    if (cr3UiCall(menu, "call_mpListBgPanel", px, py)) {
        UiFig& bg = at("mplistbgpanel", "", 0, px, py);
        if (cr3UiCall(bg, "call_mpListBg_id", cx, cy)) {
            static constexpr int kBoard[14] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16}; // (DAT_00f63f98)
            UiFig& board = uiFig("mplistbg_id00");
            char name[24];
            std::snprintf(name, sizeof name, "cltList_d%02d", kBoard[std::clamp(cl.sheet, 0, 13)]);
            if (!board.materials.empty()) cr3UiMaterial(board, board.materials[0], name);
            cr3UiPut("mplistbg_id00", "", 0, cx, cy, view);
        }
        cr3UiPut("mplistbgpanel", "", 0, px, py, view);
    }
    if (anyFresh && t < reveal + 0.5f && cr3UiCall(menu, "call_mpListGetBgPanel", cx, cy)) // (SetCollectListGetBackEffect)
        cr3UiPut("mplistgetbgpanel", "mplistgetbgpanel_io", t, cx, cy, view);
    // the entries
    int won = 0, slot[3] = {0, 0, 0};
    for (size_t k = 0; k <= sheet.entries.size(); ++k) {
        const bool prize = k == sheet.entries.size();
        int kind, id;
        if (prize) {
            if (!prog || prog->completed < 1) break;
            kind = 3, id = sheet.result.item;
        } else {
            id = sheet.entries[k].item;
            kind = sheet.entries[k].gene ? 2 : id < 100000 || (id >= 400000 && id < 500000) ? 0 : id >= 600000 && id < 700000 ? 1 : -1;
            if (kind < 0) continue;
        }
        const int s = prize ? 0 : slot[kind]++;
        const bool got = prize || (prog && k < prog->opened.size() && prog->opened[k] > 0);
        if (!got) continue;
        if (!prize) ++won;
        const bool fresh = prize ? cl.prizeAt >= 0 : k < cl.fresh.size() && cl.fresh[k];
        const float since = t - (prize ? std::max(cl.prizeAt, 0.0f) : fresh ? reveal : 0.0f);
        if (since < 0) continue;
        static const char* const kPanel[4][2] = {{"call_mpListItemPanel_00", "mplistitempanel"}, {"call_mpListGiftPanel_00", "mplistgiftpanel"},
                                                 {"call_mpListGenePanel_00", "mplistgenepanel"}, {"call_mpListGiftCmpPanel", "mplistgenepanel"}};
        const std::string call = prize ? std::string(kPanel[3][0]) : kPanel[kind][0] + std::to_string(s + 1);
        if (!cr3UiCall(menu, call, px, py)) continue;
        const std::string pn = kPanel[kind][1];
        UiFig& panel = at(pn, pn + "_in", 10, px, py); // (its _in at its end)
        const float alpha = std::min(since / 0.1333333f, 1.0f);
        int held = 0;
        if (kind >= 2) {
            for (const cr3::Cr3Gene& g : game.cr3Genes) held += g.master == id; // (GetGeneSpecificNumber)
        } else if (const auto it = game.cr3Items.find(id); it != game.cr3Items.end()) held = it->second; // (GetCampItemListItemNumbers)
        if (held < 1) cr3UiTint[0] = cr3UiTint[1] = 0.549f, cr3UiTint[2] = 0.655f;
        if (kind == 0 && cr3UiCall(panel, "call_comListIcon", cx, cy)) {
            cr3::MbeFile::Row row;
            const int icon = cr3ItemRow(id, row) ? row.i(3) : 1;
            cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), icon), cx, cy, view, alpha);
        } else if (kind == 1 && cr3UiCall(panel, "call_mpListGiftIcon", cx, cy)) {
            cr3UiPut("mplistgifticon", "", 0, cx, cy, view, alpha);
        } else if (kind >= 2 && cr3UiCall(panel, "call_cradModel", cx, cy)) {
            drawCr3Card(id, cx, cy, 0.01f, kind == 3 ? 2.9f : 2.5f, view, alpha, held > 0, -0.2618f);
        }
        cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
        float nx, ny;
        if (held > 1 && cr3UiCall(panel, "call_mpListNumPlate", cx, cy)) { // (SetGetNumber)
            const int n = std::min(held, 99);
            UiFig& plate = cr3UiPut("mplistnumplate", "", 0, cx, cy, view, alpha);
            if (n > 9 && cr3UiCall(plate, "call_mpListNum_fr00_001", nx, ny)) cr3UiPut("mplistnum_fr00", "", float(n / 10) / 30, nx, ny, view, alpha);
            if (cr3UiCall(plate, "call_mpListNum_fr00_002", nx, ny)) cr3UiPut("mplistnum_fr00", "", float(n % 10) / 30, nx, ny, view, alpha);
        }
        cr3UiZ = 0.02f; // (Pose: the New mark and the help 0.02 nearer - over the card's 0.01)
        if (fresh && cr3UiCall(panel, "call_mpListNew", cx, cy)) { // (SetPanelDataNew: ChangeAnime(0); mpListGetEffe only for those CheckAddItem lists - none from here)
            // (its material's alpha 0 in the file: PartsBase::SetAlpha sets it outright - Step's fade)
            UiFig& nw = at("mplistnew", "", since, cx, cy);
            nw.fig.place.m[11] = cr3UiZ;
            pose(nw.fig, std::fmod(since, std::max(nw.fig.animLoaded ? nw.fig.anim.duration() : 1.0f, 0.01f)), false);
            for (ViewMesh& vm : nw.fig.meshes) {
                std::vector<float>& dc = vm.material.params[0x33];
                if (dc.size() < 4) dc.resize(4, 1.0f);
                dc[3] = alpha;
            }
            nw.fig.tint[0] = nw.fig.tint[1] = nw.fig.tint[2] = nw.fig.tint[3] = 1;
            drawFigure(nw.fig, view.data());
        }
        const int tapId = kTapCollect + kind * 10 + s;
        if (cl.help == tapId && cr3UiCall(panel, "call_mpListHelp", cx, cy)) {
            UiFig& help = cr3UiPut("mplisthelp", "", 0, cx, cy, view);
            float tx, ty;
            const std::string text = kind >= 2 ? cr3NameOf("gene_name.mbe", "GeneName", id) : cr3ItemText(id, false);
            if (cr3UiCall(help, "typeM_C00", tx, ty)) {
                dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                drawUiText(text, 22, tx / 10, (ty - 0.11f) / 10, w, h, false, 1.0f);
            }
        }
        const bool added = !prize && cl.addId >= 0 && sheet.entries[k].item == cl.addId && (cl.addCategory == 1) == sheet.entries[k].gene;
        if (added || (prize && cl.prizeAt >= 0)) { // (SetPanelData's third / ShinePanel: mpListGetEffe, its own animation, at the panel)
            cr3UiPut("mplistgeteffe", "", since, px, py, view, 1);
        }
        cr3UiZ = 0;
        if (cl.phase == 0 && cr3UiCall(panel, "tapAreaS00", ax, ay) && cr3UiCall(panel, "tapAreaE00", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, tapId});
    }
    // the count (SetComplateNumber) or complete, the close button - after the new ones
    const float shown = collectShown();
    if (t >= shown) {
        const int total = int(sheet.entries.size());
        if (won < total) {
            UiFig& bg = uiFig("mplistbgpanel");
            if (cr3UiCall(bg, "call_mpListCollected", cx, cy)) {
                const float ct = t - shown;
                UiFig& col = cr3UiPut("mplistcollected", ct < 0.5f ? "mplistcollected_in" : "mplistcollected_loop", ct < 0.5f ? ct : ct - 0.5f, cx, cy, view, 1, ct >= 0.5f);
                float nx, ny;
                if (won > 9 && cr3UiCall(col, "call_mpListColNumL_fr00_001", nx, ny)) cr3UiPut("mplistcolnuml_fr00", "", float(won / 10) / 30, nx, ny, view);
                if (cr3UiCall(col, "call_mpListColNumL_fr00_002", nx, ny)) cr3UiPut("mplistcolnuml_fr00", "", float(won % 10) / 30, nx, ny, view);
                if (total > 9 && cr3UiCall(col, "call_mpListColNumS_fr00_001", nx, ny)) cr3UiPut("mplistcolnums_fr00", "", float(total / 10) / 30, nx, ny, view);
                if (cr3UiCall(col, "call_mpListColNumS_fr00_002", nx, ny)) cr3UiPut("mplistcolnums_fr00", "", float(total % 10) / 30, nx, ny, view);
            }
        } else if ((cl.cgl == 0 || (cl.cgl == 5 && cl.since >= cl.prizeAt + 1.0f)) && cr3UiCall(pos, "call_mpListCmpMenu", cx, cy, w, h)) {
            const float ct = cl.cgl == 5 ? t - cl.prizeAt - 1.0f : t - shown;
            cr3UiPut("mplistcmpmenu", ct < 0.5f ? "mplistcmpmenu_in" : "mplistcmpmenu_loop", ct < 0.5f ? ct : ct - 0.5f, cx, cy, view, 1, ct >= 0.5f);
        }
        // the prize's first showing (Update 6 - +0x148: com_027, mpListCglMenu in, looping till a
        // tap, out; then the gene's window - SetGeneParameter(prize, 1, .., not owned) -; once it's
        // closed the prize's New mark and com_026; 0xb: a second, then the close button)
        if (cl.cgl >= 1 && cl.cgl <= 3 && cr3UiCall(pos, "call_mpListCglMenu", cx, cy, w, h)) {
            static const char* const kCgl[4] = {"", "mplistcglmenu_in", "mplistcglmenu_loop", "mplistcglmenu_out"};
            cr3UiZ = 1;
            cr3UiPutOverOwn("mplistcglmenu", kCgl[cl.cgl], cl.cglSince, cx, cy, view, cl.cgl == 2);
            cr3UiZ = 0;
        }
        UiFig& bg = uiFig("mplistbgpanel");
        if ((cl.cgl == 0 || (cl.cgl == 5 && cl.since >= cl.prizeAt + 1.0f)) && cr3UiCall(bg, "call_mpListCloseBtn", cx, cy)) {
            UiFig& close = cr3UiPut("mplistclosebtn", "", 0, cx, cy, view);
            if (cl.phase == 0 && cr3UiCall(close, "tapAreaCP01", ax, ay) && cr3UiCall(close, "tapAreaAP01", bx, by)) cr3CampTaps.push_back(circleTap(ax, ay, bx, by, kTapCollectClose));
        }
    }
}

// the scripts' collection (EventRuntime::CollectList): opened over the field, closed - slid
// out over a sixth of a second - then CheckEndCollectList
void Viewer::stepScriptCollect(float dt) {
    cr3::EventRuntime* ev = activeEvents();
    Cr3CollectList& cl = collectList;
    const bool want = ev && ev->collectList().open && !(ev->worldSelect().open);
    if (want && !cl.open) {
        const cr3::EventRuntime::CollectList& c = ev->collectList();
        openCollectList(collectSheetOf(c.dungeon), true, c.category, c.id);
        audio.playSe("com_003", false);
    }
    if (!cl.open || !cl.scripted) return;
    if (!want) return void(cl.open = false);
    stepCollectList(dt);
    if (!inputScript.empty() && inputScript.front() == "collect:close" && cl.phase == 0 && cl.since > 1.0f) // (--input: closed, for tests)
        inputScript.pop_front(), chooseCollectList(-1);
    if (cl.phase == 1 && (cl.closing += dt) >= 0.1666667f) {
        cl.open = false;
        ev->collectListEnd();
    }
}

void Viewer::drawCr3ScriptCollect(int w, int h) {
    collectTaps.clear();
    if (!collectList.open || !collectList.scripted) return;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    dev->setLighting(cr3::SceneLighting());
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    dev->clearDepth();
    std::vector<Cr3GeneLabTap> keep;
    keep.swap(cr3CampTaps);
    drawCr3CollectList(w, h, view);
    collectTaps.swap(cr3CampTaps);
    cr3CampTaps.swap(keep);
}

bool Viewer::collectClick(float fx, float fy) {
    if (!collectList.open || !collectList.scripted) return false;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    for (auto it = collectTaps.rbegin(); it != collectTaps.rend(); ++it)
        if (ux >= std::min(it->x0, it->x1) && ux <= std::max(it->x0, it->x1) && uy >= std::min(it->y0, it->y1) && uy <= std::max(it->y0, it->y1))
            return chooseCollectList(it->id), true;
    chooseCollectList(0);
    return true;
}

// a dungeon point's collection: its sheet (CollectListMain::Update 0: World_Dungeon_Tbl's
// number n, DAT_00f63ad4[n - 2] where 0x1dfff has its bit, else n; past the sheets 0)
int Viewer::worldCollectSheet(int point) const {
    static constexpr int kDungeon[kWorldPoints] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 14, 16}; // (World_Dungeon_Tbl)
    return collectSheetOf(kDungeon[std::clamp(point, 0, kWorldPoints - 1)]);
}

int Viewer::collectSheetOf(int n) const {
    static constexpr int kSheet[17] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 13, 14, 15};
    const int u = n - 2;
    int sheet = u >= 0 && u < 17 && ((0x1dfff >> u) & 1) ? kSheet[u] : n;
    if (!game.cr3Db || size_t(sheet) >= game.cr3Db->itemSheets().size()) sheet = 0;
    return sheet;
}

// the collection's count, close button (and the prize's first showing) from: after the new ones
float Viewer::collectShown() const {
    const std::vector<bool>& f = collectList.fresh;
    return std::find(f.begin(), f.end(), true) != f.end() ? 2.0f : 0.5f;
}

void Viewer::stepCollectList(float dt) {
    Cr3CollectList& cl = collectList;
    cl.since += dt, cl.cglSince += dt;
    if (cl.phase != 0 || !game.cr3Db || size_t(cl.sheet) >= game.cr3Db->itemSheets().size()) return;
    const cr3::Cr3ItemSheet& sheet = game.cr3Db->itemSheets()[size_t(cl.sheet)];
    if (cl.cgl == 0 && cl.since >= collectShown()) {
        const auto it = game.cr3Sheets.find(sheet.id);
        if (it != game.cr3Sheets.end() && it->second.completedNew && it->second.completed > 0) {
            it->second.completedNew = false; // (+0x148 cleared)
            audio.playSe("com_027", false);
            cl.cgl = 1, cl.cglSince = 0;
        }
    }
    if (cl.cgl == 1 && cl.cglSince >= uiAnimSeconds("mplistcglmenu", "mplistcglmenu_in")) cl.cgl = 2, cl.cglSince = 0;
    if (cl.cgl == 3 && cl.cglSince >= uiAnimSeconds("mplistcglmenu", "mplistcglmenu_out") && areaEvents) {
        bool owned = false; // (CheckGeneNotOwn)
        for (const cr3::Cr3Gene& g : game.cr3Genes) owned = owned || g.master == sheet.result.item;
        areaEvents->openGeneGet(sheet.result.item, 1, !owned, true);
        cl.cgl = 4;
        SDL_Log("collection: the prize's window (%d) at %.2f", sheet.result.item, time);
    }
    if (cl.cgl == 4 && (!areaEvents || !areaEvents->geneGet().open)) {
        cl.cgl = 5, cl.prizeAt = cl.since;
        SDL_Log("collection: the prize shown at %.2f", time);
        audio.playSe("com_026", false); // (Update 10: SetPanelDataNew, ShinePanel)
    }
}

float Viewer::uiAnimSeconds(const std::string& fig, const std::string& anim) {
    UiFig& u = uiFig(fig);
    poseUi(u, anim, 0);
    return u.fig.animLoaded ? u.fig.anim.duration() : 0.0f;
}

void Viewer::openCollectList(int sheet, bool scripted, int addCategory, int addId) {
    Cr3CollectList& cl = collectList;
    cl = Cr3CollectList();
    cl.open = true, cl.sheet = sheet;
    cl.scripted = scripted, cl.addCategory = addCategory, cl.addId = addId;
    if (game.cr3Db && size_t(sheet) < game.cr3Db->itemSheets().size()) {
        const auto it = game.cr3Sheets.find(game.cr3Db->itemSheets()[size_t(sheet)].id);
        if (it != game.cr3Sheets.end()) {
            cl.fresh = it->second.mark;
            for (size_t k = 0; k < it->second.mark.size(); ++k) it->second.mark[k] = false; // (Update 3: +0x34 cleared)
        }
    }
    if (std::find(cl.fresh.begin(), cl.fresh.end(), true) != cl.fresh.end()) audio.playSe("com_026", false);
    SDL_Log("collection: sheet %d", sheet);
}

void Viewer::chooseCollectList(int id) {
    Cr3CollectList& cl = collectList;
    if (!cl.open || cl.phase != 0) return;
    if (cl.cgl >= 1 && cl.cgl <= 4) { // (TouchGestureTap's 0x175: the loop out on any tap; nothing else till it's done)
        if (cl.cgl == 2) cl.cgl = 3, cl.cglSince = 0;
        return;
    }
    if (id == kTapCollectClose || id == -1) {
        audio.playSe("com_004", false);
        cl.phase = 1, cl.closing = 0;
        if (areaEvents && !cl.scripted) areaEvents->menuFade(true, 10); // (0xd: Cr3FadeOut 10 frames - from the world select)
        return;
    }
    if (id >= kTapCollect) cl.help = cl.help == id ? -1 : id; // (HelpSet)
    else if (id == 0) cl.help = -1;
}

// a dungeon's item sheet (the k-th, as WorldSelectMenu reads them: DAT_0141dae8 + k * 0x150):
// its entries and those won
void Viewer::worldSheet(int k, int& total, int& won) const {
    total = won = 0;
    if (!game.cr3Db || k < 0 || size_t(k) >= game.cr3Db->itemSheets().size()) return;
    const cr3::Cr3ItemSheet& sheet = game.cr3Db->itemSheets()[size_t(k)];
    total = int(sheet.entries.size());
    const auto it = game.cr3Sheets.find(sheet.id);
    if (it != game.cr3Sheets.end())
        for (const int o : it->second.opened) won += o > 0;
}

void Viewer::drawWorldList(int w, int h, const Mat4& view) {
    Cr3WorldSelect& ws = worldSel;
    std::vector<Cr3PlaceRow> rows;
    for (const int pt : ws.listPoints) {
        Cr3PlaceRow r;
        r.image = pt + 1, r.name = worldName(pt), r.hit = game.flag(1080 + pt);
        worldSheet(pt, r.total, r.won);
        rows.push_back(r);
    }
    drawCr3PlaceRows(rows, ws.listRow, ws.since, kTapWorldRow, 0x11, w, h, view);
}

// (category: the comListCategoryPanel's frame - 0x0f the town map's, 0x11 the world's list)
void Viewer::drawCr3PlaceRows(const std::vector<Cr3PlaceRow>& rows, int f, float since, int tapBase, int category, int w, int h, const Mat4& view) {
    float x, y, lx, ly, cx, cy, ax, ay, bx, by, tx, ty;
    UiFig& lp = cr3UiPut("comlistposition", "comlistposition_io", since, 0, 0, view);
    if (!cr3UiCall(lp, "call_comListMenu", x, y, w, h)) return;
    UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
    if (cr3UiCall(menu, "call_comListCategoryPanel", lx, ly)) {
        UiFig& cat = cr3UiPut("comlistcategorypanel", "", 0, lx, ly, view);
        const float frame = float(category) / 30;
        if (cr3UiCall(cat, "call_comListCBg_fr00", cx, cy)) cr3UiPut("comlistcbg_fr00", "", frame, cx, cy, view);
        if (cr3UiCall(cat, "call_comListCIcon_fr00", cx, cy)) cr3UiPut("comlistcicon_fr00", "", frame, cx, cy, view);
        if (cr3UiCall(cat, "call_comListCtext_fr00", cx, cy)) cr3UiPut("comlistctext_fr00", "", frame, cx, cy, view);
    }
    if (!cr3UiCall(menu, "call_comListPanel", lx, ly)) return;
    const int n = int(rows.size());
    drawCr3ListBar(menu, false, f, n, 1, view);
    // its OneHelpMenu (ReOpen with the list: btHelpMenu at btHelpPosition's call, fading in over
    // a sixth of a second - Step / Pose): the focused row's help, its name (SetListHelpText)
    if (f < n) {
        const float a = std::min(since * 6.0f, 1.0f);
        UiFig& hp = cr3UiPut("bthelpposition", "", 0, 0, 0, view);
        float hx, hy, tx, ty;
        if (cr3UiCall(hp, "call_btHelpMenu", hx, hy, w, h)) {
            UiFig& help = cr3UiPut("bthelpmenu", "", 0, hx, hy, view, a);
            if (cr3UiCall(help, "typeM_L01", tx, ty)) {
                dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                drawUiText(rows[size_t(f)].name, 26, (tx + 0.075f) / 10, (ty - 0.15f) / 10, w, h, true, 1.0f);
            }
        }
    }
    for (int r = std::max(f - 1, 0); r < n && r <= f + 1; ++r) {
        const Cr3PlaceRow& row = rows[size_t(r)];
        const bool on = r == f;
        cr3UiScale = on ? 1.1f : 1.0f;
        cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = on ? 1.0f : 0.5f;
        const float rx = lx - (on ? 0.25f : 0.0f), ry = ly + 1.24f * float(f - r - 1) - (r > f ? 0.124f : 0.0f);
        UiFig& panel = cr3UiPut("comlistimgpanel", "", 0, rx, ry, view);
        if (cr3UiCall(panel, "call_comListImg_fr", cx, cy)) {
            UiFig& img = uiFig("comlistimg_fr00");
            std::vector<cr3::Texture*> keepTex;
            for (const ViewMesh& m : img.fig.meshes) keepTex.push_back(m.texture);
            if (row.image >= 14) cr3UiTexture(img, "sp_list_02");
            cr3UiPut("comlistimg_fr00", "", float(row.image >= 14 ? row.image - 14 : row.image) / 30, cx, cy, view);
            for (size_t m = 0; m < img.fig.meshes.size() && m < keepTex.size(); ++m) img.fig.meshes[m].texture = keepTex[m];
        }
        const int total = row.total, won = row.won;
        const bool all = total <= won;
        if (total >= 0 && cr3UiCall(panel, "call_comListTicket_fr", cx, cy)) {
            UiFig& ticket = cr3UiPut("comlistticket_fr00", "", all ? 0.0f : 1.0f / 30, cx, cy, view);
            float nx, ny;
            if (!all && cr3UiCall(ticket, "call_comListNumC_num", nx, ny)) {
                UiFig& num = cr3UiPut("comlistnumc_num00", "", 0, nx, ny, view);
                const int slot[5] = {won / 10, won % 10, -1, total / 10, total % 10};
                for (int k = 0; k < 5; ++k) {
                    float dx, dy;
                    if ((k == 0 && won < 10) || (k == 3 && total < 10) || !cr3UiCall(num, "call_comListNum_fr00_00" + std::to_string(k + 1), dx, dy)) continue;
                    cr3UiPut("comnum00_fr00", "", float(slot[k] < 0 ? 34 : 10 + slot[k]) / 30, dx, dy, view);
                }
            }
        }
        if (row.hit && cr3UiCall(panel, "call_comListHit", cx, cy)) cr3UiPut("comlisthit", "", 0, cx, cy, view);
        if (row.fresh && cr3UiCall(panel, "call_comListNew", cx, cy)) cr3UiPut("comlistnew", "", 0, cx, cy, view);
        if (cr3UiCall(panel, "typeM_L00", tx, ty)) {
            const float k = cr3UiScale;
            dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
            drawUiText(row.name, 29 * k, (tx + 0.075f * k) / 10, (ty - 0.15f * k) / 10, w, h, true, on ? 1.0f : 0.5f);
        }
        if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, tapBase + r});
        cr3UiScale = 1;
        cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
    }
}

void Viewer::worldListOpen(int focus) {
    Cr3WorldSelect& ws = worldSel;
    ws.listPoints.clear();
    for (int pt = 0; pt < kWorldPoints; ++pt)
        if (worldOpen(pt)) ws.listPoints.push_back(pt);
    ws.listRow = std::clamp(focus, 0, std::max(int(ws.listPoints.size()) - 1, 0));
    ws.point = ws.listPoints.empty() ? -1 : ws.listPoints[size_t(ws.listRow)];
    ws.fromList = false;
    ws.stage = 4, ws.since = 0;
}

// the levels a dungeon lists (the first, then those whose flag 1100 + n is set)
std::vector<int> Viewer::worldLevels() const {
    std::vector<int> out = {0};
    for (int n = 1; n < kWorldPoints; ++n)
        if (game.flag(1100 + n)) out.push_back(n);
    return out;
}

void Viewer::worldLeave(bool chosen) {
    Cr3WorldSelect& ws = worldSel;
    if (areaEvents) areaEvents->menuFade(true, 30); // (Cr3FadeOut 30 frames)
    ws.chosen = chosen;
    ws.stage = 3, ws.since = 0;
}

void Viewer::chooseWorldSelect(cr3::EventRuntime& ev, int id) {
    Cr3WorldSelect& ws = worldSel;
    (void)ev;
    if (ws.stage == 3) return;
    if (ws.stage == 0) {
        if (id == -1) return audio.playSe("com_004", false), worldLeave(false); // (0x11 / 0x13)
        if (id >= kTapWorldPoint && id < kTapWorldPoint + kWorldPoints) {
            audio.playSe("com_003", false);
            ws.point = id - kTapWorldPoint;
            ws.levels = worldLevels(), ws.row = 0;
            ws.fromList = false;
            ws.stage = 1, ws.since = 0;
            SDL_Log("world select: %s", worldName(ws.point).c_str());
        }
        if (id == kTapWorldList) {
            audio.playSe("com_003", false);
            worldListOpen(0);
            SDL_Log("world select: the list (%zu)", ws.listPoints.size());
        }
        if (id >= kTapWorldGift && id < kTapWorldGift + kWorldPoints) { // (0xd: its collection)
            audio.playSe("com_003", false);
            ws.point = id - kTapWorldGift;
            ws.stage = 5, ws.since = 0;
            collectList.open = false;
            if (areaEvents) areaEvents->menuFade(true, 5);
        }
        return;
    }
    if (ws.stage == 5) return chooseCollectList(id);
    if (ws.stage == 4) {
        if (id == -1) { // (Update 5: SetReturnWorldMap)
            audio.playSe("com_004", false);
            ws.point = -1, ws.stage = 0, ws.since = 0;
            return;
        }
        if (id >= kTapWorldRow && id < kTapWorldRow + kWorldPoints) {
            const int r = id - kTapWorldRow;
            if (r >= int(ws.listPoints.size())) return;
            ws.point = ws.listPoints[size_t(r)];
            if (r != ws.listRow) return audio.playSe("com_002", false), (void)(ws.listRow = r);
            audio.playSe("com_003", false);
            ws.levels = worldLevels(), ws.row = 0;
            ws.fromList = true;
            ws.stage = 1, ws.since = 0;
            SDL_Log("world select: %s (the list)", worldName(ws.point).c_str());
        }
        return;
    }
    if (ws.stage == 1) {
        if (id == -1) { // (SetReturnWorldMap: the planet again - from the list, Update 0xb: the list, its focus kept)
            audio.playSe("com_004", false);
            if (ws.fromList) return worldListOpen(ws.listRow);
            ws.point = -1, ws.stage = 0, ws.since = 0;
            return;
        }
        if (id >= kTapWorldLevel && id < kTapWorldLevel + kWorldPoints) {
            const int r = id - kTapWorldLevel;
            if (r >= int(ws.levels.size())) return;
            if (r != ws.row) return audio.playSe("com_002", false), (void)(ws.row = r);
            audio.playSe("com_003", false);
            ws.level = ws.levels[size_t(r)];
            ws.stage = 2, ws.since = 0;
        }
        return;
    }
    if (ws.stage == 2) {
        if (id == kTapWorldYes) return audio.playSe("com_003", false), worldLeave(true);
        if (id == kTapWorldNo || id == -1) { // (0xc: the list closed, the planet - or the dungeons' list)
            audio.playSe("com_004", false);
            if (ws.fromList) return worldListOpen(ws.listRow);
            ws.point = -1, ws.stage = 0, ws.since = 0;
        }
    }
}

bool Viewer::worldSelectClick(float fx, float fy) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || !ev->worldSelect().open) return false;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    for (auto it = worldTaps.rbegin(); it != worldTaps.rend(); ++it)
        if (ux >= std::min(it->x0, it->x1) && ux <= std::max(it->x0, it->x1) && uy >= std::min(it->y0, it->y1) && uy <= std::max(it->y0, it->y1)) {
            chooseWorldSelect(*ev, it->id);
            return true;
        }
    if (worldSel.stage == 5) chooseCollectList(0); // (elsewhere: a tap all the same - the prize's showing)
    worldSel.dragging = worldSel.stage == 0; // (elsewhere: the planet held to turn)
    return true;
}

// a drag (TouchSimpleMove, in the screen's 960 x 640): the yaw a 460th, the pitch too (a quarter
// past its limits), spun on by a 800th
bool Viewer::worldSelectDrag(float dx, float dy) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || !ev->worldSelect().open || !worldSel.dragging) return false;
    Cr3WorldSelect& ws = worldSel;
    ws.yaw -= dx / 460, ws.cloudYaw -= dx / 460;
    const float d = -dy / 460;
    ws.pitch -= ws.pitch > 0.5233334f || ws.pitch < -0.785f ? d * 0.25f : d;
    ws.spin = dx / 800;
    return true;
}

bool Viewer::worldSelectKey(SDL_Keycode key) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || !ev->worldSelect().open) return false;
    Cr3WorldSelect& ws = worldSel;
    const bool enter = key == SDLK_RETURN || key == SDLK_SPACE, back = key == SDLK_ESCAPE || key == SDLK_BACKSPACE;
    if (ws.stage == 0) {
        // (keys: left / right the open dungeons in turn, Enter chooses the one facing)
        std::vector<int> open;
        for (int pt = 0; pt < kWorldPoints; ++pt)
            if (worldOpen(pt)) open.push_back(pt);
        if ((key == SDLK_LEFT || key == SDLK_RIGHT) && !open.empty()) {
            const auto it = std::find(open.begin(), open.end(), ws.row);
            const int at = it == open.end() ? 0 : int(it - open.begin());
            ws.row = open[size_t((at + (key == SDLK_RIGHT ? 1 : int(open.size()) - 1)) % int(open.size()))];
            worldAim(ws.row);
            audio.playSe("com_002", false);
        } else if (enter && vitaButtons()) chooseWorldSelect(*ev, kTapWorldList);
        else if (enter && !open.empty()) chooseWorldSelect(*ev, kTapWorldPoint + (worldOpen(ws.row) ? ws.row : open[0]));
        else if (key == SDLK_L) chooseWorldSelect(*ev, kTapWorldList);
        else if (key == SDLK_G && !open.empty()) chooseWorldSelect(*ev, kTapWorldGift + (worldOpen(ws.row) ? ws.row : open[0]));
        else if (back) chooseWorldSelect(*ev, -1);
        return true;
    }
    if (ws.stage == 1) {
        const int n = int(ws.levels.size());
        if ((key == SDLK_UP || key == SDLK_DOWN) && n > 0) chooseWorldSelect(*ev, kTapWorldLevel + std::clamp(ws.row + (key == SDLK_DOWN ? 1 : -1), 0, n - 1));
        else if (enter && n > 0) chooseWorldSelect(*ev, kTapWorldLevel + ws.row);
        else if (back) chooseWorldSelect(*ev, -1);
        return true;
    }
    if (ws.stage == 5) {
        if (enter || back) chooseCollectList(-1);
        return true;
    }
    if (ws.stage == 4) {
        const int n = int(ws.listPoints.size());
        if ((key == SDLK_UP || key == SDLK_DOWN) && n > 0) chooseWorldSelect(*ev, kTapWorldRow + std::clamp(ws.listRow + (key == SDLK_DOWN ? 1 : -1), 0, n - 1));
        else if (enter && n > 0) chooseWorldSelect(*ev, kTapWorldRow + ws.listRow);
        else if (back) chooseWorldSelect(*ev, -1);
        return true;
    }
    if (ws.stage == 2) {
        if (enter || key == SDLK_Y) chooseWorldSelect(*ev, kTapWorldYes);
        else if (back || key == SDLK_N) chooseWorldSelect(*ev, kTapWorldNo);
    }
    return true;
}

// (each frame: its 30 updates a second - Update - the turns, the zoom and the shift; leaving, two
// seconds after the fade: the dungeon and the level, then the menu done)
void Viewer::stepWorldSelect(float dt) {
    cr3::EventRuntime* ev = areaEvents.get();
    Cr3WorldSelect& ws = worldSel;
    const bool open = ev && ev->worldSelect().open;
    if (open && !ws.wasOpen) {
        ws = Cr3WorldSelect();
        ws.wasOpen = true;
        ev->menuFade(false, 30); // (Update 0x22: Cr3FadeIn 30 frames)
        for (int pt = 0; pt < kWorldPoints - 1; ++pt) // (Initialize: GetDRotation for the first hit's point)
            if (game.flag(1080 + pt)) {
                worldAim(pt);
                ws.row = pt;
                break;
            }
        SDL_Log("world select: open%s", ev->worldSelect().fromPort ? " (from the Space Port)" : "");
    }
    ws.wasOpen = open;
    if (!open) return;
    ws.clock += dt, ws.since += dt, ws.tick += dt;
    for (; ws.tick >= 1.0f / 30; ws.tick -= 1.0f / 30) {
        ws.starsYaw -= 0.001f;
        ws.cloudYaw += 0.0007f;
        if (!ws.dragging) ws.spin *= 0.95f, ws.cloudYaw -= ws.spin, ws.yaw -= ws.spin;
        if (!ws.dragging && ws.stage == 0) { // (the pitch back within its limits)
            if (ws.pitch < -0.785f) ws.pitch = (ws.pitch + 0.785f) * 0.5f - 0.785f;
            if (ws.pitch > 0.5233334f) ws.pitch = (ws.pitch - 0.5233334f) * 0.5f + 0.5233334f;
        }
        if ((ws.stage == 1 || ws.stage == 4) && ws.point >= 0) { // (turned to the point, a third at a time - Update 4 / 7)
            while (ws.yaw > 3.14f || ws.yaw < -3.14f) ws.yaw += ws.yaw >= 0 ? -6.28f : 6.28f;
            const float dp = kWorldPitch[ws.point] - ws.pitch, dy = kWorldYaw[ws.point] - ws.yaw;
            if (dp < -0.001f || dp > 0.001f) ws.pitch += dp / 3;
            if (dy <= -0.001f || dy >= 0.001f) ws.yaw += dy / 3, ws.cloudYaw += dy / 3;
        }
        if (ws.stage == 0) ws.zoom = std::max(ws.zoom - 0.1f, 1.0f), ws.shift = std::max(ws.shift - 0.03f, 0.0f);
        else ws.zoom = std::min(ws.zoom + 0.1f, 1.1f), ws.shift = std::min(ws.shift + 0.03f, 0.15f);
    }
    if (ws.stage == 5) { // (0xf: half a second, then the collection; closed: a second, Cr3FadeIn, the planet)
        Cr3CollectList& cl = collectList;
        if (!cl.open && ws.since >= 0.5f) {
            openCollectList(worldCollectSheet(ws.point));
            ev->menuFade(false, 10);
        } else if (cl.open) {
            stepCollectList(dt);
            if (cl.phase == 1 && (cl.closing += dt) >= 1.0f) {
                cl.open = false;
                ev->menuFade(false, 10);
                ws.point = -1, ws.stage = 0, ws.since = 0;
            }
        }
    }
    if (ws.stage == 3 && ws.since >= 2.0f) {
        game.selectDungeon = ws.chosen ? ws.point + 1 : -1, game.selectLevel = ws.chosen ? ws.level : -1;
        SDL_Log("world select: %s", ws.chosen ? ("dungeon " + std::to_string(game.selectDungeon) + ", level " + std::to_string(game.selectLevel)).c_str() : "back");
        ev->worldSelectEnd(ws.chosen);
    }
}

int Viewer::shopChara() {
    const std::vector<int> members = cr3PartyMembers();
    if (std::find(members.begin(), members.end(), shopMenu.chara) != members.end()) return shopMenu.chara;
    return members.empty() ? 0 : members[0];
}

// (an equipment category - 3 weapons, 4 armour, 5 accessories: GetListCategoryPanelStatus -: its slot)
int Viewer::shopEquipSlot() const {
    const int v = kShopTabs[std::clamp(shopMenu.category, 0, 7)];
    return v >= 3 && v <= 5 ? v - 3 : -1;
}

int Viewer::shopTitle() const {
    if (shopMenu.alter) return 0x1d; // (CreateRemodelingtop: the header's 0x1d)
    const std::string& id = areaEvents ? areaEvents->shop().id : std::string();
    return id == "T03_1" ? 0x1b : id == "T04_1" ? 0x1c : 0x1a;
}

// (a category's value - comListCBtnIcon's frame, as the camp's - to the camp's own index)
int Viewer::shopCampCategory(int value) {
    for (int k = 0; k < 11; ++k)
        if (kItemTabs[k] == value) return k;
    return 0;
}

auto Viewer::shopRows(int value) -> std::vector<Cr3ShopMenu::Row> {
    std::vector<Cr3ShopMenu::Row> out;
    Cr3ShopMenu& m = shopMenu;
    if (!game.cr3Db || !areaEvents) return out;
    const int cat = shopCampCategory(value);
    auto heldOf = [&](int id) { const auto it = game.cr3Items.find(id); return it == game.cr3Items.end() ? 0 : it->second; };
    if (m.alter) { // (CreateReModelingListItem(slot, character): the slot's equipment held or worn that the character can wear)
        const int slot = value - 3, chara = shopChara();
        if (slot < 0 || slot > 2) return out;
        std::set<int> ids;
        for (const auto& [id, n] : game.cr3Items)
            if (id / 100000 == slot + 1 && n > 0) ids.insert(id);
        if (const int w = cr3WornItem(chara, slot)) ids.insert(w);
        for (const int id : ids)
            if (cr3CanWear(chara, id) || id == cr3WornItem(chara, slot)) {
                Cr3ShopMenu::Row r;
                r.item = id, r.held = std::max(heldOf(id), 1), r.unit = 2;
                out.push_back(r);
            }
        return out;
    }
    if (m.buy) {
        const cr3::BsonValue* t = game.cr3Db->table("shops");
        if (!t) return out;
        for (const cr3::BsonValue& shop : t->items) {
            if (shop.string("_id") != areaEvents->shop().id) continue;
            if (const cr3::BsonValue* goods = shop.get("goods"))
                for (const cr3::BsonValue& g : goods->items) {
                    const int flag = g.integer("flag", -1);
                    if (flag >= 0 && !game.flag(flag)) continue; // (GoodsAvailable)
                    Cr3ShopMenu::Row r;
                    r.goods = g.integer("_id");
                    if (const cr3::BsonValue* items = g.get("items"); items && !items->items.empty())
                        r.item = items->items[0].integer("item_id"), r.count = items->items[0].integer("count");
                    if (r.item <= 0 || !cr3ItemInCategory(r.item, cat)) continue;
                    const int coin = g.integer("coin"), gp = g.integer("GP"), gold = g.integer("gold");
                    r.unit = coin > 0 ? 0 : gp > 0 ? 1 : 2, r.price = coin > 0 ? coin : gp > 0 ? gp : gold;
                    r.held = heldOf(r.item);
                    r.affordable = r.price <= shopPurse(r.unit);
                    out.push_back(r);
                }
        }
        return out;
    }
    for (const auto& [id, n] : game.cr3Items) {
        if (id <= 0 || n <= 0 || !cr3ItemInCategory(id, cat) || cr3ItemWearer(id) > 0) continue;
        const int price = game.cr3Db->itemPrice(id);
        if (price <= 0) continue; // (not to be sold)
        Cr3ShopMenu::Row r;
        r.item = id, r.price = price, r.unit = 2, r.held = n;
        out.push_back(r);
    }
    return out;
}

int Viewer::shopPurse(int unit) const { return unit == 0 ? game.cr3Coins : unit == 1 ? game.cr3GP : int(game.cr3Money); }

std::vector<int> Viewer::shopCategories() {
    std::vector<int> out;
    if (shopMenu.alter) return {5, 6, 7}; // (weapons, armour, accessories)
    for (int k = 0; k < 8; ++k)
        if (!shopRows(kShopTabs[k]).empty()) out.push_back(k);
    return out;
}

void Viewer::shopList(int value) {
    Cr3ShopMenu& m = shopMenu;
    const std::vector<int> cats = shopCategories();
    int at = -1;
    for (int k = 0; k < 8; ++k)
        if (kShopTabs[k] == value) at = k;
    m.category = cats.empty() ? 0 : std::find(cats.begin(), cats.end(), at) != cats.end() ? at : cats[0]; // (SearchListCategoryPanelIndex)
    m.rows = shopRows(kShopTabs[m.category]);
    m.row = std::clamp(m.row, 0, std::max(int(m.rows.size()) - 1, 0));
    std::string items;
    for (const Cr3ShopMenu::Row& r : m.rows) items += " " + std::to_string(r.item) + " (" + std::to_string(r.price) + ")";
    SDL_Log("shop: %s list %d (%zu):%s", m.buy ? "buy" : "sell", m.category, m.rows.size(), items.c_str());
}

void Viewer::shopScreen(int screen) { shopMenu.screen = screen, shopMenu.since = 0; }

// (CreateReModelingAttachmentListItem(equipment, slot): the items held whose attachment fits
// the slot - Attachment +0x08's bit; their price the attachment's gold)
auto Viewer::alterRows(int equipment) -> std::vector<Cr3ShopMenu::Row> {
    std::vector<Cr3ShopMenu::Row> out;
    const int slot = equipment / 100000 - 1;
    if (!cr3TablesReady() || slot < 0 || slot > 2) return out;
    for (const auto& [id, n] : game.cr3Items) {
        cr3::Cr3BattleTables::Attachment a;
        if (id <= 0 || id >= 100000 || n <= 0 || !cr3BattleTables.attachmentOf(id, a) || !(a.fits >> slot & 1)) continue;
        Cr3ShopMenu::Row r;
        r.item = id, r.held = n, r.price = a.gold, r.unit = 2, r.affordable = a.gold <= game.cr3Money;
        out.push_back(r);
    }
    return out;
}

// (GetAttachItemPattern: 0 the equipment has none, 1 another, 2 the same - its levels added,
// under 50 -, 3 the same at 50)
int Viewer::alterPattern(int equipment, int item) const {
    const auto it = game.cr3Attachments.find(equipment);
    if (it == game.cr3Attachments.end() || it->second.first < 1 || it->second.second < 1) return 0;
    if (it->second.first != item) return 1;
    return it->second.second > 49 ? 3 : 2;
}

std::string Viewer::alterAsk() {
    const Cr3ShopMenu& m = shopMenu;
    const int item = m.row < int(m.rows.size()) ? m.rows[size_t(m.row)].item : 0;
    std::string ask = menuAnnounce(m.pattern == 0 ? 0x314 : m.pattern == 1 ? 0x315 : 0x169);
    for (const auto& [key, value] : {std::pair<const char*, std::string>{"{itemname}", cr3ItemText(m.equip, false)}, {"{attachname}", cr3ItemText(item, false)}})
        if (const size_t at = ask.find(key); at != std::string::npos) ask.replace(at, std::strlen(key), value);
    return ask;
}

void Viewer::drawCr3Shop(cr3::EventRuntime& ev, int w, int h, bool front) {
    const cr3::EventRuntime::Shop& sh = ev.shop();
    if (!front) shopTaps.clear();
    if (!sh.open || sh.talking) return;
    Cr3ShopMenu& m = shopMenu;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    dev->setLighting(cr3::SceneLighting());
    float x, y, cx, cy, ax, ay, bx, by;
    if (!front) { // (ShopMenuMain: shBgMenu behind the clerk)
        dev->clearDepth();
        UiFig& bg = cr3UiPut("shbgpositon", "", 0, 0, 0, view);
        if (cr3UiCall(bg, "call_shBgMenu", x, y, w, h)) cr3UiPut("shbgmenu", "", 0, x, y, view);
        return;
    }
    dev->clearDepth();
    std::vector<Cr3GeneLabTap> keep;
    keep.swap(cr3CampTaps);
    std::string sentence = menuAnnounce(m.alter ? (m.screen == 0 ? 0x163 : 0x164) : m.screen == 0 ? 0x26e : m.buy ? 0x26f : 0x270);
    if (m.screen >= 2 && m.row < int(m.rows.size())) sentence = cr3ItemText(m.rows[size_t(m.row)].item, true);
    // the buttons
    if (m.screen <= 1) {
        UiFig& pos = cr3UiPut("combtnposition", "", 0, 0, 0, view);
        if (cr3UiCall(pos, "call_comBtnMenu", x, y, w, h)) {
            UiFig& menu = cr3UiPut("combtnmenu", "combtnmenu_io", m.since, x, y, view);
            const int count = m.alter ? (m.screen == 0 ? 2 : 3) : m.screen == 0 ? 3 : 4;
            for (int b = 0; b < count; ++b) {
                float px, py;
                if (!cr3UiCall(menu, "call_comBtnPanel_00" + std::to_string(4 + b), px, py)) continue;
                const int id = m.alter ? (m.screen == 0 ? kAlterTop[b] : kAlterSecond[b]) : m.screen == 0 ? kShopTop[b] : kShopSecond[b];
                UiFig& panel = cr3UiPut("combtnpanel", "", 0, px, py, view);
                if (cr3UiCall(panel, "call_comBtnBg_fr00", cx, cy)) cr3UiPut("combtnbg_fr00", "", float(b) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnIcon_fr00", cx, cy)) cr3UiPut("combtnicon_fr00", "", float(id) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnText_fr00", cx, cy)) cr3UiPut(b == m.focus ? "combtntexttap_fr00" : "combtntext_fr00", "", float(id) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnR", cx, cy)) cr3UiPut("combtnr", "", 0, cx, cy, view);
                if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, m.screen == 0 ? id : kTapShopSecond + b});
            }
        }
    }
    // the list
    if (m.screen >= 2) {
        UiFig& lp = cr3UiPut("comlistposition", "comlistposition_io", m.screen == 2 ? m.since : 1.0f, 0, 0, view);
        if (cr3UiCall(lp, "call_comListMenu", x, y, w, h)) {
            UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
            const std::vector<int> cats = shopCategories();
            std::vector<int> values;
            int focus = 0;
            for (size_t k = 0; k < cats.size(); ++k) {
                if (cats[k] == m.category) focus = int(k);
                values.push_back(kShopTabs[cats[k]]);
            }
            float labelX = 0, labelY = -100, lx, ly;
            if (!values.empty() && m.screen != 6 && !(m.alter && m.screen == 4)) drawCr3Categories(menu, values.data(), int(values.size()), focus, kTapShopCat, labelX, labelY, view);
            if (cr3UiCall(menu, "call_comListPanel", lx, ly)) {
                const int n = int(m.rows.size());
                const int top = m.row - 2;
                drawCr3ListBar(menu, false, m.row, n, 1, view, 0.15f);
                drawCr3ListEnds(lx, ly, n, m.row, view);
                for (int k = std::max(top, 0); k < n && k <= m.row + 2; ++k) {
                    const Cr3ShopMenu::Row& r = m.rows[size_t(k)];
                    Cr3FocusRow focusRow(*this, k == m.row); // (not dimmed: one not affordable told so when chosen)
                    UiFig& panel = cr3UiPut("comlistpanel", "", 0, cr3ListRowX(lx, k, m.row), cr3ListRowY(ly, k, top, m.row), view);
                    if (k == m.row && cr3UiCall(panel, "call_comListCursor", cx, cy)) cr3UiPut("comlistcursor", "", 0, cx, cy, view);
                    cr3::MbeFile::Row ir;
                    if (cr3ItemRow(r.item, ir) && cr3UiCall(panel, "call_comListIcon_fr00", cx, cy))
                        cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), ir.i(3)), cx, cy, view);
                    if (!m.buy && cr3UiCall(panel, "call_comListNumI_num00", cx, cy)) drawCr3ListDigits(cr3UiPut("comlistnumi_num00", "", 0, cx, cy, view), r.held, 1, view);
                    if (!(m.alter && m.screen != 6) && cr3UiCall(panel, "call_comListNumUn_num00", cx, cy)) {
                        UiFig& num = cr3UiPut("comlistnumun_num00", "", 0, cx, cy, view);
                        float ix, iy;
                        if (cr3UiCall(num, "call_comListUnIcon_fr00", ix, iy)) cr3UiPut("comlistunicon_fr00", "", float(r.unit) / 30, ix, iy, view);
                        cr3UiNumber(num, "call_comListNum_fr00", "comlistnum_fr00", r.price, 7, 30, view);
                    }
                    cr3UiLabel(panel, "typeM_L01", cr3ItemText(r.item, false) + (r.count > 1 ? " x" + std::to_string(r.count) : std::string()), w, h, 0.3f);
                    if ((m.screen == 2 || m.screen == 6) && cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, kTapShopRow + k});
                }
            }
            if (labelY > -100 && m.screen != 6 && !(m.alter && m.screen == 4)) {
                dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                drawUiText(menuAnnounce(kShopTabs[m.category] + 0x29), 20, labelX / 10, (labelY - 0.1f) / 10, w, h, false, 1.0f);
            }
        }
    }
    // the Buy / Sell tab (shTabPositon's optSltBtn_fr00: frame 0 Buy on, 1 Sell)
    if (m.screen >= 1 && m.screen <= 2 && !m.alter) {
        UiFig& tp = cr3UiPut("shtabpositon", "shtabpositon_io", m.clock, 0, 0, view);
        if (cr3UiCall(tp, "call_optSltBtn_fr00", x, y, w, h)) {
            UiFig& tab = cr3UiPut("optsltbtn_fr00", "", m.buy ? 0.0f : 1.0f / 30, x + 3.0f, y, view); // (Pose: 3 to the right)
            for (int k = 0; k < 2; ++k) {
                float tx, ty;
                if (cr3UiCall(tab, "typeC_C0" + std::to_string(k + 1), tx, ty)) {
                    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                    drawUiText(menuAnnounce(0x114 + k), 22, tx / 10, (ty - 0.1f) / 10, w, h, false, (k == 0) == m.buy ? 1.0f : 0.55f);
                }
                if (cr3UiCall(tab, "tapAreaS0" + std::to_string(k + 1), ax, ay) && cr3UiCall(tab, "tapAreaE0" + std::to_string(k + 1), bx, by))
                    cr3CampTaps.push_back({ax, ay, bx, by, k == 0 ? kTapShopBuyTab : kTapShopSellTab});
            }
        }
    }
    // (an equipment category's: CampEquip2Menu's status for the footer's character - the row in
    // focus in its slot -, and the characters' footer, CreateCampEquipMenuFutter; else the shop's)
    const int slot = m.screen >= 2 ? shopEquipSlot() : -1;
    if (slot >= 0) {
        const int focusItem = m.row < int(m.rows.size()) ? m.rows[size_t(m.row)].item : 0;
        if (m.alter && m.screen >= 4 && m.screen != 5) { // (SetCampEquipItemAttachment: the equipment with the focused item attached)
            const std::pair<int, int> preview{m.equip, m.screen == 6 || m.screen == 4 ? focusItem : 0};
            drawCr3EquipStatus(shopChara(), slot, m.equip, 1.0f, w, h, view, &preview);
        } else
            drawCr3EquipStatus(shopChara(), slot, m.alter && m.screen == 5 ? m.bought : focusItem, m.screen == 2 ? m.since : 1.0f, w, h, view);
        UiFig& foot = cr3UiPut("comftrposition", "", 0, 0, 0, view);
        if (cr3UiCall(foot, "call_comFtrEquipMenu", x, y, w, h)) {
            UiFig& fm = cr3UiPut("comftrequipmenu", "", 0, x, y, view);
            const std::vector<int> members = cr3PartyMembers();
            const int chara = shopChara();
            for (int k = 0; k < 5 && k < int(members.size()); ++k) {
                if (!cr3UiCall(fm, "call_comFtrCharaBtn_fr00_00" + std::to_string(k + 1), cx, cy)) continue;
                const int ch = members[size_t(k)];
                const int frame = ch <= 5 ? ch - 1 + (ch == chara ? 0 : 5) : ch == chara ? 10 : 11;
                UiFig& b = cr3UiPut("comftrcharabtn_fr00", "", float(frame) / 30, cx, cy, view);
                if (m.screen == 2 && cr3UiCall(b, "tapAreaS01", ax, ay) && cr3UiCall(b, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, kTapShopChara + k}); // (alter: the list rebuilt)
            }
        }
        if (cr3UiCall(foot, "call_comBackMenu_001", x, y, w, h)) {
            UiFig& bm = cr3UiPut("combackmenu", "", 0, x, y, view);
            if (cr3UiCall(bm, "call_comBack_fr00", cx, cy)) cr3UiPut("comback_fr00", "", 0, cx, cy, view);
            if (cr3UiCall(bm, "tapAreaS01", ax, ay) && cr3UiCall(bm, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, -1});
        }
    } else {
        if (m.screen >= 2) { // (EquipOwnMenu: the equipment held - weapons, armour, accessories -, three digits each, comListEquip at comListEqPosition's call)
            UiFig& ep = cr3UiPut("comlisteqposition", "", 0, 0, 0, view);
            if (cr3UiCall(ep, "call_comListEquip", x, y, w, h)) {
                UiFig& eq = cr3UiPut("comlistequip", "", 0, x, y, view);
                long held[3] = {0, 0, 0}; // (CrxGameData::GetWeaponCount, GetArmorCount, GetAccessoryCount)
                for (const auto& [id, n] : game.cr3Items)
                    if (id / 100000 >= 1 && id / 100000 <= 3 && n > 0) held[id / 100000 - 1] += n;
                for (int k = 0; k < 3; ++k)
                    if (cr3UiCall(eq, "call_comListNumL_num00_00" + std::to_string(k + 1), cx, cy))
                        cr3UiDigits(cr3UiPut("comlistnuml_num00", "", 0, cx, cy, view), "call_comListNum_fr00", "comlistnum_fr00", held[k], 3, 0, view);
            }
        }
        drawCr3CampFooter(w, h, view);
    }
    drawCr3Header(m.clock, m.screen == 0 || m.alter ? shopTitle() : m.buy ? 0xd : 0xe, sentence, w, h, view);
    // how many, asked
    const Cr3ShopMenu::Row* r = m.row < int(m.rows.size()) ? &m.rows[size_t(m.row)] : nullptr;
    if ((m.screen == 3 || m.screen == 4) && r) {
        if (m.screen == 3) {
            cr3CampTaps.clear();
            UiFig& pos = cr3UiPut("camitemposition", "camitemposition_io", m.since, 0, 0, view);
            if (cr3UiCall(pos, "ragdoll_call_camItemReelMenu", x, y, w, h)) {
                UiFig& reel = uiFig("camitemreelmenu");
                reel.fig.place = cr3::Mat34();
                reel.fig.place.m[3] = x, reel.fig.place.m[7] = y;
                poseUi(reel, "camitemreelmenu", 0);
                reel.fig.tint[3] = 1;
                const int digits[2] = {m.amount / 10 % 10, m.amount % 10};
                for (int c = 0; c < 2; ++c) {
                    float v = 0.14f * float(2 - digits[c]);
                    while (v > 0.7f) v -= 1.4f;
                    while (v <= -0.7f) v += 1.4f;
                    const std::string a = "mat_num_0" + std::to_string(1 + 2 * c), b = "mat_num_0" + std::to_string(2 + 2 * c);
                    for (ViewMesh& vm : reel.fig.meshes) {
                        const uint32_t mi = vm.source.material;
                        if (mi >= reel.materials.size()) continue;
                        if (reel.materials[mi] == a) vm.material.params[0x5e] = {0.0f, v};
                        if (reel.materials[mi] == b) vm.material.params[0x5e] = {0.13f, v < 0 ? v + 0.7f : v - 0.7f};
                    }
                }
                drawFigure(reel.fig, view.data());
                for (int c = 0; c < 2; ++c) {
                    if (!cr3UiCall(reel, "tapAreaS0" + std::to_string(c + 1), ax, ay) || !cr3UiCall(reel, "tapAreaE0" + std::to_string(c + 1), bx, by)) continue;
                    const float mid = (ay + by) / 2;
                    cr3CampTaps.push_back({ax, ay, bx, mid, kTapShopReel + 2 * c});
                    cr3CampTaps.push_back({ax, mid, bx, by, kTapShopReel + 1 + 2 * c});
                }
            }
            if (cr3UiCall(pos, "ragdoll_call_camItemStockPriceMenu", x, y, w, h)) {
                UiFig& sp = cr3UiPut("camitemstockpricemenu", "", 0, x, y, view);
                if (cr3UiCall(sp, "call_camItemPrice_num00", cx, cy)) // (SetCampItemPriceNumberView: the price for them all)
                    cr3UiDigits(cr3UiPut("camitemprice_num00", "", 0, cx, cy, view), "call_camItemNum_fr00", "camitemnum_fr00", long(r->price) * m.amount, 7, 0, view);
                if (cr3UiCall(sp, "call_camItemStock_num00", cx, cy))
                    cr3UiDigits(cr3UiPut("camitemstock_num00", "", 0, cx, cy, view), "call_camItemNum_fr00", "camitemnum_fr00", r->held, 3, 0, view);
            }
        } else cr3CampTaps.clear();
        std::string ask = m.alter ? alterAsk() : menuAnnounce(m.buy ? 0x248 : 0x210);
        for (const auto& [key, value] : {std::pair<const char*, std::string>{"{itemname}", cr3ItemText(r->item, false)}, {"{currency}", menuAnnounce(0x249 + r->unit)},
                                         {"{num}", std::to_string(m.amount)}, {"{price}", std::to_string(r->price * m.amount)}})
            if (const size_t at = ask.find(key); at != std::string::npos) ask.replace(at, std::strlen(key), value);
        const bool yesOn = m.alter || (m.amount >= 1 && m.amount <= m.most);
        drawCr3Asked(ask, m.since, 0, yesOn, kTapShopYes, kTapShopNo, w, h, view);
    }
    if (m.screen == 5) { // (0x1a: "Equip {itemname}?" 0x20e)
        cr3CampTaps.clear();
        std::string ask = menuAnnounce(0x20e);
        if (const size_t at = ask.find("{itemname}"); at != std::string::npos) ask.replace(at, 10, cr3ItemText(m.bought, false));
        drawCr3Asked(ask, m.since, 0, true, kTapShopYes, kTapShopNo, w, h, view);
    }
    if (!m.told.empty()) {
        cr3CampTaps.clear();
        drawCr3Told(m.told, m.toldSince, kTapShopTold, w, h, view);
    }
    shopTaps.swap(cr3CampTaps);
    cr3CampTaps.swap(keep);
}

void Viewer::chooseShop(cr3::EventRuntime& ev, int id) {
    Cr3ShopMenu& m = shopMenu;
    if (!m.told.empty()) {
        if (id == kTapShopTold || id == -1) m.told.clear();
        return;
    }
    if (m.screen == 2 && id >= kTapShopChara && id < kTapShopChara + 5) { // (GetCampEquipFutterSelectChara)
        const std::vector<int> members = cr3PartyMembers();
        if (id - kTapShopChara < int(members.size()) && members[size_t(id - kTapShopChara)] != shopChara()) {
            m.chara = members[size_t(id - kTapShopChara)], audio.playSe("com_002", false);
            if (m.alter) m.row = 0, shopList(kShopTabs[m.category]); // (CreateReModelingListItem for the character)
        }
        return;
    }
    if (m.screen == 0) {
        if (id == -1) return audio.playSe("com_004", false), ev.shopEnd(); // (0x25: EndCutInMenu, done)
        if (id == 0x11) return audio.playSe("com_003", false), ev.shopTalk();
        if (id == 0x29 && m.alter) return audio.playSe("com_003", false), (void)(m.buy = false), m.focus = 0, shopScreen(1);
        if (id == 0x14 || id == 0x15) return audio.playSe("com_003", false), (void)(m.buy = id == 0x14), m.focus = 0, shopScreen(1);
        return;
    }
    if (id == kTapShopBuyTab || id == kTapShopSellTab) { // (SetBuySellTab: the other)
        if (m.buy == (id == kTapShopBuyTab)) return;
        audio.playSe("com_002", false);
        m.buy = id == kTapShopBuyTab;
        if (m.screen == 2) shopList(kShopTabs[m.category]);
        return;
    }
    if (m.screen == 1) {
        if (id == -1) return audio.playSe("com_004", false), m.focus = m.alter ? 1 : m.buy ? 1 : 2, shopScreen(0);
        if (id >= kTapShopSecond && id < kTapShopSecond + (m.alter ? 3 : 4)) {
            audio.playSe("com_003", false);
            m.row = 0;
            shopList(m.alter ? 3 + (id - kTapShopSecond) : kShopFirst[id - kTapShopSecond]);
            ev.shopClerk(false); // (EndCutInMenu(2))
            shopScreen(2);
        }
        return;
    }
    if (m.screen == 2) {
        if (id == -1) return audio.playSe("com_004", false), ev.shopClerk(true), shopScreen(1); // (0x10: the clerk again)
        if (id >= kTapShopCat && id < kTapShopCat + 8) {
            const std::vector<int> cats = shopCategories();
            const int k = id - kTapShopCat;
            if (k < int(cats.size()) && cats[size_t(k)] != m.category) audio.playSe("com_002", false), m.row = 0, shopList(kShopTabs[cats[size_t(k)]]);
            return;
        }
        if (id >= kTapShopRow && id < kTapShopRow + int(m.rows.size())) {
            const int r = id - kTapShopRow;
            if (r != m.row) return audio.playSe("com_002", false), (void)(m.row = r);
            const Cr3ShopMenu::Row& row = m.rows[size_t(r)];
            if (m.alter) { // (0xe: the items to attach to it)
                std::vector<Cr3ShopMenu::Row> rows = alterRows(row.item);
                if (rows.empty()) return void(audio.playSe("com_004", false));
                audio.playSe("com_003", false);
                m.equip = row.item, m.equipRows = m.rows, m.equipRow = m.row;
                m.rows = rows, m.row = 0;
                SDL_Log("alteration: %d - %zu items to attach", m.equip, m.rows.size());
                return shopScreen(6);
            }
            const bool equip = row.item / 100000 >= 1 && row.item / 100000 <= 3;
            m.most = m.buy ? std::min(99 - row.held, row.price > 0 ? shopPurse(row.unit) / row.price : 99) : row.held;
            if (m.buy && equip) m.most = std::min(m.most, 1);
            if (m.most < 1) { // (GetBuyCanShopListItem: none - 0x233)
                audio.playSe("com_004", false);
                m.told = menuAnnounce(0x233), m.toldSince = 0;
                return;
            }
            audio.playSe("com_003", false);
            m.amount = 1;
            shopScreen(equip ? 4 : 3);
        }
        return;
    }
    if (m.screen == 6) { // (0xf: an item to attach - its pattern asked, or told it's at its most)
        if (id == -1) return audio.playSe("com_004", false), (void)(m.rows = m.equipRows), (void)(m.row = m.equipRow), shopScreen(2);
        if (id >= kTapShopRow && id < kTapShopRow + int(m.rows.size())) {
            const int r = id - kTapShopRow;
            if (r != m.row) return audio.playSe("com_002", false), (void)(m.row = r);
            audio.playSe("com_003", false);
            m.pattern = alterPattern(m.equip, m.rows[size_t(r)].item);
            if (m.pattern == 3) return (void)(m.told = menuAnnounce(0x16a)), (void)(m.toldSince = 0); // (0x16: its levels at the most)
            shopScreen(4);
        }
        return;
    }
    if (m.screen == 4 && m.alter) { // (0x12: yes - SetAttachment, DBSystem::EnforceItem; 0x15: com_024, "Equip it?" if the character doesn't wear it)
        if (id == kTapShopNo || id == -1) return audio.playSe("com_004", false), shopScreen(6);
        if (id != kTapShopYes || m.row >= int(m.rows.size()) || !game.cr3Db || !cr3TablesReady()) return;
        cr3::Cr3BattleTables::Attachment a;
        const int item = m.rows[size_t(m.row)].item;
        if (!cr3BattleTables.attachmentOf(item, a)) return;
        const auto had = game.cr3Attachments.find(m.equip);
        const int levels = std::min(a.levels + (had != game.cr3Attachments.end() && had->second.first == item ? had->second.second : 0), 50);
        const int rc = game.cr3Db->enforceItem(game, m.equip, item, levels, a.gold);
        SDL_Log("alteration: %d on %d, level %d for %d gold -> %d", item, m.equip, levels, a.gold, rc);
        if (rc != cr3::kDbOk) {
            audio.playSe("com_004", false);
            m.told = menuAnnounce(rc == cr3::kDbNoGold ? 0xfe : 0x100), m.toldSince = 0;
            return shopScreen(6);
        }
        audio.playSe("com_024", false);
        const int chara = shopChara(), slot = m.equip / 100000 - 1;
        m.rows = shopRows(kShopTabs[m.category]);
        const auto at = std::find_if(m.rows.begin(), m.rows.end(), [&](const Cr3ShopMenu::Row& r) { return r.item == m.equip; });
        m.row = at != m.rows.end() ? int(at - m.rows.begin()) : 0;
        shopScreen(2);
        if (cr3WornItem(chara, slot) != m.equip && chara > 0 && !(chara == 5 && game.flag(1155))) m.bought = m.equip, shopScreen(5);
        return;
    }
    if (m.screen == 3 || m.screen == 4) {
        if (id >= kTapShopReel && id < kTapShopReel + 4) { // (a reel: tens or ones, up or down)
            const int step = (id - kTapShopReel) / 2 == 0 ? 10 : 1;
            m.amount = std::clamp(m.amount + ((id - kTapShopReel) % 2 == 0 ? step : -step), 0, 99);
            audio.playSe("com_002", false);
            return;
        }
        if (id == kTapShopNo || id == -1) return audio.playSe("com_004", false), shopScreen(2);
        if (id == kTapShopYes && m.amount >= 1 && m.amount <= m.most && m.row < int(m.rows.size()) && game.cr3Db) {
            const Cr3ShopMenu::Row& row = m.rows[size_t(m.row)];
            const int rc = m.buy ? game.cr3Db->buyGoods(game, areaEvents->shop().id.empty() ? -1 : game.cr3Db->findShop(areaEvents->shop().id), row.goods, m.amount)
                                 : game.cr3Db->sellItem(game, row.item, m.amount);
            audio.playSe(rc == cr3::kDbOk ? "com_024" : "com_004", false);
            SDL_Log("shop: %s %d x%d (%d)", m.buy ? "bought" : "sold", row.item, m.amount, rc);
            const int bought = row.item, group = bought / 100000;
            shopList(kShopTabs[m.category]);
            shopScreen(2);
            // (0x1a: equipment bought - "Equip it?" on the list's character, the party's first;
            // not for the fifth while flag 1155 holds)
            const std::vector<int> members = cr3PartyMembers();
            const int chara = shopChara();
            if (m.buy && rc == cr3::kDbOk && group >= 1 && group <= 3 && chara > 0 && !(chara == 5 && game.flag(1155))) m.bought = bought, shopScreen(5);
        }
        return;
    }
    if (m.screen == 5) { // (0x1b: yes - SetEquipParts(the character, the slot, it); either way the list again)
        if (id == kTapShopYes) {
            audio.playSe("com_003", false);
            const std::vector<int> members = cr3PartyMembers();
            const Cr3CampEquip keepEquip = campEquip;
            campEquip.chara = shopChara(), campEquip.slot = m.bought / 100000 - 1;
            campEquipWear(m.bought);
            campEquip = keepEquip;
            shopScreen(2);
        } else if (id == kTapShopNo || id == -1) audio.playSe("com_004", false), shopScreen(2);
    }
}

bool Viewer::cr3ShopClick(float fx, float fy) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || !ev->shop().open || ev->shop().talking) return false;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    for (auto it = shopTaps.rbegin(); it != shopTaps.rend(); ++it)
        if (ux >= std::min(it->x0, it->x1) && ux <= std::max(it->x0, it->x1) && uy >= std::min(it->y0, it->y1) && uy <= std::max(it->y0, it->y1))
            return chooseShop(*ev, it->id), true;
    return true;
}

bool Viewer::cr3ShopKey(SDL_Keycode key) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || !ev->shop().open || ev->shop().talking) return false;
    Cr3ShopMenu& m = shopMenu;
    const bool enter = key == SDLK_RETURN || key == SDLK_SPACE, back = key == SDLK_ESCAPE || key == SDLK_BACKSPACE;
    if (!m.told.empty()) return chooseShop(*ev, kTapShopTold), true;
    if (m.screen <= 1) {
        const int n = m.alter ? (m.screen == 0 ? 2 : 3) : m.screen == 0 ? 3 : 4;
        if (key == SDLK_UP || key == SDLK_DOWN) m.focus = (m.focus + (key == SDLK_DOWN ? 1 : n - 1)) % n, audio.playSe("com_002", false);
        else if (enter) chooseShop(*ev, m.screen == 0 ? (m.alter ? kAlterTop[std::min(m.focus, 1)] : kShopTop[m.focus]) : kTapShopSecond + m.focus);
        else if (back) chooseShop(*ev, -1);
        else if (key == SDLK_TAB && m.screen == 1) chooseShop(*ev, m.buy ? kTapShopSellTab : kTapShopBuyTab);
        return true;
    }
    if (m.screen == 2 || m.screen == 6) {
        const int n = int(m.rows.size());
        if ((key == SDLK_UP || key == SDLK_DOWN) && n > 0) chooseShop(*ev, kTapShopRow + std::clamp(m.row + (key == SDLK_DOWN ? 1 : -1), 0, n - 1));
        else if (m.screen == 6 && (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_TAB)) {}
        else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
            const std::vector<int> cats = shopCategories();
            const auto it = std::find(cats.begin(), cats.end(), m.category);
            const int at = it == cats.end() ? 0 : int(it - cats.begin()), c = int(cats.size());
            if (c > 0) chooseShop(*ev, kTapShopCat + (at + (key == SDLK_RIGHT ? 1 : c - 1)) % c);
        } else if (key == SDLK_TAB) chooseShop(*ev, m.buy ? kTapShopSellTab : kTapShopBuyTab);
        else if (enter && n > 0) chooseShop(*ev, kTapShopRow + m.row);
        else if (back) chooseShop(*ev, -1);
        return true;
    }
    if (m.screen == 5 || (m.screen == 4 && m.alter)) {
        if (enter || key == SDLK_Y) chooseShop(*ev, kTapShopYes);
        else if (back || key == SDLK_N) chooseShop(*ev, kTapShopNo);
        return true;
    }
    if (key == SDLK_UP || key == SDLK_DOWN) chooseShop(*ev, kTapShopReel + 2 + (key == SDLK_DOWN ? 1 : 0));
    else if (key == SDLK_RIGHT || key == SDLK_LEFT) chooseShop(*ev, kTapShopReel + (key == SDLK_LEFT ? 1 : 0));
    else if (enter || key == SDLK_Y) chooseShop(*ev, kTapShopYes);
    else if (back || key == SDLK_N) chooseShop(*ev, kTapShopNo);
    return true;
}

void Viewer::stepShop(float dt) {
    cr3::EventRuntime* ev = areaEvents.get();
    Cr3ShopMenu& m = shopMenu;
    const bool open = ev && ev->shop().open;
    if (open && !m.wasOpen) {
        m = Cr3ShopMenu();
        m.focus = 1, m.alter = ev->shop().alter;
        SDL_Log("shop: %s open", ev->shop().id.c_str());
    }
    if (!open && m.wasOpen) SDL_Log("shop: closed");
    if (open && ev->shop().talking != m.talking) SDL_Log("shop: %s", ev->shop().talking ? "talk" : "talk done");
    m.wasOpen = open;
    if (!open) return;
    m.talking = ev->shop().talking;
    m.clock += dt, m.since += dt, m.toldSince += dt;
}

void Viewer::stepCr3GeneGet(float dt) {
    cr3::EventRuntime* ev = activeEvents();
    Cr3GeneGetView& g = geneGetView;
    const bool open = ev && ev->geneGet().open;
    if (open && !g.wasOpen) {
        g = Cr3GeneGetView();
        if (ev->geneGet().se) audio.playSe("com_017", false);
    }
    g.wasOpen = open;
    if (!open) return;
    g.since += dt;
    if (g.closing && (g.closed += dt) >= kGeneGetIn) ev->geneGetClose(), g.wasOpen = false;
}

bool Viewer::geneGetTap() {
    cr3::EventRuntime* ev = activeEvents();
    Cr3GeneGetView& g = geneGetView;
    if (!ev || !ev->geneGet().open) return false;
    if (!g.closing && g.since >= kGeneGetIn + kGeneGetSpin + kGeneGetNew + kGeneGetWait) g.closing = true, g.closed = 0; // (state 4)
    return true;
}

void Viewer::drawCr3GeneGet(int w, int h) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || !ev->geneGet().open || !game.cr3Db) return;
    const cr3::EventRuntime::GeneGet& gg = ev->geneGet();
    const Cr3GeneGetView& g = geneGetView;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    dev->setLighting(cr3::SceneLighting());
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    dev->clearDepth();
    const float t = g.since;
    float x, y, cx, cy, tx, ty;
    cr3UiAlphaSet = true; // (Update: SetAlpha on its parts)
    // the line
    const float a1 = g.closing ? 1.0f : std::min(t / 0.2f, 1.0f);
    UiFig& pos = cr3UiPut("fegetposition", "", 0, 0, 0, view);
    if (cr3UiCall(pos, "call_feGetMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("fegetmenu", "fegetmenu_in", g.closing ? 0.2f + g.closed : std::min(t, 0.2f), x, y, view);
        if (cr3UiCall(menu, "call_feGetPanel", x, y)) {
            UiFig& panel = cr3UiPut("fegetpanel", "", 0, x, y, view, a1);
            if (cr3UiCall(panel, "call_feGetName_fr", cx, cy)) cr3UiPut("fegetname_fr00", "", 2.0f / 30, cx, cy, view, a1);
            if (cr3UiCall(panel, "call_comListIcon_fr", cx, cy)) cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), 0xad), cx, cy, view, a1);
            if (cr3UiCall(panel, "call_feGetDeco", cx, cy)) cr3UiPut("fegetdeco", "", 0, cx, cy, view, a1);
            std::string line = announceText("AnnounceField", 0x11);
            if (const size_t at = line.find("{item.name}"); at != std::string::npos) line.replace(at, 11, cr3NameOf("gene_name.mbe", "GeneName", gg.gene));
            if (a1 > 0.5f && cr3UiCall(panel, "tyepM_L00", tx, ty)) {
                dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                drawUiText(line, 32, (tx + 0.075f) / 10, (ty - 0.15f) / 10, w, h, true, 1.0f);
            }
        }
    }
    // the card
    dev->clearDepth();
    const float a2 = g.closing ? std::max(kGeneGetIn - g.closed, 0.0f) / 0.6666667f : std::min(t / kGeneGetIn, 1.0f);
    cr3UiZ = 3;
    UiFig& gpos = cr3UiPut("fegetgeneposition", "", 0, 0, 0, view);
    if (cr3UiCall(gpos, "call_feGetGeneMenu", x, y, w, h)) {
        cr3UiZ = 3.6f;
        UiFig& gm = cr3UiPut("fegetgenemenu", "fegetgenemenu_in", g.closing ? std::max(kGeneGetIn - g.closed, 0.0f) : std::min(t, kGeneGetIn), x, y, view);
        if (cr3UiCall(gm, "call_feGetGenePanel", x, y)) {
            cr3UiZ = 2.8f;
            UiFig& gp = cr3UiPut("fegetgenepanel", "", 0, x, y, view, a2);
            const float spin = std::clamp((t - kGeneGetIn) / kGeneGetSpin, 0.0f, 1.0f) * 6.2831853f;
            if (cr3UiCall(gp, "call_cardModel", cx, cy)) drawCr3Card(gg.gene, cx, cy, 3.8f, 8.0f, view, a2, true, -0.3f, spin);
            const float shown = t - kGeneGetIn - kGeneGetSpin; // (state 2 on)
            if (shown >= 0 && gg.isNew && cr3UiCall(gp, "call_feGetNew", cx, cy)) {
                const float na = g.closing ? a2 : shown < kGeneGetNew ? shown / 0.6666667f : 1.0f;
                cr3UiScale = (1 - na) * 0.5f + 1;
                cr3UiZ = 3.9f;
                cr3UiPut("fegetnew", "fegetnew_loop", shown, cx, cy, view, na, true);
                cr3UiScale = 1;
            }
            const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db->geneInfo(gg.gene);
            if (shown >= 0 && gg.level > 1 && cr3UiCall(gp, "call_feGetCardLv_fr", cx, cy)) {
                cr3UiZ = 3.9f;
                UiFig& lv = cr3UiPut("fegetcardlv_fr00", "", 0, cx, cy, view, a2);
                float nx, ny, dx, dy;
                if (cr3UiCall(lv, "call_camEquNumLv_num", nx, ny)) {
                    UiFig& num = cr3UiPut("camequnumlv_num00", "", 0, nx, ny, view, a2);
                    const bool top = gi && gg.level >= gi->maxLevel; // (SetParamNumber: "MAX")
                    const int d[3] = {gg.level % 1000 / 100, gg.level % 100 / 10, gg.level % 10};
                    for (int k = 0; k < 3; ++k) {
                        if (!top && ((k == 0 && gg.level < 100) || (k == 1 && gg.level < 10))) continue;
                        if (cr3UiCall(num, "call_comNum04_fr00_00" + std::to_string(k + 1), dx, dy))
                            cr3UiPut("comnum04_fr00", "", float(top ? 10 + k : d[k]) / 30, dx, dy, view, a2);
                    }
                }
            }
        }
    }
    cr3UiZ = 0;
    cr3UiAlphaSet = false;
}

bool Viewer::spacePortButtonOn(int id) const {
    if (id != 0x13) return true;
    for (int f = 1050; f <= 1063; ++f) // (DAT_014cf970 >> 26, DAT_014cf974's byte)
        if (game.flag(f)) return true;
    return false;
}

void Viewer::spacePortScreen(int screen) { spacePortMenu.screen = screen, spacePortMenu.since = 0; }

// (SpacePortMain 10: the quests listed - none: told; Sub Quests first the first time without main ones)
void Viewer::openSpacePortQuests(bool again) {
    Cr3SpacePortMenu& m = spacePortMenu;
    if (game.cr3Db) game.cr3Db->findQuests(game, std::time(nullptr));
    const size_t main = game.cr3Db ? game.cr3Db->standingQuests().size() : 0, found = game.cr3Db ? game.cr3Db->foundQuests().size() : 0;
    if (main == 0 && found == 0) {
        m.told = menuAnnounce(0x1a8);
        return spacePortScreen(4);
    }
    Cr3CampQuests& cq = campQuests;
    const int category = cq.category, row = again ? cq.row : 0; // (SetCategoryFocus: again - after "no" - at its row)
    cq = Cr3CampQuests();
    cq.category = m.first && main == 0 ? 1 : category;
    cq.row = row;
    m.first = false;
    campQuestRows();
    spacePortScreen(1);
    std::string ids;
    if (game.cr3Db)
        for (int k : cq.rows) { // (the id - the scripts' q<id> -, then its shown number, rank and the goods it asks for)
            const cr3::Cr3OfflineDb::Quest& q = game.cr3Db->quests()[size_t(k)];
            ids += " " + std::to_string(q.id) + " (no. " + std::to_string(q.no) + ", rank " + std::to_string(q.rank) +
                   (q.item > 0 ? ", " + std::to_string(q.itemCount) + " of " + std::to_string(q.item) + ", " + std::to_string(game.cr3Items[q.item]) + " held" : "") + ")";
        }
    SDL_Log("space port: quests (%zu):%s", cq.rows.size(), ids.c_str());
}

void Viewer::spacePortLeave(int questReturn) {
    Cr3SpacePortMenu& m = spacePortMenu;
    if (areaEvents) areaEvents->menuFade(true, 5);
    m.leave = questReturn;
    spacePortScreen(5);
}

bool Viewer::spacePortOkOn(const cr3::Cr3OfflineDb::Quest& q) const {
    const Cr3CampQuests& cq = campQuests;
    if (game.globalWork[24] == q.id || cq.category == 3) return false;
    return cq.category == 2 || questReady(q);
}

void Viewer::drawCr3SpacePort(cr3::EventRuntime& ev, int w, int h) {
    const cr3::EventRuntime::SpacePort& sp = ev.spacePort();
    spacePortTaps.clear();
    if (!sp.open || sp.talk) return;
    Cr3SpacePortMenu& m = spacePortMenu;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    std::vector<Cr3GeneLabTap> keep;
    keep.swap(cr3CampTaps); // (the screen's taps gathered as the camp's, then its own)
    if (m.screen == 1 || m.screen == 6) {
        drawCr3Quests(true, m.screen == 6 ? 1.0f : m.since, w, h, view);
        // Shuffle Quests (comListMenu's comShufflePanel at comListPosition's call_comShufflePanel,
        // 4.4 left and 2.2 up - ViewShuffleButtonListMenu: on Sub Quests with fewer than ten)
        const Cr3CampQuests& sq = campQuests;
        float sx, sy;
        if (!sq.detail && sq.category == 1 && sq.rows.size() < 10 && cr3UiCall(uiFig("comlistposition"), "call_comShufflePanel", sx, sy, w, h)) {
            UiFig& sh = cr3UiPut("comshufflepanel", "", 0, sx - 4.4f, sy + 2.2f, view);
            float ax, ay, bx, by;
            if (m.screen == 1 && cr3UiCall(sh, "tapAreaS01", ax, ay) && cr3UiCall(sh, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, kTapPortShuffle});
        }
        const auto* qs = game.cr3Db ? &game.cr3Db->quests() : nullptr;
        const Cr3CampQuests& cq = campQuests;
        if (cq.detail && qs && cq.row < int(cq.rows.size()) && !spacePortOkOn((*qs)[size_t(cq.rows[size_t(cq.row)])])) // (its OK off)
            cr3CampTaps.erase(std::remove_if(cr3CampTaps.begin(), cr3CampTaps.end(), [](const Cr3GeneLabTap& t) { return t.id == 500; }), cr3CampTaps.end());
    } else if (m.screen == 2) { // (0xf: the detail gone, the header and footer under the question)
        drawCr3Header(m.clock, 0x14, {}, w, h, view);
        drawCr3CampFooter(w, h, view);
    } else if (m.screen != 5) {
        drawCr3Header(m.clock, 0x13, menuAnnounce(0x26e), w, h, view);
        float x, y;
        UiFig& pos = cr3UiPut("combtnposition", "", 0, 0, 0, view);
        if (m.screen == 0 && cr3UiCall(pos, "call_comBtnMenu", x, y, w, h)) {
            UiFig& menu = cr3UiPut("combtnmenu", "combtnmenu_io", m.since, x, y, view);
            for (int b = 0; b < 3; ++b) {
                float px, py, cx, cy;
                if (!cr3UiCall(menu, "call_comBtnPanel_00" + std::to_string(4 + b), px, py)) continue;
                const int id = kSpacePortButtons[b];
                const bool off = !spacePortButtonOn(id);
                if (off) cr3UiTint[0] = cr3UiTint[1] = 0.549f, cr3UiTint[2] = 0.655f;
                UiFig& panel = cr3UiPut("combtnpanel", "", 0, px, py, view);
                if (cr3UiCall(panel, "call_comBtnBg_fr00", cx, cy)) cr3UiPut("combtnbg_fr00", "", float(b) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnIcon_fr00", cx, cy)) cr3UiPut("combtnicon_fr00", "", float(id) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnText_fr00", cx, cy)) cr3UiPut(b == m.focus ? "combtntexttap_fr00" : "combtntext_fr00", "", float(id) / 30, cx, cy, view);
                if (cr3UiCall(panel, "call_comBtnR", cx, cy)) cr3UiPut("combtnr", "", 0, cx, cy, view);
                cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
                float ax, ay, bx, by;
                if (!off && cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, id});
            }
        }
        drawCr3CampFooter(w, h, view);
    }
    if (m.screen == 2 || m.screen == 3 || m.screen == 6) {
        std::string ask = menuAnnounce(m.screen == 2 ? 0x124 : m.screen == 3 ? 0x125 : 0x313);
        if (m.screen == 2) { // (Cr3UtilSPrint: the quest's title for its %s)
            const auto* qs = game.cr3Db ? &game.cr3Db->quests() : nullptr;
            const Cr3CampQuests& cq = campQuests;
            const std::string title = qs && cq.row < int(cq.rows.size()) ? vsText("Quest_Title", (*qs)[size_t(cq.rows[size_t(cq.row)])].id) : std::string();
            if (const size_t at = ask.find("%s"); at != std::string::npos) ask.replace(at, 2, title);
        }
        cr3CampTaps.erase(std::remove_if(cr3CampTaps.begin(), cr3CampTaps.end(), [](const Cr3GeneLabTap& t) { return t.id != -1; }), cr3CampTaps.end());
        cr3CampTaps.clear(); // (the question holds the screen)
        drawCr3Asked(ask, m.since, 0, true, kTapPortYes, kTapPortNo, w, h, view);
    }
    if (m.screen == 4) {
        cr3CampTaps.clear();
        drawCr3Told(m.told, m.since, kTapPortTold, w, h, view);
    }
    spacePortTaps.swap(cr3CampTaps);
    cr3CampTaps.swap(keep);
}

void Viewer::chooseSpacePort(cr3::EventRuntime& ev, int id) {
    Cr3SpacePortMenu& m = spacePortMenu;
    Cr3CampQuests& cq = campQuests;
    if (m.screen == 5) return;
    if (m.screen == 0) {
        if (id == -1) return audio.playSe("com_004", false), ev.spacePortClerk(false), spacePortLeave(1); // (0x16: Sally away, faded out)
        if (id == 0x11) return audio.playSe("com_003", false), ev.spacePortTalk();
        if (id == 0x12) return audio.playSe("com_003", false), openSpacePortQuests();
        if (id == 0x13 && spacePortButtonOn(id)) return audio.playSe("com_003", false), spacePortScreen(3);
        return;
    }
    if (m.screen == 4) return spacePortScreen(0); // (0xe: the alert tapped - the buttons)
    if (m.screen == 3) { // (0x14: Explore?)
        if (id == kTapPortYes) {
            audio.playSe("com_003", false);
            ev.spacePortExplore();
            return spacePortLeave(-1); // (0x19: faded out, a second - Update 0x22: the world select)
        }
        if (id == kTapPortNo || id == -1) return audio.playSe("com_004", false), spacePortScreen(0);
        return;
    }
    if (m.screen == 6) { // (7: Shuffle Quests? - yes: CrxGameData::ShuffleQuest - today's drawn again -, FindQuests, the list anew)
        if (id == kTapPortYes) {
            audio.playSe("com_003", false);
            game.cr3Daily &= ~4;
            const int row = campQuests.row;
            openSpacePortQuests();
            campQuests.category = 1, campQuests.row = std::min(row, 0);
            campQuestRows();
            SDL_Log("space port: quests shuffled (%zu)", campQuests.rows.size());
            return;
        }
        if (id == kTapPortNo || id == -1) audio.playSe("com_004", false), spacePortScreen(1);
        return;
    }
    if (m.screen == 2) { // (0x10: set off?)
        if (id == kTapPortYes) {
            audio.playSe("com_003", false);
            return spacePortLeave(2);
        }
        if (id == kTapPortNo || id == -1) audio.playSe("com_004", false), ev.spacePortClerk(true), openSpacePortQuests(true); // (0x10: SetSugerCutIn, the list again)
        return;
    }
    // the quests (SpacePortMain 6 / 0xc)
    if (cq.detail) {
        if (id == -1) return audio.playSe("com_004", false), ev.spacePortClerk(true), (void)(cq.detail = false); // (0xc: SetSugerCutIn)
        if (id == 500) {
            audio.playSe("com_003", false);
            cq.detail = false; // (EndQuestDetailMenu, then asked)
            return spacePortScreen(2);
        }
        return chooseCampQuests(id); // (the arrows)
    }
    if (id == -1) { // (back: the buttons)
        audio.playSe("com_004", false);
        return spacePortScreen(0);
    }
    if (id == kTapPortShuffle) return audio.playSe("com_003", false), spacePortScreen(6); // (GetTapShuffleButton: asked 0x313)
    chooseCampQuests(id);
    if (cq.detail) ev.spacePortClerk(false); // (6: EndCutInMenu(2), the detail when she's gone)
}

bool Viewer::spacePortClick(float fx, float fy) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || !ev->spacePort().open || ev->spacePort().talk) return false;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    for (auto it = spacePortTaps.rbegin(); it != spacePortTaps.rend(); ++it)
        if (ux >= std::min(it->x0, it->x1) && ux <= std::max(it->x0, it->x1) && uy >= std::min(it->y0, it->y1) && uy <= std::max(it->y0, it->y1)) {
            chooseSpacePort(*ev, it->id);
            return true;
        }
    return true; // (the menu holds the screen)
}

// the keys: up / down the buttons or the rows, left / right the tabs (the detail's arrows), Enter
// chooses (Y / N asked), Escape / Backspace goes back
bool Viewer::spacePortKey(SDL_Keycode key) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || !ev->spacePort().open || ev->spacePort().talk) return false;
    Cr3SpacePortMenu& m = spacePortMenu;
    const bool up = key == SDLK_UP, down = key == SDLK_DOWN, enter = key == SDLK_RETURN || key == SDLK_SPACE;
    const bool back = key == SDLK_ESCAPE || key == SDLK_BACKSPACE;
    if (m.screen == 0) {
        if (up || down) {
            for (int step = 0; step < 3; ++step) {
                m.focus = (m.focus + (down ? 1 : 2)) % 3;
                if (spacePortButtonOn(kSpacePortButtons[m.focus])) break;
            }
            audio.playSe("com_002", false);
        } else if (enter) chooseSpacePort(*ev, kSpacePortButtons[m.focus]);
        else if (back) chooseSpacePort(*ev, -1);
        return true;
    }
    if (m.screen == 2 || m.screen == 3 || m.screen == 6) {
        if (enter || key == SDLK_Y) chooseSpacePort(*ev, kTapPortYes);
        else if (back || key == SDLK_N) chooseSpacePort(*ev, kTapPortNo);
        return true;
    }
    if (m.screen == 4) {
        if (enter || back) chooseSpacePort(*ev, kTapPortTold);
        return true;
    }
    if (m.screen == 1) {
        const Cr3CampQuests& cq = campQuests;
        if (back) return chooseSpacePort(*ev, -1), true;
        if (cq.detail) {
            if (enter) chooseSpacePort(*ev, 500);
            else if (key == SDLK_LEFT || key == SDLK_RIGHT) chooseSpacePort(*ev, key == SDLK_LEFT ? 501 : 502);
            return true;
        }
        const int n = int(cq.rows.size());
        if (key == SDLK_LEFT || key == SDLK_RIGHT) chooseSpacePort(*ev, 400 + (cq.category + (key == SDLK_RIGHT ? 1 : 3)) % 4);
        else if ((up || down) && n > 0) chooseSpacePort(*ev, 300 + std::clamp(cq.row + (down ? 1 : -1), 0, n - 1));
        else if (enter && n > 0) chooseSpacePort(*ev, 300 + cq.row);
        else if (key == SDLK_S && cq.category == 1 && n < 10) chooseSpacePort(*ev, kTapPortShuffle);
    }
    return true;
}

// (each frame: the clocks; leaving - a second after the fade - the result: the quest set off on in
// the global work's 24)
void Viewer::stepSpacePort(float dt) {
    cr3::EventRuntime* ev = areaEvents.get();
    Cr3SpacePortMenu& m = spacePortMenu;
    const bool open = ev && ev->spacePort().open;
    if (open && !m.wasOpen) {
        const bool first = m.first;
        m = Cr3SpacePortMenu();
        m.first = first;
        const int category = campQuests.category; // (InterfaceMain's: the tab last shown kept)
        campQuests = Cr3CampQuests();
        campQuests.category = category;
        SDL_Log("space port: open");
    }
    m.wasOpen = open;
    if (!open) return;
    m.clock += dt, m.since += dt;
    if (m.screen == 5 && m.since >= 1.0f) {
        if (m.leave == 2) {
            const auto* qs = game.cr3Db ? &game.cr3Db->quests() : nullptr;
            const Cr3CampQuests& cq = campQuests;
            if (qs && cq.row < int(cq.rows.size())) game.globalWork[24] = (*qs)[size_t(cq.rows[size_t(cq.row)])].id;
            SDL_Log("space port: set off on quest %d", game.globalWork[24]);
        } else SDL_Log("space port: closed (%d)", m.leave);
        ev->spacePortEnd(std::max(m.leave, 0));
        if (m.leave < 0) ev->openWorldSelect(true); // (Explore: SpacePortMain's 0x22)
    }
}

void Viewer::openMyPage() {
    myPage = Cr3MyPage();
    myPage.vs = vs.screen != 0;
    myPage.buttonFocus = myPage.vs ? 1 : 2;
    cr3Camp.stage = 10, cr3Camp.since = 0;
    audio.playSe("com_003", false);
    if (game.cr3Presents.size() > 79) myPage.told = menuAnnounce(0x2f5); // (MyPageNewTopMain 1: DAT_0143a080 over 0x4f)
    SDL_Log("my page: open (%s, %zu presents)", myPage.vs ? "VS" : "story", game.cr3Presents.size());
}

// a number as the game writes it (Cr3UtilNumberToUTFDoubleString: full-width digits in Japanese)
std::string Viewer::cr3Number(long v, int digits) {
    std::string n = std::to_string(v);
    while (int(n.size()) < digits) n = "0" + n;
    if (optionLanguage != 0) return n;
    std::string w;
    for (const char c : n) w += c >= '0' && c <= '9' ? std::string{char(0xef), char(0xbc), char(0x90 + (c - '0'))} : std::string(1, c);
    return w;
}

std::string Viewer::myPageValue(int row) {
    const cr3::GameState::Cr3Records& r = game.cr3Records;
    const auto with = [&](int text, long v) {
        std::string s = menuAnnounce(text);
        const size_t at = s.find("{number}");
        if (at != std::string::npos) s.replace(at, 8, cr3Number(v));
        return s;
    };
    if (row == 0) {
        const long t = long(r.playSeconds);
        std::string s = menuAnnounce(0x2c0);
        const std::pair<const char*, std::string> parts[3] = {{"{hour}", cr3Number(t / 3600, 2)}, {"{minute}", cr3Number(t / 60 % 60, 2)}, {"{second}", cr3Number(t % 60, 2)}};
        for (const auto& [key, v] : parts)
            if (const size_t at = s.find(key); at != std::string::npos) s.replace(at, std::strlen(key), v);
        return s;
    }
    if (row == 1) return with(0x2c6, r.kills);
    if (row == 3) return with(0x2c3, r.maxDamage);
    if (row == 2 || (row >= 4 && row <= 7)) {
        const int v = row == 2 ? r.vsMatches : row == 4 ? game.cr3Quests.endedCount : row == 5 ? r.genesObtained : row == 6 ? r.merges : r.fusions;
        return with(0x2c2, v);
    }
    if (!game.cr3Db) return {};
    if (row >= 8 && row <= 10) {
        int n = 0;
        for (const cr3::Cr3Gene& g : game.cr3Genes)
            if (const auto* gi = game.cr3Db->geneInfo(g.master); gi && gi->grade == row - 8) ++n;
        return with(0x2c4, n);
    }
    int sum = 0, n = 0;
    for (const cr3::Cr3Gene& g : game.cr3Genes)
        if (game.cr3Db->geneInfo(g.master)) sum += game.cr3Db->geneLevel(g), ++n;
    return with(0x2c5, n ? sum / n : 0);
}

bool Viewer::myPageButtonOn(int b) const {
    if (b == 1) return myPage.vs;
    if (b == 2) return !myPage.vs;
    if (b == 3) return !game.cr3Presents.empty();
    return b == 4;
}

void Viewer::drawMyPage(int w, int h, const Mat4& view) {
    Cr3MyPage& mp = myPage;
    const float t = cr3Camp.since;
    float x, y, cx, cy;
    UiFig& bg = cr3UiPut("combgposition", "", 0, 0, 0, view);
    if (cr3UiCall(bg, "call_comBgMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("combgmenu", "", 0, x, y, view);
        if (cr3UiCall(menu, "call_comBgMark_id", cx, cy)) cr3UiPut("combgmark_id00", "", 0, cx, cy, view);
    }
    dev->clearDepth();
    if (mp.screen == 1) return drawPresents(w, h, view);
    if (mp.screen == 2) return drawPresentDiscard(w, h, view);
    if (mp.screen == 3) return drawGeneIndex(w, h, view);
    UiFig& pos = cr3UiPut("mptopposition", "mptopposition_in", std::min(t, 0.1f), 0, 0, view); // (_in: the two slid in from +-8 over 3 frames, then held)
    // (the log)
    if (cr3UiCall(pos, "call_mpTopLog", x, y, w, h)) {
        UiFig& log = cr3UiPut("mptoplog", "", 0, x, y, view);
        float bx, by;
        if (cr3UiCall(log, "call_mpTopBarMenu", bx, by)) {
            UiFig& bar = cr3UiPut("mptopbarmenu", "", 0, bx, by, view);
            float ux, uy, lx, ly;
            if (cr3UiCall(bar, "call_mpTopBarCursor", cx, cy) && cr3UiCall(bar, "barLimitUp", ux, uy) && cr3UiCall(bar, "barLimitLow", lx, ly))
                cr3UiPut("mptopbarcursor", "", 0, cx, cy + std::clamp(mp.scroll / 1.92f, 0.0f, 1.0f) * (ly + 0.15f - uy) + 0.15f, view);
        }
        float ax, ay, ex, ey;
        if (cr3UiCall(log, "tapAreaS01", ax, ay) && cr3UiCall(log, "tapAreaE01", ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, 900}); // (the log: dragged)
        float lx0, ly0;
        if (cr3UiCall(log, "call_mpTopLogList", lx0, ly0)) {
            UiFig& list = cr3UiPut("mptoploglist", "", 0, lx0, ly0, view);
            for (int k = 0; k < 12; ++k) {
                char joint[32];
                std::snprintf(joint, sizeof joint, "call_mpTopLogTxt_%03d", k + 1);
                const float top = float(k) * 0.48f - mp.scroll; // (its place below the first's: eight show)
                if (top < -0.01f || top > 7 * 0.48f + 0.01f || !cr3UiCall(list, joint, cx, cy)) continue;
                UiFig& row = cr3UiPut("mptoplogtxt", "", 0, cx, cy + mp.scroll, view);
                float tx, ty;
                dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                if (cr3UiCall(row, "typeM_L01", tx, ty)) drawUiText(menuAnnounce(0x2c7 + k), 22, (tx + 0.06f) / 10, (ty - 0.12f) / 10, w, h, true, 0.25f);
                if (cr3UiCall(row, "typeM_R01", tx, ty)) {
                    const std::string v = myPageValue(k);
                    drawUiText(v, 22, (tx - 0.06f) / 10 - uiTextWidth(v, 22, w, h), (ty - 0.12f) / 10, w, h, true, 0.25f);
                }
            }
        }
    }
    // (the menu)
    if (cr3UiCall(pos, "call_mpTopMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("mptopmenu", "", float(mp.pressed ? mp.pressed : vitaButtons() ? mp.buttonFocus : 0) / 30, x, y, view);
        const auto put = [&](const char* joint, const char* fig, float frame = 0) {
            float px, py;
            if (cr3UiCall(menu, joint, px, py)) cr3UiPut(fig, "", frame, px, py, view);
        };
        put(mp.vs ? "call_mpTopVSOff" : "call_mpTopStoryOff", mp.vs ? "mptopvsoff" : "mptopstoryoff");
        if (geneIndexNew() > 0) put("call_mpTopNew", "mptopnew"); // (GetGeneCardNewnumber: cards not yet looked at)
        if (game.cr3Presents.empty()) put("call_mpTopPresentOff", "mptoppresentoff");
        for (int b = 1; b <= 4; ++b) {
            if ((b == 1 && !mp.vs) || (b == 2 && mp.vs)) continue;
            char joint[32];
            std::snprintf(joint, sizeof joint, "call_mpTopIcon%02d", b);
            put(joint, "mptopicon", float(b - 1) / 30);
            std::snprintf(joint, sizeof joint, "tapAreaS%02d", b);
            float ax, ay, ex, ey;
            char end[32];
            std::snprintf(end, sizeof end, "tapAreaE%02d", b);
            if (cr3UiCall(menu, joint, ax, ay) && cr3UiCall(menu, end, ex, ey)) cr3CampTaps.push_back({ax, ay, ex, ey, 200 + b});
        }
        float px, py;
        if (!game.cr3Presents.empty() && cr3UiCall(menu, "call_mpTopCome", px, py)) { // (InterfaceMain::SetViewNumbersZero: two digits)
            UiFig& come = cr3UiPut("mptopcome", "", 0, px, py, view);
            const int n = std::min(int(game.cr3Presents.size()), 99);
            if (cr3UiCall(come, "call_mpTopNum_001", cx, cy)) cr3UiPut("mptopnum", "", float(n % 10) / 30, cx, cy, view); // (the ones, on the right)
            if (cr3UiCall(come, "call_mpTopNum_002", cx, cy)) cr3UiPut("mptopnum", "", float(n / 10) / 30, cx, cy, view);
        }
    }
    drawCr3CampFooter(w, h, view);
    if (mp.ask) drawCr3Asked(menuAnnounce(mp.ask == 1 ? 0x2ee : 0x2ef), t, vitaButtons() ? mp.askFocus : 0, true, 500, 501, w, h, view);
    if (!mp.told.empty()) drawCr3Told(mp.told, mp.toldSince, 502, w, h, view);
    drawCr3Header(t, 8, std::string(), w, h, view);
}

void Viewer::chooseMyPage(int id) {
    Cr3MyPage& mp = myPage;
    if (mp.screen == 1) return choosePresents(id);
    if (mp.screen == 2) return choosePresentDiscard(id);
    if (mp.screen == 3) return chooseGeneIndex(id);
    if (!mp.told.empty()) {
        if (id == 502 || id == -1) mp.told.clear();
        return;
    }
    if (mp.ask) {
        if (id == 501 || id == -1) {
            audio.playSe("com_004", false);
            mp.ask = 0, mp.pressed = 0;
        } else if (id == 500) { // (MyPageNewTopMain 0x16 / 0x17: GameMain::SetVS - the VS mode's VSMain, or the story's field)
            audio.playSe("com_003", false);
            const int b = mp.ask;
            mp.ask = 0, mp.pressed = 0;
            cr3Camp.stage = 0;
            SDL_Log("my page: to %s mode", b == 1 ? "story" : "battle");
            if (b == 1) vs = Cr3VsMode();
            else openVs();
        }
        return;
    }
    if (id == -1) { // (MyPageNewTopMain 8 / 9: the screen's way out)
        audio.playSe("com_004", false);
        cr3Camp.stage = 0;
        SDL_Log("my page: closed");
        return;
    }
    if (id < 201 || id > 204) return;
    const int b = id - 200;
    if (!myPageButtonOn(b)) return (void)audio.playSe("com_005", false);
    audio.playSe("com_003", false);
    mp.pressed = b;
    if (b <= 2) mp.ask = b, mp.askFocus = 0;
    else if (b == 3) openPresents();
    else openGeneIndex();
}

// ---- My Page's presents (PresentGetMain 0x92, PresentGetMenu 0x93) ----
// comListSPosition's comListSMenu (fit4) - its comListPanel rows, 0.76 apart, three showing: the
// presents (PresentGetMain 1: AddPresentListItem - an item its icon, name, count, help; a gene
// its card icon 0xad and 0x321 - {genename}: {generarety}, 0x242 + grade -), Accept All (icon
// 0xc0, 0x2db / 0x2dc) first with two or more; the one in focus's help the header's line. Below,
// the system window and its OK / Cancel (SetBasicYNChoice2Window): the one in focus - 0x14c
// ({itemname}, {itemnumber}) or 0x2dc - OK off where no more of the item can be held
// (GetBuyCanItemNumbers: 99 - held). OK: ReceivePresent (its row gone, Accept All too with one
// left; none left - the screen closes) or, for Accept All, ReceivePresents and the screen
// closes; Cancel closes it. A gene with 50 held (CrxGameData::IsGeneFull - the stock not
// counted): told 0x235, then a gene to discard among the held and the present's own
// (SetGeneDeleteListItem), asked 0x236 - the present's: RejectPresent, else RemoveGene and then
// ReceivePresent -, told 0x237 ({genename})
void Viewer::openPresents() {
    Cr3MyPage& mp = myPage;
    mp.screen = 1, mp.screenSince = 0, mp.pressed = 0, mp.row = 0;
    presentRows();
    SDL_Log("my page: presents (%zu)", game.cr3Presents.size());
}

void Viewer::presentRows() {
    Cr3MyPage& mp = myPage;
    mp.rows.clear();
    if (game.cr3Presents.size() > 1) mp.rows.push_back(-1);
    for (int k = 0; k < int(game.cr3Presents.size()); ++k) mp.rows.push_back(k);
    mp.row = std::clamp(mp.row, 0, std::max(int(mp.rows.size()) - 1, 0));
}

std::string Viewer::presentName(int k) {
    if (k < 0) return menuAnnounce(0x2db);
    const cr3::Cr3Present& p = game.cr3Presents[size_t(k)];
    if (!p.gene) return cr3ItemText(p.item, false);
    const auto* gi = game.cr3Db ? game.cr3Db->geneInfo(p.geneData.master) : nullptr;
    std::string s = menuAnnounce(0x321);
    if (const size_t at = s.find("{genename}"); at != std::string::npos) s.replace(at, 10, cr3NameOf("gene_name.mbe", "GeneName", p.geneData.master));
    if (const size_t at = s.find("{generarety}"); at != std::string::npos) s.replace(at, 12, menuAnnounce(0x242 + (gi ? gi->grade : 0)));
    return s;
}

bool Viewer::presentTakeable(int k) const { // (GetBuyCanItemNumbers: an item's 99 - held; the rest as DBEmu decides)
    if (k < 0) return true;
    const cr3::Cr3Present& p = game.cr3Presents[size_t(k)];
    if (p.gene || p.item / 100000 == 1 || p.item / 100000 == 2 || p.item / 100000 == 3) return true;
    const auto it = game.cr3Items.find(p.item);
    return 99 - (it == game.cr3Items.end() ? 0 : it->second) >= 1;
}

void Viewer::drawPresents(int w, int h, const Mat4& view) {
    Cr3MyPage& mp = myPage;
    const float t = mp.screenSince;
    float x, y, cx, cy;
    const int focused = mp.row < int(mp.rows.size()) ? mp.rows[size_t(mp.row)] : -2;
    UiFig& pos = cr3UiPut("comlistsposition", "", std::min(t, 5.0f / 30), 0, 0, view); // (its own animation: in from -5.5 over 5 frames)
    if (cr3UiCall(pos, "call_comListSMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("comlistsmenu", "", 0, x, y, view);
        float lx, ly;
        if (cr3UiCall(menu, "call_comListPanel", lx, ly)) {
            const int n = int(mp.rows.size());
            const int top = std::clamp(mp.row - 1, 0, std::max(n - 3, 0)); // (three above the system window)
            drawCr3ListBar(menu, true, top, n, 3, view);
            for (int r = top; r < n && r < top + 3; ++r) {
                const int k = mp.rows[size_t(r)];
                Cr3FocusRow focusRow(*this, r == mp.row);
                UiFig& panel = cr3UiPut("comlistpanel", "", 0, cr3ListRowX(lx, r, mp.row), cr3ListRowY(ly, r, top, mp.row), view);
                if (r == mp.row && cr3UiCall(panel, "call_comListCursor", cx, cy)) cr3UiPut("comlistcursor", "", 0, cx, cy, view);
                int icon = k < 0 ? 0xc0 : game.cr3Presents[size_t(k)].gene ? 0xad : -1;
                cr3::MbeFile::Row ir;
                if (icon < 0 && cr3ItemRow(game.cr3Presents[size_t(k)].item, ir)) icon = ir.i(3);
                if (icon >= 0 && cr3UiCall(panel, "call_comListIcon_fr00", cx, cy)) cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), icon), cx, cy, view);
                if (k >= 0 && cr3UiCall(panel, "call_comListNumI_num00", cx, cy)) {
                    UiFig& num = cr3UiPut("comlistnumi_num00", "", 0, cx, cy, view);
                    const std::string digits = std::to_string(std::max(game.cr3Presents[size_t(k)].gene ? 1 : game.cr3Presents[size_t(k)].count, 0));
                    int slots = 0;
                    for (const std::string& j : num.joints) slots += j.rfind("call_comListNum_fr00_", 0) == 0;
                    for (int d = 0; d < int(digits.size()) && d < slots; ++d) {
                        float dx, dy;
                        if (cr3UiCall(num, "call_comListNum_fr00_00" + std::to_string(slots - int(digits.size()) + d + 1), dx, dy))
                            cr3UiPut("comlistnum_fr00", "", float(digits[size_t(d)] - '0') / 30, dx, dy, view);
                    }
                }
                cr3UiLabel(panel, "typeM_L01", presentName(k), w, h, 0.3f);
                float ax, ay, bx, by;
                if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) cr3CampTaps.push_back({ax, ay, bx, by, 300 + r});
            }
        }
    }
    // (the system window: the one in focus, OK / Cancel)
    if (focused >= -1 && mp.told.empty()) {
        std::string text = focused < 0 ? menuAnnounce(0x2dc) : menuAnnounce(0x14c);
        if (focused >= 0) {
            const cr3::Cr3Present& p = game.cr3Presents[size_t(focused)];
            std::string name = p.gene ? cr3NameOf("gene_name.mbe", "GeneName", p.geneData.master) : cr3ItemText(p.item, false);
            if (const size_t at = text.find("{itemname}"); at != std::string::npos) text.replace(at, 10, name);
            if (const size_t at = text.find("{itemnumber}"); at != std::string::npos) text.replace(at, 12, std::to_string(p.gene ? 1 : p.count));
        }
        drawCr3Asked(text, t, vitaButtons() ? mp.presentButton : 0, presentTakeable(focused), 500, 501, w, h, view);
    }
    if (!mp.told.empty()) drawCr3Told(mp.told, mp.toldSince, 502, w, h, view);
    std::string help;
    if (focused == -1) help = menuAnnounce(0x2dc);
    else if (focused >= 0 && !game.cr3Presents[size_t(focused)].gene) help = cr3ItemText(game.cr3Presents[size_t(focused)].item, true);
    drawCr3Header(cr3Camp.since, 8, help, w, h, view);
}

void Viewer::closePresents() {
    myPage.screen = 0, myPage.screenSince = 0, myPage.pressed = 0;
    SDL_Log("my page: presents closed");
}

void Viewer::choosePresents(int id) {
    Cr3MyPage& mp = myPage;
    if (!mp.told.empty()) {
        if (id == 502 || id == -1) {
            mp.told.clear();
            if (mp.present >= 0 && mp.screen == 1) { // (0x235 read: the gene to discard)
                mp.screen = 2, mp.screenSince = 0, mp.genes.clear(), mp.focus = 0, mp.asked = -1;
                mp.genes.push_back(-1);
                for (int g = 0; g < int(game.cr3Genes.size()); ++g)
                    if (!game.cr3Genes[size_t(g)].stocked) mp.genes.push_back(g);
            }
            if (mp.rows.empty() && mp.screen == 1) closePresents();
        }
        return;
    }
    if (id == 501 || id == -1) { // (Cancel: PresentGetMain 2 - the screen closes)
        audio.playSe("com_004", false);
        return closePresents();
    }
    if (id >= 300 && id < 300 + int(mp.rows.size())) {
        if (id - 300 != mp.row) mp.row = id - 300, audio.playSe("com_002", false);
        return;
    }
    if (id != 500 || mp.row >= int(mp.rows.size()) || !game.cr3Db) return;
    const int k = mp.rows[size_t(mp.row)];
    if (!presentTakeable(k)) return;
    audio.playSe("com_003", false);
    if (k < 0) { // (Accept All: ReceivePresents, then the screen closes)
        const int r = game.cr3Db->receivePresents(game);
        SDL_Log("my page: accepted all presents -> %#x (%zu left)", r, game.cr3Presents.size());
        return closePresents();
    }
    const cr3::Cr3Present& p = game.cr3Presents[size_t(k)];
    if (p.gene && geneLabHeld() >= game.cr3Db->constant("GENE_CAPACITY", 50)) { // (IsGeneFull: 0x235, then the gene to discard)
        mp.present = k, mp.told = menuAnnounce(0x235), mp.toldSince = 0;
        return;
    }
    acceptPresent(k);
}

void Viewer::acceptPresent(int k) {
    const int r = game.cr3Db->receivePresent(game, k);
    SDL_Log("my page: accepted present %d -> %#x (%zu left)", k, r, game.cr3Presents.size());
    presentRows();
    if (myPage.rows.empty()) closePresents();
}

// a gene to discard for a present's (PresentGetMain 0xb .. 0x10: GeneChangeMenuCamp's cards -
// the present's own first)
void Viewer::drawPresentDiscard(int w, int h, const Mat4& view) {
    Cr3MyPage& mp = myPage;
    if (!mp.told.empty()) { // (0x237: the cards gone - DeleteGeneChangeMenuCamp)
        drawCr3Told(mp.told, mp.toldSince, 502, w, h, view);
        return drawCr3Header(cr3Camp.since, 8, std::string(), w, h, view);
    }
    if (mp.present < 0 || mp.present >= int(game.cr3Presents.size())) return;
    // (the present's gene a card for the while: its own index past the held)
    game.cr3Genes.push_back(game.cr3Presents[size_t(mp.present)].geneData);
    const int own = int(game.cr3Genes.size()) - 1;
    std::vector<int> cards;
    for (const int g : mp.genes) cards.push_back(g < 0 ? own : g);
    std::swap(cr3Camp.genes, cards), std::swap(cr3Camp.focus, mp.focus);
    const float since = cr3Camp.since;
    cr3Camp.since = mp.screenSince;
    drawCr3CampGenes(w, h, view);
    cr3Camp.since = since;
    std::swap(cr3Camp.genes, cards), std::swap(cr3Camp.focus, mp.focus);
    game.cr3Genes.pop_back();
    if (mp.asked >= 0 && mp.asked < int(mp.genes.size())) {
        const int g = mp.genes[size_t(mp.asked)];
        std::string text = menuAnnounce(0x236);
        const int master = g < 0 ? game.cr3Presents[size_t(mp.present)].geneData.master : game.cr3Genes[size_t(g)].master;
        if (const size_t at = text.find("{genename}"); at != std::string::npos) text.replace(at, 10, cr3NameOf("gene_name.mbe", "GeneName", master));
        drawCr3Asked(text, mp.screenSince, 0, true, 500, 501, w, h, view);
    }
    drawCr3Header(cr3Camp.since, 8, std::string(), w, h, view); // (no title of its own: My Page's)
}

void Viewer::choosePresentDiscard(int id) {
    Cr3MyPage& mp = myPage;
    if (!mp.told.empty()) { // (0x237 read: the list again)
        if (id == 502 || id == -1) {
            mp.told.clear(), mp.present = -1, mp.screen = 1, mp.screenSince = 0;
            presentRows();
            if (mp.rows.empty()) closePresents();
        }
        return;
    }
    if (mp.asked >= 0) {
        if (id == 501 || id == -1) mp.asked = -1, audio.playSe("com_004", false);
        else if (id == 500) {
            audio.playSe("com_003", false);
            const int g = mp.genes[size_t(mp.asked)];
            const int master = g < 0 ? game.cr3Presents[size_t(mp.present)].geneData.master : game.cr3Genes[size_t(g)].master;
            if (g < 0) game.cr3Db->dropPresent(game, mp.present); // (RejectPresent)
            else {
                game.cr3Db->removeGene(game, g);
                game.cr3Db->receivePresent(game, mp.present);
            }
            SDL_Log("my page: discarded gene %d (%s) - %d held, %zu presents", master, g < 0 ? "the present's" : "held", geneLabHeld(), game.cr3Presents.size());
            mp.asked = -1;
            mp.told = menuAnnounce(0x237), mp.toldSince = 0;
            if (const size_t at = mp.told.find("{genename}"); at != std::string::npos) mp.told.replace(at, 10, cr3NameOf("gene_name.mbe", "GeneName", master));
        }
        return;
    }
    if (id == -1) { // (the footer's back: the list again, the present kept)
        audio.playSe("com_004", false);
        mp.present = -1, mp.screen = 1, mp.screenSince = 0;
        return;
    }
    if (id >= kTapCard && id < kTapRow) {
        const int g = id - kTapCard;
        if (g < 0 || g >= int(mp.genes.size())) return;
        if (g != mp.focus) return (void)(mp.focus = g, audio.playSe("com_002", false));
        mp.asked = g;
        audio.playSe("com_003", false);
    }
}

void Viewer::myPageKey(SDL_Keycode key) {
    Cr3MyPage& mp = myPage;
    const bool enter = key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE, back = key == SDLK_ESCAPE || key == SDLK_BACKSPACE;
    if (!mp.told.empty()) return (enter || back) ? chooseMyPage(502) : void();
    if (mp.screen == 3) {
        if (geneIndexKey(key)) return;
        if (cardView.gene) return (enter || back) ? chooseMyPage(904) : void();
        if (back) return chooseMyPage(-1);
        if (key == SDLK_LEFT || key == SDLK_RIGHT) return chooseMyPage(key == SDLK_LEFT ? 700 : 701);
        if (key == SDLK_TAB) return chooseMyPage(mp.profile ? 800 + (mp.rarity + 1) % 5 : 600 + (mp.bookTab + 1) % 8);
        if (enter && !mp.profile) { // (the first card had on the spread)
            int had = 0;
            const auto cards = geneIndexCards(had);
            for (int k = mp.bookPage * 6; k < int(cards.size()) && k < mp.bookPage * 6 + 12; ++k)
                if (cards[size_t(k)].second > 0) return chooseMyPage(1000 + k);
        }
        return;
    }
    if (mp.screen == 1 || mp.screen == 2) {
        if (!mp.told.empty()) return (enter || back) ? chooseMyPage(502) : void();
        if (mp.screen == 2 && mp.asked >= 0) return enter ? chooseMyPage(500) : back ? chooseMyPage(501) : void();
        if (back) return chooseMyPage(-1);
        if (mp.screen == 1) {
            const int n = int(mp.rows.size());
            if (vitaButtons() && (key == SDLK_LEFT || key == SDLK_RIGHT)) {
                const int to = key == SDLK_RIGHT ? 1 : 0;
                if (to != mp.presentButton) mp.presentButton = to, audio.playSe("com_002", false), SDL_Log("my page: present button %d", to);
                return;
            }
            if ((key == SDLK_UP || key == SDLK_DOWN) && n > 0) return chooseMyPage(300 + std::clamp(mp.row + (key == SDLK_DOWN ? 1 : -1), 0, n - 1));
            if (enter) return chooseMyPage(vitaButtons() && mp.presentButton ? 501 : 500);
            return;
        }
        const int n = int(mp.genes.size());
        if ((key == SDLK_LEFT || key == SDLK_RIGHT) && n > 0) return chooseMyPage(kTapCard + std::clamp(mp.focus + (key == SDLK_RIGHT ? 1 : -1), 0, n - 1));
        if (enter && n > 0) return chooseMyPage(kTapCard + mp.focus);
        return;
    }
    if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) return chooseMyPage(myPage.ask ? 501 : -1);
    if (myPage.ask && enter) return chooseMyPage(vitaButtons() && mp.askFocus ? 501 : 500);
    if (vitaButtons()) {
        if (myPage.ask) {
            if (key == SDLK_LEFT || key == SDLK_UP || key == SDLK_RIGHT || key == SDLK_DOWN) mp.askFocus ^= 1, audio.playSe("com_002", false);
            return;
        }
        if (key == SDLK_UP || key == SDLK_DOWN) {
            const int way = key == SDLK_DOWN ? 1 : -1;
            for (int n = 0, b = mp.buttonFocus; n < 4; ++n) {
                b = (b - 1 + way + 4) % 4 + 1;
                if (myPageButtonOn(b)) {
                    if (b != mp.buttonFocus) audio.playSe("com_002", false);
                    mp.buttonFocus = b;
                    SDL_Log("my page: the cursor on %d", b);
                    break;
                }
            }
            return;
        }
        if (enter) return chooseMyPage(200 + mp.buttonFocus);
    }
    if (key == SDLK_UP || key == SDLK_DOWN) myPageScroll(key == SDLK_DOWN ? 0.48f : -0.48f);
    if (key >= SDLK_1 && key <= SDLK_4) chooseMyPage(200 + int(key - SDLK_1) + 1);
}

// the log scrolled (TouchSimpleMove: by the drag, / 200 of the screen's 640; then settled on a
// row - AutoScroll)
void Viewer::myPageScroll(float by) {
    myPage.scroll = std::clamp(std::round((myPage.scroll + by) / 0.48f) * 0.48f, 0.0f, 1.92f);
}

void Viewer::myPagePadScroll(float axis, float dt) {
    if (!vitaButtons() || cr3Camp.stage != 10 || myPage.screen != 0 || myPage.ask || !myPage.told.empty()) return;
    if (std::fabs(axis) >= 0.4f) myPage.scroll = std::clamp(myPage.scroll + (axis > 0 ? 1.0f : -1.0f) * 6.0f * dt, 0.0f, 1.92f);
    else myPage.scroll = std::clamp(myPage.scroll + (std::round(myPage.scroll / 0.48f) * 0.48f - myPage.scroll) * std::min(dt * 15, 1.0f), 0.0f, 1.92f);
}

std::string Viewer::vsText(const char* file, int id) {
    std::vector<uint8_t>& d = messageFiles[file];
    if (d.empty() && !archive.read(std::string("message\\") + file + ".dat", d)) return {};
    auto u32 = [&](size_t o) { return o + 4 <= d.size() ? uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24) : 0u; };
    if (id < 1 || uint32_t(id) > u32(8)) return {};
    const size_t o = size_t(u32(4)) + u32(size_t(u32(0)) + size_t(id - 1) * 8 + (optionLanguage == 0 ? 0 : 4));
    std::string s;
    for (size_t k = o; k < d.size() && d[k]; ++k) s += char(d[k]);
    for (size_t p; (p = s.find('\r')) != std::string::npos;) s.erase(p, 1);
    return s;
}

void Viewer::openVs() {
    if (!game.cr3Db) return;
    game.cr3Db->getVSTournaments(game, std::time(nullptr));
    game.cr3Db->getVSMission(game);
    vs = Cr3VsMode();
    vs.screen = 1;
    audio.playSe("com_003", false);
    SDL_Log("vs: open - %zu tournaments, mission %d (goal %d)", game.cr3Db->vsTournaments().size(), game.cr3Vs.mission, game.cr3Vs.missionGoal);
}

void Viewer::openPremiumShop() {
    if (!game.cr3Db) return;
    vs = Cr3VsMode();
    vs.screen = 10, vs.shopOnly = true;
    audio.playSe("com_003", false);
    SDL_Log("shop: the Sp Shop - %zu goods, %zu boxes", vsShopGoods().size(), spBoxes().size());
}

auto Viewer::spBoxes() -> std::vector<SpBox> {
    std::vector<SpBox> out;
    if (!game.cr3Db) return out;
    const cr3::BsonValue* c = game.cr3Db->constants();
    auto fill = [c](SpBox& b, const char* prices, const char* amounts) {
        const cr3::BsonValue* p = c ? c->get(prices) : nullptr;
        const cr3::BsonValue* a = c ? c->get(amounts) : nullptr;
        for (size_t k = 0; k < 3; ++k) {
            if (p && p->isArray() && k < p->items.size()) b.price[k] = p->items[k].asInt();
            if (a && a->isArray() && k < a->items.size()) b.amount[k] = a->items[k].asInt();
        }
    };
    for (int g : game.cr3Db->month().geneBoxes) {
        SpBox b;
        b.kind = 0, b.id = g, b.texture = "cltList_gene_01", b.background = "boxBg_gene_01";
        fill(b, "GENEBOX_PRICES", "GENEBOX_AMOUNTS");
        out.push_back(b);
    }
    const auto& sheets = game.cr3Db->itemSheets();
    for (size_t k = 0; k < sheets.size(); ++k) {
        const auto it = game.cr3Sheets.find(sheets[k].id);
        if (it == game.cr3Sheets.end() || std::none_of(it->second.opened.begin(), it->second.opened.end(), [](int n) { return n > 0; })) continue;
        SpBox b;
        b.kind = 1, b.id = int(k), b.texture = "cltList_" + sheets[k].id;
        const bool vsSheet = sheets[k].id.find('e') != std::string::npos || sheets[k].id.find('v') != std::string::npos;
        b.background = vsSheet ? "boxBg_Colosseum_01" : "boxBg_" + sheets[k].id;
        fill(b, "LUCKYBOX_PRICES", "LUCKYBOX_AMOUNTS");
        out.push_back(b);
    }
    SpBox gp;
    gp.kind = 2, gp.texture = "cltList_gp_01", gp.background = "boxBg_gp_01", gp.currency = 1;
    fill(gp, "GPBOX_PRICES", "GPBOX_AMOUNTS");
    out.push_back(gp);
    return out;
}

// the n-th joint (in the figure's order) of those named so (SearchOffsetJointPositionAnyString's count)
bool Viewer::cr3UiCallNth(const UiFig& u, const std::string& prefix, int n, float& x, float& y) {
    for (const std::string& j : u.joints)
        if (j.compare(0, prefix.size(), prefix) == 0 && n-- == 0) return uiJoint(u, j, x, y);
    return false;
}

// a tap rectangle from a figure's tapAreaS01 / E01, else `half` about its anchor
void Viewer::spTap(const UiFig& u, float px, float py, float hw, float hh, int id) {
    float ax, ay, ex, ey;
    if (cr3UiCall(u, "tapAreaS01", ax, ay) && cr3UiCall(u, "tapAreaE01", ex, ey)) vsTaps.push_back({ax, ay, ex, ey, id});
    else vsTaps.push_back({px - hw, py - hh, px + hw, py + hh, id});
}

void Viewer::drawSpShop(int w, int h, const Mat4& view) {
    const float t = vs.since;
    float x, y, cx, cy;
    if (vs.screen == 10) {
        UiFig& pos = cr3UiPut("sshoppositon", "", t, 0, 0, view);
        if (cr3UiCall(pos, "call_SshopBg", x, y)) cr3UiPut("sshopbg", "", t, x, y, view);
        const auto focus = [&](int k) {
            cr3UiScale = !vitaButtons() || vs.shopFocus == k ? 1.0f : 0.9f;
            cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = !vitaButtons() || vs.shopFocus == k ? 1.0f : 0.8f;
        };
        focus(0);
        if (cr3UiCall(pos, "call_SshopBox", x, y)) spTap(cr3UiPut("sshopbox", "", t, x, y, view), x, y, 2.5f, 2.0f, 1000);
        focus(1);
        if (cr3UiCall(pos, "call_SshopItem", x, y)) spTap(cr3UiPut("sshopitem", "", t, x, y, view), x, y, 2.5f, 2.0f, 1001);
        focus(2);
        // (SetVipPanelControl: 90 days or more - the panel at half, not tappable)
        const bool vipOn = game.cr3VipDays < 90;
        if (cr3UiCall(pos, "call_SshopVipPanel", x, y)) {
            if (!vipOn) cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 0.5f;
            UiFig& vip = cr3UiPut("sshopvippanel", "", t, x, y, view);
            if (vipOn) spTap(vip, x, y, 2.5f, 1.0f, 1002);
            if (game.cr3VipDays <= 0) {
                if (cr3UiCall(vip, "call_SshopDefault", cx, cy)) cr3UiPut("sshopvipdefault", "", t, cx, cy, view);
            } else if (cr3UiCall(vip, "call_SshopVipCount", cx, cy)) {
                if (optionLanguage != 0) cx -= 0.3f;
                UiFig& count = cr3UiPut("sshopvipcount", "", t, cx, cy, view);
                const int days = std::min(game.cr3VipDays, 999);
                const int digits = days >= 100 ? 3 : days >= 10 ? 2 : 1;
                for (int k = 0; k < digits; ++k) { // (SShopMenu::Pose: the ones at the first joint, the tens, the hundreds)
                    float nx, ny;
                    int d = days;
                    for (int s2 = k; s2 > 0; --s2) d /= 10;
                    if (cr3UiCallNth(count, "call_SshopVipNum", k, nx, ny)) cr3UiPut("sshopvipnum", "", float(d % 10) / 30, nx - (optionLanguage != 0 ? 0.3f : 0.0f), ny, view);
                }
            }
        }
        cr3UiScale = 1;
        cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
        drawCr3Header(t, 9, menuAnnounce(0x275), w, h, view);
        return;
    }
    if (vs.screen == 12) { // (SShopMenu 5: SshopVipCard over the shop, the header's title 0xc, asked 0x316)
        vs.screen = 10; // (the top under it, as it stands)
        drawSpShop(w, h, view);
        vs.screen = 12;
        dev->clearDepth();
        cr3UiPut("sshopvipcard", "", t, 0, 0, view);
        drawCr3Header(t, 0xc, std::string(), w, h, view);
        return;
    }
    // the boxes
    const std::vector<SpBox> boxes = spBoxes();
    if (boxes.empty()) return;
    vs.boxFocus = std::clamp(vs.boxFocus, 0, int(boxes.size()) - 1);
    const SpBox& box = boxes[size_t(vs.boxFocus)];
    auto boxPicture = [&](const std::string& fig, const std::string& material, const std::string& image) {
        UiFig& u = uiFig(fig);
        cr3UiMaterial(u, material, image);
        // (BoxPanel::LoadBoxAlphaTexture / BoxBuyPanel::SetTexture: its mask, <image>A, beside it)
        cr3::Texture* a = texture(image + "A", false);
        for (ViewMesh& vm : u.fig.meshes)
            if (vm.source.material < u.materials.size() && u.materials[vm.source.material] == material) vm.extraMap = a;
    };
    UiFig& bgPos = cr3UiPut("mpboxbgpositon", "", t, 0, 0, view);
    if (cr3UiCall(bgPos, "call_mpBoxBgMenu", x, y, w, h)) {
        UiFig& bg = cr3UiPut("mpboxbgmenu", "", t, x, y, view);
        if (cr3UiCall(bg, "call_mpBoxBg_id00", cx, cy)) boxPicture("mpboxbg_id00", "mat_boxBg_d02", box.background), cr3UiPut("mpboxbg_id00", "", 0, cx, cy, view);
        if (cr3UiCall(bg, "call_mpBoxName_id00", cx, cy)) boxPicture("mpboxname_id00", "mat_cltList_d02", box.texture), cr3UiPut("mpboxname_id00", "", 0, cx, cy, view);
    }
    dev->clearDepth();
    UiFig& pos = cr3UiPut("mpboxpositon", "", t, 0, 0, view); // (its own animation: the carousel in)
    if ((!vs.boxBuy || vitaButtons()) && cr3UiCall(pos, "ragdoll_call_mpBoxMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("mpboxmenu", "", t, x, y, view);
        const int n = int(boxes.size());
        for (int k = 0; k < 3; ++k) { // (the one before, the one in focus - 1.35 x -, the one after)
            if (n < 2 && k != 1) continue;
            const int b = ((vs.boxFocus + k - 1) % n + n) % n;
            if (!cr3UiCallNth(menu, "call_mpBoxImagePanel", k, cx, cy)) continue;
            boxPicture("mpboximage_id00", "mat_cltList_d02", boxes[size_t(b)].texture);
            cr3UiScale = k == 1 && !vs.boxCheckFocus ? 1.35f : 1.0f; // (BoxPanel::SetBoxScale: the one in focus)
            UiFig& p = cr3UiPut("mpboximagepanel", "", 0, cx, cy, view);
            float ix, iy;
            if (cr3UiCall(p, "call_mpBoxImage_id", ix, iy)) cr3UiPut("mpboximage_id00", "", 0, ix, iy, view);
            cr3UiScale = 1;
            spTap(p, cx, cy, k == 1 ? 1.6f : 1.1f, k == 1 ? 1.6f : 1.1f, 1100 + k);
        }
    }
    if (!vs.boxBuy && cr3UiCall(pos, "ragdoll_call_mpBoxNumMenu", x, y, w, h)) { // (the box's place, of how many)
        UiFig& num = cr3UiPut("mpboxnummenu", "", t, x, y, view);
        const int values[2] = {vs.boxFocus + 1, int(boxes.size())};
        for (int k = 0; k < 2; ++k) {
            if (!cr3UiCallNth(num, "call_mpBoxNum_num", k, cx, cy)) continue;
            UiFig& digits = cr3UiPut("mpboxnum_num00", "", 0, cx, cy, view);
            float dx, dy;
            if (cr3UiCallNth(digits, "call_mpBoxNoNum_fr", 0, dx, dy) && values[k] >= 10) cr3UiPut("mpboxnonum_fr00", "", float(values[k] / 10 % 10) / 30, dx, dy, view);
            if (cr3UiCallNth(digits, "call_mpBoxNoNum_fr", 1, dx, dy)) cr3UiPut("mpboxnonum_fr00", "", float(values[k] % 10) / 30, dx, dy, view);
        }
    }
    if (!vs.boxBuy && cr3UiCall(pos, "ragdoll_call_mpBoxArrowMenu", x, y, w, h)) cr3UiPut("mpboxarrowmenu", "", uiClock, x, y, view, 1, true);
    if (vs.boxBuy) {
        UiFig& buyPos = cr3UiPut("mpboxbuypositon", "", t, 0, 0, view);
        if (cr3UiCall(buyPos, "ragdoll_call_mpBoxBuyMenu", x, y, w, h)) {
            UiFig& menu = cr3UiPut("mpboxbuymenu", "", t, x, y, view);
            for (int k = 0; k < 3; ++k) {
                if (box.amount[k] <= 0 || !cr3UiCallNth(menu, "call_mpBoxBuyPanel", k, cx, cy)) continue;
                boxPicture("mpboxbuyimage_id00", "mat_cltList_d02", box.texture);
                if (vitaButtons() && !vs.boxCheckFocus && k == vs.boxPanel) cr3UiTint[0] = 0.5f, cr3UiTint[1] = 1, cr3UiTint[2] = 1.5f;
                UiFig& panel = cr3UiPut("mpboxbuypanel", "", 0, cx, cy, view, k == vs.boxPanel ? 1.0f : 0.8f);
                cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
                float px, py, qx, qy;
                // (BoxBuyPanel::Pose: the panel's Icon - the amount, 1 / 5 / 10 frames 0 .. 2 -, Size -
                // the box's picture on its call_mpBoxBuyImage_id -, Num - the currency's
                // mpBoxBuyUni and the price's four mpBoxBuyPriceNum digits -, Tap)
                const int icon = box.amount[k] == 1 ? 0 : box.amount[k] == 5 ? 1 : box.amount[k] == 10 ? 2 : -1;
                if (icon >= 0 && cr3UiCall(panel, "call_mpBoxBuyIcon_fr", px, py)) cr3UiPut("mpboxbuyicon_fr00", "", float(icon) / 30, px, py, view);
                if (cr3UiCall(panel, "call_mpBoxBuySize_fr", px, py)) {
                    UiFig& size = cr3UiPut("mpboxbuysize_fr00", "", float(k) / 30, px, py, view);
                    if (cr3UiCall(size, "call_mpBoxBuyImage_id", qx, qy)) {
                        cr3UiPut("mpboxbuyimage_id00", "", 0, qx, qy, view);
                    }
                }
                if (cr3UiCall(panel, "call_mpBoxBuyNum_num", px, py)) {
                    UiFig& num = cr3UiPut("mpboxbuynum_num00", "", 0, px, py, view);
                    if (cr3UiCall(num, "call_mpBoxBuyUni_fr", qx, qy)) cr3UiPut("mpboxbuyuni_fr00", "", float(box.currency) / 30, qx, qy, view);
                    // (SetPriceNumber: four digits, leading zeros left out, at digit + 10 x the currency)
                    const int price = std::min(box.price[k], 9999);
                    bool shown = false;
                    for (int d = 0, div = 1000; d < 4; ++d, div /= 10) {
                        const int digit = price / div % 10;
                        if (digit > 0 || d == 3) shown = true;
                        if (shown && cr3UiCallNth(num, "call_mpBoxBuyPriceNum_fr", d, qx, qy)) cr3UiPut("mpboxbuypricenum_fr00", "", float(digit + 10 * box.currency) / 30, qx, qy, view);
                    }
                }
                // (mpBoxBuyTap: hidden - BoxBuyPanel::Initialize; a press's highlight)
                spTap(panel, cx, cy, 1.6f, 0.6f, 1110 + k);
            }
        }
        if (vitaButtons() && box.kind != 2 && cr3UiCall(buyPos, "ragdoll_call_mpBoxChkBtnMenu", x, y, w, h)) {
            if (vs.boxCheckFocus) cr3UiTint[0] = 0.5f, cr3UiTint[1] = 1, cr3UiTint[2] = 1.5f;
            UiFig& check = cr3UiPut("mpboxchkbtnmenu", "", float(box.kind == 0 ? 2 : 1) / 30, x, y, view);
            spTap(check, x, y, 1.4f, 0.4f, 1120);
            cr3UiTint[0] = cr3UiTint[1] = cr3UiTint[2] = 1;
        }
    }
    drawCr3Header(t, 0xb, std::string(), w, h, view);
}

void Viewer::openSpBoxes() {
    vs.screen = 11, vs.since = 0, vs.boxBuy = false, vs.boxPanel = 0, vs.boxCheckFocus = false;
    const std::vector<SpBox> boxes = spBoxes();
    vs.boxFocus = 0;
    for (size_t k = 0; k < boxes.size(); ++k) // (the first Lucky Box in focus where there is one)
        if (boxes[k].kind == 1) {
            vs.boxFocus = int(k);
            break;
        }
    SDL_Log("shop: the boxes - %zu (%d in focus)", boxes.size(), vs.boxFocus);
}

// LuckyBoxMain 4 / 5: yes - the box opened, its price paid, flag 39 set; what came out told
void Viewer::spBoxContentsPage() {
    // comMsgSMenu has three text lines: the page number and two contents.
    const int pages = std::max((int(vs.boxContents.size()) + 1) / 2, 1);
    vs.boxContentsPage = std::clamp(vs.boxContentsPage, 0, pages - 1);
    vs.told = "(" + std::to_string(vs.boxContentsPage + 1) + "/" + std::to_string(pages) + ")";
    for (int k = vs.boxContentsPage * 2; k < int(vs.boxContents.size()) && k < vs.boxContentsPage * 2 + 2; ++k) vs.told += "\\n" + vs.boxContents[size_t(k)];
    vs.toldSince = 0;
    SDL_Log("shop: box contents page %d of %d", vs.boxContentsPage + 1, pages);
}

void Viewer::inspectSpBox() {
    const auto boxes = spBoxes();
    if (vs.boxFocus < 0 || vs.boxFocus >= int(boxes.size()) || !game.cr3Db) return;
    const SpBox& box = boxes[size_t(vs.boxFocus)];
    vs.boxContents.clear(), vs.boxContentsPage = 0;
    if (box.kind == 1 && box.id >= 0 && box.id < int(game.cr3Db->itemSheets().size())) {
        for (const auto& p : game.cr3Db->itemSheets()[size_t(box.id)].entries)
            vs.boxContents.push_back(p.gene ? cr3NameOf("gene_name.mbe", "GeneName", p.item) : cr3ItemText(p.item, false));
    } else if (box.kind == 0) {
        if (const auto* table = game.cr3Db->table("geneboxes"))
            for (const auto& row : table->items) {
                if (row.integer("_id") != box.id) continue;
                if (const auto* baskets = row.get("baskets"))
                    for (const auto& basket : baskets->items)
                        if (const auto* items = basket.get("items"))
                            for (const auto& item : items->items) {
                                const std::string name = cr3NameOf("gene_name.mbe", "GeneName", item.integer("masterId"));
                                if (std::find(vs.boxContents.begin(), vs.boxContents.end(), name) == vs.boxContents.end()) vs.boxContents.push_back(name);
                            }
            }
    }
    if (!vs.boxContents.empty()) spBoxContentsPage();
    SDL_Log("shop: box contents (%zu)", vs.boxContents.size());
}

void Viewer::openSpBox() {
    const std::vector<SpBox> boxes = spBoxes();
    if (vs.boxFocus >= int(boxes.size())) return;
    const SpBox& box = boxes[size_t(vs.boxFocus)];
    const int k = std::clamp(vs.boxPanel, 0, 2), n = std::max(box.amount[k], 1), price = box.price[k];
    int r = -1;
    std::vector<std::string> got;
    if (box.kind == 0) {
        r = game.cr3Db->openGeneBox(game, box.id, n, price);
        if (r == cr3::kDbOk)
            for (const cr3::Cr3Gene& g : game.cr3Db->lastGenes()) got.push_back(cr3NameOf("gene_name.mbe", "GeneName", g.master));
    } else {
        r = box.kind == 1 ? game.cr3Db->openLuckyBox(game, game.cr3Db->itemSheets()[size_t(box.id)].id, n, price) : game.cr3Db->openGPBox(game, n, price);
        if (r == cr3::kDbOk)
            for (size_t i = 0; i < game.cr3Db->opened().size(); ++i)
                if (const cr3::Cr3ItemSheet::Prize* p = game.cr3Db->openedPrize(i))
                    got.push_back(p->gene ? cr3NameOf("gene_name.mbe", "GeneName", p->item) : cr3ItemText(p->item, false));
    }
    SDL_Log("shop: box %d (kind %d) x%d for %d -> %d, %zu out", box.id, box.kind, n, price, r, got.size());
    if (r != cr3::kDbOk) {
        vs.told = menuAnnounce(r == cr3::kDbNoGP ? 0xff : 0x100), vs.toldSince = 0; // (not enough GP / coins)
        return;
    }
    game.setFlag(39, true); // (LuckyBoxMain 5: Cr3FlagWork::set(0x27))
    audio.playSe("com_024", false);
    std::string text;
    for (const std::string& g : got) text += (text.empty() ? "" : "\n") + g;
    vs.told = text.empty() ? menuAnnounce(0xfb) : text, vs.toldSince = 0;
    for (const std::string& g : got) SDL_Log("shop: out of the box: %s", g.c_str());
}

void Viewer::vsList(int mode) {
    vs.mode = mode, vs.rows.clear(), vs.row = 0;
    const auto& ts = game.cr3Db->vsTournaments();
    for (size_t k = 0; k < ts.size(); ++k)
        if (ts[k].mode == mode) vs.rows.push_back(int(k));
    vs.screen = 3, vs.since = 0;
    SDL_Log("vs: %zu %s tournaments", vs.rows.size(), mode ? "special" : "normal");
}

void Viewer::vsDigits(const UiFig& panel, const std::string& prefix, const std::string& fig, long value, int places, const Mat4& view, int offset) {
    cr3UiNumber(panel, prefix, fig, value, places, offset, view);
}

auto Viewer::vsBoostTable() -> std::vector<VsBoost> {
    std::vector<VsBoost> out;
    if (const cr3::BsonValue* t = game.cr3Db ? game.cr3Db->table("vsboostitems") : nullptr)
        for (const cr3::BsonValue& r : t->items)
            out.push_back({r.integer("_id"), r.integer("icon"), r.integer("type"), r.integer("const"), r.integer("price")});
    std::sort(out.begin(), out.end(), [](const VsBoost& a, const VsBoost& b) { return a.id < b.id; });
    return out;
}

std::string Viewer::vsBoostName(int id) {
    static cr3::MbeFile f;
    static int readIn = -1; // (the language it was read in: textGeneration)
    if (readIn != textGeneration) f = cr3::MbeFile(), readIn = textGeneration;
    if (f.tables().empty()) {
        std::vector<uint8_t> d;
        if (archive.read("data\\boost.mbe", d)) f.parse(d);
    }
    cr3::MbeFile::Row r;
    if (!f.find("BoostItemName", id, "ipp", r)) return {};
    return optionLanguage == 0 || r.s(2).empty() ? r.s(1) : r.s(2);
}

// the chosen ones' coins: each not held costs its price (one of each held used)
int Viewer::vsBoostCost() {
    int cost = 0;
    std::map<int, int> held = game.cr3Vs.boosts;
    for (const VsBoost& b : vsBoostTable())
        for (const int c : vs.boosts)
            if (c == b.id) {
                if (held[b.id] > 0) --held[b.id];
                else cost += b.price;
            }
    return cost;
}

int Viewer::vsBoostBonusPercent(int n) {
    static cr3::MbeFile f;
    static int readIn = -1; // (the language it was read in: textGeneration)
    if (readIn != textGeneration) f = cr3::MbeFile(), readIn = textGeneration;
    if (f.tables().empty()) {
        std::vector<uint8_t> d;
        if (archive.read("data\\boost.mbe", d)) f.parse(d);
    }
    cr3::MbeFile::Row r;
    return f.find("BoostItemBonus", n, "ii", r) ? r.i(1) : 0;
}

void Viewer::drawVsBoosts(int w, int h, const Mat4& view) {
    float x, y, cx, cy;
    UiFig& pos = uiFig("vsposition");
    if (!cr3UiCall(pos, "call_VsMainLsideC", x, y, w, h)) return;
    UiFig& l = cr3UiPut("vsmainlsidec", "", 0, x, y, view);
    if (!cr3UiCall(l, "call_VsListItemMenu", x, y)) return;
    UiFig& menu = cr3UiPut("vslistitemmenu", "", 0, x, y, view);
    const std::vector<VsBoost> table = vsBoostTable();
    for (size_t k = 0; k < table.size() && k < 8; ++k) {
        const VsBoost& b = table[k];
        if (cr3UiCall(menu, "call_VsListItemB_fr00_00" + std::to_string(k + 1), cx, cy)) {
            const bool focused = vs.boostFocus == b.id;
            UiFig& item = cr3UiPut("vslistitemb_fr00", "", float(b.id - 1) / 30, cx, cy, view, focused ? 1.0f : 0.85f);
            float ax, ay, ex, ey;
            if (cr3UiCall(item, "tapAreaS01", ax, ay) && cr3UiCall(item, "tapAreaE01", ex, ey)) vsTaps.push_back({ax, ay, ex, ey, 500 + int(k)});
        }
        if (cr3UiCall(menu, "call_VsListStock_00" + std::to_string(k + 1), cx, cy)) {
            UiFig& stock = cr3UiPut("vsliststock", "", 0, cx, cy, view);
            const auto held = game.cr3Vs.boosts.find(b.id);
            const int n = held != game.cr3Vs.boosts.end() ? held->second : 0;
            float tx, ty;
            if (cr3UiCall(stock, "call_VSListTxt_fr00", tx, ty)) cr3UiPut("vslisttxt_fr00", "", n > 0 ? 1.0f / 30 : 0.0f, tx, ty, view);
            // (SetVsListItemBIcon: the count held - digits 10 on -, else the price)
            const int value = n > 0 ? n : b.price;
            const std::string digits = std::to_string(std::clamp(value, 0, 999));
            for (int d = 0; d < int(digits.size()); ++d) {
                float dx, dy;
                if (uiJoint(stock, "VsListNum_fr00_00" + std::to_string(3 - int(digits.size()) + d + 1), dx, dy))
                    cr3UiPut("vslistnum_fr00", "", float(digits[size_t(d)] - '0' + (n > 0 ? 10 : 0)) / 30, dx, dy, view);
            }
        }
    }
    for (size_t k = 0; k < vs.boosts.size() && k < 4; ++k)
        if (cr3UiCall(menu, "call_VsListItemA_fr00_00" + std::to_string(k + 1), cx, cy)) {
            UiFig& item = cr3UiPut("vslistitema_fr00", "", float(vs.boosts[k] - 1) / 30, cx, cy, view);
            float ax, ay, ex, ey;
            if (cr3UiCall(item, "tapAreaS01", ax, ay) && cr3UiCall(item, "tapAreaE01", ex, ey)) vsTaps.push_back({ax, ay, ex, ey, 520 + int(k)});
        }
    if (vs.boostFocus > 0) cr3UiLabel(menu, "typeM_C01", vsBoostName(vs.boostFocus), w, h, 1.0f);
}

// the list screen's footer: the coins the chosen cost (VsFtrList's VsFteCoin, its digits right to left)
void Viewer::drawVsListFooter(int w, int h, const Mat4& view) {
    float x, y, cx, cy;
    UiFig& pos = uiFig("vsposition");
    if (!cr3UiCall(pos, "call_VsFtrMenu", x, y, w, h)) return;
    UiFig& ftr = cr3UiPut("vsftrlist", "", 0, x, y, view);
    if (cr3UiCall(ftr, "call_VsFteCoin", cx, cy)) {
        UiFig& coin = cr3UiPut("vsftecoin", "", 0, cx, cy, view);
        const int cost = vsBoostCost();
        for (int d = 0, v = cost; d < 3; ++d, v /= 10) {
            if (d > 0 && v == 0) break;
            float dx, dy;
            if (cr3UiCall(coin, "call_VsFtrNumL_fr00_0" + std::to_string(d + 1), dx, dy)) cr3UiPut("vsftrnuml_fr00", "", float(v % 10) / 30, dx, dy, view);
        }
    }
}

bool Viewer::vsWildCard(int id, VsWildCard& out) {
    static cr3::MbeFile f;
    static int readIn = -1; // (the language it was read in: textGeneration)
    if (readIn != textGeneration) f = cr3::MbeFile(), readIn = textGeneration;
    if (f.tables().empty()) {
        std::vector<uint8_t> d;
        if (archive.read("data\\wildcard.mbe", d)) f.parse(d);
    }
    cr3::MbeFile::Row r;
    if (!f.find("WildCard", id, "iiiiipiiii", r)) return false;
    out.id = id, out.bonus = r.i(1), out.target = r.i(2), out.effect = r.i(3), out.thisTurn = (r.i(8) & 1) != 0;
    out.constants.clear();
    for (int k = 0; k < r.i(4) && k < 16; ++k) {
        const int32_t c = r.element(5, size_t(k));
        if (c >= 0) out.constants.push_back(c);
    }
    return true;
}

bool Viewer::vsWildPending() const { // (a VS battle's: none chosen yet this battle, and one left)
    if (vs.screen != 5 || !game.cr3Db || vs.wildChosen[std::clamp(vs.battle, 0, 2)] >= 0) return false;
    const std::vector<int>& w = game.cr3Db->wildcards();
    for (size_t k = 0; k < w.size(); ++k)
        if (std::find(std::begin(vs.wildChosen), std::end(vs.wildChosen), int(k)) == std::end(vs.wildChosen)) return true;
    return false;
}

void Viewer::drawVsWildCards(int w, int h) {
    if (!vs.wildOpen || !game.cr3Db) return;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    vsTaps.clear();
    // (OnOpen: the alert - Cr3UtilGetAnnounceData 0x31, AnnounceBattle's - under the cards)
    drawCr3Told(vsText("AnnounceBattle", 0x31), uiClock - vs.wildAt, -2, w, h, view, &vsTaps);
    dev->clearDepth();
    UiFig& null = cr3UiPut("vswildnull", "vswildnull_in", std::min(uiClock - vs.wildAt, 1.0f), 0, 0, view);
    const std::vector<int>& picked = game.cr3Db->wildcards();
    for (size_t k = 0; k < picked.size() && k < 5; ++k) {
        float x, y;
        if (!uiJoint(null, "VsWildTouch_00" + std::to_string(k + 1), x, y)) continue;
        const bool used = std::find(std::begin(vs.wildChosen), std::end(vs.wildChosen), int(k)) != std::end(vs.wildChosen);
        cr3UiScale = vitaButtons() && int(k) == vsWildFocus ? 1.08f : 1;
        UiFig& touch = cr3UiPut("vswildtouch", "", 0, x, y, view, used ? 0.4f : 1.0f);
        cr3UiScale = 1;
        float cx, cy;
        if (cr3UiCall(touch, "call_GeneCard", cx, cy)) {
            UiFig& card = uiFig("w_card_default");
            char tex[16];
            std::snprintf(tex, sizeof tex, "w_card_%02d", picked[k]);
            cr3UiMaterial(card, "picture", tex);
            card.fig.place = cr3::Mat34();
            card.fig.place.m[0] = card.fig.place.m[5] = card.fig.place.m[10] = 4.6f * (vitaButtons() && int(k) == vsWildFocus ? 1.08f : 1); // (its touch area's size)
            card.fig.place.m[3] = cx, card.fig.place.m[7] = cy, card.fig.place.m[11] = 0.5f;
            poseUi(card, "w_card_default", 0);
            card.fig.tint[3] = used ? 0.4f : 1.0f;
            drawFigure(card.fig, view.data());
        }
        VsWildCard wc;
        if (vsWildCard(picked[k], wc) && cr3UiCall(touch, "call_VsWildBonus", cx, cy)) {
            UiFig& bonus = cr3UiPut("vswildbonus", "", 0, cx, cy, view, used ? 0.4f : 1.0f);
            for (int d = 0, v = wc.bonus; d < 2; ++d, v /= 10) {
                if (d > 0 && v == 0) break;
                float dx, dy;
                if (cr3UiCall(bonus, "call_VsFtrNumL_fr00_00" + std::to_string(d + 1), dx, dy)) cr3UiPut("vsftrnuml_fr00", "", float(v % 10) / 30, dx, dy, view);
            }
        }
        float ax, ay, ex, ey;
        if (!used && cr3UiCall(touch, "tapAreaS01", ax, ay) && cr3UiCall(touch, "tapAreaE01", ex, ey)) vsTaps.push_back({ax, ay, ex, ey, 800 + int(k)});
    }
}

// a wildcard chosen (WildCardMenu::TouchGestureTap - BtlWildCard::Select), then the battle's
// boosts and wildcards applied (Apply)
void Viewer::chooseVsWildCard(int k) {
    if (!game.cr3Db || !vs.wildOpen) return;
    const std::vector<int>& picked = game.cr3Db->wildcards();
    if (k < 0 || k >= int(picked.size()) || std::find(std::begin(vs.wildChosen), std::end(vs.wildChosen), k) != std::end(vs.wildChosen)) {
        audio.playSe("com_004", false);
        SDL_Log("vs: wildcard %d unavailable", k);
        return;
    }
    vs.wildChosen[std::clamp(vs.battle, 0, 2)] = k;
    vs.wildOpen = false;
    audio.playSe("com_003", false);
    SDL_Log("vs: wildcard %d chosen (battle %d)", picked[size_t(k)], vs.battle + 1);
    applyVsWildCard(std::clamp(vs.battle, 0, 2));
}

void Viewer::applyVsWildCard(int slot) {
    const std::vector<int>& picked = game.cr3Db->wildcards();
    const int k = vs.wildChosen[slot];
    VsWildCard wc;
    if (k < 0 || k >= int(picked.size()) || !vsWildCard(picked[size_t(k)], wc)) return;
    if (wc.thisTurn && slot != std::clamp(vs.battle, 0, 2)) return;
    std::vector<int> targets;
    for (size_t f = 0; f < cr3Rules.fighters.size(); ++f) {
        const cr3::Cr3Fighter& ft = cr3Rules.fighters[f];
        bool hit = wc.target == 3 || (wc.target == 1 && ft.player) || (wc.target == 2 && !ft.player);
        if ((wc.target == 4 || wc.target == 5) && ft.player && cr3TablesReady()) {
            cr3::MbeFile::Row r;
            if (cr3BattleTables.character.find("Char", ft.id, "iiii", r)) hit = (r.i(3) >> (wc.target == 4 ? 1 : 2) & 1) != 0;
        }
        if (hit) targets.push_back(int(f));
    }
    for (const int c : wc.constants) {
        if (wc.effect == 1)
            for (const int f : targets) cr3Rules.giveEffect(f, c);
        if (wc.effect == 2) cr3Rules.gauge = std::clamp(c, -1000, 1000);
        if (wc.effect == 3)
            for (const int f : targets) cr3Rules.fighters[size_t(f)].hp = std::max(cr3Rules.fighters[size_t(f)].maxHp * c / 100, 1);
    }
    for (const int f : targets) cr3HpShown[size_t(f)] = cr3Rules.fighters[size_t(f)].hp;
}

// a VS battle's start: the lifeline items on the party, the wildcards chosen before that last
void Viewer::applyVsBattleStart() {
    if (vs.screen != 5) return;
    for (const VsBoost& b : vsBoostTable()) {
        if (std::find(vs.boosts.begin(), vs.boosts.end(), b.id) == vs.boosts.end() || b.type != 0) continue;
        for (size_t f = 0; f < cr3Rules.fighters.size(); ++f)
            if (cr3Rules.fighters[f].player) cr3Rules.giveEffect(int(f), b.constant);
    }
    for (int slot = 0; slot < 3; ++slot)
        if (slot != vs.battle) applyVsWildCard(slot);
    vs.wildOpen = vsWildPending(), vs.wildAt = uiClock, vsWildFocus = 0;
    if (vs.wildOpen) {
        audio.playSe("bt_602", false);
        SDL_Log("vs: wildcards up");
    }
}

// the score's bonuses (phaseColosseumResult): MP used, lifeline items, wildcards
void Viewer::vsScoreBonuses() {
    int itemBonus = 0;
    for (const cr3::Cr3Fighter& f : cr3Rules.fighters)
        if (f.player) itemBonus = std::max(itemBonus, cr3Rules.effectSum(f, 130));
    vs.itemBonus = (vsBoostBonusPercent(int(vs.boosts.size())) + itemBonus) * vs.score / 100;
    int wild = 0;
    const std::vector<int>& picked = game.cr3Db->wildcards();
    for (const int k : vs.wildChosen) {
        VsWildCard wc;
        if (k >= 0 && k < int(picked.size()) && vsWildCard(picked[size_t(k)], wc)) wild += wc.bonus;
    }
    vs.wildBonus = wild * vs.score / 100;
}

auto Viewer::vsShopGoods() -> std::vector<VsGoods> {
    std::vector<VsGoods> out;
    const cr3::BsonValue* t = game.cr3Db ? game.cr3Db->table("shops") : nullptr;
    if (!t) return out;
    for (const cr3::BsonValue& shop : t->items) {
        if (shop.string("_id") != "com_1") continue;
        if (const cr3::BsonValue* goods = shop.get("goods"))
            for (const cr3::BsonValue& g : goods->items) {
                VsGoods v;
                v.id = g.integer("_id"), v.icon = g.integer("icon"), v.gold = g.integer("gold"), v.gp = g.integer("GP"), v.coin = g.integer("coin");
                v.flag = g.integer("flag", -1);
                if (const cr3::BsonValue* items = g.get("items"); items && !items->items.empty())
                    v.item = items->items[0].integer("item_id"), v.count = items->items[0].integer("count");
                if (v.flag < 0 || game.flag(v.flag)) out.push_back(v);
            }
    }
    return out;
}

bool Viewer::keyGoods(VsGoods& out) {
    const int key = game.cr3Db ? game.cr3Db->constant("GOLDKEY_MASTER_ID", 400005) : 400005;
    for (const VsGoods& g : vsShopGoods())
        if (g.item == key) return out = g, true;
    return false;
}

int Viewer::keysRoom() const {
    const int key = game.cr3Db ? game.cr3Db->constant("GOLDKEY_MASTER_ID", 400005) : 400005;
    const auto held = game.cr3Items.find(key);
    return std::max(99 - (held != game.cr3Items.end() ? held->second : 0), 0);
}

void Viewer::stepKeyBuy(float dt) {
    cr3::EventRuntime* ev = activeEvents();
    const bool shown = ev && ev->keyBuyShown();
    if (shown && !keyBuy.wasShown) keyBuy = Cr3KeyBuy{1, 0, 1, true};
    if (!shown) keyBuy.stage = 0;
    keyBuy.wasShown = shown;
    if (keyBuy.stage) keyBuy.since += dt;
    if (keyBuy.stage && !inputScript.empty() && keyBuy.since > 0.5f) { // (--input key:yes / key:no / key:<n>)
        const std::string t = inputScript.front();
        if (t == "key:yes") inputScript.pop_front(), chooseKeyBuy(kTapKeyYes);
        else if (t == "key:no") inputScript.pop_front(), chooseKeyBuy(kTapKeyNo);
        else if (t.compare(0, 4, "key:") == 0 && keyBuy.stage == 2) inputScript.pop_front(), keyBuy.amount = std::clamp(std::atoi(t.c_str() + 4), 0, 99);
    }
}

void Viewer::chooseKeyBuy(int id) {
    cr3::EventRuntime* ev = activeEvents();
    if (!ev || !keyBuy.stage) return;
    VsGoods g;
    const bool have = keyGoods(g);
    if (id >= kTapKeyReel && id < kTapKeyReel + 4) { // (a reel: tens or ones, up or down)
        const int step = (id - kTapKeyReel) / 2 == 0 ? 10 : 1;
        keyBuy.amount = std::clamp(keyBuy.amount + ((id - kTapKeyReel) % 2 == 0 ? step : -step), 0, 99);
        audio.playSe("com_002", false);
        return;
    }
    if (id == kTapKeyNo) {
        audio.playSe("com_004", false);
        ev->answerKeyBuy();
        return;
    }
    if (id != kTapKeyYes) return;
    if (keyBuy.stage == 1) {
        audio.playSe("com_003", false);
        keyBuy.stage = 2, keyBuy.since = 0, keyBuy.amount = 1;
        return;
    }
    const int n = keyBuy.amount;
    if (!have || n < 1 || n > keysRoom() || long(g.coin) * n > game.cr3Coins) return void(audio.playSe("com_004", false));
    const int shop = game.cr3Db ? game.cr3Db->findShop("com_1") : -1;
    const int r = shop >= 0 ? game.cr3Db->buyGoods(game, shop, g.id, n) : -1;
    if (r == cr3::kDbOk) audio.playSe("com_024", false);
    SDL_Log("key shop: %d gold keys -> %d", n, r);
    ev->answerKeyBuy();
}

void Viewer::drawKeyBuy(int w, int h) {
    keyBuyTaps.clear();
    if (!keyBuy.stage) return;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    VsGoods g;
    const bool have = keyGoods(g);
    const std::string name = cr3ItemText(have ? g.item : 400005, false);
    if (keyBuy.stage == 1) {
        std::string ask = menuAnnounce(0x23b);
        for (const auto& [key, value] : {std::pair<const char*, std::string>{"{itemname}", name}, {"{coin}", std::to_string(game.cr3Coins)}})
            if (const size_t at = ask.find(key); at != std::string::npos) ask.replace(at, std::strlen(key), value);
        drawCr3Asked(ask, keyBuy.since, 0, true, kTapKeyYes, kTapKeyNo, w, h, view, &keyBuyTaps);
        return;
    }
    float x, y, cx, cy, ax, ay, bx, by;
    UiFig& pos = cr3UiPut("camitemposition", "camitemposition_io", keyBuy.since, 0, 0, view);
    if (cr3UiCall(pos, "ragdoll_call_camItemReelMenu", x, y, w, h)) {
        UiFig& reel = uiFig("camitemreelmenu");
        reel.fig.place = cr3::Mat34();
        reel.fig.place.m[3] = x, reel.fig.place.m[7] = y;
        poseUi(reel, "camitemreelmenu", 0);
        reel.fig.tint[3] = 1;
        const int digits[2] = {keyBuy.amount / 10 % 10, keyBuy.amount % 10};
        for (int c = 0; c < 2; ++c) { // (the reels' numbers: their UV scrolled to the digit)
            float v = 0.14f * float(2 - digits[c]);
            while (v > 0.7f) v -= 1.4f;
            while (v <= -0.7f) v += 1.4f;
            const std::string a = "mat_num_0" + std::to_string(1 + 2 * c), b = "mat_num_0" + std::to_string(2 + 2 * c);
            for (ViewMesh& vm : reel.fig.meshes) {
                const uint32_t mi = vm.source.material;
                if (mi >= reel.materials.size()) continue;
                if (reel.materials[mi] == a) vm.material.params[0x5e] = {0.0f, v};
                if (reel.materials[mi] == b) vm.material.params[0x5e] = {0.13f, v < 0 ? v + 0.7f : v - 0.7f};
            }
        }
        drawFigure(reel.fig, view.data());
        for (int c = 0; c < 2; ++c) {
            if (!cr3UiCall(reel, "tapAreaS0" + std::to_string(c + 1), ax, ay) || !cr3UiCall(reel, "tapAreaE0" + std::to_string(c + 1), bx, by)) continue;
            const float mid = (ay + by) / 2;
            keyBuyTaps.push_back({ax, ay, bx, mid, kTapKeyReel + 2 * c});
            keyBuyTaps.push_back({ax, mid, bx, by, kTapKeyReel + 1 + 2 * c});
        }
    }
    if (cr3UiCall(pos, "ragdoll_call_camItemStockPriceMenu", x, y, w, h)) {
        UiFig& sp = cr3UiPut("camitemstockpricemenu", "", 0, x, y, view);
        if (cr3UiCall(sp, "call_camItemPrice_num00", cx, cy))
            cr3UiDigits(cr3UiPut("camitemprice_num00", "", 0, cx, cy, view), "call_camItemNum_fr00", "camitemnum_fr00", long(g.coin) * keyBuy.amount, 7, 0, view);
        if (cr3UiCall(sp, "call_camItemStock_num00", cx, cy))
            cr3UiDigits(cr3UiPut("camitemstock_num00", "", 0, cx, cy, view), "call_camItemNum_fr00", "camitemnum_fr00", 99 - keysRoom(), 3, 0, view);
    }
    std::string ask = menuAnnounce(0x248);
    for (const auto& [key, value] : {std::pair<const char*, std::string>{"{itemname}", name}, {"{currency}", menuAnnounce(0x249)},
                                     {"{num}", std::to_string(keyBuy.amount)}, {"{price}", std::to_string(long(g.coin) * keyBuy.amount)}})
        if (const size_t at = ask.find(key); at != std::string::npos) ask.replace(at, std::strlen(key), value);
    const bool yesOn = have && keyBuy.amount >= 1 && keyBuy.amount <= keysRoom() && long(g.coin) * keyBuy.amount <= game.cr3Coins;
    drawCr3Asked(ask, keyBuy.since, 0, yesOn, kTapKeyYes, kTapKeyNo, w, h, view, &keyBuyTaps);
}

bool Viewer::keyBuyClick(float fx, float fy) {
    if (!keyBuy.stage) return false;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    for (auto it = keyBuyTaps.rbegin(); it != keyBuyTaps.rend(); ++it)
        if (ux >= std::min(it->x0, it->x1) && ux <= std::max(it->x0, it->x1) && uy >= std::min(it->y0, it->y1) && uy <= std::max(it->y0, it->y1)) {
            chooseKeyBuy(it->id);
            break;
        }
    return true;
}

void Viewer::drawVsShop(int w, int h, const Mat4& view) {
    float x, y, cx, cy;
    const std::vector<VsGoods> goods = vsShopGoods();
    UiFig& lp = cr3UiPut("comlistposition", "comlistposition_io", vs.since, 0, 0, view);
    if (cr3UiCall(lp, "call_comListMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("comlistmenu", "", 0, x, y, view);
        float lx, ly;
        if (cr3UiCall(menu, "call_comListPanel", lx, ly)) {
            const int n = int(goods.size());
            const int top = std::clamp(vs.row - 2, 0, std::max(n - 5, 0));
            drawCr3ListBar(menu, false, top, n, 5, view);
            for (int k = top; k < n && k < top + 5; ++k) {
                const VsGoods& g = goods[size_t(k)];
                Cr3FocusRow focusRow(*this, k == vs.row);
                UiFig& panel = cr3UiPut("comlistpanel", "", 0, cr3ListRowX(lx, k, vs.row), cr3ListRowY(ly, k, top, vs.row), view);
                if (k == vs.row && cr3UiCall(panel, "call_comListCursor", cx, cy)) cr3UiPut("comlistcursor", "", 0, cx, cy, view);
                if (cr3UiCall(panel, "call_comListIcon_fr00", cx, cy)) cr3UiPut("comlisticon_fr00", "", cr3UiIcon(uiFig("comlisticon_fr00"), g.icon), cx, cy, view);
                cr3UiLabel(panel, "typeM_L01", cr3ItemText(g.item, false) + (g.count > 1 ? " x" + std::to_string(g.count) : std::string()), w, h, 0.3f);
                if (cr3UiCall(panel, "call_comListNumUn_num00", cx, cy)) {
                    UiFig& num = cr3UiPut("comlistnumun_num00", "", 0, cx, cy, view);
                    float ix, iy;
                    const int unit = g.coin > 0 ? 0 : g.gp > 0 ? 1 : 2;
                    if (cr3UiCall(num, "call_comListUnIcon_fr00", ix, iy)) cr3UiPut("comlistunicon_fr00", "", float(unit) / 30, ix, iy, view);
                    cr3UiNumber(num, "call_comListNum_fr00", "comlistnum_fr00", g.coin > 0 ? g.coin : g.gp > 0 ? g.gp : g.gold, 7, 30, view);
                }
                float ax, ay, bx, by;
                if (cr3UiCall(panel, "tapAreaS01", ax, ay) && cr3UiCall(panel, "tapAreaE01", bx, by)) vsTaps.push_back({ax, ay, bx, by, 700 + k});
            }
        }
    }
    drawCr3Header(vs.since, 9, vs.row < int(goods.size()) ? cr3ItemText(goods[size_t(vs.row)].item, true) : std::string(), w, h, view);
}

// the tournament's results (VSMain 0xb: VsAchieveProductionMenu - VsResultMenu at VsPosition's
// call_VsResultMenu: the tournament's score in VsResultNumL_fr00's seven, the week's in
// VsResultNumMPanel's nine, how far the next of vsscore's totals is in VsResultNumSPanel's seven
// and the bar from the last; VsResultDayPanel - the day's mission (vsmission_name, its goal for
// {score}) at typeM_L01, its tally of the goal on VsResultbar_per, VsResultComplete once done;
// VsResultMonthPanel - the season's); a tap: the prizes told, back to Battle's screen
void Viewer::drawVsResults(int w, int h, const Mat4& view) {
    float x, y, cx, cy;
    UiFig& pos = uiFig("vsposition");
    if (!cr3UiCall(pos, "call_VsResultMenu", x, y, w, h)) return;
    dev->clearDepth();
    UiFig& menu = cr3UiPut("vsresultmenu", "", 0, x, y, view);
    const cr3::Cr3Vs& v = game.cr3Vs;
    const cr3::Cr3OfflineDb::VsRewards& rw = game.cr3Db->vsRewards();
    auto digits = [&](const UiFig& panel, const char* prefix, const char* fig, long value, int places) {
        for (int d = 0; d < places; ++d, value /= 10) { // (_001 the ones)
            if (d > 0 && value == 0) break;
            float dx, dy;
            char j[48];
            std::snprintf(j, sizeof j, "%s_%03d", prefix, d + 1);
            if (cr3UiCall(panel, j, dx, dy)) cr3UiPut(fig, "", float(value % 10) / 30, dx, dy, view);
        }
    };
    if (cr3UiCall(menu, "call_VsResultNumL_fr00", cx, cy)) digits(cr3UiPut("vsresultnumlpanel", "", 0, cx, cy, view), "call_VsResultNumL_fr00", "vsresultnuml_fr00", vs.endScore, 7);
    if (cr3UiCall(menu, "call_VsResultNumMPanel", cx, cy)) digits(cr3UiPut("vsresultnummpanel", "", 0, cx, cy, view), "call_VsResultNumM_fr00", "vsresultnumm_fr00", v.weekScore, 9);
    // (the week's score prizes: vsscore's totals - the next one's distance, the bar from the last)
    long last = 0, next = 0;
    if (const cr3::BsonValue* sc = game.cr3Db->table("vsscore")) {
        if (sc->isArray()) sc = sc->items.empty() ? nullptr : &sc->items[0];
        if (const cr3::BsonValue* totals = sc ? sc->get("totals") : nullptr)
            for (const cr3::BsonValue& t : totals->items) {
                if (t.asInt() <= v.weekScore) last = t.asInt();
                else if (!next) next = t.asInt();
            }
    }
    if (cr3UiCall(menu, "call_VsResultNumSPanel", cx, cy) && next > 0)
        digits(cr3UiPut("vsresultnumspanel", "", 0, cx, cy, view), "call_VsResultNumS_fr00", "vsresultnums_fr00", next - v.weekScore, 7);
    if (next > last && cr3UiCall(menu, "ca_VsResult_bar_per_001", cx, cy))
        cr3UiPut("vsresultbar_per", "", float((v.weekScore - last) * 100 / (next - last)) / 30, cx, cy, view);
    UiFig& day = cr3UiPut("vsresultdaypanel", "", 0, x, y, view);
    std::string mission = vsText("vsmission_name", v.mission);
    if (const size_t at = mission.find("{score}"); at != std::string::npos) mission.replace(at, 7, std::to_string(v.missionGoal));
    cr3UiLabel(day, "typeM_L01", mission, w, h, 1.0f);
    if (cr3UiCall(day, "ca_VsResult_bar_per_002", cx, cy))
        cr3UiPut("vsresultbar_per", "", float(v.missionGoal > 0 ? std::min(v.missionTotal * 100 / v.missionGoal, 100) : 0) / 30, cx, cy, view);
    if (v.missionCleared && cr3UiCall(day, "call_VsResultComplete_002", cx, cy)) cr3UiPut("vsresultcomplete", "", 0, cx, cy, view);
    // (the seasonal event's: its points - shown while a season holds)
    if (cr3UiCall(menu, "call_VsResultMonthPanel", cx, cy)) {
        UiFig& month = cr3UiPut("vsresultmonthpanel", "", 0, cx, cy, view);
        float mx, my;
        if (v.season >= 0 && cr3UiCall(month, "call_VsResultMonth_fr00", mx, my)) cr3UiPut("vsresultmonth_fr00", "", float(v.season) / 30, mx, my, view);
    }
    vsTaps.push_back({-6, -4, 6, 4, 301});
}

void Viewer::drawVs(int w, int h) {
    vsTaps.clear();
    if (cr3Camp.stage != 0) return; // (the camp opened from Battle's screen over it - My Page too)
    if (areaEvents && areaEvents->geneLab().open) return; // (SetAnotherScreenVsModeMenu: the lab over it)
    if (vs.screen == 0 || vs.screen == 5 || vs.screen == 6) return;
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    const float t = vs.since;
    float x, y, cx, cy;
    if (vs.screen >= 10 && vs.screen <= 12) {
        drawSpShop(w, h, view);
        UiFig& foot = cr3UiPut("comftrposition", "", 0, 0, 0, view);
        if (cr3UiCall(foot, "call_comBackMenu_001", x, y, w, h)) {
            UiFig& menu = cr3UiPut("combackmenu", "", 0, x, y, view);
            float ax, ay, ex, ey;
            if (cr3UiCall(menu, "call_comBack_fr00", cx, cy)) cr3UiPut("comback_fr00", "", 0, cx, cy, view);
            if (cr3UiCall(menu, "tapAreaS01", ax, ay) && cr3UiCall(menu, "tapAreaE01", ex, ey)) vsTaps.push_back({ax, ay, ex, ey, -1});
        }
        if (vs.ask) drawCr3Asked(menuAnnounce(vs.ask == 5 ? 0x316 : 0x123), vs.since, 0, true, 600, 601, w, h, view, &vsTaps);
        if (!vs.told.empty()) drawCr3Told(vs.told, vs.toldSince, 602, w, h, view, &vsTaps);
        return;
    }
    UiFig& pos = cr3UiPut("vsposition", "", 0, 0, 0, view);
    if (cr3UiCall(pos, "call_VsMainBlack", x, y, w, h)) cr3UiPut("vsmainblack", "", 0, x, y, view);
    dev->clearDepth();
    // the header
    if (cr3UiCall(pos, "call_VsHeMenu", x, y, w, h)) {
        UiFig& he = cr3UiPut("vshemenu", "", 0, x, y, view);
        if (cr3UiCall(he, "call_VSHeInfoAMenu", cx, cy)) {
            UiFig& info = cr3UiPut("vsheinfoamenu", "", 0, cx, cy, view);
            float px, py;
            if (cr3UiCall(info, "call_cVsHeCoinPanel", px, py)) vsDigits(cr3UiPut("vshecoinpanel", "", 0, px, py, view), "call_VsHeNum_fr00", "vshenum_fr00", game.cr3Coins, 5, view);
            if (cr3UiCall(info, "call_VsHeMlPanel", px, py)) vsDigits(cr3UiPut("vshemlpanel", "", 0, px, py, view), "call_VsHeNum_fr00", "vshenum_fr00", game.cr3Money, 7, view);
        }
        const char* title = vs.screen == 1 ? "VS" : vs.screen == 2 ? "Battle" : nullptr;
        (void)title;
    }
    // the footer: the week's score, the tournament's (the list's: the chosen lifeline items' coins)
    if (vs.screen == 3 || vs.screen == 4) drawVsListFooter(w, h, view);
    else if (vs.screen != 8 && cr3UiCall(pos, "call_VsFtrMenu", x, y, w, h)) {
        UiFig& ftr = cr3UiPut("vsftrmenu", "", 0, x, y, view);
        if (cr3UiCall(ftr, "call_VsFtrTop", cx, cy)) {
            UiFig& top = cr3UiPut("vsftrtop", "", 0, cx, cy, view);
            float px, py;
            // (VsModeMenu::SetTotalScore / SetNowScore: the week's in ten, the tournament's in seven - _01 ..)
            if (cr3UiCall(top, "call_VsFteTotal", px, py))
                cr3UiNumber(cr3UiPut("vsftetotal", "", 0, px, py, view), "call_VsFtrNumL_fr00", "vsftrnuml_fr00", game.cr3Vs.weekScore, 10, 0, view, 1, -1, 2);
            if (cr3UiCall(top, "call_VsFtrNow", px, py))
                cr3UiNumber(cr3UiPut("vsftrnow", "", 0, px, py, view), "call_VsFtrNumS_fr00", "vsftrnums_fr00", game.cr3Vs.score, 7, 0, view, 1, -1, 2);
        }
    }
    const auto& ts = game.cr3Db->vsTournaments();
    if (vs.screen == 1) {
        if (cr3UiCall(pos, "call_VsMainLsideA", x, y, w, h)) {
            UiFig& l = cr3UiPut("vsmainlsidea", "", 0, x, y, view);
            if (cr3UiCall(l, "call_VsMainBanner", cx, cy)) cr3UiPut("vsmainbanner", "", 0, cx, cy, view);
            if (cr3UiCall(l, "call_VsMainTeLv", cx, cy)) cr3UiPut("vsmaintelv", "", 0, cx, cy, view);
        }
        if (cr3UiCall(pos, "call_VsMainRsideA", x, y, w, h)) {
            UiFig& r = cr3UiPut("vsmainrsidea", "", 0, x, y, view);
            for (const auto& [joint, fig, id] : {std::tuple<const char*, const char*, int>{"call_VsMainBtn01_fr00", "vsmainbtn01_fr00", 1},
                                                 {"call_VsMainBtn02_fr00", "vsmainbtn02_fr00", 2}}) {
                if (!cr3UiCall(r, joint, cx, cy)) continue;
                UiFig& b = cr3UiPut(fig, "", vitaButtons() && vs.buttonFocus == id - 1 ? 1.0f / 30 : 0, cx, cy, view);
                vsTaps.push_back({cx - 1.4f, cy - 0.45f, cx + 1.4f, cy + 0.45f, id}); // (its face: ProvisionalCheckHitTapCollision on the figure)
                (void)b;
            }
            for (int k = 0; k < 4; ++k) {
                if (!uiJoint(r, "VsMainBtn05_fr00_00" + std::to_string(k + 1), cx, cy)) continue;
                cr3UiPut("vsmainbtn05_fr00", "", float(2 * k + (vitaButtons() && vs.buttonFocus == k + 2 ? 1 : 0)) / 30, cx, cy, view);
                vsTaps.push_back({cx - 0.1f, cy + 0.1f, cx + 1.9f, cy - 0.9f, 10 + k});
            }
            if (cr3UiCall(r, "call_VsMainChr", cx, cy)) cr3UiPut("vsmainchr", "", 0, cx, cy, view);
        }
    } else if (vs.screen == 2) {
        if (cr3UiCall(pos, "call_VsMainLsideB", x, y, w, h)) {
            UiFig& l = cr3UiPut("vsmainlsideb", "", 0, x, y, view);
            const std::vector<int> members = cr3PartyMembers();
            int k = 0;
            for (const std::string& j : l.joints) {
                if (j.compare(0, 16, "call_VsMainParty") != 0 || !uiJoint(l, j, cx, cy)) continue;
                UiFig& party = cr3UiPut("vsmainparty", "", 0, cx, cy, view);
                if (k < int(members.size())) {
                    const int gk = game.wornGene(members[size_t(k)]);
                    float gx, gy;
                    if (gk >= 0 && cr3UiCall(party, "call_VsMainPC", gx, gy)) { // (VsModeCharaPanel::Pose: the card - 4.6 x - at call_VsMainPC, the face at call_GeneCard)
                        dev->clearDepth();
                        drawCr3Card(game.cr3Genes[size_t(gk)].master, gx, gy, 1.0f, 4.6f, view);
                    }
                    if (cr3UiCall(party, "call_GeneCard", gx, gy)) cr3UiPut("vsmainpc", "", float(members[size_t(k)] - 1) / 30, gx, gy, view);
                    if (gk >= 0 && game.cr3Db) {
                        const cr3::Cr3Gene& g = game.cr3Genes[size_t(gk)];
                        const std::array<int, 6> st = game.cr3Db->geneStats(g);
                        for (const auto& [joint, fig, value, places] :
                             {std::tuple<const char*, const char*, int, int>{"call_camTopLv_num00", "camtoplv_num00", game.cr3Db->geneLevel(g), 3},
                              {"call_camTopHp_num00", "camtophp_num00", st[0], 5}, {"call_camTopMp_num00", "camtopmp_num00", st[1], 4}}) {
                            if (!cr3UiCall(party, joint, gx, gy)) continue;
                            UiFig& num = cr3UiPut(fig, "", 0, gx, gy, view);
                            const bool lv = std::strcmp(fig, "camtoplv_num00") == 0;
                            cr3UiNumber(num, lv ? "call_camTopNumLv_fr00" : "call_camTopNum_fr00", lv ? "camtopnumlv_fr00" : "camtopnum_fr00", value, places, 0, view);
                        }
                    }
                }
                ++k;
            }
        }
        if (cr3UiCall(pos, "call_VsMainRsideB", x, y, w, h)) {
            UiFig& r = cr3UiPut("vsmainrsideb", "", 0, x, y, view);
            for (const auto& [joint, fig, id] : {std::tuple<const char*, const char*, int>{"call_VsMainBtn03_fr00", "vsmainbtn03_fr00", 3},
                                                 {"call_VsMainBtn04_fr00", "vsmainbtn04_fr00", 4}}) {
                if (!cr3UiCall(r, joint, cx, cy)) continue;
                cr3UiPut(fig, "", vitaButtons() && vs.buttonFocus == id - 3 ? 1.0f / 30 : 0, cx, cy, view);
                vsTaps.push_back({cx - 1.4f, cy - 0.45f, cx + 1.4f, cy + 0.45f, id});
            }
            // (VsMainBtn05_fr00 x 4 - at frame 0, 2, 4, 6: the gene lab, the camp, MP recovery,
            // My Page - VsModeMenu's taps 9 .. 0xc)
            for (int k = 0; k < 4; ++k) {
                if (!uiJoint(r, "VsMainBtn05_fr00_00" + std::to_string(k + 1), cx, cy)) continue;
                cr3UiPut("vsmainbtn05_fr00", "", float(2 * k + (vitaButtons() && vs.buttonFocus == k + 2 ? 1 : 0)) / 30, cx, cy, view);
                vsTaps.push_back({cx - 0.1f, cy + 0.1f, cx + 1.9f, cy - 0.9f, 10 + k});
            }
            if (cr3UiCall(r, "call_VsMainChr", cx, cy)) cr3UiPut("vsmainchr", "", 0, cx, cy, view);
        }
    } else if (vs.screen == 3 || vs.screen == 4) {
        if (vs.screen == 3) drawVsBoosts(w, h, view);
        if (cr3UiCall(pos, "call_VsMainRsideC", x, y, w, h)) {
            UiFig& r = cr3UiPut("vsmainrsidec", "", 0, x, y, view);
            if (cr3UiCall(r, "call_VsListTitel", cx, cy)) cr3UiPut("vslisttitel", "", 0, cx, cy, view);
            float lx, ly;
            if (cr3UiCall(r, "call_VsListList", lx, ly)) {
                const int top = std::clamp(vs.row - 1, 0, std::max(int(vs.rows.size()) - 3, 0));
                for (int k = top; k < int(vs.rows.size()) && k < top + 3; ++k) {
                    const cr3::Cr3OfflineDb::VsTournament& tt = ts[size_t(vs.rows[size_t(k)])];
                    const float ry = ly - 1.4f * float(k - top);
                    UiFig& row = cr3UiPut("vslistlist", "", 0, lx + (k == vs.row ? 0.05f : 0.0f), ry, view, k == vs.row ? 1.0f : 0.8f);
                    float bx, by;
                    if (cr3UiCall(row, "call_VsListBoss_fr00", bx, by)) cr3UiPut("vslistboss_fr00", "", float(tt.icon) / 30, bx, by, view);
                    if (tt.cleared && cr3UiCall(row, "call_VsListClear", bx, by)) cr3UiPut("vslistclear", "", 0, bx, by, view);
                    cr3UiLabel(row, "typeM_L00", vsText("VSmode_name", tt.id), w, h, 1.0f);
                    float rx, ry2; // (an R joint: the text's right edge - measured off the screen first)
                    if (cr3UiCall(row, "typeM_R00", rx, ry2)) {
                        dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
                        const std::string lv = "Lv " + std::to_string(tt.level);
                        const float tw = drawUiText(lv, 26, 100.0f, 100.0f, w, h, true, 1.0f);
                        drawUiText(lv, 26, (rx - 0.075f) / 10 - tw, (ry2 - 0.15f) / 10, w, h, true, 1.0f);
                    }
                    float ax, ay, ex, ey;
                    if (cr3UiCall(row, "tapAreaS01", ax, ay) && cr3UiCall(row, "tapAreaE01", ex, ey)) vsTaps.push_back({ax, ay, ex, ey, 100 + k});
                }
            }
        }
        if (vs.screen == 4 && vs.chosen >= 0 && cr3UiCall(pos, "call_VsContMenu", x, y, w, h)) {
            dev->clearDepth();
            const cr3::Cr3OfflineDb::VsTournament& tt = ts[size_t(vs.chosen)];
            UiFig& cm = cr3UiPut("vscontmenu", "", 0, x, y, view);
            if (cr3UiCall(cm, "call_VsContBase", cx, cy)) {
                UiFig& base = cr3UiPut("vscontbase", "", 0, cx, cy, view);
                cr3UiLabel(base, "typeM_C01", vsText("VSmode_name", tt.id), w, h, 1.0f);
                float bx, by;
                if (cr3UiCall(base, "call_VsContTxt", bx, by)) {
                    UiFig& txt = cr3UiPut("vsconttxt", "", 0, bx, by, view);
                    const std::string help = vsText("VSmode_help", tt.id);
                    size_t from = 0;
                    for (int line = 1; line <= 10 && from <= help.size(); ++line) {
                        const size_t at = help.find("\\n", from);
                        char j[24];
                        std::snprintf(j, sizeof j, "typeM_L01_%03d", line);
                        cr3UiLabel(txt, j, help.substr(from, at == std::string::npos ? std::string::npos : at - from), w, h, 1.0f);
                        if (at == std::string::npos) break;
                        from = at + 2;
                    }
                }
                if (cr3UiCall(base, "call_VsContItem_fr00", bx, by)) cr3UiPut("vscontitem_fr00", "", float(tt.prizeIcon) / 30, bx, by, view);
                if (cr3UiCall(base, "call_VsContOk", bx, by)) {
                    cr3UiPut("vscontok", "", 0, bx, by, view);
                    float ax, ay, ex, ey;
                    if (cr3UiCall(base, "tapAreaS02", ax, ay) && cr3UiCall(base, "tapEreaS02", ex, ey)) vsTaps.push_back({ax, ay, ex, ey, 200});
                    else vsTaps.push_back({bx - 1.0f, by - 0.4f, bx + 1.0f, by + 0.4f, 200});
                }
            }
        }
    } else if (vs.screen == 8) {
        drawVsShop(w, h, view);
    } else if (vs.screen == 9) {
        drawVsResults(w, h, view);
    } else if (vs.screen == 7) { // (the tournament's end: its prizes on the system window)
        const cr3::Cr3OfflineDb::VsRewards& rw = game.cr3Db->vsRewards();
        std::string line = vs.lost ? "Lost." : rw.won ? "Tournament cleared!" : "Over.";
        if (rw.won) line += "\\nGP +" + std::to_string(rw.gp) + (rw.item ? "\\nItem: " + cr3ItemText(rw.item, false) : std::string());
        geneMenu.message = line;
        const int screen = geneMenu.screen;
        const float since = geneMenu.since;
        geneMenu.screen = 4, geneMenu.since = vs.since;
        drawGeneAsk(w, h, view);
        geneMenu.screen = screen, geneMenu.since = since, geneMenu.message.clear();
        for (const Cr3GeneLabTap& tp : geneLabTaps)
            if (tp.id == kTapYes || tp.id == kTapNo) vsTaps.push_back({tp.x0, tp.y0, tp.x1, tp.y1, 300});
        geneLabTaps.clear();
    }
    (void)t;
    // the back (comBackMenu at comFtrPosition's fit6)
    UiFig& foot = cr3UiPut("comftrposition", "", 0, 0, 0, view);
    if (vs.screen != 7 && vs.screen != 9 && cr3UiCall(foot, "call_comBackMenu_001", x, y, w, h)) {
        UiFig& menu = cr3UiPut("combackmenu", "", 0, x, y, view);
        float ax, ay, ex, ey;
        if (cr3UiCall(menu, "call_comBack_fr00", cx, cy)) cr3UiPut("comback_fr00", "", 0, cx, cy, view);
        if (cr3UiCall(menu, "tapAreaS01", ax, ay) && cr3UiCall(menu, "tapAreaE01", ex, ey)) vsTaps.push_back({ax, ay, ex, ey, -1});
    }
    if (vs.ask) {
        std::string text;
        if (vs.ask == 1) {
            const int cost = vsBoostCost();
            text = menuAnnounce(cost > 0 ? 0x2bf : 0x306);
            if (const size_t at = text.find("{number}"); at != std::string::npos) text.replace(at, 8, std::to_string(cost));
        } else if (vs.ask == 2)
            text = menuAnnounce(0x2a9);
        else
            text = menuAnnounce(0x123); // (Complete this purchase?)
        drawCr3Asked(text, vs.since, 0, true, 600, 601, w, h, view, &vsTaps);
    }
    if (!vs.told.empty()) drawCr3Told(vs.told, vs.toldSince, 602, w, h, view, &vsTaps);
}

// a battle's score (VsColosseumResultMenu), over the won battle
void Viewer::drawVsScore(int w, int h) {
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f * 10;
    const Mat4 view = cr3::mat4Multiply(uiProjection(w, h, 10), camera);
    cr3::SceneLighting none;
    dev->setLighting(none);
    dev->clearDepth();
    UiFig& p = cr3UiPut("clvspositon", "", std::min(uiClock - vs.scoreAt, 0.4f), 0, 0, view); // (its own animation: in from the right over 5 frames, held - its out from frame 15)
    float x, y;
    // (SetScoreVsColosseumResultMenu: the battle's score, then the bonuses - MP used, lifeline items, wildcards)
    if (cr3UiCall(p, "call_clVsNumLPanel", x, y)) cr3UiNumber(cr3UiPut("clvsnumlpanel", "", 0, x, y, view), "call_clVsNumL_fr00", "clvsnuml_fr00", vs.score, 7, 0, view);
    const long parts[3] = {vs.mpBonus, vs.itemBonus, vs.wildBonus};
    for (int k = 0; k < 3; ++k) {
        char j[40];
        std::snprintf(j, sizeof j, "call_clVsLine0%d", k + 1);
        if (cr3UiCall(p, j, x, y)) cr3UiPut("clvsline01", "", 0, x, y, view);
        std::snprintf(j, sizeof j, "call_clText_fr00_%03d", k + 1);
        if (cr3UiCall(p, j, x, y)) cr3UiPut("cltext_fr00", "", float(k) / 30, x, y, view);
        std::snprintf(j, sizeof j, "call_clVsNumSPanel_%03d", k + 1);
        if (cr3UiCall(p, j, x, y)) cr3UiNumber(cr3UiPut("clvsnumspanel", "", 0, x, y, view), "call_clVsNumS_fr00", "clvsnums_fr00", parts[k], 6, 0, view);
    }
}

// the tournament's battles: the next coupling (FldUtilReserveVsBattleIN - its field, level, music)
void Viewer::vsNextBattle() {
    const cr3::Cr3OfflineDb::VsTournament& tt = game.cr3Db->vsTournaments()[size_t(vs.chosen)];
    while (vs.battle < 3 && tt.coupling[vs.battle] <= 0) ++vs.battle;
    if (vs.battle >= 3 || vs.battle >= std::max(tt.count, 1)) return vsEnd();
    if (!cr3BattleMode && areaWalker) { // (the field kept to come back to, as BranchAfterEvent's)
        savedArea = area.name;
        const cr3::Vec3 p = areaWalker->position();
        savedPos = {p[0], p[1], p[2]}, savedYaw = areaYaw;
    }
    game.next.battleLevel = tt.level, game.next.battleMusic = tt.bgm, game.next.battleNext = 1;
    vs.screen = 5, vs.since = 0;
    SDL_Log("vs: battle %d - coupling %d on field %d at level %d", vs.battle + 1, tt.coupling[vs.battle], tt.mapId, tt.level);
    requestCr3Battle(tt.coupling[vs.battle], tt.mapId);
}

// a battle over (from the battle's end): its score, EndVSBattle; on, or the tournament's end
bool Viewer::vsMoreBattles() const {
    const cr3::Cr3OfflineDb::VsTournament& tt = game.cr3Db->vsTournaments()[size_t(vs.chosen)];
    for (int b = vs.battle; b < 3 && b < std::max(tt.count, 1); ++b)
        if (tt.coupling[b] > 0) return true;
    return false;
}

bool Viewer::vsBattleOver(bool won) {
    if (vs.screen == 5) {
        vs.score = cr3Rules.partyDamage;
        vs.mpBonus = vs.score * cr3Rules.partyMp / 100;
        vsScoreBonuses();
        vs.total = std::min(vs.score + vs.mpBonus + vs.itemBonus + vs.wildBonus, 9999999L);
        const int tally = game.cr3Vs.missionType == 2 ? int(vs.total) : cr3Rules.killed; // (SM_EndVSBattle: by the mission's type)
        game.cr3Db->endVSBattle(game, int(vs.total), tally, !won);
        ++game.cr3Records.vsMatches;
        SDL_Log("vs: battle %d %s - score %ld (+%ld for %d MP), tournament %d", vs.battle + 1, won ? "won" : "lost", vs.score, vs.mpBonus,
                cr3Rules.partyMp, game.cr3Vs.score);
        vs.lost = !won;
        if (won) {
            vs.screen = 6, vs.since = 0, vs.scoreAt = uiClock; // (the score shown over the battle)
            return true;
        }
        endCr3Battle();
        vsEnd();
        return true;
    }
    if (vs.screen != 6) return false;
    ++vs.battle;
    if (vsMoreBattles()) {
        cr3BattleMode = false, cr3BattleEvents.reset(); // (on to the next: no field between)
        vsNextBattle();
    } else {
        endCr3Battle();
        vsEnd();
    }
    return true;
}

void Viewer::vsEnd() {
    vs.endScore = game.cr3Vs.score;
    game.cr3Db->endVSTournament(game, std::time(nullptr));
    const auto& rw = game.cr3Db->vsRewards();
    SDL_Log("vs: tournament over - won %d, GP +%d, item %d, week %d", int(rw.won), rw.gp, rw.item, game.cr3Vs.weekScore);
    game.cr3Db->getVSTournaments(game, std::time(nullptr));
    vs.screen = 9, vs.since = 0; // (the results, then the prizes)
    audio.playSe("com_035", false);
}

void Viewer::chooseVs(int id) {
    if (!vs.told.empty()) {
        if (id == 602 || id == -1) vs.told.clear(), vs.boxContents.clear();
        return;
    }
    if (vs.ask) {
        if (id == 601 || id == -1) {
            if (vs.ask == 5) vs.screen = 10, vs.since = 0; // (SShopMenu 8: no - the card away)
            vs.ask = 0, audio.playSe("com_004", false);
        }
        else if (id == 600) vsAnswered();
        return;
    }
    if (id == -1) {
        audio.playSe("com_004", false);
        if (vs.screen == 11 && vs.boxBuy) return (void)(vs.boxBuy = false, vs.since = 0);
        vs.screen = vs.screen == 4 ? 3 : vs.screen == 3 ? 2 : vs.screen == 2 ? 1 : vs.screen == 8 ? (vs.shopOnly ? 10 : 1) : vs.screen == 11 || vs.screen == 12 ? 10 : 0, vs.since = 0;
        if (vs.screen == 0) SDL_Log("vs: closed");
        return;
    }
    if (id < -1 || id == 602) return;
    audio.playSe("com_003", false);
    if (vs.screen == 10 && id == 1000) openSpBoxes();
    else if (vs.screen == 10 && id == 1001) vs.screen = 8, vs.since = 0, vs.row = 0, SDL_Log("shop: the goods");
    else if (vs.screen == 10 && id == 1002 && game.cr3VipDays < 90) vs.screen = 12, vs.since = 0, vs.ask = 5, SDL_Log("shop: VIP");
    else if (vs.screen == 11 && id >= 1100 && id < 1103) { // (LuckyBoxMenu: a neighbour turned to; the one in focus - its buy panels)
        const int n = std::max(int(spBoxes().size()), 1);
        if (id == 1101) vs.boxBuy = true, vs.boxPanel = 0, vs.boxCheckFocus = false, vs.since = 0;
        else vs.boxFocus = ((vs.boxFocus + (id == 1100 ? -1 : 1)) % n + n) % n;
    } else if (vs.screen == 11 && vs.boxBuy && id >= 1110 && id < 1113) {
        vs.boxPanel = id - 1110, vs.ask = 4, vs.since = 0; // (LuckyBoxMain 3: asked 0x123)
    } else if (vs.screen == 11 && vs.boxBuy && id == 1120) {
        inspectSpBox();
    } else if (vs.screen == 1 && id == 1) vs.screen = 2, vs.since = 0, vs.buttonFocus = 0;
    else if (vs.screen == 1 && id == 2) vs.screen = 8, vs.since = 0, vs.row = 0, SDL_Log("vs: sp shop");
    else if ((vs.screen == 1 || vs.screen == 2) && id >= 10 && id < 14) {
        if (id == 11) openCr3Camp(); // (VsModeMain 0xe: the camp, its Gene Swap off)
        else if (id == 12) vs.ask = 2, vs.since = 0;
        else if (id == 10 && areaEvents) { // (VsModeMain 10: CreateGeneSynthesisMainToVS - the lab, the clerk's cutIn115_L01)
            areaEvents->openGeneLab("cutIn115_L01", true);
            SDL_Log("vs: the gene lab");
        } else if (id == 13)
            openMyPage(); // (VsModeMain 0x15: CreateMyPageTop)
    } else if (vs.screen == 3 && id >= 500 && id < 508) { // (a lifeline item chosen: four at most)
        const std::vector<VsBoost> table = vsBoostTable();
        if (id - 500 >= int(table.size())) return;
        vs.boostFocus = table[size_t(id - 500)].id;
        if (vs.boosts.size() < 4 && std::find(vs.boosts.begin(), vs.boosts.end(), vs.boostFocus) == vs.boosts.end()) vs.boosts.push_back(vs.boostFocus);
    } else if (vs.screen == 3 && id >= 520 && id < 524) {
        if (id - 520 < int(vs.boosts.size())) vs.boostFocus = vs.boosts[size_t(id - 520)], vs.boosts.erase(vs.boosts.begin() + (id - 520));
    } else if (vs.screen == 8 && id >= 700) {
        const int k = id - 700;
        if (k != vs.row) vs.row = k;
        else vs.ask = 3, vs.since = 0;
    } else if (vs.screen == 9 && id == 301) {
        vs.screen = 7, vs.since = 0;
    } else if (vs.screen == 7 && id == 300) {
        vs.screen = 2, vs.since = 0, vs.boosts.clear();
    } else if (vs.screen == 2 && (id == 3 || id == 4)) vsList(id - 3);
    else if (vs.screen == 3 && id >= 100) {
        const int k = id - 100;
        if (k < 0 || k >= int(vs.rows.size())) return;
        if (k != vs.row) {
            vs.row = k;
            return;
        }
        vs.chosen = vs.rows[size_t(k)], vs.screen = 4, vs.since = 0;
    } else if (vs.screen == 4 && id == 200) {
        vs.ask = 1; // (asked first: the coins the lifeline items cost)
    }
}

// a question answered yes (VsModeMain 0x1d - the tournament; 0x12 - BuyMP; PiuComMenu - BuyGoods)
void Viewer::vsAnswered() {
    const int ask = vs.ask;
    vs.ask = 0;
    audio.playSe("com_003", false);
    if (ask == 4) return openSpBox();
    if (ask == 5) { // (SShopMenu 7: yes - under 30 coins told 0x317, else BuyVIP (request 0x5e) and com_024)
        const int r = game.cr3Db->buyVIP(game);
        if (r == cr3::kDbOk) audio.playSe("com_024", false);
        else vs.told = menuAnnounce(0x317), vs.toldSince = 0;
        SDL_Log("shop: VIP bought -> %d (%d days)", r, game.cr3VipDays);
        vs.screen = 10, vs.since = 0;
        return;
    }
    if (ask == 2) {
        const int r = game.cr3Db->buyMP(game);
        if (r == cr3::kDbOk) game.cr3Mp.clear(); // (the party's MP full)
        else vs.told = menuAnnounce(0x100), vs.toldSince = 0; // (not enough coins)
        SDL_Log("vs: MP recovered -> %d", r);
        return;
    }
    if (ask == 3) {
        const std::vector<VsGoods> goods = vsShopGoods();
        if (vs.row >= int(goods.size())) return;
        const int shop = game.cr3Db->findShop("com_1");
        const int r = shop >= 0 ? game.cr3Db->buyGoods(game, shop, goods[size_t(vs.row)].id, 1) : -1;
        std::string text = menuAnnounce(0xfb);
        if (const size_t at = text.find("%s"); at != std::string::npos) text.replace(at, 2, cr3ItemText(goods[size_t(vs.row)].item, false));
        vs.told = r == cr3::kDbOk ? text : menuAnnounce(r == cr3::kDbNoGP ? 0xff : r == cr3::kDbNoGold ? 0xfe : 0x100), vs.toldSince = 0;
        if (r == cr3::kDbOk) game.setFlag(38, true), audio.playSe("com_024", false); // (ShopBuyCoreMain 0xe: flag 38 - the Using Coins quest's)
        SDL_Log("vs: goods %d bought -> %d", goods[size_t(vs.row)].id, r);
        return;
    }
    if (ask == 1) {
        const int r = game.cr3Db->beginVSTournament(game, vs.chosen, vs.boosts, std::time(nullptr));
        SDL_Log("vs: tournament %d begun -> %d", game.cr3Db->vsTournaments()[size_t(vs.chosen)].id, r);
        if (r != cr3::kDbOk) return;
        game.cr3Db->pickWildcard();
        vs.battle = 0, vs.lost = false;
        std::fill(std::begin(vs.wildChosen), std::end(vs.wildChosen), -1);
        vsNextBattle();
    }
}

bool Viewer::vsClick(float fx, float fy) {
    if (cr3Camp.stage != 0) return false; // (the camp's over it)
    if (vs.screen == 5 && vs.wildOpen) {
        const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
        const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
        for (auto it = vsTaps.rbegin(); it != vsTaps.rend(); ++it)
            if (it->id >= 800 && ux >= std::min(it->x0, it->x1) && ux <= std::max(it->x0, it->x1) && uy >= std::min(it->y0, it->y1) && uy <= std::max(it->y0, it->y1))
                return chooseVsWildCard(it->id - 800), true;
        return true;
    }
    if (vs.screen == 0 || vs.screen == 5) return false;
    if (vs.screen == 6) return vsBattleOver(true), true;
    const float k = std::min(float(screenW) / 9.6f, float(screenH) / 6.4f);
    const float ux = (fx - 0.5f) * float(screenW) / k, uy = (0.5f - fy) * float(screenH) / k;
    for (auto it = vsTaps.rbegin(); it != vsTaps.rend(); ++it)
        if (ux >= std::min(it->x0, it->x1) && ux <= std::max(it->x0, it->x1) && uy >= std::min(it->y0, it->y1) && uy <= std::max(it->y0, it->y1)) {
            chooseVs(it->id);
            return true;
        }
    return true;
}

bool Viewer::vsShoulder(bool right) {
    if (cr3Camp.stage || vs.screen != 4 || vs.ask || !vs.told.empty() || vs.rows.empty()) return false;
    const int to = std::clamp(vs.row + (right ? 1 : -1), 0, int(vs.rows.size()) - 1);
    if (to != vs.row) {
        vs.row = to, vs.chosen = vs.rows[size_t(to)], vs.since = 0;
        audio.playSe("com_003", false);
        SDL_Log("vs: tournament detail %d", to);
    }
    return true;
}

bool Viewer::vsKey(SDL_Keycode key) {
    if (cr3Camp.stage != 0) return false;
    if (vs.screen == 5 && vs.wildOpen) { // (the Vita's cards: left / right stop at either end, confirm chooses)
        if (key >= SDLK_1 && key <= SDLK_5) chooseVsWildCard(int(key - SDLK_1));
        else if (key == SDLK_LEFT || key == SDLK_RIGHT) {
            const int count = game.cr3Db ? std::min(int(game.cr3Db->wildcards().size()), 5) : 0;
            if (count > 0) vsWildFocus = std::clamp(vsWildFocus + (key == SDLK_RIGHT ? 1 : -1), 0, count - 1);
            SDL_Log("vs: wildcard cursor %d", vsWildFocus);
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) chooseVsWildCard(vsWildFocus);
        return true;
    }
    if (vs.screen == 0 || vs.screen == 5) return false;
    const bool enter = key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE;
    if (!vs.told.empty()) {
        if (!vs.boxContents.empty() && (key == SDLK_LEFT || key == SDLK_UP || key == SDLK_RIGHT || key == SDLK_DOWN)) {
            vs.boxContentsPage += key == SDLK_RIGHT || key == SDLK_DOWN ? 1 : -1;
            spBoxContentsPage();
        } else if (enter || key == SDLK_ESCAPE || key == SDLK_BACKSPACE) chooseVs(602);
        return true;
    }
    if (vs.ask) {
        if (enter || key == SDLK_Y) chooseVs(600);
        else if (key == SDLK_N || key == SDLK_ESCAPE || key == SDLK_BACKSPACE) chooseVs(601);
        return true;
    }
    if (vs.screen == 9) return enter ? (chooseVs(301), true) : true;
    if (vs.screen == 10) {
        if (vitaButtons()) {
            const int was = vs.shopFocus;
            if (key == SDLK_RIGHT && vs.shopFocus == 0) vs.shopFocus = 1;
            else if (key == SDLK_LEFT && vs.shopFocus >= 1) vs.shopFocus = 0;
            else if (key == SDLK_DOWN && vs.shopFocus == 1) vs.shopFocus = 2;
            else if (key == SDLK_UP && vs.shopFocus == 2) vs.shopFocus = 1;
            if (was != vs.shopFocus) SDL_Log("shop: the cursor on %d", vs.shopFocus);
            if (enter) {
                if (vs.shopFocus == 2 && game.cr3VipDays >= 90) audio.playSe("com_005", false);
                else chooseVs(1000 + vs.shopFocus);
                return true;
            }
        }
        if (key == SDLK_1 || key == SDLK_2 || key == SDLK_3) chooseVs(key == SDLK_1 ? 1000 : key == SDLK_2 ? 1001 : 1002);
        else if (enter) chooseVs(1000);
        else if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) chooseVs(-1);
        return true;
    }
    if (vs.screen == 11) {
        if (!vs.boxBuy && (key == SDLK_LEFT || key == SDLK_RIGHT)) chooseVs(key == SDLK_LEFT ? 1100 : 1102);
        else if (!vs.boxBuy && enter) chooseVs(1101);
        else if (vs.boxBuy && vitaButtons() && (key == SDLK_UP || key == SDLK_DOWN)) {
            const auto boxes = spBoxes();
            if (vs.boxFocus < int(boxes.size()) && boxes[size_t(vs.boxFocus)].kind != 2) {
                vs.boxCheckFocus = !vs.boxCheckFocus;
                audio.playSe("com_002", false);
                SDL_Log("shop: box focus %s", vs.boxCheckFocus ? "check" : "buy");
            }
        }
        else if (vs.boxBuy && (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN)) {
            if (!vs.boxCheckFocus) {
                const int was = vs.boxPanel;
                vs.boxPanel = std::clamp(vs.boxPanel + (key == SDLK_RIGHT || key == SDLK_DOWN ? 1 : -1), 0, 2);
                if (was != vs.boxPanel) audio.playSe("com_002", false), SDL_Log("shop: box buy panel %d", vs.boxPanel);
            }
        }
        else if (vs.boxBuy && enter) chooseVs(vs.boxCheckFocus ? 1120 : 1110 + vs.boxPanel);
        else if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) chooseVs(-1);
        return true;
    }
    if (vs.screen == 8) {
        const int n = int(vsShopGoods().size());
        if ((key == SDLK_UP || key == SDLK_DOWN) && n > 0) vs.row = std::clamp(vs.row + (key == SDLK_DOWN ? 1 : -1), 0, n - 1);
        else if (enter && n > 0) chooseVs(700 + vs.row);
        else if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) chooseVs(-1);
        return true;
    }
    if (vs.screen == 3 && key >= SDLK_1 && key <= SDLK_8) return chooseVs(500 + int(key - SDLK_1)), true; // (1 .. 8: a lifeline item)
    if (vs.screen == 6) return enter ? (vsBattleOver(true), true) : true;
    if (vitaButtons() && (vs.screen == 1 || vs.screen == 2)) {
        const int was = vs.buttonFocus;
        if (key == SDLK_UP && was > 0) vs.buttonFocus -= was < 3 ? 1 : 2;
        else if (key == SDLK_DOWN && was < 5) vs.buttonFocus += was < 2 ? 1 : was < 4 ? 2 : 0;
        else if (key == SDLK_LEFT && was >= 2 && (was & 1)) --vs.buttonFocus;
        else if (key == SDLK_RIGHT && was >= 2 && !(was & 1)) ++vs.buttonFocus;
        if (vs.buttonFocus != was) audio.playSe("com_002", false), SDL_Log("vs: the cursor on %d", vs.buttonFocus);
        if (enter) {
            SDL_Log("vs: the cursor presses %d", vs.buttonFocus);
            chooseVs(vs.buttonFocus < 2 ? vs.buttonFocus + (vs.screen == 1 ? 1 : 3) : vs.buttonFocus + 8);
            return true;
        }
    }
    if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) chooseVs(-1);
    else if (vs.screen == 1 && enter) chooseVs(1);
    else if (vs.screen == 2 && (key == SDLK_1 || key == SDLK_2)) chooseVs(key == SDLK_1 ? 3 : 4);
    else if (vs.screen == 3 && (key == SDLK_UP || key == SDLK_DOWN) && !vs.rows.empty())
        vs.row = std::clamp(vs.row + (key == SDLK_DOWN ? 1 : -1), 0, int(vs.rows.size()) - 1);
    else if (vs.screen == 3 && enter && !vs.rows.empty()) chooseVs(100 + vs.row);
    else if (vs.screen == 3 && vitaButtons() && key == SDLK_TAB && !vs.rows.empty()) {
        chooseVs(100 + vs.row);
        if (vs.screen == 4) chooseVs(200);
    }
    else if (vs.screen == 3 && vitaButtons() && key == SDLK_LEFT) chooseVs(-1);
    else if (vs.screen == 4 && enter) chooseVs(200);
    else if (vs.screen == 7 && enter) chooseVs(300);
    return true;
}

} // namespace cr3_viewer
