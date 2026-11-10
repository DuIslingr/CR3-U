// cr3view: CR3-U's model and field viewer.
//
// Models: a figure (`.geom`) from your copy's archive, its meshes with their materials' textures,
// render states and the game's shading (its generated shaders, as shading.glsl), lit by a map's
// character lights (`<map>c.geom`; d0101c unless --lights says), skinned to its skeleton (`.skel`)
// and playing its animations (`<figure>*.anim`).
//
// Fields (--field <map>): the map's painted layers and 3D figure with a character (--model, c001
// unless given) at one of its points (--at; defaultPos), through the map's camera, scrolling to
// keep the character centred (field/fieldmap.hpp). The maps' table is in the game's executable:
// found beside the iPad's or the Vita's archive, in the APK beside Android's OBB (its
// libChaosRings2.so; assets/release.hpp), or given with --binary.
//
// Events (--event <function> --vista <map>): an event's script played by the runtime (event/
// runtime.hpp) in its vista - the map's figure (d0151b for the opening's), the scene's models on
// their locators with their animations, through the scene's camera, under its lights, with the
// screen's fades - 20 updates a second. A message's pages turn on Return or a click (or by
// themselves after --page seconds; 0 never). --time runs the event that far first.
//
// The output: --resolution WxH (up to 3840 x 2160; F1 steps through the window's own size and the
// 16:9 ones) renders frames of that size, shown in the window at their shape (letterboxed; GL
// draws into the window, the resolution's shape letterboxed in it). --upscaler fsr3 (F2 steps
// through its modes) renders the scene smaller - --quality native (AA at full size), quality
// (1.5x), balanced (1.7x), performance (2x), ultra (3x) - and AMD FSR 3.1 resolves it into the
// frame, with --sharpness (0..1, 0.3) for its RCAS pass; Vulkan only.
//
// Frames: 60 a second (--fps; 0 as many as the display shows). The game itself updates 20 times
// a second (its scripts, fades, physics); the frames between show its timelines where they are
// by then - an event's camera, moves, animations and fades - and the field's player between its
// last two updates.
//
// Vulkan by default, OpenGL where Vulkan can't start (or with --renderer opengl).
//
//   cr3view --data <CRDB.ios.mvgl | main.212...obb | CRDB.psp2.mvgl, or a folder holding one:
//           the Vita's PCSG00498, Android's OBB + APK, an unpacked .ipa> [--model <name>]
//           [--anim <name>] [--time s] [--size WxH] [--yaw deg] [--pitch deg]
//           [--lights <figure> | none] [--field <map> [--at <point>] [--binary <file>]
//           [--walk sx,sy,seconds]] [--event <function> [--vista <map>] [--page s]]
//           [--resolution WxH] [--upscaler off|fsr3 [--quality q] [--sharpness s]] [--run s] [--fps n]
//           [--renderer vulkan|opengl] [--movie cNN] [--capture out.bmp]
//
// Models: drag to turn, the wheel to zoom, Left / Right (or PageUp / PageDown) for the archive's
// other models, Up / Down for the figure's animations. Fields: the arrow keys (or WASD) run,
// with Shift walk, on the map's floors and against its walls;
// PageUp / PageDown for the character's animations. Space pauses, Esc quits. --capture draws
// one frame (at --time; --run plays that many seconds more first, 60 frames a second) to a BMP
// and exits.
#include "viewer/viewer.hpp"
#include <SDL3/SDL_main.h>

using namespace cr3_viewer;

namespace {

bool saveBmp(const char* path, const std::vector<uint8_t>& rgba, int w, int h) {
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
    if (!s) return false;
    for (int y = 0; y < h; ++y) std::memcpy(static_cast<uint8_t*>(s->pixels) + size_t(y) * s->pitch, &rgba[size_t(y) * w * 4], size_t(w) * 4);
    const bool ok = SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    return ok;
}

std::string upscaleName(const cr3::UpscaleSettings& u) {
    if (u.mode == cr3::Upscaler::Off) return "off";
    std::string q = "x" + std::to_string(u.ratio);
    for (const Quality& k : kQualities)
        if (k.ratio == u.ratio) q = k.name;
    return std::string(u.mode == cr3::Upscaler::Fsr3 ? "FSR 3.1 " : u.mode == cr3::Upscaler::Fsr4 ? "FSR 4 " : u.mode == cr3::Upscaler::MetalFx ? "MetalFX " : "DLSS ") + q;
}

} // namespace

// Where a copy of the game is looked for without --data: beside the executable (Android: the app's
// folder on the shared storage, reachable over USB - Android/data/local.preservation.chaosrings3/
// files/), beside an AppImage or a macOS .app (their base paths are inside them), the working directory
std::vector<std::string> dataPlaces() {
    std::vector<std::string> out;
    auto add = [&](std::string p) {
        if (p.empty()) return;
        if (p.back() != '/' && p.back() != '\\') p += '/';
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    };
#ifdef SDL_PLATFORM_ANDROID
    if (const char* ext = SDL_GetAndroidExternalStoragePath()) add(ext);
#else
    const char* basePath = SDL_GetBasePath();
    const std::string base = basePath ? basePath : "";
#ifdef SDL_PLATFORM_LINUX
    if (const char* image = SDL_getenv("APPIMAGE")) add(std::string(image).substr(0, std::string(image).rfind('/') + 1));
#endif
#ifdef SDL_PLATFORM_MACOS
    if (const size_t app = base.rfind(".app/"); app != std::string::npos) add(base.substr(0, base.rfind('/', app) + 1));
#endif
    add(base);
    if (char* cwd = SDL_GetCurrentDirectory()) add(cwd), SDL_free(cwd);
    if (char* pref = SDL_GetPrefPath("DuIslingr", "CR3-U")) add(pref), SDL_free(pref); // (the profile folder: an .ipa the first start unpacked there)
#endif
    return out;
}
// The fan translations in a folder - a folder each (translation/en/, translation/es/, a download's
// extracted/ ...), else the folder one itself - over the release, each once (a folder already
// over it, the same one by another way, left). `loud`: a folder that holds none said
void addTranslations(cr3::Database& db, const std::string& dir, bool loud) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto known = [&](const std::string& p) {
        for (size_t i = 0; i < db.overlayCount(); ++i)
            if (fs::equivalent(db.overlayPath(int(i)), p, ec)) return true;
        return false;
    };
    std::vector<std::string> subs;
    if (fs::is_directory(dir, ec))
        for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
            const std::string n = e.path().filename().string();
            if (e.is_directory(ec) && !n.empty() && n[0] != '.' && !(n.size() > 5 && n.compare(n.size() - 5, 5, ".part") == 0))
                subs.push_back(e.path().string()); // (a .part: a copy the setup didn't finish)
        }
    std::sort(subs.begin(), subs.end());
    bool any = false;
    for (const std::string& p : subs) {
        if (known(p)) { any = true; continue; }
        if (db.addOverlay(p)) any = true, SDL_Log("fan translation: %s", p.c_str());
    }
    std::string err;
    if (any || known(dir)) return;
    if (db.addOverlay(dir, &err)) SDL_Log("fan translation: %s", dir.c_str());
    else if (loud) SDL_Log("fan translation: %s", err.c_str());
}

// settings.ini's line `key` (data=, patch= - the first start's setup's: a copy read where it lies, the
// Vita release's fan translation): its value; and set, the other lines kept
static std::string settingsIni() {
    char* dir = SDL_GetPrefPath("DuIslingr", "CR3-U");
    const std::string path = (dir ? std::string(dir) : std::string()) + "settings.ini";
    SDL_free(dir);
    return path;
}
std::string storedSetting(const char* key) {
    std::string out;
    size_t size = 0;
    if (char* text = static_cast<char*>(SDL_LoadFile(settingsIni().c_str(), &size))) {
        const std::string all(text, size);
        SDL_free(text);
        const size_t kl = std::strlen(key);
        for (size_t at = 0; at < all.size();) {
            const size_t e = std::min(all.find('\n', at), all.size());
            if (all.compare(at, kl, key) == 0) out = all.substr(at + kl, e - at - kl);
            at = e + 1;
        }
    }
    while (!out.empty() && (out.back() == '\r' || out.back() == ' ')) out.pop_back();
    return out;
}
void storeSetting(const char* key, const std::string& value) {
    size_t size = 0;
    char* text = static_cast<char*>(SDL_LoadFile(settingsIni().c_str(), &size));
    std::string all = text ? std::string(text, size) : std::string(), out;
    SDL_free(text);
    const size_t kl = std::strlen(key);
    for (size_t at = 0; at < all.size();) {
        const size_t e = std::min(all.find('\n', at), all.size());
        if (all.compare(at, kl, key) != 0) out += all.substr(at, e - at) + "\n";
        at = e + 1;
    }
    if (!value.empty()) out += key + value + "\n";
    std::string error;
    if (!crstorage::replaceFile(settingsIni(), out, &error)) SDL_Log("settings: %s", error.c_str());
}

int inputCancelViewerSelfTest() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    {
        Viewer v;
        crinput::CancelWatch watch;
        if (!watch.start(42)) return 1;
        for (Uint32 type : {Uint32(SDL_EVENT_WINDOW_FOCUS_LOST), Uint32(SDL_EVENT_FINGER_CANCELED), Uint32(SDL_EVENT_WILL_ENTER_BACKGROUND)}) {
            v.menuSwipe.begin(0.5f, 0.5f, 1);
            v.areaDrag.on = true;
            v.fastHeld = true, v.fastUntil = 100;
            v.cr3Camp.confirmHeld = true;
            v.iai.stage = v.aim.stage = 2;
            v.keyLock.stage = 1, v.keyLock.pressing = true;
            v.vslash.stage = 1, v.vslash.pressed = true;
            SDL_Event e{};
            e.type = type;
            if (type == SDL_EVENT_FINGER_CANCELED) e.tfinger.windowID = 42;
            else e.window.windowID = 42;
            SDL_PushEvent(&e);
            const bool consumed = v.consumeInputCancel(watch);
            const bool clean = !v.menuSwipe.down && !v.areaDrag.on && !v.fastHeld && v.fastUntil == 0 &&
                               v.iai.stage == 1 && v.aim.stage == 1 && !v.keyLock.pressing && !v.vslash.pressed && !v.cr3Camp.confirmHeld;
            const bool unchanged = v.keyLock.stage == 1 && v.vslash.stage == 1 && v.pointerCancelled;
            problems += !consumed + !clean + !unchanged;
            if (!clean) std::printf("viewer input cancel FAILED: event %u retained an active gesture\n", type);
            const bool heldOver = !v.padHeldOver.live(true) && v.padHeldOver.live(false);
            problems += !heldOver;
            if (!heldOver) std::printf("viewer input cancel FAILED: event %u left a held controller live\n", type);
        }
    }
    SDL_Quit();
    std::printf("viewer input cancellation: %d problems\n", problems);
    return problems ? 1 : 0;
}

int lifecycleClockViewerSelfTest() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    {
        Viewer v;
        crtime::LifecycleClock clock;
        const Uint64 second = SDL_NS_PER_SECOND;
        auto frame = [&](Uint64 wall) {
            const float dt = clock.step(wall);
            v.advance(dt, 0, 0, false);
        };
        frame(second);
        frame(second + second/20);
        const float before = v.uiClock;
        SDL_Event event{};
        event.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
        clock.note(event, 2*second);
        event.type = SDL_EVENT_DID_ENTER_FOREGROUND;
        clock.note(event, 3602*second);
        frame(3602*second);
        problems += v.uiClock != before; // no scene/UI catch-up after an hour asleep
        frame(3602*second + second/20);
        problems += std::fabs(v.uiClock - before - 0.05f) > 1e-5f;
        std::printf("viewer lifecycle: %.3f seconds before pause, %.3f after resumed frame; %d problems\n", before, v.uiClock, problems);
    }
    SDL_Quit();
    return problems ? 1 : 0;
}

int sceneAudioViewerSelfTest() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    {
        auto device = cr3::makeGlDevice(); // resetHistory is a no-op; no GL context needed
        Viewer v;
        v.dev = device.get();
        auto check = [&](bool ok, const char* what) {
            if (!ok) { ++problems; std::printf("scene audio FAILED: %s\n", what); }
        };
        v.voiceJobs.push_back({2, 123});
        v.seJobs.push_back({2, 456});
        v.scene.active = true, v.scene.skippable = false, v.scene.hits = true;
        v.scene.start = 0, v.battleClock = 1;
        check(!v.skipScene() && v.voiceJobs.size() == 1 && v.seJobs.size() == 1,
              "a protected scene keeps its pending sounds");
        v.scene.skippable = true;
        check(v.skipScene(), "a permitted scene can be skipped");
        check(v.voiceJobs.empty() && v.seJobs.empty(), "skip cancels pending speech and effects");
        v.audio.log.clear();
        v.battleClock = 3;
        v.updateBattleSounds();
        check(v.audio.log.empty(), "advancing past the skipped cue cannot replay its voice");
        auto seed = [&] {
            v.voiceJobs.push_back({10, 123});
            v.seJobs.push_back({10, 456});
            v.pendingBgm = 15, v.pendingBgmAt = 10;
        };
        seed();
        v.leaveBattle();
        check(v.voiceJobs.empty() && v.seJobs.empty() && v.pendingBgm == 0,
              "abandoning battle cancels pending clips and the delayed result music");
        v.audio.log.clear();
        v.battleClock = 11;
        v.updateBattleSounds();
        check(v.audio.log.empty(), "abandoned battle cannot emit audio later");
        seed();
        v.startTitle();
        check(v.voiceJobs.empty() && v.seJobs.empty() && v.pendingBgm == 0,
              "return to title cancels old scene audio requests");
        v.audio.log.clear();
        v.updateBattleSounds();
        check(v.audio.log.empty(), "old battle cannot replace title music");
    }
    SDL_Quit();
    std::printf("scene audio cancellation: %d problems\n", problems);
    return problems ? 1 : 0;
}

int fieldRestoreSelfTest() {
    Viewer v;
    v.controller = std::make_unique<cr3::CharacterController>(v.field.collision());
    v.controller->setPosition({-9,2,-7});
    std::fill(std::begin(v.playerPrev),std::end(v.playerPrev),-8.0f);
    std::fill(std::begin(v.playerCur),std::end(v.playerCur),-6.0f);
    v.stepClock = 0.01f, v.substep = 2;
    const float saved[3] = {4,5,6};
    v.restoreFieldPosition(saved,0.75f);
    int problems = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { ++problems; std::printf("field restore FAILED: %s\n",what); }
    };
    auto near = [](float a,float b) { return std::abs(a-b)<0.00001f; };
    const auto at = v.controller->position();
    check(near(at[0],4) && near(at[1],5.02f) && near(at[2],6),"collision controller starts at the saved location");
    check(v.stepClock==0 && v.substep==0,"old interpolation time is discarded");
    check(near(v.heading,0.75f) && near(v.fig.place.m[3],4) && near(v.fig.place.m[7],5) && near(v.fig.place.m[11],6),
          "saved facing and initial render placement are restored");
    v.walk(0.001f,0,0,false); // first displayed frame, before a physics substep
    check(near(v.player[0],4) && near(v.player[1],5) && near(v.player[2],6),"first interpolated frame stays at the save instead of snapping to spawn");
    v.controller->substep(); // an empty world can fall, but horizontal position must stay at the save
    check(near(v.controller->position()[0],4) && near(v.controller->position()[2],6),"physics continues at the restored horizontal location");
    std::printf("field restore: %d problems\n",problems);
    return problems ? 1 : 0;
}

std::shared_ptr<vitaimport::Session::Choice> dirtySessionUiForTest(Viewer& v) {
    v.campOpen = v.mapOpen = v.settingsOpen = v.optionsOpen = v.optionsAsk = true;
    v.saveStage = 1, v.manualStage = 2;
    v.world.stage = v.shop.stage = v.party.stage = v.library.stage = v.pub.stage = 1;
    v.credits.stage = 1;
    v.paused = true;
    v.fieldTabOpen = true;
    v.fieldMode = v.eventMode = true;
    v.script = std::make_unique<cr3::FieldScript>(v.archive, v.game);
    v.event = std::make_unique<cr3::EventRuntime>(v.archive, v.game);
    v.manualEvents = std::make_unique<cr3::EventRuntime>(v.archive, v.game);
    v.menuSwipe.begin(0.5f, 0.5f, 1);
    v.fastHeld = true, v.fastUntil = 100;
    v.keyLock.stage = v.iai.stage = v.aim.stage = v.vslash.stage = 2;
    v.tapRects.push_back({999,0,0,1,1});
    v.game.setFlag(123,true);
    v.settings.fieldSpeed = 3;
    v.saveImport.state = vitaimport::Session::Picking;
    v.saveImport.choice = std::make_shared<vitaimport::Session::Choice>();
    v.cr3Camp.stage = 9;
    v.inputDialog.open = true;
    v.areaMode = v.cr3BattleMode = v.cr3Playing = v.cr3SavePending = true;
    v.areaEvents = std::make_unique<cr3::EventRuntime>(v.archive,v.game);
    v.geneLabWasOpen = true;
    v.town.stage = 3, v.worldSel.wasOpen = true;
    v.myPage.dragging = true;
    v.campOptions.hands = 1;
    return v.saveImport.choice;
}
int checkSessionUiForTest(Viewer& v, const std::shared_ptr<vitaimport::Session::Choice>& picker) {
    int problems = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { ++problems; std::printf("session UI FAILED: %s\n", what); }
    };
    check(!v.campOpen && !v.mapOpen && !v.settingsOpen && !v.optionsOpen && !v.optionsAsk &&
          v.saveStage == 0 && v.manualStage == 0 && !v.world.stage && !v.shop.stage && !v.party.stage &&
          !v.library.stage && !v.pub.stage && !v.credits.stage && !v.paused && !v.fieldTabOpen,
          "old menus and pause cannot block a new title/session");
    check(!v.script && !v.event && !v.manualEvents && !v.activeEvents(), "old field/tutorial runtimes are detached");
    check(!v.menuSwipe.down && !v.fastHeld && v.fastUntil == 0 && v.tapRects.empty() &&
          !v.keyLock.stage && !v.iai.stage && !v.aim.stage && !v.vslash.stage,
          "old input targets and ability controls are discarded");
    { std::lock_guard<std::mutex> lock(picker->mutex); picker->path = "/no-such-late-import-after-title"; picker->ready = true; }
    v.saveImport.poll();
    check(!v.saveImport.active() && !v.saveImport.choice, "late picker completion cannot reactivate the importer");
    check(v.game.flag(123) && v.settings.fieldSpeed == 3, "progress and preferences are retained until a new game/load is selected");
    check(!v.cr3Camp.stage && !v.inputDialog.open && !v.areaMode && !v.cr3BattleMode &&
          !v.cr3Playing && !v.cr3SavePending && !v.areaEvents && !v.geneLabWasOpen &&
          !v.town.stage && !v.worldSel.wasOpen && !v.myPage.dragging && v.campOptions.hands == 1,
          "native CR3 menus, text input and pending sync are cleared without resetting control preferences");
    v.saveImport.cancel();
    return problems;
}
int sessionUiViewerSelfTest() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    {
        Viewer v;
        const auto picker = dirtySessionUiForTest(v);
        v.startTitle();
        problems = checkSessionUiForTest(v,picker);
    }
    SDL_Quit();
    std::printf("viewer session UI: %d problems\n",problems);
    return problems ? 1 : 0;
}

// (a game's saves, CR3's title's too - which has no --play; a capture writes none)
static bool gameSaves(bool played, bool resumed, bool captured) { return (played || resumed) && !captured; }

int newSessionSelfTest() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    {
        Viewer v;
        const auto picker = dirtySessionUiForTest(v);
        v.systemRead = v.systemFound = true;
        v.systemFlags = {123}; // global unlocks survive a fresh game
        v.game.setFlag(1500, true);
        v.game.oz = 54321;
        v.game.itemCount[1] = 7;
        v.playSeconds = 987;
        v.voiceJobs.push_back({10,123});
        v.seJobs.push_back({10,456});
        v.pendingBgm = 15, v.pendingBgmAt = 10;
        v.newGame(); // the same entry point used by direct-play restart and title/retry
        problems += checkSessionUiForTest(v,picker);
        if (v.game.flag(1500) || v.game.oz == 54321 || v.game.itemCount[1] != 0 || v.playSeconds != 0) {
            ++problems; std::puts("new session FAILED: old progress or playtime survived");
        }
        if (!v.voiceJobs.empty() || !v.seJobs.empty() || v.pendingBgm || v.pendingBgmAt) {
            ++problems; std::puts("new session FAILED: abandoned scene audio survived");
        }
        // Native CR3 starts with its offline database, without legacy system story flags.
        v.game.setFlag(1500, true);
        v.playSeconds = 456;
        v.cr3Playing = v.cr3SavePending = true;
        v.resetForNewSession();
        if (v.game.flag(1500) || v.playSeconds != 0 || v.cr3Playing || v.cr3SavePending ||
            !v.game.onSync || v.campOptions.hands != 1) {
            ++problems; std::puts("new session FAILED: native CR3 reset or persistent controls");
        }
    }
    if (!gameSaves(true,false,false) || gameSaves(true,false,true) || gameSaves(false,false,false)) {
        ++problems; std::puts("new session FAILED: a game from the title does not save its options");
    }
    {
        struct Entry { uint64_t used = 0; };
        std::map<int, Entry> cache;
        for (int k = 0; k < 20; ++k) cache[k].used = uint64_t(k + 1);
        cache[0].used = 99; // (the oldest, drawn just now)
        int freed = 0;
        cr3::trimLeastUsed(cache, 16, [&](Entry&) { ++freed; });
        if (freed != 8 || cache.size() != 12 || !cache.count(0) || cache.count(1) || !cache.count(19)) {
            ++problems; std::puts("new session FAILED: the text cache's limit");
        }
        cr3::trimLeastUsed(cache, 16, [&](Entry&) { ++freed; });
        if (freed != 8) { ++problems; std::puts("new session FAILED: a cache within its limit was trimmed"); }
    }
    {
        Viewer v;
        cr3::Image picture;
        picture.width = picture.height = 1, picture.rgba = {1, 2, 3, 4};
        v.modelImages["p"] = picture;
        cr3::Texture& held = v.textures["p"];
        held.id = 7;
        std::vector<std::pair<std::string, cr3::Image>> again{{"p", picture}};
        v.takeModelPictures(again);
        const auto it = v.textures.find("p");
        if (it == v.textures.end() || &it->second != &held || held.id != 7) {
            ++problems; std::puts("new session FAILED: an HD model's reload took a texture still held");
        }
    }
    SDL_Quit();
    std::printf("new session: %d problems\n",problems);
    return problems ? 1 : 0;
}

// utils::StartQuake / QuakeControlTask (OnUpdate 0x00944084, Render 0x009440f8, Start 0x00944188):
// the layer's y offset width x (from .. to) / the display's height, the way flipped at a 60-Hz
// update only after a drawn frame, below-0 frames held, Stop
int quakeSelfTest() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    {
        Viewer v;
        cr3::ScriptVM vm;
        v.bindQuakeNatives(vm);
        const char* source = "function start(){Cr3System.StartQuake(10, 0.2, 1.0, 143);} function held(){Cr3System.StartQuake(30, 0.1, 0.3, -10);} "
                             "function stop(){Cr3System.StopQuake();}";
        if (SQ_FAILED(sq_compilebuffer(vm.vm(), source, SQInteger(std::strlen(source)), "quake-fixture", SQTrue)) || !vm.runMain()) return 1;
        auto check = [&](bool ok, const char* what) {
            if (!ok) { ++problems; std::printf("quake FAILED: %s\n", what); }
        };
        auto offset = [&]() {
            Mat4 p = cr3::mat4Identity();
            v.applyQuake(p);
            return p[13];
        };
        auto near = [](float a, float b) { return std::fabs(a - b) < 1e-6f; };
        vm.call("start");
        check(v.fieldQuake.on && near(v.fieldQuake.seconds, 143.0f / 30.0f), "StartQuake: frames / 30 s");
        check(near(offset(), -10 * 0.2f / 720), "the first frame down by width x from / 720");
        v.stepQuake(1.0f / 60);
        check(near(offset(), 10 * (0.2f + 0.8f * (1.0f / 60) / (143.0f / 30)) / 720), "after a drawn frame's update: up, from .. to by the time");
        v.stepQuake(1.0f / 60), v.stepQuake(1.0f / 60);
        check(v.fieldQuake.parity == 0, "no second flip without another drawn frame");
        v.stepQuake(143.0f / 30);
        check(!v.fieldQuake.on, "off at its frames");
        check(near(offset(), 0), "nothing moved once off");
        vm.call("held");
        v.stepQuake(5.0f);
        check(v.fieldQuake.on && near(std::fabs(offset()), 30 * 0.3f / 720), "below-0 frames: held at `to` past the time");
        vm.call("stop");
        check(!v.fieldQuake.on, "StopQuake");
    }
    SDL_Quit();
    std::printf("quake: %d problems\n", problems);
    return problems ? 1 : 0;
}

int vistaPauseSelfTest() {
    const char* data = SDL_getenv("CR3_VITA_DATA");
    if (!data || !*data) data = SDL_getenv("CR3_DATA");
    if (!data || !*data) return 77;
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    {
        Viewer v;
        std::string error;
        if (!v.archive.open(data, &error) || !v.vitaButtons()) {
            std::fprintf(stderr, "Vita pause fixture: %s\n", error.c_str());
            return 1;
        }
        v.event = std::make_unique<cr3::EventRuntime>(v.archive, v.game);
        v.eventMode = true;
        cr3::ScriptVM vm;
        v.event->bindNatives(vm);
        const char* source = "function open(){EvtWindow.Open(1);} function show(){EvtWindow.SetVistaPauseButtonEnable(true);} function hide(){EvtWindow.SetVistaPauseButtonEnable(false);} function close(){EvtWindow.Close(1);}";
        if (SQ_FAILED(sq_compilebuffer(vm.vm(), source, SQInteger(std::strlen(source)), "pause-fixture", SQTrue)) || !vm.runMain() || !vm.call("open")) return 1;
        auto check = [&](bool ok, const char* what) {
            if (!ok) { ++problems; std::printf("vista pause FAILED: %s\n", what); }
        };
        check(!v.event->vistaPauseEnabled, "stop button starts hidden");
        check(v.vistaPad(SDL_GAMEPAD_BUTTON_NORTH) && !v.event->vistaPaused && v.vistaControlsUntil > v.uiClock,
              "first triangle reveals controls without requiring script permission");
        check(v.vistaPad(SDL_GAMEPAD_BUTTON_NORTH) && v.event->vistaPaused, "second triangle pauses");
        const int frame = v.event->frame();
        const float eventClock = v.eventClock;
        v.advance(5, 0, 0, false);
        check(v.event->frame() == frame && v.eventClock == eventClock, "paused scene does not advance even past the reveal timeout");
        check(v.vistaPad(SDL_GAMEPAD_BUTTON_EAST) && v.event->vistaPaused, "confirm cannot advance a paused scene");
        vm.call("hide");
        check(v.event->vistaPaused, "hiding the stop button does not resume the scene");
        vm.call("close");
        check(v.vistaPad(SDL_GAMEPAD_BUTTON_NORTH) && !v.event->vistaPaused, "triangle resumes even after the window closes");
        check(!v.vistaPad(SDL_GAMEPAD_BUTTON_NORTH), "closed Vista window does not capture triangle");
        vm.call("open"), vm.call("show");
        check(v.vistaPad(SDL_GAMEPAD_BUTTON_NORTH) && v.event->vistaPaused, "script-shown pause controls are immediately usable");
        v.vistaPad(SDL_GAMEPAD_BUTTON_NORTH);
        v.resetSessionUi();
        check(v.vistaControlsUntil < 0, "new session clears the reveal deadline");
    }
    SDL_Quit();
    std::printf("vista pause: %d problems\n", problems);
    return problems ? 1 : 0;
}

int saveSlotSafetySelfTest() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    {
        Viewer v;
        auto check = [&](bool ok, const char* what) { if (!ok) { ++problems; std::printf("save slot FAILED: %s\n",what); } };
        auto& m = v.saveMenu;
        m.slots[0].occupied = true; // an unreadable existing file
        v.chooseSave(Viewer::kTapSaveFile); v.chooseSave(Viewer::kTapSaveFile);
        check(!m.notice.empty() && !m.ask && v.cr3Title.stage != 5,"unreadable slot cannot start a new game");
        m.notice.clear(); m.mode = 2;
        check(!v.saveSlotOn(0),"copy cannot overwrite an unreadable occupied slot");
        m.mode = 5;
        check(!v.saveSlotOn(0),"New Game Plus cannot overwrite an unreadable occupied slot");
        m.mode = 0;
        v.chooseSave(Viewer::kTapSaveDelete);
        check(m.mode == 3 && v.saveSlotOn(0),"unreadable files remain available for explicit deletion");
        v.chooseSave(Viewer::kTapSaveFile); v.chooseSave(Viewer::kTapSaveFile);
        check(m.ask == 3,"deletion requires its first confirmation");
        v.chooseSave(Viewer::kTapSaveYes);
        check(m.ask == 4,"deletion still requires its second confirmation");
        m.ask = 0; m.mode = 0;
        m.slots[1].exists = true;
        for (auto& slot : m.slots) slot.occupied = true;
        v.chooseSave(Viewer::kTapSaveCopy);
        check(m.mode == 0,"copy requires a genuinely empty destination");
        m.slots[2].occupied = false;
        v.chooseSave(Viewer::kTapSaveCopy);
        check(m.mode == 1,"copy remains available with a readable source and empty destination");
    }
    SDL_Quit();
    std::printf("save slot safety: %d problems\n",problems);
    return problems ? 1 : 0;
}

// The game as the setup (viewer/setup.hpp) knows it: the first start's on a PC, a Vita dump copied
// into vita/ on any start
static cr3::SetupGame cr3SetupGame() {
    cr3::SetupGame setup;
    setup.title = "Chaos Rings III", setup.port = "CR3-U", setup.vitaId = "PCSG00500", setup.trilogyDir = "CR3";
    setup.obbHint = "asset packs (the .apks has them all)";
    setup.package = "com.square_enix.chaosrings3gp";
    setup.oneRelease = true; // (one copy is the game)
    auto releaseIs = [](const std::string& p, cr3::Database::Kind kind) {
        cr3::Database db;
        return db.open(p) && db.kind() == kind;
    };
    setup.iosOk = [releaseIs](const std::string& p) { return releaseIs(p, cr3::Database::Kind::Ios); };
    setup.androidOk = [releaseIs](const std::string& p) { return releaseIs(p, cr3::Database::Kind::Android); };
    setup.vitaOk = [releaseIs](const std::string& p) { return releaseIs(p, cr3::Database::Kind::Vita); };
    setup.patchOk = [](const std::string& p) {
        cr3::Database db;
        return db.addOverlay(p);
    };
    setup.patchName = [](const std::string& p) { // (translation/en/, translation/es/: its language's)
        cr3::Database db;
        if (!db.addOverlay(p)) return std::string();
        const std::string key = cr3::languageKey(cr3::overlayLanguage(db, 0)); // ("fan-en", "fan-es")
        return key.compare(0, 4, "fan-") == 0 ? key.substr(4) : key;
    };
    setup.untranslated = [](const std::string& p) { // (a Vita copy without the fan translation installed into it)
        cr3::Database db;
        return db.open(p) && db.kind() == cr3::Database::Kind::Vita && !cr3::fanPatchedInPlace(db);
    };
    return setup;
}

int main(int argc, char** argv) {
    if (argc>1 && std::strcmp(argv[1],"--selftest-vista-pause")==0) return vistaPauseSelfTest();
    if (argc > 1 && std::strcmp(argv[1], "--selftest-quake") == 0) return quakeSelfTest();
    if (argc>1 && std::strcmp(argv[1],"--selftest-save-slots")==0) return saveSlotSafetySelfTest();
    if (argc>1 && std::strcmp(argv[1],"--selftest-new-session")==0) return newSessionSelfTest();
    if (argc>1 && std::strcmp(argv[1],"--selftest-field-restore")==0) return fieldRestoreSelfTest();
    if (argc>1 && std::strcmp(argv[1],"--selftest-session-ui")==0) return sessionUiViewerSelfTest();
    if (argc > 1 && std::strcmp(argv[1], "--selftest-scene-audio") == 0) return sceneAudioViewerSelfTest();
    if (argc > 1 && std::strcmp(argv[1], "--selftest-lifecycle-clock") == 0) return lifecycleClockViewerSelfTest();
    if (argc > 1 && std::strcmp(argv[1], "--selftest-input-cancel") == 0) return inputCancelViewerSelfTest();
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    std::string data, model, capture, animName, renderer = "vulkan", lights = "d0101c", fieldName, at = "defaultPos", binary;
    // --patch: a fan translation over the release (Database::addOverlay; once for each); --language ja | en | fan-en | fan-es
    std::vector<std::string> patches; // (--patch, each; else translation/'s)
    std::string languageArg;
    std::string bgmArg, seArg, voiceArg; // --bgm m01, --se bt_001, --voice <name>: played at the start
    float walkX = 0, walkY = 0, walkSeconds = 0; // --walk: the field's character runs first (captures)
    bool tapAfter = false, mute = false, forceAudio = false;
    int originalArtArg = -1;
    std::string originalArtSource;
    int hdArg = -1;        // --hd-textures / --no-hd-textures (-1: the settings')
    int speedArg[2] = {0, 0}; // --field-speed / --battle-speed n (0: the settings')
    int smoothArg = -1;       // --smooth-models 0 | 2 | 3 (-1: the settings')
    bool hdModelsArg = false; // --hd-models (on whatever the settings say)
    std::string exportDir; // --export-textures [dir]: every image as <hash>.png (upright), for tools/hd_textures.py (textures/export/ in the game folder without one)
    bool exportTextures = false;
    bool exportModels = false; // --export-models [dir]: every figure as <figure>.glb (models/export/ in the game folder without one)
    std::string exportModelsDir;
    bool exportAnimations = true; // --no-animations: --export-models without them (they are for previewing; a character's run to hundreds of MB)
    std::string eventName, vista = "null", fontPath, playFrom, callEntry = "evt000";
    std::string areaName, areaPlayerArg; // --area <area> [--player <model>]: CR3's field
    std::string chapterJumpArg;
    int creditsArg = -1;
    int fightMode = 0;
    std::string inputArg; // (--input: the commands chosen, as tokens)
    std::vector<int> flagsArg;
    std::vector<std::pair<int, int>> genesArg; // (--gene id[:level]: a gene held to begin with - tests)
    int goldArg = -1, coinsArg = -1;
    int teamLevelArg = 0;
    std::vector<std::string> presentsArg; // (--present item:count | g<gene>:level: in the presents box - tests)
    bool continueSave = false;
    std::string gameSavePath;
    bool titleScreen = false; // (--title: the game's title screen first)
    std::vector<std::pair<int, int>> itemArg;
    std::vector<int> cr3PartyArg;
    std::vector<std::pair<int, int>> cr3ItemArgs; // (--cr3-item id:count - an item's full id -, tests)
    std::vector<std::array<int, 4>> cr3GeneArgs; // (--cr3-gene ch:gene[:exp[:skill]]: worn - its slots learnt, a skill added in slot 7 -, tests)
    std::vector<std::array<int, 3>> sheetArg; // (--cr3-sheet sheet:entry[:new] - entry -1 its prize)
    std::vector<std::tuple<int, int, int>> equipArg;
    int chargeArg = -1, difficultyArg = -1, membersArg[2] = {-2, -2}, ozArg = -1, gaugeArg = 0;
    std::vector<std::string> workArgs;
    std::vector<std::pair<int, int>> stateArg;
    std::vector<std::pair<int, int>> sopiaArg;
    bool skipBattlesOpt = false;
    uint32_t fightSeed = 1;
    int battleId = 0, battleParty[2] = {1, 2}; // (--battle: Darwin and Orlando unless --party)
    int battleFieldArg = 44; // (CR3's --battle-field: battle_field.mbe's; d1251b, the first battle's, unless given)
    float page = 3.0f;
    int width = 0, height = 0;    // the window (--size; else the resolution's, fitted to the display)
    int resW = 0, resH = 0;       // --resolution: the output frames (else the window's)
    std::string upscaler = "off", quality = "quality";
    float sharpness = 0.3f;
    float runSeconds = 0; // --run: a capture plays this long first, 60 frames a second
    std::string movieArg; // --movie cNN: that movie played at once (as Cr3System.PlayMovie)
    int fps = 60;         // --fps: the frame rate the window aims at (0: as fast as the display shows them)
    float yaw = 30, pitch = 15, startTime = 0;
    // no arguments (a double-click, Android's launcher): the game, from its title screen
    if (argc <= 1) titleScreen = true;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
        if (a == "--data" && next) data = next, ++i;
        else if (a == "--patch" && next) patches.push_back(next), ++i;
        else if (a == "--bgm" && next) bgmArg = next, ++i;
        else if (a == "--se" && next) seArg = next, ++i;
        else if (a == "--voice" && next) voiceArg = next, ++i;
        else if (a == "--language" && next) languageArg = next, ++i;
        else if (a == "--model" && next) model = next, ++i;
        else if (a == "--capture" && next) capture = next, ++i;
        else if (a == "--anim" && next) animName = next, ++i;
        else if (a == "--time" && next) startTime = float(std::atof(next)), ++i;
        else if (a == "--size" && next) std::sscanf(next, "%dx%d", &width, &height), ++i;
        else if (a == "--yaw" && next) yaw = float(std::atof(next)), ++i;
        else if (a == "--pitch" && next) pitch = float(std::atof(next)), ++i;
        else if (a == "--renderer" && next) renderer = next, ++i;
        else if (a == "--lights" && next) lights = next, ++i;
        else if (a == "--field" && next) fieldName = next, ++i;
        else if (a == "--at" && next) at = next, ++i;
        else if (a == "--binary" && next) binary = next, ++i;
        else if (a == "--walk" && next) std::sscanf(next, "%f,%f,%f", &walkX, &walkY, &walkSeconds), ++i;
        else if (a == "--event" && next) eventName = next, ++i;
        else if (a == "--area" && next) areaName = next, ++i;
        else if (a == "--chapter-jump" && next) chapterJumpArg = next, ++i; // (the game's own chapter_jump_<x_y> on a new game, then saved: tests)
        else if (a == "--player" && next) areaPlayerArg = next, ++i;
        else if (a == "--battle" && next) battleId = std::atoi(next), ++i;
        else if (a == "--battle-field" && next) battleFieldArg = std::atoi(next), ++i;
        else if (a == "--skip-battles") skipBattlesOpt = true;
        else if (a == "--fight" && next) fightMode = !std::strcmp(next, "pair") ? 2 : !std::strcmp(next, "manual") ? 3 : 1, ++i;
        else if (a == "--input" && next) inputArg = next, ++i;
        else if (a == "--flag" && next) flagsArg.push_back(std::atoi(next)), ++i; // (a story flag set to begin with: tests)
        else if (a == "--gene" && next) { // (a gene held to begin with, at its level: tests)
            const char* colon = std::strchr(next, ':');
            genesArg.push_back({std::atoi(next), colon ? std::atoi(colon + 1) : 1}), ++i;
        } else if (a == "--team-level" && next)
            teamLevelArg = std::atoi(next), ++i;
        else if (a == "--present" && next)
            presentsArg.push_back(next), ++i;
        else if (a == "--gold" && next)
            goldArg = std::atoi(next), ++i;
        else if (a == "--coins" && next)
            coinsArg = std::atoi(next), ++i;
        else if (a == "--sopia" && next) { // (a sopia's level: id:level, tests)
            int id = 0, level = 0;
            if (std::sscanf(next, "%d:%d", &id, &level) == 2) sopiaArg.push_back({id, level});
            ++i;
        }
        else if (a == "--seed" && next) fightSeed = uint32_t(std::atoi(next)), ++i;
        else if (a == "--state" && next) { // (a state a fighter has to begin with: fighter:state, tests)
            int f = 0, id = 0;
            if (std::sscanf(next, "%d:%d", &f, &id) == 2) stateArg.push_back({f, id});
            ++i;
        }
        else if (a == "--charge" && next) chargeArg = std::atoi(next), ++i; // (the charge gauge to begin with: tests)
        else if (a == "--gauge" && next) gaugeArg = std::atoi(next), ++i; // (the break gauge to begin with, -4..4: tests)
        else if (a == "--difficulty" && next) difficultyArg = std::atoi(next), ++i; // (the world select's level - 1: tests)
        else if (a == "--oz" && next) ozArg = std::atoi(next), ++i; // (the Oz to begin with: tests)
        else if (a == "--work" && next) workArgs.push_back(next), ++i; // (n:v - Cr3UtilGlobalWork n to begin with: tests)
        else if (a == "--members" && next) std::sscanf(next, "%d,%d", &membersArg[0], &membersArg[1]), ++i; // (the field's two, from 0: tests)
        else if (a == "--item" && next) { // (an item's count to begin with: id:count, tests)
            int id = 0, n = 0;
            if (std::sscanf(next, "%d:%d", &id, &n) == 2) itemArg.push_back({id, n});
            ++i;
        }
        else if (a == "--equip" && next) { // (a character's equipment slot: character:slot:id - 3..7 its sopia; tests)
            int c = 0, slot = 0, item = 0;
            if (std::sscanf(next, "%d:%d:%d", &c, &slot, &item) == 3) equipArg.push_back({c, slot, item});
            ++i;
        }
        else if (a == "--party" && next) std::sscanf(next, "%d,%d", &battleParty[0], &battleParty[1]), ++i;
        else if (a == "--cr3-sheet" && next) { // (an item sheet's entry won - sheet:entry, :1 new too; tests)
            int sh = 0, e = 0, fresh = 0;
            if (std::sscanf(next, "%d:%d:%d", &sh, &e, &fresh) >= 2) sheetArg.push_back({sh, e, fresh});
            ++i;
        }
        else if (a == "--cr3-party" && next) { // (CR3's party: up to five char_para ids, tests)
            int p[5] = {};
            std::sscanf(next, "%d,%d,%d,%d,%d", &p[0], &p[1], &p[2], &p[3], &p[4]);
            cr3PartyArg.assign(std::begin(p), std::end(p));
            ++i;
        }
        else if (a == "--cr3-item" && next) {
            int id = 0, n = 1;
            std::sscanf(next, "%d:%d", &id, &n);
            cr3ItemArgs.push_back({id, n});
            ++i;
        }
        else if (a == "--cr3-gene" && next) {
            std::array<int, 4> g{0, 0, 0, 0};
            std::sscanf(next, "%d:%d:%d:%d", &g[0], &g[1], &g[2], &g[3]);
            cr3GeneArgs.push_back(g);
            ++i;
        }
        else if (a == "--vista" && next) vista = next, ++i;
        else if (a == "--page" && next) page = float(std::atof(next)), ++i;
        else if (a == "--font" && next) fontPath = next, ++i;
        else if (a == "--tap") tapAfter = true;
        else if (a == "--mute") mute = true;
        else if (a == "--original-art") originalArtArg = 1;
        else if (a == "--release-art") originalArtArg = 0;
        else if (a == "--original-art-source" && next) originalArtSource = next, ++i;
        else if (a == "--hd-textures") hdArg = 1;
        else if (a == "--no-hd-textures") hdArg = 0;
        else if (a == "--field-speed" && next) speedArg[0] = std::clamp(std::atoi(next), 1, 5), ++i;
        else if (a == "--smooth-models" && next) smoothArg = std::atoi(next) == 2 || std::atoi(next) == 3 ? std::atoi(next) : 0, ++i;
        else if (a == "--hd-models") hdModelsArg = true;
        else if (a == "--battle-speed" && next) speedArg[1] = std::clamp(std::atoi(next), 1, 5), ++i;
        else if (a == "--export-textures") {
            exportTextures = true;
            if (next && next[0] != '-') exportDir = next, ++i;
        }
        else if (a == "--export-models") {
            exportModels = true;
            if (next && next[0] != '-') exportModelsDir = next, ++i;
        }
        else if (a == "--no-animations") exportAnimations = false;
        else if (a == "--audio") forceAudio = true; // (the device even under --capture: tests, SDL_AUDIO_DRIVER=dummy)
        else if (a == "--call" && next) callEntry = argv[++i]; // (--play: the map's event function, not its evt000)
        else if (a == "--credits" && next) creditsArg = std::atoi(argv[++i]); // (the staff roll n at once: --play's, for tests)
        else if (a == "--play") playFrom = next && next[0] != '-' ? (++i, next) : "d1020";
        else if (a == "--continue") continueSave = true; // (--play from the save the field's Save wrote)
        else if (a == "--game-save" && next) gameSavePath = next, ++i; // (--play from the game's own game.dat / cont.dat)
        else if (a == "--title") titleScreen = true;
        else if (a == "--resolution" && next) std::sscanf(next, "%dx%d", &resW, &resH), ++i;
        else if (a == "--upscaler" && next) upscaler = next, ++i;
        else if (a == "--quality" && next) quality = next, ++i;
        else if (a == "--fps" && next) fps = std::clamp(std::atoi(next), 0, 1000), ++i;
        else if (a == "--run" && next) runSeconds = float(std::atof(next)), ++i;
        else if (a == "--movie" && next) movieArg = next, ++i;
        else if (a == "--sharpness" && next) sharpness = std::clamp(float(std::atof(next)), 0.0f, 1.0f), ++i;
    }
    if (resW || resH) {
        if (resW < 320 || resH < 180 || resW > kMaxResW || resH > kMaxResH)
            return std::fprintf(stderr, "--resolution: 320x180 up to %dx%d\n", kMaxResW, kMaxResH), 2;
    }
    cr3::UpscaleSettings upscale;
    upscale.sharpness = sharpness;
    if (upscaler == "fsr3" || upscaler == "fsr") upscale.mode = cr3::Upscaler::Fsr3;
    else if (upscaler == "dlss") upscale.mode = cr3::Upscaler::Dlss;
    else if (upscaler == "metalfx") upscale.mode = cr3::Upscaler::MetalFx;
    else if (upscaler == "fsr4") upscale.mode = cr3::Upscaler::Fsr4;
    else if (upscaler != "off") return std::fprintf(stderr, "--upscaler: off, fsr3, fsr4, dlss or metalfx\n"), 2;
    {
        const auto q = std::find_if(std::begin(kQualities), std::end(kQualities), [&](const Quality& k) { return quality == k.name; });
        if (q == std::end(kQualities)) return std::fprintf(stderr, "--quality: native, quality, balanced, performance or ultra\n"), 2;
        upscale.ratio = q->ratio;
    }
    std::vector<std::string> searched; // (where a copy was looked for: the message if none)
    if (data.empty()) { // (CR3_DATA, else a copy in a game folder: vita/, android/, ios/ ...)
        searched = dataPlaces();
        for (const std::string& place : searched) { // (a Vita dump copied into vita/ - a phone's way -, its files encrypted: decrypted into place)
            cr3::SetupResult got;
            if (cr3::installDroppedVita(cr3SetupGame(), place, got, !capture.empty())) break;
        }
        data = cr3::defaultArchive(searched);
        if (data.empty()) data = storedSetting("data="); // (an older setup's: an Android or Vita copy where it lay)
    }
    data = cr3::resolveArchive(data); // (a folder: the OBB or the CRDB.*.mvgl in it, a Vita copy's PCSG00498 ...)
#if !defined(SDL_PLATFORM_IOS)
    if (data.empty() && (argc <= 1 || cr3::setupScripted()) && !std::getenv("CR3_DATA")) { // the first start without a copy: where is it? (viewer/setup.hpp - a phone's too)
        SDL_Init(SDL_INIT_VIDEO); // (its message boxes and the system's dialogs)
        const cr3::SetupGame setup = cr3SetupGame();
        std::string profile;
#ifdef SDL_PLATFORM_ANDROID
        if (!searched.empty()) // (the folders made, to be seen over USB)
            for (const char* f : {"ios", "android", "vita"}) SDL_CreateDirectory((searched.front() + f).c_str());
#else
        if (char* pref = SDL_GetPrefPath("DuIslingr", "CR3-U")) profile = pref, SDL_free(pref); // (a phone's game folder is always its own: no other)
#endif
        cr3::SetupResult got;
        if (!cr3::runSetup(setup, searched.empty() ? profile : searched.front(), profile, got)) return 0;
        // (copied into the game folder - ios/, android/, vita/, translation/ -: found as on any start; a dump copied in meanwhile decrypted)
        for (const std::string& place : dataPlaces()) {
            cr3::SetupResult dropped;
            if (cr3::installDroppedVita(setup, place, dropped, !capture.empty())) break;
        }
        data = cr3::resolveArchive(cr3::defaultArchive(dataPlaces()));
        SDL_Log("setup: iOS %s, Android %s, Vita %s, its translation %s - the game: %s", got.ios.c_str(), got.android.c_str(), got.vita.c_str(),
                got.patch.c_str(), data.c_str());
        if (cr3::setupScripted()) return data.empty() ? 1 : 0; // (a test's: the setup alone)
    }
#endif
    if (data.empty() && argc <= 1) { // (opened with nothing: a window, not the terminal's usage)
#ifdef SDL_PLATFORM_ANDROID
        if (!searched.empty()) // (the folders made, to be seen over USB)
            for (const char* f : {"ios", "android", "vita"}) SDL_CreateDirectory((searched.front() + f).c_str());
#endif
        std::string msg = "Could not find your copy of the game.\n\nPut any one of these in the folder here:\n";
        msg += searched.empty() ? std::string("(no folder)") : searched.front();
        msg += "\n\n  ios/      the iOS ChaosRings3_FINAL_ENG.app (or the unpacked .ipa)"
               "\n  android/  the Android release's .apks (or its base.apk with the asset packs)"
               "\n  vita/     the Vita's PCSG00500 folder (or the Prequel Trilogy's PCSG00457), or a NoNpDrm dump's folder"
               "\n            (decrypted here on the next start)\n\nAlso looked in:";
        for (size_t k = 1; k < searched.size(); ++k) msg += "\n" + searched[k];
        msg += "\n\nThe README's \"Your copy\" has the details.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chaos Rings III", msg.c_str(), nullptr);
        return 2;
    }
    if (data.empty()) {
        std::fprintf(stderr, "cr3view --data <archive> [--model name] [--anim name] [--time s] [--size WxH] [--yaw deg] "
                             "[--pitch deg] [--lights figure|none] [--field map [--at point] [--binary file]] "
                             "[--event function [--vista map] [--page s]] [--resolution WxH] [--upscaler off|fsr3|dlss "
                             "[--quality native|quality|balanced|performance|ultra] [--sharpness 0..1]] "
                             "[--renderer vulkan|opengl] [--capture out.bmp]\n");
        return 2;
    }
    // the game folder (textures/, translation/ go there) and its HD textures: textures/hd/ - on, unless
    // settings.ini says otherwise, when it is there (CR3_HD_TEXTURES elsewhere; the older hd-textures/
    // beside the executable or in the profile folder while there is no textures/hd/)
    const std::string gameFolder = searched.empty() ? dataPlaces().front() : searched.front();
    std::string hdFolder;
    bool hdBundled = true, patchFound = false;
    {
        auto isDir = [](const std::string& p) {
            SDL_PathInfo info;
            return !p.empty() && SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
        };
        const char* env = SDL_getenv("CR3_HD_TEXTURES");
        const char* base = SDL_GetBasePath();
        const std::string hd = gameFolder + "textures/hd/", beside = base ? std::string(base) + "hd-textures" : std::string();
        if (env && *env) hdFolder = env, hdBundled = false;
        else if (isDir(hd)) hdFolder = hd;
        else if (isDir(beside)) hdFolder = beside;
        else if (isDir(Viewer::savePath("hd-textures"))) hdFolder = Viewer::savePath("hd-textures"), hdBundled = false;
        else hdFolder = hd, hdBundled = false;
        if (!hdFolder.empty() && hdFolder.back() != '/' && hdFolder.back() != '\\') hdFolder += '/';
        // the fan translation (--patch, else translation/ in the game folder: its rePatch/ or PCSG00500/ - for the Vita release)
        // (each in a folder of its own there - en/, es/ -: the English one and the Spanish one both)
        if (patches.empty()) cr3::installDroppedPatch(cr3SetupGame(), gameFolder, !capture.empty()); // (a .rar / .zip / .7z copied into translation/: unpacked)
        if (patches.empty()) {
            if (isDir(gameFolder + "translation")) patches.push_back(gameFolder + "translation");
            if (isDir(Viewer::savePath("translation"))) patches.push_back(Viewer::savePath("translation")); // (the profile folder's: the setup's, where the game folder can't be written)
            const std::string stored = storedSetting("patch="); // (the first start's setup's)
            if (!stored.empty()) patches.push_back(stored);
            patchFound = !patches.empty();
        }
    }
    Viewer v;
    { // CR3-U's settings (settings.ini; not for captures and exports), the command line's flags over them
        auto given = [&](const char* flag) {
            for (int i = 1; i < argc; ++i)
                if (std::strcmp(argv[i], flag) == 0) return true;
            return false;
        };
        Viewer::Settings& s = v.settings;
        if (capture.empty() && !exportTextures && !exportModels) {
            v.settingsPersist = true;
            v.loadSettings();
            if (!given("--page")) page = 0; // (played: a page waits for a tap, as the game's - --page n turns them by themselves, its voice heard out)
            if (!given("--renderer")) renderer = s.renderer ? "opengl" : "vulkan";
            if (!given("--fps")) fps = s.fps;
            if (!given("--resolution")) resW = kResolutions[s.resolution][0], resH = kResolutions[s.resolution][1];
            if (!given("--upscaler")) upscale.mode = cr3::Upscaler(s.upscaler);
            if (!given("--quality")) upscale.ratio = kQualities[s.quality].ratio;
            if (!given("--sharpness")) upscale.sharpness = float(s.sharpness) / 100.0f;
            if (hdArg < 0) hdArg = s.hdSet ? (s.hdTextures ? 1 : 0) : hdBundled ? 1 : 0;
        }
        if (originalArtArg >= 0) s.originalCardArt = originalArtArg != 0;
        // (what runs, as the list shows it)
        s.renderer = renderer == "opengl" ? 1 : 0;
        s.fps = fps;
        s.resolution = -1;
        for (size_t k = 0; k < std::size(kResolutions); ++k)
            if (kResolutions[k][0] == resW && kResolutions[k][1] == resH) s.resolution = int(k);
        s.upscaler = int(upscale.mode);
        for (size_t k = 0; k < std::size(kQualities); ++k)
            if (kQualities[k].ratio == upscale.ratio) s.quality = int(k);
        s.sharpness = int(std::lround(upscale.sharpness * 100.0f));
        s.hdTextures = hdArg == 1;
    }
    std::string err;
    if (!v.archive.open(data, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        if (argc <= 1) // (opened with nothing - a double-click -: said in a window, not only to a terminal)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chaos Rings III", ("The copy of the game in\n" + data + "\ncan't be read: " + err).c_str(), nullptr);
        return 1;
    }
    SDL_Log("data: %s", data.c_str());
    if (patchFound && v.archive.kind() != cr3::MvglArchive::Kind::Vita) patches.clear(); // (the others are in English already)
    for (const std::string& p : patches) addTranslations(v.archive, p, !patchFound);
    { // the add-ons: models/addons/ in the game folder (CR3_ADDONS elsewhere) - before anything reads the tables
        const char* env = SDL_getenv("CR3_ADDONS");
        std::string dir = env && *env ? std::string(env) : gameFolder + "models/addons/";
        v.addons.load(v.archive, dir);
        for (const cr3::Cr3Addon& a : v.addons.list())
            SDL_Log("add-on: %s %d \"%s\" for %s (item %d, %s)", a.weapon ? "weapon" : "outfit", a.id, a.name.c_str(), a.wearer.c_str(), a.item, a.figure.c_str());
        for (const std::string& p : v.addons.problems()) SDL_Log("add-on left out: %s", p.c_str());
    }
    if (v.offlineDb.load(v.archive, &err)) {
        v.addons.addGoods(v.offlineDb);
        SDL_Log("database: offline, %zu server tables, %zu item sheets", v.offlineDb.tables().size(), v.offlineDb.itemSheets().size());
    } else if (v.archive.kind() != cr3::Database::Kind::None)
        SDL_Log("database: %s", err.c_str());
    v.resetGame();
    {
        // the language: --language, else English where the release has one (the iPhone's, Android's,
        // the fan translation's), else Japanese
        // (else the one the options last picked)
        cr3::Language want = cr3::Language::FanEnglish;
        if (!languageArg.empty() && !cr3::parseLanguage(languageArg, want)) SDL_Log("--language %s: ja, en, fan-en or fan-es", languageArg.c_str());
        else if (languageArg.empty() && v.settings.language > 0) want = cr3::Language(v.settings.language - 1);
        const cr3::LanguageChoice l = cr3::applyLanguage(v.archive, want);
        v.languages = cr3::releaseLanguages(v.archive);
        for (size_t i = 0; i < v.languages.size(); ++i)
            if (v.languages[i].id == l.id) v.useLanguage(i, false);
        v.englishTextures = l.slot == 1; // (the release's own English: its images_eng)
        std::string offered;
        for (const cr3::LanguageChoice& c : v.languages) offered += (offered.empty() ? "" : ", ") + std::string(c.label);
        if (v.languages.size() > 1) SDL_Log("languages: %s", offered.c_str());
        SDL_Log("%s release, %s (messages' slot %d)%s", v.archive.kindName(), l.label, l.slot,
                l.overlay >= 0 ? (" over " + v.archive.overlayPath(l.overlay)).c_str() : "");
    }
    if (!v.archive.unpackFailed().empty()) { // (an .apks' asset packs that couldn't be unpacked beside it: no room, no access)
        std::string names;
        for (const std::string& n : v.archive.unpackFailed()) names += (names.empty() ? "" : ", ") + n;
        SDL_Log("asset packs not unpacked: %s", names.c_str());
        if (capture.empty())
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Chaos Rings III",
                                     ("Couldn't unpack the game's asset packs from the .apks (" + names +
                                      ") into the assetpacks folder beside it - is there about 1.3 GB free?\n\n"
                                      "The game will run without its sound, voices and movies.").c_str(),
                                     nullptr);
    }
    {
        std::vector<std::string> sources;
        if (!originalArtSource.empty()) sources.push_back(originalArtSource);
        if (const char* env = SDL_getenv("CR3_ORIGINAL_ART"); env && *env) sources.push_back(env);
        for (const auto& place : dataPlaces())
            for (const char* sub : {"artwork/", "vita/", "ios/", "ipad/"}) sources.push_back(place + sub);
        v.cardArtwork.configure(v.archive, std::move(sources));
        v.cardArtwork.setEnabled(v.settings.originalCardArt);
    }
    v.hdDir = hdFolder;
    { // (movies/: cNN.mp4 the release lacks - the iPad's c04..c11 - from another; CR3_MOVIES elsewhere)
        const char* env = SDL_getenv("CR3_MOVIES");
        v.moviesDir = env && *env ? std::string(env) : gameFolder + "movies";
    }
    if (hdArg >= 0) v.hdTextures = hdArg == 1;
    { // the HD models: models/hd/ in the game folder (CR3_HD_MODELS elsewhere)
        const char* env = SDL_getenv("CR3_HD_MODELS");
        v.modelsDir = env && *env ? std::string(env) : gameFolder + "models/hd/";
        if (!v.modelsDir.empty() && v.modelsDir.back() != '/' && v.modelsDir.back() != '\\') v.modelsDir += '/';
        if (hdModelsArg) v.settings.hdModels = true;
        v.hdModels = v.settings.hdModels;
    }
    if (smoothArg >= 0) v.settings.smoothModels = smoothArg;
    if (speedArg[0]) v.settings.fieldSpeed = speedArg[0];
    if (speedArg[1]) v.settings.battleSpeed = speedArg[1];
    if (exportModels) { // every figure as glTF (assets/gltf_figure.hpp): its skeleton, meshes, materials, pictures, animations
        std::string dir = exportModelsDir.empty() ? gameFolder + "models/export/" : exportModelsDir;
        if (dir.back() != '/' && dir.back() != '\\') dir += '/';
        if (!SDL_CreateDirectory(dir.c_str()) && exportModelsDir.empty()) dir = Viewer::savePath("models/export/"), SDL_CreateDirectory(dir.c_str());
        std::printf("exporting to %s\n", dir.c_str());
        cr3::ShaderLibrary shaders(v.archive);
        int written = 0, failed = 0, empty = 0;
        size_t bytes = 0;
        for (const auto& e : v.archive.entries()) {
            if (e.name.size() < 6 || e.name.compare(e.name.size() - 5, 5, ".geom") != 0) continue;
            const std::string base = e.name.substr(0, e.name.size() - 5);
            if (v.addons.byFigure(base)) continue; // (an add-on's: its base's under another name)
            std::vector<uint8_t> d;
            cr3::Geom g;
            cr3::Skeleton sk;
            std::string why;
            if (!v.archive.read(e.name, d, &why) || !cr3::parseGeom(d, g, &why)) { std::printf("  %s: %s\n", e.name.c_str(), why.c_str()); ++failed; continue; }
            if (v.archive.read(base + ".skel", d)) cr3::parseSkeleton(d, sk);
            for (cr3::GeomMesh& m : g.meshes) // (the joints as MatrixPalette's rows, as loadFigure resolves them)
                if (m.material < g.materials.size()) {
                    const std::array<uint32_t, 4>& key = g.materials[m.material].shaderKey;
                    const bool builtIn = !key[0] && !key[1] && !key[2] && !key[3];
                    m.resolveJoints(builtIn ? (g.version == 0x3c ? 3.0f : 1.0f) : shaders.jointScale(key));
                }
            cr3::FigureExport fe;
            fe.geom = &g, fe.skeleton = &sk;
            if (v.archive.read(base + ".name", d)) fe.jointNames = cr3::parseJointNames(d), fe.materialNames = cr3::parseMaterialNames(d);
            {
                std::vector<uint32_t> hashes;
                for (const cr3::GeomMaterial& m : g.materials) hashes.push_back(m.hash);
                if (!fe.materialNames.empty()) fe.materialNames = cr3::orderNamesByHash(fe.materialNames, hashes);
            }
            for (const cr3::GeomMaterial& m : g.materials)
                for (const auto& [id, picture] : m.textures) {
                    const std::string& img = picture; // (a lambda can't take a structured binding before C++20 - Apple's clang)
                    if (img.empty() || std::any_of(fe.images.begin(), fe.images.end(), [&](const auto& p) { return p.first == img; })) continue;
                    cr3::Image im;
                    if (v.decode(img, im)) fe.images.emplace_back(img, std::move(im));
                }
            std::vector<std::unique_ptr<cr3::Animation>> anims;
            for (const auto& a : v.archive.entries()) // (<figure>*.anim: c217.anim, c217e080.anim, ...)
                if (exportAnimations && a.name.size() > 5 && a.name.compare(0, base.size(), base) == 0 && a.name.compare(a.name.size() - 5, 5, ".anim") == 0) {
                    auto an = std::make_unique<cr3::Animation>();
                    if (v.archive.read(a.name, d) && an->parse(d) && an->joints() == int(sk.basePose.size()))
                        fe.anims.emplace_back(a.name.substr(0, a.name.size() - 5), an.get()), anims.push_back(std::move(an));
                }
            // (a character's or a monster's motions move over the base pose; a map's or an object's own replace it)
            fe.absolute = !(base.size() > 1 && (base[0] == 'c' || base[0] == 'm' || base[0] == 'v') && std::isdigit(static_cast<unsigned char>(base[1])));
            cr3::GltfStats st;
            if (cr3::exportFigureGlb(fe, dir + base + ".glb", &why, &st)) {
                ++written, bytes += st.bytes;
                if (SDL_getenv("CR3_EXPORT_VERBOSE"))
                    std::printf("  %s: %d meshes, %d triangles, %d pictures, %d animations (%d channels), %zu KB\n", base.c_str(), st.meshes,
                                st.triangles, st.images, st.animations, st.channels, st.bytes / 1024);
            } else if (why == "no meshes") {
                ++empty; // (a figure of lights, cameras or anchors only)
            } else {
                std::printf("  %s: %s\n", base.c_str(), why.c_str());
                ++failed;
            }
        }
        std::printf("%d figures written (%zu MB), %d without meshes, %d failed\n", written, bytes >> 20, empty, failed);
        return failed && !written ? 1 : 0;
    }
    if (exportTextures) { // (the archive's every image, once each by its pixels: tools/hd_textures.py's input)
        if (exportDir.empty()) exportDir = gameFolder + "textures/export/"; // (else the profile folder's, if that can't be written)
        if (exportDir.back() != '/' && exportDir.back() != '\\') exportDir += '/';
        if (!SDL_CreateDirectory(exportDir.c_str())) exportDir = Viewer::savePath("textures/export/"), SDL_CreateDirectory(exportDir.c_str());
        std::printf("exporting to %s\n", exportDir.c_str());
        std::set<uint64_t> seen;
        int written = 0, failed = 0, dupes = 0, kinds[4] = {0, 0, 0, 0};
        for (const auto& e : v.archive.entries()) {
            if (e.name.rfind("images\\", 0) != 0 || e.name.size() < 12 || e.name.compare(e.name.size() - 4, 4, ".img") != 0) continue;
            std::vector<uint8_t> file;
            cr3::Image img;
            if (!v.archive.read(e.name, file) || !cr3::decodeImg(file, img)) { ++failed; continue; }
            const uint64_t h = cr3::imageHash(img);
            if (!seen.insert(h).second) { ++dupes; continue; }
            const std::string path = exportDir + cr3::hashName(h) + ".png";
            if (SDL_GetPathInfo(path.c_str(), nullptr)) continue; // (already there)
            if (cr3::savePngImage(path, img)) ++written, ++kinds[cr3::imageStorage(img)];
            else ++failed;
        }
        std::printf("exported %d pictures to %s (%d lossless, %d block-compressed, %d 16-bit, %d unknown); %d alike, %d failed\n",
                    written, exportDir.c_str(), kinds[1], kinds[2], kinds[3], kinds[0], dupes, failed);
        return failed ? 1 : 0;
    }
    // the sound: played unless captured (or --mute) - then silent, its state kept all the same
    const bool device = (capture.empty() || forceAudio) && !mute;
    // CR3's (audio/cr3sound.hpp: data\bgm.csv's tracks, sound\*), else CR2-U's numbered banks
    static cr3::Cr3Sound cr3sound;
    if (cr3sound.open(v.archive, &err)) {
        v.audio.openCr3(cr3sound, device, &err);
        SDL_Log("sound: %zu tracks, %zu effects, %zu voices%s", cr3sound.tracks().size(), cr3sound.effects().size(), cr3sound.voices().size(),
                device ? "" : " (silent)");
        if (!bgmArg.empty()) v.audio.playBgm(bgmArg, 0);
        if (!seArg.empty()) v.audio.playSe(seArg, false);
        if (!voiceArg.empty()) v.audio.playVoice(voiceArg);
    } else if (!v.audio.open(data, device, &err)) SDL_Log("sound: %s", err.c_str());
    else SDL_Log("sound: on%s%s", device ? "" : " (silent)", err.empty() ? "" : (" - " + err).c_str());
    for (const auto& e : v.archive.entries())
        if (e.name.size() > 5 && e.name.compare(e.name.size() - 5, 5, ".geom") == 0) v.models.push_back(e.name);
    std::sort(v.models.begin(), v.models.end());
    if (v.models.empty()) return std::fprintf(stderr, "no models in %s\n", data.c_str()), 1;
    const bool cr3Continue = continueSave && !v.cr3SavedArea().empty();
    const bool leaderDefaulted = model.empty() && (!fieldName.empty() || !playFrom.empty() || (continueSave && !cr3Continue));
    if (leaderDefaulted) model = "c001"; // (the leader: Darwin - then the field's, refreshLeader)
    size_t start = 0;
    if (!model.empty()) {
        const std::string want = model.size() > 5 && model.compare(model.size() - 5, 5, ".geom") == 0 ? model : model + ".geom";
        const auto it = std::find(v.models.begin(), v.models.end(), want);
        if (it == v.models.end()) return std::fprintf(stderr, "%s: not in the archive\n", want.c_str()), 1;
        start = size_t(it - v.models.begin());
    }
    v.yaw = yaw, v.pitch = pitch;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return std::fprintf(stderr, "SDL: %s\n", SDL_GetError()), 1;
    // The window: --size, else a capture's the resolution itself, else the resolution's shape
    // fitted to 90% of the display (the frames stay the resolution, scaled into it)
    if (!width || !height) {
        width = resW ? resW : 1280, height = resW ? resH : 800;
        SDL_Rect usable{};
        if (capture.empty() && SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0 && usable.h > 0) {
            const float fit = std::min({1.0f, 0.9f * float(usable.w) / float(width), 0.9f * float(usable.h) / float(height)});
            width = std::max(int(float(width) * fit), 320), height = std::max(int(float(height) * fit), 180);
        }
    }
    // The device: Vulkan unless asked otherwise or it can't start, then OpenGL
    cr3::useBundledVulkan(); // (macOS: the .app's own loader)
#ifdef CR_HAVE_VULKAN
    { // FSR 4's files (AMD's DLLs, Linux's ffx-vk-linux): fsr4/ in the game folder, beside the executable, CR3_FSR4_DIR
        const char* env = SDL_getenv("CR3_FSR4_DIR");
        const char* base = SDL_GetBasePath();
        cr3::setFsr4Dirs({env && *env ? std::string(env) : std::string(), gameFolder + "fsr4/", base ? std::string(base) + "fsr4/" : std::string()});
    }
#endif
    // (the Vulkan pipelines compiled in earlier runs: the profile's, as the settings are kept - not
    // a capture's or an export's; CR3_PIPELINE_CACHE names another file, "" none)
    if (const char* pc = SDL_getenv("CR3_PIPELINE_CACHE")) cr3::setPipelineCacheFile(pc);
    else if (v.settingsPersist) cr3::setPipelineCacheFile(Viewer::savePath("vulkan-pipelines.bin"));
    std::unique_ptr<cr3::RenderDevice> dev;
    SDL_Window* win = nullptr;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    const SDL_WindowFlags shown = capture.empty() ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_HIDDEN; // (the system's bars hidden from the start)
#else
    const SDL_WindowFlags shown = capture.empty() ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_HIDDEN;
#endif
    for (const cr3::Backend b : {cr3::Backend::Vulkan, cr3::Backend::OpenGL}) {
        if (b == cr3::Backend::Vulkan && renderer != "vulkan") continue;
        std::unique_ptr<cr3::RenderDevice> d = b == cr3::Backend::Vulkan ? cr3::makeVkDevice() : cr3::makeGlDevice();
        if (!d) continue;
        win = SDL_CreateWindow(titleScreen ? "Chaos Rings III" : "CR3-U viewer", width, height, SDL_WindowFlags(d->windowFlags()) | shown);
        if (win && d->init(win)) {
            dev = std::move(d);
            SDL_Log("renderer: %s", dev->description().c_str());
            break;
        }
        SDL_Log("%s: could not start (%s)%s", cr3::backendName(b), SDL_GetError(), b == cr3::Backend::Vulkan ? ", trying OpenGL" : "");
        if (d) d->shutdown();
        if (win) SDL_DestroyWindow(win);
        win = nullptr;
    }
    if (!dev) return std::fprintf(stderr, "no renderer could start\n"), 1;
    v.dev = dev.get();
    v.resW = resW, v.resH = resH;
    if (upscale.mode != cr3::Upscaler::Off) {
        const std::string why = dev->upscalerProblem(upscale.mode);
        if (!why.empty()) SDL_Log("upscaler %s: %s - off", upscaler.c_str(), why.c_str()), upscale.mode = cr3::Upscaler::Off;
    }
    v.upscale = upscale;
    dev->setUpscaler(upscale);
    v.window = capture.empty() ? win : nullptr; // (a capture's hidden window stays as it is)
    v.rendererInUse = dev->offscreen() ? 0 : 1;
    v.settings.upscaler = int(upscale.mode); // (one that can't run here: off)
    v.applySettings();
    v.shaders = std::make_unique<cr3::ShaderLibrary>(v.archive);
    // (nothing asked for - the release's own launch -: the title)
    if (model.empty() && animName.empty() && fieldName.empty() && eventName.empty() && areaName.empty() && battleId <= 0 && !continueSave &&
        gameSavePath.empty() && playFrom.empty() && creditsArg < 0 && bgmArg.empty() && seArg.empty() && voiceArg.empty())
        titleScreen = true;
    const bool cr3Title = titleScreen && v.archive.contains("ttlLogoMenu.geom"); // (CR3's title: its own flow, into the prologue)
    if (titleScreen && playFrom.empty() && !cr3Title) playFrom = "d1020"; // (the title's New Game: --play's start)
    if (!playFrom.empty()) fieldName = playFrom, v.playMode = true;
    v.scriptSaves = gameSaves(!playFrom.empty() || cr3Title, continueSave, !capture.empty());
    if (continueSave && !cr3Continue && fieldName.empty()) fieldName = "d1020"; // (the windows and fonts set up for the field)
    v.skipBattles = skipBattlesOpt; // (a new game: every flag clear)
    if (!fieldName.empty() || !eventName.empty() || battleId > 0 || cr3Title || cr3Continue || !areaName.empty()) {
        // the font: --font, CR3_FONT, the Vita's beside its archive, the Android release's APK beside
        // its OBB (assets/00001: the Regular face it draws English in), else the system's
        std::string dataDir = data.substr(0, data.find_last_of("/\\") == std::string::npos ? 0 : data.find_last_of("/\\") + 1);
        const char* envFont = SDL_getenv("CR3_FONT");
        std::string fp = cr3::TextRasterizer::firstFont({fontPath, envFont ? envFont : "", dataDir + "font/TBGoPro-Bold.otf"});
        std::vector<uint8_t> apkFont;
        std::string apkFontFrom;
        // CR3: the release's own (Database: F0001.bin - iOS, Android -, the Vita's font\TBGB_0KP.TTC):
        // TB Gothic B, its proportional face (1) the English's
        int fontFace = 0;
        if (fp.empty())
            for (const char* f : {"F0001.bin", "font\\TBGB_0KP.TTC"})
                if (v.archive.read(f, apkFont)) {
                    apkFontFrom = v.archive.sourceOf(f), fontFace = 1;
                    v.archiveFont = f, v.archiveFontFrom = apkFontFrom; // (a language changed: its translation's own, if it has one)
                    break;
                }
        if (fp.empty() && apkFont.empty() && cr3::androidFile(data, "assets/F0001.bin", apkFont, &apkFontFrom))
            fontFace = 1; // (the APK's own, an .apks' too: TB Gothic B, its proportional face)
        v.loadMessageWindow(fp, std::move(apkFont), apkFontFrom, fontFace);
        v.initEventDrawing();
        v.pageSeconds = page;
    }
    v.newGame();
    // (the test's story flags and sopia levels: before the field's entry or the battle runs)
    for (int f : flagsArg) v.game.setFlag(f, true);
    if (v.game.cr3Db) { // (utils::GetGene's: its level's EXP, its own commands and skills)
        for (const auto& [id, lv] : genesArg) {
            cr3::Cr3Gene g(id, v.game.cr3Db->geneExp(id, std::max(lv, 1)));
            v.game.cr3Db->initGene(g);
            v.game.cr3Genes.push_back(g);
            v.game.cr3GeneBook.insert(id);
        }
        if (goldArg >= 0) v.game.cr3Money = goldArg;
        if (teamLevelArg > 0) v.game.cr3TeamLevel = teamLevelArg;
        if (coinsArg >= 0) v.game.cr3Coins = coinsArg;
        for (const std::string& a : presentsArg) {
            cr3::Cr3Present p;
            const size_t colon = a.find(':');
            const int n = colon == std::string::npos ? 1 : std::atoi(a.c_str() + colon + 1);
            if (!a.empty() && a[0] == 'g') p.gene = true, p.geneData = cr3::Cr3Gene(std::atoi(a.c_str() + 1), v.game.cr3Db->geneExp(std::atoi(a.c_str() + 1), std::max(n, 1)));
            else p.item = std::atoi(a.c_str()), p.count = std::max(n, 1);
            p.from = v.game.cr3Db->constant("PRESENT_FROM_SERVICE");
            v.game.cr3Db->sendPresent(v.game, p);
        }
    }
    for (const auto& [id, level] : sopiaArg)
        if (id >= 0 && id < 32) v.game.sopiaLevel[id] = static_cast<signed char>(level);
    for (size_t k = 0; k < cr3PartyArg.size() && k < 5; ++k) v.game.cr3Party[k] = cr3PartyArg[k];
    for (const auto& [id, n] : cr3ItemArgs) v.game.cr3Items[id] = n;
    for (const auto& [ch, gene, exp, skill] : cr3GeneArgs) {
        v.game.cr3Genes.emplace_back(gene, exp);
        if (v.game.cr3Db) v.game.cr3Db->initGene(v.game.cr3Genes.back());
        if (skill > 0) v.game.cr3Genes.back().slots[6][0] = 1, v.game.cr3Genes.back().slots[6][1] = skill;
        v.game.cr3GeneOf[ch] = v.game.geneUid(v.game.cr3Genes.back());
    }
    for (const auto& [sh, e, fresh] : sheetArg) {
        if (!v.game.cr3Db || sh < 0 || size_t(sh) >= v.game.cr3Db->itemSheets().size()) continue;
        const cr3::Cr3ItemSheet& sheet = v.game.cr3Db->itemSheets()[size_t(sh)];
        cr3::Cr3SheetProgress& pr = v.game.cr3Sheets[sheet.id];
        pr.opened.resize(sheet.entries.size()), pr.mark.resize(sheet.entries.size());
        if (e == -1) { // (sheet:-1[:1] - its prize won, new)
            pr.completed = std::max(pr.completed, 1), pr.completedNew = fresh != 0;
            continue;
        }
        if (e < 0 || size_t(e) >= sheet.entries.size()) continue;
        pr.opened[size_t(e)] = std::max(pr.opened[size_t(e)], 1), pr.mark[size_t(e)] = fresh != 0;
    }
    for (const auto& [id, n] : itemArg) {
        if (id > 0 && id < 300) v.game.itemCount[id] = static_cast<short>(n);
        if (id > 0) v.game.cr3Items[id] = n; // (CR3's: by the full id)
    }
    for (const auto& [c, slot, item] : equipArg)
        if (c >= 1 && c <= 9 && slot >= 0 && slot < 8) v.game.equip[c - 1][slot] = static_cast<short>(item);
    if (difficultyArg >= 0) v.game.difficulty = difficultyArg;
    if (ozArg >= 0) v.game.oz = ozArg;
    for (const std::string& w : workArgs) {
        const size_t c = w.find(':');
        const int n = std::atoi(w.c_str()), val = c == std::string::npos ? 0 : std::atoi(w.c_str() + c + 1);
        if (n >= 0 && n < int(std::size(v.game.globalWork))) v.game.globalWork[size_t(n)] = val;
    }
    if (membersArg[0] > -2) v.game.party[0] = membersArg[0], v.game.party[1] = membersArg[1];
    if (cr3Title) {
        std::shared_ptr<vitaimport::Session::Choice> sessionPicker;
        if (SDL_getenv("CR_SELFTEST_SESSION_UI")) sessionPicker = dirtySessionUiForTest(v);
        int oldEffect = -1;
        const bool checkAudio = SDL_getenv("CR_SELFTEST_SCENE_AUDIO") != nullptr;
        if (checkAudio) {
            if (cr3sound.voices().empty() || cr3sound.effects().empty())
                return std::fprintf(stderr,"scene audio test needs the sound asset packs\n"),1;
            if (v.audio.playVoice(cr3sound.voices().front()) < 0) return 1;
            oldEffect = v.audio.playSe(cr3sound.effects().front(), true);
            if (oldEffect < 0) return 1;
            v.voiceJobs.push_back({10,123});
            v.seJobs.push_back({10,456});
            v.pendingBgm = 15, v.pendingBgmAt = 10;
        }
        if (!v.startCr3Title()) return 1;
        if (SDL_getenv("CR_SELFTEST_BAD_SAVE")) {
            const std::string path = v.savePath(v.cr3SaveFile().c_str());
            if (std::filesystem::exists(std::filesystem::u8path(path))) return 1;
            const std::string broken = "CR3-U save 1\narea missing_area_fixture\n";
            if (!crstorage::replaceFile(path, broken)) return 1;
            v.cr3Title.stage = 5, v.cr3Title.stageClock = 0.5f;
            v.updateCr3Title(0.1f);
            if (v.cr3SavePending && v.cr3Playing) v.writeCr3Save(); // any unintended new-game autosave
            const bool safe = v.readSaveText() == broken && !v.cr3Playing && !v.cr3SavePending &&
                              v.cr3Title.stage == 4 && !v.saveMenu.notice.empty();
            SDL_RemovePath(path.c_str()); // this test's own synthetic file only
            SDL_Log("bad save: original bytes preserved, returned to file menu %s", safe ? "yes" : "NO");
            return safe ? 0 : 1;
        }
        if (SDL_getenv("CR_SELFTEST_NEW_SESSION")) {
            v.playSeconds = 456;
            v.game.setFlag(1500, true);
            v.cr3Title.stage = 5, v.cr3Title.stageClock = 0.5f;
            v.updateCr3Title(0.1f); // actual native title -> selected empty file -> prologue
            const bool clean = v.playSeconds == 0 && !v.game.flag(1500) && v.cr3Playing && v.areaMode;
            SDL_Log("native new session: fresh progress, zero playtime, prologue entered %s", clean ? "yes" : "NO");
            if (!clean) return 1;
            if (SDL_getenv("CR_SELFTEST_STORAGE")) {
                // Only an empty, isolated test profile may supply this fixture.
                const std::filesystem::path path = std::filesystem::u8path(v.savePath(v.cr3SaveFile().c_str()));
                std::error_code ec;
                if (!std::filesystem::create_directory(path, ec)) return 1;
                const bool rejected = !v.writeCr3Save() && v.cr3SavePending && v.cr3SaveRetryAt > v.uiClock &&
                                      std::filesystem::is_directory(path);
                if (!std::filesystem::remove(path, ec)) return 1;
                const bool recovered = v.writeCr3Save() && !v.cr3SavePending && v.cr3SaveRetryAt == 0;
                cr3::GameState loaded;
                const bool valid = loaded.load(v.readSaveText());
                std::filesystem::remove(path, ec);
                SDL_Log("native save storage: rejected write preserved destination, retry recovered %s", rejected && recovered && valid ? "yes" : "NO");
                return rejected && recovered && valid ? 0 : 1;
            }
            return 0;
        }
        if (sessionPicker) {
            const int problems = checkSessionUiForTest(v,sessionPicker);
            SDL_Log("native CR3 title session UI: %d problems",problems);
            if (problems) return 1;
        }
        if (checkAudio) {
            const bool clean = !v.audio.voicePlaying() && !v.audio.playing(oldEffect) &&
                               v.voiceJobs.empty() && v.seJobs.empty() && v.pendingBgm == 0 && v.audio.bgmTrack() == 33;
            SDL_Log("native CR3 title audio: old voice/effect and delayed cues cleared, title music selected %s",clean?"yes":"NO");
            if (!clean) return 1;
        }
    } else if (titleScreen) {
        v.playMode = true;
        v.startTitle();
    } else if (!gameSavePath.empty()) {
        v.playMode = true;
        if (!v.loadGameSave(gameSavePath, binary, data)) return 1;
    } else if (continueSave) {
        v.playMode = true;
        if (!v.cr3SavedArea().empty()) {
            if (!v.loadCr3Save()) return 1;
        } else if (!v.continueSave(binary, data)) return 1;
    } else if (!fieldName.empty()) {
        if (!v.loadField(fieldName, at, binary, data, callEntry)) return 1;
        if (creditsArg >= 0) v.returnMap = fieldName, v.startCredits(creditsArg);
    } else if (lights != "none") {
        if (v.loadLights(lights)) SDL_Log("lights: %s", lights.c_str());
        else SDL_Log("lights: %s has none (the engine's defaults)", lights.c_str());
    }
    v.load(start);
    if (!areaName.empty() && !v.loadArea(areaName, areaPlayerArg)) return 1;
    // (--chapter-jump x_y: script\\chapter_jump.nut's chapter_jump_<x_y> - the debug jump: its
    // FlgSet_Chapter_x_y's flags, party and goods, then ppJump - run on the new game, that place
    // entered and CR3-U's save1.txt written there, the title's tap to take it up)
    if (!chapterJumpArg.empty()) {
        cr3::EventRuntime jump(v.archive, v.game);
        v.game.next = cr3::GameState::Next();
        if (!jump.start("chapter_jump", "chapter_jump_" + chapterJumpArg)) {
            SDL_Log("chapter jump %s: %s", chapterJumpArg.c_str(), jump.error().c_str());
            return 1;
        }
        for (int k = 0; k < 600 && jump.running(); ++k) jump.tick();
        for (int f : flagsArg) v.game.setFlag(f, true); // (the jump's own flag reset undone for the test's)
        const std::string pop = v.game.next.popPoint;
        v.game.next = cr3::GameState::Next();
        SDL_Log("chapter jump %s: to %s", chapterJumpArg.c_str(), pop.c_str());
        v.cr3Playing = v.playMode = true;
        if (pop.size() <= 5 || !v.enterPopPoint(pop) || !v.writeCr3Save()) return 1;
        return 0;
    }
    if (model == "c001" && leaderDefaulted) v.leaderFollows = true, v.refreshLeader();
    v.stateArg = stateArg;
    v.chargeArg = chargeArg, v.gaugeArg = gaugeArg;
    if (battleId > 0 && v.archive.contains("data\\battle_field.mbe")) { // (CR3's: battle/cr3battle.hpp)
        if (!v.startCr3Battle(battleId, battleFieldArg)) return 1;
    } else if (battleId > 0 && !v.startBattle(battleId, battleParty)) return 1;
    // the commands: chosen on the screen in a window (--fight manual, or --play), or given (--input)
    v.manualCommands = fightMode == 3 || !inputArg.empty() || (capture.empty() && !playFrom.empty());
    for (size_t p = 0; p < inputArg.size();) {
        const size_t e = inputArg.find_first_of(" ,", p);
        if (e != p) v.inputScript.push_back(inputArg.substr(p, e == std::string::npos ? std::string::npos : e - p));
        if (e == std::string::npos) break;
        p = e + 1;
    }
    if (battleId > 0 && fightMode) v.startFight(fightMode == 3 ? 2 : fightMode, fightSeed);
    if (!eventName.empty()) {
        if (!v.startEvent(eventName, vista, page)) return 1;
        for (float t = 0; t + cr3::EventRuntime::kTick * 0.5f < startTime; t += cr3::EventRuntime::kTick) {
            v.stepEvent();
            v.audio.update(cr3::EventRuntime::kTick);
        }
        startTime = 0;
    }
    if (!movieArg.empty()) v.game.movieRequest = movieArg, v.game.moviePlaying = true;
    if (!animName.empty()) {
        const std::string want = animName.size() > 5 && animName.compare(animName.size() - 5, 5, ".anim") == 0 ? animName : animName + ".anim";
        const auto it = std::find(v.fig.anims.begin(), v.fig.anims.end(), want);
        if (it == v.fig.anims.end()) return std::fprintf(stderr, "%s: not one of the figure's animations\n", want.c_str()), 1;
        v.loadAnim(v.fig, size_t(it - v.fig.anims.begin()));
    }
    if (v.fieldMode && animName.empty()) v.playMotion(v.fig, "fn01");
    // the title's choice: a new game, the save, the continue data (false: it failed)
    auto takeTitle = [&]() {
        if (v.titleMenu.stage != 0 || !v.titleMenu.action) return true;
        const int act = v.titleMenu.action;
        v.titleMenu.action = 0;
        v.newGame();
        if (act == 4) v.carryOver();
        const bool ok = act == 1 || act == 4 ? v.loadField(fieldName, at, binary, data, callEntry)
                                             : v.continueSave(binary, data, act == 2 ? "save1.txt" : "cont.txt");
        SDL_Log("title: %s", act == 1 ? "a new game" : act == 4 ? "a new game with the clear data" : act == 2 ? "the save loaded" : "the continue data");
        return ok;
    };
    auto frameSize = [&](int& w, int& h) {
        SDL_GetWindowSizeInPixels(win, &w, &h);
        if (dev->offscreen() && v.resW > 0 && v.resH > 0) w = v.resW, h = v.resH;
    };
    auto catchGlass = [&]() { // (frames not drawn: drawn as the screen breaks, to catch it)
        if (!v.glassPending && !v.cr3BattleRequest.active) return;
        int w = 0, h = 0;
        frameSize(w, h);
        v.draw(w, h);
        dev->endFrame(false);
    };
    if (v.playMode) { // --time: the game played that long first (60 frames a second), then --walk
        for (float t = 0; t + 0.5f / 60.0f < startTime; t += 1.0f / 60.0f) {
            v.advance(1.0f / 60.0f, 0, 0, false);
            catchGlass();
            if (!takeTitle()) return 1;
        }
        for (float t = 0; t < walkSeconds; t += 1.0f / 60.0f) v.advance(1.0f / 60.0f, walkX, walkY, true);
        if (tapAfter) v.tap(); // (--tap: then a tap, before --run)
        if (const char* row = SDL_getenv("CR3_CAPTURE_SETTINGS")) { // (tests: as below, before --run)
            if (v.areaMode) { // (CR3's camp: Options, its Other tab; -1 that alone)
                v.openCr3Camp();
                v.openCampOptions();
                v.campOptions.tab = 2;
            } else {
                v.optionsOpen = true, v.campOpen = !v.battleMode;
            }
            if (std::atoi(row) >= 0) v.openSettings(), v.settingsCursor = std::atoi(row);
        }
        for (float t = 0; t < runSeconds; t += 1.0f / 60.0f) v.advance(1.0f / 60.0f, 0, 0, false), catchGlass(); // (--run: on after the walk)
        runSeconds = 0;
        SDL_Log("play: after %.2f s (+ %.2f s walking) %s, %d battles skipped", startTime, walkSeconds,
                v.eventMode ? ("in the vista " + v.status).c_str() : ("on " + v.field.info().name).c_str(), v.battlesSkipped);
        startTime = 0;
    } else {
        for (float t = 0; v.fieldMode && t < walkSeconds; t += 1.0f / 60.0f) v.walk(1.0f / 60.0f, walkX, walkY, true);
    }
    if (v.fieldMode && walkSeconds > 0) SDL_Log("after --walk: %s at (%.3f %.3f %.3f)", v.field.info().name.c_str(), v.player[0], v.player[1], v.player[2]);
    v.time = startTime;
    v.pose();
    SDL_Log("%s%s", v.status.c_str(), v.animStatus().c_str());

    // a frame's size: the resolution where the device draws offscreen, else the window's
    auto describe = [&]() {
        int w = 0, h = 0;
        frameSize(w, h);
        // (the device's sceneSize follows the mode it last ran: this is the one set)
        const float r = v.upscale.mode == cr3::Upscaler::Off ? 1.0f : std::clamp(v.upscale.ratio, 1.0f, 3.0f);
        const int rw = std::max(int(float(w) / r), 1), rh = std::max(int(float(h) / r), 1);
        char buf[160];
        std::snprintf(buf, sizeof buf, "%dx%d%s, upscaler %s (scene %dx%d)", w, h, v.resW ? "" : " (the window's)", upscaleName(v.upscale).c_str(), rw, rh);
        return std::string(buf);
    };
    SDL_Log("output: %s", describe().c_str());

    int code = 0;
    if (!capture.empty()) { // one frame to a file
        int w = 0, h = 0;
        frameSize(w, h);
        // (a temporal upscaler first gathers a still frame's history: its jitter's phases)
        const int warmup = v.upscale.mode != cr3::Upscaler::Off ? 32 : 0;
        for (int k = 0; k < warmup; ++k) {
            v.draw(w, h);
            dev->endFrame(false);
        }
        // --run: frames as the window would show them, the event and animations going on
        if (v.battleMode && !v.playMode) { // (--time: that far into the battle, played at 60 a second)
            for (float t = 0; t + 0.5f / 60.0f < startTime; t += 1.0f / 60.0f) {
                v.advance(1.0f / 60.0f, 0, 0, false), v.pose();
                catchGlass(); // (the screen caught as it breaks)
                if (!takeTitle()) return 1; // (a game over's "try again": its save loaded)
            }
        }
        if (v.cr3Title.stage && !v.playMode) { // (CR3's title: --time that long on it, --tap then a tap, then --run)
            for (float t = 0; t + 0.5f / 60.0f < startTime; t += 1.0f / 60.0f) v.advance(1.0f / 60.0f, 0, 0, false), v.pose();
            if (tapAfter) v.tap();
        }
        if (const char* row = SDL_getenv("CR3_CAPTURE_SETTINGS"); row && !v.settingsOpen) { // (tests: the settings, that row in focus)
            if (v.areaMode) { // (CR3's camp: Options, its Graphics tab; -1 the Other tab)
                v.openCr3Camp();
                v.openCampOptions();
                v.campOptions.tab = std::atoi(row) >= 0 ? 3 : 2;
                v.settingsCursor = std::max(std::atoi(row), 0);
            } else {
                v.optionsOpen = true, v.campOpen = !v.battleMode;
                if (std::atoi(row) >= 0) v.openSettings(), v.settingsCursor = std::atoi(row);
            }
        }
        for (int k = 0; k < int(runSeconds * 60.0f + 0.5f); ++k) {
            v.time += 1.0f / 60.0f;
            // (the field on in --walk's direction; a CR3 area's only for --walk's seconds)
            const bool walking = areaName.empty() || k < int(walkSeconds * 60.0f);
            float sx = walking ? walkX : 0.0f, sy = walking ? walkY : 0.0f;
            bool run = true;
            if (v.areaDrag.on) v.areaDrag.held += 1.0f / 60.0f;
            if (v.areaCam.on) v.areaCam.held += 1.0f / 60.0f;
            v.areaDragStick(sx, sy, run); // (--input drag:..: a finger's drag)
            v.advance(1.0f / 60.0f, sx, sy, run);
            v.pose();
            v.draw(w, h);
            dev->endFrame(false);
        }
        if (v.areaMode && v.areaWalker && walkSeconds > 0) {
            const cr3::Vec3 p = v.areaWalker->position();
            SDL_Log("after --walk: %s at (%.3f %.3f %.3f)", v.area.name.c_str(), p[0], p[1], p[2]);
        }
        if ((v.areaMode || v.cr3BattleMode) && tapAfter && !v.cr3Title.stage) { // (CR3's field or battle: --tap after --run, then 4 s on)
            v.tap();
            for (int k = 0; k < 240; ++k) v.time += 1.0f / 60.0f, v.advance(1.0f / 60.0f, 0, 0, false), v.pose();
        }
        v.draw(w, h);
        dev->endFrame(false);
        std::vector<uint8_t> px;
        bool recoveryOk = true;
        if (SDL_getenv("CR_SELFTEST_GRAPHICS")) {
            dev->readPixels(px, w, h);
            const auto before = px;
            size_t visible = 0;
            for (size_t i = 0; i + 3 < before.size(); i += 4)
                visible += int(before[i]) + before[i + 1] + before[i + 2] > 32;
            recoveryOk = visible >= before.size() / 4 / 10;
            SDL_Log("graphics recovery: visible scene pixels %zu (%s)", visible, recoveryOk ? "enough" : "FAILED");
            for (int cycle = 0; cycle < 3; ++cycle) {
                SDL_GL_DestroyContext(SDL_GL_GetCurrentContext());
                if (!SDL_GL_CreateContext(win) || !dev->restoreAfterContextLoss()) {
                    recoveryOk = false;
                    break;
                }
                v.draw(w, h);
                dev->endFrame(false);
                dev->readPixels(px, w, h);
                const bool sameFrame = !before.empty() && before == px && dev->takeErrors() == 0;
                recoveryOk &= sameFrame;
                SDL_Log("graphics recovery: cycle %d, identical scene frame %s", cycle + 1, sameFrame ? "yes" : "NO");
            }
        }
        const bool ok = recoveryOk && dev->readPixels(px, w, h) && saveBmp(capture.c_str(), px, w, h);
        SDL_Log("capture %s: %s", capture.c_str(), ok ? "written" : SDL_GetError());
        if (v.hdTextures) SDL_Log("hd textures: %d replaced, %d without one (%s)", v.hdLoaded, v.hdMissing, v.hdDir.c_str());
        code = ok ? 0 : 1;
    } else {
        bool running = true, dragging = false;
        // A controller, as the Vita's buttons (its own code - docs/CONTROLS.md): by position, ○ the right
        // button and × the bottom one (the settings' Swap ○ and × the other way); its presses as the
        // keys the menus take - ○ Return, × Backspace, the d-pad (or the left stick pushed all the way)
        // the arrows, repeating as InputEquipment's do (after 0.6 s, then every 0.08 s), L / R a list's
        // page, START the title's "Please Touch", □ held the fast-forward; △ (the port's own: the Vita's is
        // a touch) the field's tab. On the field the left stick walks, a long push running
        SDL_Gamepad* pad = nullptr;
        SDL_Keycode padDir = 0;
        Uint64 padDirNext = 0;
        crinput::CancelWatch inputCancel;
        inputCancel.start(SDL_GetWindowID(win));
        crtime::LifecycleClock frameClock;
        if (!frameClock.start()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "lifecycle clock: %s", SDL_GetError());
            return 1;
        }
        auto consumeInputCancel = [&] {
            if (v.consumeInputCancel(inputCancel)) dragging = false, padDir = 0, padDirNext = 0;
        };
        auto pushKey = [](SDL_Keycode key) {
            SDL_Event k{};
            k.type = SDL_EVENT_KEY_DOWN;
            k.key.key = key;
            k.key.down = true;
            SDL_PushEvent(&k);
        };
        // CR3_PAD_SCRIPT (tests): a virtual controller, its buttons and stick set at their times - "1.5:east
        // 2:down 3:lx=1 4:lx=0" (a button is held 0.1 s; east / south / north / west, up / down / left /
        // right, l / r, start, back; lx / ly the left stick, rx / ry the right, -1 .. 1, y up positive; blur the window's focus
        // lost; shot=<file.bmp> the frame then; until=<text> the later steps held till a log line has it -
        // '_' a space -, their times from then; quit the end). Each press of a held direction is logged
        struct PadStep {
            float at;
            std::string what;
        };
        std::vector<PadStep> padScript;
        std::string padShot;
        // (until=<text>: the steps after it wait for a log line with that text - '_' a space - and
        // their times count from then)
        struct PadWait {
            std::string text;
            bool seen = false;
            SDL_LogOutputFunction next = nullptr;
            void* nextData = nullptr;
        };
        static PadWait padWait;
        float padShift = 0;
        SDL_Joystick* testPad = nullptr;
        if (const char* ps = SDL_getenv("CR3_PAD_SCRIPT")) {
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.name = "CR3-U test controller";
            if (const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc)) testPad = SDL_OpenJoystick(id);
            SDL_GetLogOutputFunction(&padWait.next, &padWait.nextData);
            SDL_SetLogOutputFunction(
                [](void*, int category, SDL_LogPriority priority, const char* message) {
                    if (!padWait.text.empty() && message && std::strstr(message, padWait.text.c_str())) padWait.seen = true;
                    if (padWait.next) padWait.next(padWait.nextData, category, priority, message);
                },
                nullptr);
            std::string all = ps;
            for (size_t at = 0; at < all.size();) {
                const size_t sp = std::min(all.find(' ', at), all.size());
                const std::string t = all.substr(at, sp - at);
                if (const size_t c = t.find(':'); c != std::string::npos) padScript.push_back({float(std::atof(t.c_str())), t.substr(c + 1)});
                at = sp + 1;
            }
        }
        std::vector<std::pair<Uint64, SDL_GamepadButton>> testReleases;
        auto padStick = [&](float& x, float& y) { // (the left stick, up positive; the Vita's 0.25 dead zone)
            x = pad ? float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX)) / 32767.0f : 0.0f;
            y = pad ? -float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY)) / 32767.0f : 0.0f;
            if (std::hypot(x, y) < 0.25f) x = y = 0;
        };
        // (the window's point on the game's screen, letterboxed in the window at its own shape: its fractions)
        auto screenFraction = [&](float wx, float wy, float& fx, float& fy) {
            int ww = 1, wh = 1, pw = 1, ph = 1;
            SDL_GetWindowSize(win, &ww, &wh);
            SDL_GetWindowSizeInPixels(win, &pw, &ph);
            const float px = wx * float(pw) / float(std::max(ww, 1)), py = wy * float(ph) / float(std::max(wh, 1));
            const float sw = float(v.screenW), sh = float(v.screenH);
            // (the UI's screen: inside the frame - an offscreen device's scaled into the window to fit,
            // centred; GL's in the window's own pixels -, centred in it too)
            const float k = v.dev->offscreen() ? std::min(float(pw) / float(v.frameW), float(ph) / float(v.frameH)) : 1.0f;
            const float ox = (float(pw) - sw * k) * 0.5f, oy = (float(ph) - sh * k) * 0.5f;
            fx = (px - ox) / (sw * k), fy = (py - oy) / (sh * k);
        };
        const Uint64 firstFrame = SDL_GetTicksNS();
        // --fps (the settings' Frame Rate): each frame starts no sooner than its slot (the display's
        // vsync may hold it longer)
        Uint64 nextFrame = firstFrame, fpsSince = firstFrame;
        int fpsFrames = 0, totalFrames = 0;
        float measured = 0;
        const Uint64 started = firstFrame;
        while (running) {
            SDL_Event e;
            if (v.window && v.inputDialogShown() != SDL_TextInputActive(v.window)) { // (the text entry's typing: a phone's keyboard up with it)
                if (v.inputDialogShown()) SDL_StartTextInput(v.window);
                else SDL_StopTextInput(v.window);
            }
            while (SDL_PollEvent(&e)) {
                const bool cancelled = inputCancel.note(e);
                consumeInputCancel();
                if (cancelled) continue;
                if (e.type == SDL_EVENT_RENDER_DEVICE_RESET) {
                    if (e.render.windowID == SDL_GetWindowID(win) && !dev->restoreAfterContextLoss()) {
                        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unable to restore the OpenGL context");
                        code = 1;
                        running = false;
                        break;
                    }
                    continue;
                }
                if (e.type == SDL_EVENT_QUIT) running = false;
                else if (e.type == SDL_EVENT_TEXT_INPUT) v.typeInputDialog(e.text.text);
                else if (e.type == SDL_EVENT_KEY_DOWN) {
                    // (Android's Back: the menus' back, as Backspace)
                    const SDL_Keycode k = e.key.key == SDLK_AC_BACK ? SDLK_BACKSPACE : e.key.key;
                    if (v.eventMode && v.event && v.event->vistaPaused) continue;
                    if (v.movie.mv && k != SDLK_RETURN && k != SDLK_KP_ENTER && k != SDLK_ESCAPE) continue;
                    if (v.lunchKey(k)) continue;
                    if (v.cr3OptionsOver()) { // (CR3-U's Options over the title or a battle: all its keys)
                        v.cr3CampKey(k);
                        continue;
                    }
                    if (v.saveMenuKey(k)) continue; // (CR3's save files)
                    if (v.inputDialogKey(k)) continue; // (CR3's text entry)
                    if (v.cr3Title.stage == 2 && v.cr3Camp.stage == 0 && (k == SDLK_O || k == SDLK_G)) { // (the title's Options)
                        v.openCr3OptionsFrom(1);
                        continue;
                    }
                    if ((k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_ESCAPE || k == SDLK_BACKSPACE) && v.geneGetTap()) continue; // (the gene obtained window's)
                    if (v.scriptGeneKey(k)) continue; // (an event's gene status / disposal)
                    if (v.geneSelectorKey(k)) continue; // (the VS tutorial's gene cards)
                    if ((k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_ESCAPE || k == SDLK_BACKSPACE) && v.collectList.open && v.collectList.scripted) {
                        v.chooseCollectList(-1); // (a script's collection: closed)
                        continue;
                    }
                    if (v.geneLabKey(k)) continue; // (the gene lab's keys: its back is Escape too)
                    if (v.spacePortKey(k)) continue; // (the Space Port's)
                    if (v.cr3ShopKey(k)) continue;   // (a CR3 shop's)
                    if (v.worldSelectKey(k)) continue; // (the world select's)
                    if (v.townMoveKey(k)) continue;    // (the town map's)
                    if (v.vsKey(k)) continue;       // (the VS mode's)
                    if (v.cr3CampKey(k)) continue;  // (CR3's camp: C opens it)
                    if (k == SDLK_ESCAPE) running = false;
                    if (k == SDLK_SPACE) v.paused = !v.paused;
                    // F1: the next resolution; F2: the upscaler's next mode (off, FSR 3.1 native
                    // AA .. ultra performance)
                    if (k == SDLK_F1) {
                        size_t i = 0;
                        while (i < std::size(kResolutions) && (kResolutions[i][0] != v.resW || kResolutions[i][1] != v.resH)) ++i;
                        i = (i + 1) % std::size(kResolutions);
                        v.resW = kResolutions[i][0], v.resH = kResolutions[i][1];
                        v.settings.resolution = int(i);
                        v.saveSettings();
                        dev->resetHistory();
                        SDL_Log("output: %s", describe().c_str());
                        continue;
                    }
                    if (k == SDLK_F2) {
                        cr3::UpscaleSettings u = v.upscale;
                        size_t q = 0;
                        while (q < std::size(kQualities) && kQualities[q].ratio != u.ratio) ++q;
                        if (u.mode == cr3::Upscaler::Off) u.mode = cr3::Upscaler::Fsr3, u.ratio = kQualities[0].ratio;
                        else if (q + 1 < std::size(kQualities)) u.ratio = kQualities[q + 1].ratio;
                        else u.mode = cr3::Upscaler::Off;
                        const std::string why = dev->upscalerProblem(u.mode);
                        if (!why.empty()) SDL_Log("upscaler: %s", why.c_str()), u.mode = cr3::Upscaler::Off;
                        v.settings.upscaler = int(u.mode);
                        for (size_t k = 0; k < std::size(kQualities); ++k)
                            if (kQualities[k].ratio == u.ratio) v.settings.quality = int(k);
                        v.applySettings(); // (the upscaler, MSAA and the textures' LOD bias with it)
                        v.saveSettings();
                        SDL_Log("output: %s", describe().c_str());
                        continue;
                    }
                    if (v.cr3BattleMode && v.cr3BattleKey(k)) continue; // (CR3's battle menus)
                    if (v.keyBuy.stage) { // (the gold key shop: Return yes, Backspace no, up / down one more or less)
                        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) v.chooseKeyBuy(v.kTapKeyYes);
                        else if (k == SDLK_BACKSPACE || k == SDLK_ESCAPE || k == SDLK_N) v.chooseKeyBuy(v.kTapKeyNo);
                        else if (k == SDLK_UP) v.chooseKeyBuy(v.kTapKeyReel + 2);
                        else if (k == SDLK_DOWN) v.chooseKeyBuy(v.kTapKeyReel + 3);
                        continue;
                    }
                    if (v.clearQuestShown()) { // (the cleared quest's detail: Return its OK)
                        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) v.answerClearQuest();
                        continue;
                    }
                    if (v.choiceShown()) { // (an event's choice: 1, 2 or 3; the Vita's cursor)
                        if (v.choiceKey(k)) {
                        } else if (k >= SDLK_1 && k <= SDLK_3) v.answerChoice(int(k - SDLK_1));
                        else if (k >= SDLK_KP_1 && k <= SDLK_KP_3) v.answerChoice(int(k - SDLK_KP_1));
                        continue;
                    }
                    if (v.yesNoKey(k)) continue;
                    if ((k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) && v.listShown()) { // (a list's page: four rows on)
                        for (int r = 0; r < 4; ++r) pushKey(k == SDLK_PAGEUP ? SDLK_UP : SDLK_DOWN);
                        continue;
                    }
                    if (v.focusKey(k)) continue; // (the title's, the shop's and the party's buttons; the field's tab)
                    if (v.battleMode && v.choosing) {
                        v.battleKey(k);
                        continue;
                    }
                    if (v.campOpen) { // (the camp: Up / Down and Return, Backspace back)
                        v.campKey(k);
                        continue;
                    }
                    if (v.party.stage) { // (the party: 1 / 2, Left / Right, Return, Backspace)
                        v.partyKey(k);
                        continue;
                    }
                    if (v.shop.stage) { // (the shop: 1 .. 4 its buttons, Up / Down, Left / Right, Return, Backspace; Y / N)
                        v.shopKey(k);
                        continue;
                    }
                    if (v.world.stage) { // (the world select: Up / Down, Return, Backspace; Y / N its question)
                        v.worldKey(k);
                        continue;
                    }
                    if (v.pub.stage) { // (the pub: Left / Right, Return, Backspace; Y / N)
                        v.pubKey(k);
                        continue;
                    }
                    if (v.library.stage == 1) { // (the library: Up / Down, Return, Backspace)
                        v.libraryKey(k);
                        continue;
                    }
                    if (v.mapOpen) { // (the map: any key closes it)
                        v.audio.playSe(0xb5, false), v.mapOpen = false;
                        continue;
                    }
                    if (v.saveStage) { // (saving: Return yes, Backspace no)
                        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) v.saveAnswer(true);
                        else if (k == SDLK_BACKSPACE || k == SDLK_N) v.saveAnswer(false);
                        continue;
                    }
                    if (k == SDLK_TAB && v.fieldTabShown()) { // (the field's tab: open or closed; C the camp)
                        v.fieldTabTap(1);
                        continue;
                    }
                    if (k == SDLK_C && v.fieldTabShown()) {
                        v.fieldToken("camp");
                        continue;
                    }
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) v.tap();
                    if (v.eventMode) continue;
                    ViewFigure& f = v.fig;
                    if (v.fieldMode) {
                        if ((k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) && !f.anims.empty())
                            v.loadAnim(f, (f.animIndex + (k == SDLK_PAGEDOWN ? 1 : f.anims.size() - 1)) % f.anims.size());
                        continue;
                    }
                    size_t to = v.current;
                    if (k == SDLK_RIGHT || k == SDLK_PAGEDOWN) to = (v.current + (k == SDLK_PAGEDOWN ? 10 : 1)) % v.models.size();
                    else if (k == SDLK_LEFT || k == SDLK_PAGEUP)
                        to = (v.current + v.models.size() * 10 - (k == SDLK_PAGEUP ? 10 : 1)) % v.models.size();
                    if (to != v.current) v.load(to);
                    if ((k == SDLK_UP || k == SDLK_DOWN) && !f.anims.empty())
                        v.loadAnim(f, (f.animIndex + (k == SDLK_DOWN ? 1 : f.anims.size() - 1)) % f.anims.size());
                } else if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
                    if (!pad && (pad = SDL_OpenGamepad(e.gdevice.which))) SDL_Log("controller: %s", SDL_GetGamepadName(pad));
                } else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
                    if (pad && SDL_GetGamepadID(pad) == e.gdevice.which) SDL_CloseGamepad(pad), pad = nullptr, padDir = 0;
                } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && pad) {
                    const SDL_GamepadButton b = SDL_GamepadButton(e.gbutton.button);
                    const SDL_GamepadButton circle = v.settings.padSwap ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;
                    const SDL_GamepadButton cross = v.settings.padSwap ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH;
                    if (v.moviePad(b) || v.vistaPad(b)) continue;
                    if (b == SDL_GAMEPAD_BUTTON_WEST && v.goodsPad(false)) continue;
                    const bool shopList = v.shop.stage >= 3 && v.shop.kind > 0; // (ShopCutInPiupiuMenu: L / R the character)
                    using PP = Viewer::PadPress;
                    const PP press = b == circle ? PP::Confirm : b == cross ? PP::Cancel : b == SDL_GAMEPAD_BUTTON_NORTH ? PP::Triangle
                                   : b == SDL_GAMEPAD_BUTTON_DPAD_UP ? PP::Up : b == SDL_GAMEPAD_BUTTON_DPAD_DOWN ? PP::Down
                                   : b == SDL_GAMEPAD_BUTTON_DPAD_LEFT ? PP::Left : b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? PP::Right : PP::None;
                    if (press != PP::None && v.cr3BarPad(press)) continue; // (the Vita's field buttons under its own)
                    if ((b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER || b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) && v.cr3Shoulder(b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
                        continue; // (the Vita's L / R over CR3's menus)
                    SDL_Keycode key = 0;
                    if (b == circle) key = SDLK_RETURN;
                    else if (b == cross) key = SDLK_BACKSPACE;
                    else if (b == SDL_GAMEPAD_BUTTON_NORTH) key = SDLK_TAB;
                    else if (b == SDL_GAMEPAD_BUTTON_WEST && v.vitaButtons() &&
                             (v.inMenu() || v.cr3Camp.stage || v.vs.screen || (v.activeEvents() && v.activeEvents()->geneLab().open))) key = SDLK_X;
                    else if (b == SDL_GAMEPAD_BUTTON_START && v.vitaButtons() && v.activeEvents() && v.activeEvents()->geneLab().open) key = SDLK_O;
                    else if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && (v.inMenu() || v.cr3Title.stage == 4)) key = shopList ? SDLK_LEFT : SDLK_PAGEUP;
                    else if (b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER && (v.inMenu() || v.cr3Title.stage == 4)) key = shopList ? SDLK_RIGHT : SDLK_PAGEDOWN;
                    else if (b == SDL_GAMEPAD_BUTTON_START && v.titleMenu.stage >= 2 && v.titleMenu.stage <= 3) key = SDLK_RETURN;
                    else if (b == SDL_GAMEPAD_BUTTON_START && v.vitaButtons() && v.cr3Title.stage == 2) // (CR3's TitleMenu, 0x81256cde: ○ or START its tap)
                        key = SDLK_RETURN, SDL_Log("title: START");
                    if (key) pushKey(key);
                } else if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION || e.type == SDL_EVENT_FINGER_UP ||
                           e.type == SDL_EVENT_FINGER_CANCELED) { // (the fingers SDL makes no mouse of: the field's second touch)
                    int ww = 1, wh = 1;
                    SDL_GetWindowSize(win, &ww, &wh);
                    float fx, fy;
                    screenFraction(e.tfinger.x * float(ww), e.tfinger.y * float(wh), fx, fy);
                    v.fingerEvent(e.type, e.tfinger.fingerID, fx, fy);
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT && e.button.which != SDL_TOUCH_MOUSEID &&
                           v.areaCamFree()) { // (CR3-U's: the right button's drag turns the field's camera)
                    float fx, fy;
                    screenFraction(e.button.x, e.button.y, fx, fy);
                    if (!v.areaCam.on) v.areaCamStart(fx, fy, true);
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT && v.areaCam.on && v.areaCam.mouse) {
                    v.areaCamEnd();
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && v.areaCam.on && !v.areaCam.own && (v.areaCam.mouse || e.motion.which == SDL_TOUCH_MOUSEID)) {
                    float fx, fy;
                    screenFraction(e.motion.x, e.motion.y, fx, fy);
                    v.areaCamMove(fx, fy);
                    if (dragging && v.areaDrag.on && !v.areaDrag.own && !v.areaCam.mouse) v.areaDragMove(fx, fy);
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    v.pointerCancelled = false;
                    dragging = true;
                    float fx, fy;
                    screenFraction(e.button.x, e.button.y, fx, fy);
                    v.pointerTouch = e.button.which == SDL_TOUCH_MOUSEID;
                    v.pointerDown(fx, fy);
                    v.pointerTouch = false;
                }
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    if (e.button.which == SDL_TOUCH_MOUSEID && v.areaCam.on && !v.areaCam.own && !v.areaCam.mouse) {
                        v.areaCamEnd(); // (the camera's half let go)
                        dragging = false;
                        continue;
                    }
                    if (v.pointerCancelled) continue; // an interrupted gesture cannot activate or finish anything
                    float fx, fy;
                    screenFraction(e.button.x, e.button.y, fx, fy);
                    if (v.menuPointerUp(fx, fy)) { dragging = false; continue; }
                    if (!v.areaDrag.own) v.areaDragEnd();
                    v.worldSel.dragging = false; // (the planet let go: spun on)
                    v.town.dragging = false;    // (the town map let go)
                    dragging = false;
                    v.fastHeld = false;
                    if (v.fieldMode && v.keyLock.stage == 1) v.unlockPointer(0, 0, 2);
                    if (v.vslash.stage == 1) {
                        float fx, fy;
                        screenFraction(e.button.x, e.button.y, fx, fy);
                        v.vslashPointer(fx, fy, 2);
                    }
                    if (v.fieldMode && v.aim.stage == 2) {
                        float fx, fy;
                        screenFraction(e.button.x, e.button.y, fx, fy);
                        v.aimPointer(fx, fy, 2);
                    }
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && v.menuSwipe.down) {
                    float fx, fy;
                    screenFraction(e.motion.x, e.motion.y, fx, fy);
                    v.menuPointerMove(fx, fy);
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL && v.menuScroll(e.wheel.y > 0 ? -1 : e.wheel.y < 0 ? 1 : 0)) {
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && v.areaDrag.on && !v.areaDrag.own) {
                    float fx, fy;
                    screenFraction(e.motion.x, e.motion.y, fx, fy);
                    v.areaDragMove(fx, fy);
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && v.fieldMode && (v.keyLock.stage == 1 || v.iai.stage == 2 || v.aim.stage == 2)) {
                    float fx, fy;
                    screenFraction(e.motion.x, e.motion.y, fx, fy);
                    if (v.iai.stage == 2) v.iaiPointer(fx, fy, 1);
                    else if (v.aim.stage == 2) v.aimPointer(fx, fy, 1);
                    else v.unlockPointer(fx, fy, 1);
                }
                else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && [&] { // (the world select's planet turned: in the screen's 960 x 640)
                             int ww = 1, wh = 1;
                             SDL_GetWindowSize(win, &ww, &wh);
                             return v.worldSelectDrag(e.motion.xrel * 960.0f / float(std::max(ww, 1)), e.motion.yrel * 640.0f / float(std::max(wh, 1)));
                         }()) {
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && [&] { // (the town map dragged: in the screen's 960 x 640)
                             int ww = 1, wh = 1;
                             SDL_GetWindowSize(win, &ww, &wh);
                             return v.townMoveDrag(e.motion.xrel * 960.0f / float(std::max(ww, 1)), e.motion.yrel * 640.0f / float(std::max(wh, 1)));
                         }()) {
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && [&] { // (the camp's model turned and sized: in the screen's 960 x 640)
                             int ww = 1, wh = 1;
                             SDL_GetWindowSize(win, &ww, &wh);
                             return v.cr3CampDrag(e.motion.xrel * 960.0f / float(std::max(ww, 1)), e.motion.yrel * 640.0f / float(std::max(wh, 1)));
                         }()) {
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && v.campOpen && v.campLink == 201) { // (the scenarios dragged up and down)
                    int ww = 1, wh = 1;
                    SDL_GetWindowSize(win, &ww, &wh);
                    v.scrollScenarios(-e.motion.yrel / float(std::max(wh, 1)) * 0.64f);
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL && v.settingsOpen) { // (the settings' list: a row up / down)
                    if (e.wheel.y != 0) v.settingsKey(e.wheel.y > 0 ? SDLK_UP : SDLK_DOWN);
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL && v.cr3Camp.stage == 9 && v.campOptions.tab == 3) { // (CR3's Options' Graphics: a row up / down)
                    if (e.wheel.y != 0) v.campOptionsKey(e.wheel.y > 0 ? SDLK_UP : SDLK_DOWN);
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL && v.campOpen && v.campLink == 201) {
                    v.scrollScenarios(e.wheel.y > 0 ? -0.1f : 0.1f);
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && !v.fieldMode) {
                    v.yaw -= e.motion.xrel * 0.4f;
                    v.pitch = std::clamp(v.pitch + e.motion.yrel * 0.4f, -89.0f, 89.0f);
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL && !v.fieldMode) {
                    v.zoom = std::clamp(v.zoom * (e.wheel.y > 0 ? 0.9f : 1.1f), 0.05f, 20.0f);
                }
            }
            consumeInputCancel();
            if (!running) break;
            if (!takeTitle()) running = false;
            if (v.toTitle) { // (the options' Title Screen, yes: the title screen; --play without it from a new game again; else the end)
                v.toTitle = false;
                if (titleScreen) {
                    v.leaveBattle();
                    v.startTitle();
                } else if (v.playMode) {
                    v.leaveBattle();
                    v.newGame();
                    if (!v.loadField(fieldName, at, binary, data, callEntry)) running = false;
                } else running = false;
            }
            if (const Uint64 period = v.settings.fps > 0 ? Uint64(1000000000) / Uint64(v.settings.fps) : 0) {
                const Uint64 t = SDL_GetTicksNS();
                if (t < nextFrame) SDL_DelayPrecise(nextFrame - t);
                nextFrame = std::max(nextFrame + period, SDL_GetTicksNS() - period); // (no catching up after a stall)
            }
            const Uint64 now = SDL_GetTicksNS();
            const float dt = frameClock.step();
            if (testPad) { // (CR3_PAD_SCRIPT's steps, by the seconds since the window opened)
                static const std::map<std::string, SDL_GamepadButton> kButtons = {
                    {"east", SDL_GAMEPAD_BUTTON_EAST}, {"south", SDL_GAMEPAD_BUTTON_SOUTH}, {"north", SDL_GAMEPAD_BUTTON_NORTH},
                    {"west", SDL_GAMEPAD_BUTTON_WEST}, {"up", SDL_GAMEPAD_BUTTON_DPAD_UP}, {"down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
                    {"left", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT}, {"l", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
                    {"r", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}, {"start", SDL_GAMEPAD_BUTTON_START}, {"back", SDL_GAMEPAD_BUTTON_BACK}};
                const float since = float(double(now - started) / 1e9);
                if (!padWait.text.empty() && padWait.seen) padWait.text.clear(), padShift = since; // (the awaited line: on from here)
                while (padWait.text.empty() && !padScript.empty() && padScript.front().at + padShift <= since) {
                    const std::string w = padScript.front().what;
                    padScript.erase(padScript.begin());
                    if (w.rfind("until=", 0) == 0) { // (logged before it's awaited: the line itself no match)
                        std::string text = w.substr(6);
                        std::replace(text.begin(), text.end(), '_', ' ');
                        SDL_Log("test controller: until \"%s\"", text.c_str());
                        padWait.text = text, padWait.seen = false;
                        continue;
                    }
                    if (w.rfind("hold=", 0) == 0 || w.rfind("release=", 0) == 0) {
                        const bool hold = w.rfind("hold=", 0) == 0;
                        if (auto b = kButtons.find(w.substr(hold ? 5 : 8)); b != kButtons.end()) {
                            testReleases.erase(std::remove_if(testReleases.begin(), testReleases.end(), [&](const auto& release) { return release.second == b->second; }), testReleases.end());
                            SDL_SetJoystickVirtualButton(testPad, b->second, hold);
                        }
                    }
                    else if (w.rfind("lx=", 0) == 0) SDL_SetJoystickVirtualAxis(testPad, SDL_GAMEPAD_AXIS_LEFTX, Sint16(std::clamp(std::atof(w.c_str() + 3), -1.0, 1.0) * 32767));
                    else if (w.rfind("ly=", 0) == 0) SDL_SetJoystickVirtualAxis(testPad, SDL_GAMEPAD_AXIS_LEFTY, Sint16(-std::clamp(std::atof(w.c_str() + 3), -1.0, 1.0) * 32767));
                    else if (w.rfind("rx=", 0) == 0) SDL_SetJoystickVirtualAxis(testPad, SDL_GAMEPAD_AXIS_RIGHTX, Sint16(std::clamp(std::atof(w.c_str() + 3), -1.0, 1.0) * 32767));
                    else if (w.rfind("ry=", 0) == 0) SDL_SetJoystickVirtualAxis(testPad, SDL_GAMEPAD_AXIS_RIGHTY, Sint16(-std::clamp(std::atof(w.c_str() + 3), -1.0, 1.0) * 32767));
                    else if (w == "blur") {
                        SDL_Event focus{};
                        focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
                        focus.window.windowID = SDL_GetWindowID(win);
                        SDL_PushEvent(&focus);
                    } else if (w.rfind("shot=", 0) == 0) {
                        padShot = w.substr(5);
                    } else if (w == "quit") {
                        SDL_Event quit{};
                        quit.type = SDL_EVENT_QUIT;
                        SDL_PushEvent(&quit);
                    } else if (auto b = kButtons.find(w); b != kButtons.end()) {
                        SDL_SetJoystickVirtualButton(testPad, b->second, true);
                        testReleases.push_back({now + 100000000, b->second});
                    }
                    SDL_Log("test controller: %s", w.c_str());
                }
                for (size_t r = 0; r < testReleases.size();)
                    if (testReleases[r].first <= now) SDL_SetJoystickVirtualButton(testPad, testReleases[r].second, false), testReleases.erase(testReleases.begin() + long(r));
                    else ++r;
            }
            SDL_Gamepad* livePad = nullptr;
            if (pad) {
                float ax, ay;
                padStick(ax, ay);
                bool held = ax != 0 || ay != 0 ||
                            std::hypot(float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX)), float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY))) >= 0.25f * 32767;
                for (auto button : {SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_WEST})
                    held = held || SDL_GetGamepadButton(pad, button);
                held = held || SDL_GetGamepadButton(pad, v.settings.padSwap ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST);
                if (v.padHeldOver.live(held)) livePad = pad;
            }
            v.cr3CampPadHeld(livePad && SDL_GetGamepadButton(pad, v.settings.padSwap ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST));
            if (livePad) { // (the d-pad, or the stick pushed all the way: a direction's press, then its repeats)
                float ax, ay;
                padStick(ax, ay);
                SDL_Keycode d = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)      ? SDLK_UP
                                : SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)  ? SDLK_DOWN
                                : SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)  ? SDLK_LEFT
                                : SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? SDLK_RIGHT
                                                                                           : 0;
                if (!d && std::max(std::abs(ax), std::abs(ay)) >= 0.9f)
                    d = std::abs(ay) >= std::abs(ax) ? (ay > 0 ? SDLK_UP : SDLK_DOWN) : (ax > 0 ? SDLK_RIGHT : SDLK_LEFT);
                auto padPress = [&] {
                    if (v.cr3BarCursorOn()) return; // (the Vita's field buttons: its cursor by the presses alone)
                    pushKey(d);
                    if (testPad) SDL_Log("test controller: pressed %s", SDL_GetKeyName(d));
                };
                if (d != padDir) {
                    padDir = d;
                    if (d) padPress(), padDirNext = now + 600000000;
                } else if (d && now >= padDirNext) {
                    padPress(), padDirNext = now + 80000000;
                }
            }
            ++fpsFrames, ++totalFrames;
            if (now - fpsSince >= 500000000) {
                measured = float(double(fpsFrames) * 1e9 / double(now - fpsSince));
                fpsFrames = 0, fpsSince = now;
            }
            if (!v.paused) v.time += dt;
            if (!v.paused && v.battleMode && !v.fieldMode && !v.eventMode) v.advance(dt, 0, 0, false);
            // (CR3's battles too: one an event or an encounter starts turns the field and the event off,
            // and without this nothing advanced it - the screen's glass held, Battle_Start_Base never run)
            if (!v.paused && (v.eventMode || v.fieldMode || v.areaMode || v.cr3BattleMode || v.credits.stage || v.titleMenu.stage || v.cr3Title.stage)) { // the arrows / WASD run, with Shift walk (the title: its clock)
                const bool* keys = SDL_GetKeyboardState(nullptr);
                if (keys[SDL_SCANCODE_F] || (livePad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST) &&
                    v.cr3Camp.stage == 0 && !v.vs.screen && !v.movie.mv && (!v.cr3FieldIdle() || v.game.goodsButtons.empty()))) v.fastHeld = true;
                else if (!(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) v.fastHeld = false;
                float sx = float((keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) - (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]));
                float sy = float((keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) - (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]));
                bool run = !(keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
                if (livePad && !v.fieldTabOpen) { // (Fld2TaskPlayer::CalcPlayerMoveDir 0x810d60c0: a light tilt walks, further runs)
                    float ax, ay;
                    padStick(ax, ay);
                    const float dx = float(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) - float(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT));
                    const float dy = float(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) - float(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN));
                    if (ax != 0 || ay != 0) sx = ax, sy = ay, run = std::hypot(ax, ay) >= 0.55f;
                    else if ((dx != 0 || dy != 0) && !v.cr3BarCursorOn()) sx = dx, sy = dy, run = true; // (the d-pad the Vita's cursor's while it's up)
                }
                if (v.areaDrag.on) v.areaDrag.held += dt;
                if (v.areaCam.on) v.areaCam.held += dt;
                if (livePad) { // (the right stick: the Vita's camera, its 0.25 dead zone)
                    float rx = float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX)) / 32767.0f, ry = float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY)) / 32767.0f;
                    if (std::hypot(rx, ry) < 0.25f) rx = ry = 0;
                    v.myPagePadScroll(ry, dt);
                    v.areaCamStick(rx, ry, dt);
                }
                v.areaDragStick(sx, sy, run); // (a finger's drag: the field's stick)
                v.advance(dt, sx, sy, run);
            }
            if (v.fig.animLoaded && v.time > v.fig.anim.duration()) v.time = std::fmod(v.time, std::max(v.fig.anim.duration(), 0.05f));
            v.pose();
            SDL_SetWindowTitle(win, ("CR3-U viewer (" + std::string(cr3::backendName(dev->windowFlags() & SDL_WINDOW_VULKAN ? cr3::Backend::Vulkan : cr3::Backend::OpenGL)) + ", " +
                                     describe() + ", " + std::to_string(int(measured + 0.5f)) + " fps) - " + v.status + v.animStatus()).c_str());
            int w = 0, h = 0;
            frameSize(w, h);
            if (!padShot.empty()) { // (CR3_PAD_SCRIPT's shot=: this frame to a file, as --capture writes one)
                v.draw(w, h);
                dev->endFrame(false);
                std::vector<uint8_t> px;
                const bool ok = dev->readPixels(px, w, h) && saveBmp(padShot.c_str(), px, w, h);
                SDL_Log("test controller: shot %s %s", padShot.c_str(), ok ? "written" : SDL_GetError());
                padShot.clear();
            }
            v.draw(w, h);
            dev->endFrame(true);
        }
        const double secs = double(SDL_GetTicksNS() - started) / 1e9;
        SDL_Log("%d frames in %.1f s: %.1f a second (aiming at %s)", totalFrames, secs, totalFrames / std::max(secs, 1e-3),
                fps ? std::to_string(fps).c_str() : "the display's rate");
    }
    v.unload(v.fig);
    v.unload(v.mapFigure);
    v.unload(v.vistaMap);
    v.unload(v.secondMap);
    v.unload(v.battleMap);
    for (auto& u : v.battleUnits) v.unload(u.fig);
    for (auto* wl : {&v.vistaWindow, &v.vistaName, &v.narrationWindow, &v.conversationWindow, &v.tutorialMenuWindow,
                     &v.helpWindow, &v.annoWindow, &v.announceWindow, &v.announceTopWindow, &v.announceMidWindow,
                     &v.announceBotWindow, &v.systemWindow, &v.itemWindow})
        v.unload(wl->fig), v.unload(wl->positionFig);
    v.unload(v.conversationCursor);
    for (ViewFigure& f : v.cutInFigures) v.unload(f);
    v.unload(v.pageCursor);
    for (auto& c : v.cr3CutIns) v.unload(c.position), v.unload(c.portrait), v.unload(c.menu), v.unload(c.effect);
    v.unload(v.mapNameParts.position), v.unload(v.mapNameParts.menu), v.unload(v.mapNameParts.name);
    for (auto& d : v.mapNameParts.digits) v.unload(d);
    for (auto* p : {&v.messageButton, &v.vistaPageCursor, &v.announcePageCursor, &v.vistaFastButton, &v.vistaStopButton, &v.tutorialMenuPicture})
        v.unload(p->position), v.unload(p->fig);
    for (auto& q : v.textQuads) dev->destroy(q.tex);
    for (auto& q : v.nameQuads) dev->destroy(q.tex);
    for (ViewFigure& g : v.eventFigures) v.unload(g);
    for (ViewFigure& g : v.eventHeads) v.unload(g);
    for (auto& slots : v.eventAttachments)
        for (auto& [slot, att] : slots) v.unload(att.fig);
    if (v.white.id) dev->destroy(v.white);
    if (v.crossFadeTexture.id) dev->destroy(v.crossFadeTexture);
    if (v.cr3GlassTexture.id) dev->destroy(v.cr3GlassTexture);
    if (v.glassTexture.id) dev->destroy(v.glassTexture);
    for (ViewFigure& g : v.gimmicks) v.unload(g);
    for (auto& [k, q] : v.uiTexts) dev->destroy(q.tex);
    for (auto& [k, t] : v.textures) dev->destroy(t);
    dev->shutdown();
    SDL_DestroyWindow(win);
    v.audio.close(); // (its SDL streams and device before SDL goes: the Viewer outlives SDL_Quit)
    SDL_Quit();
    return code;
}
