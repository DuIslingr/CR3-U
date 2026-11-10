#pragma once
#include "platform/vita_import_ui.hpp"
#include "platform/atomic_file.hpp"
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <set>
#include <filesystem>
#include <functional>
#include <sstream>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include "assets/geom.hpp"
#include "assets/smooth.hpp"
#include "assets/hd_texture.hpp"
#include "assets/gltf_figure.hpp"
#include "assets/addons.hpp"
#include "assets/image.hpp"
#include "assets/maptable.hpp"
#include "assets/release.hpp"
#include "assets/language.hpp"
#include "assets/card_art.hpp"
#include "assets/mvgl.hpp"
#include "assets/skeleton.hpp"
#include "assets/spark.hpp"
#include "assets/movie.hpp"
#include "battle/battledata.hpp"
#include "battle/camera.hpp"
#include "battle/motion.hpp"
#include "battle/command.hpp"
#include "battle/rules.hpp"
#include "battle/script.hpp"
#include "battle/special.hpp"
#include "battle/glass.hpp"
#include "battle/cr3battle.hpp"
#include "battle/cr3rules.hpp"
#include "script/gamesave.hpp"
#include "event/runtime.hpp"
#include "script/vm.hpp"
#include <squirrel.h>
#include "viewer/audio.hpp"
#include "viewer/mobile_input.hpp"
#include "platform/input_cancel.hpp"
#include "platform/lifecycle_clock.hpp"
#include "viewer/setup.hpp"

std::string storedSetting(const char* key); // (settings.ini's data= / patch=: below)
#include "field/character.hpp"
#include "field/cr3field.hpp"
#include "field/fieldmap.hpp"
#include "field/fieldscript.hpp"
#include "gfx/material.hpp"
#include "gfx/render_device.hpp"
#include "gfx/cache_trim.hpp"
#include "gfx/text.hpp"

namespace cr3_viewer {

using cr3::Mat4;

// GL enums the materials' render states use
inline constexpr uint32_t kGlSrcAlpha = 0x0302, kGlOneMinusSrcAlpha = 0x0303, kGlFront = 0x0404, kGlBack = 0x0405;

// a place (rotation and translation, no scale) as a quaternion x y z w
inline void placeQuat(const cr3::Mat34& a, float q[4]) {
    const float* m = a.m;
    const float t = m[0] + m[5] + m[10];
    if (t > 0) {
        const float s = std::sqrt(t + 1) * 2;
        q[3] = s / 4, q[0] = (m[9] - m[6]) / s, q[1] = (m[2] - m[8]) / s, q[2] = (m[4] - m[1]) / s;
    } else if (m[0] > m[5] && m[0] > m[10]) {
        const float s = std::sqrt(1 + m[0] - m[5] - m[10]) * 2;
        q[3] = (m[9] - m[6]) / s, q[0] = s / 4, q[1] = (m[1] + m[4]) / s, q[2] = (m[2] + m[8]) / s;
    } else if (m[5] > m[10]) {
        const float s = std::sqrt(1 + m[5] - m[0] - m[10]) * 2;
        q[3] = (m[2] - m[8]) / s, q[0] = (m[1] + m[4]) / s, q[1] = s / 4, q[2] = (m[6] + m[9]) / s;
    } else {
        const float s = std::sqrt(1 + m[10] - m[0] - m[5]) * 2;
        q[3] = (m[4] - m[1]) / s, q[0] = (m[2] + m[8]) / s, q[1] = (m[6] + m[9]) / s, q[2] = s / 4;
    }
}

// between two places at t: the translation straight, the rotation by slerp
inline cr3::Mat34 blendPlace(const cr3::Mat34& a, const cr3::Mat34& b, float t) {
    float qa[4], qb[4];
    placeQuat(a, qa), placeQuat(b, qb);
    float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    if (d < 0) d = -d, qb[0] = -qb[0], qb[1] = -qb[1], qb[2] = -qb[2], qb[3] = -qb[3];
    float wa = 1 - t, wb = t;
    if (d < 0.9995f) {
        const float th = std::acos(std::min(d, 1.0f)), s = std::sin(th);
        wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    }
    cr3::JointTransform j;
    float l = 0;
    for (int k = 0; k < 4; ++k) j.rotation[k] = qa[k] * wa + qb[k] * wb, l += j.rotation[k] * j.rotation[k];
    l = std::sqrt(l);
    for (int k = 0; k < 4; ++k) j.rotation[k] /= l;
    for (int k = 0; k < 3; ++k) j.translation[k] = a.m[3 + 4 * k] + (b.m[3 + 4 * k] - a.m[3 + 4 * k]) * t;
    return j.matrix();
}

inline Mat4 perspective(float fovY, float aspect, float zn, float zf) {
    const float f = 1.0f / std::tan(fovY / 2);
    Mat4 r{};
    r[0] = f / aspect;
    r[5] = f;
    r[10] = (zf + zn) / (zn - zf);
    r[11] = -1;
    r[14] = 2 * zf * zn / (zn - zf);
    return r;
}

// a camera at `eye` looking at `at`, y up
inline Mat4 lookAt(const float* eye, const float* at) {
    float f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& x : f) x /= fl;
    float s[3] = {-f[2], 0, f[0]}; // f x up
    const float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    for (float& x : s) x /= sl;
    const float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    Mat4 r{};
    r[0] = s[0], r[4] = s[1], r[8] = s[2];
    r[1] = u[0], r[5] = u[1], r[9] = u[2];
    r[2] = -f[0], r[6] = -f[1], r[10] = -f[2];
    r[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    r[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    r[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
    r[15] = 1;
    return r;
}

struct ViewMesh {
    cr3::GpuMesh gpu;
    bool lit = false;
    cr3::Texture* texture = nullptr;
    cr3::Texture* normalMap = nullptr; // 0x35
    cr3::Texture* extraMap = nullptr;  // 0x48 CLUT or 0x43 light map
    cr3::GeomMesh source;              // (for skinning)
    cr3::GeomMaterial material;        // (its render states; its parameters as the animation leaves them)
    cr3::GeomMaterial baseMaterial;    // (as the file has it)
    std::vector<uint8_t> vertices;     // kVertexStride bytes each
    bool hidden = false;               // (PartsBase::SetVisible(joint, false): its joint's meshes)
    uint32_t placing = 0;              // its shader's cr3::VertexPlacing (billboards)
};

// A figure on screen: its meshes, skeleton, animations and where it stands
struct ViewFigure {
    std::string name; // "c005.geom"
    std::vector<ViewMesh> meshes;
    cr3::Skeleton skeleton;
    bool skinned = false;
    std::vector<cr3::Mat34> inverseBind;
    std::vector<std::string> anims; // the figure's animations
    size_t animIndex = 0;
    cr3::Animation anim;
    bool animLoaded = false;
    float centre[3] = {0, 0, 0}, radius = 1;
    cr3::Mat34 place; // in the world (the skinning matrices are place * world * inverse bind)
    bool absolute = false; // its animation alone in slot 0 (map figures): not a motion over the base
    // a cross-fade (EvtMotionManager's two slots): the animation it fades from, where in it, how
    // far the fade is (1: none)
    std::string blendName;
    cr3::Animation blendAnim;
    bool blendLoaded = false, blendAbsolute = false, blendLoop = true;
    float blendTime = 0, blendWeight = 1;
    size_t vertices = 0, triangles = 0;
    std::vector<cr3::JointTransform> posed; // (the pose last drawn)
    // its colour and opacity over its materials' (BtlModel::StartDiffuseAnimation /
    // StartAlphaAnimation: the special scenes'); short of 1 opaque meshes drawn blended
    float tint[4] = {1, 1, 1, 1};
    // joints whose world is set outright (Figure::SetJointWorldTransform: the matrix as it is, the
    // figure's place not over it) - by the joint's index
    std::vector<std::pair<size_t, cr3::Mat34>> jointWorld;
};

inline void packVertex(uint8_t* o, const float* p, const float* n, const float* uv, const float* c, const float* t, const float* b,
                const float* uv1) {
    std::memcpy(o, p, 12);
    std::memcpy(o + 12, uv, 8);
    std::memcpy(o + 60, uv1, 8);
    for (int k = 0; k < 4; ++k) o[20 + k] = uint8_t(std::clamp(c[k], 0.0f, 1.0f) * 255.0f + 0.5f);
    std::memcpy(o + 24, n, 12);
    std::memcpy(o + 36, t, 12);
    std::memcpy(o + 48, b, 12);
}

// The output resolutions F1 steps through: the window's own size, then 16:9 up to 3840 x 2160
inline constexpr int kResolutions[][2] = {{0, 0}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800}, {3840, 2160}};
inline constexpr int kMaxResW = 3840, kMaxResH = 2160;

// FSR 3.1's quality modes (the output / render ratio per axis; native AA renders at full size)
struct Quality {
    const char* name;
    float ratio;
};
inline constexpr Quality kQualities[] = {{"native", 1.0f}, {"quality", 1.5f}, {"balanced", 1.7f}, {"performance", 2.0f}, {"ultra", 3.0f}};

struct Viewer {
    cr3::RenderDevice* dev = nullptr;
    cr3::MvglArchive archive;
    cr3::AudioPlayer audio; // (the music, effects and voices: viewer/audio.hpp)
    std::vector<std::string> models; // the archive's .geom names, sorted
    size_t current = 0;
    ViewFigure fig;                  // the model (fields: the character)
    std::map<std::string, cr3::Texture> textures; // by name
    // HD textures (tools/hd_textures.py's): replacements found by the original's pixels' hash in
    // hdDir - CR3_HD_TEXTURES, else textures/hd/ in the game folder (the older hd-textures/ beside
    // the executable or in the profile folder while there is none)
    bool hdTextures = false;
    std::string hdDir;
    int hdLoaded = 0, hdMissing = 0;
    // HD models (the model bridge, assets/gltf_figure.hpp): replacements for figures' meshes from
    // modelsDir - CR3_HD_MODELS, else models/hd/ in the game folder - and the pictures they bring
    bool hdModels = false;
    std::string modelsDir;
    int hdModelsLoaded = 0;
    std::map<std::string, cr3::Image> modelImages; // "gltf\\<figure>\\<n>" -> its pixels (decode's first look)
    // the add-ons (assets/addons.hpp): outfits and weapons of the player's own, from models/addons/
    // (CR3_ADDONS elsewhere) - their items, names and goods in the release's tables; an outfit worn by
    // a character HD Models replaces looks like its like item, unless it says it fits (Cr3Addons::ownModel)
    cr3::Cr3Addons addons;
    std::map<std::string, std::vector<std::string>> hdReplacedCache; // (a wearer's: its figures in modelsDir)
    std::vector<std::string> hdReplaced(const std::string& wearer);
    bool addonOwnModel(const cr3::Cr3Addon& a);
    std::map<std::string, bool> textureMips; // (each cached texture's mip chain: a reload's)

    // CR3-U's own settings - CR3's Options' Graphics tab (the inherited options' list: settingsOpen), kept in
    // settings.ini in the profile folder: read at the start, written on each change (a capture
    // neither reads nor writes it); the command line's flags over them
    struct Settings {
        int display = 0;          // 0 a window, 1 fullscreen (the display's own mode), 2 borderless (the desktop)
        int resolution = 0;       // kResolutions: 0 the window's own size
        bool vsync = true;
        int fps = 60;             // frames drawn a second at most (0: as many as the display takes)
        int renderer = 0;         // 0 Vulkan, 1 OpenGL - from the next start
        int upscaler = 0;         // cr3::Upscaler: 0 off, 1 FSR 3.1, 2 DLSS, 3 MetalFX
        int quality = 1;          // kQualities
        int sharpness = 30;       // % (FSR's sharpening pass, RCAS)
        int msaa = 1;             // 1, 2, 4, 8 (Vulkan; none while an upscaler is on)
        int anisotropy = 16;      // samples (1: off)
        bool sharpTextures = false;
        bool hdTextures = false;
        bool hdSet = false;       // (settings.ini says which: else on when there are HD textures)
        bool hdModels = false;    // replacement models from models/hd (what loads next)
        int smoothModels = 0;     // smoothed models (assets/smooth.hpp): 0 off, 2 / 3 the PN triangles' level (what loads next)
        bool originalCardArt = false;
        bool lockOrientation = false, landscapeFlipped = false;
        bool padSwap = false;     // a controller's: × (south) confirms, ○ (east) goes back - else as the Vita
        int fieldSpeed = 1;       // the field while the player walks it (nothing running there), this many times as fast: 1, 2 .. 5
        int battleSpeed = 1;      // the battles, this many times as fast: 1, 2 .. 5
        int language = 0;         // the options' language: cr3::Language + 1 (0: none picked yet)
    } settings;
    bool settingsPersist = false;
    int rendererInUse = 0;        // (the device that started: the Renderer row's "next start")
    SDL_Window* window = nullptr;
    int appliedDisplay = -1, appliedMsaa = -1;
    bool appliedVsync = true, vsyncApplied = false;
    bool orientationApplied = false, appliedLock = false, appliedFlip = false;
    static const char* settingsFile();
    void loadSettings();
    void saveSettings() const;
    void applySettings();
    // The HD replacement for a picture; one that doesn't fit it is left out, said once
    std::set<std::string> hdRefused;
    bool loadHd(const cr3::Image& img, cr3::Image& hd);
    void reloadTexture(const std::string& name, cr3::Texture& t);
    void reloadTextures();
    static std::string shortDir(const std::string& dir);
    struct SettingRow {
        std::string label, value, help;
        bool enabled = true;
        std::function<void(int)> change;
    };
    bool settingsOpen = false;
    int settingsCursor = 0;
    float settingsStart = 0;
    std::vector<SettingRow> settingRows();
    void settingChange(int row, int dir, bool click = true);
    void openSettings();
    void closeSettings();
    void drawSettings(const Mat4& view, int w, int h);
    bool settingsKey(SDL_Keycode k);
    bool settingsTapped(int id);
    cr3::SceneLighting lighting;                  // the light figure's (--lights)
    std::unique_ptr<cr3::ShaderLibrary> shaders;  // the archive's (its GLSL, or the keys decoded)
    float yaw = 30, pitch = 15, zoom = 1;
    std::string status;
    bool paused = false;
    float time = 0;
    // the field
    bool fieldMode = false;
    cr3::FieldMap field;
    ViewFigure mapFigure; // <map>f
    std::vector<ViewFigure> gimmicks; // the figures <map>p's NPCs, objects, boxes and effects show
    cr3::GameState game;                        // the story's flags and work values (a new game's)
    // the game's database, offline as the Vita plays it (db/cr3db.hpp: the release's server
    // tables and DBEmu's rules) - every release's, never a server
    cr3::Cr3OfflineDb offlineDb;
    void resetGame();
    std::unique_ptr<cr3::FieldScript> script;   // the dungeon's script
    float player[3] = {0, 0, 0}, heading = 0;
    std::unique_ptr<cr3::CharacterController> controller; // (on the map's walls and floors)
    float stepClock = 0;

    // FldPlayer::Update: the stick (screen right / up, -1..1) as a direction along the ground from
    // the camera's view, walking (0.09 units a second) or running (0.48). The game's update (20 a
    // second) is the controller's three substeps (character.hpp), one each 1/60 s - the stick read
    // at each - with the exits checked at the update's end as the game does. A frame between
    // substeps shows the player between the last two; the figure stands 0.02 below the sphere.
    float playerPrev[3] = {0, 0, 0}, playerCur[3] = {0, 0, 0};
    int substep = 0;

    // The event boxes the leader walks into (Gimmick::Event, not _check - those are tapped):
    // entering one with its event on (Cr3GimmickData.SetEventEnable) calls its function, the
    // script's coroutine then running as the area's entry does (FldMain::CallEvent)
    std::set<std::string> insideEvents;
    bool pendingBranch = false;
    void touchEvents(const cr3::Vec3& p);
    void placePlayer(const cr3::FieldScript::Placement& pl);

    // The balloons over what the leader can tap (FldMain::CheckBalloon, each update it has control:
    // Gimmick::checkLength), the nearest the one a tap talks to (CalcNearestBalloonNum)
    std::vector<size_t> balloons; // (field.gimmicks() indices)
    int nearestBalloon = -1;
    std::string boxesDungeon;               // (the dungeon the boxes were drawn for: "d01", "t01")
    std::map<std::string, float> boxOpening; // (a box's gimmick name -> when it opened: its fa01)
    static uint32_t boxRandom();
    void checkBalloons();

    // The field walked by a drag (Fld2TaskTouch, Fld2TaskPlayer::CalcPlayerMoveDir): the finger's
    // offset from where it touched, as a fraction of the screen's width and height - under 2%
    // standing, under 5% walking, else running (Fld2TaskPlayer's 0.0002 / 0.0005 of that x 0.01) -,
    // turned by the camera; let go without having moved: a tap. Fld2Controller shows it once moved
    // or held half a second: feConMove01 where it touched, feConMove03 at the finger - 128 of the
    // screen's 960 at most -, feConMove02 halfway (its own view 200 pixels a unit: 2 x the UI's)
    struct Cr3AreaDrag {
        bool on = false, moved = false;
        float x0 = 0, y0 = 0, x = 0, y = 0, held = 0;
        bool own = false;        // (a finger of its own; else the pointer's - the mouse, or the finger SDL makes one of)
        SDL_FingerID finger = 0;
    } areaDrag;
    bool areaDragFree() const;
    void areaDragStart(float fx, float fy, bool own = false, SDL_FingerID finger = 0);
    void areaDragMove(float fx, float fy);
    void areaDragEnd();
    // The camera's half of the screen (Fld2TaskTouch::UpdateTouch, with the Gom camera - Fld2TaskCamera
    // type 1 - the touches split by fieldTouchMode, the Options' Controls: Two-Handed 0 the right half,
    // One-Handed 1 the top half; the other half walks, both at once). Held, its offset from where it
    // pressed turns the camera (Fld2CameraGom::UpdateControl) - each way under 20 pixels of the
    // screen's 960 x 640 nothing, under 60 75, further 250, x 60 x 0.0001 a frame (0.45 or 1.5 rad a
    // second): across turning it, up and down tilting it (the finger down, the view down). Let go
    // within 20 of where it pressed: a tap. Fld2Controller(1) shows it once moved or held half a
    // second: feConCamera where it pressed, feConMove03 at the finger. CR3-U's: the right mouse
    // button's drag, anywhere
    struct Cr3AreaCam {
        bool on = false, moved = false, mouse = false; // (mouse: the right button's)
        float x0 = 0, y0 = 0, x = 0, y = 0, held = 0;
        bool own = false; // (as the walk's)
        SDL_FingerID finger = 0;
    } areaCam;
    bool pointerTouch = false; // (the press being handled: SDL's from a finger)
    bool areaCamFree() const;
    bool cameraHalf(float fx, float fy) const;
    void uiUnits(float fx, float fy, float& ux, float& uy) const;
    void areaCamStart(float fx, float fy, bool mouse, bool own = false, SDL_FingerID finger = 0);
    void areaCamMove(float fx, float fy);
    void areaCamEnd();
    // The Vita's right stick (Fld2CameraGom::UpdateControl 0x810c4f9a, FUN_8128d144's axes 2 / 3, with
    // no camera touch): its push x -120 as the touch's offset - the same 20 / 60 steps -, so the
    // stick right as a finger moved right, down as one moved down. With the Vita copy as the game
    bool areaCamSticking = false;
    void areaCamStick(float rx, float ryDown, float dt);
    void areaCamStep(float dt);
    // a finger SDL makes no mouse of (a second one down): the walk's or the camera's by its half
    bool mouseFingerOn = false; // (the finger SDL's mouse follows - SDL_touch.c: the first down while none is)
    SDL_FingerID mouseFinger = 0;
    void fingerEvent(Uint32 type, SDL_FingerID id, float fx, float fy);
    bool areaDragStick(float& sx, float& sy, bool& run) const;
    void tap();
    void callGimmick(const std::string& function);
    void openBox(const cr3::Gimmick& g);

    // A key box's lock (FldBox's new mark, FldUnlock). The leader at an unopened key box gets its
    // mark over it (0.125 above it; FieldGimickEffect02Menu for Orlando - gmk2ring, its io then its
    // wait, and gmkBtn's io - else 00's gmk0rng_io). Tapped with Orlando leading, the lock: gmkBtn
    // out, then gmk2csr_loop; two fingers put down within 450 of the mark (the interface's units x
    // 2048: Cr3UtilSetBalloonPos) and spread 1000 apart (platform 1's; the ring scaled 1 + spread /
    // 1000) open it - SE 0x164, the ring out, 20 updates on the box opened (FldMain step 0x16:
    // its function called, FldBox::Open, SE 0x154); anything else fails it (SE 0x165, the mark
    // anew). The mouse is one finger, the other mirrored about the mark.
    struct KeyLock {
        int gimmick = -1;       // (the key box marked: field.gimmicks()'s; -1 none)
        bool orlando = false;   // (the mark's kind: 2, else 0)
        int stage = 0;          // 0 the mark, 1 the lock (fingers awaited), 2 opened (counting down)
        float markAt = 0, stageAt = 0;
        float spread = 0;       // (the fingers' distance, x 2048)
        bool pressing = false;
        int countdown = 0;
    } keyLock;
    static constexpr float kUnlockReach = 450, kUnlockSpread = 1000;
    void startUnlock(int gimmick);
    void unlockEnd(bool success);
    void updateKeyLock();
    bool keyMarkPlace(float& x, float& y);
    bool uiPlace(const float p[3], float& x, float& y);
    void unlockPointer(float fx, float fy, int phase);
    // A sprint (FldSprint): the leader within 0.15 of an enabled one's place, a tap within 128 (x
    // 2048: FldUtilCheckTouchPosCollision) of its marker (obj151) - or Return - starts it
    // (FldMain step 0x15): crouched (fj01), then a hop to each of its spots in turn (fj02, SE
    // 0x155: a parabola under 0.98 - up 0.04 above the higher of the two -, Cr3Calculate...ArrivesAt;
    // fj03 to land, fj01 0.2 s before it, SE 0x156), then idle (fn01)
    struct Sprint {
        int stage = 0; // 0 none, 1 crouched, 2 a hop
        std::vector<std::array<float, 3>> to;
        size_t k = 0;
        float t = 0, T = 0, v = 0, vx = 0, vz = 0;
        float from[3] = {0, 0, 0};
        int air = 0; // (the hop's motions: 0 fj02, 1 fj03, 2 fj01)
        bool jump = false; // (a high jump's: the voice 0x8fa at each launch)
    } sprint;
    // The touch actions' marks (FldMain::CollisionAdded: the leader's body touching an enabled
    // one's place) on its first spot: its own character leading (kind 1 high jump - Darwin, 0 -,
    // 3 mystic - 2 -, 4 iai - 3 -, 5 aim - 4 -, 6 hakkei - 5), FieldGimickEffect0NMenu's (1:
    // gmk1rng, its io; 3..6: gmkNrng, its io then its wait, and gmkBtn's io), anyone else 00's
    // gmk0rng_io; gone as the leader leaves. A tap within 128 of it (FldMain::
    // CheckTouchActionStartOne) starts the action - for anyone else SE 0xb6. A high jump hops as
    // a sprint does (FldHighJump: the voice 0x8fa with each launch; its mark out, gmk1rng_out);
    // an iai (FldIai) takes the button out (then gmk4csr_loop) and waits for a slash: a press,
    // then within 10 frames its line from where it began past the mark - the mark's foot on the
    // line within 128 of it (Cr3UtilCheckCuttingCircle) - SE 0x164, the ring out, the gimmick's
    // function called (FldMain::CallEvent); else SE 0x165, the mark anew. An aim (FldAim) takes
    // the button out too (then FieldGimickEffect05Menu::SetAimingSet: gmk5mark_io on the mark,
    // gmk5goal_io on the leader, gmk5line between): a press within 128 of the mark, the ring
    // dragged with it (the line to it, scaled by its length / 0.112), let go within 128 of the
    // leader - SE 0x164, the function called -; let go elsewhere, or a press away from the mark,
    // SE 0x165 and the mark anew. A hakkei (FldHakkei) takes the button out, then its balls show
    // (FieldGimickEffect06Menu::SetCube: gmk5ball at the ring's call_gmk5ball joints - the
    // triangle's three, the pentagon's five from four on), one for each tap it wants (SetHakkei
    // TouchCnt; 5): each tap within 128 of the mark a punch (fp02 .. fp06), SE 0x160 or 0x161, a
    // ball gone; all of them, from the first, within 60 frames - SE 0x164, the function called -;
    // a tap away from the mark, or the time out, SE 0x165 and the mark anew. A mystic's mark is on
    // its active spot (SetMysticActiveSpotName); started (FldMystic), the button out and a ring
    // on each of its other spots (FieldGimickEffect03bMenu: gmk3rng); a tap within 128 of one
    // names it (SetTapSpotName - GetMysticTapSpotName: the script moves its object there), SE
    // 0x164, its ring out, the function called; a tap elsewhere SE 0x165 and the mark anew
    struct ActionMark {
        int gimmick = -1, kind = 0;
        bool able = false;
        float at = 0;         // (shown: uiClock)
        float startedAt = -1; // (begun)
        float endedAt = -1;   // (succeeded: the ring out)
    } actionMark;
    static int actionKind(const std::string& n);
    static int actionCharacter(int kind);
    struct Iai {
        int stage = 0; // 0 none, 1 the slash awaited, 2 pressed
        float x0 = 0, y0 = 0, pressedAt = 0;
    } iai;
    struct Aim {
        int stage = 0; // 0 none, 1 a press on the mark awaited, 2 dragging
        float x = 0, y = 0; // (the touch: the interface's units)
    } aim;
    struct Hakkei {
        int stage = 0; // 0 none, 1 the taps awaited
        int wanted = 5;
        std::vector<float> tappedAt;
    } hakkei;
    struct Mystic {
        int stage = 0;  // 0 none, 1 a spot's tap awaited
        int chose = -1; // (the spot tapped: its ring out)
        float choseAt = 0;
    } mystic;
    bool actionRunning() const;
    void updateActionMark();
    bool actionMarkPlace(float& x, float& y);
    void actionTapped();
    bool actionTap(float fx, float fy);
    void iaiEnd(bool success);
    void iaiPointer(float fx, float fy, int phase);
    bool leaderPlace(float& x, float& y);
    void aimPointer(float fx, float fy, int phase);
    void hakkeiTap(bool onMark);
    void hakkeiPointer(float fx, float fy);
    void mysticTap(int spot);
    void mysticPointer(float fx, float fy);
    void updateIai();
    void drawActionMark(const Mat4& view);
    int sprintAt() const;
    bool sprintTap(float fx, float fy);
    void startSprint(int i);
    void sprintHop();
    void updateSprint(float dt);
    void drawKeyMark(int w, int h);
    void walk(float dt, float sx, float sy, bool run);

    // GameSystem::InitLang: a language other than Japanese adds the texture folder images_eng,
    // looked in before images (the iPhone's and Android's English interface: 314 of them)
    bool englishTextures = false;
    cr3::CardArtwork cardArtwork;
    bool originalTextJapanese = false; // the language choice, not the fan translation's slot number
    bool decode(const std::string& name, cr3::Image& img);
    cr3::Texture* texture(const std::string& name, bool mipmaps = true);
    bool loadLights(const std::string& figure);
    bool figureLights(const std::string& figure, cr3::SceneLighting& l);

    // a figure's joint as last posed, in the world (its .name file's order)
    std::map<std::string, std::vector<std::string>> jointNames;
    bool jointWorld(const ViewFigure& f, const std::string& joint, cr3::Mat34& out);
    void drawAttached(ViewFigure& a, const ViewFigure& on, const std::string& joint, float t, const float* mvp);
    void unload(ViewFigure& f);

    // CR3's texture variations (CrxModel::GetVariationTexture): its textures' "a" replaced by the
    // variation's letter, where that texture exists (m234a00 -> m234c00); set while loading
    std::string figureVariant;
    std::string variantOf(const std::string& name) const;
    static std::string motionBase(const std::string& base);
    void takeModelPictures(std::vector<std::pair<std::string, cr3::Image>>& pictures);
    void replaceFigure(const std::string& name, cr3::Geom& g);
    bool addonFigure(const std::string& name, cr3::Geom& g);
    bool loadFigure(const std::string& name, ViewFigure& f, std::string* err);
    bool load(size_t index);
    void loadAnim(ViewFigure& f, size_t index);

    // A field motion of the character (FldPlayer::CreatePlayer: c<id> + "fn01" idle, "ff01" walk,
    // "ff02" run, "fj01".. jumps, "fp01".. its own), if it has it and isn't playing it already
    // A character's shadow (FldShadow: obj001 on its own animation, given the character's place -
    // FldPlayer::Pose - and drawn before it in the transparent pass): the leader's, and an event's
    // c / v models' (EvtModel::Init) unless EvtScene.ShadowVisible hid it
    ViewFigure shadowFig;
    bool shadowTried = false;
    void drawShadow(const cr3::Mat34& place, const float* mvp);
    std::string leaderMotion; // (a script's: held while the leader stands)
    int leaderMotionSerial = 0;
    void playMotion(ViewFigure& f, const char* motion);
    std::string animStatus() const;
    void pose(ViewFigure& f, float t, bool loop = true);

    // The camera the figures are posed for (its place in the world): billboards turn to it
    cr3::Mat34 poseView;
    float modelEye(float* eye) const;
    void billboard(ViewMesh& vm, const cr3::Mat34& p0);
    void pose();
    void drawFigure(const ViewFigure& f, const float* mvp);
    void drawLayer(const std::string& image, const Mat4& proj, float shiftX = 0);

    // The output: a chosen resolution (0 x 0: the window's), and the temporal upscaler (Vulkan)
    int resW = 0, resH = 0;
    cr3::UpscaleSettings upscale;
    int screenX = 0, screenY = 0, screenW = 1, screenH = 1; // the game's screen in the frame: the UI's, inside the display's safe area
    int frameX = 0, frameY = 0, frameW = 1, frameH = 1;     // the frame the scene fills: under a camera hole, into the corners

    cr3::Texture crossFadeTexture;
    // GameMain::PostEffectBlur (0x0096ca54): the scene's ping-pong buffers (PostEffectBuffer 4 / 5,
    // read [n & 1], written [(n & 1) ^ 1]), the frames it has run (DAT_014d0d20), layer 0xc's zoom
    // (the scene put on the screen: the special blur's) and the frame copy that zoom draws from
    struct SceneBlur {
        cr3::Texture buf[2], zoomed;
        uint32_t n = 0;
        float zoom = 1;
    } sceneBlur;
    void applySceneBlur(int fw, int fh);
    // GameMain::PostEffectBloom (PostEffects::RenderBloom 0x00b90908): the scene copied to 512 x 512,
    // that to 32 x 32 (its luminance read back - the other buffer's, a frame late), the bright pass
    // into 128 x 128 (bloomLimit: exp(sum ln(r + g + b) / 1023 / 1024) + the limit), five blurs
    // (V, H, V, H, V: blurOffset the offset x 0.01, x the scale), added onto the scene (ONE, ONE)
    struct SceneBloom {
        cr3::Texture scene, r512, r32[2], rt[3];
        int read = 0;
        bool logged = false, unsupported = false;
    } sceneBloom;
    void applySceneBloom(int fw, int fh);
    void blurQuad(const cr3::Texture& t, int fw, int fh, float scale, float roll, float alpha, bool additive);
    void grabCrossFade(int fw, int fh, cr3::EventRuntime& ev);
    void draw(int fw, int fh);
    void fullFrameQuad(const cr3::Texture& t, const float* v, const float* rgba);
    void safeInsets(int fw, int fh, int& ix, int& iy) const;
    void placeScreen(int fw, int fh);
    void drawScene(int w, int h);

    // The maps' table from the game's executable (--binary, or beside the iPad's / Vita's archive)
    std::vector<cr3::MapInfo> table;
    std::string tableFrom;
    bool exitsArmed = false; // (an exit counts once the character has left them all since arriving)

    std::string dataArg, binaryArg; // (the release's: the staff roll's table from its executable too)
    bool loadTable(const std::string& binary, const std::string& dataPath);

    // the gimmicks shown with a figure: placed by their joints, NPCs idling (FldNPC: <figure>fn01) -
    // as the script has them now
    // the resident effects the script played (FldEffectResident: its figure on the leader - its
    // place and facing - or a gimmick's, scaled, its animator once from its start)
    std::map<int, ViewFigure> residentFigs;
    std::map<int, int> residentSerial;
    void drawResidents(const float* mvp);
    std::vector<std::string> shownGimmicks;
    std::vector<std::string> gimmickIds; // (the figures' gimmicks: the script's names for them)
    std::vector<int> gimmickSerial;      // (the event motion each has loaded: its SetMotion's count)
    struct Facing {
        int serial = 0;
        float yaw = 0, from = 0, to = 0;
        bool done = true; // (the leader's: its turn's end applied - the player's to steer then)
    };
    std::vector<Facing> gimmickFace; // (each one's turn: ChangeDirection)
    std::vector<int> gimmickColor;   // (the colours each wears: ChangeModelColor's, -1 its own)
    Facing leaderFace;
    bool gimmickTarget(const std::string& target, float& x, float& z) const;
    static float turnBetween(float from, float to, float a);
    void refreshGimmicks();
    bool enterMap(const std::string& map, const std::string& at, const std::string& entry = "evt000");
    bool loadField(const std::string& map, const std::string& at, const std::string& binary, const std::string& dataPath,
                   const std::string& entry = "evt000");

    // The field's script, a coroutine stepped at the game's 20 updates a second (its events'
    // scene, windows and fades with it); the leader held while it runs, as FldMain holds it
    float fieldClock = 0, pageSeconds = 3.0f;
    bool fieldScriptRunning() const;
    void fieldInput();
    void fieldUpdate(float dt);

    // A frame's time: the vista's event, else the field (its script, then the leader - held while
    // the script runs)
    float uiClock = 0; // (the menus' seconds: the options', the manual's, the camp's - in a battle or out)
    // the fast-forward button's place as last drawn (id 0: not shown), whether it is held (the
    // mouse on it, or F), and --input fast's two seconds of it
    struct { int id = 0; float x0 = 0, y0 = 0, x1 = 0, y1 = 0; } fastRect;
    bool fastHeld = false;
    float fastUntil = -1;
    // a press on the screen at (fx, fy) - fractions of it, from the top left: what's over it takes it
    struct MenuList { int* cursor = nullptr; int count = 0, context = 0; };
    mobile::MenuSwipe menuSwipe;
    MenuList menuList();
    bool menuScroll(int rows);
    bool pointerCancelled = false;
    void cancelPointerInput();
    crinput::HeldOver padHeldOver; // (the controller: dead after a cancel or a session's end, until let go)
    bool consumeInputCancel(crinput::CancelWatch& cancel);
    bool menuPointerDown(float x, float y);
    void menuPointerMove(float x, float y);
    bool menuPointerUp(float x, float y);
    void pointerDown(float fx, float fy);
    void pointerActivate(float fx, float fy);
    void testClick();
    bool overFast(float fx, float fy) const;
    int loggedTeamLevel = 1; // (the team level as last logged)
    void advance(float dt, float sx, float sy, bool run);
    cr3::EventRuntime* activeEvents();

    // --play: the game's flow - the field's entry, then what it reserved (GameState::next):
    // another area (SetPopPoint), a battle, a vista event (FldMain::BranchAfterEvent); after a
    // vista, its battle, another vista, else the field again (VistaMain::BranchAfterVista: evt200
    // after SetComeFromVistaToField); after a battle the field (evt100). Battles aren't in yet:
    // one is taken as won.
    bool playMode = false;
    std::string returnMap;
    float returnPlayer[3] = {0, 0, 0}, returnHeading = 0;
    void branchAfterField();
    // a battle the flow reached: fought (its party SetBattleParty's, else the party now; the party
    // attacking as a pair each turn), then on as the game goes after it (BtlMain::Exit) - or,
    // with --skip-battles, taken as won at once
    bool skipBattles = false;
    void skipBattle();
    bool fightingInFlow = false;
    void leaveBattle();
    void afterBattle();
    int battlesSkipped = 0;
    void branchAfterVista();

    // The staff roll (ReserveCredit(n) - CreditMain; InterfaceMain::SetStaffrollParameter n + 1:
    // StaffrollMenu, interface 0x76 + n): from black (Cr3FadeIn 60 frames), the ending's music
    // (0x51e0e8: none, 17, 28, 29 - its fade 20), the ending's figure (end2_BG, end1_BG, end3_BG,
    // end4_BG: the table's four after its lines) on its own animation - scaled aspect / 1.5 across
    // a screen wider than 3:2 -, its typeS_L01 rising: line i (the executable's 544 - 44 .. 49 only
    // with flag 201, 438 .. 533 smaller) i x 0.0322 (scaled) below it, shown once it has risen so
    // far and gone above 0.4; the speedUp button, held, the figure five times as fast. All shown
    // and the animation ended: 120 updates, then black (60) with the music stopped, 62 more, and the
    // field (mode 4: the area's `credit`).
    struct Credits {
        int stage = 0; // 0 none, 1 rolling, 2 the wait after it, 3 fading out
        int number = 0;
        float animTime = 0, stageTime = 0, startY = 0;
        bool started = false;
    } credits;
    std::vector<std::string> staffLines;
    std::string staffFigures[4];
    bool staffTried = false;
    void startCredits(int n);
    std::string creditFigure() const;
    float creditScale() const;
    bool creditsLineShown(size_t i) const;
    void updateCredits(float dt);
    void drawCredits();
    void returnToField(int mode);

    // A battle's stage (--battle: BtlMainCtrl::StepInit): the coupling's map (its own lights - the
    // directional one turned to come from the camera, BtlMain::SetLights' default), the party and
    // the monsters each on its battledpos joint (BtlModel::SetPosRot: the joint's place and turn)
    // playing its first motion (BtlMotionTable: the party bb02 then bb01, the monsters bb01), seen
    // through the commands' camera (Cam_Getset_<pattern>_00, a camera of BattleCamera_nomove /
    // _move; BtlCamera::Start: near halved - at least 0.1 - far five times, at most 5000)
    bool battleMode = false;
    cr3::BattleData battleData;
    cr3::BattleSetup battle;
    ViewFigure battleMap;
    ViewFigure specialMap;        // (BtlMap +0x13c: d1154b, Darwin's third awakening's)
    bool specialMapShown = false; // (+0x144)
    ViewFigure mapEffect;         // (+0x140: eff284, drawn with the map - +0x145)
    float specialMapStart = 0;
    // the screen breaking away (BtlPostEffectGlassFragment: scene op 33): the map alone drawn
    // into a texture at the next frame (BtlMain::PostEffectGlassFragments), its shards over the scene
    cr3::GlassFragments glass;
    bool glassPending = false;
    float glassStart = 0;
    cr3::Texture glassTexture;
    cr3::Rand48 glassRng;
    struct Ramp { // (a value from `from` to `to` over `time` s from `start`: StartAlphaAnimation's)
        float from = 1, to = 1, start = 0, time = 0;
        float at(float t) const {
            if (t <= start) return from;
            if (time <= 0 || t >= start + time) return to;
            return from + (to - from) * (t - start) / time;
        }
        void begin(float t, float f, float e, float d) { from = f, to = e, start = t, time = d; }
    };
    struct BattleUnit {
        ViewFigure fig;
        std::string figure;      // ("j001", "m206")
        std::string position;    // its battledpos joint
        cr3::Mat34 home;         // (there)
        // its motion: `motion` (a suffix: "bb02") from `motionStart` (battle seconds), once unless
        // `loop`, then `then` looping
        std::string motion = "bb01", then = "bb01";
        float motionStart = 0;
        bool loop = true;
        bool hidden = false;
        bool hold = false;        // (its motion kept at its end: the downed's bd02)
        bool guarding = false;    // (Defend chosen: its stance bg01 - motion 4 - till the turn's end)
        float hideAt = -1;        // (SetAlphaAnimationAutoHide: gone once its fading is over)
        bool sceneHidden = false; // (a special scene's: SetVisibility)
        Ramp alpha, colour;       // (its opacity, its brightness: SetAlpha, SetColor)
        // the frame its target camera is in (StepBattleIn_Setup's: its DummyCP on its stance's
        // first frame there), once found
        cr3::Mat34 targetFrame;
        bool targetFrameSet = false;
    };
    std::vector<BattleUnit> battleUnits; // (the party, then the monsters)
    struct CameraFigure {
        cr3::Geom geom;
        cr3::Skeleton skel;
        cr3::Animation anim;
        bool animated = false;
    } battleCameras[2];
    std::string battleCamera;
    float battleClock = 0, battleCameraStart = 0; // (the camera's animation runs from its start)
    // (`place`: the frame it is in - a target's camera its monster's -, `still`: its first frame
    // held, BtlCameraCtrl::GetCameraInfo's for StartCtrlCamera)
    cr3::Mat34 battleCameraPlace;
    bool battleCameraStill = false;
    // a move from `battleCameraFrom` (the camera's world when it began) to the camera over 0.5 s
    // from `battleCameraMoveStart` (< 0: none) - BtlMoveCtrl's interpolation 1, k 10
    cr3::Mat34 battleCameraFrom;
    float battleCameraMoveStart = -1;
    int targetCameraFighter = -1; // (whose target camera it is; -1 another)
    void setBattleCamera(const std::string& name, const cr3::Mat34& place = cr3::Mat34(), bool still = false);
    bool battleCameraLoop = false; // (BtlCameraCtrl::SetLooping)
    void setBattleCamera(int number);

    // a new game's party state (Cr3InitWorkFromBattleData): the sopia's levels, the items, the
    // equipment - before the scripts change them
    // The system data (Cr3Util_Commit / Save / Load_SystemData: system.dat - the flags 0..991,
    // cr3_flag_system_work, and the options - kept across games: a new game's Cr3UtilResetWork
    // leaves them): CR3-U's system.txt beside the saves, written as the game saves (SaveConfirmationMenu)
    // and as the options close (OptionMenu's end), laid over every game begun or loaded
    static constexpr int kSystemFlags = 992;
    bool systemRead = false, systemFound = false;
    std::vector<int> systemFlags;
    int systemOptions[6] = {-1, -1, -1, -1, -1, -1}; // (+ fieldTouchMode: the Controls' hands - DBLoadPlayer::ReadSaveData_Option)
    void readSystem();
    void applySystem(bool importFlags = true);
    void writeSystem();
    void resetForNewSession();
    void newGame();
    bool startBattle(int id, const int party[2]);
    bool battleView(cr3::Mat34& world, float& fovY, float& zn, float& zf);
    bool useMotion(BattleUnit& u, const std::string& suffix);
    float motionLength(BattleUnit& u, const std::string& suffix);
    void playMotion(BattleUnit& u, const std::string& suffix, float at, const std::string& then = "bb01");
    std::string stanceOf(const BattleUnit& u) const;
    int fighterOfUnit(const BattleUnit& u) const;
    void poseBattle();

    // --fight solo | pair: the battle played by its rules (battle/rules.cpp), each turn's actions
    // one after another as BtlActionCtrl shows an attack - the command's motion
    // (BtlMotionTable's), the attacker moving to its aPos joint by the target over the table's
    // move times and back over the second's, the targets' damage motion (bd01) at the first hit
    // time, the downed hidden - the party attacking every turn
    std::unique_ptr<cr3::BattleRules> battleRules;
    int battleFight = 0; // 0 off, 1 solo, 2 pair
    int fighterUnit[5] = {-1, -1, -1, -1, -1};
    cr3::Skeleton battleApos;
    std::vector<cr3::Mat34> battleAposWorld;
    struct ShowAction {
        int actor = -1, partner = -1, command = 0;
        std::vector<cr3::BattleRules::Event> hits;
    };
    std::vector<ShowAction> showActions;
    size_t showIndex = 0;
    float showStart = 0; // (battle seconds)
    float showHitAt = 0, showEnd = 0;
    int showCamera2 = -1;
    bool showHitDone = false;
    struct Move {
        int unit = -1;
        cr3::Mat34 to;
        float start = -1, end = -1, back = -1, backEnd = -1;
        bool active = false;
    } showMoves[2];
    bool aposPlace(const std::string& name, cr3::Mat34& out);
    std::string aposName(int actor, int target);

    // The fight's steps (BtlMainCtrl / BtlCommand / BtlActionCtrl), the battle's script between:
    // the opening, Battle_Start; each turn Turn_Start, its order, then for each action
    // Command_Start, the action shown, Command_End; the end - each step after the script's call
    // is over (BtlEvent::IsRunning), its talks on the screen meanwhile
    enum class BattleStep { Opening, TurnBegin, TurnChoose, ActionStart, ActionDo, ActionEnd, Over };
    // The party's commands chosen on the screen (BtlCommand: the menus, a tap or the keys - Up /
    // Down and Return, Backspace back, Left / Right among the targets), or given (--input: "pair
    // attack 2 ...", one a 0.4 s); else (a capture, --fight) the party attacking each turn
    bool manualCommands = false;
    bool choosing = false;
    cr3::CommandInput commandInput;
    float menuStart = 0;
    int menuCursor = 0;
    std::deque<std::string> inputScript;
    float inputNext = 0;
    struct TapRect {
        int id;
        float x0, y0, x1, y1; // (UI units)
    };
    std::vector<TapRect> tapRects;
    // ---- The keys' (and a controller's) focus where the iPad's and Android's game is touch alone and
    // the Vita moves a cursor (GameTitleMenu's buttons and questions, the shop's first two menus, the
    // party's): the arrows to the next button that way (by their tap areas' middles, round past the
    // last), Return its tap, the focused one in the game's own look (CircleBtn::SetForcusTarget: PRG_shiftR
    // half across, as a focused command button) - and the field's tab, which the Vita steps round
    // (FieldMenuTabMenu: camp, encounters, save, map; ○ the one lit)
    int keyFocus = -1; // (a tap area's id; -1 none yet)
    int tabFocus = 0;  // (the tab's: its area - 2 camp, 5 encounters, 4 save, 3 map; 0 none)
    bool focusScreen() const;
    bool focusKey(SDL_Keycode k);
    bool tabFocusKey(SDL_Keycode k);
    bool listShown() const;
    bool inMenu() const;
    void menuChanged();
    void setTargetCamera(int f);
    const cr3::Mat34& targetFrame(BattleUnit& bu);
    void inputToken(const std::string& t);
    std::vector<int> menuChoices() const;
    void choose(int idOrFighter);
    void battleSe(int n);
    struct VoiceJob {
        float at = 0;
        int clip = -1;
    };
    std::vector<VoiceJob> voiceJobs;
    void battleVoice(int f, int kind, float delay = 0, bool tableKind = false);
    std::vector<VoiceJob> seJobs; // (BtlSound::CreatePlaySEJob: an effect's clip later)
    void cancelSceneAudio();
    void updateBattleSounds();
    int pendingBgm = 0;
    float pendingBgmAt = 0;
    // a turn break beginning (BtlTurnEnd: the break due, +0x1f 3 -> 1 / 2): SE 2, then 1 s on 3 for
    // the party's, 4 the monsters'; turnBrk / turnBrkEnemy (Battle / EnemyBattleBreakEffectMenu)
    // played once over the HUD
    int breakShown = 0;
    float breakShownAt = 0;
    void battleTutorial(const char* function, int flag);
    bool keysUsed = false; // (a command button's focus shown once a key moves it: a touch shows none)
    bool optionsKey(SDL_Keycode k);
    void volumeSet(int bar);
    bool optionsTapped(const TapRect& r, float x);
    void battleKey(SDL_Keycode k);
    void battleClick(float fx, float fy);
    BattleStep battleStep = BattleStep::Over;
    std::unique_ptr<cr3::BattleScript> battleScript;
    float battleScriptClock = 0;
    bool battleScriptOk = true;
    void updateBattleScript(float dt);
    bool battleScriptRunning() const;

    std::vector<std::pair<int, int>> stateArg; // (--state fighter:state)
    int chargeArg = -1;                        // (--charge: the gauge to begin with)
    int gaugeArg = 0;
    void startFight(int mode, uint32_t seed);
    bool dposPosition(const char* name, float* out);
    void placeParty(bool pair);
    void stepBattleShow();

    // the HUD's as the show has it: until an action's hits land, the HP, the break gauge and the
    // charge before it (the windows change with the hit, as the reference video's)
    struct HudValues {
        std::vector<int> hp;
        int gauge = 0, charge = 0;
    } hudBefore;
    bool hudHeld() const;
    int shownHp(int f) const;
    // state 33's revivals still to show, in turn (BtlActionCtrl: ActionReviveStandby - the
    // announcement 0x5d, "Regenerate" -, ActionReviveCamera - Cam_Unit_PCrevival_00 at the one
    // revived -, ActionReviveMain - its motion 8 (bh01), its HP back in green at DummyDP01, eff216 -,
    // ActionReviveWait - the motion's end)
    std::deque<int> regenPending;
    bool regenStarted = false;
    void showRegen();
    int shownGauge() const;
    int shownCharge() const;
    // BtlResult, a battle won: a moment (Battle_End, the music fading: 1.2 s), then "Victory!"
    // (announce 6) with the party standing in its victory motion (bb03) under Cam_Win_*, a tap
    // (or 5 s), then the result window (ResultMenu) - over 60 frames the EXP goes in and the HP
    // back (ProcessLvup; a tap: at once) -, then a tap (or 5 s): its messages in the window, a tap
    // (or 3 s) each (ProcessLvup1 / 2: the levels gained; ProcessResult: CheckSpUp - the sopia's
    // SP, what they learn -, CheckOz, CheckDropItem, CheckDropSopia), and the battle is over
    struct ResultMember {
        int ch = -1, id = 0, level = 1, exp0 = 0, gain = 0, hp0 = 0, hpMax = 1, mp = 0, mpMax = 0;
    };
    struct BattleResultShow {
        int stage = 0; // 0 none, 1 the moment, 2 victory, 3 the window, 4 over
        float start = 0, windowStart = 0, transferStart = 0, transferEnd = 0;
        bool tapped = false;
        int exp = 0, sp = 0, oz = 0;
        ResultMember member[2];
        std::vector<std::string> messages; // (once the EXP is in)
        int message = -1;                  // (the one shown; -1 "Victory!")
        float messageAt = 0;
    } result;
    static constexpr float kTransfer = 2.0f; // (ProcessLvup's 60 frames at 30 a second)
    // the battle's announcements (BtlAnnounceData, message\announcebattle.dat: u32 the records'
    // offset, the texts', the count; a record 4 bytes a language - CR3's 2 (Japanese, English), CR2's 7 / 8)
    // - and the names', the same: message\\charaname, sopianame, commandname, autoskillname,
    // itemname (by number, from 1)
    std::map<std::string, std::vector<uint8_t>> messageFiles;
    std::string announce(int id);
    std::string messageText(const std::string& file, int id);
    static std::string formatAnnounce(std::string t, const std::vector<std::string>& args);
    void finishResult();
    bool announceHeld = false; // (the banner kept: the victory's)

    // The downed, after the command (BtlActionCtrl's dead actions, ActionDeadStart ..): one of
    // the party its motion 7 (bd02), kept, waited for (ActionDeadWaitDeadMotion); a monster with
    // its +0x3a its size's defeat camera and bd02, waited for (ActionDeadWaitBossMotion: 5 s at
    // most), then - unless its +0x38 - eff202 above it (its figure's height,
    // GetEffectDeadOffsetByModelName), fading 1 -> 0 over 1 s after 0.25 s and gone, SE 0x66, then
    // 0.95 s; any other at once - unless its +0x38 - fading over 0.5 s and gone, eff201 above it,
    // SE 0x65, not waited for
    struct Death {
        int f = -1, stage = 0;
        float start = 0;
    };
    std::vector<Death> deaths;
    bool deathsQueued = false;
    void deathEffect(const char* name, int f, BattleUnit& u);
    bool stepDeaths();

    // Analyze (command kind 26; BtlActionCtrl::ActionSopiaWaitEffect -> ActionSopiaAnalyze): its
    // effect over, the one struck's "HP: %d / %d" on the banner until a tap, then its MonsterHelp
    // (the monster's number), until a tap - 5 s each for a fight nobody taps
    struct {
        int stage = 0; // 1 the HP, 2 the help, 3 done
        float start = 0;
        bool tapped = false;
    } analyze;
    bool stepAnalyze();
    void beginResult();
    // a member's EXP, level, next and HP as the window has them now
    struct ResultNow {
        int exp, level, next, percent, hp;
    };
    ResultNow resultNow(const ResultMember& r) const;
    // A battle lost (BtlGameover::Update; BtlMainCtrl::StepBattleOut): the announcements and
    // effects cleared, Cam_Lose_SoloPair_00 looping, the music faded over 20 updates and 18 1.1 s
    // on; the game over's figure (`dead`: BattleGameOverMenu) where it is one - an event battle
    // (+0x18), else once flag 0x7f9 is set - and the banner ("%s was defeated..." alone, 0x5c, else
    // "%s's team was defeated...", 7). A tap: no game over, or no continue data nor save - the end
    // (StepBattleOut: no game over - flag 0x7f8 set, back to the field -, else the title); else
    // faded out over 20 updates, the HUD and the party gone, faded in, 0.5 s on "Would you like to
    // try again?" (0x87) yes / no - yes the continue data, else the save, loaded (+0x1a 1: the
    // field); no the title
    struct GameOverState {
        int stage = 0; // 0 none, 1 the banner (a tap ends it), 5 the fade, 7 the wait, 8 the question
        float start = 0;
        bool over = false, tapped = false, hidden = false;
    } gameOver;
    void beginGameOver();
    void endGameOver(bool title);
    void updateGameOver();
    void gameOverAnswer(bool yes);
    void drawGameOver(const Mat4& view, int w, int h);
    void updateResult();
    void showNumber(int f, int value, int set);
    void beginShowAction(const ShowAction& a);
    void poseBattleShow();

    // the fight's time going on: the next action (or turn), the hits as they land
    std::deque<std::string> announceQueue;
    float announceAt = -10;
    void updateBattleShow();

    // an action's hits landing (the targets' damage motion, the sounds, the hit's effect, the
    // numbers); a special scene's later hits (`first` false) the blows alone
    std::vector<int> groupShown; // (the sides a group's effect has had this action)
    void throwBack(BattleUnit& bu, const cr3::BattleRules::Event& e);
    void landHits(const ShowAction& a, bool first);

    // The special scenes - the awakenings, the advents, the monsters' specials (BtlActionCtrl::
    // ActionAwakeStandby, ActionAdventLaunch .. Cleanup, ActionSpecialCommand; battle/special.hpp):
    // each step once the scene's clock reaches it, in the list's order, the scene over once all
    // have run - its effects in their slots, its cameras loaded and started where it puts them,
    // the fighters hidden, shown, faded and tinted, the screen faded, its sounds; its hits the
    // action's (landHits: the numbers at the first). An advent first has the pair call it (the
    // motion bm01, its launch sound and a voice, 1.25 s - ActionAdventLaunch / LaunchWait) and fade
    // away (1 s, ActionAdventHide / HideWait); the HUD is gone till it is over (SetBattleStatusDisplay)
    struct SceneEffect {
        std::unique_ptr<ViewFigure> fig;
        const cr3::EffectSetting* set = nullptr;
        float start = 0, end = -1; // (battle seconds: hidden before it starts, gone at its end - < 0 none)
        bool shown = true;
        int follows = -1; // (the fighter whose model it has: connect, SetEffectTarget)
        short locator = -1; // (else its setting's: a pair's action's command effect - +0x32)
        // a shot (BtlEffectCtrl::SetShot / ControlShot): from `from` to `to` over `flight` s,
        // straight or falling (thrown up at 2 a second, down to arrive), gone once there
        bool shot = false;
        int path = 0;
        float from[3] = {0, 0, 0}, to[3] = {0, 0, 0}, flight = 0;
        cr3::Mat34 place;
        Ramp alpha;
        bool kept = false; // (BtlEffectCtrl's type 4: no control - there till deleted)
    };
    struct SceneState {
        bool active = false, advent = false;
        bool skippable = true; // (ActionAwake / AdventStandby: on at its start; SetSkipEnable)
        float start = 0; // (battle seconds: the scene's clock's 0)
        std::vector<cr3::SpecialStep> steps;
        std::vector<bool> done;
        int actor = -1, partner = -1, target = -1, hits = 0;
        std::map<int, SceneEffect> effects; // (by slot, +0x46's)
    } scene;
    std::vector<std::pair<std::string, CameraFigure>> sceneCameras; // (LoadSpecialCamera's)
    struct BattleFade {
        int colour = 0; // (0 black, 1 white: Cr3Fade's)
        Ramp alpha{0, 0, 0, 0};
    } battleFade;
    bool battleMapHidden = false;
    Ramp mapColour;
    BattleUnit* unitOf(int f);
    static void unscaled(cr3::Mat34& m);
    bool jointPlace(const ViewFigure& f, const char* joint, cr3::Mat34& out);
    cr3::Mat34 scenePlace(int target, bool apos, bool dpos, int tpos);
    static cr3::Mat34 turnY(float degrees);
    static void setRotation(cr3::Mat34& m, const cr3::Mat34& from);
    bool startScene(const ShowAction& a, const cr3::BattleData::Command& c);
    SceneEffect* sceneEffect(int id);
    void runStep(const cr3::SpecialStep& st);
    void updateScene();
    bool skipScene();
    void endScene();
    bool drawEffect(SceneEffect& fx, const cr3::Mat34& camera, const Mat4& vp);
    void drawSceneEffects(const cr3::Mat34& camera, const Mat4& vp);
    // Defend (BtlCommand::StepGuard, as it's chosen): the one choosing - a pair's both - to its
    // stance bg01 (motion 4), and character 6 its guard, obj303, on its model 0.2 s on
    // (BtlEffectCtrl::CreateEffect(.., 4, hidden, 0.2): ControlWait, then no control - kept); its
    // choice taken back (StepCommandLoop), the stance and obj303 gone. The guard hidden as it
    // attacks (HideAttachObject: a counter), made again 0.5 s after (ShowAttachObject); gone as it
    // falls (ActionDeadStart) and with the turn's effects (BtlTurnEnd: DeleteAll)
    std::unique_ptr<SceneEffect> guardObjects[2];
    bool guardReturn[2] = {false, false};
    void guardObject(int f, float delay);
    void dropGuardObject(int f);
    void setGuard(int f, bool on);
    void updateGuards();
    // A command's effect (BtlActionCtrl::CreateEffect - its settings' delay - and
    // SetCommandEffectPosition with 9): on the fighter `target` - following its model at the
    // setting's locator (a pair's action's +0x32) while the setting follows the position, else
    // put there once; `side` >= 0: a group's (HitGroupEffect), once at that side's middle
    std::vector<SceneEffect> commandEffects;
    void commandEffect(const std::string& name, int target, bool pair, int side = -1);
    void drawBattle(int w, int h, bool mapOnly = false);
    void glassSize(int& hw, int& hh) const;
    void grabGlass(int fw, int fh);
    void drawGlass();

    // The battle's effects (BtlEffectCtrl / BtlEffect: an effNNN figure playing its animation once,
    // where it was made - turned to the camera, its quads facing it)
    struct BattleEffect {
        std::unique_ptr<ViewFigure> fig;
        float start = 0, length = 0;
        float at[3] = {0, 0, 0};
    };
    std::vector<BattleEffect> battleEffects;
    void spawnEffect(const std::string& name, const float at[3]);
    void drawEffects(const cr3::Mat34& camera, const Mat4& vp);

    // The event (--event): its runtime, the vista's map figure, a figure for each of the scene's
    // models (in its order)
    std::unique_ptr<cr3::EventRuntime> event;
    bool eventMode = false;
    ViewFigure vistaMap;
    std::string eventScript; // (CR3: the area script the event is in - the next vista's, too)
    std::string vistaMapName; // (the vista's map: "d0101f" - the field it returns to)
    std::string eventScriptOf(const std::string& fn) const;
    ViewFigure secondMap;      // (VistaEvent.LoadSecondMap's)
    std::string secondMapName;
    float vistaMapTime = 0; // (its own animation's time: held while SetMapAnimPause says)
    std::vector<ViewFigure> eventFigures;
    std::vector<ViewFigure> eventHeads; // (a parts model's head - CrxModel::Build's <name>h01 - by its body's index; empty: none)
    std::vector<std::string> eventFigureNames; // (what eventFigures were made for)
    std::vector<int> eventModelArmors;         // (EvtScene.ChangeArmor override per model)
    struct EventAttachmentView {
        std::string figure, variant;
        ViewFigure fig;
    };
    std::vector<std::map<int, EventAttachmentView>> eventAttachments;
    float eventClock = 0;
    float fadeColours[4][3] = {{0, 0, 0}, {1, 1, 1}, {1, 1, 1}, {0, 0, 0}};
    cr3::Texture white;
    bool eventEnded = false, messageLogged = false;
    bool startEvent(const std::string& function, std::string map, float page);

    bool eventDrawingReady = false;
    void initEventDrawing();

    // CR3's field (--area <area> [--player <model>]: field/cr3field.hpp) - the area's figures,
    // the player walking its floors as Fld2TaskPlayer does, the camera following (Fld2CameraGom)
    bool areaMode = false;
    cr3::Cr3Area area;
    std::vector<ViewFigure> areaFigures;
    ViewFigure areaPlayer, areaPlayerHead; // (a parts model's head: CrxModel::Build's <name>h01)
    std::string areaPlayerName, areaPlayerArgName;
    std::unique_ptr<cr3::CharacterController> areaWalker;
    cr3::Cr3GomCamera areaCamera;
    float areaYaw = 0, areaClock = 0, areaMotionAge = 0;
    cr3::Cr3Motion areaMotion = cr3::Cr3Motion::Idle;
    bool areaMotionSet = false;
    // the area's script (Fld2Main::StartScript: its evt000 as the area is entered - d01's for
    // d0101 -, stepped with the game's updates)
    std::unique_ptr<cr3::EventRuntime> areaEvents;
    float areaEventClock = 0;
    // the area's gimmicks (Fld2TaskGimmick::SetGimmick: <area>p's joints ragdoll_<kind>..): event
    // boxes (evt..: Fld2Event - a box the joint's scale x 0.05 each way; walked into, its function
    // runs) and check objects (cobj..: Fld2CheckObject - within it, a tap runs its function: the
    // doors' DoorJump)
    struct AreaGimmick {
        std::string function;
        bool eventBox = false;
        cr3::Mat34 world;
        bool inside = false;
        std::string areaChange; // (Fld2AreaChange - ragdoll_ac_<pop point>: walked into, that area at that point)
        bool shaped = false;    // (its own body in <area>p.phys - Fld2AreaChange::Create's trigger: its shape's bounds)
        cr3::Vec3 boxAt{}, boxLo{}, boxHi{};
        std::array<float, 4> boxTurn{0, 0, 0, 1};
        int box = -1;          // (a drawn treasure / gold box: its GameState::cr3Boxes index - its function OpenTBox / OpenGBox)
        std::string boxName;   // ("tbox_00")
        float checkRange = 0;  // (Field row +0x34 when > 0: custom check distance)
    };
    std::vector<AreaGimmick> areaGimmicks;
    // the area's camera (Fld2TaskCamera::SetupCamera by the map table's type - the executable's
    // 12-byte records from "t0101": 0 rail, 1 Gom, 2 ortho, 3 look-down, 4 script, 5 toy): the
    // look-down one's offset by FieldCameraLD (data\fieldcamerald.mbe: the dungeon - a town's tNN
    // N - 1 -, the area from 1, the offset) - the rest Gom for now
    static constexpr const char* kLookDownMaps[] = {"t0102", "t0104", "t0105", "t0110", "t0111", "t0113", "t0114", "t0115", "t0116", "t0117",
                                                   "t0118", "t0119", "t0120", "t0203", "t0204", "t0207", "t0208", "t0210", "t0211", "t0402",
                                                   "d0711", "d1312", "d1501", "d1502", "d1506", "d1507", "d1508", "d1509"};
    // Fld2CameraRail (type 0: the village, Sky Front - 23 maps): <area>_cam's camera groups - each
    // cameraN and cameraN_aim joint sampled at every frame of its animation (SetupCamera: frames
    // its length x 30 less 0.1) -, its cam_boxAA_BB boxes (0.05 of the joint's scale each way, the
    // player's sphere entering one: group AA - CallbackCollisionEvent, a 0.3 s blend from where the
    // eye was), the look-at point the player plus the area's FieldCameraLD offset. Each update
    // (SearchBestPath2) the aim path's pair of points nearest it (their two distances summed) and
    // the nearest point between them give a frame; the eye is the camera path there (RailAction),
    // eased 2.5 frames an update when that would jump it more than 1. The group to begin with is
    // the one whose aim point is nearest the player (StartCameraGroup)
    static constexpr const char* kRailMaps[] = {"t0101", "t0106", "t0107", "t0108", "t0112", "t0121", "t0122", "t0201",
                                               "t0202", "t0205", "t0206", "t0209", "t0301", "t0302", "t0303", "t0401",
                                               "t0501", "t0502", "d1503", "d1504", "d1510", "d1511", "d1512"};
    struct Cr3RailCam {
        bool on = false, started = false;
        std::vector<std::vector<cr3::Vec3>> aim, cam;
        struct Box {
            cr3::Mat34 world;
            int group = 0;
            bool in = false;
        };
        std::vector<Box> boxes;
        int group = 0;
        float best = 0, shown = 0, blend = 0, tick = 0;
        bool easing = false, switching = false;
        cr3::Vec3 from{}, eye{};
        std::array<float, 3> offset{};
    } rail;
    std::array<float, 3> fieldCameraLD(const std::string& name, bool& found);
    bool setupRail(const std::string& name);
    static float railLen(const cr3::Vec3& a, const cr3::Vec3& b);
    cr3::Vec3 railCamAt(float f) const;
    void railSearch(const cr3::Vec3& target);
    void railReset(const cr3::Vec3& feet);
    bool railBoxHolds(const cr3::Mat34& w, const cr3::Vec3& p) const;
    void railStep(const cr3::Vec3& feet, const cr3::Vec3& centre, float dt);
    void setupAreaCamera(const std::string& name);
    // the field's effects (GameState::fieldEffects - Fld2Effect: its model, a CrxSimpleModel, at its
    // target's place - Fld2TaskGimmick::GetPositionFromFigure, the gimmick's joint; "player" the
    // player's - + the offset, scaled; its animation looped, or once and then gone)
    std::map<std::string, ViewFigure> fieldEffectFigs;
    bool fieldTargetAt(const std::string& name, cr3::Vec3& out);
    std::map<std::string, cr3::Vec3> areaJoints;
    std::string areaJointsOf;
    // an effect's particles (its model's .sprk - fe201am10, the lab's rain: assets/spark.hpp), by
    // the effect's serial; the quads drawn from a buffer of its own a material
    std::map<std::string, std::unique_ptr<cr3::SparkAsset>> sparkAssets; // (null: none)
    struct ViewSpark {
        std::unique_ptr<cr3::SparkEffect> fx;
        std::map<const cr3::SparkMaterial*, std::pair<cr3::GpuMesh, size_t>> meshes; // (its quads' room)
    };
    std::map<uint32_t, ViewSpark> fieldSparks;
    const cr3::SparkAsset* sparkAsset(const std::string& model);
    void dropSpark(ViewSpark& v);
    void stepFieldEffects(float dt);
    void drawFieldEffects(const float* vp, const cr3::Mat34& camera);
    // a Spark effect's sprites (SparkSprite::DrawSprite): its material's texture times DiffuseColor
    // (the shader 08810000_00000111_00070300's), blended as the material says (SparkUtils::
    // SetupAssetRuntimeShader: +0x186 0 SRC_ALPHA ONE, 1 the same subtracted, 2 SRC_ALPHA
    // ONE_MINUS_SRC_ALPHA), tested against the depth, not written to it, both faces
    std::vector<cr3::SparkEffect::Quad> sparkQuads;
    void drawSpark(ViewSpark& v, const cr3::Mat34& root, const cr3::Mat34& camera, const float* vp);
    // the walls: the area's own (<area>c.phys) and the solid gimmicks standing (their <area>p.phys
    // bodies - Fld2Gimmick's rigid bodies: a broken rock's, a hidden one's gone)
    cr3::CollisionWorld areaBaseWalls;
    cr3::PhysScene areaPhysP;
    std::string areaWallKey, areaPrefix;
    // the moving objects (Fld2MovingObject::SetMove / UpdatePosRot): SetMove's end frame / 30 reached
    // at end / frames a second from 0, <area>p posed at that time, the object on its ragdoll joint
    // (its model kept its first offset from it)
    ViewFigure areaP; // (Fld2TaskGimmick +0xe0: <area>p, its animation)
    struct AreaNpc;
    void placeMover(AreaNpc& npc);
    void stepAreaMovers(float dt);
    void stepStepSwitches();
    void updateGimmickWalls();
    // The bomb (Fld2BombManager, Fld2Bomb - the goods button's first): three at most, each put at the
    // player's feet (Put; SE fe_011, the fuse fe_012 looped while any burns) as fe002bom01 - its fuse,
    // played once; at its end or 1.5 s (or a blast's chain: 0.4 s a bomb gone off) it goes off
    // (fe002bom02 there, turned at random; SE fe_013), its blast on for its first 15 frames
    // (CheckBlastObject: a rock within its radius of it breaks; CheckBlastChain: a bomb put within 1
    // goes off after the chain's count x 0.4 s), then gone at its animation's end
    struct FieldBomb {
        bool on = false, blasting = false, chained = false, fresh = false; // (fresh: its first update gone off)
        int stage = 0; // 1 the fuse, 2 gone off (blasting), 3 dying away
        float t = 0, chainT = 0, chainDelay = 0, yaw = 0;
        cr3::Vec3 pos{};
    };
    std::array<FieldBomb, 3> fieldBombs;
    std::vector<std::string> fieldPendingFunctions; // (gimmicks' functions set off while another ran)
    // (utils::StartQuake - the root's QuakeControlTask, the field's, a battle's and a vista's alike:
    // the 3D layers 5..9 and 0 moved up and down by width x (from .. to over the time) - seconds
    // = frames / 30, below 0 held at `to` until StopQuake -, the way flipped at each 60-Hz update
    // after a drawn frame)
    struct FieldQuake {
        bool on = false, drawn = false;
        float width = 0, from = 0, to = 0, time = 0, seconds = 0, sinceUpdate = 0;
        int parity = 0;
    } fieldQuake;
    void startQuake(float width, float from, float to, float frames);
    void stepQuake(float dt);
    float fieldAnimDuration(const std::string& model);
    void putBomb();
    static float bombDist(const cr3::Vec3& a, const cr3::Vec3& b);
    void stepFieldBombs(float dt);
    void drawFieldBombs(const float* vp);
    // the area's people (Fld2NPC): data\field_<town>.mbe's Field rows for this area - the area +0,
    // the quest +0x10 (-1: any; GetFieldParamByName takes those first), the gimmick +0x18
    // (ragdoll_c121_00), the model +0x20 (c121b: c121 with its b textures - CrxModel::
    // ChangePartTextureVariation) - placed by the gimmick's own joint (c121_00: GetPosition /
    // Rotation / ScaleFromFigure), its first motion looped (Fld2NPCAnimeNameTbl[0]: fn01), started
    // apart; hidden by Cr3GimmickData.Disp
    struct AreaNpc {
        std::string name, model;
        ViewFigure body, head;
        cr3::Mat34 place;
        float phase = 0;
        bool box = false;               // (a Fld2Box: f0000box00 ..)
        float openAt = -1, fade = 1;    // (BoxOpen: its _a01 from then, held; DeleteBox: 0.1 less each update)
        bool solid = false;             // (the Field row's +0x2c: a rigid body - its <area>p.phys body, named as it)
        bool rock = false;              // (Fld2Rock: ragdoll_krock_..)
        bool bombSwitch = false;        // (Fld2PhysicalSwitch: ragdoll_bswc_.. - a blast near turns it, its other model <model less 2>03)
        bool stepSwitch = false;        // (Fld2StepSwitch: ragdoll_fswc_.. - stood on, it goes down - its _n01 -, then its function)
        bool eventMotion = false;       // (Cr3GimmickData.SetMotion: its own animation, from motionAt)
        float turnFrom = 0, turnTo = 0;  // (Cr3GimmickData.ChangeDirection - Fld2DirectionManager: its yaw from, to)
        int wander = 0;                 // (the Field row's +0x30: 4 / 5 walks / runs about its rectangle)
        cr3::Mat34 rect;                // (ragdoll_npc_<name>: its place and turn, the sides apart)
        float rectW = 0, rectD = 0, talkRange = 0, walkSpeed = 0, wait = 0, walkTime = 0;
        bool walking = false, held = false; // (held: NPCMove false)
        bool idle2 = false, idle2On = true; // (Fld2NPCIdle2Name's model - +0x1e8 -; NPCIdle2Enable - +0x2a4)
        int idle2State = 0;             // (+0x1e4: 0 fn01, 1 fn02 once)
        float idle2Left = 0;            // (+0x1e0)
        std::array<float, 3> target{};
        int turnStep = 0, turnFrames = 0; // (updates done, of how many)
        float motionAt = 0;
        int stepState = 0;              // 0 up, 1 going down, 2 down (done: once)
        float stepAt = 0;
        bool mover = false;             // (Fld2MovingObject: ragdoll_fmove_.. - SetMove plays <area>p's animation, it follows its ragdoll joint)
        bool moving = false;
        float moveTime = 0, moveEnd = 0, moveSpeed = 0;
        cr3::Mat34 ragdoll;             // (the ragdoll joint now - its rigid box is made with collision off: Fld2MovingObject::Create)
        ViewFigure alt;
        float blastRadius = 0;          // (+0x34: a bomb's blast this near breaks it - CheckBlastObject)
        bool broken = false;
        float brokenAt = 0;
        // its chips flying (Fld2Rock::RockChip: ten of <model less 2>01, twenty smaller of ..02 -
        // each its place, speed, turn and spin; 0.98 down an update, a bounce off the ground at 0.7;
        // faded out by 1.2 s)
        struct Chip {
            bool big = false, bounced = false;
            cr3::Vec3 pos{}, vel{};
            float rx = 0, ry = 0, vrx = 0, vry = 0, scale = 1;
        };
        std::vector<Chip> chips;
    };
    std::vector<AreaNpc> areaNpcs;
    bool cr3CampWasOpen = false;
    ViewFigure boxGet;                  // (fe101get01: the light from an opened box - Fld2Box +0x170)
    bool boxGetTried = false;
    void setupAreaGimmicks(const std::string& name);
    void stepAreaBox(AreaNpc& npc, float dt);
    void loadAreaNpcs(const std::string& name, const std::vector<std::string>& joints, const std::vector<cr3::Mat34>& world);
    // The area's own natives (Fld2's, over the 3D map):
    // - Cr3FieldMapCamera.SetScriptCamera(eye xyz, target xyz, frames) (Fld2SetFieldScriptCamera,
    //   Fld2CameraScript::SetupCamera / Update): the camera's eye and target moved in a straight line
    //   from where they are to there over frames / 30 s (at once for 0); SetScriptCameraPosition /
    //   Target (x, y, z, frames) the one, the other kept; ResetScriptCamera: the field's own again
    // - Cr3System.StartQuake(width, from, to, frames) / StopQuake (utils::StartQuake -
    //   QuakeControlTask): the picture shaken up and down (fieldQuake)
    // - Cr3GimmickData.SetMotion(name, motion) (Fld2SetGimmickMotion): a person's animation by its
    //   name (Fld2NPC::SetEventAnim), a block's or check object's <model>_<motion> (Fld2Block /
    //   Fld2CheckObject::SetMotion) - looped from its start
    // - Cr3GimmickData.SetMapScrollSpeed(material, u, v) (Fld2TaskMap::SetScrollSpeed): the map's
    //   material's ScrollSpeedSet1 (0x55)
    struct AreaScriptCam {
        bool on = false;
        std::array<float, 3> eye{}, at{}, toEye{}, toAt{};
        float left = 0;
    } areaScriptCam;
    std::map<std::string, std::string> areaMotions; // (asked, not yet put on)
    void areaScriptCamera(const std::array<float, 3>& eye, const std::array<float, 3>& at, float frames);
    void stepAreaScriptCam(float dt);
    std::map<int, int> bonusMpBefore; // (RecoverBonusMPPreparation's: each member's most MP)
    void bindQuakeNatives(cr3::ScriptVM& vm);
    void applyQuake(Mat4& proj);
    void bindAreaNatives(cr3::ScriptVM& vm);
    // the field and camp natives an area's events and a vista's share (area: the field mode's -
    // Fld2Main's mode type 1 -, where Fld2* natives act; a vista's mode, VistaMain's 0xe, leaves them be)
    void bindCr3FieldUiNatives(cr3::ScriptVM& vm, bool area);
    // (Fld2ChangePlayerEquip 0x00870038 -> Fld2TaskPlayer::ChangePlayer: the field's leader - the
    // party's first - built again in its armour's body where it stands)
    void cr3RebuildLeader();
    std::string cr3LeaderBody() const;
    bool areaPlayerShown = true; // (Fld2SetPlayerVisible 0x0086d4e0: Fld2TaskPlayer's flag 0x400; ChangeArea sets it)
    // InterfaceWindow.SetCampWindowParty (InterfaceMain::SetCampWindowParty 0x008f00dc): the camp's
    // top over an event - CampTopMenu +0xf5 -, the scripts waiting while it's open
    // (CheckAliveCampWindowParty); LockCampTopCharacterPanel(slot) / UnLock (InterfaceMain +0x288 ..
    // +0x28c, not saved): a party slot's panel kept where it is - SetPanelLock: grey (SetSelectStatus 3,
    // 0.5 0.5 0.5), not taken up (CheckTopArea), not swapped with (TouchSimpleMove)
    bool cr3CampEvent = false;
    std::string campTopTold; // (the camp top's alert - 0x307: a member without a gene, in the VS mode)
    float campTopToldAt = 0;
    std::array<bool, 5> cr3PanelLock{};
    bool cr3PanelLocked(int member) const; // (a panel - the party's members in order - its slot's lock)
    // An event's gene windows (round 9). InterfaceWindow.CreateGeneStatusSCModalMenu(gene, level, skills)
    // (InterfaceMain::CreateGeneStatusSCModalMenu 0x8dbee4: GeneStatusMenu 0xa7 - CrxGeneManager::GetGene
    // at that level, geneCngStsMenu's status and its card -; a tap closes it, the skill slots' help on a
    // tap when skills is true) and AliveGeneStatusMenu (a Func0: while it's there). CreateDisposalEquipMenu_Gene
    // (CreateDisposalMain(0x14) 0x8f2688: DisposalEquipMain 0xc5) with GetDisposalEquipMenuResult
    // (InterfaceMain +0x290: 0 running, 8 done - no cancel): the box not full (IsGeneFull), done at
    // once; full, told 0x218 ({categoryname} 0x303), a held gene picked (CreateBaseGeneSelectMenu),
    // asked 0x236, DBSystem::RemoveGene, told 0x237 - then done. Before a story's or a quest's
    // CardGet_Msg, so its GeneGet finds room
    bool scriptGeneStatus = false;
    struct ScriptGeneDisposal {
        int stage = 0;    // 0 none, 1 told 0x218, 2 the genes, 3 told 0x237
        int result = 0;   // (GetDisposalEquipMenuResult)
        int focus = 0, asked = -1;
        float since = 0;
        std::string told;
        std::vector<int> genes; // (cr3Genes indices: the box's, the stock not)
    } geneDisposal;
    void openScriptGeneStatus(int master, int level);
    void openScriptGeneDisposal();
    void drawCr3ScriptGenes(int w, int h);
    bool scriptGeneClick(float fx, float fy);
    bool scriptGeneKey(SDL_Keycode key);
    void chooseScriptGene(int id);
    // the motions asked for, put on their people and objects
    // the people turning (Fld2DirectionManager::Update: an update's share - 20 a second - of the
    // turn, the shorter way round)
    float areaTurnClock = 0;
    void stepAreaTurns(float dt);
    bool setNpcAnim(AreaNpc& npc, const std::string& motion);
    bool areaPassJoint(AreaNpc& npc, float t, std::array<float, 3>& at);
    void stepAreaPassers(float dt, const cr3::Vec3& pp);
    void stepAreaIdle2(float dt);
    void stepAreaWanderers(float dt);
    void stepAreaMotions();
    int areaCheckNear = -1; // (the check object the player is within: a tap's)
    // where the player was as the field was left for a vista (Fld2SaveData), to stand there again
    std::string savedArea;
    std::array<float, 3> savedPos{};
    float savedYaw = 0;
    bool loadArea(std::string name, const std::string& player);
    void setAreaMotion(cr3::Cr3Motion m);
    bool gimmickHolds(const AreaGimmick& g) const;
    bool enterPopPoint(const std::string& pop);

    // the area's script calling a gimmick's function (Fld2TaskGimmick::SetNextEvent -> Fld2Main::StartScript)
    // ---- the dungeon's treasure and gold boxes (Fld2TBoxList) ----
    // Drawn as a dungeon is entered (Fld2TBoxList::Drawing(dungeon, level)): the dungeon's
    // ragdoll_tbox rows (field_<d>.mbe's Field table, by area) shuffled - a hundred swaps -, and the
    // first fourteen given TBoxTable's kinds for the dungeon and its level number (level x 10 + 1;
    // a row whose flag is -1 or set); each kind's content from TBoxItemTable (its weight; a unique
    // one not twice in a draw) - an item by its category (0 .. 6, 10) and id, or 8 a box of monsters
    // (the coupling). Gold boxes: one more for each GBOX_PROBS chance met (rand x 100 at or under
    // it), the dungeon's ragdoll_gbox rows in order - their prize drawn from its item sheet as one is
    // opened (OpenGBoxRequest). A box not drawn isn't there
    int lastAreaDungeon = -1;
    cr3::Rand48 boxRng;
    int drawnBox(const std::string& joint) const;
    void drawCr3Boxes(const std::string& areaName);
    void runAreaFunction(const std::string& fn);
    void afterAreaScript();

    // the random encounters (Fld2TaskEncount): the area's row of enc.mbe - its battle field, music,
    // steps between rolls and the chance to start with - and the couplings of mon_enc.mbe that
    // can be met in it (Setup: the dungeon's, within the level's band, their event flag set, and
    // weighed for this area above 0)
    struct AreaEncounters {
        struct Coupling {
            int weight = 0, index = 0, id = 0, playerFirst = 0, enemyFirst = 0;
        };
        std::vector<Coupling> couplings;
        // (ace_coupling.mbe AceCoupling, 56-byte rows: ints the id, -, -, the coupling, its level by
        // CalcEnemyLevel (else the next int's), the level, its level-up step and cap (none used), the
        // band's base and max levels, an event flag (-1 none), -, the chance %; shorts the first strikes)
        struct Ace {
            int id = 0, coupling = 0, calcLevel = 0, level = 0, stepLv = 0, capLv = 0, base = 0, max = -1, flag = -1, chance = 0, playerFirst = 0, enemyFirst = 0;
        };
        std::vector<Ace> aces;
        int dungeon = 0;
        int total = 0, field = -1, bgm = -1, steps = 0, rate = 0, counted = 0;
        float walked = 0, last[2] = {0, 0};
        bool lastSet = false;
    } areaEnc;
    int activeAceId = -1;
    cr3::Rand48 encountRng;
    int encountDifficulty() const;
    void setupAreaEncounters(const std::string& name);
    int pickEncounter();
    int drawAce();
    int encounterLevel();
    void startAceEncounter(const AreaEncounters::Ace& a);
    void startEncounter(const AreaEncounters::Coupling& met, int level = -1);
    bool forceEncounter();
    int encounterUp();
    void countEncounter();

    // one frame: the stick in the camera's frame (FldPlayer::CalcPlayerMoveDir), a run or a walk
    // (SM_Run 4.8, SM_Walk 1.2 a second, as far as it's pushed), the camera's Q / E and R / V
    float areaScriptRun = 0;     // (seconds the area's script has run)
    bool areaScriptOver = false; // (the area's evt000 over as it started - its BranchAfterEvent still to come)
    void updateArea(float dt, float sx, float sy, bool run);
    void drawArea(int w, int h);

    // CR3's battle (--battle <coupling> [--battle-field <n>], an area's BattleIn: battle/cr3battle.hpp)
    // - the field's figure, the units where BtlUnitList::Align puts them in their stance (bb01,
    // looped: BtlUnit::PlayStayAnimation), the camera BtlCameraSystem plays: BtlSysStateStart's
    // battle-in - the get-set camera of the largest unit's size, from Cam_bt (or a figure of its
    // own) over its end time -, held at its end (PlayGetSet: SeekEnd), then the command camera on
    // the party's first (PlayCommandSelect: Cam_bt_command_PC_00 looped over 26.67 s, riding the
    // unit - its position, its turn - and the picture shifted a third of the screen's half width
    // left: BtlCamera::SetScreenOffsetX(-width / 3))
    bool cr3BattleMode = false;
    cr3::Cr3BattleTables cr3BattleTables;
    bool cr3BattleTablesLoaded = false;
    void cr3PartyRules();
    int cr3CharaMp(int ch, int most) const;
    bool cr3CharaStats(int ch, cr3::Cr3Fighter& f);
    bool cr3TablesReady();
    std::string cr3PartsBody(const std::string& model);
    cr3::Cr3Battle cr3Battle;
    ViewFigure cr3BattleField;
    struct Cr3BattleUnitView {
        ViewFigure body, head, weapon; // (weapon: a character's - CrxModel::LoadWeapon -, on the body's DummyWP)
        float phase = 0; // (BtlUnitList::Standby: the monsters' stances started apart)
        // its motion (BtlUnit::PlayAnimation: an AnimCommon suffix - bb01 its stance, looped),
        // from when; a dedicated knockout motion is held at its end.
        std::string motion = "bb01";
        float motionStart = 0;
        bool motionLoop = true, motionHold = false;
        bool knockedOut = false;
        bool scriptHidden = false; // (BattleUnit.Hide - BtlUnit::DisableDraw: undrawn at once, for the battle's rest)
        Ramp opacity;
        cr3::Vec3 meleeOffset{};
        Ramp meleeStep{0, 0}, meleeTurn{0, 0};
    };
    std::vector<Cr3BattleUnitView> cr3BattleUnits;
    cr3::Cr3BattleCamera cr3BattleCam, cr3BattleOwnCam; // (Cam_bt; a camera figure of its own: Cam_human_getset ...)
    // BtlSysStateStart's battle-in and get-set; BtlSysStateSelect's command and target selection;
    // BtlSysStateAction's actions; the end (won: Battle_Victory_Base, then on; lost)
    enum class Cr3BattleStage { In, GetSet, Menu, Command, List, Target, Escape, Action, Won, Result, Lost };
    // its script (BtlScriptSystem: battle_NNNN run through the event system - Battle_Boot_Base as
    // it boots, Battle_Start_Base once the get-set camera holds: the tutorials)
    std::unique_ptr<cr3::EventRuntime> cr3BattleEvents;
    float cr3BattleEventClock = 0;
    bool cr3BattleStarted = false; // (Battle_Start_Base called)
    // (BattleArgument: the action a direction's end is about, the unit a monster's script is for)
    int cr3ArgAction = -1, cr3ArgSource = -1;
    bool runCr3BattleScript(const char* fn, bool quiet = false);
    Cr3BattleStage cr3BattleStage = Cr3BattleStage::In;
    float cr3BattleClock = 0;
    struct Cr3BattleCamPlay {
        std::string name;
        float start = 0, end = 0; // (the motion's clock: from start, its end time)
        bool loop = false, held = false;
        std::array<float, 3> actor{}; // (BtlCameraActor: where it rides, turned by yaw)
        float actorYaw = 0, screenShift = 0; // (the shift in NDC: -1/3 for the command camera)
        float scale = 1;
        // (BtlCameraActor::SetTarget: the view turned toward a point by this much - a slerp of
        // the motion's rotation toward facing it; 0 none)
        std::array<float, 3> lookAt{};
        float lookWeight = 0;
    } cr3Cam;
    bool cr3ScriptCamera = false; // (a battle script's BattleCamera.Play since its event started: the victory's own camera not put over it)
    // (BtlUnit +0x238 / +0x240: the unit a unit holds and on which of its effect points -
    // BtlDamageTarget::Hold, BattleUnit.Attach -, by the holder's fighter; BtlUnit::Detach the
    // release - BtlStatus::PlayRelease)
    std::map<int, std::pair<int, int>> cr3Held;
    // (BtlDirectionManager::Insert: a direction of no command after the current one - its kinds,
    // DirectionType 0x27 BtlReleaseDirection, 0x28 BtlWaitDieDirection - as actions' commands)
    static constexpr int kCr3ReleaseDirection = -0x27, kCr3WaitDieDirection = -0x28;
    void releaseCr3Hold(int holder);
    // (BattleCommand.SetStart: a command the battle opens with - phaseGetSetStart; -1 none)
    std::array<int, 2> cr3StartCommand{-1, -1};
    // (Battle.CloseUI / OpenUI: BtlBreakGaugeUI and BtlStatusUI closed)
    bool cr3BattleUiClosed = false;
    // (BtlField's animator: Battle.ResetFieldAnimatorLooping - its loop off -, SetFieldAnimatorEndTime
    // - held at that time; the field objects - Battle.AddFieldObject: a figure of its own, its own
    // animation looped, on one of the field's joints)
    bool cr3FieldLoop = true;
    float cr3FieldEnd = -1, cr3FieldTime = 0, cr3FieldDrawnAt = 0;
    struct Cr3FieldObject {
        ViewFigure figure;
        std::string joint;
    };
    std::vector<Cr3FieldObject> cr3FieldObjects;
    // (BtlScript::BattleModel: a script's own models by slot - Create, PlayAnimation looped,
    // SetPosition, SetRotation in degrees)
    struct Cr3ScriptModel {
        ViewFigure figure;
        std::array<float, 3> at{}, rotation{};
    };
    std::map<int, Cr3ScriptModel> cr3ScriptModels;
    void playCr3Camera(const std::string& name, float end, bool loop, const std::array<float, 3>& actor, float yaw, float shift);

    // The screen breaking into the battle (GameMain::SetGlassEffect at the reservation: the next
    // frame caught - PostEffectGlassFragment's capture - and held; StartGlassEffect as the battle
    // starts: the shards, SE 0x253): a battle asked for is started by the next draw, after it
    // has drawn and caught the frame as it was
    struct {
        bool active = false;
        int coupling = -1, field = -1;
    } cr3BattleRequest;
    cr3::Cr3Glass cr3Glass;
    cr3::Rand48 cr3GlassRng;
    cr3::Texture cr3GlassTexture;
    float cr3GlassStart = 0;
    void requestCr3Battle(int coupling, int fieldId);
    void startRequestedCr3Battle(int fw, int fh);
    void drawCr3Glass();

    // BtlSystem::Init's start mode: the party's first strike (1) when rand() % 100 is under the boot
    // param's chance, else the enemy's surprise attack (2) under its own (the skills' corrections -
    // GetFirstStrikeChanceCorrection / GetSurppriseAttackChanceCorrection - not in yet); then
    // BtlSysStateStart::phaseStart: the break gauge +-500 and the announcement (AnnounceBattle 1 -
    // 2 a party of one - the leader's name for its {source}; 3 the surprise) for 2.5 s. CR3_START_MODE sets it:
    // tests
    int cr3StartMode = 0;
    struct Cr3Announce {
        std::string text;
        float at = 0, seconds = 0;
    } cr3Announce;
    void setCr3Announce(const std::string& text, float seconds);
    std::string cr3FighterName(const cr3::Cr3Fighter& f);
    void announceCr3Effects(const cr3::Cr3Action& a);
    // a monster's drop (BtlMonsterStatus::vDrop, once, as it falls): mon_para Monster +0x70 and +0x7c
    // - (kind, id, chance) each; rand() % 256 under the first's chance its first, under both its
    // second, else none (a seasonal one - kind 8 - only in its event: none here). Announced
    // (vAnnounceDrop: AnnounceBattle 0x1d, its "{item.name}", com_013) and kept for the result
    // (VAcquireDrop - MbDropInfo::AddToSaveData: one of it)
    std::map<int, int> cr3Drops; // (fighter: the item's full id)
    void rollCr3Drop(int fi);
    // the continue (BtlSysStateAction, after the defeat script, where the coupling allows it -
    // ContinueMenu: btCtnPositon, its OneHelpMenu - AnnounceMenuName 0xbe -, btCtnBtnMenu's two
    // contBtns at call_btCtnBtnPanel_001 / 002 - give up (background 8, icon and text 12), continue
    // for a jem (background 1, 13, its price btCtnbuyNum_fr00 frame 0) -, btCtnJemCoinMenu with the
    // jems held (item 400006 - CreateContinueMenu's count -, btCtnNumJ_num00's two btCtnNum_fr00).
    // Give up asks AnnounceBattle 0x2a, a jem 0x2b (none: AnnounceMenuName 0x132's alert, "{jem}"
    // the count held); yes to it pays one (PaymentJem) and phaseContinue: the party revived whole, HP
    // and MP - command 59, all of the player's side, 100 -, the gauge even, the battle's music, the
    // next turn. The menu's buttons close while it asks.
    // Music 0x2f while it's up)
    // the game over (GameOverMenu: btGameOverMenu at btGameOverPositon's call_btGameOverMenu_fit0,
    // its own animation to its end - "GAME OVER" letter by letter -, then BtlSysStateAction's
    // FadeOut(0.5) and the field)
    float cr3GameOverAt = -1;
    float cr3GameOverLength();
    void drawCr3GameOver(int w, int h);
    struct Cr3Continue {
        int stage = 0; // 1 the menu, 2 asked to give up, 3 asked to pay, 4 told there's no jem
        float since = 0;
        int focus = 0;
        int askFocus = 0; // (the question's: 0 Yes, 1 No)
        bool decided = false;
    } cr3Ctn;
    static constexpr int kCtnGiveUp = 900, kCtnJem = 901, kCtnYes = 902, kCtnNo = 903, kCtnOk = 904;
    static constexpr int kJemItem = 400006;
    int cr3Jems() const;
    void openCr3Continue();
    // --input auto / manual (tests): the battles from then on fought alone - each command an
    // attack on the first monster alive, the result taken - where the input has no choice of its own
    bool autoBattle = false, autoGene = false, testPartyUnhurt = false;
    int autoGeneSkip = -1; // (auto:gene - the chooser with no gene command to use this turn)
    float wanderLeft = 0, wanderTurn = 0; // (--input wander:<seconds>)
    float inputWait = 0;                  // (--input wait:<seconds>)
    void takeAutoBattleToken();
    static bool isCr3BattleToken(const std::string& t);
    void stepCr3Continue();
    bool cr3ContinueKey(SDL_Keycode k);
    void chooseCr3Continue(int id);
    void continueCr3Battle();
    void drawCr3Continue(int w, int h);
    void rollCr3StartMode();
    void drawCr3Announce(const Mat4& view, int w, int h);
    std::vector<int> cr3BattlePartyIds() const;
    void loadCr3BattleUnits(size_t from);
    // the next wave (BattleCoupling.SetId in a victory script - BtlSysStateAction::phaseVictoryEvent's
    // end: AcquireRemuneration, then reinforceByCoupling - BtlSystem::CreateMonster: the monsters
    // gone, the coupling's in their place -, the coupling's script loaded (not booted), the units
    // aligned, the camera's size, FadeInNonPlayers; then phaseCallTurnEndEvent - its Battle_Turn_End
    // - and the next turn. The party as it stands - HP, MP, status effects -, the gauge, the turn)
    int cr3NextCoupling = -1;
    void reinforceCr3Battle(int coupling);
    bool startCr3Battle(int coupling, int fieldId);
    void updateCr3Battle(float dt);

    // ---- the turn (BtlSysStateSelect / BtlSysStateAction, battle/cr3rules.hpp) ----
    cr3::Cr3BattleRules cr3Rules;
    std::vector<int> cr3HpShown;            // (each fighter's HP as shown: the hits land one by one)
    std::vector<int> cr3MpShown;            // (each fighter's MP as shown: costs and MP heals/drains)
    std::vector<int> cr3ComboStartHp;       // (BtlTotalDamageUI: enemy HP at start of player combo chain)
    std::vector<int> cr3ComboDamage;        // (BtlTotalDamageUI: total damage across consecutive player actions)
    std::vector<int> cr3ComboHits;          // (BtlTotalDamageUI: total hits across consecutive player actions)
    float cr3ComboSince = -1;               // (BtlTotalDamageUI: when first combo hit landed)
    int cr3VictorUnit = -1;                 // (BtlStatusList::PlayVictory: solo finisher unit during phaseVictory)
    float cr3VictoryDuration = 2.5f;
    int cr3GaugeShown = 0;                  // (the break gauge as shown - BtlBreakGaugeUI's direction point: hit by hit)
    std::vector<std::string> cr3AnimCommon; // (model.mbe AnimCommon: a motion type's suffix)
    std::vector<std::array<int, 3>> cr3Choices;
    std::vector<cr3::Cr3Action> cr3Actions;
    size_t cr3ActionAt = 0;
    float cr3ActionStart = 0, cr3StageStart = 0, cr3MenuStart = 0;
    int cr3Chooser = -1, cr3Focus = 0;
    struct Cr3ActionPlay {
        cr3::Cr3BattleTables::ActionTiming timing;
        size_t nextMotion = 0;
        size_t nextEffect = 0, nextCastEffect = 0;
        float end = 1.5f;
        int hitsShown = 0;
        int directionCommand = -1;
        bool melee = false;
        cr3::Cr3BattleTables::MotionTravel returnTravel{};
        std::vector<int> spellCards;
        float spellCardAge = -1;
        bool spellHandoffPending = false, cardWatchesCamera = false, cardPaused = false;
    } cr3ActionPlay;
    // the damage shown (DamageInfo: btNumMenu over the target, btNumMenu_in)
    struct Cr3Popup {
        int value = 0; // (below 0: none - a word alone)
        std::array<float, 3> at{};
        float start = 0;
        int bonus = 0; // (DamageInfoMes's kind - btBonus_fr00's frame + 1: 1 MISS, 3 blocked, 5 CRITICAL; 0 none)
        int color = 0; // (BtlDamageUI: 0 normal, 1 weak, 2 resist, 3 HP heal, 4 MP heal)
    };
    std::vector<Cr3Popup> cr3Popups;
    struct Cr3Tap {
        float x0, y0, x1, y1;
        int id;
    };
    std::vector<Cr3Tap> cr3Taps; // (the menu's buttons as last drawn, UI units)
    Mat4 cr3BattleVP = cr3::mat4Identity();
    // the command buttons (BtlInterfaceSystem::EntryCommandSelect: 6 Attack, 7 Gene, 8 Item, 9
    // Guard - comBtn's name, icon frames; Gene and Item not in yet)
    static constexpr int kCr3Buttons[4] = {6, 7, 8, 9};
    void cr3CountRecords();
    void setCr3Motion(size_t k, const std::string& suffix, bool loop, bool hold = false);
    std::string cr3MotionOf(int type) const;
    void startCr3Melee(size_t source, size_t target);
    void stepCr3MeleeReturn(size_t source, float actionTime);
    // a unit's joint as last posed (DummyCP its camera point, DummyDP01 where its damage shows)
    // the hit effects (BtlDamageTarget::PlaySingleHitEffect -> BtlUnitList::PlayHitEffect): the
    // command's CommandDirection +0x2c (direction.mbe column 9: attack 1, be001hit01), on every hit
    // that lands, at the target; and with +0x59 bit 0 set the critical's (4) and the element's
    // (element + 4) too. Its effect.mbe Effect row: the figure and its sound (+0x10). A random size
    // with +0x58 bit 7 ((rand - 0.5) x 2 x +0x68 + 1). Each a figure of its own animation, played
    // once (the .sprk particles and the random turn - bit 3 - not in)
    struct Cr3ProjectilePath {
        cr3::Vec3 from{}, via{}, to{};
        float speed = 0, acceleration = 0;
        cr3::Vec3 at(float t) const {
            const float progress = std::clamp(t * (speed + 0.5f * acceleration * t), 0.0f, 1.0f);
            cr3::Vec3 a, b, tangent;
            for (int i = 0; i < 3; ++i) {
                a[i] = via[i] - from[i];
                b[i] = to[i] - via[i];
            }
            const float lenA = std::hypot(a[0], a[1], a[2]), lenB = std::hypot(b[0], b[1], b[2]);
            cr3::Vec3 out;
            if (lenA < 1e-5f || lenB < 1e-5f) {
                for (int i = 0; i < 3; ++i) out[i] = from[i] + (to[i] - from[i]) * progress;
                return out;
            }
            // MbRNS weights the two Hermite spans by their lengths and shares a unit tangent at the waypoint.
            for (int i = 0; i < 3; ++i) tangent[i] = a[i] / lenA + b[i] / lenB;
            const float len = std::hypot(tangent[0], tangent[1], tangent[2]);
            const float distance = progress * (lenA + lenB);
            const bool first = distance <= lenA;
            const float u = first ? distance / lenA : (distance - lenA) / lenB;
            const float u2 = u * u, u3 = u2 * u;
            for (int i = 0; i < 3; ++i) {
                const float middle = len > 1e-5f ? tangent[i] * (first ? lenA : lenB) / len : 0;
                const float start = first ? from[i] : via[i], end = first ? via[i] : to[i];
                const float entry = first ? (3 * a[i] - middle) * 0.5f : middle;
                const float exit = first ? middle : (3 * b[i] - middle) * 0.5f;
                out[i] = (2 * u3 - 3 * u2 + 1) * start + (u3 - 2 * u2 + u) * entry +
                         (-2 * u3 + 3 * u2) * end + (u3 - u2) * exit;
            }
            return out;
        }
    };
    struct Cr3Fx {
        std::string figure;
        cr3::Vec3 at{};
        float start = 0, scale = 1, life = -1, yaw = 0;
        float animStart = 0, animLength = 0;
        float sparkTime = 0;
        bool loop = false;
        int followUnit = -1, point = 0;
        Cr3ProjectilePath path;
        ViewSpark spark;
    };
    std::vector<Cr3Fx> cr3Fx;
    std::map<std::string, std::unique_ptr<ViewFigure>> cr3FxFigures;
    ViewFigure* cr3FxFigure(const std::string& name);
    void clearCr3Effects();
    Cr3Fx* spawnCr3Effect(int id, const cr3::Vec3& at, float scale, float start = -1);
    cr3::Vec3 cr3EffectPoint(size_t unit, int index);
    void launchCr3Projectile(size_t source, size_t target, float at);
    void knockOutCr3Unit(size_t k);
    void restoreCr3Unit(size_t k);
    void cr3HitEffects(int command, int element, size_t targetUnit, bool critical);
    void drawCr3Effects(const cr3::Mat34& camera, const Mat4& vp);
    void stepCr3CastEffects(size_t source, float t);
    void stepCr3ActionEffects(const cr3::Cr3Action& action, size_t source, float t);
    std::array<float, 3> unitPoint(size_t k, const char* joint, float up);
    int cr3FighterOfUnit(size_t k) const;

    // a turn begins: the party's first able member chooses (PlayCommandSelect on it)
    // (BtlSysStateSelect::phaseGetSetStart: the get-set camera at its end, then EntryGetSet -
    // Battle, Escape - not where the coupling's +0x44 bit 1 forbids it -, Options; SE bt_001 the
    // first time)
    bool cr3GetSetSeen = false;
    std::map<int, int> cr3TurnItems; // (the items chosen this turn, by fighter: their names announced)
    void beginCr3Turn(bool countTurn = true);
    bool cr3EscapeAllowed() const;
    void chooseCr3GetSet(int b);
    bool cr3Escaped = false, cr3EscapeWorks = false, cr3EscapeAsk = false;
    int cr3EscapeThreshold = 0;
    void chooseCr3Escape(int b);
    void stepCr3Escape();
    // the list (Gene: BtlStatusList::OpenCommandList - the gene's commands; Item: BtlItemListUI -
    // the items held, each its Item row's command): a row each, its cost, usable or not
    struct Cr3ListRow {
        int command = -1, item = 0; // (item: its full id, 0 for a gene command)
        std::string name;
        int cost = 0, count = 0;
        bool usable = true;
    };
    std::vector<Cr3ListRow> cr3List;
    bool cr3ListItems = false;
    int cr3ItemCategory = 0; // (BtlItemListUI: Items / Gifts)
    int cr3ChosenCommand = -1, cr3ChosenItem = 0;
    bool cr3TargetChanged = false;
    std::string cr3NameOf(const char* file, const char* table, int id);
    bool cr3CommandHasTarget(const cr3::Cr3Command& c) const;
    void openCr3List(bool items, bool keepCategory = false);
    bool battleShoulder(bool right);
    void chooseCr3ListRow(int k);
    void nextCr3Chooser();
    void chooseCr3Command(int button);
    std::vector<int> cr3LivingMonsters() const;
    bool cr3TargetCanChange();
    std::vector<int> cr3Targets();
    void chooseCr3Target(int n);
    void playCr3AttackCamera(const cr3::Cr3Action& action, size_t source);
    void playCr3CasterCamera(const std::string& name, size_t source);
    void playCr3SpellReceiverCamera(const cr3::Cr3Action& action, size_t source);
    float stepCr3SpellCardClock(float dt);
    bool finishCr3SpellHandoff(const cr3::Cr3Action& action, size_t source, float time);
    void startCr3Action();
    bool cr3PendingStart = false; // (an action to start once the battle's script is over)
    void stepCr3Action();
    void endCr3Actions();

    // the commands a battle script queued (BattleCommand.SetId / SetIdAndTarget - BtlSysStateAction::
    // VUpdate's states 0xc, after Battle_Defeat_Base, and 0x12, after Battle_Turn_End_Base: with any
    // queued, AllEndEffectToCalculation(0xb), the command manager cleared, each added with no unit -
    // source -1 - and calculated; the battle goes on, no defeat). Every battle's defeat script with
    // flag 5053 (chapter 1's d02 .. t01) queues 72: the party's side (target 11) back whole - KO
    // (status effect 1) taken away, HP its power's percentage of the max
    std::vector<std::pair<int, int>> cr3ScriptCommands; // (command, target fighter - -1 the command's own)
    bool playCr3ScriptCommands();

    // the result (BattleResultV2Menu: the EXP and money - the monsters' MbMonsterInfo::GetExp /
    // GetMoney at their levels: +0x3c / +0x40 + (level - 1) x the s16 at +0x44 / +0x46)
    int cr3ResultExp = 0, cr3ResultMoney = 0;
    // the EXP into the genes (BattleResultMenu::Update, CrxGene::AddTemporaryExp / SaveExp): each
    // party member's worn gene - the first three slots' standing ones the whole, the rest (and the
    // fallen) half; its level from its EXP (MbGeneInfo::GetLevel), at its top the EXP held there; a
    // level up com_018 and EffectGeneUp. The cards: the gene's level, its bar the level's share
    // done (1 - CrxGene::GetExpRate)
    struct Cr3ResultCard {
        int ch = 0, level = 0;
        float progress = 0;
        bool up = false; // (com_018 at the result)
    };
    std::vector<Cr3ResultCard> cr3ResultCards;
    void giveCr3GeneExp();
    // (BtlStatusList::AcquireRemuneration: the fallen monsters' alone - each wave's at its victory -;
    // VAcquireExperience: x (1 + (the monsters' average level - the party's highest gene level) x
    // 0.02), 0.5 .. 2, at least 1)
    int cr3WaveExp = 0, cr3WaveMoney = 0;
    std::vector<int> cr3WaveDrops; // (the items dropped, the waves' before)
    bool cr3VictoryIn = false;     // (phaseVictoryIn: the victory event over, the result's music on)
    void acquireCr3Remuneration();
    void enterCr3Result();
    bool cr3BattleKey(SDL_Keycode k);
    bool cr3BattleClick(float fx, float fy);
    std::array<float, 3> cameraPoint(size_t k);
    void cr3BattleTap();

    // the battle over (won): where SetBattleNext sent it - the vista SetVistaFuncName2 named
    // (SetBattleToVista), else - and after a game over - the field it came from (Fld2Main::SM_FirstEvent:
    // evt000, evt002 if lost)
    bool cr3BattleLost = false;
    bool cr3BattleInSkipped = false;
    bool cr3BattleVista = false; // (its script reserved a vista: SetVistaFuncName)
    int cr3Turn = 0, cr3ForcedEnd = -1, cr3BattleFieldId = -1;
    std::vector<int> cr3FieldEffects; // (BtlFieldEffectManager: the boot script's AddFieldStatusEffect)
    int cr3ResultAdd[2] = {0, 0}, cr3ResultFactor[2] = {1, 1}; // (the scripts' EXP / money added, and factors)
    void endCr3Battle();
    void drawCr3Battle(int w, int h);
    ViewSpark cr3BattleSpark; // (Battle.SetFieldVisualEffect's particles, stepped by the battle's clock)
    float cr3BattleSparkClock = 0;
    bool startVista(const std::string& function, const std::string& map);

    // A lunch (LunchMain: LunchEventStart's function and map - a vista whose scene stays). Its
    // script runs as a vista's (LoadScript: the function, its script by its name - s001_t02_00301_evt
    // t02's); once over, LunchEvent.Setup's balloons shown over the models (feFlag_fr00 at the
    // model's place and its offset, through the scene's camera - Cr3UtilSetBalloonPos); a tap
    // within 0.55 of the point half a unit above one (ProcessBalloonTouch: its square under 0.3)
    // runs <function less _evt>_<model> (s001_t02_00301_c106), the balloons held while it runs.
    // LunchEvent.Shutdown, the script over: on as a vista ends (BranchAfterVista)
    bool lunchActive = false, lunchWaitLogged = false;
    std::string lunchFocus;
    std::string lunchFunction;
    Mat4 lastEventVP{};
    bool lastEventVPSet = false;
    void startLunch(const std::string& function, const std::string& map);
    bool lunchBalloonAt(const cr3::EventRuntime::LunchBalloon& b, float& x, float& y);
    bool lunchIdle() const;
    bool lunchKey(SDL_Keycode key);
    void chooseLunchBalloon(const std::string& model);
    bool lunchClick(float fx, float fy);
    void drawLunchBalloons(int w, int h);
    void stepEvent();
    void syncEventFigures(cr3::EventRuntime& ev);
    float eventLead() const;
    void updateEvent(float dt);
    void poseEvent();
    void poseEventModels(cr3::EventRuntime& ev);
    void poseEventFigure(cr3::EventRuntime& ev, const cr3::EventModel& m, ViewFigure& f);
    void drawEvent(int w, int h);

    cr3::Mat34 lastCamera;

    // The message windows, in the interface's space (the hud_camera2D camera: orthographic,
    // 0.96 x 0.64 units, 3:2), fitted to the screen and centred. A window is a figure whose joints
    // type<size>_<align><NN> place its text (Cr3UtilSearchTextPosition: size S 28, M 32, else 64
    // pixels of a 1000-a-unit layout, drawn 4 smaller; L left, C centred, R right) - slot k the
    // joint numbered by the window's table - and call_sendCsr_loop* its page cursor:
    // - the vista's (VistaConversationWindowMenu, vstWin): a black band across the screen's foot
    //   and the name line (vstWin_talk slides it in), stretched across screens wider than 3:2
    //   (SetScale aspect / 1.5); slots 01 the name, 11 / 12 a page's lines, 21 / 22 centred ones;
    // - the narration (NarrationWindowMenu, narrationPos): no figure of its own - the text over the
    //   screen (the fade's black, as the scripts have it), slot k numbered k + 1 in its centred
    //   column (EvtWindow::ShowMessage: a line's slot its place on the page; NarrationWindowMenu
    //   ::SetStringData: column C).
    cr3::TextRasterizer font;
    struct WindowLayout {
        ViewFigure fig;
        std::vector<std::string> joints;
        bool stretch = false; // (across a wide screen)
        float span = 0;       // (its panel's width over a 3:2 screen's: on a screen wider than that, stretched to it - 0 never)
        bool nameTag = false; // (the name in slot 0, a page's lines from slot 1)
        int linesPerPage = 2; // (with a name tag: slots 1.. the lines, the centred ones after them)
        std::vector<std::string> slots; // slot -> the joint's number
        std::string cursorJoint;
        int window = -1;                // (EventRuntime's)
        std::string inAnim, outAnim;    // (opening / closing animators, posed at the window's age)
        bool lowerBelow3x2 = false;     // (narrower than 3:2: 0.0403125 lower)
        bool centredSlots = false;      // (no name tag: a centred page's lines in the slots after linesPerPage)
        float frame = 0;                // (where its animation was posed: its joints' places)
        bool outBackwards = false;      // (no out animator: the in one played back)
        // CR3: placed by a Position figure (PartsBase::SetParameterDataBase "comMsgVPosition" ...):
        // the window at its joint call_<window>... (SearchOffsetJointPositionAnyString), the
        // Position figure itself SetPosition'd (0, positionY)
        ViewFigure positionFig;
        std::vector<std::string> positionJoints;
        std::string positionCall;
        float positionY = 0;
        int fitOverride = 0; // (0 joint's _fitN, -1 none for middle announce, 1..9 explicit _fitN)
        bool speakerOnly = false; // (CR3's name window: the speaker's name alone, shown while one speaks)
        // CR3's interface space is ten times CR2's: 960 x 640 at 100 pixels a unit (CRXPartsBase::
        // GetFitXOffset's (width - 960) / 2 / 100), its HUD camera the same - the units scale
        float uiUnit = 1;
        // a page's text where its colour is the default (0): CR2's windows white; CR3's message
        // box draws dark slate on its light panel (measured from a playthrough: ~(76, 83, 91))
        float lineRgb[3] = {1, 1, 1};
        bool lineShadow = false;
        float lineShadowRgb[3] = {0, 0, 0};
        // CR3's text sizes as its window's SetStringData asks (0: CR2's placing), the name's own
        // offset, and the vista's lines as one block (each left where the widest is)
        int nameSize = 0, lineSize = 0;
        float nameDx = 0, nameDy = 0;
        bool blockLines = false;
    };
    WindowLayout vistaWindow, vistaName, narrationWindow, conversationWindow;
    // CR3's tutorial menu (TutorialMenu): ttlMnlMenu at ttlMnlPositon's call_ttlMnlMenu_fit8, four
    // lines at typeM_L01..L04 (SetMenuText: size 30, white, left), its picture ttlMnlImg_fr00 at
    // call_ttlMnlImg_fr00 - tuto_ss<n>, the frame its animation's (id - 1) % 8 / 30 s
    WindowLayout tutorialMenuWindow;
    // the tutorials' and announcements' windows (CR3 FieldAnnouncementMenu: comMsgAMenu at
    // comMsgAPosition's call_comMsgAMenu_fit8, windows 2/4/8 top, 3/9 middle, 10 bottom; CR2: annoWin / helpWin)
    WindowLayout helpWindow, annoWindow, announceWindow, announceTopWindow, announceMidWindow, announceBotWindow;
    // the system window (EvtSystemWindow, window 5 - SetSystemMessageWindowParameter, interface 0x20:
    // SystemWindowMenu, sysWin_io): three lines at typeM_L01..L03 (its slot table, 0x5afff8), its io
    // animator forward as it opens and back as it closes (InterfaceCloseAnimeSet: ChangeAnime 2)
    WindowLayout systemWindow;
    WindowLayout itemWindow; // (window 7, GetItemWindowMenu: getItem - its text at typeM_C01, the item's icon at call_mainIcon_id00)
    ViewFigure pageCursor;         // sendCsr_loop: once the page is typed
    ViewFigure conversationCursor; // msgCsr_loop: the conversation window's
    struct TextQuad {
        cr3::Texture tex;
        float x = 0, y = 0, w = 0, h = 0; // UI units: the quad's left, centre, size
        float rgb[3] = {1, 1, 1};
        std::vector<float> pen;           // each character's left edge and the end, in UI units
        int from = -1;                    // the page's characters before it (-1: shown whole - the name)
        std::string joint;                // (its place: the joint's as the window's animation has it, + dx, dy)
        float dx = 0, dy = 0;
        uint64_t used = 0;                // (uiTexts: when last drawn)
        float border = 0;                 // (a black outline this wide, UI units: ProvisionalMenuText type 2's SetBordering)
        float below = 0;                  // (the bitmap's rows under the line's box: its descenders, UI units)
        bool shadow = false;              // (a drop shadow: ProvisionalMenuText type 3's copy at (0.01, -0.0172), its back colour)
        float shadowRgb[3] = {0, 0, 0};
    };
    std::vector<TextQuad> textQuads;
    std::string shownText; // (what textQuads were made for)
    int shownPx = 0;
    // (CR3's name window's own: drawn in the same frame as the vista window's text)
    std::vector<TextQuad> nameQuads;
    std::string nameText;
    int namePx = 0;
    void loadWindow(WindowLayout& w, const char* figure);
    void loadPosition(WindowLayout& w, const char* figure, const char* call, float y);
    void loadMessageWindow(const std::string& fontPath, std::vector<uint8_t> fontBytes = {}, const std::string& fontName = {}, int fontFace = 0);
    static Mat4 uiProjection(int w, int h, float unit = 1);
    static void fitOffset(const std::string& joint, int w, int h, float unit, float& dx, float& dy);
    bool windowJoint(const WindowLayout& wl, const std::string& name, float& x, float& y);
    std::string slotJoint(const WindowLayout& wl, size_t slot);
    void drawWindow(WindowLayout& wl, bool showsMessage, int w, int h);

    // The cut-ins' portraits (CutInWindowMenu): `cutIn`, its first material's colour map the face
    // (SetTexture), its DiffuseColor the brightness (ChangeDiffuseColor), posed by the layout's io
    // animation; the other side's joints hidden (GEO_face*_L or _R); on screens wider than 3:2
    // moved toward its side by the margin's width over the 3:2 area's (PositionCorrection: the fit
    // parameter's side - 3 / 6 right, 1 / 4 left - the two-sided layout not moved), narrower ones
    // 0.0403125 lower. Behind the conversation window (their depths: -0.064 against -0.048).
    std::array<ViewFigure, cr3::EventRuntime::kCutIns> cutInFigures;
    // CR3's (CutInMenu, interface 0x3a - n for EvtFace.Create(n): 0 right, 2 left): a Position
    // figure slid in by its _io animator in slot 1 (relative to its base: +0xc4 = 1), the portrait
    // (comMsgCut<R|L>_id00, its material the face) at its call_ joint's fit anchor, posed by its
    // own animation (the right one's turns its image about y: the left-facing faces mirrored)
    // The centre one (CutInCenterMenu, Create(1)): comMsgCenterPosition places comMsgCenterMenu at
    // call_comMsgCenterMenu_fit0, its _in animation played as it opens (absolute) - a flash over
    // the screen (its user channels) and the joint call_comMsgCenter_id00, where the portrait
    // (comMsgCenter_id00, mat_cutIn001_C01 the face) sits at that joint's scale (GetScaleSize),
    // hidden for its first 1/6 s, then faded in over the next; comMsgeff_01 (ten times its size)
    // burst over it from 0.367 s until its animation ends
    struct Cr3CutIn {
        ViewFigure position, portrait;
        ViewFigure menu, effect; // (the centre one's)
        std::vector<std::string> joints, menuJoints;
        std::string call;
        bool loaded = false;
    };
    std::array<Cr3CutIn, 3> cr3CutIns;
    bool cr3CutInsLoaded = false;
    void loadCr3CutIns();
    void drawCr3CentreCutIn(Cr3CutIn& c, const cr3::EventRuntime::Portrait& p, cr3::EventRuntime& ev, int w, int h, const float* view);
    void drawCr3CutIns(int w, int h);

    // CR3's interface parts placed by a Position figure: the part at its call_<part>_fitN joint,
    // fitted (SearchOffsetJointPositionAnyString), posed by a looping animator over its base (slot
    // 1: AddAnimator .., SetAnimeLoop) or its own animation (slot 0)
    struct Cr3Part {
        ViewFigure position, fig;
        std::vector<std::string> positionJoints;
        std::string call;
        bool loaded = false;
    };
    // the message windows' buttons: MessageWindowMenu's comMsgBtn at comMsgPosition's
    // call_comMsgBtn_fit3 (rendered while the page waits for a tap - +0x62 -, typing or not), and
    // the vista's PageSendCursorButton - comMsgPBtnMenu, comMsgPBtnMenu_loop, at comMsgPBtnVPosition's
    // call_comMsgPBtnMenu_fit3 - reopened once the page is typed, fading in over 1/6 s (Pose:
    // alpha the time * 30 / 5)
    Cr3Part messageButton, vistaPageCursor, announcePageCursor, vistaFastButton, vistaStopButton;
    Cr3Part tutorialMenuPicture; // (the tutorial menu's: tutorialMenuWindow's)
    int vistaTypedFrame = -1, announceTypedFrame = -1, narrationTypedFrame = -1; // (the event update the page was typed by: the cursor's fade)
    bool loadCr3Part(Cr3Part& p, const std::string& position, const std::string& figure, const std::string& call, const std::string& loop);
    void drawCr3Part(Cr3Part& p, int w, int h, float t, float alpha, float dy = 0, int fitOverride = 0);

    // CR3's title (TitleMenuMain, interface 0xd): behind it Fld2CameraDemo's reel - six shots of
    // four areas (t0209f, d0304f, d0704f, d0405f; the boot archive's) seen through the cameras
    // f0000title01..06, each shot's map and camera played from its start, faded in from / out to
    // black or white over half a second (its table: seconds, map, camera, the colours in and out,
    // -1 none) -; over it TitleMenu: ttlLogoMenu (the logo, "Tap to play", the copyright) at
    // ttlLogoPositon's call_ttlLogoMenu_fit0, the title music (33) - a tap (SE com_001) takes it
    // out (ttlLogoPositon_out) and, a first game's, on through white into the prologue
    // (TitleMain: t01's Login, then the new game's first vista - s000_t01_00101_evt)
    struct Cr3Title {
        int stage = 0;  // 0 off, 1 fading in, 2 waiting for a tap, 3 the logo away, 4 the save files, 5 on into the game
        float clock = 0, stageClock = 0;
        int shot = 0;
        float shotClock = 0;
        std::array<ViewFigure, 4> maps;
        std::array<cr3::CameraFigure, 6> cameras;
        Cr3Part logo;
        int lightsOf = -1; // (the map whose lights are on)
    } cr3Title;
    struct DemoShot {
        float seconds;
        int map, camera;
        float in, out;
    };
    static constexpr DemoShot kDemoShots[6] = {{9.733f, 0, 0, 0, -1}, {6.467f, 0, 1, -1, 1}, {8.167f, 1, 2, 1, 1},
                                               {10.0f, 2, 3, 1, 1},   {12.5f, 3, 4, 1, 1},  {11.0f, 0, 5, 1, 0}};
    static constexpr const char* kDemoMaps[4] = {"t0209f", "d0304f", "d0704f", "d0405f"};
    bool startCr3Title();
    void updateCr3Title(float dt);
    bool cr3TitleTap();
    void drawCr3TitleScene(int w, int h);
    void drawCr3TitleUi();
    void drawCr3TitleOptions();

    // CR3's map name (MapNameMenu): feNameMenu at feNamePosition's call_feNameMenu_fit3, its _in
    // animator (slot 1, over its base) 0 .. 0.967 s in, held 2 s, then on to 1.333 s out; on it
    // feNameList_fr00 (the dungeon's name: frame n) at call_feNameList_fr00 and six feNameNum_fr00
    // digits (the level's range: 3 + 3) at its call_feNameNum_fr00.. - each turned 30 degrees
    // about z; all faded in over 0.667 s, out over 0.1 (SetAllFade)
    struct MapNameParts {
        ViewFigure position, menu, name;
        std::array<ViewFigure, 6> digits;
        std::vector<std::string> positionJoints, menuJoints;
        bool loaded = false;
    } mapNameParts;
    void loadMapNameParts();
    void drawMapName(const cr3::EventRuntime& ev, int w, int h);
    void loadCutIns();
    void drawCutIns(int w, int h);

    // The balloons (FldBalloon: the `balloon` figure, looping), drawn after the foreground through
    // the interface's camera: each over its gimmick - its height up - where the field's camera
    // shows that (Cr3UtilSetBalloonPos: projected, then back through the interface's camera)
    Mat4 fieldScene = cr3::mat4Identity();
    ViewFigure balloonFigure;
    bool balloonLoaded = false;
    // The battle's interface (BtlInterface): an interface figure by name, posed by one of its
    // animations; its joints placed
    struct UiFig {
        ViewFigure fig;
        std::vector<std::string> joints, materials;
        bool loaded = false;
    };
    std::map<std::string, UiFig> uiFigs;
    bool loadingUi = false; // (uiFig loading its figure: left unsmoothed)
    UiFig& uiFig(const std::string& name);
    void poseUi(UiFig& u, const std::string& anim, float t, bool loop = false);
    static float uiRight(const UiFig& u);
    bool uiJoint(const UiFig& u, const std::string& joint, float& x, float& y);
    // DotNumId2: a number's digits from hud_dotnum (DotNumParamPackTable: set `set`'s glyph d at
    // set + d, its size w x h pixels of a 1000-a-unit layout and its UVs), right to left from
    // (x, y), `digits` places (the leading ones blank, but a 0)
    struct Glyph {
        float w, h, u0, v0, u1, v1;
    };
    std::vector<Glyph> dotNumGlyphs;
    void loadDotNum();
    void drawDigits(int value, int set, int digits, float x, float y, const Mat4& proj, float alpha = 1, float scale = 1, bool zeros = false);
    // The title screen (`--title`, the options' Title Screen; GameTitleMenu - InterfaceMain's
    // interface 0x3b): logoInc over a fade from black, then `title` (its animators: 1 title_in, 2
    // title_wait, 3 title_loop looping, 4 title_out) coming in over title_bg - stretched past 3:2 -
    // and divaBtn_io from white, BGM 1, looping until a tap anywhere (SE 0xb4): title_wait brings
    // its buttons in (the circle buttons at call_circleBtn_frNN, drawn short of x 0.67 - 08 Start,
    // 20 Options, 24 Scenarios, ScenarioFlowMenu), and title.nut's `title` runs. Leaving them
    // (CloseTitleMenu) title_in runs backwards (PartsBase +0x5c 2). Start: the continue data (cont.txt) - its calendar and
    // "Resume ...?" (104) - else the panel (newlyBtn_io: 09 New Game, 10 Load - only New Game
    // without a save; bossIcon_io's three by the save's flags 1, 3, 4; the save's calendar; btlBack
    // 100 back): New Game "Begin a new game?" (101; with the save's flag 5 102, then 103 for a plain
    // one), Load (menuLoad_io) "Load data?" (107), then "Load complete." (108) a second; then the
    // game. (The site and Game Center links left out.)
    struct TitleMenu {
        int stage = 0; // 0 off; 1 the logo, 2 the title in, 3 waiting for a tap, 4 its buttons
                       // coming in, 5 its buttons, 6 the panel, 7 a question, 8 "Load complete.", 9
                       // options / scenarios, 10 leaving the buttons (then `next`)
        int next = 0;
        float start = 0;
        int ask = 0;        // the question's announcement
        bool tapped = false;
        int action = 0;     // for the main loop: 1 a new game, 2 the save, 3 the continue data
        float doneAt = 0;
    } titleMenu;
    std::unique_ptr<cr3::EventRuntime> titleEvents; // (title.nut's calls)
    double playSeconds = 0;                         // (the save's play time, cr3_savedata +0x10 / 20)
    static bool saveExists(const char* file);
    // a save's flags, its time and play time (its extras)
    struct SaveInfo {
        bool exists = false;
        std::set<int> flags;
        std::time_t saved = 0;
        long playFrames = 0;
    };
    static SaveInfo saveInfo(const char* file);
    void resetSessionUi();
    void startTitle();
    bool titleRunning() const;
    void titleScript(const char* function);
    float titleAnimLength(const std::string& fig, const std::string& anim);
    void carryOver();
    void titleAsk(int id);
    void titleButton(int id);
    void titleAnswer(bool yes);
    void titleToken(const std::string& tok);
    void titleTap();
    bool titleClick(float fx, float fy);
    void updateTitle(float dt);
    void drawCalendar(const SaveInfo& s, const Mat4& view, const Mat4& proj, float t);
    static void showJoint(UiFig& u, const std::string& joint, bool visible);
    static void hideJoint(UiFig& u, const std::string& joint);
    void focusLook(UiFig& c, int id);
    void drawTitleButtons(UiFig& parts, const Mat4& view, int skip);
    void drawTitle();
    void drawEnchant(int sign, float x, float y, const Mat4& proj);
    void drawStatusSign(int n, float x, float y, const Mat4& proj);
    static int elementSign(const cr3::Fighter& f);
    static std::vector<int> stateSigns(const cr3::Fighter& f);
    // a line of text at a UI place (centred), made once
    // (every string at every size: past the limit the ones not drawn for longest go, their textures with them)
    static constexpr size_t kUiTextLimit = 1024;
    std::map<std::string, TextQuad> uiTexts;
    uint64_t uiTextUses = 0;
    // `left`: from x (typeS_L: the text's left), else centred on it; `shade` its colour's
    // `maxWidth` (UI units, 0 none): narrowed to fit it (MenuText's width limit)
    // A text's width as drawUiText draws it (UI units: 0 none)
    float uiTextScale = 1; // (the text's size times this: CR3's Options scaled to a desktop's window)
    float uiTextWidth(const std::string& text, float size, int w, int h);
    float drawUiText(const std::string& text, float size, float x, float y, int w, int h, bool left = false, float shade = 1, float maxWidth = 0, float border = 0);

    // The battle's HUD: the party's windows (BattleStatusMenu: pc01win / pc02win, their HP and MP
    // bars - btlHpBar_per posed at HP x 5 / max seconds, at least 0.07 - the face, the numbers:
    // pc0NhpNum's call_dotNum_num1, five digits of set 90), the break gauge (BreakWindowMenu:
    // brkWin, brkGauge at (gauge + 4) / 2 seconds - 2 even -, brkInfo's EV / PL / EN), the
    // command's name (btlMsg, typeM_C01) and the damage (DamageInfo00: btlNum, btlNum_fgrN for N
    // digits, a digit of set 20 at each call_dotNum_idNN, at the target's DummyDP01)
    Mat4 battleVP = cr3::mat4Identity();
    // (BtlActionCtrl::ShowDamage: the number - damageinfodata01's set by its kind: 20, 40 a
    // weakness's, 50 / 60 the restores' -, and its mark, DamageInfo01: `bonus` with
    // damageinfodata02's animator - 0 bonus_miss, 2 bonus_crtcl; 1 block, 3 blow - at the same place)
    struct DamageShown {
        int value = 0, set = 20, mark = -1; // (the value -1: the mark alone)
        float x = 0, y = 0, start = 0;
        // where it is in the world: its place on the screen the next frame drawn's (the camera
        // as it is then - a scene's may cut at the hit)
        float at[3] = {0, 0, 0};
        bool placed = false;
    };
    bool battleUi(const float* p, float& x, float& y) const;
    std::vector<DamageShown> damageShown;
    std::string commandShown;
    float commandShownStart = -10;
    float chargeShown = 0, chargeShownAt = 0; // (the charge gauge's time shown)
    void drawCommandMenu(const std::string& anim, float t, const Mat4& view, const std::vector<int>& shown,
                         const std::map<int, std::string>& labels = {}, int w = 0, int h = 0, int frameOf = 0,
                         const std::string& figure = "btlbtn");
    // The skill list (BattleSkillListMenu: a ScrollListMenu of scrlMtr_per / scrlBar / scrlWin /
    // scrlBtn ... - GetModelDataName): the frame scrlBar (scrlBar_io), a row scrlBtn (scrlBtn_io)
    // each 0.08 lower (GetColumnSpacing) - its name at typeS_L00 (white; half, not selectable), its
    // number (MP, or the awakening's level: DotNumId2, set 90) at call_dotNum_id01, its icon
    // (mainIcon_id at its frame / 20 s) at call_mainIcon_id11, the focused one's sBtnSlct at
    // call_sBtnSlct14 -, scrlBack (scrlBack_io) to go back. Each row's tap area kept: its id
    struct ListRow {
        int id = 0;
        std::string name;
        int number = 0, icon = 0;
        bool selectable = true;
        std::string help; // (the command's or item's text: "\\n" between its lines)
        int seal = -1; // (ScrollListItem::SetIcon 2: seal_id at call_seal_id12 - 2 "new")
    };
    void drawSkillList(const std::vector<ListRow>& rows, int focused, float t, const Mat4& view, int w, int h, int tagFrame, int costIcon);
    // a button's look as drawn: 0 not selectable, 1 as it is, 2 focused (the keys' cursor)
    std::function<int(int)> buttonLook;
    std::function<int(int)> buttonFighter; // (a target button's fighter, -1 none)
    std::function<int(int)> buttonLevel;   // (an awakening button's level)
    std::vector<ListRow> listRows;         // (the skill list as drawn: a tap on a row focuses it)
    // the menu of the choosing's step, the cursor's choice marked (the keys')
    // BattleOptionMenu (an OptionMenu - InterfaceMain's 0x3e - with the battle's labels): the title
    // (`menu` with menuOpt_io), the seat (seatOpt_io): its seven labels at typeM_L01..L07 - BGM,
    // Sound Effects, Voiceovers, Language, Lock Orientation, Title Screen, Manual
    // (announcemenuname 103, 104, 105, 102, 106, 108, 101: InterfaceMain::m_IdCounter + 4) - and
    // the quality (143 High, 144 Middle, 145 Low) at typeM_C01; the three volumes (VolumControlBar:
    // vlmBar_per at call_vlmBar_per13..15, posed at its volume, 0..100, / 20 s - cr3_system_data +0,
    // +4, +8); circleBtn_fr at call_circleBtn_frNN (frame NN), mainIcon_id at call_mainIcon_idNN;
    // btlBack to leave. (No sound here: the volumes kept, the other settings shown.)
    bool optionsOpen = false;
    float optionsStart = 0; // (uiClock's)
    // the language (cr3_system_data +0xc, Cr3UtilOptionLanguage: the messages' own order - 0
    // Japanese, 1 English, 2 French, 3 German, 4 Spanish, 5 / 6 simplified / traditional Chinese,
    // 7 Korean) and the quality (+0x11, SetOffScreenMode: 0 High, 1 Middle, 2 Low - labels 143..145)
    int optionLanguage = 1, optionQuality = 0;
    // The languages to pick from (cr3::releaseLanguages: the release's own, then each fan
    // translation over it - English's, Spanish's) and the one in use. The Vita's had no language
    // option: CR3-U's steps through these, the options' slots (two in the iPhone's and Android's
    // messages, one in the Vita's - the translations' all in its 0) not one by one
    std::vector<cr3::LanguageChoice> languages;
    size_t languageIndex = 0;
    int textGeneration = 0;   // (a language changed: the name tables kept read again)
    std::string archiveFont;  // (the font the release - or a translation over it - has: font\TBGB_0KP.TTC, F0001.bin; "" another)
    std::string archiveFontFrom;
    void stepLanguage(int by);
    void languageFromSlot();
    void useLanguage(size_t i, bool live);
    void setEventLanguage();
    // the Title Screen's question (BattleOptionMenu::Update 3, 5, 6: the window closed, a system
    // window with announcebattle 0x6d, choice_io's Yes / No - yes to the title, no back)
    bool optionsAsk = false, toTitle = false, optionsWasOpen = false;
    float optionsAskStart = 0;
    void optionTap(int id);
    // The manual (the options' Manual: BattleOptionMenu 4 -> 9, InterfaceMain's 0x22 -> 0x61,
    // InstructionsWindowMenu): `message\instructions` (u32 the records' offset, the strings', the
    // count; a record 0x28 - the eight languages' titles, +0x20 a story flag it waits for (0 none),
    // +0x24 its tutorial) listed by those whose flag is on (SetTutorialList) in InstructionsListMenu
    // (0x62, a ScrollListMenu: Back, no help). One picked (SetParamNumber 4) plays title.nut's
    // manual_disp - flag 1122 on (the pictures through InterfaceWindow), GetTutorialID the pick:
    // its Tutorial_NNN's pictures (tutorialPic) and AnnounceTutorialTxt's pages in the help window,
    // a tap a page - then the list again (Update 3 -> 0); Back the options again.
    int manualStage = 0; // 0 none, 1 the list, 2 a tutorial playing
    struct ManualEntry {
        std::string title;
        int id = 0;
    };
    std::vector<ManualEntry> manualEntries;
    int manualCursor = 0;
    bool manualToList = true; // (the tutorial over: the list again, else the options - Tutorial_Option's)
    float manualStart = 0, manualClock = 0;
    std::unique_ptr<cr3::EventRuntime> manualEvents;
    void openManual();
    void closeManual();
    void playManual(int k);
    bool playTutorial(const char* function, int id, bool toList);
    void updateManual(float dt);
    void drawManualList(const Mat4& view, int w, int h);
    void drawOptionsAsk(const Mat4& view, int w, int h);
    int optionVolume[3] = {100, 100, 100}, optionFocus = 0; // (0..100: GetOptionWork's time the volume / 20)
    void drawOptions(const Mat4& view, const Mat4& proj, int w, int h);
    void drawChoosing(const Mat4& view, int w, int h);
    // The result window (ResultMenu: ResultTitle - `menu` with menuRslt_io -, ResultSeat -
    // seatRslt_io: once in, the EXP at call_dotNum_id01, 6 digits, and the SP at id11, 4, of set
    // 90 -, the two characters' CharaStatusWindowMenu - campWin with campWin2_io: each one's face
    // (campFace, cmpFaceC00<n> / A00<n>) at call_cmpFace_id000 / 500, name (pcNameEN_id at the
    // character's frame / 20 s) at call_pcNameEN_id021 / 521, labels at call_status_frNNN (the sign
    // NNN, less 500), level (3 digits of set 130) at call_dotNum_id031, HP (5 of 110: the most,
    // glyph 164's "/", the HP - right to left) at id041 / 100 / 051, MP (4 of 110) at 061 / 200 /
    // 071, the EXP to the
    // next level (6 of 110) at 081 - the second's 531 ... 581, 0.01 to the right but its level -,
    // their HP / MP / next bars (cmpHpBar_per ... at its percent / 20 s) -, and the system window
    // (sysWin_io) with "Victory!" (announce 16) at typeM_L01
    // CharaStatusWindowMenu (campWin): the party's windows - campWin2_io for two (`twoAlways`: so
    // for the result's, a lone one in the first), its third - a lone one's - hidden; campWin1_io
    // for one, the third's joints (id9000 ..) - each one's face (campFace, cmpFaceC00<n> / A00<n>)
    // at call_cmpFace_id000 / 500, name (pcNameEN_id at the character's frame / 20 s) at
    // call_pcNameEN_id021 / 521, labels at call_status_frNNN, level (3 digits of set 130) at
    // call_dotNum_id031, HP (5 of 110: the most, glyph 164's "/", the HP) at id041 / 100 / 051, MP
    // (4) at 061 / 200 / 071, the EXP to the next level (6) at 081 - the second's 5NN, 0.01 to the
    // right but its level -; the HP / MP / next bars at their percent / 20 s
    struct StatusShown {
        int id = 0, level = 1, hp = 0, hpMax = 1, mp = 0, mpMax = 0, next = 0, percent = 0;
    };
    void drawStatusWindows(const StatusShown* rows, int count, float t, const Mat4& view, const Mat4& proj, bool twoAlways = false);
    void drawResult(const Mat4& view, const Mat4& proj, int w, int h);
    void drawBattleHud(int w, int h);
    void drawBattleHudParts(int w, int h);
    void drawBattleHudMain(int w, int h);
    int dungeonNum() const;
    int areaNum() const;
    // The dungeon's map (FldDungeonMap -> DungeonMapMenu, interface 0x67): mapGuide_io with the
    // dungeon's guide_<t01 | dNN> on it; mapInfo with mapInfo_<..> at the area + 1 / 20 s, mapSymbol
    // (looping) at its call_mapSymbol - where the party is; the goals - each of the dungeon's
    // entries of the table at 0x535794 (from 0x535774's start) whose flag is set, mapGoal at
    // call_mapGoal with mapInfo at its place within the dungeon / 20 s. A tap closes it (0xb5).
    bool mapOpen = false;
    float mapStart = 0;
    void drawDungeonMap(const Mat4& view);

    // Saving (FldSaveLoad::ExecuteSave -> SaveConfirmationMenu, 0x4b, with the party's status
    // windows): "menu" with menuSave_io, the question (announce 0x69) - yes: 0xbd, "saved" (0x6a),
    // the game's data written (Cr3Util_Save_GameData; here CR3-U's own save, GameState::save, with
    // the map and the place, in the user's folder for the app - `--continue` resumes it), closed
    // after a second
    int saveStage = 0; // (1 asked, 2 saved)
    float saveStart = 0;
    bool saveFailed = false;
    static std::string savePath(const char* file = "save1.txt");
    // the scripts' saves (Cr3Save, FieldScript::onSave): the game's as the field's Save does, the
    // continue data (each area's entry - cont.txt, which --continue takes first), that emptied -
    // only as a game is played (--play / --continue, not a capture's)
    bool scriptSaves = false;
    void scriptSave(int kind);
    bool writeSave(const char* file = "save1.txt");
    // CR3's save (Fld2TabMenu::Save, the field's feBtnSave - tab 3: SE 0x16d, Fld2SaveData(-2) -
    // the dungeon, area, place and turn - and DBSystem::Sync, the game's data kept): CR3-U's save1.txt
    // with the area and the place; the title's tap takes it back up (TitleMain: CrxGameData::Load,
    // then the field where it was - Fld2Main::SM_FirstEvent's evt000)
    // (a game played from the title: saved where the game syncs - entering an area, Fld2Main's
    // SM_Init / SM_ChangeArea / SM_TownMove, back from a battle, the scripts' Sync and SavePoint -,
    // once the area's script is done and the field is idle)
    bool cr3Playing = false, cr3SavePending = false;
    float cr3SaveRetryAt = 0;
    static constexpr int kCr3SaveSlots = 12, kCr3SavePageSize = 3;
    static constexpr int kCr3SavePages = kCr3SaveSlots / kCr3SavePageSize;
    int cr3SaveSlot = 1; // the file played: save1.txt .. save12.txt
    std::string cr3SaveFile(int slot = 0) const;
    bool writeCr3Save();
    std::string readSaveText(int slot = 0);
    std::string cr3SavedArea();
    bool loadCr3Save();
    void restoreFieldPosition(const float* position, float yaw);
    bool continueSave(const std::string& binary, const std::string& dataPath, const char* only = nullptr);
    bool loadGameSave(const std::string& path, const std::string& binary, const std::string& dataPath);
    void saveAnswer(bool yes);
    void drawSave(const Mat4& view, const Mat4& proj, int w, int h);
    bool encountersOff = false; // (cr3_field_data +0x31: the tab's 5th, FldMain step 8 / 9)

    // The field's tab (FldTabMenu -> FieldMenuTabMenu: fldMenu_tab, over the field while nothing
    // runs): its own animation played forward to open it, back to close it (Cr3UtilSoundPlaySE
    // 0xba / 0xbb); its tap areas 01 the tab, 02 Camp, 03 the map, 04 Save, 05 the encounters'
    // switch - each but the tab only once it is open (CheckOpenMenu), 0xb4 as one is pressed. Camp
    // opens the camp (FldMain step 5, FldCamp); the others are not here yet.
    bool fieldTabOpen = false;
    float fieldTabAt = -10; // (uiClock: when it last began to open or close)
    bool fieldTabShown() const;
    float fieldTabPose(UiFig& tab);
    void drawFieldTab(const Mat4& view);
    void fieldTabTap(int area);

    // The camp (FldCamp -> InterfaceMain::SetCampWindowParameter): the party's status windows
    // (CharaStatusWindowMenu: 0x23 one, 0x24 two), the title (CampTitleMenu: menu, menuCamp_io),
    // the buttons (CampMenu: campBtn_io's circle buttons - 16 Items, selectable with any item;
    // 17 Equipment, 18 Sopia, 20 Options -, btlBack, campLnk's links), the Oz (ozWin_io: 7 digits)
    // and the clock (timeWin_io). Items (CampItemListMenu: by category - type 0 / 1, else 4 -,
    // item 36 first, each with its count, those the camp can use - Cr3UtilUseItemByCamp - lit) and
    // Sopia (CampSopiaListMenu: those had, by number) on the list; Options the options' window
    // (CampOptionMenu: as the battle's, its Manual the manual); Back (or Backspace) closes it.
    bool campOpen = false;
    float campStart = 0;
    int campList = 0; // (0 none, 16 the items, 18 the sopia)
    int campCategory = 0, campCursor = 0;
    void openCamp();
    void closeCamp();
    std::vector<ListRow> campRows();
    StatusShown statusOf(int ch) const;
    void drawCamp(const Mat4& view, const Mat4& proj, int w, int h);
    int campCursorButton() const;
    std::vector<int> validChars() const;
    cr3::BattleRules::PlayerStats statsOf(int ch) const;
    int mpOf(int ch) const;
    // a question on the system window (syswin_io's typeM_L01) with choice_io's Yes / No (call_cBtn_fr11 /
    // 12: circleBtn_fr at those frames) - their taps `yes` / `no`
    // CR3's text entry (Cr3System.ShowInputDialog: utils::ShowInputDialog -> GameDebug::ShowCUI ->
    // GUITools::OpenEditBox, the OS's own - Android's EditBoxActivity: an AlertDialog with the text,
    // the cursor at its end, CANCEL and OK, not cancelable otherwise): the screen dimmed, the box in
    // its upper half (a phone's keyboard under it), typed into (SDL's text input), Return its OK,
    // Escape its CANCEL (the text as it was), Delete the whole text. Up to 16 characters for the
    // name, 24 for the others (ShowInputDialog's lengths)
    struct YesNoRectBox {
        float x0, y0, x1, y1;
    };
    struct InputDialog {
        bool open = false;
        int kind = 0;
        std::string text, was;
        float since = 0;
        YesNoRectBox ok{}, cancel{};
    } inputDialog;
    bool japaneseText = false; // (the release's Japanese, no English over it: the box's hint in Japanese)
    bool inputDialogShown() const;
    static size_t codePoints(const std::string& t);
    void syncInputDialog();
    void closeInputDialog(bool ok);
    void typeInputDialog(const char* utf8);
    bool inputDialogKey(SDL_Keycode k);
    bool inputDialogClick(float fx, float fy);
    void drawInputDialog(int w, int h);
    // An event's yes / no buttons (EvtWindow.ShowYesNoButton - drawYesNo): their tap areas kept
    // for a click
    struct YesNoRect {
        bool yes;
        float x0, y0, x1, y1;
    };
    std::vector<YesNoRect> yesNoRects;
    int yesNoCursor = 0;
    float yesNoSeenAge = -1;
    void drawYesNo(cr3::EventRuntime& ev, int w, int h);
    // CR3's choice menus (Msg_Choice2 / 3 - SelectTwo/ThreeChoicesMenu, interface event): no dimming;
    // comMsgC<n>Position, comMsgC<n>Menu at its call_comMsgC<n>Menu with the options' texts at the
    // menu's typeM_L joints - two's a line or two each (option k's at 2k + 1, 2k + 2; one line alone
    // 0.24 off to the middle), three's one each at k + 1 -, all fading in over 0.1333 s
    // (SetAllAlpha); the options' comMsgC<n>Panels at its
    // call_comMsgC<n>Panel are hidden - only their tap areas -, comBtnEffe at a tapped one's
    // call_comBtnEffe plays until the menu closes
    // A flat menu quad cut in three across - its ends as drawn, the middle between them - so it can
    // be widened without its corners and border stretching: the fan translations' choices (a
    // bubble a long line needs wider on a screen wider than 3:2). `widen` its ends moved out, each
    // that far in the quad's own units; the quad's columns (left, cut, cut, right) kept to move from
    struct SlicedQuad { bool sliced = false; float x[4] = {0, 0, 0, 0}; std::vector<int> column; float widen = 0; };
    std::map<std::string, SlicedQuad> slicedQuads;
    float sliceUiQuad(const std::string& name, ViewFigure& f, float cap);
    void widenUiQuad(const std::string& name, ViewFigure& f, float widen);
    std::vector<YesNoRect> choiceRects; // (yes: unused; their order the options')
    void drawChoice(cr3::EventRuntime& ev, int w, int h);
    void answerChoice(int index);
    bool choiceShown();
    // The Vita's cursor over them (FUN_8128d1ea: ↑ the one before, ↓ the one after, round, com_002 -
    // the first to begin with; ○ the one under it, com_003): with the Vita copy as the game
    int choiceCursor = 0;
    bool choiceKey(SDL_Keycode k);
    bool choiceClick(float fx, float fy);
    void answerYesNo(bool yes);
    bool yesNoShown();
    bool yesNoKey(SDL_Keycode k);
    bool tutorialShoulder(bool right);
    bool yesNoClick(float fx, float fy);
    void drawAsk(const std::string& message, float t, const Mat4& view, int w, int h, int yes, int no);
    void drawStatusB(int ch, int hpChange, int mpChange, float t, const Mat4& view, const Mat4& proj);
    void drawCapacity(int ch, const int* change, float t, const Mat4& view, const Mat4& proj, int w, int h);

    // Using an item (CampItemListMenu 9 -> CharaSelectButtonMenu, 10 -> CampItemDirectUseMenu):
    // whom - faceWin_io's faceBtn per character (its texture faceBtn_00<n>), faceSlct on the one
    // focused, campHelp_io's "on whom" (announce 0x84), its window and parameters with what the
    // item would add; a tap on another focuses it (0xb3), on the focused uses it (0xb4) - one
    // used up first asks how many (shopNum_io: tap 01 one fewer, 02 one more, 1 .. its count)
    // and Yes / No (announce 0x74); Emergency Exit (36) asks (0x73), then leaves for t0101_p1
    int campItem = 0;  // (the item being used; 0 none)
    int campUse = 0;   // (1 whom, 2 how many and yes / no, 3 Emergency Exit's yes / no)
    int campFace = 0;  // (the one focused: its place among validChars)
    int campCount = 1;
    float campUseStart = 0;
    void useItemOn(int ch, int item, int n);
    void campUseTap(int id);
    void drawItemUse(const Mat4& view, const Mat4& proj, int w, int h);
    void campPickItem(int item);
    void drawSopiaWindows(int sopia, float t, const Mat4& view, const Mat4& proj, int w, int h);

    // The equipment (CampEquipmentListMenu, with CharaStatusWindowBMenu / CharaStatusCapacityMenu
    // and CampEyeButtonMenu's character buttons - eyeBtnPos, eyeBtn_id at the character's number
    // / 20 s at call_eyeBtn_id<n>, eyeSlct and eyeCsr_loop on the one chosen; a tap another, 0xb7).
    // Its view: the weapon, the armour, the accessory (menu name 0x4b, none to wear: half), the
    // character's own sopia (not to change), Darwin's human sopia (0x4d), the three monster sopia
    // (0x4e .. 0x50) and the skills (0x54); a row its list - the items of its kind (+0x10: 1 2 3)
    // had that the character may wear (+0x12's bit), the one worn lit yellow and fixed, those all
    // worn by others half; the sopia (human: those had past Darwin's own whose flag 0xbe1 + n is
    // set; monster: +0x10 1 - one of another's taken from them after a question, announce 0x8a);
    // "Remove" (0x53) when the place holds one. A choice: Cr3UtilChangeEquipItem / Sopia (199).
    int campEquipChar = 0, campEquipMode = 0, campEquipCursor = 0;
    int campTakeSopia = 0, campTakeFrom = -1; // (a sopia another wears, asked for)
    struct EquipRow {
        int id = 0; // (300 + the mode a view row opens; an item; 400 + a sopia; 399 Remove)
        std::string name;
        bool selectable = true, worn = false;
        int sopia = 0; // (what the sopia windows show for it)
    };
    std::string menuName(int id);
    std::vector<EquipRow> equipRows();
    std::vector<int> humanSopia() const;
    void campEquipPick(int id);
    void clampMp(int ch);
    void campTakeAnswer(bool yes);
    void campEquipBack();
    void campEquipChange(int dir);
    void drawEquipment(const Mat4& view, const Mat4& proj, int w, int h);

    // The camp's links (CampPubLink, campLnk: tap 01 Scenarios - 201, CampMenu's result 8, flag
    // 0x41d cleared; tap 02 the pub - 202, only with a request taken, cr3_global_work 7, flag 0x41e
    // cleared - its colour half without one)
    // Scenarios (ScenarioFlowMenu, 0x73): scenariosBG - its panels GEO_panel0<n> shown by their
    // flags (the table at 0x5348c4 / 0x5348f4), its chapters' covers GEO_chapter<n> while their
    // flags are clear (0x534924 / 0x53492c) and their names (menu names 14 .. 17 at typeM_L01 ..)
    // once set -, snrHi_loop / snrLow_loop, "menu" with menuSnr_io, the part opened (percent_io: 3
    // digits of set 80 - flags 1 .. 4 and 151 .. 192 had, of 46), Back
    // The pub's request (CampPubDetailMenu, 0x6d): pubWin_io with the request's text (PUBTxt: its
    // number + 60, a line each "\n" at typeS_L01 ..), pubGirls_io, campHelp_io's line (its number), Back
    int campLink = 0; // (201 the scenarios, 202 the pub's request)
    float scenarioScroll = 0; // (ScenarioFlowMenu::TouchSimpleMove: scenariosBG raised 0 .. 0.704 - 1.472 with flag 1 - by a drag)
    void scrollScenarios(float dy);
    void drawScenarios(const Mat4& view, const Mat4& proj, int w, int h);
    void drawPubRequest(const Mat4& view, int w, int h);
    void campPress(int id);
    void campKey(SDL_Keycode k);
    bool fieldClick(float fx, float fy);
    bool fieldToken(const std::string& t);
    // The leader's figure (FldPlayer::CreatePlayer: c + its character's 001..007), the field's
    // character the script has leading - unless --model gave one
    bool leaderFollows = false;
    void refreshLeader();
    // The character change (FldChangeCharacter: CharaTradeMenu's chrTrade button, shown in the
    // field once allowed - Cr3ChangeCharacter.SetEnable - and flag 0x3fb set, the second of the
    // two there; its tap SE 0xb4): FldMain::ChangeCharacterExecute - the leader darkened over 16
    // frames (its materials' diffuse 1 - n / 16), then the other in its place (its idle), lit over
    // 32 (n / 32)
    int changeStage = 0; // 0 none, 1 out, 2 in
    float changeFrames = 0;
    bool changeButtonShown() const;
    void startChange();
    void updateChange(float dt);
    void drawChangeButton(const Mat4& view);

    // The world select (FldWorldSelect - InterfaceMain::SetMapSelectParameter: MapSelectMenu, a
    // scroll list, with MapSelectTitleMenu's `menu` on menuWld_io and MapSelectPicMenu's `mapPic`
    // wearing the focused dungeon's mapPic_dNN). Its rows (SetMapList): the dungeons whose flags
    // are set (0x534510: 1041 ..), in the table's order (0x534544: 1, 9, 6, 7, 8, 4, 3, 5, 2, 10 ..),
    // named by announcemenuname (the dungeon's number); picked, the levels (SetLevelList: Lv. 1 ..
    // 11, those whose flags 0x5345a8 say - announcemenuname 0x14 + the level); picked, the
    // question (the System window: the dungeon, the level, announce 0x80 - yes / no). Yes: the
    // dungeon chosen (GetSelectDungeonNum), the difficulty the level less one (Cr3UtilSetDifficulty),
    // the town's WarpTask called; no: the levels again; back: the dungeons, then the field.
    //
    // The chapter select (the town's book: ReserveChapter - FldMain step 0xe, FldChapterSelect;
    // InterfaceMain's ChapterSelectMenu / ChapterSelectTitleMenu - `menu` on menuCpt_io - /
    // ChapterSelectPicMenu - `mapPic` wearing mapPic_cNN, the focused row's place + 1) works the
    // same way: its rows (SetChapterList) the chapters whose flags are set (0x5355a8: 11, 18, 19,
    // 20), their ids 0x5355b4 (1, 5, 7, 8), named by announcemenuname 0x5355c4 (14 .. 17); picked,
    // the question (the System window: the chapter, announce 0x8b). Yes: the chapter chosen
    // (InterfaceWindow.GetChapterID), the town's chapter_select called; no: the chapters again;
    // back: the field.
    struct WorldSelect {
        bool chapters = false; // (the chapter select)
        int stage = 0; // 0 shut, 1 the dungeons, 2 the levels, 3 the question, 4 the pair's question
        float at = 0;  // (since: uiClock)
        int focus = 0, dungeonFocus = 0;
        int dungeon = 0, level = 0;
    } world;
    std::vector<ListRow> worldRows();
    void openWorldSelect();
    void openChapterSelect();
    static bool worldPair(const cr3::GameState& g, int dungeon, int pair[2]);
    void worldPick(const ListRow& r);
    void worldBack();
    void worldAnswer(bool yes);
    void worldTap(int id);
    void worldKey(SDL_Keycode k);
    void drawWorldSelect(const Mat4& view, int w, int h);

    // The library (a shelf: ReserveLibrary - FldMain step 0xf, FldLibrary; InterfaceMain::
    // SetLibraryParameter - ArchiveWindowMenu, its ArchiveListMenu): the shelf's entries
    // (SetArchiveList: those of its category - InterfaceWindow.SetArchiveID - from
    // ArchiveCategoryStartTable[c - 1] to [c], each shown if its ArchiveOpenFlagTable flag is none
    // or set; entry k's id k + 1, its title LibraryTxt's), each with main icon 69 and the seal
    // "new" (2) while flag 1350 + id is set (SetListItemParam). Picked, the list shut and the
    // area's library_disp called (InterfaceEvent::CallScript: the entry - GetLibraryID - read in
    // window 6, its flag cleared), then the list again; Back: the field.
    struct Library {
        int stage = 0; // 0 shut, 1 the list, 2 an entry being read (library_disp)
        int focus = 0;
        float at = 0;
    } library;
    std::vector<ListRow> libraryRows();
    void openLibrary();
    void libraryPick(const ListRow& r);
    void libraryBack();
    void libraryTap(int id);
    void libraryKey(SDL_Keycode k);
    void drawLibrary(const Mat4& view, int w, int h);

    // The shop (Piupiu's: ReserveShop - FldMain step 0xb, FldShop; InterfaceMain::
    // SetFieldShopParameter): ShopTitleMenu's `menu` (menuShop_io), Piupiu's cut-in (cutIn on
    // cutInSgl_io, wearing cutIn102_L02, its GEO_face parts hidden), the Oz window; ShopFirstMenu
    // (shopBtn_io: circle buttons 13 talk - the town's shop_talk -, 14 buy, 15 sell, btlBack);
    // ShopSecondMenu (shopCmd_io: 16 items, 21 weapons, 22 armour, 23 accessories); the lists
    // (ShopItem / ShopWepon Buy / SellListMenu: the shop's rank the flags 1081 .. 1085 set - those
    // items of the kind (+0x10) whose rank (+0x2c) is no higher, an equipment's for the character
    // shown (its wearers, +0x12), at their price (+0x24) - greyed past the Oz, or held 99 -; to sell,
    // those held, at +0x28 - an equipment's greyed when none is spare of what is worn); then how many
    // (CampChooseQuantityMenu: shopNum_io, 1 .. GetItemBuyUpLimit - the Oz's worth, 99 less those held -
    // / the spare ones; pliceNum_io the whole price) and the System window's question (announce 0x70 /
    // 0x71: the item, those held): yes - the items and the Oz (SE 200).
    struct Shop {
        int stage = 0; // 0 shut, 1 buy / sell / talk, 2 the kinds, 3 the list, 4 how many and the question
        bool buy = true;
        int kind = 0;     // (0 items, 1 weapons, 2 armour, 3 accessories)
        int focus = 0, item = 0, count = 1, who = 0;
        float at = 0;
    } shop;
    // The pub (ReservePUB - FldMain step 0xd, FldPUB; InterfaceMain::SetPubParameter: PubTitleMenu's
    // `menu` on menuPub_io, PubTopMenu, PubMainMenu's pubGirls_io): the board (posterBtn) - a
    // pubPoster_io at each call_pubPoster_ioNN for the requests posted (flags 1201 .., nine at
    // most), the one taken (Cr3UtilGlobalWorkGet(7)) its checkMark_io, a new one (flag 1250 + id)
    // its seal_id; a tap focuses one (SE 0xb3: MTR_posterL at 1.5, faceSlct on it, its title -
    // PUBTxt id - in campHelp_io), another on it picks it (SE 0xb4, its "new" flag cleared); Back
    // (btlBack) ends it (3: the field again). Picked, PubDetailMenu: pubWin_io with its text (PUBTxt
    // id + 60, a line each "\n"), its title, accept_io (or giveUp_io for the one taken: its circle
    // button), Back to the board. Accepted: the request taken (work 7), 1 - the town's pub_checkA;
    // given up: the System window's question (announce 0x83), yes - none taken, 2 - pub_checkG.
    struct Pub {
        int stage = 0; // 0 shut, 1 the board, 2 a request, 3 giving it up?, 4 the list
        int focus = 0, request = 0, listFocus = 0; // (listFocus: the list's request, 0 none)
        float at = 0;
    } pub;
    std::vector<int> pubPosters() const;
    int pubListCount() const;
    int pubSign(int id) const;
    void openPub();
    void pubEnd(int result);
    void pubTap(int id);
    void pubKey(SDL_Keycode k);
    void drawPub(const Mat4& view, int w, int h);
    void openShop();
    int shopRank() const;
    std::vector<int> shopChars() const;
    int spareCount(int id) const;
    std::vector<ListRow> shopRows();
    int shopLimit() const;
    void shopClose(const char* call = nullptr);
    void shopTap(int id);
    void shopKey(SDL_Keycode k);
    void shopToken(const std::string& t);
    void drawShop(const Mat4& view, const Mat4& proj, int w, int h);

    // The party (the town's c007: ReserveParty - FldMain step 0xc, FldPartyOrganize;
    // PartyOrganizationMenu): its console (`console_io`: circle buttons 25 Organize, 13 Talk - the
    // town's party_talk -, btlBack - the field); organizing (SetSelectActorScene: `partyBtn` on
    // partyBtn1_io, a circle button for each of the two - its character's frame, + 1, and level -
    // or none, `partyUnder`, the two's portraits - cutIn00N_L01, 5's cutIn006_L02 - on the cut-in
    // layouts 4 and 3, btlBack - the town's party_end); a slot picked (SetHeroSelectScene: partyBtn2_io,
    // its one's button and portrait; SetHeroSelectFaceScene: `faceWin_io` with a `faceBtn` - wearing
    // faceBtn_00N - at call_faceBtnN for each who can join (flags 1011 + the character) and isn't in
    // the two, faceSlct on the focused): a face picks that slot's character (cr3_common_data +3 / +4).
    struct PartyMenu {
        int stage = 0; // 0 shut, 1 the console, 2 the two, 3 a slot's choice
        int slot = 0, focus = 0;
        float at = 0;
    } party;
    std::vector<int> partyChoices() const;
    void partyTap(int id);
    void partyKey(SDL_Keycode k);
    void drawParty(const Mat4& view, const Mat4& proj, int w, int h);
    void drawFieldUi(int w, int h);
    // battery_id at a frame by the device's power (BatteryWindowMenu::Update, every 20 frames:
    // charging - 0.35 full, 0.3 under 30%, 0.25 under 60%, else 0.2; charged 0.2; discharging 0.15
    // under 30%, 0.1 under 60%, else 0.05; unknown 0) - SDL's power info; a machine without a
    // battery as charged
    bool batteryLow = false;
    float batteryCheckedAt = -100, batteryFrameAt = -100, batteryFrame = 0.2f;
    void updateBattery();
    void drawBattery(const Mat4& view);
    void drawBalloons(int w, int h);
    bool attentionShown = false;
    float attentionAt = 0;
    void drawFlagMarks(int w, int h);
    void drawIntro(int w, int h);
    // The vista's slash (VistaEvent.ExecutionStart / ExecutionEndStart - EvtUtilVistaExecution*:
    // InterfaceMain::SetEnforecementEffectParameter 0 / 1, FieldGimickEffect07Menu, interface 0x45
    // / 0x7b): `kill` (`finale`) - kill_io in, kill_wait looping - and redCsr_loop (blueCsr_loop)
    // at its place. A swipe through it (TouchSimpleRelease: the place's projection on the press ..
    // release line within it, and the slide area): `execution` (`catastrophe`) once, SE 0x15c,
    // then kill_io backwards and CheckExecution true (EndCheckEnforecementEffectWindow 1); a swipe
    // past it: kill_io backwards, then the whole again (2). `--input slash` / `slashfail`
    struct VistaSlash {
        int stage = 0; // 0 none, 1 in / waiting, 2 the hit's figure, 3 out
        bool end = false, hit = false, pressed = false;
        float at = 0, x0 = 0, y0 = 0;
    } vslash;
    void updateVistaSlash();
    void vslashEnd(bool hit);
    bool vslashPlace(float& x, float& y);
    void vslashPointer(float fx, float fy, int phase);
    float uiDuration(const std::string& anim, const std::string& fig = "");
    void drawVistaSlash();

    // ---- the battle's interface (CR3: CRXParts figures at 100 pixels a unit) ----
    // a figure posed (its own animation - or `anim`, an animator beside it - at t) at (x, y)
    // a list's focused row (comListItem::SetSelectFlag): it and all on it 1.1 x, its name white -
    // set for the row's drawing by a Cr3FocusRow
    float cr3UiScale = 1;
    float cr3UiZ = 0; // (a part raised toward the camera: over a card)
    bool cr3UiAlphaSet = false; // (the alpha its materials' outright - PartsBase::SetAlpha, over a file's 0 - not a tint)
    float cr3UiTint[3] = {1, 1, 1}; // (SetMaterialDiffuseColor over the part: a category button not in focus)
    // (FUN_8132d3ea: one material's DiffuseColor times this - the Vita's cursor on a field button's mat_fe_parts_01)
    std::string cr3UiTintMaterial;
    float cr3UiMaterialTint[3] = {1, 1, 1};
    bool cr3UiFocus = false;
    // (an item not enabled - AddListItem's bool: a battle row not usable, a quest whose item isn't
    // held - only the row, without its cursor or the white; comListItem::Initialize colours its
    // parts (0.549, 0.549, 0.655) and its name half as bright - 0.107 against 0.215)
    bool cr3UiDim = false;
    struct Cr3FocusRow {
        Viewer& v;
        Cr3FocusRow(Viewer& viewer, bool focused, bool enabled = true) : v(viewer) {
            v.cr3UiScale = focused ? 1.1f : 1.0f, v.cr3UiFocus = focused && enabled, v.cr3UiDim = !enabled;
            if (!enabled) v.cr3UiTint[0] = v.cr3UiTint[1] = 0.549f, v.cr3UiTint[2] = 0.655f;
        }
        ~Cr3FocusRow() { v.cr3UiScale = 1, v.cr3UiFocus = v.cr3UiDim = false, v.cr3UiTint[0] = v.cr3UiTint[1] = v.cr3UiTint[2] = 1; }
    };
    static float cr3ListRowX(float x, int k, int focus);
    static float cr3ListRowY(float y, int k, int top, int focus);
    UiFig& cr3UiPose(const std::string& name, const std::string& anim, float t, float x, float y, bool loop = false);
    UiFig& cr3UiPut(const std::string& name, const std::string& anim, float t, float x, float y, const Mat4& view, float alpha = 1, bool loop = false);
    UiFig& cr3UiPutOverOwn(const std::string& name, const std::string& anim, float t, float x, float y, const Mat4& view, bool loop);
    bool cr3UiCall(const UiFig& u, const std::string& prefix, float& x, float& y, int w = 0, int h = 0);
    void cr3UiTexture(UiFig& u, const std::string& image, const std::string& mask = {});
    void cr3SpriteNum(int value, int places, float x, float y, int w, int h, const float rgb[3]);
    std::string cr3MonsterName(int id);

    // The gene lab's screen (EventRuntime::GeneLab: GeneSynthesisMain and its company). Behind the
    // clerk: shBgMenu at shBgPositon's call_shBgMenu_fit0. Over it: the header (HeaderMenu 0x42 -
    // comHePosition sliding in by comHePosition_io, comHeMenu at its fit7 with the title
    // comHeTitle_fr00 at frame 30 (37 in VS), 31 choosing genes, and the help line at its typeM_L01
    // (SetHeaderSentence: AnnounceMenuName's), comHeIconMenu's battery, comHeInfoAMenu's money
    // (comHeMlPanel: 7 comHeNum_fr00 digits, frames 0..9), clock (comHeTimePanel: HHMM, frames 30..,
    // the hours' tens hidden under 10) and Oz coins (comHeCoinPanel: 5, frames 20..) - InterfaceMain::
    // SetViewNumbers: leading zeros hidden); the buttons (comBtnMenu 0x2c, from slot 004 on:
    // SetGeneSynthesisTopButton's Talk 17, Combine Genes 28, Sell 21, Gene Repository 29 - the last
    // three off while flags 3613 / 3614 / 3615 are set; Combine's SetGeneSynthesisSecondButton:
    // Combine 30, Enhance 31 - Friends 32 only with friends, none offline); the footer and its back
    // button. Combine Genes (GeneSynthesisMain 0xb ..): the clerk away, the base gene chosen from
    // the cards (GeneChangeMenu), then the material from the merges it could make (GeneSynthesisList
    // Menu: their results, by the footer's grade tabs) beside the two (NormalSynthesisMenu - the mix
    // button shows the result's card instead), asked (AnnounceMenuName 0x175), merged (DBEmu
    // MergeGene), its card shown
    struct Cr3GeneLabTap {
        float x0, y0, x1, y1;
        int id; // (a button's: 17, 28, 21, 29, 30, 31; -1 back; kTapCard + n a card, kTapRow + n a row, kTapTab + n a tab; kTapMix the mix button; kTapYes / kTapNo)
    };
    static Cr3GeneLabTap circleTap(float cx, float cy, float ax, float ay, int id);
    static constexpr int kTapCard = 1000, kTapRow = 2000, kTapTab = 3000, kTapMix = 4000, kTapYes = 4001, kTapNo = 4002, kTapOk = 4003, kTapAlert = 4004;
    std::vector<Cr3GeneLabTap> geneLabTaps;
    bool geneLabWasOpen = false;
    int geneLabFocus = 0;
    float geneLabClock = 0; // (since it opened: the header's and buttons' io)
    struct Cr3GeneMenu {
        // 0 the lab's buttons, 1 Combine's, 2 the base gene (GeneChangeMenu), 3 the material
        // (GeneSynthesisListMenu, NormalSynthesisMenu), 4 asked, 5 the merge's flash, 6 its card;
        // 7 the genes picked (Enhance's materials, Sell's, the repository's), 8 asked about them;
        // 9 the Gene Repository's buttons
        int screen = 0;
        int mode = 0;            // (0 Combine, 1 Enhance, 2 Sell, 3 store, 4 take out, 5 expand the repository)
        std::vector<int> pickRows; // (7: the genes listed - cr3Genes indices, by the tab)
        std::vector<int> picked; // (7: those checked - cr3Genes indices, 20 at most)
        std::string alert;       // (AlertwindowMenu's text over it - SetAutoEndAlertWindowMenu: gone by itself)
        float alertSince = 0;
        float since = 0;         // (the screen's clock)
        std::vector<int> genes;  // (2: the cards - cr3Genes' held ones, in order)
        int focus = 0;           // (2: the card in focus)
        int base = -1;           // (3 ..: the base, its cr3Genes index)
        std::vector<int> rows;   // (3: the merges listed - mergePreviews() indices, by the tab)
        int tab = 0;             // (3: GeneSynthesisFutter's tab - 0 ALL, 1 .. 3 N, R, SR: the result's grade + 1)
        int row = 0;             // (3: the row in focus)
        bool result = false;     // (3: the mix button's - the result's card over the two)
        int made = -1;           // (6: the new gene - its cr3Genes index)
        int lights = 2;          // (5: GeneSynthesizeEffect's gene lights - a merge's two, an enhance's fed + its own)
        float effectEnd = 0;     // (5: the camera's animation's length - the effect over)
        std::string message;     // (an alert's, over it)
        bool leftFocus = false;
        int materialFocus = 0;   // base, material, Mix
    } geneMenu;
    bool geneInspectOpen = false;
    cr3::Cr3Gene inspectedGene;
    void inspectGene(int master, int held = -1);
    bool geneInspectionKey(SDL_Keycode key);
    void geneMenuScreen(int screen);
    std::vector<int> geneLabButtons() const;
    int geneLabHeld() const;
    bool geneLabButtonOn(int id) const;
    std::string menuAnnounce(int id);
    std::string announceText(const std::string& file, int id);
    void cr3UiDigits(const UiFig& panel, const std::string& prefix, const std::string& figure, long value, int places, int offset,
                     const Mat4& view, bool hideLead = true);
    void cr3UiNumber(const UiFig& panel, const std::string& prefix, const std::string& fig, long value, int places, int offset, const Mat4& view,
                     float alpha = 1, int maxFrame = -1, int suffix = 3);
    // a figure's meshes of one material drawn with another image (PartsBase::SetMaterialColorSampler;
    // its own where the release has no such image)
    std::map<std::string, std::vector<cr3::Texture*>> uiOwnTextures;
    float cr3UiIcon(UiFig& u, int icon);
    void cr3UiMaterial(UiFig& u, const std::string& material, const std::string& image);
    void drawCr3Card(int master, float x, float y, float z, float scale, const Mat4& view, float alpha = 1, bool selectable = true, float roll = 0, float spin = 0);
    void drawGeneLabSentence(const UiFig& menu, const std::string& text, int w, int h);
    void drawCr3Header(float t, int title, const std::string& sentence, int w, int h, const Mat4& view);
    bool geneLabVs = false; // (the lab opened from the VS mode: its title 37, Talk off)
    void drawCr3GeneLab(cr3::EventRuntime& ev, int w, int h, bool front);
    void drawGeneBase(int w, int h, const Mat4& view, const cr3::Cr3Gene* inspected = nullptr);
    void drawGeneRowParts(const UiFig& mixing, int master, int level, int maxLevel, int gold, const Mat4& view, float alpha, int w, int h, int style = 0);
    void geneMenuRows();
    void drawGeneMaterials(int w, int h, const Mat4& view);
    bool geneBaseSelectable(const cr3::Cr3Gene& g) const;
    void geneMenuPickRows();
    void cr3UiSlotNumber(const UiFig& panel, int slot, long value, int places, int offset, const Mat4& view, bool top = false);
    void drawCr3ListDigits(const UiFig& num, int value, float alpha, const Mat4& view);
    void drawCr3ListEnds(float lx, float ly, int n, int focus, const Mat4& view);
    void drawCr3ListBar(const UiFig& menu, bool shortList, int top, int n, int visible, const Mat4& view, float lowExtra = 0);
    void cr3UiLabel(const UiFig& panel, const char* joint, const std::string& text, int w, int h, float shade = 1.0f);
    void drawGenePicks(int w, int h, const Mat4& view);
    void drawGeneAlert(int w, int h, const Mat4& view);
    void geneLabAlert(int id);
    void drawGeneAsk(int w, int h, const Mat4& view);
    // the merge (GeneSynthesizeEffect 0xb2, the field hidden): the scene through Cam_ue002jene01's
    // camera EV1000 - c115_ue002jene01 (the clerk) and ue002jene01 (the lights' stage) each on its
    // own animation; ue002jene02, a gene's light, at the stage's g_1 .. g_N joints (a merge's two at
    // g_4 and g_5 - SetParamNumber 5; an enhance's fed genes from g_1 and its own at g_d - 6, 7), each
    // with the joint's whole transform; the new card at g_result (and its move offset, -0.087 across)
    // from the clerk's animation's end - 2.4 s; over at the camera's end or a tap. Then the new gene's card, 9 x, its name under it; a
    // tap back to the lab's buttons
    cr3::Cr3BattleCamera geneEffectCam;
    bool geneEffectCamLoaded = false;
    bool cr3JointMatrix(const UiFig& u, const std::string& joint, cr3::Mat34& out);
    float geneEffectLength();
    void drawGeneEffect(int w, int h, const cr3::Cr3Gene& made);
    void drawGeneMerge(int w, int h, const Mat4& view);
    void geneMenuOpenBase(cr3::EventRuntime& ev, int mode);
    void geneMenuOpenPicks(int mode);
    void chooseGeneLab(cr3::EventRuntime& ev, int id);
    bool geneLabClick(float fx, float fy);
    bool geneLabKey(SDL_Keycode key);

    // ---- CR3's field buttons and camp ----
    // The field's buttons (CampFieldMenu: feBtnMenu at feBtnPosition's fit9, its feBtnPanel at
    // call_feBtnPanel - feBtnPanel_in's 0 .. 0.133 s opening, .. 0.3 closing - and on it feBtnOpen,
    // feBtnSave, feBtnEncount, feBtnBattle, and with flag 27 feBtnCamp and feBtnMypage, with 26
    // feBtnShop, each at its call_ joint - the game's names one round from their pictures and
    // what they do: feBtnShop is Camp, feBtnCamp My Page, feBtnMypage the SpShop (TapChkFieldTabMenu 1,
    // 6, 7: Fld2Main states 6, 9, 10) -; feBtnTown at fit7 unless flag 1001). Camp (Fld2Main::SM_Camp
    // - SetCampWindowParameter): CampTopMenu (0x33: comBgMenu, camTopCharaMenu's five
    // camTopCharaPanel - a party member's gene: its picture cutin_<series> on camTopGene_id00's
    // mat_cutin_001, camTopStatusPanel's level, HP and MP; empty slots camTopNoEntory_fr00 -, the
    // frame camTopFramMenu), CampcomBtnMenu (0x5d: comBtnSMenu at comBtnSPosition's fit6, buttons 0,
    // 1, 2, 5, 4, 3, 6 from slot 004 - comBtnSPanel, comBtnIconS_fr00 at the id's frame; 2, Genes,
    // off in VS and with flag 21), CampFutterMenu (comFtrTeamMenu, the back). Genes (InterfaceMain::
    // Update 0x1d, 2: CreateGeneCharaListMenu): the party's characters (GeneCharaListMenu - comListMenu
    // rows of comListLGene: the character's icon and name, its gene's picture, name and level), then
    // a character's gene from the cards (GeneChangeMenu, camp: those worn by others marked with their
    // camEquChr_fr00), the card in focus tapped again worn
    struct Cr3Camp {
        int stage = 0;          // 0 the field, 1 the camp's top, 2 Genes: the characters, 3 a character's gene
        float since = 0;
        bool panelOpen = false; // (the field's buttons: feBtnOpen's panel - closed to begin with)
        float panelAt = -10;
        int chara = 0;          // (3: its char_para id)
        int row = 0;            // (2: the row in focus)
        std::vector<int> genes; // (3: the cards - cr3Genes indices)
        int focus = 0;
        int button = 0;         // (1: the button in focus)
        int panel = -1;        // (Vita's top: -1 the buttons, otherwise a party panel)
        bool confirmHeld = false;
        bool geneButton = false;
        int geneSkill = -1;
        int geneTake = -1, geneAskFocus = 1;
        int geneSort = 0, geneTab = 0;
        int barCursor = 5;      // (the Vita's cursor over the field's buttons - kCr3BarIds' place; 5 Camp)
    } cr3Camp;
    static constexpr int kCampButtons[7] = {0, 1, 2, 5, 4, 3, 6};
    std::vector<Cr3GeneLabTap> cr3CampTaps;
    std::vector<Cr3GeneLabTap> cr3CtnTaps; // (the battle's continue: its buttons, its question's)
    bool cr3FieldIdle();
    std::vector<int> cr3PartyMembers() const;
    bool cr3CampButtonOn(int id) const;
    std::vector<float> cardUVs;
    float cardUV(int series, int k);
    // The town map (TwonMoveMenu, interface 0x37 - Fld2Main::SM_TownMove): from the field's
    // feBtnTown (Fld2TabMenu tab 8: SetDirectTownMove) or a script's ReserveTownMove (its
    // SetPopPoint2 where a cancel goes). TownQuestClearNEXT runs first; then the town's painting
    // (t0100plane on a 5.945 plane - 344.5 / 118 UI units each: 17.36 across -) under its places'
    // points: each open place (its flag, 0xf6995c) a spPointMenu at its joint of t0100town00 (scaled
    // 2.5929 across: x 3.8, y 10 times, by 890 / 1100), spPointOn on the focused one, spPointOff on
    // the others, spIconMenu there with spIcon_fr00 (0xf69ab4) and comListHit (0xf69ae0 / 0xf69b0c).
    // It opens on the list (Update 3: WorldSelectListMenu's rows - the places in 0xf69b38's order,
    // pictures 0xf69b90 by place, names AnnounceMenuName 0x92 + n, the hit mark, New 0xf69b64); a
    // row focused brings its point to (-3, 0), half the way each update. Back: the map alone
    // (SetReturnTownMap: dragged about, its points and spListBtn tapped); back again leaves. A place
    // chosen: the System window asks (0x9f with 0x91 + its id) - no: back where it was; yes: a
    // 30-frame fade, two seconds, then its map (Fld2Main 4: 0xf3fb1c by id - Fld2TaskMap's
    // indices, 0xf3fb4c with flag 2401 and not 2500 -, the monorail's (25) MonorailSound), the pop
    // point cleared. Left: SetPopPoint2's point, else the field as it was.
    static constexpr int kTownPoints = 11;
    static constexpr int kTownFlag[kTownPoints] = {1043, 1044, 1042, 1041, 1047, 1045, 1046, 1048, 1067, 1068, 1069};
    static constexpr int kTownIcon[kTownPoints] = {18, 18, 19, 20, 21, 22, 0, 0, 23, 24, 0};
    static constexpr int kTownHit[kTownPoints] = {1073, 1074, 1072, 1071, 1077, 1075, 1076, 1078, 1097, 1098, 1099};
    static constexpr int kTownHit2[kTownPoints] = {1173, 1174, 1172, 1171, 1177, 1175, 1176, 1178, 1179, 1180, 1299};
    static constexpr int kTownNew[kTownPoints] = {1163, 1164, 1162, 1161, 1167, 1165, 1166, 1168, 1169, 1170, 1160};
    static constexpr int kTownOrder[kTownPoints] = {2, 0, 1, 3, 4, 5, 6, 7, 8, 9, 10};
    static constexpr int kTownImage[kTownPoints] = {20, 18, 19, 21, 22, 23, 24, 25, 26, 27, 0}; // (by the list's place)
    static constexpr int kTownId[kTownPoints] = {1, 2, 3, 4, 5, 6, 100, 100, 9, 10, 11};
    static constexpr const char* kTownJoint[kTownPoints] = {"town01_01", "town01_01", "town01_06", "town01_04", "town01_10", "town01_07",
                                                            nullptr,     nullptr,     "town01_51", "town01_52", "town01_53"};
    static constexpr const char* kTownMap[12] = {"", "t0101", "t0102", "t0106", "t0104", "t0110", "t0107", "t0108", "t0203", "d1606", "t0123", "e0007"};
    static constexpr const char* kTownMapAlt[12] = {"", "t0121", "t0102", "t0106", "t0113", "t0117", "t0122", "t0108", "t0203", "d1606", "t0123", "e0007"};
    static constexpr int kTapTownRow = 5100, kTapTownPoint = 5150, kTapTownList = 5170, kTapTownYes = 5171, kTapTownNo = 5172;
    struct Cr3TownMove {
        int stage = 0;           // 0 shut, 1 TownQuestClearNEXT running, 2 the list, 3 the map, 4 asked, 5 leaving
        float since = 0, clock = 0, tick = 0;
        std::vector<int> rows;   // (the list's places, in its order) and their pictures
        std::vector<int> images;
        int row = 0;
        int asked = -1;          // (the id asked about, then chosen)
        bool fromList = false;   // (asked from the list: back to it - 0x28c)
        bool direct = false;     // (the field's button: a fade in on staying - 0xf9)
        float scrollX = 0, scrollY = 0, aimX = 0, aimY = 0;
        bool aiming = false, dragging = false;
    } town;
    std::vector<Cr3GeneLabTap> townTaps;
    bool townOpen(int pt) const;
    std::string townPlace(int id);
    bool townMerged() const;
    int townPointOf(int id) const;
    bool townPointAt(int pt, float& x, float& y);
    void townScrollClamp(float& x, float& y) const;
    void townAim(int id);
    void openTownMove(bool direct);
    void townListOpen(int focus);
    void townMoveShow();
    void townAsk(int id, bool fromList);
    void townLeave(int id);
    void townFinish();
    void chooseTownMove(int id);
    bool stepTownMove(float dt);
    void drawCr3TownMove(int w, int h);
    bool townMoveClick(float fx, float fy);
    bool townMoveDrag(float dx, float dy);
    bool townMoveKey(SDL_Keycode key);
    void drawCr3Field(int w, int h);
    bool cr3InDungeon() const;
    void drawCr3FieldButtons(int w, int h, const Mat4& view);
    // The minimap (MiniMapMenu, interface 0x5a: SetFieldTabMenuParameter's with a dungeon's tab - its
    // taps off there): feMapPositon, feMapMenu at its call_feMapMenu, feMap_id00 at that's
    // call_feMap_id with <area>m_map on its first material (ChangeMapTexture; d0904 / d0908 / d0912
    // by the layer: Fld2Main::UpdateMinimap - the ragdoll_minimap_<name> box the player is in,
    // <name>m_map, the gimmicks of other layers left off); then each Field row's gimmick marked +0x28
    // 1 feMapWarp, 2 feMapObject, and feMapPlayerPanel - at call_feMapMenu plus x / 30, -z / 30 - with
    // feMapCamera at its call_feMapCamera turned to the camera's way to the player, feMapPlayer at
    // call_feMapPlayer turned as the player faces (atan2(-x, -z) of its direction)
    struct MinimapMark {
        int kind = 0, layer = -1;
        float x = 0, z = 0;
        std::string name;
    };
    struct MinimapLayer {
        std::string name;
        cr3::Vec3 lo{}, hi{};
    };
    std::vector<MinimapMark> minimapMarks;
    std::vector<MinimapLayer> minimapLayers;
    int minimapLayer = -1;
    float minimapAt = 0;
    void loadMinimap(const std::string& name, const std::vector<std::string>& joints, const std::vector<cr3::Mat34>& world);
    int minimapLayerAt(const cr3::Vec3& p) const;
    UiFig& cr3UiPutTurned(const std::string& name, float x, float y, float turn, const Mat4& view);
    void drawCr3Minimap(int w, int h, const Mat4& view);
    // The goods button (FieldGoodsMenu, interface 0x36 - Fld2GoodsButton): feGoodsMenu at
    // feGoodsPosition's call_feGoodsMenu_fit3 (the bottom right), its five slots call_feGoodsPanel_01
    // .. _05 an arc out of the corner; the goods in focus a feGoodsPanel (feGoodsPanel_in) at the
    // middle one - feGoodsIcon_fr00 at its frame by the goods: 0 the bomb, 1 the toy camera -;
    // tapped, used (Fld2GoodsButton::OnUpdate: 1 PutBomb). The others round the arc, the
    // feGoodsNext arrows, the names: not yet
    int goodsFocus = -1;
    int focusedGoods();
    bool goodsPad(bool cycle);
    void drawCr3Goods(int w, int h, const Mat4& view);
    void useGoods(int goods);
    void openCr3Camp();
    void drawCr3Camp(int w, int h, const Mat4& view);
    void drawCr3CampFooter(int w, int h, const Mat4& view);
    void drawCr3CampCharas(int w, int h, const Mat4& view);
    void drawCr3CampGenes(int w, int h, const Mat4& view);
    void chooseCr3Camp(int id);
    bool cr3CampClick(float fx, float fy);
    // ---- The Vita's buttons over the field's (CampFieldMenu, FUN_811116b4 - Fld2's cases 0x1d .. 0x1f,
    // each update): △ (0x80) opens the panel (com_008); open, × (0x40) or △ shuts it (com_009), a
    // cursor over its buttons in the game's order - ↑ / ← the one before (from the first two, the
    // last), ↓ / → the one after (from the last, the town map's, else the way back's) with com_002,
    // passing those not shown (past either end: Camp) -, the one under it in DiffuseColor (0.5, 1,
    // 1.5) (0x81615ed0: the whole part, or its mat_fe_parts_01 - FUN_8132d3ea), ○ (0x100) pressing
    // it as its tap. Opened or shut, the cursor is on Camp. The iPad's and Android's executables had
    // no button code, but their archives carry the same UI parts: enabled for any open CR3 release.
    static constexpr int kCr3BarIds[8] = {110, 108, 103, 102, 101, 106, 104, 105}; // (the way back to town, the town map, Battle, the encounters, Save, Camp, My Page, the SpShop)
    bool vitaButtons() const;
    bool cr3BarShown(int id) const;
    bool cr3BarCursorOn();
    void cr3BarSettle(int way);
    enum class PadPress { None, Up, Down, Left, Right, Confirm, Cancel, Triangle };
    bool cr3BarPad(PadPress p);
    bool cr3Shoulder(bool right);
    void cr3CampPadHeld(bool held);
    bool campGeneShoulder(bool right);
    void sortCampGenes();
    bool cr3CampKey(SDL_Keycode key);
    void drawAreaDrag(int w, int h);
    void drawAreaCam(int w, int h);

    // ---- the camp's Items (CampItemMain, interface 0x61) ----
    // CreateCampItemList: comListMenu (0x2d) with its category buttons (SetCategoryPanelStatus 0x13,
    // 0x14, 2, 0, 0x15, 0x16, 0x17, 1, 3, 4, 5 - each a comListCategoryBtn, 0.57 apart from
    // call_comListCategoryBtn, six shown; its icon comListCBtnIcon_fr00 at that frame, the focused
    // one raised (-0.072, +0.057) with its name at typeM_C01 - AnnounceMenuName value + 0x29);
    // CreateCampItemListItem(category): 0 all held, 1 Items of type (+8) 0, 2 gifts, 3 type 0x12,
    // 4 type 1, 5 type 0x13, 6 type 0x14, 7 event items, 8 weapons, 9 armour, 10 accessories - each
    // a comListPanel row (0.76 apart: its icon comListIcon_fr00 at the row's +0xc frame, the name
    // at typeM_L01, the count in comListNumI_num00; an Item without a field effect - MbItemInfo::
    // HasFieldItemEffect - dimmed; a weapon or armour worn its wearer's camEquChr_fr00); the help
    // of the one in focus across the header (title 1). Chosen (CampItemMain::Update): a gift -
    // ItemizeGift asked (0x1d0); an Item by its first field effect (field_item.mbe: kind, value -
    // 1 .. 4 attack / defence / speed / luck, 5 levels, 6 MP, 7 a command, 8 a skill): how many
    // (CampItemMenu 0x60 - camItemReelMenu's two digit reels, mat_num_01/02 the tens and 03/04 the
    // ones, a digit 0.14 of the strip's V - d at 0.14 x (2 - d) -; camItemStock_num00 the count
    // held - with the system window's 0xdc and Yes / No, Yes only for 1 .. held), then a gene for
    // it (kinds 1 .. 5, 7, 8: GeneChangeMenuCamp's cards - EnforceGene 9 for 1 .. 4, 6 with the
    // levels' EXP for 5, 4 with the command or skill in slot 6, else 7, for 7 / 8) or a character
    // (6: CreateGeneCharaListMenuListOnly - its MP up, its most the gene's)
    struct Cr3CampItems {
        int category = 0;                       // (the category button in focus, 0 .. 10)
        int row = 0;                            // (the row in focus)
        std::vector<std::pair<int, int>> rows;  // (the category's: the item's full id, how many)
        int item = 0, count = 1, held = 0;      // (the item chosen, how many to use, how many held)
        int kind = 0, amount = 0;               // (its first field effect)
        int step = 0;                           // 0 the list, 1 how many, 2 a gene for it, 3 a character, 4 asked (a gift itemised), 5 asked (the Microcommunicator)
        int countFocus = 0;                     // tens, ones, Yes, No (Vita's CampItemMenu)
        std::string told;                       // (an alert over it - a tap ends it)
        float toldSince = 0;
    } campItems;
    static constexpr int kItemTabs[11] = {0x13, 0x14, 2, 0, 0x15, 0x16, 0x17, 1, 3, 4, 5};
    bool cr3ItemRow(int id, cr3::MbeFile::Row& r);
    std::string cr3EquipHelp(int full);
    std::string cr3ItemText(int id, bool help);
    std::pair<int, int> cr3FieldEffect(int id, int k = 0);
    bool cr3ItemInCategory(int id, int category);
    bool cr3ItemUsable(int id);
    void campItemRows();
    int cr3ItemWearer(int id);
    void drawCr3Asked(const std::string& text, float since, int focus, bool yesOn, int yes, int no, int w, int h, const Mat4& view,
                      std::vector<Cr3GeneLabTap>* out = nullptr);
    void drawCr3Told(const std::string& text, float since, int told, int w, int h, const Mat4& view, std::vector<Cr3GeneLabTap>* out = nullptr);
    void campItemsTell(const std::string& text);
    void openCampItems();
    void drawCampItems(int w, int h, const Mat4& view);
    void drawCampItemCount(int w, int h, const Mat4& view);
    bool campItemCanTargetGene(const cr3::Cr3Gene& g) const;
    void campItemOnGene(int g);
    void campItemOnChara(int ch);
    // (after an item's use: back to the list, its rows again)
    struct CampItemsUsed {
        Viewer& v;
        explicit CampItemsUsed(Viewer& viewer) : v(viewer) {}
        ~CampItemsUsed() {
            v.audio.playSe("com_003", false);
            v.cr3Camp.stage = 4, v.cr3Camp.since = 1, v.campItems.step = 0;
            v.campItemRows();
        }
    };
    void chooseCampItems(int id);
    void campItemsTarget();
    void campItemsKey(SDL_Keycode key);

    // ---- the camp's Equipment (CampEquipMenuMain 0x68, CampEquipFutterMenu 0x69, CampEquip2Menu 0x6d) ----
    // The character's three slots (SetEquipListItem: comListMenu rows - weapon, armour, accessory,
    // the worn one's icon and name; header title 2, 0x18b), then a slot's candidates
    // (CreateCampEquipStatusItemList / SetVariousEquipment: the held ones of its kind the
    // character may wear - item_para's +0x2c, a bit a character -, the worn one in focus; an
    // accessory's "none" last; those others wear marked camEquChr_fr00): tapped again, worn - one
    // only others wear taken from them, each given this character's old one (SetEquipPartsDirect).
    // CampEquip2Menu's status (camEquPosition io, camEquMenu at its fit4: camEquWepPanel for a weapon
    // or armour, camEquJewPanel for an accessory - the character's icon camEquCharaIcon_fr00, the
    // item in focus's name and help at typeM_L01 / L02). The footer's character buttons
    // (comFtrEquipMenu: comFtrCharaBtn_fr00 at call_comFtrCharaBtn_fr00_001 .., the frame by the
    // character - 0 .. 4 the chosen, 5 .. 9 the others; 10 / 11 past five - libcrx 0xe63198 / 0xe631d4)
    struct Cr3CampEquip {
        int chara = 0;          // (char_para id)
        int slot = 0;           // (0 weapon, 1 armour, 2 accessory - the row in focus)
        int step = 0;           // 0 the slots, 1 a slot's candidates
        std::vector<int> rows;  // (1: the candidates' full ids; 0 none)
        int row = 0;
        // the model view (camEquBtnPanel's button: CampEquip2Menu +0x440 - the panel closed, the
        // character's model shown, turned by a drag across and sized 2 .. 3 by one up and down)
        bool model = false;
        float modelSince = 0, yaw = 0, scale = 2;
        float btnX = 0, btnY = -100;
    } campEquip;
    struct Cr3CampModel { // (the model drawn: its key - the character, body, weapon -, its figures)
        std::string key;
        ViewFigure body, head, weapon;
    } campModel;
    std::map<int, int>& cr3Worn(int slot);
    int cr3WornItem(int ch, int slot);
    bool cr3CanWear(int ch, int full);
    void campEquipRows();
    void openCampEquip();
    void campEquipWear(int full);
    bool cr3EquipStats(int ch, int slot, int full, cr3::Cr3Fighter& f);
    std::string cr3GeneNameOf(int ch);
    void cr3EquipChange(const UiFig& panel, const std::string& joint, int value, int places, bool percent, const Mat4& view);
    void cr3EquipNumber(const UiFig& panel, int k, int value, const Mat4& view);
    void drawCr3EquipNumbers(const UiFig& panel, int slot, const cr3::Cr3Fighter& was, const cr3::Cr3Fighter& now, const Mat4& view, bool both = false);
    void drawCr3CampModel(int w, int h, const Mat4& view);
    bool cr3CampDrag(float dx, float dy);
    UiFig* drawCr3EquipStatus(int chara, int slot, int item, float t, int w, int h, const Mat4& view, const std::pair<int, int>* preview = nullptr);
    void drawCampEquip(int w, int h, const Mat4& view);
    void chooseCampEquip(int id);
    void campEquipKey(SDL_Keycode key);
    void cr3BodiesChanged();
    static constexpr int kEquipTabs[3] = {3, 4, 5};
    void drawCr3Categories(const UiFig& menu, const int* values, int count, int focus, int tapBase, float& labelX, float& labelY, const Mat4& view);

    // ---- the camp's Fuse Gifts (GiftSynthesizeMain 0x71, its list GiftSynthesizeListMenu 0xb4) ----
    // The fusions (data\mergegifts: _id, category, gene_id, items - the gifts' codes, available - a
    // flag; -1 none) of the category (the buttons 6, 7, 8 - Dungeons, Colosseum, Other: categories
    // 0, 1, 3), each a comListLPanel row 1.24 apart (its gene's icon, element, rarity and name -
    // comListLMixing); those whose every gift is held - a gift's code its gift_color + gift_number
    // (item_para's Gift +0x40 / +0x44) - bright, the rest dimmed. The one in focus: its gifts on
    // camConpoListMenu's six camConpoListPanels (camConpoListPositon, its own animation sliding it
    // in; the gift's icon - comListIcon_fr00 -, dimmed where not held) and its gene's card at
    // mixMtrPositon's call_GeneCard; the header (title 4) 0x23e + the category. Chosen: with the
    // genes held at GENE_CAPACITY - 0x232; else asked (0x118: its rarity's N / R / SR, 0x242 +
    // grade, and name) - MergeGift(id, the gifts), the new gene told (0xfb)
    struct Cr3CampGifts {
        int category = 0;        // (the button: 0 .. 2)
        std::vector<int> rows;   // (mergegifts' row indices)
        int row = 0;
        int step = 0;            // 0 the list, 1 asked
        int askFocus = 0;
        std::string told;
        float toldSince = 0;
    } campGifts;
    static constexpr int kGiftTabs[3] = {6, 7, 8};
    int cr3GiftCode(int id);
    std::vector<int> cr3FusionGifts(const cr3::BsonValue& f);
    const cr3::BsonValue* cr3Fusion(int k);
    void campGiftRows();
    void openCampGifts();
    void drawCampGifts(int w, int h, const Mat4& view);
    void chooseCampGifts(int id);
    void campGiftsKey(SDL_Keycode key);

    // ---- the camp's Quests (QuestMenuMain 0x6a, mode 1; QuestDitailMenu) ----
    // FindQuests (request 0x45) first, then CreateQuestList: comListMenu's category buttons 9, 0x10,
    // 0x12, 0x10 - Main Quest (the rank 0 quests: DBEmu's first list), Sub Quests (the drawn: the
    // second), Tutorial (the first's of rank 6), Current Quests (those begun, not ended) - and
    // CreateQuestListItem's rows (CopyQuest: its id, rank, level, item and count): the icon at
    // comListIcon_fr00 rank + 0xae, the title (message\Quest_Title by the id) at typeM_L01, those
    // ended marked comListClear; one whose item isn't held (its count) dimmed. Tapped again: its
    // detail (spQstPosition - its own animation sliding it in - spQstMenu: the title at typeM_C01,
    // Quest_text's lines on spQstText's typeM_L01 .. L10, the OK button spQstBtn); the header title 5
    struct Cr3CampQuests {
        int category = 0;
        std::vector<int> rows; // (quests() indices)
        int row = 0;
        bool detail = false;
        float detailSince = 0; // (the detail's clock: spQstPosition's animation)
    } campQuests;
    static constexpr int kQuestTabs[4] = {9, 10, 0x12, 0x10};
    void campQuestRows();
    void openCampQuests();
    void drawCampQuests(int w, int h, const Mat4& view);
    void drawCr3Quests(bool port, float t, int w, int h, const Mat4& view);
    bool questReady(const cr3::Cr3OfflineDb::Quest& q) const;
    void drawCr3QuestDetail(const cr3::Cr3OfflineDb::Quest& q, int count, float t, int w, int h, const Mat4& view, bool okOn = true, bool cleared = false);
    // The cleared quest's detail (EventRuntime::clearQuestShown - ClearQuestMenu): the detail as the
    // Space Port shows it, its clear mark on, over the event (the script faded the screen out under
    // it); its OK closes it
    std::vector<Cr3GeneLabTap> clearQuestTaps;
    void drawClearQuest(cr3::EventRuntime& ev, int w, int h);
    void drawLevelUp(cr3::EventRuntime& ev, int w, int h);
    void drawCr3GetItem(cr3::EventRuntime& ev, bool withText);
    void answerClearQuest();
    bool clearQuestShown();
    bool clearQuestClick(float fx, float fy);
    // ---- CR3's save files (TitleMenuMain 3 .. 0x1a: CreateSaveFileMenu - SaveFileMenu) ----
    // Over the title's reel: ttlDataPositon on its _in; at its call_ttlDataFile three panels per page
    // (ttlDataSave01 .. 03: frame 1 the one picked - ttlDataOk on it, 0 the copy's source -, 0 a
    // file, 2 none; ttlDataTxt - "File %d" 0x312 at typeM_L01, 0x2c7 at L02, the play time "%4d:%02d"
    // at R03, the hero's name at C04, ttlDataStar_fr00 at its call -, half lit where it can't be
    // picked), ttlDataCopy and ttlDatadelete (frame 1 while theirs); at call_ttlDataParty the picked
    // file's chapter (ttlDataTitle01 .. 05 at call_ttlDataTitle, its frame) and its party's three
    // (VsModeCharaPanel at call_VsMainParty_01 .. 03). The header 0x26 (the file's coins and ML), its
    // line 0x311 / 0x309 / 0x30a / 0x30c; the footer the file's team level and name, the back. The
    // chapter and stars: MbSaveFileInfo's table (the executable's) - the last of flags 701 .. 733
    // set, the chapter 0 .. 32 and the stars 0 .. 4; 6458, five. A file tapped is picked (com_002),
    // tapped again asked (0x308 "{number}") - yes, it's played (utils::LoadData: none there, a new
    // game in it). Copy: a file, then an empty one, asked (0x30b) - copied (com_011); Delete: a
    // file, asked (0x30d) and again (0x30e) - deleted (com_012). Back: the title again
    struct Cr3SaveSlotInfo {
        bool exists = false, occupied = false;
        cr3::GameState state;
        long seconds = 0;
        int hp[3] = {}, mp[3] = {}; // (the party's: CrxCharacter::GetMaxHp, its MP now)
    };
    struct Cr3SaveMenu {
        int mode = 0;     // 0 play, 1 copy source, 2 copy destination, 3 delete, 5 NG+ destination
        int focus = kCr3SaveSlots; // kCr3SaveSlots: no file picked
        int footerFocus = -1; // Vita's Copy / Delete row, -1 while over files
        int page = 0;
        int source = -1;  // (copy: the file copied)
        int ask = 0;      // 0 none, 1 play, 2 copy there, 3 delete, 4 delete - surely, 5 NG+, 6 cleared file
        int askSlot = 0, askFocus = 0; // NG+ confirmation starts on No
        std::string notice;
        float since = 0, askSince = 0;
        std::array<Cr3SaveSlotInfo, kCr3SaveSlots> slots;
    } saveMenu;
    std::vector<Cr3GeneLabTap> saveTaps;
    static constexpr int kTapSaveFile = 1300, kTapSaveCopy = 1320, kTapSaveDelete = 1321, kTapSaveYes = 1322, kTapSaveNo = 1323,
                         kTapSavePrev = 1324, kTapSaveNext = 1325, kTapSavePlus = 1326;
    void loadSaveSlots();
    void openSaveMenu(int page = 0);
    bool saveSlotOn(int k) const;
    static void saveChapter(const cr3::GameState& st, int& title, int& stars);
    bool savePlusAvailable() const;
    bool createSavePlus(int source, int destination);
    void chooseSave(int id);
    void stepSaveMenu(float dt);
    bool saveMenuKey(SDL_Keycode k);
    bool saveMenuClick(float fx, float fy);
    // ttlDataSave's pattern 2 contains the original silver NEW GAME artwork and
    // a baked File 01/02/03. On later pages only, cover that number using a blank
    // strip at the same height in the native panel, then print the global slot.
    // Both the colour atlas and its alpha mask follow the figure's animated UVs.
    ViewFigure saveNumberPatch;
    bool drawNumberedEmptySavePanel(const UiFig& panel, const Mat4& view);
    void drawClearedSaveChoices(int w, int h, const Mat4& view);
    void drawSaveMenu(int w, int h);

    // ---- the world select (EventRuntime::WorldSelect: WorldSelectMenu 0x2b) ----
    // The planet - t0100star01 (the earth, its cloud and earth_eff: SetJointWorldTransform'd scale
    // 2.3 x the zoom, turned by the pitch about X after the yaw about Y; the cloud by its own yaw
    // drifting 0.0007 a frame), t0100star00 and t0100star02 (scale 1.5 x the zoom, turning 0.001 a
    // frame) - seen through star01's own camera (GetContaindCamera) lit by its own lights, moved
    // across by the shift (0.15 while a dungeon's levels are up, the zoom then 1.1). Over it each
    // dungeon open (World_Flag_Tbl: flags 1050 .. 1064) facing the camera: spPointMenu at its point
    // (star01_D<StarNameTable> through the planet's turn, Cr3UtilSetBalloonPos), its ring -
    // spPointOff, spPointOn the chosen's: their own animations, looped - and spIconMenu there: the dungeon's picture
    // (spIcon_fr00, frame the point + 1), the gift (spCheckBtn), "NEXT" (comListHit) where its
    // Hit_Flag_Tbl flag (1080 ..) is set; the last (1064) at spBtnPositon's call_spIconMenu instead;
    // nearest drawn last. The header "Select Dungeon" (0x15). A point tapped (TouchGestureTap):
    // the planet turned to its pitch and yaw (DAT_00f6af14 / aed8) a third at a time, the levels
    // listed (CreateListMenu: World_Level_Icon_Tbl's icon, "Lvl .." 0x15 + n - the first, then those
    // whose flag 1100 + n is set; 0x129), its name at spMapName; a level: asked (0xa4 - the
    // dungeon's name 0xe9 + point, 0xfa for the fourth once flag 6004 is set -, the level's; 0x12a);
    // yes: Cr3FadeOut 30 frames, two seconds, GetSelectDungeonNum the point + 1, GetSelectDungeon
    // Level the level, CheckQuestReturn 3 - the town's WarpTask; no or back: the planet again. Back
    // from the planet: the same with no dungeon (-1) - or, from the Space Port's Explore, the Space
    // Port again. Dragged: turned a 460th of the screen's 960 each, spun on after a release. The
    // list button: the dungeons' list (drawWorldList); the gift: its collection (drawCr3CollectList)
    struct Cr3WorldSelect {
        int stage = 0;        // 0 the planet, 1 the levels, 2 asked, 3 leaving, 4 the dungeons' list, 5 a collection
        float since = 0, clock = 0, tick = 0;
        float yaw = 0, cloudYaw = 0, spin = 0, zoom = 1, shift = 0, starsYaw = 0, pitch = 0;
        int point = -1;       // (the point chosen)
        std::vector<int> levels;
        int row = 0;          // (the levels' focus)
        int level = -1;       // (the level chosen)
        bool chosen = false;  // (3: a dungeon chosen, else back)
        bool wasOpen = false, dragging = false;
        std::vector<int> listPoints; // (4: the open dungeons)
        int listRow = 0;      // (4: the list's focus)
        bool fromList = false; // (the levels reached from the list - back to it; 0x490 clear)
    } worldSel;
    std::vector<Cr3GeneLabTap> worldTaps;
    // A dungeon's collection (CollectListMain / CollectListMenu): its item sheet's entries on the
    // dungeon's board, as drawCr3CollectList shows them
    struct Cr3CollectList {
        bool open = false;
        int sheet = 0;              // (its item sheet)
        int phase = 0;              // 0 shown (sliding in, the new ones after a second), 1 closing
        float since = 0, closing = 0;
        std::vector<bool> fresh;    // (the entries new on opening: their New mark, their get effect)
        int help = -1;              // (the panel tapped: its name over it)
        bool scripted = false;      // (a script's - CollectListDisp: over the field, no fades, slid out)
        int addCategory = -1, addId = -1; // (CheckAddItem's: the entry just added - its get effect)
        int cgl = 0;                // (the prize's first showing: 0 none, 1 mpListCglMenu in, 2 its loop - a tap -, 3 out, 4 the gene's window, 5 done)
        float cglSince = 0, prizeAt = -1; // (prizeAt: when the prize's New mark came - after the window)
    } collectList;
    std::vector<Cr3GeneLabTap> collectTaps;
    std::vector<Cr3GeneLabTap> scriptGeneTaps; // (an event's gene status / disposal: its taps)
    static constexpr int kTapCollectClose = 799, kTapCollect = 800; // (+ kind x 10 + its slot: 0 item, 1 gift, 2 gene, 3 the prize)
    static constexpr int kWorldPoints = 15;
    static constexpr int kWorldStar[kWorldPoints] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 1, 99};
    static constexpr float kWorldPitch[kWorldPoints] = {-0.3f, -0.3f, -0.6f, 0.4f, 0.5f, 0.3f, -0.2f, -0.1f, 0.2f, -0.1f, 0.5f, 1.1f, 1.2f, 0.9f, 0.0f};
    static constexpr float kWorldYaw[kWorldPoints] = {-2.165f, 2.222f, 0.607f, -2.252f, -1.852f, -2.841f, -0.414f, -1.637f, 0.458f, 1.193f, -0.177f, -0.177f, -1.99f, 2.648f, 0.0f};
    static constexpr int kWorldLevelIcon[kWorldPoints] = {200, 200, 200, 201, 201, 201, 202, 202, 202, 202, 203, 203, 203, 203, 203};
    static constexpr int kTapWorldYes = 701, kTapWorldNo = 702, kTapWorldList = 703, kTapWorldPoint = 710, kTapWorldLevel = 730, kTapWorldGift = 760, kTapWorldRow = 790;
    bool worldOpen(int k) const;
    std::string worldName(int k);
    struct Cr3WorldView {
        Mat4 vp{};
        cr3::Mat34 planet;
        bool ok = false;
    };
    cr3::Geom worldStarGeom;
    bool worldStarTried = false;
    static cr3::Mat34 worldTurn(float scale, float pitch, float yaw);
    bool worldPointLocal(const UiFig& star, int k, float* out);
    static void worldApply(const cr3::Mat34& m, const float* p, float* q);
    Cr3WorldView drawWorldPlanet(int w, int h);
    void worldAim(int k);
    void drawCr3WorldSelect(cr3::EventRuntime& ev, int w, int h);
    void drawCr3CollectList(int w, int h, const Mat4& view);
    void stepScriptCollect(float dt);
    void drawCr3ScriptCollect(int w, int h);
    bool collectClick(float fx, float fy);
    int worldCollectSheet(int point) const;
    int collectSheetOf(int n) const;
    float collectShown() const;
    void stepCollectList(float dt);
    float uiAnimSeconds(const std::string& fig, const std::string& anim);
    void openCollectList(int sheet, bool scripted = false, int addCategory = -1, int addId = -1);
    void chooseCollectList(int id);
    void worldSheet(int k, int& total, int& won) const;
    // The dungeons' list (WorldSelectListMenu - Update 3: AddWorldSelectListItem for each open one;
    // its rows WorldSelectListLItem's comListImgPanel): DummyAddListItem's blank at each end, the
    // rows 1.24 apart (0xc0) from the menu's call_comListPanel, the focused one second - the one
    // before it at the panel's place, one after it 0.124 lower (0xc4), three in view (0xc8) - 0.25
    // to the left, 1.1 x and bright (SetSelectFlag; the others' parts and name 0.5). A row: the
    // dungeon's picture (comListImg_fr00 frame n + 1 - from 14 on sp_list_02's, frame n - 13), its
    // ticket (comListTicket_fr00: frame 1 - its numbers, comListNumC_num00: those won, a slash -
    // comNum00_fr00 frame 34 - and the sheet's count, two places each, comNum00_fr00 frame 10 + d,
    // a leading zero blank - or frame 0 once all won), comListHit with its hit flag and its name
    // (size 29) at typeM_L00. A tap on the focused row: its levels (Update 6 - back to the list,
    // its focus kept: 0xb); on the next or the one before: scrolled to it. Its back: the planet
    // (SetReturnWorldMap). While it's open the planet turns to the focused dungeon, its point on
    // a row of a WorldSelectListMenu (WorldSelectListLItem::InitializeL): its picture (comListImg_fr00
    // frame `image` - from 14 on sp_list_02's), its ticket (`total` < 0: none), the hit and New marks, its name
    struct Cr3PlaceRow {
        int image = 0;
        std::string name;
        int won = 0, total = -1;
        bool hit = false, fresh = false;
    };
    void drawWorldList(int w, int h, const Mat4& view);
    void drawCr3PlaceRows(const std::vector<Cr3PlaceRow>& rows, int f, float since, int tapBase, int category, int w, int h, const Mat4& view);
    void worldListOpen(int focus);
    std::vector<int> worldLevels() const;
    void worldLeave(bool chosen);
    void chooseWorldSelect(cr3::EventRuntime& ev, int id);
    bool worldSelectClick(float fx, float fy);
    bool worldSelectDrag(float dx, float dy);
    bool worldSelectKey(SDL_Keycode key);
    void stepWorldSelect(float dt);
    // ---- a town's shop (EventRuntime::Shop: ShopMenuMain) ----
    // Over shBgMenu (the gene lab's backdrop) and the clerk: the header - its title the shop's (0x1a
    // T01_1, 0x1b T03_1, 0x1c T04_1; 0xd buying, 0xe selling), "Select command." 0x26e - and its
    // buttons (comBtnMenu, SetShopTopButton: Talk 0x11, Buy 0x14, Sell 0x15), the footer and its
    // back. Buy / Sell: SetShopSecondButton's Items 8, Weapons 0x16, Armor 0x17, Accessories 0x18
    // (0x26f / 0x270), shTabPositon's optSltBtn_fr00 - Buy 0x114 / Sell 0x115, the other's tap
    // switching (SetBuySellTab). One chosen: the clerk away, the list (CreateShopBuyCoreList: the
    // shop's goods - data\shops, those whose flag holds (GoodsAvailable) - by their first item's
    // category, comListPanel rows with its icon, name and price - comListNumUn_num00, its unit's
    // icon 0 coins, 1 GP, 2 gold; selling the items held, but those
    // worn, at item_para's price), the category buttons over it, the row's help the header's line.
    // The focused row tapped again: an Item - how many (CampItemNumberSelect's reels: up to what's
    // affordable and 99 held, none - 0x233 told; camItemPrice_num00 the price for them, camItemStock
    // the count held), its question (0x248 {itemname} for {currency} - 0x249 + the unit; selling
    // 0x210) - equipment asked at once, one. Yes: BuyGoods / SellItem, com_024, the list again;
    // equipment bought then asked 0x20e - "Equip it?" - on the party's first (not the fifth while
    // flag 1155 holds), yes wearing it (SetEquipParts)
    struct Cr3ShopMenu {
        bool wasOpen = false, talking = false;
        int screen = 0;       // 0 the buttons, 1 the categories' buttons, 2 the list, 3 how many, 4 asked, 5 equip it?
        float since = 0, clock = 0;
        bool buy = true;
        int focus = 0;        // (the buttons' - keys)
        int category = 0;     // (kShopTabs' index)
        int row = 0;
        struct Row {
            int goods = -1, item = 0, count = 1, price = 0, unit = 2, held = 0;
            bool affordable = true;
        };
        std::vector<Row> rows;
        int amount = 1, most = 1; // (how many, and at most)
        int bought = 0;           // (5: the equipment just bought)
        int chara = 0;            // (the footer's character - ShopMenuMain +0x60 -: the status's, who wears what's bought; 0 the party's first)
        // the alteration shop's (RemodelingMain): the equipment chosen, the list it came from
        // (screen 6 lists the items to attach in its place), what attaching it would do
        bool alter = false;
        int equip = 0, equipRow = 0, pattern = 0;
        std::vector<Row> equipRows;
        std::string told;
        float toldSince = 0;
    } shopMenu;
    std::vector<Cr3GeneLabTap> shopTaps;
    static constexpr int kShopTop[3] = {0x11, 0x14, 0x15};
    static constexpr int kAlterTop[2] = {0x11, 0x29};        // (RemodelingMain 8: Talk, Alter)
    static constexpr int kAlterSecond[3] = {0x16, 0x17, 0x18}; // (6: Weapons, Armor, Accessories)
    static constexpr int kShopSecond[4] = {8, 0x16, 0x17, 0x18};
    static constexpr int kShopFirst[4] = {0x14, 3, 4, 5}; // (the second buttons' categories)
    static constexpr int kShopTabs[8] = {0x14, 0, 0x15, 0x16, 0x17, 3, 4, 5};
    static constexpr int kTapShopYes = 801, kTapShopNo = 802, kTapShopTold = 803, kTapShopBuyTab = 804, kTapShopSellTab = 805;
    static constexpr int kTapShopSecond = 810, kTapShopRow = 820, kTapShopCat = 900, kTapShopReel = 950, kTapShopChara = 960;
    int shopChara();
    int shopEquipSlot() const;
    int shopTitle() const;
    static int shopCampCategory(int value);
    std::vector<Cr3ShopMenu::Row> shopRows(int value);
    int shopPurse(int unit) const;
    std::vector<int> shopCategories();
    void shopList(int value);
    void shopScreen(int screen);
    std::vector<Cr3ShopMenu::Row> alterRows(int equipment);
    int alterPattern(int equipment, int item) const;
    std::string alterAsk();
    void drawCr3Shop(cr3::EventRuntime& ev, int w, int h, bool front);
    void chooseShop(cr3::EventRuntime& ev, int id);
    bool cr3ShopClick(float fx, float fy);
    bool cr3ShopKey(SDL_Keycode key);
    void stepShop(float dt);
    // ---- the gene obtained window (EventRuntime::GeneGet: GetGeneMenu, interface 0x41) ----
    // feGetPosition's call_feGetMenu: feGetMenu (feGetMenu_in to 0.2 s, its panel, name plate -
    // feGetName_fr00 frame 2 -, gene icon 0xad and deco fading in with it; the line, FldAnnounce
    // 0x11 with {item.name}, size 32 at its tyepM_L00 - so spelt); feGetGenePosition 3 nearer with
    // feGetGeneMenu (0.6 nearer; feGetGeneMenu_in to 0.267 s) and its feGetGenePanel (0.8 back),
    // the card at its call_cardModel (1 nearer, 8 x, turned -17.2 degrees about its normal) fading
    // in with it, then one turn about its upright, 0.314 a frame. A new gene: feGetNew (its loop)
    // from 1.5 x to 1 over a sixth of a second; above level 1 feGetCardLv_fr00 with camEquNumLv_
    // num00's three comNum04_fr00 - "MAX" (frames 10 .. 12) at its top level. Two thirds of a second
    // on, a tap: feGetMenu_in on to 0.467 (out), feGetGeneMenu_in back to 0 over 0.267 s, fading -
    // then closed (CheckGeneGetWindow false)
    struct Cr3GeneGetView {
        bool wasOpen = false;
        float since = 0;
        bool closing = false;
        float closed = 0; // (since the tap)
    } geneGetView;
    static constexpr float kGeneGetIn = 0.2666667f, kGeneGetSpin = 20.0f / 30, kGeneGetNew = 1.0f / 6, kGeneGetWait = 2.0f / 3;
    void stepCr3GeneGet(float dt);
    bool geneGetTap();
    void drawCr3GeneGet(int w, int h);
    // ---- the Space Port (EventRuntime::SpacePort: SpacePortMain 0x7b) ----
    // Over the field and Sally's portrait: the header (0x13 "Spaceport", "Select command." 0x26e; 0x14
    // "Get Quest" with the quests), its buttons (comBtnMenu 0x2c, SetSpacePortTopButton: Talk 0x11,
    // Daily Quest 0x12, Explore 0x13 - off unless a flag of 1050 .. 1063 is set) and the footer
    // (SpacePortFutter 0x7c: comFtrPosition's team and back). Talk: her talk (the town's c121a_talk)
    // with the menu set aside. Daily Quest (SpacePortMain 9 / 10): FindQuests (request 0x45), then -
    // none at all: told 0x1a8 - the quests (CreateQuestList: the camp's four tabs, Sub Quests first
    // the first time when there's no main one) and a quest's detail, its arrows going round the list;
    // its OK (CheckOkButtonPermit: not the quest set off on already, nor a current one, its goods held)
    // asks 0x124 with its title, yes setting off (Cr3FadeOut 5 frames, a second: the global work's 24
    // the quest, CheckQuestReturn 2). Explore asks 0x125; yes - SelectFreeSpacePort, the town's
    // quest_cancel - leads to the world select (0x22: WorldSelectMenu - not yet in: back to the town,
    // as Back). Back: Cr3FadeOut 5 frames, a second, CheckQuestReturn 1
    struct Cr3SpacePortMenu {
        int screen = 0;       // 0 the buttons, 1 the quests (a detail: campQuests.detail), 2 set off?, 3 explore?, 4 told, 5 leaving, 6 shuffle?
        float since = 0;      // (the screen's)
        float clock = 0;      // (since it opened: the header's and buttons' io)
        int focus = 0;        // (the buttons' - keys)
        int leave = 0;        // (5: CheckQuestReturn's to be)
        bool wasOpen = false;
        bool first = true;    // (the quests not yet shown: Sub Quests first without a main one)
        std::string told;
    } spacePortMenu;
    std::vector<Cr3GeneLabTap> spacePortTaps;
    static constexpr int kSpacePortButtons[3] = {0x11, 0x12, 0x13};
    static constexpr int kTapPortYes = 601, kTapPortNo = 602, kTapPortTold = 603, kTapPortShuffle = 604;
    bool spacePortButtonOn(int id) const;
    void spacePortScreen(int screen);
    void openSpacePortQuests(bool again = false);
    void spacePortLeave(int questReturn);
    bool spacePortOkOn(const cr3::Cr3OfflineDb::Quest& q) const;
    void drawCr3SpacePort(cr3::EventRuntime& ev, int w, int h);
    void chooseSpacePort(cr3::EventRuntime& ev, int id);
    bool spacePortClick(float fx, float fy);
    bool spacePortKey(SDL_Keycode key);
    void stepSpacePort(float dt);
    void chooseCampQuests(int id);
    void campQuestsKey(SDL_Keycode key);

    // ---- the camp's Status (StatusMain 0xbc, StatusMenu 0xbd) ----
    // The character's picture (camStaPosition - its own animation -, camStaCut at its call_camStaCut
    // + (1.92, -2.56), the frame the character - 1) and four categories (StatusMain::Update's
    // SetCategoryBarWithText: icons 0xe, 3, 0x15, 4 - texts 0x42 .. 0x45; SetStatusParameter): 0 the
    // status (camEqustaJPanel at camStaPosition's call_camEqustaJPanel: the gene's name, HP and MP -
    // the MP left of the most - bars camEquHp_per / camEquMp_per and numbers, attack, defence,
    // speed, luck - camEquNumSts_num00's five comNum00_fr00 digits, the leading zeros hidden:
    // statusEquipMenu::SetNumberOfStatus), 1 the equipment's numbers (camEqustaWPanel at
    // call_camEqustaWPanel: weight, power, hits, the weapon's lowered resistances and the armour's -
    // CampEquip2Menu's panel without the changes), 2 Skills - the gene's eight slots, a command's or
    // skill's name -, 3 Equipment - the three worn; the characters' buttons below
    // (CreateCampEquipMenuFutter); the header title 6, the row in focus's help
    struct Cr3CampStatus {
        int chara = 0;
        int category = 0; // (0 the status, 1 the equipment's numbers - the panels -, 2 Skills, 3 Equipment)
        int row = 0;
    } campStatus;
    static constexpr int kStatusTabs[4] = {0xe, 3, 0x15, 4};
    static constexpr int kStatusTexts[4] = {0x42, 0x43, 0x44, 0x45};
    struct Cr3StatusRow {
        std::string name, help;
        int icon = -1;
    };
    std::vector<Cr3StatusRow> campStatusRows();
    void openCampStatus();
    void drawCampStatus(int w, int h, const Mat4& view);
    void chooseCampStatus(int id);
    void campStatusKey(SDL_Keycode key);

    // ---- My Page (MyPageNewTopMain 0xdd, MyPageNewTopMenu 0xde; CreateMyPageMenu: the header 0x42
    // titled 8, ItemFutterMenu 0x62 - the team's name, the back) ----
    // Behind: comBgPosition's comBgMenu and its comBgMark_id00. mpTopPosition (mpTopPosition_in: its
    // two joints slid in from -8 / +8 over 0.1 s) holds mpTopLog and mpTopMenu. The log (SetListLogData): 12 mpTopLogTxt on
    // mpTopLogList's call_mpTopLogTxt_001 .., 0.48 apart, eight showing - its label (AnnounceMenuName
    // 0x2c7 + n) at typeM_L01, its value at typeM_R01 (right): the play time (0x2c0: {hour} at
    // least two digits, {minute}, {second}), enemy kills (0x2c6), battle mode matches (0x2c2), the
    // most damage (0x2c3), quests completed, genes obtained, gene combinations, gift fusions (0x2c2),
    // the N, R and SR genes held - stock too - (0x2c4: GetGeneRaretyNumber, gene_para +8) and their
    // mean level (0x2c5: GetGeneMeanLevel); Japanese in full-width digits (Cr3UtilNumberToUTFDouble
    // String). Dragged it scrolls 0 .. 1.92, settling on a row (AutoScroll), mpTopBarMenu's
    // mpTopBarCursor moved down its bar (barLimitUp .. barLimitLow + 0.15) as far. The menu's four
    // buttons (tapAreaS0n / E0n; the menu at frame n pressed - SE com_003, com_005 where off): 1 story
    // mode - only in the VS mode -, 2 the VS mode - only outside it (mpTopStoryOff / mpTopVSOff over
    // the one you're in, its mpTopIcon not drawn) -, asked (0x2ee / 0x2ef); 3 the presents (off -
    // mpTopPresentOff - with none; mpTopCome with their count in two mpTopNum digits); 4 the picture
    // book. mpTopIcon at call_mpTopIcon01 .. 04, its frame the button - 1. Over 80 presents: told
    // 0x2f5 on the way in
    struct Cr3MyPage {
        float since = 0;
        float scroll = 0;     // (+0x1bc: the log's, 0 .. 1.92)
        float dragY = 0;
        bool dragging = false;
        int pressed = 0;      // (the menu's frame: the button)
        int buttonFocus = 0, presentButton = 0, askFocus = 0;
        int ask = 0;          // (1 / 2: the button asked about)
        bool vs = false;      // (opened in the VS mode: GameMain +0x24b)
        std::string told;
        float toldSince = 0;
        int screen = 0;       // 0 the top, 1 the presents, 2 a gene to discard for one, 3 the picture book
        float screenSince = 0;
        std::vector<int> rows; // (1: the presents listed - cr3Presents indices; -1 Accept All, first with two or more)
        int row = 0;
        int present = -1;      // (2: the present whose gene there's no room for)
        std::vector<int> genes; // (2: the cards - cr3Genes indices; -1 the present's own gene)
        int focus = 0;
        int asked = -1;        // (2: the card asked about)
        int bookTab = 7;       // (3: mpCatTopMat_fr00's chosen - 0 .. 5 the kinds 1 .. 6, 6 kind 0, 7 all)
        int bookPage = 0;      // (3: the spread's left page, from 0 - even)
        int profile = 0;       // (3: the gene whose profile shows; 0 none)
        int rarity = 0;        // (3: the profile's rarity button - collection_book<n + 1>)
        float profileSince = 0;
    } myPage;
    void openMyPage();
    std::string cr3Number(long v, int digits = 1);
    std::string myPageValue(int row);
    bool myPageButtonOn(int b) const;
    void drawMyPage(int w, int h, const Mat4& view);
    void chooseMyPage(int id);

    // ---- My Page's Gene Index (PictureBookMain 0xc6, PictureBookMenu; MyPageNewTopMain 0xd: the
    // header and footer gone, its own back button) ----
    // collection_book's lines (DAT_0141e0a8), six to a page, two pages a spread: SetGeneData - all
    // (mpCatTopMat_fr00 7, the first chosen): every line, the highest of its genes ever had (else an
    // empty card); a kind (tabs 0 .. 5 kinds 1 .. 6, tab 6 kind 0 - gene_para +0x30): the lines with
    // one of that kind ever had, its highest. mpCatTopPositon holds mpCatTopBgMenu (fit0: the title
    // 0x21c, the collection rate - 0x21d, those had of all x 100, three mpCatNum01_fr00 -, the page
    // - the spread's right - and of how many in two mpCatNum02_fr00 each, the eight tabs - the
    // others at 0.75 x, half lit), the two mpCatTopCardMenu (each six mpCatTopCardPanel: card_default
    // 3.6 x at call_GeneCard, the line's _id in three mpCatNum03_fr00 - leading zeros hidden -,
    // mpCatTopNew over those not yet looked at) and mpCatTopArrowMenu (with more than two pages:
    // turns the spread). A card tapped: looked at (CheckTapCard), its profile (SetCardProfile:
    // mpCatDataPositon_in over 5 frames, mpCatDataMenu at its fit0 - 0x21e at typeM_R01, the gene's
    // name at typeM_C01, its card 5.6 x at call_GeneCard, collection_book<n + 1>'s text of the line
    // at typeM_L01 .. L12, broken at its \ - Cr3UtilNPrint -, a mpCatRarity_fr00 for each of N ..
    // its own grade - frame 2n chosen, 2n + 1 not; the arrows the list's next or last gene, looked
    // at too); tapped outside it, the profile closes
    int bookCursor = 0;
    bool bookTabsFocus = false;
    bool geneIndexKey(SDL_Keycode key);
    void openGeneIndex();
    std::vector<std::pair<int, int>> geneIndexCards(int& had) const;
    int geneIndexNew() const;
    std::vector<std::string> geneProfileLines(int gene, int rarity);
    void drawGeneIndex(int w, int h, const Mat4& view);
    // A gene card's page (GeneCardMenu - InterfaceMain::SetGeneCardStatus with the profile's gene:
    // DBSystem::ShowCard puts its series' NNN.jpg, its grade's r_NN.png and its kind's e_NN.png -
    // gene_para +4, +8, +0x30 - beside index.html in the temp folder and shows that in the web
    // view): the picture fitted to the screen's height (or width), centred, from the top, on
    // black (index.html's resizeImage, style.css's .wrap); a tap - api://close - closes it. Its
    // files missing, nothing: the menu ends at once. The pictures lie on their side, for a
    // landscape-locked phone turned to see the card upright: as so on Android; on a desktop's
    // screen, which isn't turned, the card stood upright (a quarter turn anticlockwise), fitted
    // to the height.
    struct Cr3CardView {
        int gene = 0;
        cr3::Texture layers[3];
    } cardView;
    void openCr3CardView(int gene);
    void closeCr3CardView();
    void drawCr3CardView();
    void chooseGeneIndex(int id);
    void openPresents();
    void presentRows();
    std::string presentName(int k);
    bool presentTakeable(int k) const;
    void drawPresents(int w, int h, const Mat4& view);
    void closePresents();
    void choosePresents(int id);
    void acceptPresent(int k);
    void drawPresentDiscard(int w, int h, const Mat4& view);
    void choosePresentDiscard(int id);
    void myPageKey(SDL_Keycode key);
    void myPageScroll(float by);
    void myPagePadScroll(float axis, float dt);

    // ---- the camp's Options (OptionMain 0xac, OptionMenu 0xad) ----
    // optTopPositon (its io animator): the tabs optTab_fr00 (the frame the tab; Sound, Controls,
    // Other at typeM_C01 .. C03 - AnnounceMenuName 0x190 .. 0x192), then the tab's: Sound -
    // optSltMenu's optSltPanel_001 .. 003 (BGM, Sound Effects, Voiceovers 0x198 .. 0x19a at
    // typeM_L00), each an optSltVolume - optSltBar_fr00 at the volume's frame, optSltBarCursor
    // moved along (barLimitLow - 0.23) - (barLimitUp + 0.23) x the volume / 100 (SetBarControlPoint);
    // Controls - optHndMenu's two optHnd_fr00 (Two-Handed, One-Handed 0x19b / 0x19c at typeM_C01;
    // the chosen at frame 1 - fieldTouchMode: the field's camera half, right or top); Other - optSltLRPanel's Resolution
    // (0x230: Low, Medium, High 0x22b .. 0x22d - the port's quality) with optSltLbtn / optSltRbtn.
    // The header title 7, its line the tab's (0x19f .. 0x1a1); left - the volumes kept
    // (Cr3Util_Save_SystemData)
    vitaimport::Session saveImport;
    struct Cr3CampOptions {
        int tab = 0;
        int otherRow = 0; // 0 resolution, 1 the language (where there's more than one), 2 Vita save import
        int hands = 0; // (Controls: 0 two-handed, 1 one-handed)
        int from = 0;  // (CR3-U's: 0 the camp's, 1 the title's - its own Options button -, 2 a battle's get-set Options)
        int top = 0;   // (Graphics: the first row shown)
        bool footer = false;
        bool asking = false; // (CR3-U's Reset: its question up)
        int askFocus = 1;    // (0 Yes, 1 No)
        float askSince = 0;
    } campOptions;
    void openCr3OptionsFrom(int from);
    bool cr3OptionsOver() const;
    void drawCr3OptionsOver(int w, int h);
    bool cr3OptionsClick(float fx, float fy);
    static constexpr int kTapTitleOptions = 7000;
    void cr3OptionsInput();
    void openCampOptions();
    // CR3-U's fourth tab, Graphics: the game's three-tab bar (optTab_fr00) drawn twice half a tab
    // apart - the left one without its right end (bg_03, bg_06, frame_03), the right one without its
    // left (bg_01, bg_04, frame_01) -, so their dividers fall at -1, 0 and +1 tab: four tabs. The
    // chosen one's highlight (tap_*, the bar's frame its tab) from the half that has it, drawn last
    static constexpr int kOptionTabs = 4, kGraphicsRows = 4;
    static constexpr int kTapGraphicsRow = 1000, kTapGraphicsUp = 1100, kTapGraphicsDown = 1101;
    static constexpr int kTapOptionsReset = 1200, kTapResetYes = 1201, kTapResetNo = 1202;
    void drawCr3OptionTabs(int chosen, float x, float y, int w, int h, const Mat4& view);
    float cr3OptionsScale(int w, int h) const;
    float cr3OptionsFloor(int w, int h) const;
    void drawCr3GraphicsTab(const UiFig& menu, int w, int h, const Mat4& view);
    void drawCr3OptionsReset(float backX, float backY, int w, int h, const Mat4& view);
    std::string cr3OptionsResetAsk() const;
    void cr3OptionsReset();
    void saveImportAnswer(bool yes);
    void drawCampOptions(int w, int h, const Mat4& view);
    void chooseCampOptions(int id);
    void campOptionsKey(SDL_Keycode key);

    // ---- CR3's VS mode (VSMain, VsModeMain, VsModeMenu ...) ----
    // VSMain::OnUpdate: GetVSTournaments and the day's mission, VsModeMenu (VsPosition's joints: the
    // dark VsMainBlack, VsHeMenu - its typeM_L01, the coins / ML / clock in VsHeNum_fr00 digits -,
    // VsFtrMenu - VsFtrTop's week score (VsFtrNumL_fr00) and the tournament's (VsFtrNumS_fr00) - and
    // by the screen: A VsMainLsideA (the banner, the team's level) and VsMainRsideA (VsMainBtn01 -
    // Battle - and 02); B VsMainLsideB (the party: VsMainParty x 3, a gene's card and its level, HP,
    // MP) and VsMainRsideB (VsMainBtn03 normal tournaments, 04 special); the list VsMainRsideC
    // (VsModeListMenu: VsListTitel, VsListList rows 1.4 apart - the tournament's name, VSmode_name,
    // at typeM_L00, its level at typeM_R00, VsListClear once cleared, VsListBoss_fr00 its icon);
    // the detail VsContMenu (TournamentDitailMenu: VsContBase - the name at typeM_C01, VSmode_help's
    // lines at typeM_L01_001 .., the prize's VsContItem_fr00, VsContOk). OK: BeginVSTournament, then
    // VS_BattleIn - FldUtilReserveVsBattleIN: the tournament's field (mapId), level, music, its
    // couplings in turn. A battle won: its score (the party's damage) and the clear bonus (score x
    // the MP the party used / 100) on VsColosseumResultMenu (clVsPositon: clVsNumLPanel, three
    // clVsNumSPanel with clText_fr00 0 .. 2), EndVSBattle(score, mission); the last won or one
    // lost: EndVSTournament and its prizes, back to B
    struct Cr3VsMode {
        int screen = 0;        // 0 off, 1 the top (A), 2 Battle (B), 3 the list, 4 the detail, 5 the battles, 6 a battle's score, 7 the tournament's end
        float since = 0;
        int mode = 0;          // (the list's: vstournaments' mode - 0 normal, 1 special)
        std::vector<int> rows; // (vsTournaments() indices)
        int row = 0;
        int chosen = -1;       // (vsTournaments() index)
        int battle = 0;        // (the coupling in course)
        long score = 0, mpBonus = 0, total = 0; // (the last battle's)
        bool lost = false;
        float scoreAt = 0;     // (6: when the score came up - uiClock)
        // (8 the Sp Shop, 9 the tournament's results - then 7 its prizes)
        std::vector<int> boosts; // (the lifeline items chosen: vsboostitems ids, four at most)
        int boostFocus = 0;      // (the last touched: its name shown)
        int wildChosen[3] = {-1, -1, -1}; // (each battle's wildcard: wildcards() indices)
        bool wildOpen = false;   // (the battle's wildcards up)
        float wildAt = 0;
        long itemBonus = 0, wildBonus = 0, endScore = 0;
        int ask = 0;             // (asked: 1 the tournament entered, 2 MP recovered, 3 goods bought, 4 a box opened)
        // (10 the Sp Shop's top, 11 its boxes - LuckyBoxMenu: the box in focus, its buy panels up)
        int boxFocus = 0, boxPanel = 0;
        int buttonFocus = 0, shopFocus = 0;
        bool boxCheckFocus = false;
        std::vector<std::string> boxContents;
        int boxContentsPage = 0;
        bool boxBuy = false;
        bool shopOnly = false;   // (the Sp Shop opened from the field's Shop button: back closes it)
        std::string told;        // (an alert over it)
        float toldSince = 0;
    } vs;
    std::vector<Cr3GeneLabTap> vsTaps;
    std::string vsText(const char* file, int id);
    // The VS mode's own scripts (round 10; VSMain 0x96e25c / 0x96e2dc, VsModeMain::Update 0xb6b600):
    // on each entry vs.nut's evt000 (the tutorial's steps: SetVistaEvent - a vista, VS again after
    // it), else the menu; the menu's hooks run over it (state 0x24: its taps held) - VS_Top on the top
    // (entered, back from the camp or the gene lab), VS_Top2 on Battle, VS_Normal_Top on the normal
    // list, VS_Normal_SelectedFullItem once four lifeline items are chosen, VS_MPRecovery_End after
    // BuyMP -, and VS_BattleIn as a tournament starts: its vista (s910_t01_99999 / 00801) reserves
    // the battle (Cr3System.VsBattleIn - its battles after the vista). The menu's buttons greyed and
    // shut by flags 3601 .. 3612 (SetButtonDiffuse 0xb5f4e8: 0.5); CreateOverLay's mask (OverLayMain
    // 0xdf: black at 0.7, under the script's windows)
    std::unique_ptr<cr3::EventRuntime> vsEvents;
    float vsEventClock = 0;
    bool vsOverlay = false;
    bool vsFromVista = false;     // (a vista VS reserved: back to VS after it - its battle, or VSMain anew)
    int vsPendingTournament = -1; // (VsBattleIn's id: the tournament's battles once the vista's over)
    bool vsCampOpened = false, vsLabOpened = false;
    bool vsBattleHookPending = false; // (VS_BattleIn running: its battles once it's over)
    void vsEnter();               // (VSMain: evt000, then the vista it reserved or the menu)
    void openVsMenu();
    void runVsScript(const std::string& function);
    bool vsScriptRunning() const;
    void stepVsScript(float dt);
    bool vsVistaReserved();       // (after a VS script: a vista it reserved started)
    void vsScriptEnded();
    void bindVsNatives(cr3::ScriptVM& vm);
    bool vsButtonLocked(int id) const;
    // InterfaceWindow.CreateGeneCardSelector(gene, text, gene, text, gene, text) (OpenGeneCardSelector
    // 0x8f8778, menu 0xe1): three gene cards on VsWildNull's VsWildTouch_002 .. 004, AnnounceBattle
    // 0x32 above; a press puts one in focus (its text shown), a press on it again takes it - the
    // others dimmed, com_003 and bt_603 -, no way back; GetGeneCardSelectorResult its index (-1 till
    // then), CheckAliveGeneCardSelector while it's up (its out: 0.5 s)
    struct GeneCardSelector {
        bool open = false;
        int genes[3] = {}, texts[3] = {};
        int focus = -1, result = -1;
        float since = 0, closeAt = -1;
        cr3::EventRuntime* events = nullptr; // (whose shared text the cards' are)
    } geneSelector;
    std::vector<Cr3GeneLabTap> geneSelectorTaps;
    void drawGeneSelector(int w, int h);
    bool geneSelectorClick(float fx, float fy);
    bool geneSelectorKey(SDL_Keycode key);
    void chooseGeneSelector(int k);
    void openVs();
    void openPremiumShop();
    // ---- the Sp Shop (Fld2Main::SM_Shop - CreatePiucomTop: HeaderMenu 0x42 title 9, SShopMenu 0x74) ----
    // Its top (SShopMenu: SshopPositon's call_SshopBg / call_SshopBox / call_SshopItem /
    // call_SshopVipPanel - the VIP days, SshopVipCount and SshopVipNum's digits, or SshopVipDefault;
    // the header's sentence 0x275): the Box (+0x70 1 - SShopMenu 0x11, 0x12: CreateLuckyBoxMenu),
    // the Item (2 - 0xc: the goods - CR3-U's PiuComMenu list) and the VIP panel (3 - not in).
    // The boxes (LuckyBoxMenu, interface 0x44; LuckyBoxMain 0x7a's header title 0xb): the gene boxes
    // on offer (GeneBox: cltList_gene_01, boxBg_gene_01; GENEBOX_PRICES / _AMOUNTS in coins), a Lucky
    // Box for each item sheet won from (any entry's +0x30 count above 0 - LuckyBox: cltList_<sheet>,
    // boxBg_<sheet> - boxBg_Colosseum_01 for the VS sheets, names with an e or v -;
    // LUCKYBOX_PRICES / _AMOUNTS in coins) and the GP box (GpBox: cltList_gp_01, boxBg_gp_01;
    // GPBOX_PRICES / _AMOUNTS in GP). mpBoxBgPositon's board (mpBoxBg_id00's mat_boxBg_d02 the box's
    // background, mpBoxName_id00's mat_cltList_d02 its name plate), mpBoxPositon's mpBoxMenu - the
    // box in focus and its neighbours on call_mpBoxImagePanel (mpBoxImagePanel: mpBoxImage_id00) -,
    // mpBoxNumMenu (the box's place and the count, mpBoxNum_num00's mpBoxNoNum_fr digits) and
    // mpBoxArrowMenu (looped). The box tapped: mpBoxBuyPositon's mpBoxBuyMenu, three BoxBuyPanels
    // (mpBoxBuyPanel: mpBoxBuyIcon_fr00 the amount - 1, 5, 10 frames 0 .. 2 -, mpBoxBuyUni_fr00 the
    // currency - 0 coins, 1 GP -, mpBoxBuyPriceNum_fr00 its digits at digit + 10 x currency). A panel
    // tapped: asked 0x123; yes - LuckyBoxMain 5: OpenLuckyBox / OpenGeneBox / OpenGPBox, flag 39 set
    // (the Using Coins quest's), com_024, the header's coins again; what came out told
    struct SpBox {
        int kind = 0;         // 0 a gene box, 1 a Lucky Box, 2 the GP box
        int id = 0;           // (the gene box's _id; the Lucky Box's sheet index)
        std::string texture, background;
        int currency = 0;     // 0 coins, 1 GP
        int price[3] = {}, amount[3] = {};
    };
    std::vector<SpBox> spBoxes();
    bool cr3UiCallNth(const UiFig& u, const std::string& prefix, int n, float& x, float& y);
    void spTap(const UiFig& u, float px, float py, float hw, float hh, int id);
    void drawSpShop(int w, int h, const Mat4& view);
    void openSpBoxes();
    void spBoxContentsPage();
    void inspectSpBox();
    void openSpBox();
    void vsList(int mode);
    void vsDigits(const UiFig& panel, const std::string& prefix, const std::string& fig, long value, int places, const Mat4& view, int offset = 0);

    // ---- the VS mode's extras ----
    // Lifeline items (VsModeMenu's list screen: VsMainLsideC - VsListItemMenu: the eight boost items
    // (data\vsboostitems by _id: icon, type, const, price) as VsListItemB_fr00 at frame id - 1 with
    // VsListStock - VsListNum_fr00's three digits: the held count, or its price with VsListTxt_fr00
    // at frame 1 -; four chosen ones VsListItemA_fr00 (AddVsListItemAIcon: frame id - 1), the focused's
    // name at typeM_C01 (BoostItemName); the footer VsFtrList's VsFteCoin - the coins it costs):
    // a tournament entered asks (0x2bf with the coins, else 0x306) and BeginVSTournament(the chosen).
    // In battle (BtlBoostItem) type 0 gives the party its status effect (const) for the battle, type 1
    // its command at each turn's end (79, Format: the party's passing effects cured); the score's
    // second bonus is (BoostItemBonus's percent for how many - 0, 2, 4, 8, 10 - + the most kind-130
    // effect) x the score / 100 (phaseColosseumResult)
    struct VsBoost {
        int id = 0, icon = 0, type = 0, constant = 0, price = 0;
    };
    std::vector<VsBoost> vsBoostTable();
    std::string vsBoostName(int id);
    int vsBoostCost();
    int vsBoostBonusPercent(int n);
    void drawVsBoosts(int w, int h, const Mat4& view);
    void drawVsListFooter(int w, int h, const Mat4& view);
    // Wildcards (BtlSysStateSelect::phaseOpenWildCard - WildCardMenu, VsWildNull_in: the five picked,
    // VsWildTouch 1.86 apart - each a w_card_default with w_card_<id> on its "picture", its bonus %
    // on VsWildBonus; Cr3UtilGetAnnounceData 0x31 told). One chosen each battle (BtlWildCard::Select:
    // three slots), then Apply - by its effect (WildCard +0xc): 1 the status effects (its constants)
    // on its targets (+0x8: 1 the party, 2 the monsters, 3 everyone, 4 / 5 the characters with
    // char_para +0x0c bit 1 / 2), 2 the break gauge set, 3 the targets' HP to that percent; IsThisTurn
    // (+0x28 bit 0) only the battle it was chosen in, else the later ones too; the score's third
    // bonus is the chosen ones' bonuses (+0x4) summed x the score / 100
    struct VsWildCard {
        int id = 0, bonus = 0, target = 0, effect = 0;
        std::vector<int> constants;
        bool thisTurn = false;
    };
    bool vsWildCard(int id, VsWildCard& out);
    bool vsWildPending() const;
    int vsWildFocus = 0;
    void drawVsWildCards(int w, int h);
    void chooseVsWildCard(int k);
    void applyVsWildCard(int slot);
    void applyVsBattleStart();
    void vsScoreBonuses();
    // MP recovery (VsModeMain 0xb: 0x2a9 asked, BuyMP - VS_MP_PRICE coins - the party's MP full)
    // and the Sp Shop (VsModeMenu's Btn02 - PiuComMenu: data\shops' "com_1" goods, GP or coins -
    // BuyGoods(shop, goods, 1)): a list of comListPanel rows, the goods' icon, its first item's name
    // and count, its price (comListNumUn_num00 - comListUnIcon_fr00 0 coins, 1 GP, 2 gold); tapped
    // again, asked (0x123) and bought - or not enough (0xfe ML, 0xff GP, 0x100 coins)
    struct VsGoods {
        int id = 0, icon = 0, gold = 0, gp = 0, coin = 0, item = 0, count = 0, flag = -1;
    };
    std::vector<VsGoods> vsShopGoods();
    // The gold key shop (EventRuntime::keyBuyShown - FieldBuyMenu, interface 0xcd): asked first
    // (SetBasicYNChoiceWindow: AnnounceMenuName 0x23b - {itemname} the key's, {coin} the coins held);
    // yes, how many (CampItemNumberSelect's reels, the price in coins for them and the keys held -
    // up to what's held room for, GetBuyCanItemNumbers - with CreateShopBuySelectYN's question 0x248),
    // OK only within the coins; yes, BuyGoods(shop "com_1", the key's goods, n) and com_024; no or
    // back, closed - nothing bought
    struct Cr3KeyBuy {
        int stage = 0; // 0 none, 1 asked, 2 how many
        float since = 0;
        int amount = 1;
        bool wasShown = false;
    } keyBuy;
    std::vector<Cr3GeneLabTap> keyBuyTaps;
    static constexpr int kTapKeyYes = 1201, kTapKeyNo = 1202, kTapKeyReel = 1210;
    bool keyGoods(VsGoods& out);
    int keysRoom() const;
    void stepKeyBuy(float dt);
    void chooseKeyBuy(int id);
    void drawKeyBuy(int w, int h);
    bool keyBuyClick(float fx, float fy);
    void drawVsShop(int w, int h, const Mat4& view);
    void drawVsResults(int w, int h, const Mat4& view);
    void drawVs(int w, int h);
    void drawVsScore(int w, int h);
    void vsNextBattle();
    bool vsMoreBattles() const;
    bool vsBattleOver(bool won);
    void vsEnd();
    void chooseVs(int id);
    void vsAnswered();
    bool vsClick(float fx, float fy);
    bool vsShoulder(bool right);
    bool vsKey(SDL_Keycode key);
    void drawCr3Result(int w, int h);

    static constexpr std::array<float, 3> kCr3SpellCardSoundTimes = {10.0f / 30, 15.0f / 30, 20.0f / 30};
    static float cr3SpellCardDuration(size_t count);
    void drawCr3SpellCards(int w, int h, float elapsed, const std::vector<int>& geneIds);
    void drawCr3BattleHud(int w, int h);

    // The movies (Cr3System.PlayMovie - MovieControlTask: MVGL::AV::MediaPlayer over the game,
    // IsPlayingMovie till its end): cNN.mp4 from the release (the iPad's .app, the Vita's movie/,
    // Android's CRDBmov pack), else movies/ in the game folder or CR3_MOVIES (another release's -
    // the iPad lacks c04..c11); its sound at the music's volume; its subtitles (MovieControlTask::SM_Set:
    // "<name>_jp.srt" in Japanese - the fan translation's English too -, else "_en") along the bottom.
    // A tap shows Skip (the iOS release's btn_movie_skip), a second within 3 s ends it; --input
    // movie:skip ends it
    struct MovieShow {
        std::unique_ptr<cr3::Movie> mv;
        std::string name;
        float clock = 0, skipShown = -10;
        int shown = 0; // (pictures decoded)
        bool ended = false, paused = false;
        cr3::Texture tex;
        std::vector<uint8_t> rgba;
        std::vector<cr3::Subtitle> subs;
    } movie;
    std::string moviesDir; // (movies/ in the game folder, or CR3_MOVIES)
    bool movieBytes(const std::string& file, std::vector<uint8_t>& out);
    void startMovie(const std::string& name);
    void endMovie();
    void stepMovie(float dt);
    bool movieTap();
    bool moviePad(SDL_GamepadButton button);
    float vistaControlsUntil = -1;
    bool vistaPad(SDL_GamepadButton button);
    void drawMovie(int w, int h);
    void drawStaffroll(const cr3::EventRuntime& ev, int w, int h);
    void drawEventUi();
};

} // namespace cr3_viewer
