#include "viewer/viewer.hpp"

namespace cr3_viewer {

#include "gfx/dotnumtable.inc"

std::vector<std::string> Viewer::hdReplaced(const std::string& wearer) {
    if (!hdModels || modelsDir.empty() || wearer.empty()) return {};
    const auto it = hdReplacedCache.find(wearer);
    if (it != hdReplacedCache.end()) return it->second;
    std::vector<std::string>& out = hdReplacedCache[wearer];
    int count = 0;
    if (char** names = SDL_GlobDirectory(modelsDir.c_str(), nullptr, SDL_GLOB_CASEINSENSITIVE, &count)) {
        for (int k = 0; k < count; ++k) {
            std::string n = names[k];
            for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
            for (const char* ext : {".glb", ".gltf"}) {
                const size_t e = std::strlen(ext);
                if (n.size() > e && n.compare(n.size() - e, e, ext) == 0 && n.compare(0, wearer.size(), wearer) == 0 && !addons.byFigure(n.substr(0, n.size() - e)))
                    out.push_back(n.substr(0, n.size() - e));
            }
        }
        SDL_free(names);
    }
    return out;
}

bool Viewer::addonOwnModel(const cr3::Cr3Addon& a) { return cr3::Cr3Addons::ownModel(a, hdReplaced(a.wearer)); }

const char* Viewer::settingsFile() { return "settings.ini"; }

void Viewer::loadSettings() {
    std::FILE* f = std::fopen(savePath(settingsFile()).c_str(), "rb");
    if (!f) return;
    char line[256];
    Settings& s = settings;
    while (std::fgets(line, sizeof line, f)) {
        char key[64];
        int v = 0;
        if (std::sscanf(line, " %63[a-z_] = %d", key, &v) != 2) continue;
        const std::string k = key;
        if (k == "display") s.display = std::clamp(v, 0, 2);
        else if (k == "resolution") s.resolution = std::clamp(v, 0, int(std::size(kResolutions)) - 1);
        else if (k == "vsync") s.vsync = v != 0;
        else if (k == "fps") s.fps = std::clamp(v, 0, 1000);
        else if (k == "renderer") s.renderer = std::clamp(v, 0, 1);
        else if (k == "upscaler") s.upscaler = std::clamp(v, 0, 4);
        else if (k == "quality") s.quality = std::clamp(v, 0, int(std::size(kQualities)) - 1);
        else if (k == "sharpness") s.sharpness = std::clamp(v, 0, 100);
        else if (k == "msaa") s.msaa = v >= 8 ? 8 : v >= 4 ? 4 : v >= 2 ? 2 : 1;
        else if (k == "anisotropy") s.anisotropy = v >= 16 ? 16 : v >= 8 ? 8 : v >= 4 ? 4 : v >= 2 ? 2 : 1;
        else if (k == "sharp_textures") s.sharpTextures = v != 0;
        else if (k == "hd_textures") s.hdTextures = v != 0, s.hdSet = true;
        else if (k == "hd_models") s.hdModels = v != 0;
        else if (k == "smooth_models") s.smoothModels = v == 2 || v == 3 ? v : 0;
        else if (k == "original_card_art") s.originalCardArt = v != 0;
        else if (k == "lock_orientation") s.lockOrientation = v != 0;
        else if (k == "landscape_flipped") s.landscapeFlipped = v != 0;
        else if (k == "pad_swap") s.padSwap = v != 0;
        else if (k == "field_speed") s.fieldSpeed = std::clamp(v, 1, 5);
        else if (k == "battle_speed") s.battleSpeed = std::clamp(v, 1, 5);
        else if (k == "language") s.language = std::clamp(v, 0, 4);
    }
    std::fclose(f);
}

void Viewer::saveSettings() const {
    if (!settingsPersist) return;
    const std::string keepData = storedSetting("data="), keepPatch = storedSetting("patch="); // (the setup's, kept - read before the file is rewritten)
    char* serialized = nullptr;
    const Settings& s = settings;
    if (SDL_asprintf(&serialized, "# CR3-U: the Options' Graphics tab (the command line's flags go over these)\n"
                    "display=%d\nresolution=%d\nvsync=%d\nfps=%d\nrenderer=%d\nupscaler=%d\nquality=%d\nsharpness=%d\n"
                    "msaa=%d\nanisotropy=%d\nsharp_textures=%d\nhd_textures=%d\nhd_models=%d\nsmooth_models=%d\npad_swap=%d\nfield_speed=%d\nbattle_speed=%d\n",
                 s.display, s.resolution, s.vsync ? 1 : 0, s.fps, s.renderer, s.upscaler, s.quality, s.sharpness, s.msaa,
                 s.anisotropy, s.sharpTextures ? 1 : 0, s.hdTextures ? 1 : 0, s.hdModels ? 1 : 0, s.smoothModels, s.padSwap ? 1 : 0, s.fieldSpeed,
                 s.battleSpeed) < 0 || !serialized) return;
    std::string text = serialized;
    SDL_free(serialized);
    text += "lock_orientation=" + std::to_string(int(s.lockOrientation)) + "\nlandscape_flipped=" + std::to_string(int(s.landscapeFlipped)) + "\n";
    text += "original_card_art=" + std::to_string(int(s.originalCardArt)) + "\n";
    if (s.language) text += "language=" + std::to_string(s.language) + "\n";
    if (!keepData.empty()) text += "data=" + keepData + "\n";
    if (!keepPatch.empty()) text += "patch=" + keepPatch + "\n";
    std::string error;
    if (!crstorage::replaceFile(savePath(settingsFile()), text, &error)) SDL_Log("settings: %s", error.c_str());
}

// The settings onto the device, the window and the textures (each only as it changes)
void Viewer::applySettings() {
    if (!dev) return;
    Settings& s = settings;
    if (!vsyncApplied || appliedVsync != s.vsync) dev->setVSync(s.vsync), appliedVsync = s.vsync, vsyncApplied = true;
    const int r = std::clamp(s.resolution, 0, int(std::size(kResolutions)) - 1);
    if (s.resolution >= 0 && (resW != kResolutions[r][0] || resH != kResolutions[r][1])) {
        resW = kResolutions[r][0], resH = kResolutions[r][1];
        dev->resetHistory();
    }
    cr3::UpscaleSettings u;
    u.mode = cr3::Upscaler(std::clamp(s.upscaler, 0, 4));
    u.ratio = kQualities[std::clamp(s.quality, 0, int(std::size(kQualities)) - 1)].ratio;
    u.sharpness = float(s.sharpness) / 100.0f;
    if (u.mode != cr3::Upscaler::Off && !dev->upscalerProblem(u.mode).empty()) u.mode = cr3::Upscaler::Off;
    if (u.mode != upscale.mode || u.ratio != upscale.ratio || u.sharpness != upscale.sharpness) upscale = u, dev->setUpscaler(u);
    const int samples = upscale.mode == cr3::Upscaler::Off ? s.msaa : 1;
    if (samples != appliedMsaa) dev->setSamples(samples), appliedMsaa = samples;
    cr3::TextureFiltering tf;
    tf.anisotropy = s.anisotropy;
    tf.sharp = s.sharpTextures;
    // (an upscaler's scene is smaller: the mips its output would take - DLSS's and FSR's advice)
    tf.lodBias = upscale.mode != cr3::Upscaler::Off ? -std::log2(upscale.ratio) : 0.0f;
    dev->setFiltering(tf);
    if (window && s.display != appliedDisplay) {
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
        SDL_SetWindowFullscreen(window, true); // (a phone's or tablet's: the whole screen, the system's bars hidden - SDL's immersive mode)
#else
        if (s.display == 1) SDL_SetWindowFullscreenMode(window, SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window)));
        else SDL_SetWindowFullscreenMode(window, nullptr);
        SDL_SetWindowFullscreen(window, s.display != 0);
#endif
        appliedDisplay = s.display;
    }
    const bool artChanged = cardArtwork.setEnabled(s.originalCardArt);
    if (s.hdTextures != hdTextures || artChanged) hdTextures = s.hdTextures, reloadTextures();
    if (artChanged && cardView.gene) openCr3CardView(cardView.gene);
    if (hdModels != s.hdModels) hdReplacedCache.clear();
    if (!orientationApplied || appliedLock != s.lockOrientation || appliedFlip != s.landscapeFlipped) {
        mobile::orientation(window, s.lockOrientation, s.landscapeFlipped);
        appliedLock = s.lockOrientation, appliedFlip = s.landscapeFlipped, orientationApplied = true;
    }
    hdModels = s.hdModels; // (from the next figure loaded)
}

bool Viewer::loadHd(const cr3::Image& img, cr3::Image& hd) {
    std::string why;
    if (cr3::loadHdImage(hdDir, img, hd, &why)) return true;
    if (!why.empty() && hdRefused.insert(why).second) SDL_Log("hd textures: %s - left out", why.c_str());
    return false;
}

void Viewer::reloadTexture(const std::string& name, cr3::Texture& t) {
    cr3::Image img;
    if (!decode(name, img)) return;
    if (hdTextures && !hdDir.empty()) {
        cr3::Image hd;
        if (loadHd(img, hd)) hd.logicalW = img.width, hd.logicalH = img.height, img = std::move(hd), ++hdLoaded;
        else ++hdMissing;
    }
    img.mipmaps = textureMips.count(name) ? textureMips[name] : true;
    cr3::Texture old = t;
    t = dev->upload(img);
    dev->destroy(old);
}

// Every cached texture again (HD Textures turned on or off): in place, so what holds one sees the new
void Viewer::reloadTextures() {
    hdLoaded = hdMissing = 0;
    for (auto& [name, t] : textures)
        if (t.id) reloadTexture(name, t);
    SDL_Log("hd textures %s: %d replaced, %d without one", hdTextures ? "on" : "off", hdLoaded, hdMissing);
}

// The list's rows: its label and value, its help (the list's window), what Left / Right / a
// tap does (+1 / -1)
// a folder as the settings' help shows it: its last two names (".../models/hd/")
std::string Viewer::shortDir(const std::string& dir) {
    size_t at = dir.size() > 1 ? dir.find_last_of("/\\", dir.size() - 2) : std::string::npos;
    if (at != std::string::npos && at > 0) at = dir.find_last_of("/\\", at - 1);
    return at == std::string::npos ? dir : "..." + dir.substr(at);
}

auto Viewer::settingRows() -> std::vector<SettingRow> {
    Settings& s = settings;
    std::vector<SettingRow> rows;
    auto step = [](int& v, const std::vector<int>& values, int dir) {
        size_t i = 0;
        while (i < values.size() && values[i] != v) ++i;
        const int n = int(values.size());
        v = values[size_t(((i < values.size() ? int(i) : 0) + dir + n) % n)];
    };
    auto onOff = [](bool b) { return std::string(b ? "On" : "Off"); };
    static const char* const kDisplay[] = {"Window", "Fullscreen", "Borderless"};
    rows.push_back({"Display", kDisplay[std::clamp(s.display, 0, 2)], "A window, fullscreen at the display's own mode, or borderless over the desktop",
                    true, [&s](int d) { s.display = (s.display + d + 3) % 3; }});
    {
        const int r = std::clamp(s.resolution, 0, int(std::size(kResolutions)) - 1);
        const std::string v = s.resolution < 0 ? std::to_string(resW) + " x " + std::to_string(resH) // (--resolution's own)
                              : r == 0          ? "Window"
                                                : std::to_string(kResolutions[r][0]) + " x " + std::to_string(kResolutions[r][1]);
        rows.push_back({"Resolution", v, "The frames' size (Window: the window's own), scaled to the window",
                        true, [&s](int d) { const int n = int(std::size(kResolutions)); s.resolution = s.resolution < 0 ? 0 : (s.resolution + d + n) % n; }});
    }
    rows.push_back({"VSync", onOff(s.vsync), "Each frame waits for the display's refresh: no tearing", true, [&s](int) { s.vsync = !s.vsync; }});
    rows.push_back({"Frame Rate", s.fps ? std::to_string(s.fps) : std::string("Unlimited"),
                    "The most frames drawn a second; the game runs at 20, smoothed between them",
                    true, [&s, step](int d) { step(s.fps, {30, 60, 120, 144, 165, 240, 0}, d); }});
    {
        static const char* const kRenderer[] = {"Vulkan", "OpenGL"};
        std::string v = kRenderer[std::clamp(s.renderer, 0, 1)];
        if (s.renderer != rendererInUse) v += " (next start)";
        rows.push_back({"Renderer", v, "Vulkan: the upscalers, MSAA and the faster path; OpenGL where Vulkan won't start",
                        true, [&s](int) { s.renderer = 1 - s.renderer; }});
    }
    const bool vk = dev && dev->offscreen(); // (the Vulkan device renders offscreen: the upscalers', MSAA's)
    static const char* const kUpscaler[] = {"Off", "FSR 3.1", "DLSS", "MetalFX", "FSR 4"};
    {
        std::string v = kUpscaler[std::clamp(s.upscaler, 0, 4)];
        std::string why = s.upscaler && dev ? dev->upscalerProblem(cr3::Upscaler(s.upscaler)) : std::string();
        if (!why.empty()) v += " (n/a)";
        rows.push_back({"Upscaler", v, why.empty() ? "The scene renders smaller and is rebuilt at full size; the menus stay sharp" : kUpscaler[s.upscaler] + std::string(": ") + why,
                        vk, [this, &s](int d) {
                            // (the ones this device can run, Off always)
                            for (int k = 1; k <= 5; ++k) {
                                const int u = ((s.upscaler + d * k) % 5 + 5) % 5;
                                if (u == 0 || (dev && dev->upscalerProblem(cr3::Upscaler(u)).empty())) {
                                    s.upscaler = u;
                                    return;
                                }
                            }
                        }});
    }
    {
        static const char* const kModes[] = {"Native AA", "Quality", "Balanced", "Performance", "Ultra Performance"};
        const int q = std::clamp(s.quality, 0, int(std::size(kQualities)) - 1);
        char h[96];
        std::snprintf(h, sizeof h, "The scene at %d%% of the frame per side%s", int(100.0f / kQualities[q].ratio + 0.5f),
                      q == 0 ? " (DLSS: DLAA)" : "");
        rows.push_back({"Upscale Mode", q == 0 && s.upscaler == 2 ? "DLAA" : kModes[q], h, vk && s.upscaler != 0,
                        [&s](int d) { const int n = int(std::size(kQualities)); s.quality = (s.quality + d + n) % n; }});
    }
    rows.push_back({"Sharpness", std::to_string(s.sharpness) + "%", "FSR's sharpening pass (RCAS); DLSS and MetalFX have none",
                    vk && (s.upscaler == 1 || s.upscaler == 4), [&s](int d) { s.sharpness = std::clamp(s.sharpness + d * 5, 0, 100); }});
    {
        const int most = dev ? dev->maxSamples() : 1;
        rows.push_back({"Anti-aliasing", s.msaa > 1 ? "MSAA " + std::to_string(s.msaa) + "x" : std::string("Off"),
                        s.upscaler ? "Off while an upscaler is on (its own anti-aliasing)" : "Multisampling: smoother edges",
                        vk && most > 1 && s.upscaler == 0, [&s, step, most](int d) {
                            std::vector<int> v{1};
                            for (int k = 2; k <= std::min(most, 8); k *= 2) v.push_back(k);
                            step(s.msaa, v, d);
                        }});
    }
    {
        const int most = dev ? dev->maxAnisotropy() : 1;
        rows.push_back({"Anisotropic Filtering", s.anisotropy > 1 ? std::to_string(std::min(s.anisotropy, std::max(most, 1))) + "x" : std::string("Off"),
                        most > 1 ? "Textures stay sharp on floors and walls seen at an angle" : "Not on this GPU",
                        most > 1, [&s, step](int d) { step(s.anisotropy, {1, 2, 4, 8, 16}, d); }});
    }
    rows.push_back({"Texture Filtering", s.sharpTextures ? "Sharp" : "Smooth",
                    "Sharp: square texels up close; Smooth: blended",
                    true, [&s](int) { s.sharpTextures = !s.sharpTextures; }});
    {
        SDL_PathInfo info;
        const bool have = !hdDir.empty() && SDL_GetPathInfo(hdDir.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
        rows.push_back({"HD Textures", onOff(s.hdTextures),
                        have ? "Upscaled textures from " + shortDir(hdDir) : "None in " + shortDir(hdDir) + " yet (the README's HD textures)",
                        true, [&s](int) { s.hdTextures = !s.hdTextures; }});
    }
    {
        SDL_PathInfo info;
        const bool have = !modelsDir.empty() && SDL_GetPathInfo(modelsDir.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
        rows.push_back({"HD Models", onOff(s.hdModels),
                        have ? "Replacement models from " + shortDir(modelsDir) + ", from the next scene"
                             : "None in " + shortDir(modelsDir) + " yet (the README's HD models)",
                        true, [&s](int) { s.hdModels = !s.hdModels; }});
    }
    {
        static const char* const kSmooth[] = {"Off", "", "Smooth", "Smoother"};
        rows.push_back({"Smooth Models", kSmooth[std::clamp(s.smoothModels, 0, 3)],
                        s.smoothModels ? "Rounder models (4x / 9x triangles), hard edges kept - from the next scene" : "The models as the game built them",
                        true, [&s](int d) { // (Off, Smooth, Smoother)
                            static const int kOrder[3] = {0, 2, 3};
                            int i = s.smoothModels == 2 ? 1 : s.smoothModels == 3 ? 2 : 0;
                            s.smoothModels = kOrder[(i + (d < 0 ? 2 : 1)) % 3];
                        }});
    }
    rows.push_back({"Original Card Art", s.originalCardArt && cardArtwork.available() < 3 ? "On (" + std::to_string(cardArtwork.available()) + "/3)" : onOff(s.originalCardArt),
                    s.originalCardArt && cardArtwork.available() < 3
                        ? "Some originals are missing. Add Vita or iOS card archives to artwork/."
                        : "Original art, names and profiles for cards 80, 81 and 86.",
                    true, [&s](int) { s.originalCardArt = !s.originalCardArt; }});
    rows.push_back({"Lock Orientation", onOff(s.lockOrientation),
                    "Hold the current landscape direction and restore it on restart", true, [this, &s](int) {
                        s.lockOrientation = !s.lockOrientation;
                        mobile::orientation(window, s.lockOrientation, s.landscapeFlipped, s.lockOrientation);
                        orientationApplied = false;
                    }});
    auto speed = [](int x) { return x > 1 ? std::to_string(x) + "x" : std::string("Off"); };
    rows.push_back({"Field Speed", speed(s.fieldSpeed), "Walking the field 2 to 5 times as fast; scenes, talk and menus as they are", true,
                    [&s](int d) { s.fieldSpeed = (s.fieldSpeed - 1 + d + 5) % 5 + 1; }});
    rows.push_back({"Battle Speed", speed(s.battleSpeed), "The battles 2 to 5 times as fast", true,
                    [&s](int d) { s.battleSpeed = (s.battleSpeed - 1 + d + 5) % 5 + 1; }});
    rows.push_back({"Swap \u25cb and \u00d7", onOff(s.padSwap),
                    s.padSwap ? "A controller's \u00d7 confirms and \u25cb goes back" : "A controller's \u25cb confirms and \u00d7 goes back, as on the Vita",
                    true, [&s](int) { s.padSwap = !s.padSwap; }});
    return rows;
}

void Viewer::settingChange(int row, int dir, bool click) {
    std::vector<SettingRow> rows = settingRows();
    if (row < 0 || row >= int(rows.size()) || !rows[size_t(row)].enabled || !rows[size_t(row)].change) return;
    rows[size_t(row)].change(dir);
    applySettings();
    saveSettings();
    const std::vector<SettingRow> now = settingRows();
    SDL_Log("settings: %s %s", now[size_t(row)].label.c_str(), now[size_t(row)].value.c_str());
    if (click) audio.playSe(0xb4, false);
}

// (over the options, which stay open beneath: what waits for them closing waits on)
void Viewer::openSettings() { settingsOpen = true, settingsStart = uiClock, settingsCursor = 0; }

void Viewer::closeSettings() { settingsOpen = false, optionsStart = uiClock; }

void Viewer::drawSettings(const Mat4& view, int w, int h) {
    tapRects.clear();
    const std::vector<SettingRow> rows = settingRows();
    std::vector<ListRow> list;
    for (size_t k = 0; k < rows.size(); ++k) {
        ListRow r;
        r.id = 6000 + int(k);
        r.name = rows[k].value.empty() ? rows[k].label : rows[k].label + ":  " + rows[k].value;
        r.number = -1;
        r.selectable = rows[k].enabled;
        r.help = rows[k].help;
        list.push_back(r);
    }
    listRows = list;
    drawSkillList(list, 6000 + std::clamp(settingsCursor, 0, int(rows.size()) - 1), uiClock - settingsStart, view, w, h, -1, -1);
}

bool Viewer::settingsKey(SDL_Keycode k) {
    if (!settingsOpen) return false;
    const int n = int(settingRows().size());
    if (k == SDLK_UP) settingsCursor = (settingsCursor + n - 1) % n;
    else if (k == SDLK_DOWN) settingsCursor = (settingsCursor + 1) % n;
    else if (k == SDLK_LEFT) settingChange(settingsCursor, -1);
    else if (k == SDLK_RIGHT || k == SDLK_RETURN || k == SDLK_KP_ENTER) settingChange(settingsCursor, 1);
    else if (k == SDLK_BACKSPACE) closeSettings();
    return true;
}

bool Viewer::settingsTapped(int id) { // (a tap's TapRect id)
    if (!settingsOpen) return false;
    if (id == 100) closeSettings();
    else if (id >= 6000) settingChange(id - 6000, 1);
    else if (id >= 1000) settingsCursor = id - 1000;
    return true;
}

void Viewer::resetGame() { // (Cr3UtilResetWork: a new game's state, the database kept - logged in, as Auth offline)
    game = cr3::GameState();
    game.cr3Db = offlineDb.loaded() ? &offlineDb : nullptr;
    game.onSync = [this] { cr3SavePending = cr3Playing; };
    if (game.cr3Db) offlineDb.login(game, std::time(nullptr));
}

bool Viewer::decode(const std::string& name, cr3::Image& img) {
    if (auto it = modelImages.find(name); it != modelImages.end()) return img = it->second, true; // (an HD model's own)
    if (cardArtwork.decodeTexture(cr3::CardArtwork::textureSeries(name), img)) return true;
    std::vector<uint8_t> data;
    std::string err;
    if (englishTextures && archive.read("images_eng\\" + name + ".img", data) && cr3::decodeImg(data, img)) return true;
    if (archive.read("images\\" + name + ".img", data, &err) && cr3::decodeImg(data, img, &err)) return true;
    SDL_Log("texture %s: %s", name.c_str(), err.c_str());
    return false;
}

// A material's texture (`mipmaps` off for lookup tables: CLUTs)
cr3::Texture* Viewer::texture(const std::string& name, bool mipmaps) {
    if (name.empty()) return nullptr;
    if (auto it = textures.find(name); it != textures.end()) return it->second.id ? &it->second : nullptr;
    cr3::Image img;
    cr3::Texture& t = textures[name];
    if (!decode(name, img)) return nullptr;
    if (hdTextures && !hdDir.empty()) { // (drawn at the original's size, sharper)
        cr3::Image hd;
        if (loadHd(img, hd)) {
            hd.logicalW = img.width, hd.logicalH = img.height;
            img = std::move(hd);
            ++hdLoaded;
        } else {
            ++hdMissing;
        }
    }
    img.mipmaps = mipmaps;
    textureMips[name] = mipmaps;
    t = dev->upload(img);
    return t.id ? &t : nullptr;
}

// The scene's lights from a light figure (a map's <name>c): false when it has none
bool Viewer::loadLights(const std::string& figure) {
    cr3::SceneLighting l;
    if (!figureLights(figure, l)) return false;
    lighting = l;
    return true;
}

bool Viewer::figureLights(const std::string& figure, cr3::SceneLighting& l) {
    std::vector<uint8_t> data;
    cr3::Geom g;
    cr3::Skeleton sk;
    if (!archive.read(figure + ".geom", data) || !cr3::parseGeom(data, g) || !archive.read(figure + ".skel", data) ||
        !cr3::parseSkeleton(data, sk))
        return false;
    if (SDL_getenv("CR3_MATERIALS"))
        for (const cr3::GeomLight& gl : g.lights)
            SDL_Log("%s light: type %d index %d v %.3f %.3f %.3f %.3f %.3f %.3f", figure.c_str(), gl.type, gl.index, gl.v[0], gl.v[1], gl.v[2], gl.v[3],
                    gl.v[4], gl.v[5]);
    return cr3::lightsFromFigure(g, sk, l);
}

bool Viewer::jointWorld(const ViewFigure& f, const std::string& joint, cr3::Mat34& out) {
    if (!f.skinned || f.name.size() < 6) return false;
    const std::string base = f.name.substr(0, f.name.size() - 5);
    auto it = jointNames.find(base);
    if (it == jointNames.end()) {
        std::vector<uint8_t> d;
        it = jointNames.emplace(base, archive.read(base + ".name", d) ? cr3::parseJointNames(d) : std::vector<std::string>()).first;
    }
    const auto at = std::find(it->second.begin(), it->second.end(), joint);
    const std::vector<cr3::JointTransform>& p = f.posed.empty() ? f.skeleton.basePose : f.posed;
    const std::vector<cr3::Mat34> w = f.skeleton.world(p, f.place);
    const size_t j = size_t(at - it->second.begin());
    if (at == it->second.end() || j >= w.size()) return false;
    out = w[j];
    return true;
}

// an attachment (CrxModel::LoadAttachment: its own figure and animation, looped) on a posed
// figure's joint
void Viewer::drawAttached(ViewFigure& a, const ViewFigure& on, const std::string& joint, float t, const float* mvp) {
    if (a.meshes.empty() || on.meshes.empty() || !jointWorld(on, joint, a.place)) return;
    for (int k = 0; k < 4; ++k) a.tint[k] = on.tint[k];
    pose(a, t, true);
    drawFigure(a, mvp);
}

void Viewer::unload(ViewFigure& f) {
    for (ViewMesh& m : f.meshes) dev->destroy(m.gpu);
    f.meshes.clear();
}

std::string Viewer::variantOf(const std::string& name) const {
    if (figureVariant.empty() || figureVariant == "a" || name.empty()) return name;
    std::string v = name;
    for (size_t at = v.find('a'); at != std::string::npos; at = v.find('a', at + figureVariant.size()))
        v.replace(at, 1, figureVariant);
    return archive.contains("images\\" + v + ".img") ? v : name;
}

// the name a figure's motions go by: a hero's costume body's the b01 body's (j001b02 -> j001b01)
std::string Viewer::motionBase(const std::string& base) {
    return base.size() >= 7 && base[0] == 'j' && base[4] == 'b' ? base.substr(0, 4) + "b01" : base; // (an add-on's j001b101 too)
}

// An HD model's pictures as its figure loads: figures being drawn hold these textures by their
// address, so the entry stays - as it is for the same picture, its texture replaced for a new one
void Viewer::takeModelPictures(std::vector<std::pair<std::string, cr3::Image>>& pictures) {
    for (auto& [k, img] : pictures) {
        const auto known = modelImages.find(k);
        if (known != modelImages.end() && known->second.width == img.width && known->second.height == img.height && known->second.rgba == img.rgba)
            continue;
        modelImages[k] = std::move(img);
        const auto t = textures.find(k);
        if (t == textures.end()) continue;
        if (t->second.id) reloadTexture(k, t->second);
        else textures.erase(t); // (none could be made before: tried again when asked for)
    }
}

// HD Models: models/hd/<figure>.glb (or .gltf) in place of the figure's meshes - its skeleton,
// animations and materials kept (assets/gltf_figure.hpp)
void Viewer::replaceFigure(const std::string& name, cr3::Geom& g) {
    if (!hdModels || modelsDir.empty() || name.size() < 6) return;
    const std::string base = name.substr(0, name.size() - 5);
    for (const char* ext : {".glb", ".gltf"}) {
        const std::string path = modelsDir + base + ext;
        if (!SDL_GetPathInfo(path.c_str(), nullptr)) continue;
        std::vector<uint8_t> d;
        cr3::Skeleton sk;
        if (archive.read(base + ".skel", d)) cr3::parseSkeleton(d, sk);
        std::vector<std::pair<std::string, cr3::Image>> pictures;
        std::string why;
        cr3::GltfStats st;
        if (cr3::applyFigureReplacement(g, sk, path, "gltf\\" + base + "\\", pictures, &why, &st)) {
            takeModelPictures(pictures);
            ++hdModelsLoaded;
            SDL_Log("%s: replaced by %s (%d meshes, %d triangles%s)", name.c_str(), path.c_str(), st.meshes, st.triangles,
                    pictures.empty() ? "" : ", pictures of its own");
        } else {
            SDL_Log("%s: %s left out: %s", name.c_str(), path.c_str(), why.c_str());
        }
        return;
    }
}

// an add-on's figure (assets/addons.hpp: its base's files under its name): the add-on's meshes
// on it - or, for an outfit whose wearer HD Models replaces (not made to fit), its base as that
// is drawn: the like item's look, its own numbers kept, as the game's armour
bool Viewer::addonFigure(const std::string& name, cr3::Geom& g) {
    if (name.size() < 6) return false;
    const cr3::Cr3Addon* a = addons.byFigure(name.substr(0, name.size() - 5));
    if (!a) return false;
    if (!addonOwnModel(*a)) {
        replaceFigure(a->base + ".geom", g);
        return true;
    }
    std::vector<uint8_t> d;
    cr3::Skeleton sk;
    if (archive.read(a->figure + ".skel", d)) cr3::parseSkeleton(d, sk);
    std::vector<std::pair<std::string, cr3::Image>> pictures;
    std::string why;
    cr3::GltfStats st;
    if (cr3::applyFigureReplacement(g, sk, a->model, "addon\\" + a->figure + "\\", pictures, &why, &st)) {
        takeModelPictures(pictures);
        SDL_Log("%s: add-on %s (%d meshes, %d triangles)", name.c_str(), a->model.c_str(), st.meshes, st.triangles);
    } else {
        SDL_Log("%s: add-on %s left out: %s", name.c_str(), a->model.c_str(), why.c_str());
    }
    return true;
}

bool Viewer::loadFigure(const std::string& name, ViewFigure& f, std::string* err) {
    unload(f);
    const size_t keepAnim = f.animIndex;
    const cr3::Mat34 keepPlace = f.place;
    f = ViewFigure();
    f.name = name;
    f.animIndex = keepAnim;
    f.place = keepPlace;
    std::vector<uint8_t> data;
    cr3::Geom g;
    if (!archive.read(name, data, err) || !cr3::parseGeom(data, g, err)) return false;
    if (!addonFigure(name, g)) replaceFigure(name, g);
    static const float kWhite[4] = {1, 1, 1, 1}, kZero[3] = {0, 0, 0};
    cr3::SmoothStats smoothed;
    for (const cr3::GeomMesh& raw : g.meshes) {
        ViewMesh vm;
        vm.source = raw;
        if (raw.material < g.materials.size()) { // (the joints as rows: the iPad's slots, a joint in the position's w)
            const std::array<uint32_t, 4>& key = g.materials[raw.material].shaderKey;
            const bool builtIn = !key[0] && !key[1] && !key[2] && !key[3]; // (no shader of its own: its file's form says)
            vm.source.resolveJoints(builtIn ? (g.version == 0x3c ? 3.0f : 1.0f) : shaders->jointScale(key));
            if (!builtIn) vm.placing = shaders->features(key).vertex;
        }
        // Smooth Models: every figure in the world - not the menus' (uiFig) nor a billboard
        if (settings.smoothModels >= 2 && !loadingUi && vm.placing == 0) cr3::smoothMesh(vm.source, settings.smoothModels, 60.0f, &smoothed);
        const cr3::GeomMesh& m = vm.source;
        vm.vertices.resize(size_t(m.vertexCount) * cr3::kVertexStride);
        for (uint32_t i = 0; i < m.vertexCount; ++i) {
            static const float kNoUv[2] = {0, 0};
            auto at3 = [&](const std::vector<float>& v) { return v.empty() ? kZero : &v[size_t(i) * 3]; };
            packVertex(&vm.vertices[size_t(i) * cr3::kVertexStride], &m.position[size_t(i) * 3], at3(m.normal),
                       m.uv.empty() ? kNoUv : &m.uv[size_t(i) * 2], m.colour.empty() ? kWhite : &m.colour[size_t(i) * 4],
                       at3(m.tangent), at3(m.binormal), m.uv1.empty() ? kNoUv : &m.uv1[size_t(i) * 2]);
        }
        const std::vector<uint32_t> idx = m.triangles();
        vm.gpu = dev->upload(vm.vertices, idx, -1);
        vm.lit = !m.normal.empty();
        if (m.material < g.materials.size()) {
            vm.material = vm.baseMaterial = g.materials[m.material];
            if (SDL_getenv("CR3_MATERIALS")) { // (debugging: the materials' keys and states)
                const cr3::GeomMaterial& gm = g.materials[m.material];
                std::string st;
                for (const auto& [id, v] : gm.states) st += " " + std::to_string(id) + "=" + std::to_string(v[0]) + "/" + std::to_string(v[1]);
                SDL_Log("%s mesh %zu: material %u key %08x %08x %08x %08x states%s textures %s / %s / %s", name.c_str(), f.meshes.size(),
                        m.material, gm.shaderKey[0], gm.shaderKey[1], gm.shaderKey[2], gm.shaderKey[3], st.c_str(),
                        vm.material.diffuse().c_str(), vm.material.texture(0x43).c_str(), vm.material.texture(0x48).c_str());
            }
            vm.texture = texture(variantOf(vm.material.diffuse()));
            vm.normalMap = texture(variantOf(vm.material.texture(0x35)));
            vm.extraMap = vm.material.textures.count(0x48)   ? texture(variantOf(vm.material.texture(0x48)), false)
                          : vm.material.textures.count(0x44) ? texture(variantOf(vm.material.texture(0x44))) // (OverlayColorSampler)
                                                             : texture(variantOf(vm.material.texture(0x43)));
            // an overlay without its texture (d0252b's): the sampler never set, so unit 0's -
            // the colour's own, laid over itself
            if (!vm.material.textures.count(0x44) && (shaders->features(vm.material.shaderKey).shade & SHADE_OVERLAY))
                vm.extraMap = vm.texture;
        }
        if (SDL_getenv("CR3_MATERIALS")) { // (debugging: each mesh's material as it loads)
            std::string tx, ps, st;
            for (const auto& [k, v] : vm.material.textures) tx += std::to_string(k) + ":" + v + " ";
            for (const auto& [k, v] : vm.material.params) {
                ps += std::to_string(k) + "=";
                for (float x : v) ps += std::to_string(x).substr(0, 6) + ",";
                ps += " ";
            }
            for (const auto& [k, v] : vm.material.states) st += std::to_string(k) + "=" + std::to_string(v[0]) + "/" + std::to_string(v[1]) + " ";
            int top = 0;
            for (uint8_t j : vm.source.joints) top = std::max(top, int(j));
            SDL_Log("%s mesh %zu: joints up to %d for a palette of %zu", name.c_str(), f.meshes.size(), top, vm.source.palette.size());
            {
                float lo = 9, hi = -9;
                for (float c : vm.source.colour) lo = std::min(lo, c), hi = std::max(hi, c);
                SDL_Log("%s mesh %zu: %zu vertex colours %.3f .. %.3f", name.c_str(), f.meshes.size(), vm.source.colour.size() / 4, lo, hi);
                float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
                for (size_t k = 0; k + 1 < vm.source.uv.size(); k += 2)
                    u0 = std::min(u0, vm.source.uv[k]), u1 = std::max(u1, vm.source.uv[k]), v0 = std::min(v0, vm.source.uv[k + 1]), v1 = std::max(v1, vm.source.uv[k + 1]);
                SDL_Log("%s mesh %zu: uv %.3f .. %.3f, %.3f .. %.3f", name.c_str(), f.meshes.size(), u0, u1, v0, v1);
                float lo3[3] = {1e9f, 1e9f, 1e9f}, hi3[3] = {-1e9f, -1e9f, -1e9f};
                for (size_t k = 0; k + 2 < vm.source.position.size(); k += 3)
                    for (int a = 0; a < 3; ++a) lo3[a] = std::min(lo3[a], vm.source.position[k + a]), hi3[a] = std::max(hi3[a], vm.source.position[k + a]);
                SDL_Log("%s mesh %zu: positions (%.1f %.1f %.1f) .. (%.1f %.1f %.1f), %zu w, palette %s", name.c_str(), f.meshes.size(), lo3[0], lo3[1], lo3[2], hi3[0], hi3[1], hi3[2], vm.source.positionW.size(),
                        vm.source.palette.empty() ? "-" : std::to_string(vm.source.palette[0]).c_str());
            }
            SDL_Log("%s mesh %zu: key %08x_%08x_%08x_%08x textures [%s] params {%s} states {%s} lit %d", name.c_str(), f.meshes.size(),
                    vm.material.shaderKey[0], vm.material.shaderKey[1], vm.material.shaderKey[2], vm.material.shaderKey[3], tx.c_str(),
                    ps.c_str(), st.c_str(), int(vm.lit));
        }
        f.vertices += m.vertexCount;
        f.triangles += idx.size() / 3;
        f.meshes.push_back(std::move(vm));
    }
    if (smoothed.meshes && SDL_getenv("CR3_SMOOTH_LOG"))
        SDL_Log("%s smoothed: %d meshes, %d -> %d triangles", name.c_str(), smoothed.meshes, smoothed.trianglesBefore, smoothed.trianglesAfter);
    float r2 = 0;
    for (int c = 0; c < 3; ++c) {
        f.centre[c] = (g.boundsMin[c] + g.boundsMax[c]) / 2;
        const float h = (g.boundsMax[c] - g.boundsMin[c]) / 2;
        r2 += h * h;
    }
    f.radius = std::max(std::sqrt(r2), 1e-3f);
    // the skeleton and the figure's animations (<figure>*.anim: c217.anim, c217e080.anim, ...)
    const std::string base = name.substr(0, name.size() - 5);
    f.skinned = archive.read(base + ".skel", data) && cr3::parseSkeleton(data, f.skeleton) &&
                g.inverseBind.size() == f.skeleton.basePose.size();
    f.inverseBind = g.inverseBind;
    // (a hero's costume - j001b02 - moves by the b01 body's motions: the same skeleton, only
    // j001b01_* animations in the archives)
    const std::string shared = motionBase(base);
    if (f.skinned)
        for (const auto& e : archive.entries())
            if (e.name.size() > 5 && e.name.compare(e.name.size() - 5, 5, ".anim") == 0 &&
                (e.name.compare(0, base.size(), base) == 0 || (shared != base && e.name.compare(0, shared.size() + 1, shared + "_") == 0)))
                f.anims.push_back(e.name);
    std::sort(f.anims.begin(), f.anims.end());
    if (!f.anims.empty()) loadAnim(f, std::min(f.animIndex, f.anims.size() - 1));
    return true;
}

// the model viewer's figure: the archive's model `index`
bool Viewer::load(size_t index) {
    current = index;
    dev->resetHistory();
    const std::string& name = models[index];
    std::string err;
    if (!loadFigure(name, fig, &err)) {
        status = name + ": " + err;
        SDL_Log("%s", status.c_str());
        return false;
    }
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s (%zu/%zu): %zu meshes, %zu vertices, %zu triangles", name.c_str(), index + 1,
                  models.size(), fig.meshes.size(), fig.vertices, fig.triangles);
    status = buf;
    SDL_Log("%s", status.c_str());
    return true;
}

void Viewer::loadAnim(ViewFigure& f, size_t index) {
    f.animIndex = index;
    std::vector<uint8_t> data;
    std::string err;
    // (a costume body - j001b02, the b01 skeleton and a DirLamp01 after it - takes the b01
    // motions over its first joints)
    const std::string base = f.name.size() > 5 ? f.name.substr(0, f.name.size() - 5) : f.name;
    const bool costume = motionBase(base) != base;
    f.animLoaded = archive.read(f.anims[index], data, &err) && f.anim.parse(data, &err) &&
                   (f.anim.joints() == int(f.skeleton.basePose.size()) ||
                    (costume && f.anim.joints() > 0 && f.anim.joints() < int(f.skeleton.basePose.size())));
    if (!f.animLoaded) SDL_Log("%s: %s", f.anims[index].c_str(), err.empty() ? "not this skeleton's" : err.c_str());
    time = 0;
}

void Viewer::drawShadow(const cr3::Mat34& place, const float* mvp) {
    if (!shadowTried) {
        shadowTried = true;
        std::string err;
        if (!loadFigure("obj001.geom", shadowFig, &err)) SDL_Log("shadow: %s", err.c_str());
        shadowFig.absolute = true;
        const auto it = std::find(shadowFig.anims.begin(), shadowFig.anims.end(), "obj001.anim");
        if (it != shadowFig.anims.end()) {
            const float keep = time;
            loadAnim(shadowFig, size_t(it - shadowFig.anims.begin()));
            time = keep;
        }
    }
    if (shadowFig.meshes.empty()) return;
    shadowFig.place = place;
    const float keep = time;
    pose(shadowFig, shadowFig.animLoaded && shadowFig.anim.duration() > 0 ? std::fmod(time, shadowFig.anim.duration()) : 0.0f, false); // (its animation: a single frame)
    time = keep;
    drawFigure(shadowFig, mvp);
}

void Viewer::playMotion(ViewFigure& f, const char* motion) {
    const std::string want = f.name.substr(0, f.name.size() - 5) + motion + ".anim";
    const auto it = std::find(f.anims.begin(), f.anims.end(), want);
    if (it != f.anims.end() && (!f.animLoaded || f.anims[f.animIndex] != want)) loadAnim(f, size_t(it - f.anims.begin()));
}

std::string Viewer::animStatus() const {
    if (!fig.skinned) return "";
    if (!fig.animLoaded) return " - base pose";
    char buf[160];
    std::snprintf(buf, sizeof buf, " - %s (%zu/%zu) %.2f / %.2f s", fig.anims[fig.animIndex].c_str(), fig.animIndex + 1,
                  fig.anims.size(), time, fig.anim.duration());
    return buf;
}

// A figure's pose at time t, the skinning matrices (place * world * inverse bind), the meshes'
// vertices through their palettes (as the game's vertex shaders: MatrixPalette[index / 3 slot],
// the weights as stored)
void Viewer::pose(ViewFigure& f, float t, bool loop) {
    if (!f.skinned) return;
    std::vector<cr3::JointTransform> p = f.skeleton.basePose;
    if (f.animLoaded) {
        const float frames = float(std::max(f.anim.frames() - 1, 1));
        float fr = t * f.anim.sampleRate();
        if (fr > frames) fr = loop ? std::fmod(fr, frames) : frames;
        // (relative to the base pose); the user channels: material parameters (UI fades, colours)
        std::vector<float> user(f.skeleton.userChannels.size(), std::nanf(""));
        p = f.absolute ? f.anim.poseAbsolute(f.skeleton.basePose, fr, f.anim.hasUserChannels() ? &user : nullptr)
                       : f.anim.poseOver(f.skeleton.basePose, fr, f.anim.hasUserChannels() ? &user : nullptr);
        if (f.blendWeight < 1 && f.blendLoaded) { // (fading in from the motion before)
            const float bframes = float(std::max(f.blendAnim.frames() - 1, 1));
            float bfr = f.blendTime * f.blendAnim.sampleRate();
            if (bfr > bframes) bfr = f.blendLoop ? std::fmod(bfr, bframes) : bframes;
            const std::vector<cr3::JointTransform> from = f.blendAbsolute ? f.blendAnim.poseAbsolute(f.skeleton.basePose, bfr)
                                                                          : f.blendAnim.poseOver(f.skeleton.basePose, bfr);
            const float w = std::clamp(f.blendWeight, 0.0f, 1.0f);
            for (size_t j = 0; j < p.size() && j < from.size(); ++j) {
                cr3::JointTransform& to = p[j];
                const cr3::JointTransform& a = from[j];
                float dot = 0;
                for (int c = 0; c < 4; ++c) dot += a.rotation[c] * to.rotation[c];
                const float sgn = dot < 0 ? -1.0f : 1.0f;
                float len = 0;
                for (int c = 0; c < 4; ++c) to.rotation[c] = a.rotation[c] * (1 - w) + to.rotation[c] * sgn * w, len += to.rotation[c] * to.rotation[c];
                len = len > 0 ? 1.0f / std::sqrt(len) : 1.0f;
                for (int c = 0; c < 4; ++c) to.rotation[c] *= len;
                for (int c = 0; c < 3; ++c) {
                    to.translation[c] = a.translation[c] + (to.translation[c] - a.translation[c]) * w;
                    to.scale[c] = a.scale[c] + (to.scale[c] - a.scale[c]) * w;
                }
            }
        }
        for (ViewMesh& vm : f.meshes) {
            vm.material = vm.baseMaterial;
            for (size_t k = 0; k < user.size() && k < f.skeleton.userChannels.size(); ++k) {
                const cr3::Skeleton::UserChannel& c = f.skeleton.userChannels[k];
                if (std::isnan(user[k]) || c.material != vm.material.hash) continue;
                std::vector<float>& v = vm.material.params[c.param];
                const bool fresh = v.size() <= c.component;
                if (fresh) v.resize(size_t(c.component) + 1, c.param == 0x33 ? 1.0f : 0.0f); // (DiffuseColor's default white)
                // the figure's own animation sets the value; an animator beside it adds to the
                // material's (brkInfo's colour -0.5 .. +0.43 .. 0 about its 0.99: a flash, then as
                // it is; bonus_crtcl's fade -1 .. 0 about 1)
                v[c.component] = f.absolute ? user[k] : v[c.component] + user[k];
            }
        }
    }
    std::vector<cr3::Mat34> world = f.skeleton.world(p, f.place);
    for (const auto& [j, m] : f.jointWorld)
        if (j < world.size()) world[j] = m;
    f.posed = p;
    std::vector<cr3::Mat34> skin(world.size());
    for (size_t j = 0; j < world.size(); ++j) skin[j] = world[j] * f.inverseBind[j];
    for (ViewMesh& vm : f.meshes) {
        const cr3::GeomMesh& m = vm.source;
        std::vector<const cr3::Mat34*> pal;
        for (uint32_t j : m.palette) pal.push_back(j < skin.size() ? &skin[j] : nullptr);
        if (vm.placing & cr3::kBillboard) {
            billboard(vm, pal.empty() || !pal[0] ? f.place : *pal[0]);
            continue;
        }
        for (uint32_t i = 0; i < m.vertexCount; ++i) {
            const float* sp = &m.position[size_t(i) * 3];
            const float* sn = m.normal.empty() ? nullptr : &m.normal[size_t(i) * 3];
            const float* st = m.tangent.empty() ? nullptr : &m.tangent[size_t(i) * 3];
            const float* sb = m.binormal.empty() ? nullptr : &m.binormal[size_t(i) * 3];
            float op[3] = {0, 0, 0}, on[3] = {0, 0, 0}, ot[3] = {0, 0, 0}, ob[3] = {0, 0, 0};
            auto add = [&](const cr3::Mat34& M, float w) {
                auto rot = [&](const float* v, float* o, int r) {
                    o[r] += w * (M.m[r * 4] * v[0] + M.m[r * 4 + 1] * v[1] + M.m[r * 4 + 2] * v[2]);
                };
                for (int r = 0; r < 3; ++r) {
                    op[r] += w * (M.m[r * 4] * sp[0] + M.m[r * 4 + 1] * sp[1] + M.m[r * 4 + 2] * sp[2] + M.m[r * 4 + 3]);
                    if (sn) rot(sn, on, r);
                    if (st) rot(st, ot, r);
                    if (sb) rot(sb, ob, r);
                }
            };
            if (m.joints.empty()) {
                add(pal.empty() || !pal[0] ? f.place : *pal[0], 1.0f);
            } else {
                for (int k = 0; k < 4; ++k) {
                    const float w = m.weights.empty() ? (k == 0 ? 1.0f : 0.0f) : m.weights[size_t(i) * 4 + k];
                    const size_t slot = m.joints[size_t(i) * 4 + k] / 3;
                    if (w != 0.0f && slot < pal.size() && pal[slot]) add(*pal[slot], w);
                }
            }
            // (normalised, as the game's vertex shaders do)
            auto unit = [](float* v) {
                const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                if (l > 0) v[0] /= l, v[1] /= l, v[2] /= l;
            };
            unit(on);
            unit(ot);
            unit(ob);
            uint8_t* o = &vm.vertices[size_t(i) * cr3::kVertexStride];
            std::memcpy(o, op, 12);
            if (sn) std::memcpy(o + 24, on, 12);
            if (st) std::memcpy(o + 36, ot, 12);
            if (sb) std::memcpy(o + 48, ob, 12);
        }
        dev->updateVertices(vm.gpu, vm.vertices);
    }
}

// the model viewer's camera: where it is (orbiting the figure by yaw / pitch / zoom), how far
float Viewer::modelEye(float* eye) const {
    const float yr = yaw * 3.14159265f / 180, pr = pitch * 3.14159265f / 180, d = fig.radius * 2.6f * zoom;
    eye[0] = fig.centre[0] + d * std::cos(pr) * std::sin(yr);
    eye[1] = fig.centre[1] + d * std::sin(pr);
    eye[2] = fig.centre[2] + d * std::cos(pr) * std::cos(yr);
    return d;
}

// A billboard mesh's vertices (the game's vertex shaders with ViewInverse): turned as the
// camera is - kUpright: by camPos, the view's third row flattened (y 0), then y x it and the
// two's cross - then scaled by MatrixPalette[0]'s columns' lengths and moved to its translation
// (its rotation and the vertices' joints left out)
void Viewer::billboard(ViewMesh& vm, const cr3::Mat34& p0) {
    const cr3::GeomMesh& m = vm.source;
    float r[3][3]; // (the rows: vertex v -> r v)
    for (int i = 0; i < 3; ++i)
        for (int c = 0; c < 3; ++c) r[i][c] = poseView.m[i * 4 + c];
    auto unit = [](float* v) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 0) v[0] /= l, v[1] /= l, v[2] /= l;
    };
    for (int c = 0; c < 3; ++c) { // (the camera's axes as unit columns)
        float col[3] = {r[0][c], r[1][c], r[2][c]};
        unit(col);
        for (int i = 0; i < 3; ++i) r[i][c] = col[i];
    }
    if (vm.placing & cr3::kUpright) {
        float z[3] = {r[2][0], 0, r[2][2]};
        unit(z);
        float x[3] = {z[2], 0, -z[0]}; // (0 1 0) x z
        unit(x);
        const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
        for (int c = 0; c < 3; ++c) r[0][c] = x[c], r[1][c] = y[c], r[2][c] = z[c];
    }
    float scale[3], trans[3];
    for (int c = 0; c < 3; ++c) {
        scale[c] = std::sqrt(p0.m[c] * p0.m[c] + p0.m[4 + c] * p0.m[4 + c] + p0.m[8 + c] * p0.m[8 + c]);
        trans[c] = p0.m[c * 4 + 3];
    }
    auto turn = [&](const std::vector<float>& from, uint32_t i, uint8_t* to, bool place) {
        if (from.empty()) return;
        const float* v = &from[size_t(i) * 3];
        float o[3];
        for (int k = 0; k < 3; ++k) o[k] = r[k][0] * v[0] + r[k][1] * v[1] + r[k][2] * v[2];
        if (place)
            for (int k = 0; k < 3; ++k) o[k] = o[k] * scale[k] + trans[k];
        std::memcpy(to, o, 12);
    };
    for (uint32_t i = 0; i < m.vertexCount; ++i) {
        uint8_t* o = &vm.vertices[size_t(i) * cr3::kVertexStride];
        turn(m.position, i, o, true);
        turn(m.normal, i, o + 24, false);
        turn(m.tangent, i, o + 36, false);
        turn(m.binormal, i, o + 48, false);
    }
    dev->updateVertices(vm.gpu, vm.vertices);
}

void Viewer::pose() {
    { // (this frame's camera, for the billboards)
        cr3::Mat34 cw;
        float fov, zn, zf;
        if (battleMode) {
            if (battleView(cw, fov, zn, zf)) poseView = cw;
        } else if (eventMode) {
            if (event->sceneShown() && event->scene().camera(cw, fov, zn, zf, eventLead())) poseView = cw;
        } else if (fieldMode) {
            poseView = field.cameraPlace();
        } else { // (lookAt's inverse)
            float eye[3];
            modelEye(eye);
            const Mat4 v = cr3::mat4RigidInverse(lookAt(eye, fig.centre));
            for (int row = 0; row < 3; ++row)
                for (int c = 0; c < 4; ++c) poseView.m[row * 4 + c] = v[size_t(c * 4 + row)];
        }
    }
    if (battleMode) {
        poseBattle();
        return;
    }
    if (eventMode) {
        poseEvent();
        return;
    }
    if (fieldMode) {
        // the character stands at `player`, facing `heading` (radians about y, 0 = +z)
        const float c = std::cos(heading), s = std::sin(heading);
        fig.place = cr3::Mat34();
        fig.place.m[0] = c, fig.place.m[2] = s, fig.place.m[8] = -s, fig.place.m[10] = c;
        fig.place.m[3] = player[0], fig.place.m[7] = player[1], fig.place.m[11] = player[2];
        pose(mapFigure, time);
        for (size_t k = 0; k < gimmicks.size(); ++k) { // (a box opening: fa01 once, then fn02 - FldBox::Open)
            ViewFigure& g = gimmicks[k];
            const auto op = k < shownGimmicks.size() ? boxOpening.find(shownGimmicks[k]) : boxOpening.end();
            if (op != boxOpening.end()) {
                playMotion(g, "fa01");
                const float t = uiClock - op->second;
                if (g.animLoaded && t < g.anim.duration()) {
                    pose(g, t, false);
                    continue;
                }
                boxOpening.erase(op);
                playMotion(g, "fn02");
            }
            // the script's: its event motion (SetMotion: from when it was set, once unless
            // looping), its alpha (ChangeAlpha)
            const auto st = k < gimmickIds.size() ? script->gimmicks().find(gimmickIds[k]) : script->gimmicks().end();
            if (st != script->gimmicks().end()) {
                g.tint[3] = st->second.alpha();
                if (st->second.color != gimmickColor[k]) { // (ChangeModelColor: its variant's textures)
                    gimmickColor[k] = st->second.color;
                    const std::string id = gimmickIds[k];
                    const std::string tex = st->second.color < 0 ? std::string() : "m" + id.substr(1, 3) + "abcd"[st->second.color] + "01";
                    cr3::Texture* t = tex.empty() ? nullptr : texture(tex);
                    cr3::Texture* n = tex.empty() ? nullptr : texture(tex + "n");
                    if (!tex.empty() && !t) SDL_Log("%s: no %s", id.c_str(), tex.c_str());
                    for (ViewMesh& vm : g.meshes) {
                        if (t) vm.texture = t;
                        if (n) vm.normalMap = n;
                    }
                }
                if (st->second.faceUpdates > 0) { // (ChangeDirection: turned about y toward its target, in place)
                    if (gimmickFace[k].serial != st->second.faceSerial) {
                        float tx, tz;
                        gimmickFace[k].serial = st->second.faceSerial;
                        gimmickFace[k].from = gimmickFace[k].yaw;
                        if (gimmickTarget(st->second.faceTarget, tx, tz)) gimmickFace[k].to = std::atan2(tx - g.place.m[3], tz - g.place.m[11]);
                    }
                    const float a = std::clamp((float(script->ticks() - st->second.faceTick) + fieldClock / cr3::EventRuntime::kTick) / float(st->second.faceUpdates), 0.0f, 1.0f);
                    gimmickFace[k].yaw = turnBetween(gimmickFace[k].from, gimmickFace[k].to, a);
                    const float sc = std::hypot(g.place.m[0], g.place.m[4], g.place.m[8]), c = std::cos(gimmickFace[k].yaw), sn = std::sin(gimmickFace[k].yaw);
                    g.place.m[0] = c * sc, g.place.m[1] = 0, g.place.m[2] = sn * sc;
                    g.place.m[4] = 0, g.place.m[5] = sc, g.place.m[6] = 0;
                    g.place.m[8] = -sn * sc, g.place.m[9] = 0, g.place.m[10] = c * sc;
                }
                if (!st->second.motion.empty()) {
                    if (gimmickSerial[k] != st->second.motionSerial) {
                        gimmickSerial[k] = st->second.motionSerial;
                        const auto a = std::find(g.anims.begin(), g.anims.end(), st->second.motion + ".anim");
                        if (a != g.anims.end()) {
                            const float keep = time;
                            loadAnim(g, size_t(a - g.anims.begin()));
                            time = keep;
                        }
                    }
                    pose(g, float(script->ticks() - st->second.motionTick) * cr3::EventRuntime::kTick + fieldClock, st->second.motionLoop);
                    continue;
                }
            }
            pose(g, time);
        }
        if (script) fig.tint[3] = script->playerAlpha();
        if (script && script->playerState().faceUpdates > 0) { // (the leader turned: Cr3FieldMapCharacter.ChangeDirection)
            const cr3::GimmickState& ps = script->playerState();
            if (leaderFace.serial != ps.faceSerial) {
                float tx, tz;
                leaderFace.serial = ps.faceSerial, leaderFace.from = heading, leaderFace.done = false;
                leaderFace.to = gimmickTarget(ps.faceTarget, tx, tz) ? std::atan2(tx - player[0], tz - player[2]) : heading;
            }
            const float a = (float(script->ticks() - ps.faceTick) + fieldClock / cr3::EventRuntime::kTick) / float(ps.faceUpdates);
            if (!leaderFace.done) heading = turnBetween(leaderFace.from, leaderFace.to, std::clamp(a, 0.0f, 1.0f)), leaderFace.done = a >= 1;
        }
        if (script && script->events().scene().loaded()) poseEventModels(script->events());
    }
    pose(fig, time);
}

// A figure's meshes with their materials' render states; opaque meshes first, then those blending
void Viewer::drawFigure(const ViewFigure& f, const float* mvp) {
    if (f.tint[3] <= 0) return;
    const float* q = f.place.m;
    const bool mirrored = q[0] * (q[5] * q[10] - q[6] * q[9]) - q[1] * (q[4] * q[10] - q[6] * q[8]) + q[2] * (q[4] * q[9] - q[5] * q[8]) < 0;
    std::vector<const ViewMesh*> order;
    for (const ViewMesh& m : f.meshes) order.push_back(&m);
    std::stable_sort(order.begin(), order.end(), [](const ViewMesh* a, const ViewMesh* b) {
        uint32_t ba = 0, bb = 0;
        a->material.state(0xa4, 0, ba);
        b->material.state(0xa4, 0, bb);
        return ba < bb;
    });
    const char* only = SDL_getenv("CR3_MESHES"); // (debugging: "1,3" draws meshes 1 and 3 only)
    for (const ViewMesh* mp : order) {
        const ViewMesh& m = *mp;
        if (m.hidden) continue;
        if (only && !std::strstr((std::string(",") + only + ",").c_str(),
                                 (',' + std::to_string(mp - f.meshes.data()) + ',').c_str()))
            continue;
        cr3::DrawState st;
        cr3::MeshShading sh;
        uint32_t v = 0, v2 = 0;
        if (m.material.state(0xa1, 0, v) && v) { // alpha test: GREATER (the game's only func) than ref
            uint32_t bits = 0;
            m.material.state(0xa0, 0, bits, 1);
            std::memcpy(&sh.alphaRef, &bits, 4);
        }
        m.material.state(0xa4, 0, v);
        st.blend = v != 0;
        m.material.state(0xa2, kGlSrcAlpha, v, 0);
        m.material.state(0xa2, kGlOneMinusSrcAlpha, v2, 1);
        st.blendSrc = cr3::blendCode(v, false);
        st.blendDst = cr3::blendCode(v2, true);
        m.material.state(0xa3, 0x8006, v); // blend_equation (271 materials of 166 battle effects: REVERSE_SUBTRACT)
        st.blendOp = v == 0x800a ? 1 : v == 0x800b ? 2 : v == 0x8007 ? 3 : v == 0x8008 ? 4 : 0;
        m.material.state(0xa6, 1, v);
        m.material.state(0xa5, kGlBack, v2);
        st.cull = v && !SDL_getenv("CR3_NO_CULL") ? (v2 == kGlFront ? 1 : 2) : 0;
        if (mirrored) st.cull = 0; // (placed mirrored: Fld2TaskMap::OnDraw - EntryCullFaceEnable(false) while it draws)
        m.material.state(0xa8, 1, v);
        st.depthWrite = v != 0;
        m.material.state(0xa9, 1, v);
        st.depthTest = v != 0;
        // each texture with its own sampler (the material's texture entries)
        st.sampling = cr3::sampling(m.material.sampler(0x32));
        sh.texMode = m.texture ? 4 : -1; // (the built-in Figure shaders: modulate the vertex colour)
        sh.lit = m.lit;
        cr3::applyMaterial(m.material, shaders->features(m.material.shaderKey), sh); // (the game's shading: keyed materials)
        for (int c = 0; c < 4; ++c) sh.color[c] *= f.tint[c];
        if (f.tint[3] < 1 && !st.blend)
            st.blend = true, st.blendSrc = cr3::blendCode(kGlSrcAlpha, false), st.blendDst = cr3::blendCode(kGlOneMinusSrcAlpha, true);
        sh.normalMap = m.normalMap;
        sh.normalSampling = cr3::sampling(m.material.sampler(0x35));
        sh.extraMap = m.extraMap;
        sh.extraSampling = cr3::sampling(m.material.sampler(m.material.textures.count(0x48)   ? 0x48
                                                            : m.material.textures.count(0x44) ? 0x44
                                                            : (sh.shade & SHADE_OVERLAY)     ? 0x32
                                                                                              : 0x43));
        dev->drawMesh(m.gpu, m.texture, mvp, sh, st);
    }
}

// A painted layer: the 4096-unit quad with its image
void Viewer::drawLayer(const std::string& image, const Mat4& proj, float shiftX) {
    cr3::Texture* t = texture(image);
    if (!t) return;
    float v[24];
    cr3::FieldMap::layerQuad(v, shiftX);
    static const float kWhite[4] = {1, 1, 1, 1};
    dev->drawQuad(*t, v, proj.data(), kWhite);
}

void Viewer::grabCrossFade(int fw, int fh, cr3::EventRuntime& ev) {
    ev.markCrossFadeCaptured();
    dev->beginFrame(fw, fh);
    placeScreen(fw, fh);
    if ((game.bgColor[0] > 0 || game.bgColor[1] > 0 || game.bgColor[2] > 0) && white.id) {
        static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
        const Mat4 id = cr3::mat4Identity();
        const float rgba[4] = {game.bgColor[0], game.bgColor[1], game.bgColor[2], 1};
        dev->drawQuad(white, kScreen, id.data(), rgba);
        dev->clearDepth();
    }
    drawScene(frameW, frameH);
    dev->endScene();
    dev->endFrame(false);
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (!dev->readPixels(px, w, h) || frameW <= 0 || frameH <= 0 || frameX + frameW > w || frameY + frameH > h) return;
    cr3::Image img;
    img.width = frameW, img.height = frameH;
    img.rgba.resize(size_t(frameW) * frameH * 4);
    for (int y = 0; y < frameH; ++y) {
        const uint8_t* row = &px[(size_t(frameY + frameH - 1 - y) * w + frameX) * 4];
        std::memcpy(&img.rgba[size_t(y) * frameW * 4], row, size_t(frameW) * 4);
    }
    for (size_t k = 3; k < img.rgba.size(); k += 4) img.rgba[k] = 255;
    if (crossFadeTexture.id) dev->destroy(crossFadeTexture);
    crossFadeTexture = dev->upload(img);
    dev->resetHistory();
    SDL_Log("event: crossfade captured (%dx%d, %.2f s)", frameW, frameH, ev.crossFade().duration);
}

// A frame of fw x fh: the game's screen all of it (an offscreen device's frames are the
// chosen resolution, letterboxed into the window when shown), or - drawing straight into the
// window (GL) - the resolution's shape letterboxed in it. The scene (smaller while an
// upscaler is on), then the UI over the resolved frame at full size.
void Viewer::draw(int fw, int fh) {
    if (battleMode && glassPending) grabGlass(fw, fh);
    if (cr3BattleRequest.active) startRequestedCr3Battle(fw, fh);
    if (cr3::EventRuntime* ev = activeEvents(); ev && ev->crossFade().capture) grabCrossFade(fw, fh, *ev);
    dev->beginFrame(fw, fh);
    placeScreen(fw, fh);
    if ((game.bgColor[0] > 0 || game.bgColor[1] > 0 || game.bgColor[2] > 0) && white.id) { // (SetBGColor: GameMain::Draw's clear colour)
        static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
        const Mat4 id = cr3::mat4Identity();
        const float rgba[4] = {game.bgColor[0], game.bgColor[1], game.bgColor[2], 1};
        dev->drawQuad(white, kScreen, id.data(), rgba);
        dev->clearDepth();
    }
    drawScene(frameW, frameH); // (the scene: all of the frame)
    dev->endScene();
    applySceneBlur(fw, fh); // (PostEffectBlur: before the cross-fade, under the UI)
    if (const cr3::EventRuntime* ev = activeEvents(); ev && ev->crossFade().active && crossFadeTexture.id) {
        const float alpha = ev->crossFade().alpha(eventLead());
        if (alpha > 0) {
            static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
            const float rgba[4] = {1, 1, 1, alpha};
            fullFrameQuad(crossFadeTexture, kScreen, rgba);
        }
    }
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH)); // (the UI: inside the safe area)
    drawEventUi(); // (the vista's, or the field's events')
    drawMovie(screenW, screenH);
}

// a quad over the whole frame - a fade, a dimming, a movie's black -, not the UI's screen alone;
// the UI's viewport back after it
void Viewer::fullFrameQuad(const cr3::Texture& t, const float* v, const float* rgba) {
    static const Mat4 id = cr3::mat4Identity();
    dev->setViewport(float(frameX), float(frameY), float(frameW), float(frameH));
    dev->drawQuad(t, v, id.data(), rgba);
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
}

// (PostEffectRenderer's full-screen figure - SetScale / SetRotation / SetAlpha -: a frame copy over
// the frame, scaled and turned about its centre; pass 5 alpha-blended, pass 1 added)
void Viewer::blurQuad(const cr3::Texture& t, int fw, int fh, float scale, float roll, float alpha, bool additive) {
    if (!t.id || fw <= 0 || fh <= 0) return;
    const float u0 = float(frameX) / float(fw), u1 = float(frameX + frameW) / float(fw);
    float vTop = float(frameY) / float(fh), vBottom = float(frameY + frameH) / float(fh); // (row 0 the top)
    if (t.bottomUp) vTop = 1 - vTop, vBottom = 1 - vBottom;
    const float hw = float(frameW) * 0.5f, hh = float(frameH) * 0.5f, c = std::cos(roll), s = std::sin(roll);
    auto corner = [&](float x, float y, float u, float v, float* out) { // (x, y: -1 .. 1 of the frame)
        const float px = x * hw * scale, py = y * hh * scale;
        out[0] = (px * c - py * s) / hw, out[1] = (px * s + py * c) / hh, out[2] = u, out[3] = v;
    };
    float q[24];
    corner(-1, -1, u0, vBottom, q), corner(1, -1, u1, vBottom, q + 4), corner(1, 1, u1, vTop, q + 8);
    corner(-1, -1, u0, vBottom, q + 12), corner(1, 1, u1, vTop, q + 16), corner(-1, 1, u0, vTop, q + 20);
    const float rgba[4] = {1, 1, 1, alpha};
    if (additive) dev->setQuadAdditive(true);
    fullFrameQuad(t, q, rgba);
    if (additive) dev->setQuadAdditive(false);
}

// GameMain::PostEffectBlur (0x0096ca54), after the scene (its bloom) and before the cross-fade and
// the UI - GameMain::Render: once a frame. Motion (GameSystem +0x68): the scene copied to the next
// buffer, the last one put over it at 0.5 (pass 5). Zoom (+0x69): the last one put over at 0.5
// scaled 1.2, then the result copied on - a trail. Special (+0x6a): the last one added over
// (pass 1: SRC_ALPHA, ONE) scaled 1.1, turned 0.01, at min(n / 50 + 0.5, 0.8), the result copied
// on (pass 3), and the scene put on the screen zoomed min(1 + n / 1000, 1.02) (layer 0xc). All off:
// the count and the zoom back to 0 and 1
void Viewer::applySceneBlur(int fw, int fh) {
    SceneBlur& b = sceneBlur;
    const bool motion = game.motionBlur, zoom = game.zoomBlur, special = game.specialBlur;
    if (!motion && !zoom && !special) {
        if (b.n) SDL_Log("scene blur: off at %.2f s, %u frames", double(uiClock), b.n);
        b.n = 0, b.zoom = 1;
        return;
    }
    if (!b.n) SDL_Log("scene blur: %s%s%s at %.2f s", motion ? "motion " : "", zoom ? "zoom " : "", special ? "special " : "", double(uiClock));
    const int read = int(b.n & 1u), write = read ^ 1;
    bool copied = true;
    if (motion) {
        copied = dev->copyFrame(b.buf[write]) && copied;
        if (b.n && copied) blurQuad(b.buf[read], fw, fh, 1.0f, 0, 0.5f, false);
    }
    if (zoom) {
        if (b.n && copied) blurQuad(b.buf[read], fw, fh, 1.2f, 0, 0.5f, false);
        copied = dev->copyFrame(b.buf[write]) && copied;
    }
    if (special) {
        const float alpha = std::min(float(b.n) / 50.0f + 0.5f, 0.8f);
        b.zoom = std::min(1.0f + float(b.n) / 1000.0f, 1.02f);
        if (b.n && copied) blurQuad(b.buf[read], fw, fh, 1.1f, 0.01f, alpha, true);
        copied = dev->copyFrame(b.buf[write]) && copied; // (pass 3 - a shader of type 8 - drawn here as a plain copy)
    }
    if (!copied) { // (a device without frame copies: no blur at all)
        b.n = 0, b.zoom = 1;
        return;
    }
    ++b.n;
    if (b.zoom != 1) { // (layer 0xc: the scene - its buffer just copied, else a copy of its own - zoomed onto the screen)
        const cr3::Texture* scene = special ? &b.buf[write] : dev->copyFrame(b.zoomed) ? &b.zoomed : nullptr;
        if (scene) blurQuad(*scene, fw, fh, b.zoom, 0, 1, false);
    }
}

// The display's safe area in a frame of fw x fh: SDL lays the window out under a camera hole
// (LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS), so the scene can fill all of it while the UI stays
// clear - the insets, each axis's larger on both its sides so the UI's screen stays centred, in
// the frame's pixels (an offscreen device's frame is scaled into the window to fit: the insets
// less the bars around it, in its own pixels). CR3_SAFE_INSETS "l,t,r,b" in the frame's pixels: tests
void Viewer::safeInsets(int fw, int fh, int& ix, int& iy) const {
    ix = iy = 0;
    int l = 0, t = 0, r = 0, b = 0;
    if (const char* e = SDL_getenv("CR3_SAFE_INSETS"); e && std::sscanf(e, "%d,%d,%d,%d", &l, &t, &r, &b) == 4) {
    } else if (SDL_Rect safe; window && SDL_GetWindowSafeArea(window, &safe)) {
        int ww = 0, wh = 0, pw = 0, ph = 0;
        if (!SDL_GetWindowSize(window, &ww, &wh) || ww <= 0 || wh <= 0 || !SDL_GetWindowSizeInPixels(window, &pw, &ph) || pw <= 0 || ph <= 0) return;
        const float sx = float(pw) / float(ww), sy = float(ph) / float(wh); // (the insets in the window's pixels)
        float fl = float(safe.x) * sx, ft = float(safe.y) * sy, fr = float(ww - safe.x - safe.w) * sx, fb = float(wh - safe.y - safe.h) * sy;
        if (dev->offscreen()) { // (the frame scaled to fit the window, centred: the bars around it count against the insets)
            const float k = std::min(float(pw) / float(fw), float(ph) / float(fh));
            const float ox = (float(pw) - float(fw) * k) * 0.5f, oy = (float(ph) - float(fh) * k) * 0.5f;
            fl = (fl - ox) / k, fr = (fr - ox) / k, ft = (ft - oy) / k, fb = (fb - oy) / k;
        } else { // (the frame is the window's pixels - a chosen resolution letterboxed in it: placeScreen)
            fl *= float(fw) / float(pw), fr *= float(fw) / float(pw), ft *= float(fh) / float(ph), fb *= float(fh) / float(ph);
        }
        l = int(std::ceil(std::max(fl, 0.0f))), t = int(std::ceil(std::max(ft, 0.0f)));
        r = int(std::ceil(std::max(fr, 0.0f))), b = int(std::ceil(std::max(fb, 0.0f)));
    }
    ix = std::clamp(std::max(l, r), 0, fw / 4), iy = std::clamp(std::max(t, b), 0, fh / 4);
}

// The frame (frameX ...): all of fw x fh, or - drawing straight into the window (GL) - a chosen
// resolution's shape letterboxed in it; the scene's viewport on it. The UI's screen (screenX ...):
// the frame inside the safe area
void Viewer::placeScreen(int fw, int fh) {
    frameX = frameY = 0, frameW = fw, frameH = fh;
    if (!dev->offscreen() && resW > 0 && resH > 0) {
        if (int64_t(resW) * fh > int64_t(fw) * resH) frameH = std::max(int(int64_t(fw) * resH / resW), 1);
        else frameW = std::max(int(int64_t(fh) * resW / resH), 1);
        frameX = (fw - frameW) / 2, frameY = (fh - frameH) / 2;
    }
    int ix = 0, iy = 0;
    safeInsets(fw, fh, ix, iy);
    const int x0 = std::max(frameX, ix), y0 = std::max(frameY, iy);
    const int x1 = std::min(frameX + frameW, fw - ix), y1 = std::min(frameY + frameH, fh - iy);
    screenX = x0, screenY = y0, screenW = std::max(x1 - x0, 1), screenH = std::max(y1 - y0, 1);
    int rw = fw, rh = fh;
    dev->sceneSize(fw, fh, rw, rh);
    const float kx = float(rw) / float(fw), ky = float(rh) / float(fh);
    dev->setViewport(frameX * kx, frameY * ky, frameW * kx, frameH * ky);
}

void Viewer::drawScene(int w, int h) {
    if (cr3Title.stage) return drawCr3TitleScene(w, h);
    if (cr3BattleMode) return drawCr3Battle(w, h);
    if (credits.stage || titleMenu.stage) return; // (the staff roll: its figure over black, in the interface's pass; the title)
    if (battleMode) {
        drawBattle(w, h);
        return;
    }
    if (eventMode) {
        drawEvent(w, h);
        return;
    }
    if (areaMode) {
        drawArea(w, h);
        return;
    }
    if (fieldMode) {
        // FldMain's order: the background (and midground), the 3D figures, the foreground
        // the scroll: following the leader, else the script's camera (a slide drawn on between updates)
        cr3::FieldFrame fr;
        field.setQuake(script ? script->quakeOffset() : 0.0f); // (Cr3FieldMapCamera.QuakeStart)
        if (script && script->camera().scripted()) {
            const cr3::FieldScript::Camera& c = script->camera();
            float cx = c.x, cy = c.y;
            if (c.frames > 0 && c.t < c.frames) {
                const float k = std::clamp(fieldClock / cr3::EventRuntime::kTick, 0.0f, 1.0f) / float(c.frames);
                cx += (c.toX - c.fromX) * k, cy += (c.toY - c.fromY) * k;
            }
            fr = field.frameScrolled(w, h, player, cx, cy);
        } else {
            fr = field.frame(w, h, player);
        }
        if (script) script->setShownScroll(-fr.scroll[0] / (float(w) / 960.0f), -fr.scroll[1] / (float(w) / 960.0f));
        fieldScene = fr.scene;
        dev->setCamera(fr.zNear, fr.zFar, fr.fovY * 3.14159265f / 180);
        cr3::SceneLighting l = field.lighting();
        std::memcpy(l.camera, fr.camera, sizeof l.camera);
        // FldMap::Draw on the sky's maps (+0x15): the point light carried - the light figure's
        // place + the leader's DummyEP04 + 0.1 up
        cr3::Mat34 ep;
        if (field.info().sharedBackground && jointPlace(fig, "DummyEP04", ep))
            l.pointPos[0] += ep.m[3], l.pointPos[1] += ep.m[7] + 0.1f, l.pointPos[2] += ep.m[11];
        l.time = time;
        dev->setLighting(l);
        // (the layers the script hid - Cr3FieldMap.SetVisibleBG / MG / 3D / FG - left out)
        auto shown = [this](int layer) { return !script || script->layerVisible(layer); };
        if (shown(0)) drawLayer(field.backgroundImage(), fr.background);
        if (shown(2)) drawLayer(field.midgroundImage(), fr.midground);
        if (shown(3)) drawFigure(mapFigure, fr.scene.data());
        for (const ViewFigure& g : gimmicks) drawFigure(g, fr.scene.data());
        drawResidents(fr.scene.data());
        // an event's scene in the field (EvtScene in FldMain: its models among the field's
        // figures, through the field's camera), the leader unless the script hid it
        if (script && script->events().sceneShown()) {
            const std::vector<cr3::EventModel>& models = script->events().scene().models();
            for (size_t i = 0; i < models.size() && i < eventFigures.size(); ++i)
                if (models[i].visible) {
                    if (models[i].hasShadow() && models[i].shadow) drawShadow(eventFigures[i].place, fr.scene.data());
                    drawFigure(eventFigures[i], fr.scene.data());
                    if (i < eventHeads.size() && !eventHeads[i].meshes.empty()) drawFigure(eventHeads[i], fr.scene.data());
                }
        }
        if (!script || script->playerVisible()) drawShadow(fig.place, fr.scene.data()), drawFigure(fig, fr.scene.data());
        if (shown(1)) drawLayer(field.foregroundImage(), fr.foreground, field.info().foregroundShift == 1 ? -1152.0f : 0.0f);
        return;
    }
    float eye[3];
    const float d = modelEye(eye);
    cr3::SceneLighting l = lighting;
    std::memcpy(l.camera, eye, sizeof l.camera);
    l.time = time;
    dev->setLighting(l);
    const float zn = fig.radius * 0.01f * zoom, zf = fig.radius * 20 * zoom + d;
    dev->setCamera(zn, zf, 0.7f);
    const Mat4 mvp = cr3::mat4Multiply(perspective(0.7f, float(w) / float(std::max(h, 1)), zn, zf), lookAt(eye, fig.centre));
    drawFigure(fig, mvp.data());
}

auto Viewer::menuList() -> MenuList {
    if (saveImport.active() || inputDialog.open) return {};
    if (settingsOpen) return {&settingsCursor, int(settingRows().size()), 1};
    if (cr3Camp.stage == 9 && campOptions.tab == 3 && !campOptions.asking)
        return {&settingsCursor, int(settingRows().size()), 2};
    if (cr3Camp.stage == 3) return {&cr3Camp.focus, int(cr3Camp.genes.size()), 3};
    if (cr3Camp.stage == 4 && campItems.told.empty()) {
        if (campItems.step == 0) return {&campItems.row, int(campItems.rows.size()), 100 + campItems.category};
        if (campItems.step == 2) return {&cr3Camp.focus, int(cr3Camp.genes.size()), 4};
    }
    if (cr3Camp.stage == 5 && !campEquip.model)
        return campEquip.step == 0 ? MenuList{&campEquip.slot, 3, 5} : MenuList{&campEquip.row, int(campEquip.rows.size()), 6 + campEquip.slot};
    if (auto* ev = activeEvents(); ev && ev->geneLab().open && !ev->geneLab().talking && geneMenu.alert.empty()) {
        if (geneMenu.screen == 2) return {&geneMenu.focus, int(geneMenu.genes.size()), 10};
        if (geneMenu.screen == 3) return {&geneMenu.row, int(geneMenu.rows.size()), 11};
        if (geneMenu.screen == 7) return {&geneMenu.row, int(geneMenu.pickRows.size()), 12};
    }
    return {};
}

bool Viewer::menuScroll(int rows) {
    const MenuList list = menuList();
    if (!list.cursor || list.count <= 0) return false;
    const int old = *list.cursor;
    *list.cursor = mobile::scrollCursor(old, list.count, rows);
    if (*list.cursor != old && audio.opened()) audio.playSe("com_002", false);
    return true;
}

void Viewer::cancelPointerInput() {
    menuSwipe = mobile::MenuSwipe();
    areaDrag = Cr3AreaDrag();
    areaCam = Cr3AreaCam();
    worldSel.dragging = town.dragging = myPage.dragging = false;
    fastHeld = false, fastUntil = 0;
    cr3Camp.confirmHeld = false;
    keyLock.pressing = vslash.pressed = false;
    if (iai.stage == 2) iai.stage = 1;
    if (aim.stage == 2) aim.stage = 1;
    pointerCancelled = true;
    padHeldOver.cancel();
}

bool Viewer::consumeInputCancel(crinput::CancelWatch& cancel) {
    if (!cancel.consume()) return false;
    cancelPointerInput();
    return true;
}

bool Viewer::menuPointerDown(float x, float y) {
    const MenuList list = menuList();
    if (!list.cursor || list.count <= 0 || x < 0.1f || x > 0.98f || y < 0.12f || y > 0.88f) return false;
    menuSwipe.begin(x, y, list.context);
    return true;
}

void Viewer::menuPointerMove(float x, float y) {
    if (!menuSwipe.down) return;
    const auto list = menuList();
    if (!list.cursor || list.context != menuSwipe.context) { menuSwipe = {}; return; }
    menuScroll(menuSwipe.move(x, y));
}

bool Viewer::menuPointerUp(float x, float y) {
    if (!menuSwipe.down) return false;
    menuPointerMove(x, y);
    const auto gesture = menuSwipe;
    menuSwipe = {};
    if (gesture.down && !gesture.moved) pointerActivate(x, y);
    return true;
}

void Viewer::pointerDown(float fx, float fy) {
    if (eventMode && event && event->vistaPaused) return;
    if (!menuPointerDown(fx, fy)) pointerActivate(fx, fy);
}

void Viewer::pointerActivate(float fx, float fy) {
    if (inputDialogClick(fx, fy)) {} // (CR3's text entry: over everything)
    else if (overFast(fx, fy)) fastHeld = true; // (the fast-forward button: held, not a tap)
    else if (cr3OptionsClick(fx, fy)) {} // (CR3-U's Options over the title or a battle; the title's button)
    else if (saveMenuClick(fx, fy)) {} // (CR3's save files)
    else if (cr3Title.stage == 2) tap(); // (CR3's title: a click anywhere is its tap)
    else if (titleMenu.stage) titleClick(fx, fy);
    else if (geneGetTap()) {}
    else if (collectClick(fx, fy)) {}
    else if (yesNoClick(fx, fy)) {}
    else if (choiceClick(fx, fy)) {}
    else if (clearQuestClick(fx, fy)) {}
    else if (keyBuyClick(fx, fy)) {}
    else if (lunchClick(fx, fy)) {}
    else if (geneLabClick(fx, fy)) {}
    else if (spacePortClick(fx, fy)) {}
    else if (cr3ShopClick(fx, fy)) {}
    else if (worldSelectClick(fx, fy)) {}
    else if (townMoveClick(fx, fy)) {}
    else if (vsClick(fx, fy)) {}
    else if (cr3CampClick(fx, fy)) {}
    else if (battleMode && choosing) battleClick(fx, fy);
    else if (cr3BattleMode && cr3BattleClick(fx, fy)) {}
    else if (vslash.stage == 1) vslashPointer(fx, fy, 0);
    else if (fieldMode && !eventMode && keyLock.stage != 0) unlockPointer(fx, fy, 0);
    else if (fieldMode && !eventMode && iai.stage) iaiPointer(fx, fy, 0);
    else if (fieldMode && !eventMode && aim.stage) aimPointer(fx, fy, 0);
    else if (fieldMode && !eventMode && hakkei.stage) hakkeiPointer(fx, fy);
    else if (fieldMode && !eventMode && mystic.stage) mysticPointer(fx, fy);
    else if (fieldMode && !eventMode && (sprintTap(fx, fy) || actionTap(fx, fy))) {}
    else if (fieldMode && !eventMode && fieldClick(fx, fy)) {
    } else if (areaDragFree()) { // (CR3's field: a drag walks, a touch let go taps; a finger on the camera's half turns it)
        if (pointerTouch && areaCamFree() && cameraHalf(fx, fy) && !areaCam.on) areaCamStart(fx, fy, false);
        else areaDragStart(fx, fy);
    }
    else if (eventMode || fieldMode || battleMode || cr3BattleMode || areaMode) tap();
}

// (--input click:<fx>:<fy>: a press and its release there, for tests)
void Viewer::testClick() {
    if (inputScript.empty() || inputScript.front().compare(0, 6, "click:") != 0 || uiClock < inputNext) return;
    float fx = 0.5f, fy = 0.5f;
    std::sscanf(inputScript.front().c_str() + 6, "%f:%f", &fx, &fy);
    inputScript.pop_front();
    inputNext = uiClock + 0.4f;
    SDL_Log("click at %.3f %.3f", fx, fy);
    pointerDown(fx, fy);
    menuPointerUp(fx, fy);
    areaDragEnd(), fastHeld = false;
}

bool Viewer::overFast(float fx, float fy) const {
    if (fastRect.id == 0) return false;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    return x >= std::min(fastRect.x0, fastRect.x1) && x <= std::max(fastRect.x0, fastRect.x1) && y >= std::min(fastRect.y0, fastRect.y1) &&
           y <= std::max(fastRect.y0, fastRect.y1);
}

void Viewer::advance(float dt, float sx, float sy, bool run) {
    uiClock += dt;
    const bool vistaPaused = eventMode && event && event->vistaPaused;
    if (!vistaPaused && (!eventMode || !event || !event->windowOpen(cr3::EventRuntime::Vista))) vistaControlsUntil = -1;
    audio.pauseSceneSound(vistaPaused);
    if (vistaPaused) return;
    stepQuake(dt); // (the root's QuakeControlTask: whichever script started it)
    // (tests: --input's language@s - the options' language one on at s seconds, as its arrow)
    if (!inputScript.empty() && inputScript.front().compare(0, 9, "language@") == 0 && uiClock >= float(std::atof(inputScript.front().c_str() + 9)))
        inputScript.pop_front(), optionTap(3106);
    testClick();
    if (!inputScript.empty() && inputScript.front().rfind("lunch:start:", 0) == 0) {
        const std::string request = inputScript.front().substr(12);
        inputScript.pop_front();
        const size_t colon = request.find(':');
        startLunch(request.substr(0, colon), colon == std::string::npos ? "null" : request.substr(colon + 1));
    }
    stepMovie(dt);
    if (areaMode || cr3BattleMode) game.cr3Records.playSeconds += dt; // (My Page's play time)
    if (game.cr3TeamLevel != loggedTeamLevel) SDL_Log("team level %d -> %d", loggedTeamLevel, game.cr3TeamLevel), loggedTeamLevel = game.cr3TeamLevel;
    if (cr3::EventRuntime* ev = activeEvents()) {
        if (!inputScript.empty() && inputScript.front() == "fast" && uiClock >= fastUntil && (ev->fastForwardShown() || ev->skipButtonShown()))
            inputScript.pop_front(), fastUntil = uiClock + 2.0f; // (each two seconds of it, one after the other)
        ev->fastForward = fastHeld || uiClock < fastUntil;
    }
    if (!inputScript.empty() && inputScript.front().compare(0, 6, "lunch:") == 0 && lunchIdle()) { // (--input lunch:<model> / lunch:any: its balloon tapped)
        std::string want = inputScript.front().substr(6);
        if (want == "any") // (the marked one, else the meal's end, else another - then waiting for it to take taps)
            for (int frame : {1, 2, 0})
                for (const cr3::EventRuntime::LunchBalloon& b : event->lunch().balloons)
                    if (want == "any" && b.shown && b.frame == frame) want = b.model;
        for (const cr3::EventRuntime::LunchBalloon& b : event->lunch().balloons)
            if (b.model == want && b.shown && b.delay <= 0) {
                inputScript.pop_front();
                chooseLunchBalloon(b.model);
                break;
            }
    }
    stepKeyBuy(dt);
    if (!inputScript.empty() && inputScript.front() == "questok" && clearQuestShown() && activeEvents()->clearQuestAge() > 0.5f) { // (--input: the cleared quest's OK)
        answerClearQuest();
        inputScript.pop_front();
    }
    if (!inputScript.empty() && inputScript.front().compare(0, 7, "choice:") == 0 && choiceShown() && activeEvents()->choiceAge() > 0.5f) { // (--input: an event's choice, from 1)
        answerChoice(std::atoi(inputScript.front().c_str() + 7) - 1);
        inputScript.pop_front();
    }
    syncInputDialog();
    if (!inputScript.empty() && inputScript.front().compare(0, 5, "type:") == 0 && inputDialog.open && uiClock - inputDialog.since > 0.5f) { // (--input type:<text>: the text entry's text, then its OK)
        inputDialog.text.clear(), typeInputDialog(inputScript.front().c_str() + 5), closeInputDialog(true);
        inputScript.pop_front();
    }
    if (!inputScript.empty() && (inputScript.front() == "yes" || inputScript.front() == "no") && yesNoShown() &&
        activeEvents()->yesNoAge() > 0.5f) { // (--input: an event's yes / no)
        answerYesNo(inputScript.front() == "yes");
        inputScript.pop_front();
    }
    audio.setVolumes(float(optionVolume[0]) / 100.0f, float(optionVolume[1]) / 100.0f, float(optionVolume[2]) / 100.0f);
    audio.update(dt);
    updateVistaSlash();
    if (optionsWasOpen && !optionsOpen && scriptSaves) writeSystem(); // (OptionMenu's end: Cr3Util_Save_SystemData)
    optionsWasOpen = optionsOpen;
    // (--input wait:<seconds>: tests - the inputs after it held that long, whatever runs meanwhile:
    // the field, a vista, a battle, the title)
    if (!inputScript.empty() && inputScript.front().compare(0, 5, "wait:") == 0 && (inputWait += dt) >= float(std::atof(inputScript.front().c_str() + 5)))
        inputScript.pop_front(), inputWait = 0;
    if (credits.stage) return updateCredits(dt);
    if (titleMenu.stage) return updateTitle(dt);
    if (cr3OptionsOver()) cr3OptionsInput(), cr3Camp.since += dt, campOptions.askSince += dt; // (its clock: the field's camp keeps its own)
    if (cr3Title.stage) return updateCr3Title(dt);
    // (Battle Speed, Field Speed: the battles, and the field while the player walks it - nothing
    // running or open over it -, that many times as fast)
    if (cr3BattleMode) return updateCr3Battle(dt * float(optionsOpen || settingsOpen || cr3OptionsOver() ? 1 : settings.battleSpeed));
    if (areaMode && !eventMode)
        return updateArea(dt * float(areaEvents && areaEvents->skipHeld() ? 5 : cr3FieldIdle() && cr3Camp.stage == 0 && !optionsOpen && !settingsOpen ? settings.fieldSpeed : 1), sx, sy, run);
    if (playMode && (fieldMode || eventMode || battleMode) && !paused) playSeconds += dt; // (the save's play time)
    if (battleMode) {
        battleClock += dt;
        if (glass.running()) glass.advanceTo(battleClock - glassStart); // (its task: a frame each update)
        if (!inputScript.empty() && inputScript.front() == "skip" && scene.active && battleClock > scene.start + 1.0f) { // (--input: a tap on the scene)
            inputScript.pop_front();
            SDL_Log("input skip%s", skipScene() ? "" : " (not now)");
        }
        updateBattleScript(dt);
        updateManual(dt);
        updateBattleSounds();
        updateBattleShow();
        updateResult();
        // a fight of the flow over: its last action shown, a moment after (a win: its result)
        updateGameOver();
        if (fightingInFlow && battleStep == BattleStep::Over && !battleScriptRunning() && battleClock > showEnd + 1.5f &&
            (result.stage == 0 || result.stage >= 4) && gameOver.stage == 0) {
            fightingInFlow = false;
            SDL_Log("play: battle %s", battleRules->exited() ? "ended by its script" : battleRules->escaped() ? "escaped" : battleRules->outcome() == 1 ? "won" : "lost (back to the field: no game over yet in the story)");
            ++battlesSkipped;
            afterBattle();
        }
    } else if (eventMode) {
        updateEvent(event && event->skipHeld() ? dt * 5 : dt); // (the skip button held: five updates a frame)
    } else if (fieldMode && (campOpen || mapOpen || saveStage || world.stage || shop.stage || party.stage || library.stage == 1 || pub.stage)) { // (FldMain steps 5 .. 7, 10: the camp, the map, saving, the world select - the field held)
        updateManual(dt);
        if (saveStage == 2 && uiClock - saveStart > (saveFailed ? 3.0f : 1.0f)) saveStage = 0; // (a second, then closed)
        if (script && !script->running()) { // (the fade, the windows go on under a menu: GameMain's update - the library's fade back in)
            fieldClock = std::min(fieldClock + dt, 0.25f);
            for (; fieldClock >= cr3::EventRuntime::kTick; fieldClock -= cr3::EventRuntime::kTick) script->tick();
        }
        fieldInput();
    } else if (fieldMode) {
        fieldUpdate(script && script->events().skipHeld() ? dt * 5 : dt);
        updateChange(dt);
        updateSprint(dt);
        updateIai();
        fieldInput();
        if (fieldMode && !eventMode) {
            const bool held = fieldScriptRunning() || keyLock.stage != 0 || changeStage != 0 || actionRunning();
            walk(dt, held ? 0 : sx, held ? 0 : sy, run);
        }
    }
}

void Viewer::setBattleCamera(const std::string& name, const cr3::Mat34& place, bool still) {
    if (name.empty() || (name == battleCamera && std::equal(place.m, place.m + 12, battleCameraPlace.m))) return;
    battleCamera = name, battleCameraStart = battleClock, battleCameraPlace = place, battleCameraStill = still, battleCameraLoop = false;
    battleCameraMoveStart = -1, targetCameraFighter = -1;
    dev->resetHistory(); // (a cut)
}

void Viewer::setBattleCamera(int number) {
    if (const char* n = cr3::battleCameraName(number)) setBattleCamera(std::string(n));
}

bool Viewer::startBattle(int id, const int party[2]) {
    std::vector<uint8_t> d;
    std::string err;
    if (!archive.read("data\\battledata.dat", d, &err) || !battleData.parse(d, &err) || !cr3::setupBattle(battleData, id, party, battle, &err)) {
        SDL_Log("battle %d: %s", id, err.c_str());
        return false;
    }
    game.initSopia(battleData, cr3::sopiaExp); // (a new game's levels, the first time)
    game.initItems(battleData); // (and items)
    game.initEquip(battleData, cr3::levelExpTable()); // (and equipment, levels)
    cr3::Skeleton dpos;
    if (!archive.read("battledpos.skel", d, &err) || !cr3::parseSkeleton(d, dpos, &err)) {
        SDL_Log("battledpos: %s", err.c_str());
        return false;
    }
    const std::vector<cr3::Mat34> places = dpos.world(dpos.basePose);
    unload(battleMap);
    battleMap = ViewFigure();
    if (!loadFigure(battle.map + ".geom", battleMap, &err)) SDL_Log("battle map %s: %s", battle.map.c_str(), err.c_str());
    // (BtlMainCtrl::LoadResources: couplings 221, 271, 272, 317 - the map's eff284 too, drawn with it)
    unload(mapEffect), mapEffect = ViewFigure();
    if (id == 221 || id == 271 || id == 272 || id == 317)
        if (!loadFigure("eff284.geom", mapEffect, &err)) SDL_Log("eff284: %s", err.c_str());
    battleMap.absolute = true;
    cr3::Geom mg;
    cr3::Skeleton ms;
    lighting = cr3::SceneLighting();
    if (archive.read(battle.map + ".geom", d) && cr3::parseGeom(d, mg) && archive.read(battle.map + ".skel", d) && cr3::parseSkeleton(d, ms))
        cr3::lightsFromFigure(mg, ms, lighting);
    for (BattleUnit& u : battleUnits) unload(u.fig);
    battleUnits.clear();
    for (BattleEffect& fx : battleEffects) unload(*fx.fig);
    battleEffects.clear();
    for (SceneEffect& fx : commandEffects) unload(*fx.fig);
    commandEffects.clear();
    for (int f = 0; f < 2; ++f) dropGuardObject(f);
    for (auto& [id, fx] : scene.effects)
        if (fx.fig) unload(*fx.fig);
    scene = SceneState(), sceneCameras.clear(), battleFade = BattleFade(), battleMapHidden = false, mapColour = Ramp();
    glass = cr3::GlassFragments(), glassPending = false;
    // (the last battle's result goes with it: left at its "over" stage it kept the HUD's place -
    // drawBattleHudMain draws the result window from stage 3 on - and posed that window at
    // battleClock - its old windowStart, before its start: the next battle's HUD gone, the
    // result's panels coming in over the fight until the clock caught up - CR2-U issue #1)
    result = BattleResultShow();
    std::string log;
    auto add = [&](const cr3::BattleSetup::Unit& su) {
        BattleUnit u;
        if (!loadFigure(su.figure + ".geom", u.fig, &err)) SDL_Log("battle %s: %s", su.figure.c_str(), err.c_str());
        // a monster's colour: its images' letter (m206 + a: m206a01)
        if (!su.colour.empty())
            for (ViewMesh& vm : u.fig.meshes) {
                std::string t = vm.baseMaterial.diffuse();
                if (t.size() > su.figure.size() && t.compare(0, su.figure.size(), su.figure) == 0 && std::isalpha(static_cast<unsigned char>(t[su.figure.size()]))) {
                    t[su.figure.size()] = su.colour[0];
                    if (cr3::Texture* tex = texture(t)) vm.texture = tex;
                }
            }
        const int j = dpos.joint(cr3::nameHash(su.position.c_str()));
        if (j >= 0 && size_t(j) < places.size()) {
            cr3::Mat34 m = places[size_t(j)];
            for (int c = 0; c < 3; ++c) { // (its turn without the joint's scale)
                const float l = std::sqrt(m.m[c] * m.m[c] + m.m[4 + c] * m.m[4 + c] + m.m[8 + c] * m.m[8 + c]);
                if (l > 0) m.m[c] /= l, m.m[4 + c] /= l, m.m[8 + c] /= l;
            }
            u.fig.place = m;
        } else {
            SDL_Log("battle: no battledpos joint %s", su.position.c_str());
        }
        u.figure = su.figure, u.position = su.position, u.home = u.fig.place;
        u.motion = su.idle, u.then = "bb01", u.loop = su.idle == "bb01", u.motionStart = 0;
        log += " " + su.figure + su.colour + "@" + su.position;
        battleUnits.push_back(std::move(u));
    };
    for (const auto& su : battle.party) add(su);
    for (const auto& su : battle.monsters) add(su);
    const char* camFigures[2] = {"battlecamera_nomove", "battlecamera_move"};
    battleCamera.clear();
    for (int k = 0; k < 2; ++k) {
        CameraFigure& c = battleCameras[k];
        c = CameraFigure();
        if (archive.read(std::string(camFigures[k]) + ".geom", d)) cr3::parseGeom(d, c.geom);
        if (archive.read(std::string(camFigures[k]) + ".skel", d)) cr3::parseSkeleton(d, c.skel);
        c.animated = archive.read(std::string(camFigures[k]) + ".anim", d) && c.anim.parse(d) && c.anim.joints() == int(c.skel.basePose.size());
    }
    battleCamera = battle.camera;
    battleClock = 0;
    battleMode = true;
    fieldMode = eventMode = false;
    dev->resetHistory();
    status = "battle " + std::to_string(id) + " in " + battle.map;
    SDL_Log("battle %d: map %s, pattern %s,%s; camera %s", id, battle.map.c_str(), battle.patternName.c_str(), log.c_str(), battleCamera.c_str());
    return true;
}

// the battle's camera now: the named camera's joint in whichever figure has it
bool Viewer::battleView(cr3::Mat34& world, float& fovY, float& zn, float& zf) {
    const uint32_t h = cr3::nameHash(battleCamera.c_str());
    std::vector<const CameraFigure*> figures = {&battleCameras[0], &battleCameras[1]};
    for (const auto& [name, c] : sceneCameras) figures.push_back(&c); // (a scene's loaded ones)
    for (const CameraFigure* cp : figures)
        for (const cr3::GeomCamera& gc : cp->geom.cameras) {
            const CameraFigure& c = *cp;
            if (gc.hash != h) continue;
            const int j = c.skel.joint(h);
            if (j < 0) continue;
            std::vector<cr3::JointTransform> p = c.skel.basePose;
            if (c.animated) {
                const float frames = float(std::max(c.anim.frames() - 1, 1));
                const float at = battleCameraStill ? 0.0f : (battleClock - battleCameraStart) * c.anim.sampleRate();
                p = c.anim.poseAbsolute(c.skel.basePose, battleCameraLoop ? std::fmod(std::max(at, 0.0f), frames) : std::clamp(at, 0.0f, frames));
            }
            const std::vector<cr3::Mat34> w = c.skel.world(p);
            if (size_t(j) >= w.size()) continue;
            world = battleCameraPlace * w[size_t(j)];
            if (battleCameraMoveStart >= 0) {
                const float u = std::max((battleClock - battleCameraMoveStart) / 0.5f, 0.0f);
                if (u >= 1) battleCameraMoveStart = -1;
                else world = blendPlace(battleCameraFrom, world, u * (2 - u)); // (u + u (1 - u) k / 10)
            }
            fovY = gc.fovY;
            zn = std::max(gc.near * 0.5f, 0.1f), zf = std::min(gc.far * 5.0f, 5000.0f);
            zf = std::max(zf, zn * 2);
            return true;
        }
    return false;
}

// a unit's figure on one of its motions (<figure><suffix>.anim; false: it has none)
bool Viewer::useMotion(BattleUnit& u, const std::string& suffix) {
    const std::string want = u.figure + suffix + ".anim";
    const auto it = std::find(u.fig.anims.begin(), u.fig.anims.end(), want);
    if (it == u.fig.anims.end()) return false;
    if (!u.fig.animLoaded || u.fig.anims[u.fig.animIndex] != want) {
        const float keep = time;
        loadAnim(u.fig, size_t(it - u.fig.anims.begin()));
        time = keep;
    }
    return true;
}

float Viewer::motionLength(BattleUnit& u, const std::string& suffix) { return useMotion(u, suffix) ? u.fig.anim.duration() : 0.0f; }

void Viewer::playMotion(BattleUnit& u, const std::string& suffix, float at, const std::string& then) {
    u.motion = suffix, u.then = then == "bb01" ? stanceOf(u) : then, u.motionStart = at, u.loop = false, u.hold = false;
}

// a fighter's stance: bg01 guarding (Defend), bx02 in War's Executioner's Pose (state 37 -
// ActionSopiaLaunch: bx01, then bx02), else bb01
std::string Viewer::stanceOf(const BattleUnit& u) const {
    if (u.guarding) return "bg01";
    const int f = fighterOfUnit(u);
    if (f >= 0 && battleRules && battleRules->fighter(f).hasState(37) && battleRules->fighter(f).alive) return "bx02";
    return "bb01";
}

int Viewer::fighterOfUnit(const BattleUnit& u) const {
    for (int f = 0; f < 5; ++f)
        if (fighterUnit[f] >= 0 && size_t(fighterUnit[f]) < battleUnits.size() && &battleUnits[size_t(fighterUnit[f])] == &u) return f;
    return -1;
}

void Viewer::poseBattle() {
    pose(battleMap, battleClock);
    if (!specialMap.meshes.empty()) pose(specialMap, battleClock - specialMapStart); // (its animation from its making)
    if (!mapEffect.meshes.empty()) pose(mapEffect, battleClock);
    poseBattleShow();
    const float mc = mapColour.at(battleClock);
    for (int c = 0; c < 3; ++c) battleMap.tint[c] = mc;
    for (BattleUnit& u : battleUnits) {
        const float k = u.colour.at(battleClock);
        for (int c = 0; c < 3; ++c) u.fig.tint[c] = k;
        u.fig.tint[3] = u.alpha.at(battleClock);
        if (!u.fig.skinned) continue;
        if (u.hideAt >= 0 && battleClock >= u.hideAt) u.hidden = true, u.hideAt = -1;
        float t = battleClock - u.motionStart;
        if (!u.loop) { // (once, then its stance)
            const float d = motionLength(u, u.motion);
            if (u.hold && t >= d) t = d; // (kept at its end)
            else if (d <= 0 || t >= d) u.motionStart += std::max(d, 0.0f), u.motion = u.then, u.loop = true, t = battleClock - u.motionStart;
        }
        if (!useMotion(u, u.motion) && !useMotion(u, "bb01")) continue;
        pose(u.fig, std::max(t, 0.0f), u.loop);
    }
}

// the battleapos joint `name`'s place (false: none)
bool Viewer::aposPlace(const std::string& name, cr3::Mat34& out) {
    const int j = battleApos.joint(cr3::nameHash(name.c_str()));
    if (j < 0 || size_t(j) >= battleAposWorld.size()) return false;
    out = battleAposWorld[size_t(j)];
    for (int c = 0; c < 3; ++c) {
        const float l = std::sqrt(out.m[c] * out.m[c] + out.m[4 + c] * out.m[4 + c] + out.m[8 + c] * out.m[8 + c]);
        if (l > 0) out.m[c] /= l, out.m[4 + c] /= l, out.m[8 + c] /= l;
    }
    return true;
}

// where an attacker goes to strike a target (BtlActionCtrl::GetAposName): by the target the
// party's "aPos_<its place><the attacker's: pSoloA, pPairA...>", by one of the party a
// monster's "aPos_<pSoloA | pSoloB | pPair><its place: s, ssL, m, msL...>"
std::string Viewer::aposName(int actor, int target) {
    auto key = [&](int f) {
        const int u = fighterUnit[f];
        std::string k = u >= 0 ? battleUnits[size_t(u)].position.substr(5) : "";
        return k;
    };
    const bool pair = battleRules && battleRules->fighter(actor).partner >= 0;
    if (actor < 2) return "aPos_" + key(target) + std::string(pair ? (actor == 0 ? "pPairA" : "pPairB") : (actor == 0 ? "pSoloA" : "pSoloB"));
    std::string mk = key(actor);
    if (mk == "sC") mk = "s";
    else if (mk == "mC") mk = "m";
    const bool targetPair = battleRules && battleRules->fighter(target).partner >= 0;
    return "aPos_" + std::string(targetPair ? "pPair" : target == 0 ? "pSoloA" : "pSoloB") + mk;
}

bool Viewer::focusScreen() const {
    return titleMenu.stage == 5 || titleMenu.stage == 6 || titleMenu.stage == 7 || shop.stage == 1 || shop.stage == 2 ||
           party.stage == 1 || party.stage == 2;
}

bool Viewer::focusKey(SDL_Keycode k) {
    if (tabFocusKey(k)) return true;
    if (!focusScreen() || (titleMenu.stage && titleRunning())) return false;
    if (k == SDLK_BACKSPACE && titleMenu.stage == 6) return titleButton(100), true;
    if (k == SDLK_BACKSPACE && titleMenu.stage == 7) return titleAnswer(false), true;
    std::vector<TapRect> rs;
    for (const TapRect& r : tapRects)
        if (r.id >= 0) rs.push_back(r);
    if (rs.empty()) return false;
    auto mid = [](const TapRect& r, float& x, float& y) { x = (r.x0 + r.x1) * 0.5f, y = (r.y0 + r.y1) * 0.5f; };
    const auto cur = std::find_if(rs.begin(), rs.end(), [&](const TapRect& r) { return r.id == keyFocus; });
    const bool arrow = k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT;
    if (arrow && cur == rs.end()) {
        keyFocus = rs.front().id;
        audio.playSe(0xb3, false); // (the cursor's: the Vita's ScrollList / CircleBtn move)
        return true;
    }
    if (arrow) {
        const float dx = k == SDLK_RIGHT ? 1.0f : k == SDLK_LEFT ? -1.0f : 0.0f, dy = k == SDLK_UP ? 1.0f : k == SDLK_DOWN ? -1.0f : 0.0f;
        float cx, cy;
        mid(*cur, cx, cy);
        int best = -1, wrap = -1;
        float bestScore = 1e9f, wrapScore = 1e9f;
        for (size_t i = 0; i < rs.size(); ++i) {
            if (rs[i].id == cur->id) continue;
            float x, y;
            mid(rs[i], x, y);
            const float along = (x - cx) * dx + (y - cy) * dy, across = std::abs((x - cx) * dy - (y - cy) * dx);
            if (along > 0.01f) {
                if (along + 2 * across < bestScore) bestScore = along + 2 * across, best = int(i);
            } else if (along < -0.01f && across < 0.08f && along < wrapScore) { // (round: the farthest back in line)
                wrapScore = along, wrap = int(i);
            }
        }
        const int to = best >= 0 ? best : wrap;
        if (to >= 0) keyFocus = rs[size_t(to)].id, audio.playSe(0xb3, false), SDL_Log("focus: %d", keyFocus);
        return true;
    }
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
        if (cur == rs.end()) return keyFocus = rs.front().id, true;
        float x, y;
        mid(*cur, x, y);
        const float s = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float fx = x * s / float(std::max(screenW, 1)) + 0.5f, fy = 0.5f - y * s / float(std::max(screenH, 1));
        SDL_Log("focus: tap %d", cur->id);
        if (titleMenu.stage) titleClick(fx, fy);
        else fieldClick(fx, fy);
        return true;
    }
    return false;
}

// the open tab's ring (FUN_81098da8: Up / Left back, Down / Right on - the encounters' left out
// while off; ○ the one lit)
bool Viewer::tabFocusKey(SDL_Keycode k) {
    if (!fieldTabShown() || !fieldTabOpen) return tabFocus = 0, false;
    const bool encounters = game.flag(0x40f);
    static const int kRing[4] = {2, 5, 4, 3};
    int at = 0;
    while (at < 4 && kRing[at] != tabFocus) ++at;
    if (k == SDLK_DOWN || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_LEFT) {
        const int step = k == SDLK_DOWN || k == SDLK_RIGHT ? 1 : 3;
        at = at >= 4 ? (step == 1 ? 0 : 3) : (at + step) % 4;
        if (kRing[at] == 5 && !encounters) at = (at + step) % 4;
        tabFocus = kRing[at];
        audio.playSe(0xb3, false);
        SDL_Log("field: the tab's %d lit", tabFocus);
        return true;
    }
    if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && tabFocus) return fieldTabTap(tabFocus), true;
    return false;
}

// a list shown (a page of it a press of L / R: ScrollListMenu's)
bool Viewer::listShown() const {
    using CI = cr3::CommandInput;
    const bool battleList = battleMode && choosing &&
                            (commandInput.menu() == CI::Menu::Items || commandInput.menu() == CI::Menu::Advent ||
                             commandInput.menu() == CI::Menu::Awake || commandInput.menu() == CI::Menu::Sopia);
    return battleList || manualStage == 1 || settingsOpen || (campOpen && campList != 0) || library.stage == 1;
}

bool Viewer::inMenu() const {
    return campOpen || (battleMode && choosing) || shop.stage || world.stage || pub.stage || library.stage == 1 || party.stage;
}

// a step of the choosing begun: its camera (StepGetset: Cam_Getset; SetCommandCamera: 0x1a the
// first's, 0x1b the second's, 0x1d the pair's command, 0x1c the one alone's - the target's camera
// BtlCommand::SetCommandTargetCamera's - setTargetCamera)
void Viewer::menuChanged() {
    menuStart = battleClock, menuCursor = 0;
    using CI = cr3::CommandInput;
    if (commandInput.menu() == CI::Menu::First) setBattleCamera(battle.camera);
    else if (commandInput.menu() == CI::Menu::Command || commandInput.menu() == CI::Menu::Awake || commandInput.menu() == CI::Menu::Advent ||
             commandInput.menu() == CI::Menu::Sopia || commandInput.menu() == CI::Menu::Items)
        setBattleCamera(fighterUnit[1] < 0 ? 0x1c : commandInput.pairTurn() ? 0x1d : commandInput.actor() == 0 ? 0x1a : 0x1b);
    // (the sopia's and the items' lists the first time - flags 61, 62: their tutorials first)
    if (commandInput.menu() == CI::Menu::Sopia) battleTutorial("Tutorial_ComSopia", 61);
    else if (commandInput.menu() == CI::Menu::Items) battleTutorial("Tutorial_ComItem", 62);
}

// the camera a target is chosen under (BtlCommand::SetCommandTargetCamera: the fighter's
// camera, StepBattleIn_Setup's BtlWork +0x580 - a party member's GetTarget 0 / 1, 2 for the one
// alone, 3 for a pair's; a monster's by its size, 4 + 0..3, in its own frame, the human
// patterns' 8 / 9 (Hpair), 10 (Hsolo), the boss's 12, XLXL's 13 in the battle's)
void Viewer::setTargetCamera(int f) {
    if (f < 0 || f >= 5) return;
    cr3::Mat34 place;
    int k;
    if (f < 2) k = !commandInput.pairTurn() ? (battleRules->fighter(1).present ? f : 2) : 3;
    else {
        const int p = battle.pattern;
        const cr3::BattleData::Monster* mo = battleData.monster(battleRules->fighter(f).id);
        k = p == 10 ? 10 : p == 11 ? (f == 2 ? 8 : 9) : p == 12 ? 12 : p == 9 ? 13 : (mo ? mo->size : 0) + 4;
        if (p != 10 && p != 11 && fighterUnit[f] >= 0) place = targetFrame(battleUnits[size_t(fighterUnit[f])]);
    }
    const char* n = cr3::battleCameraName(cr3::cameraTarget(k));
    if (!n || f == targetCameraFighter) return;
    // (another target on the same side: the camera moves there - SetMovePosition /
    // SetMoveRotation over 0.5 s; the other side's, or the first: a cut, StartCtrlCamera)
    cr3::Mat34 from;
    float fov, zn, zf;
    const bool move = targetCameraFighter >= 0 && (targetCameraFighter < 2) == (f < 2) && battleView(from, fov, zn, zf);
    setBattleCamera(std::string(n), place, true);
    targetCameraFighter = f;
    if (move) battleCameraFrom = from, battleCameraMoveStart = battleClock;
}

// a monster's target camera's frame (StepBattleIn_Setup: BtlModel::GetPosition / GetRotation
// ("DummyCP") - the joint's world place, its turn without scale - on the pose then, its stance's
// first frame at its battledpos; its feet if it has none)
const cr3::Mat34& Viewer::targetFrame(BattleUnit& bu) {
    if (bu.targetFrameSet) return bu.targetFrame;
    bu.targetFrameSet = true;
    cr3::Mat34& place = bu.targetFrame;
    place = bu.home;
    const int cj = bu.fig.skeleton.joint(cr3::nameHash("DummyCP"));
    if (cj >= 0) {
        std::vector<cr3::JointTransform> pose = bu.fig.skeleton.basePose;
        std::vector<uint8_t> d;
        cr3::Animation a;
        if (archive.read(bu.figure + "bb01.anim", d) && a.parse(d) && a.joints() == int(pose.size())) pose = a.poseAbsolute(pose, 0);
        const std::vector<cr3::Mat34> w = bu.fig.skeleton.world(pose, bu.home);
        if (size_t(cj) < w.size()) place = w[size_t(cj)];
    }
    for (int c = 0; c < 3; ++c) {
        const float l = std::sqrt(place.m[c] * place.m[c] + place.m[4 + c] * place.m[4 + c] + place.m[8 + c] * place.m[8 + c]);
        if (l > 0) place.m[c] /= l, place.m[4 + c] /= l, place.m[8 + c] /= l;
    }
    return place;
}

// a token of --input as a choice
void Viewer::inputToken(const std::string& t) {
    using CI = cr3::CommandInput;
    static const std::pair<const char*, int> kNames[] = {{"solo", CI::Solo}, {"pair", CI::Pair}, {"escape", CI::Escape},
                                                         {"attack", CI::Attack}, {"defend", CI::Defend}, {"sopia", CI::Sopia},
                                                         {"items", CI::Items}};
    if (t == "options") {
        choose(CI::Options);
        SDL_Log("input options");
        return;
    }
    bool ok = false;
    static const std::pair<const char*, SDL_Keycode> kKeys[] = {{"up", SDLK_UP}, {"down", SDLK_DOWN}, {"left", SDLK_LEFT},
                                                                {"right", SDLK_RIGHT}, {"enter", SDLK_RETURN}};
    for (const auto& [n, key] : kKeys)
        if (t == n) {
            battleKey(key);
            SDL_Log("input %s", t.c_str());
            return;
        }
    // (the options' own: the Title Screen button, its question's answers, the language's and the
    // quality's arrows)
    static const std::pair<const char*, int> kOptionTaps[] = {{"title", 3101}, {"manual", 3102}, {"quality-", 3103}, {"quality+", 3104},
                                                              {"language-", 3105}, {"language+", 3106}, {"yes", 3201}, {"no", 3202},
                                                              {"graphics", 3110}};
    for (const auto& [n, id] : kOptionTaps)
        if (t == n && (optionsOpen || optionsAsk)) {
            optionTap(id);
            SDL_Log("input %s", t.c_str());
            return;
        }
    if (t == "back" && settingsOpen) closeSettings(), ok = true;
    else if (t == "back" && manualStage == 1) closeManual(), ok = true;
    else if (t == "back" && optionsOpen) optionsOpen = false, ok = true;
    else if (t == "back") commandInput.back(), audio.playSe(0xb5, false), ok = true;
    else if (t.size() > 1 && t[0] == 'a' && std::isdigit(static_cast<unsigned char>(t[1]))) ok = commandInput.pickAwakening(std::atoi(t.c_str() + 1));
    else if (t.size() > 1 && t[0] == 's' && std::isdigit(static_cast<unsigned char>(t[1]))) ok = commandInput.pickSopia(std::atoi(t.c_str() + 1));
    else if (t.size() > 1 && t[0] == 'i' && std::isdigit(static_cast<unsigned char>(t[1]))) ok = commandInput.pickItem(std::atoi(t.c_str() + 1));
    else if (t.size() > 1 && t[0] == 'v' && std::isdigit(static_cast<unsigned char>(t[1]))) ok = commandInput.pickAdvent(std::atoi(t.c_str() + 1));
    else if (t == "awake") ok = commandInput.press(cr3::CommandInput::Awake);
    else if (t == "advent") ok = commandInput.press(cr3::CommandInput::Advent);
    else if (!t.empty() && std::isdigit(static_cast<unsigned char>(t[0]))) ok = commandInput.pick(std::atoi(t.c_str()));
    else
        for (const auto& [n, id] : kNames)
            if (t == n) {
                if (commandInput.menu() == CI::Menu::Command && id == CI::Defend) battleTutorial("Tutorial_ComDefende", 63);
                ok = commandInput.press(id);
            }
    SDL_Log("input %s%s", t.c_str(), ok ? "" : " (not taken)");
    if (!ok && commandInput.menu() == cr3::CommandInput::Menu::Sopia) // (what it offers)
        for (const auto& sc : commandInput.sopiaCommands()) SDL_Log("  sopia %d: MP %d%s", sc.command, sc.mp, sc.selectable ? "" : " (not selectable)");
    menuChanged();
}

// the menu's choices in order (the buttons selectable, or the targets)
std::vector<int> Viewer::menuChoices() const {
    std::vector<int> out;
    if (commandInput.menu() == cr3::CommandInput::Menu::Target) return commandInput.targets();
    if (commandInput.menu() == cr3::CommandInput::Menu::Awake) {
        for (const auto& a : commandInput.awakenings())
            if (a.selectable) out.push_back(a.command);
        return out;
    }
    if (commandInput.menu() == cr3::CommandInput::Menu::Advent) {
        for (const auto& a : commandInput.advents())
            if (a.selectable) out.push_back(a.command);
        return out;
    }
    if (commandInput.menu() == cr3::CommandInput::Menu::Sopia) {
        for (const auto& sc : commandInput.sopiaCommands())
            if (sc.selectable) out.push_back(sc.command);
        return out;
    }
    if (commandInput.menu() == cr3::CommandInput::Menu::Items) {
        for (const auto& e : commandInput.items())
            if (e.selectable) out.push_back(e.item);
        return out;
    }
    for (const auto& b : commandInput.buttons())
        if (b.selectable) out.push_back(b.id);
    return out;
}

void Viewer::choose(int idOrFighter) {
    audio.playSe(0xb4, false); // (the buttons' decide: Cr3UtilSoundPlaySE 0xb4)
    if (commandInput.menu() == cr3::CommandInput::Menu::Target) commandInput.pick(idOrFighter);
    else if (commandInput.menu() == cr3::CommandInput::Menu::Awake) commandInput.pickAwakening(idOrFighter);
    else if (commandInput.menu() == cr3::CommandInput::Menu::Advent) commandInput.pickAdvent(idOrFighter);
    else if (commandInput.menu() == cr3::CommandInput::Menu::Sopia) commandInput.pickSopia(idOrFighter);
    else if (commandInput.menu() == cr3::CommandInput::Menu::Items) commandInput.pickItem(idOrFighter);
    else if (commandInput.menu() == cr3::CommandInput::Menu::First && idOrFighter == cr3::CommandInput::Options) {
        optionsOpen = true, optionsStart = uiClock; // (BtlCommand::StepOption: the window, the HUD gone)
        // (OptionMenu::PartsUpdate: the first time, title.nut's Tutorial_Option - flag 84)
        if (!game.flag(84)) optionsOpen = false, playTutorial("Tutorial_Option", 0, false);
        return;
    } else {
        // (StepCommandLoop: Defend the first time - flag 63 - its tutorial first, StepTutoEventInit)
        if (commandInput.menu() == cr3::CommandInput::Menu::Command && idOrFighter == cr3::CommandInput::Defend)
            battleTutorial("Tutorial_ComDefende", 63);
        commandInput.press(idOrFighter);
    }
    menuChanged();
}

// a first-time tutorial of battle_tuto's (BattleScript::tutorial) - in an encounter without a
// script of its own, its story flag still off (BtlWork +0x18, which would turn them off, never
// set here); the battle's steps wait for it as for any of its script's calls
// The battles' sounds (BtlSound): an effect by its number (PlaySE: soundtable's clip), a
// fighter's voice by its kind (PlayVoiceFromData -> Cr3VoiceTable: its figure's four
// characters) - one voice at a time, queued for when it's due (CreatePlayVoiceJob)
void Viewer::battleSe(int n) {
    const int clip = cr3::SoundData::battleSe(n);
    if (clip >= 0) audio.playSe(clip, false);
}

// (`kind`: the data's - one of its three table kinds at random; `tableKind`: Cr3VoiceTable's
// own, as a command's +0x68)
void Viewer::battleVoice(int f, int kind, float delay, bool tableKind) {
    const int u = f >= 0 && f < 5 ? fighterUnit[f] : -1;
    if (u < 0) return;
    if (!tableKind) {
        const std::array<int, 3>* choices = battleData.voice(kind);
        if (!choices) return;
        kind = (*choices)[size_t(int(battleClock * 997.0f) % 3)];
    }
    const int clip = cr3::SoundData::voiceClip(battleUnits[size_t(u)].figure, kind);
    if (clip >= 0) voiceJobs.push_back({battleClock + delay, clip});
}

// Stop both playback and delayed requests owned by a skipped/abandoned scene.
// Apply remaining combat hits after this, so their new cues keep their normal timing.
void Viewer::cancelSceneAudio() {
    voiceJobs.clear();
    seJobs.clear();
    audio.stopAllVoices(0);
    audio.stopAllSe(0);
}

void Viewer::updateBattleSounds() {
    for (size_t k = 0; k < seJobs.size();)
        if (battleClock >= seJobs[k].at) {
            audio.playSe(seJobs[k].clip, false);
            seJobs.erase(seJobs.begin() + std::ptrdiff_t(k));
        } else ++k;
    for (size_t k = 0; k < voiceJobs.size();)
        if (battleClock >= voiceJobs[k].at) {
            audio.stopAllVoices(0);
            audio.playVoice(voiceJobs[k].clip);
            voiceJobs.erase(voiceJobs.begin() + std::ptrdiff_t(k));
        } else ++k;
    if (pendingBgm > 0 && battleClock >= pendingBgmAt) { // (BtlResult / BtlGameover: 1.1 s on)
        audio.releaseBgm();
        audio.playBgm(pendingBgm, 0);
        pendingBgm = 0;
    }
}

void Viewer::battleTutorial(const char* function, int flag) {
    if (const cr3::BattleData::Coupling* cp = battleData.coupling(battle.coupling); cp && cp->flag) return; // (+0x18)
    if (!battleScript || !battleScriptOk || battleScript->own() || battleScriptRunning() || game.flag(flag)) return;
    SDL_Log("tutorial: %s", function);
    if (!(battleScriptOk = battleScript->tutorial(function))) SDL_Log("battle script: %s", battleScript->error().c_str());
}

void Viewer::battleKey(SDL_Keycode k) {
    if (gameOver.stage == 8) { // (the question: Y / Return yes, N / Backspace no)
        if (k == SDLK_Y || k == SDLK_RETURN || k == SDLK_KP_ENTER) gameOverAnswer(true);
        else if (k == SDLK_N || k == SDLK_BACKSPACE) gameOverAnswer(false);
        return;
    }
    if (!choosing) return;
    keysUsed = true;
    const std::vector<int> c = menuChoices();
    const bool target = commandInput.menu() == cr3::CommandInput::Menu::Target;
    // (the awakenings' list goes up and down)
    if (optionsKey(k)) return;
    if (k == SDLK_BACKSPACE) commandInput.back(), audio.playSe(0xb5, false), menuChanged(); // (InterfaceMain: 0xb5 back)
    else if (c.empty()) return;
    else if (k == (target ? SDLK_RIGHT : SDLK_DOWN)) menuCursor = (menuCursor + 1) % int(c.size());
    else if (k == (target ? SDLK_LEFT : SDLK_UP)) menuCursor = (menuCursor + int(c.size()) - 1) % int(c.size());
    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) choose(c[size_t(std::clamp(menuCursor, 0, int(c.size()) - 1))]);
}

// a click at the screen's (fx, fy) - 0..1 across, down: a button's tap area (UI units)
void Viewer::battleClick(float fx, float fy) {
    if (gameOver.stage == 8) { // (the question's Yes / No)
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        for (const TapRect& r : tapRects)
            if ((r.id == 7701 || r.id == 7702) && x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) &&
                y <= std::max(r.y0, r.y1))
                return gameOverAnswer(r.id == 7701);
        return;
    }
    if (!choosing) return;
    if (manualStage == 2) { // (a tap: the tutorial's page)
        if (manualEvents) manualEvents->advance();
        return;
    }
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    for (const TapRect& r : tapRects)
        if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
            if (optionsTapped(r, x)) return;
            if (r.id == 100) commandInput.back(), audio.playSe(0xb5, false), menuChanged();
            else if (r.id >= 1000) { // (another row: the focus to it - ScrollListMenu)
                const std::vector<int> c = menuChoices();
                const size_t row = size_t(r.id - 1000);
                if (row < listRows.size()) {
                    const auto it = std::find(c.begin(), c.end(), listRows[row].id);
                    if (it != c.end()) menuCursor = int(it - c.begin());
                }
            } else if (r.id >= 0) choose(r.id);
            return;
        }
}

// one of the game's updates for the script (20 a second), and the exit's timer
void Viewer::updateBattleScript(float dt) {
    if (!battleRules) return;
    battleRules->runExitTimer(dt);
    if (!battleScript || !battleScriptOk) return;
    battleScript->events().language = optionLanguage; // (the options' language)
    battleScriptClock = std::min(battleScriptClock + dt, 0.25f);
    for (; battleScriptClock >= cr3::EventRuntime::kTick; battleScriptClock -= cr3::EventRuntime::kTick)
        if (!(battleScriptOk = battleScript->tick())) SDL_Log("battle script: %s", battleScript->error().c_str());
}

bool Viewer::battleScriptRunning() const { return battleScript && battleScriptOk && battleScript->running(); }

void Viewer::startFight(int mode, uint32_t seed) {
    battleFight = mode;
    glassRng.seed(seed); // (the game's lrand48: seeded by the clock)
    battleRules = std::make_unique<cr3::BattleRules>(battleData, seed);
    std::string err;
    int party[2] = {0, 0};
    for (size_t k = 0; k < battle.party.size() && k < 2; ++k) party[k] = battle.party[k].id;
    if (!battleRules->start(battle.coupling, party, 0, &err, game.sopiaLevel, game.equip, game.level, game.mp, game.seed, game.itemCount)) {
        SDL_Log("fight: %s", err.c_str());
        battleRules.reset();
        return;
    }
    // its script: battle.nut's Battle_Init now (BtlMainCtrl::StepBattleIn), the encounter's own after
    const int chars[2] = {party[0] - 1, party[1] - 1};
    const int monsters[3] = {battleRules->fighter(2).id, battleRules->fighter(3).id, battleRules->fighter(4).id};
    battleScript = std::make_unique<cr3::BattleScript>(archive, game, *battleRules, battle.coupling, chars, monsters);
    battleScript->events().askYesNo = true;
    battleScriptClock = 0;
    battleScriptOk = battleScript->begin() && battleScript->runToEnd(100);
    if (!battleScriptOk) SDL_Log("battle script: %s", battleScript->error().c_str());
    else SDL_Log("battle script: %s%s", battleScript->name().c_str(), battleScript->own() ? " (its own)" : "");
    for (int id = 1; id < 300; ++id) battleRules->setItemCount(id, game.itemCount[id]); // (BtlWork +0x52)
    // the music (BtlMainCtrl::StepBattleIn: the encounter's +0 - but with story flag 0x7f7, the
    // script's own)
    voiceJobs.clear(), seJobs.clear(), pendingBgm = 0;
    if (const cr3::BattleData::Coupling* cp = battleData.coupling(battle.coupling); cp && !game.flag(0x7f7)) audio.playBgm(cp->music, 0);
    for (const auto& [f, id] : stateArg) // (--state: one use of it, tests)
        if (f >= 0 && f < 5) battleRules->fighter(f).states.push_back({id, 0, 1});
    if (chargeArg >= 0) battleRules->setCharge(chargeArg); // (--charge, tests)
    if (gaugeArg != 0) battleRules->setGauge(gaugeArg); // (--gauge, tests)
    battleRules->setSopiaOwned(game.sopiaOwned); // (the drops: a sopia not had yet)
    battleRules->script = [this](const std::string& f) {
        if (battleScript && battleScriptOk && battleScript->own() && !(battleScriptOk = battleScript->call(f)))
            SDL_Log("battle script: %s", battleScript->error().c_str());
    };
    battleStep = BattleStep::Opening;
    int u = 0;
    for (int k = 0; k < 2; ++k) fighterUnit[k] = party[k] > 0 ? u++ : -1;
    for (int k = 0; k < 3; ++k) fighterUnit[2 + k] = k < int(battle.monsters.size()) ? int(battle.party.size()) + k : -1;
    std::vector<uint8_t> d;
    if (archive.read("battleapos.skel", d) && cr3::parseSkeleton(d, battleApos)) battleAposWorld = battleApos.world(battleApos.basePose);
    showActions.clear(), showIndex = 0;
    // the opening: Cam_Start_<pattern>_00 (BtlMainCtrl::StepBattleIn), then the turns
    setBattleCamera(battle.startCamera);
    showEnd = battleClock + 3.0f;
    SDL_Log("fight: %s", mode == 2 ? "pair" : "solo");
}

// a battledpos joint's place (BtlModel::GetPosition(dpos, name))
bool Viewer::dposPosition(const char* name, float* out) {
    static cr3::Skeleton dpos;
    static std::vector<cr3::Mat34> w;
    if (w.empty()) {
        std::vector<uint8_t> d;
        if (!archive.read("battledpos.skel", d) || !cr3::parseSkeleton(d, dpos)) return false;
        w = dpos.world(dpos.basePose);
    }
    const int j = dpos.joint(cr3::nameHash(name));
    if (j < 0 || size_t(j) >= w.size()) return false;
    for (int c = 0; c < 3; ++c) out[c] = w[size_t(j)].m[c * 4 + 3];
    return true;
}

// the party's places for its formation (BtlUtil::SetPairPosRot)
void Viewer::placeParty(bool pair) {
    cr3::Skeleton dpos;
    std::vector<uint8_t> d;
    if (!archive.read("battledpos.skel", d) || !cr3::parseSkeleton(d, dpos)) return;
    const std::vector<cr3::Mat34> w = dpos.world(dpos.basePose);
    for (int k = 0; k < 2; ++k) {
        const int u = fighterUnit[k];
        if (u < 0) continue;
        const bool alone = fighterUnit[1 - k] < 0;
        const std::string name = alone ? "dPos_pSoloC" : pair ? (k == 0 ? "dPos_pPairA" : "dPos_pPairB") : (k == 0 ? "dPos_pSoloA" : "dPos_pSoloB");
        const int j = dpos.joint(cr3::nameHash(name.c_str()));
        if (j < 0) continue;
        cr3::Mat34 m = w[size_t(j)];
        for (int c = 0; c < 3; ++c) {
            const float l = std::sqrt(m.m[c] * m.m[c] + m.m[4 + c] * m.m[4 + c] + m.m[8 + c] * m.m[8 + c]);
            if (l > 0) m.m[c] /= l, m.m[4 + c] /= l, m.m[8 + c] /= l;
        }
        BattleUnit& bu = battleUnits[size_t(u)];
        bu.home = bu.fig.place = m, bu.position = name;
    }
}

// the fight's next step once the last is over and the script's call is
void Viewer::stepBattleShow() {
    if (!battleRules || battleFight == 0) return;
    if (battleScriptRunning() || battleClock < showEnd) return;
    if (battleRules->exitTimer() >= 0) return; // (its exit coming: before a win or a loss is shown)
    if (battleStep != BattleStep::Over && (battleRules->exited() || (battleRules->outcome() && battleStep != BattleStep::ActionEnd))) {
        if (!battleRules->exited() && battleRules->outcome() == 1) {
            battleRules->script("Battle_End"); // (BtlResult)
            beginResult();
            // (its music faded over 20 updates, then the fanfare - 15 - 1.1 s on, unless the
            // encounter's +0x15 is 3; the victory voice, kind 22)
            const cr3::BattleData::Coupling* cp = battleData.coupling(battle.coupling);
            if (!cp || cp->resultKind != 3) audio.stopBgm(20), pendingBgm = 15, pendingBgmAt = battleClock + 1.1f;
            for (int k = 0; k < 2; ++k)
                if (battleRules->fighter(k).alive) {
                    battleVoice(k, 22, 1.2f);
                    break;
                }
        } else if (!battleRules->exited() && battleRules->outcome() == 2) { // (BtlGameover: 18)
            audio.stopBgm(20), pendingBgm = 18, pendingBgmAt = battleClock + 1.1f;
            beginGameOver();
        }
        for (int id = 1; id < 300; ++id) game.itemCount[id] = static_cast<short>(battleRules->itemCount(id)); // (the items used)
        for (int k = 0; k < 2; ++k) { // (the MP kept - the most with RestoreMp, skill 3, 5, standing; the HP back: Cr3RestorePlayerHP)
            const cr3::Fighter& f = battleRules->fighter(k);
            if (f.present && f.id >= 1 && f.id <= 9) game.mp[f.id - 1] = f.alive && f.has(3, 5) ? -1 : f.mp;
        }
        battleStep = BattleStep::Over;
        showIndex = showActions.size();
        setBattleCamera(battle.camera);
        return;
    }
    switch (battleStep) {
    case BattleStep::Opening: // (the opening over: StepBattleIn's Battle_Start)
        battleStep = BattleStep::TurnBegin;
        // (StepBattleIn: the start's announcement - the leader's name; 91 a party of one)
        if (battleRules->advantage() == 1) {
            const bool solo = !battleRules->fighter(1).present;
            announceQueue.push_back(formatAnnounce(announce(solo ? 91 : 1), {battleRules->fighter(0).name}));
        } else if (battleRules->advantage() == 2) {
            announceQueue.push_back(announce(2));
        }
        if (battleRules->advantage()) SDL_Log("start: %s", battleRules->advantage() == 1 ? "the party first" : "the monsters' assault");
        battleRules->script("Battle_Start");
        break;
    case BattleStep::TurnBegin:
        // (StepBattleIn: the start's announcement up its time before the first turn)
        if (battleRules->turn() == 0 && battleRules->advantage() && (!announceQueue.empty() || battleClock - announceAt < 1.5f)) break;
        battleRules->beginTurn();
        battleStep = BattleStep::TurnChoose;
        // (BtlCommand::StepInit: an armoured monster standing - its +0x48 bit 0 - the first time,
        // flag 54)
        for (int m = 2; m < 5; ++m) {
            const cr3::Fighter& f = battleRules->fighter(m);
            if (f.alive && f.monster && (f.monster->skills & 1u)) {
                battleTutorial("Tutorial_Armor", 54);
                break;
            }
        }
        break;
    case BattleStep::TurnChoose: { // the party attacking the first monster standing
        if (battleRules->turn() == 0 && battleRules->advantage() == 2) { // (the assault: no menus - StepBattleIn's step 2)
            battleRules->chooseTurn(false, {});
            SDL_Log("turn 1 (the monsters' assault)");
            battleStep = BattleStep::ActionStart;
            break;
        }
        if (manualCommands && !battleRules->scriptTurnReserved()) {
            if (!choosing) {
                commandInput.begin(*battleRules, battleData.coupling(battle.coupling) && battleData.coupling(battle.coupling)->flag,
                                   [this](int f) { return game.flag(f); },
                                   [this](int s) { return s >= 0 && s < 32 ? int(game.sopiaLevel[s]) : 0; });
                choosing = true;
                placeParty(false); // (the choosing in the solo places: BtlCommand's)
                menuChanged();
                inputNext = battleClock + 0.4f;
            }
            if (!inputScript.empty() && battleClock >= inputNext && manualStage != 2) { // (a tutorial over first)
                inputToken(inputScript.front());
                inputScript.pop_front();
                inputNext = battleClock + 0.4f;
            }
            updateGuards();
            if (commandInput.menu() == cr3::CommandInput::Menu::Target) { // (the focused target's camera)
                const std::vector<int> c = menuChoices();
                if (!c.empty()) setTargetCamera(c[size_t(std::clamp(menuCursor, 0, int(c.size()) - 1))]);
            }
            if (commandInput.menu() != cr3::CommandInput::Menu::Done) break;
            choosing = false;
            if (commandInput.escape()) { // (BtlCommand::StepEscapeLoop: away after 2 s, else the turn the monsters')
                const bool away = battleRules->tryEscape();
                SDL_Log("escape: %s", away ? "got away" : "failed");
                showEnd = battleClock + (away ? 2.0f : 1.0f);
                if (away) {
                    battleStep = BattleStep::Over;
                    setBattleCamera(battle.camera);
                    break;
                }
            }
            battleRules->chooseTurn(commandInput.pairTurn(), commandInput.orders());
            placeParty(battleRules->fighter(0).partner >= 0);
            SDL_Log("turn %d (%s)", battleRules->turn() + 1, commandInput.pairTurn() ? "pair" : "solo");
            battleStep = BattleStep::ActionStart;
            break;
        }
        const bool pair = battleFight == 2;
        std::vector<cr3::BattleRules::Order> orders;
        for (int k = 0; k < 2; ++k) {
            const cr3::Fighter& f = battleRules->fighter(k);
            if (!f.alive) continue;
            cr3::BattleRules::Order o;
            o.actor = k, o.command = f.attack;
            for (int m = 2; m < 5; ++m)
                if (battleRules->fighter(m).alive) {
                    o.target = m;
                    break;
                }
            orders.push_back(o);
        }
        battleRules->chooseTurn(pair, orders);
        placeParty(battleRules->fighter(0).partner >= 0);
        SDL_Log("turn %d", battleRules->turn() + 1);
        battleStep = BattleStep::ActionStart;
        break;
    }
    case BattleStep::ActionStart:
        if (!battleRules->actionsLeft()) {
            const int breakBefore = battleRules->breakState();
            battleRules->endTurn();
            if (breakBefore == 3 && (battleRules->breakState() == 1 || battleRules->breakState() == 2)) {
                battleSe(2);
                seJobs.push_back({battleClock + 1.0f, cr3::SoundData::battleSe(battleRules->breakState() == 1 ? 3 : 4)});
                breakShown = battleRules->breakState(), breakShownAt = battleClock;
                SDL_Log("turn break: %s (%.1f s)", breakShown == 1 ? "the party's" : "the monsters'", battleClock);
            }
            for (int f = 0; f < 2; ++f) setGuard(f, false), dropGuardObject(f); // (BtlTurnEnd: DeleteAll; the stance back)
            if (announceQueue.empty()) commandShown.clear(); // (BtlTurnEnd: ClearAnnounce)
            for (const cr3::BattleRules::Event& e : battleRules->turnEndEvents()) { // (BtlTurnEnd: the poison's, the pools')
                if (e.damage > 0) showNumber(e.target, e.damage, 20);
                if (e.heal > 0) showNumber(e.target, e.heal, 50);
                if (e.healMp > 0) showNumber(e.target, e.healMp, 60);
                SDL_Log("    %s: %d poison, %d HP back, %d MP back", battleRules->fighter(e.target).name.c_str(), e.damage, e.heal, e.healMp);
            }
            if (!battleRules->turnEndEvents().empty()) showEnd = battleClock + 1.2f;
            SDL_Log("turn over; gauge %d, charge %d; MP %d %d", battleRules->gauge(), battleRules->charge(), battleRules->fighter(0).mp, battleRules->fighter(1).mp);
            battleStep = BattleStep::TurnBegin;
            break;
        }
        battleRules->startAction();
        battleStep = BattleStep::ActionDo;
        break;
    case BattleStep::ActionDo: {
        hudBefore.hp.clear();
        for (int k = 0; k < 5; ++k) hudBefore.hp.push_back(battleRules->fighter(k).hp);
        hudBefore.gauge = battleRules->gauge(), hudBefore.charge = battleRules->charge();
        std::vector<cr3::BattleRules::Event> events;
        battleRules->doAction(events);
        showActions.clear(), showIndex = 0;
        for (const auto& e : events) {
            if (showActions.empty()) showActions.push_back({e.actor, e.partner, e.command, {}});
            showActions.back().hits.push_back(e);
            if (e.regenerated) regenPending.push_back(e.target);
        }
        battleStep = BattleStep::ActionEnd;
        if (!showActions.empty()) beginShowAction(showActions[0]);
        break;
    }
    case BattleStep::ActionEnd:
        showIndex = showActions.size();
        for (int f = 0; f < 2; ++f) // (back in its stance: ShowAttachObject - made again)
            if (guardReturn[f] && guardObjects[f] && battleRules->fighter(f).alive) guardObject(f, 0.5f), guardReturn[f] = false;
        if (stepAnalyze()) break;
        if (stepDeaths()) break;
        if (!regenPending.empty() || battleScriptRunning()) { // (a revival, a tutorial first)
        } else if (!showActions.empty()) {
            // (ActionCommonEventCommandEnd: a command lending an element - +0x70 / +0x71 1..3 -
            // the first time, flag 53 (CheckTutoScript_Element); else one standing with a state
            // shown, flag 55 (CheckTutoScript_Status))
            const cr3::BattleData::Command* c = battleData.command(showActions.front().command);
            const bool element = c && ((c->attach >= 1 && c->attach <= 3) || (c->attachSelf >= 1 && c->attachSelf <= 3));
            bool status = false;
            static const int kShown[] = {0, 1, 2, 12, 35, 30, 29, 33, 5, 6, 7, 8, 9, 13, 14, 15, 16, 17, 21, 11, 32};
            for (int f = 0; f < 5 && !status; ++f)
                if (battleRules->fighter(f).alive)
                    for (int st : kShown) status = status || battleRules->fighter(f).hasState(st);
            if (element && !game.flag(53)) battleTutorial("Tutorial_Element", 53);
            else if (status) battleTutorial("Tutorial_Status", 55);
            if (battleScriptRunning()) break;
        }
        if (!regenPending.empty()) { // (the downed with state 33 back first)
            showRegen();
            break;
        }
        battleRules->endAction();
        analyze.stage = 0, deathsQueued = false;
        battleStep = BattleStep::ActionStart;
        break;
    case BattleStep::Over: break;
    }
}

bool Viewer::hudHeld() const { return showIndex < showActions.size() && !showHitDone && hudBefore.hp.size() == 5; }

int Viewer::shownHp(int f) const {
    if (std::find(regenPending.begin(), regenPending.end(), f) != regenPending.end()) return 0; // (down until its revival shows)
    return hudHeld() ? hudBefore.hp[size_t(f)] : battleRules->fighter(f).hp;
}

void Viewer::showRegen() {
    const int f = regenPending.front();
    const int u = fighterUnit[f];
    if (!regenStarted) {
        regenStarted = true;
        announceQueue.clear();
        commandShown = announce(0x5d), commandShownStart = announceAt = battleClock;
        SDL_Log("    announce: %s", commandShown.c_str());
        const char* cam = "Cam_Unit_PCrevival_00";
        bool found = false;
        for (const CameraFigure& c : battleCameras)
            for (const cr3::GeomCamera& gc : c.geom.cameras) found = found || gc.hash == cr3::nameHash(cam);
        if (found && u >= 0) {
            cr3::Mat34 at; // (SetPosition: the camera's frame moved there, not turned)
            for (int r = 0; r < 3; ++r) at.m[r * 4 + 3] = battleUnits[size_t(u)].home.m[r * 4 + 3];
            setBattleCamera(cam, at);
        }
        showEnd = battleClock + 0.2f; // (the camera's cut drawn before the number is placed by it)
        return;
    }
    regenStarted = false;
    regenPending.pop_front();
    if (u < 0) return;
    BattleUnit& bu = battleUnits[size_t(u)];
    bu.hidden = false;
    playMotion(bu, "bh01", battleClock);
    showNumber(f, battleRules->fighter(f).hp, 50);
    float at[3] = {bu.home.m[3], bu.home.m[7] + 0.15f, bu.home.m[11]};
    const int dj = bu.fig.skeleton.joint(cr3::nameHash("DummyDP01"));
    if (dj >= 0 && !bu.fig.posed.empty()) {
        const std::vector<cr3::Mat34> w = bu.fig.skeleton.world(bu.fig.posed, bu.fig.place);
        if (size_t(dj) < w.size()) at[0] = w[size_t(dj)].m[3], at[1] = w[size_t(dj)].m[7], at[2] = w[size_t(dj)].m[11];
    }
    spawnEffect("eff216", at);
    SDL_Log("    %s revived: %d HP", battleRules->fighter(f).name.c_str(), battleRules->fighter(f).hp);
    showEnd = battleClock + std::max(motionLength(bu, "bh01"), 1.0f) + 0.3f;
}

int Viewer::shownGauge() const { return hudHeld() ? hudBefore.gauge : battleRules->gauge(); }

int Viewer::shownCharge() const { return hudHeld() ? hudBefore.charge : battleRules->charge(); }

std::string Viewer::announce(int id) { return messageText("announcebattle", id); }

std::string Viewer::messageText(const std::string& file, int id) {
    std::vector<uint8_t>& d = messageFiles[file];
    if (d.empty() && !archive.read("message\\" + file + ".dat", d)) return {};
    auto u32 = [&](size_t o) { return o + 4 <= d.size() ? uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24) : 0u; };
    if (id < 1 || uint32_t(id) > u32(8)) return {};
    // (4 bytes a language - CR3's copies have 2, Japanese and English; read from the records' size)
    const uint32_t records = u32(0), strings = u32(4), n = u32(8);
    size_t langs = n && strings > records && (strings - records) % n == 0 ? (strings - records) / n / 4 : 8;
    if (langs < 1 || langs > 8) langs = 8;
    const size_t want = size_t(std::clamp(optionLanguage, 0, 7)), lang = want < langs ? want : (langs > 1 ? 1 : 0); // (else English)
    const size_t o = size_t(strings) + u32(size_t(records) + size_t(id - 1) * 4 * langs + 4u * lang);
    std::string s;
    for (size_t k = o; k < d.size() && d[k]; ++k) s += char(d[k]);
    return s;
}

// BtlAnnounce::SetAnnounce's %s / %d, in turn
std::string Viewer::formatAnnounce(std::string t, const std::vector<std::string>& args) {
    size_t a = 0;
    for (size_t p = 0; (p = t.find('%', p)) != std::string::npos;) {
        if (p + 1 < t.size() && (t[p + 1] == 's' || t[p + 1] == 'd')) {
            const std::string v = a < args.size() ? args[a++] : "";
            t.replace(p, 2, v);
            p += v.size();
        } else ++p;
    }
    return t;
}

// ProcessLvup1 / 2, ProcessResult: the result's messages - and the party's state after it: the
// levels and EXP, the sopia's SP and levels, the Oz, the items and sopia left
void Viewer::finishResult() {
    std::vector<std::string>& out = result.messages;
    for (const ResultMember& r : result.member) {
        if (r.ch < 0 || r.ch >= 9) continue;
        const ResultNow n = resultNow(r);
        const int up = n.level - r.level;
        const std::string name = messageText("charaname", r.id);
        if (up == 1) out.push_back(formatAnnounce(announce(0x11), {name}));
        else if (up > 1) out.push_back(formatAnnounce(announce(0x12), {name, std::to_string(up)}));
        game.level[r.ch] = n.level, game.exp[r.ch] = n.exp;
    }
    // CheckSpUp: each sopia the party carries (once), its SP, a level each time it passes its
    // table's next (5 at most) - each level's command (or auto skill) learned
    if (result.sp > 0) {
        std::vector<int> seen;
        for (const ResultMember& r : result.member)
            for (int k = 3; r.ch >= 0 && r.ch < 9 && k < 8; ++k) {
                const int s = game.equip[r.ch][k];
                if (s <= 0 || s >= 32 || std::find(seen.begin(), seen.end(), s) != seen.end()) continue;
                seen.push_back(s);
                const cr3::BattleData::Sopia* so = battleData.sopia(s);
                if (!so) continue;
                const int from = std::max(int(game.sopiaLevel[s]), 1);
                int level = from;
                game.sopiaExp[s] = std::min(game.sopiaExp[s] + result.sp, 99999999);
                while (level < 5 && game.sopiaExp[s] >= cr3::sopiaExp(so->growth != 0, level + 1)) ++level;
                for (int l = from; l < level; ++l) {
                    const int cmd = so->commands[l];
                    out.push_back(formatAnnounce(announce(cmd > 0 ? 0x16 : 0x17),
                                                 {messageText("sopianame", s), cmd > 0 ? messageText("commandname", cmd) : messageText("autoskillname", so->skills[l])}));
                }
                game.sopiaLevel[s] = static_cast<signed char>(level);
            }
    }
    if (result.oz > 0) out.push_back(formatAnnounce(announce(0xf), {std::to_string(result.oz)}));
    game.oz = std::min(game.oz + result.oz, 99999999);
    for (int id = 1; id < 300; ++id) {
        const int n = battleRules->dropped(id);
        if (n <= 0) continue;
        out.push_back(n == 1 ? formatAnnounce(announce(0x14), {messageText("itemname", id)})
                             : formatAnnounce(announce(0x15), {messageText("itemname", id), std::to_string(n)}));
        game.itemCount[id] = static_cast<short>(std::min(game.itemCount[id] + n, 99));
    }
    for (int s = 1; s < 32; ++s)
        if (battleRules->sopiaDropped(s)) { // (Cr3UtilGetSopia: had, at level 1)
            out.push_back(formatAnnounce(announce(0x13), {messageText("sopianame", s)}));
            game.sopiaOwned |= 1u << s, game.sopiaLevel[s] = 1, game.sopiaExp[s] = 0;
        }
    for (const std::string& s : out) SDL_Log("result: %s", s.c_str());
}

void Viewer::deathEffect(const char* name, int f, BattleUnit& u) {
    commandEffect(name, f, false);
    if (commandEffects.empty()) return;
    SceneEffect& fx = commandEffects.back();
    fx.follows = -1;
    fx.place = u.home;
    unscaled(fx.place);
    fx.place.m[7] += cr3::deadOffset(u.figure);
}

bool Viewer::stepDeaths() {
    if (!deathsQueued) {
        deathsQueued = true;
        deaths.clear();
        for (const ShowAction& a : showActions)
            for (const auto& e : a.hits)
                if (e.defeated && !e.regenerated && std::none_of(deaths.begin(), deaths.end(), [&](const Death& d) { return d.f == e.target; }))
                    deaths.push_back({e.target});
    }
    bool wait = false;
    for (Death& d : deaths) {
        BattleUnit* u = unitOf(d.f);
        const cr3::BattleData::Monster* m = d.f >= 2 ? battleRules->fighter(d.f).monster : nullptr;
        if (!u || d.stage >= 9) continue;
        if (d.stage == 0) {
            d.start = battleClock;
            if (d.f < 2) dropGuardObject(d.f), u->guarding = false; // (ActionDeadStart: its guard gone)
            if (d.f < 2 || (m && m->deathMotion)) {
                playMotion(*u, "bd02", battleClock), u->hold = true;
                d.stage = d.f < 2 ? 3 : 1;
                if (m) { // (the defeat camera: by its size, the size pattern)
                    const char* cam = m->size == 2 ? "Cam_defeat_L_00" : battle.pattern == 12 ? "Cam_defeat_neron_00"
                                    : m->size == 3 ? "Cam_defeat_XL_00" : battle.pattern == 11 ? "Cam_defeat_Hpair_00"
                                    : m->size == 4 ? "Cam_defeat_Hsolo_00" : nullptr;
                    if (cam) {
                        setBattleCamera(cam);
                        if (m->size == 4 && battle.pattern != 11)
                            for (int c = 0; c < 3; ++c) battleCameraPlace.m[c * 4 + 3] = u->home.m[c * 4 + 3];
                    }
                }
            } else {
                if (!m || !m->deathPlain) {
                    u->alpha.begin(battleClock, 1, 0, 0.5f), u->hideAt = battleClock + 0.5f;
                    deathEffect("eff201", d.f, *u);
                }
                battleSe(0x65);
                d.stage = 9;
                continue;
            }
        }
        const float len = motionLength(*u, "bd02");
        if (d.stage == 3) { // (one of the party: its motion's end)
            if (battleClock - d.start < len) wait = true;
            else d.stage = 9;
            continue;
        }
        if (d.stage == 1) {
            if (battleClock - d.start < std::min(len, 5.0f)) {
                wait = true;
                continue;
            }
            if (!m->deathPlain) {
                deathEffect("eff202", d.f, *u);
                u->alpha.begin(battleClock + 0.25f, 1, 0, 1.0f), u->hideAt = battleClock + 1.25f;
                battleSe(0x66);
            }
            d.stage = 2, d.start = battleClock;
        }
        if (d.stage == 2) {
            if (battleClock - d.start < 0.95f) wait = true;
            else d.stage = 9;
        }
    }
    return wait;
}

bool Viewer::stepAnalyze() {
    if (showActions.empty() || analyze.stage >= 3) return false;
    const cr3::BattleData::Command* c = battleData.command(showActions.front().command);
    const int target = showActions.front().hits.empty() ? -1 : showActions.front().hits.front().target;
    if (!c || c->kind != 26 || target < 0) return false;
    const bool tapped = analyze.tapped;
    analyze.tapped = false;
    const cr3::Fighter& t = battleRules->fighter(target);
    if (analyze.stage == 0) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "HP: %d / %d", t.hp, t.maxHp);
        announceQueue.clear();
        commandShown = buf, commandShownStart = announceAt = battleClock, announceHeld = true;
        analyze.stage = 1, analyze.start = battleClock;
        SDL_Log("    analyze: %s", buf);
    } else if (tapped || battleClock - analyze.start > 5.0f) {
        if (analyze.stage == 1 && t.monster) {
            commandShown = messageText("monsterhelp", t.id), commandShownStart = announceAt = battleClock;
            analyze.stage = 2, analyze.start = battleClock;
            SDL_Log("    analyze: the help (%zu bytes)", commandShown.size());
        } else {
            analyze.stage = 3, announceHeld = false, commandShown.clear();
            return false;
        }
    }
    return true;
}

void Viewer::beginResult() {
    result = BattleResultShow();
    result.stage = 1, result.start = battleClock;
    result.exp = battleRules->expGained(), result.sp = battleRules->spGained(), result.oz = battleRules->ozGained();
    // CheckBonus: states 38 / 39 / 40 on either of the party double the EXP / SP / Oz (once,
    // both or one); ExpUp / SpUp / OzUp (skill 3, 0 / 1 / 2) of either +20 in 100
    auto either = [&](int st) { return battleRules->fighter(0).hasState(st) || battleRules->fighter(1).hasState(st); };
    if (either(38)) result.exp *= 2;
    if (either(39)) result.sp *= 2;
    if (either(40)) result.oz *= 2;
    auto bonus = [&](int bit, int& v) {
        for (int k = 0; k < 2; ++k)
            if (battleRules->fighter(k).present && battleRules->fighter(k).has(3, bit)) {
                v += v * 20 / 100;
                return;
            }
    };
    bonus(0, result.exp), bonus(1, result.sp), bonus(2, result.oz);
    for (int k = 0; k < 2; ++k) {
        const cr3::Fighter& f = battleRules->fighter(k);
        if (!f.present) continue;
        ResultMember& r = result.member[k];
        r.ch = f.id - 1, r.id = f.id;
        r.level = r.ch >= 0 && r.ch < 9 && game.level[r.ch] > 0 ? game.level[r.ch] : f.level;
        r.exp0 = r.ch >= 0 && r.ch < 9 ? game.exp[r.ch] : cr3::levelExp(r.level);
        r.gain = f.alive ? result.exp : result.exp * 50 / 100; // (CheckLvUp: the downed half)
        r.hp0 = f.alive ? f.hp : 0, r.hpMax = f.maxHp, r.mp = f.mp, r.mpMax = f.maxMp;
    }
}

auto Viewer::resultNow(const ResultMember& r) const -> ResultNow {
    const float p = result.stage >= 4 ? 1.0f : result.transferEnd > result.transferStart
                                                   ? std::clamp((battleClock - result.transferStart) / (result.transferEnd - result.transferStart), 0.0f, 1.0f)
                                                   : 0.0f;
    ResultNow n;
    n.exp = r.exp0 + int(float(r.gain) * p);
    n.level = std::max(r.level, 1);
    while (n.level < 200 && cr3::levelExp(n.level + 1) <= n.exp) ++n.level;
    const int lo = cr3::levelExp(n.level), hi = cr3::levelExp(n.level + 1);
    n.next = n.level >= 200 ? 0 : hi - n.exp;
    n.percent = n.level >= 200 || hi <= lo ? 0 : int((int64_t(n.exp) - lo) * 100 / (hi - lo));
    n.hp = r.hp0 + int(float(r.hpMax - r.hp0) * p);
    return n;
}

void Viewer::beginGameOver() {
    gameOver = GameOverState();
    gameOver.stage = 1, gameOver.start = battleClock;
    const cr3::BattleData::Coupling* cp = battleData.coupling(battle.coupling);
    gameOver.over = (cp && cp->flag) || game.flag(0x7f9);
    announceQueue.clear();
    for (SceneEffect& fx : commandEffects) unload(*fx.fig);
    commandEffects.clear();
    for (BattleEffect& fx : battleEffects) unload(*fx.fig);
    battleEffects.clear();
    for (int f = 0; f < 2; ++f) dropGuardObject(f);
    setBattleCamera("Cam_Lose_SoloPair_00");
    battleCameraLoop = true;
    const bool alone = !battleRules->fighter(1).present;
    commandShown = formatAnnounce(announce(alone ? 0x5c : 7), {battleRules->fighter(0).name});
    commandShownStart = battleClock, announceHeld = true;
    SDL_Log("game over%s", gameOver.over ? "" : " (none yet: back to the field)");
}

void Viewer::endGameOver(bool title) {
    gameOver.stage = 0, announceHeld = false, commandShown.clear();
    battleFade = BattleFade();
    if (title) {
        SDL_Log("game over: the title");
        toTitle = true;
        return;
    }
    game.setFlag(0x7f8, true); // (StepBattleOut: lost with no game over)
}

void Viewer::updateGameOver() {
    if (gameOver.stage == 0) return;
    const bool tapped = gameOver.tapped;
    gameOver.tapped = false;
    const float t = battleClock - gameOver.start;
    if (gameOver.stage == 1 && t > 1.1f) {
        bool tap = tapped;
        if (!inputScript.empty() && (inputScript.front() == "tap" || inputScript.front() == "pick")) inputScript.pop_front(), tap = true;
        if (!tap) return;
        if (!gameOver.over || (!saveExists("cont.txt") && !saveExists("save1.txt"))) return endGameOver(gameOver.over);
        gameOver.stage = 5, gameOver.start = battleClock;
        SDL_Log("game over: faded out, then the question");
        battleFade.colour = 0, battleFade.alpha.begin(battleClock, 0, 1, 1.0f); // (FadeOut 20 updates)
    } else if (gameOver.stage == 5 && t >= 1.0f) { // (HideBattleStatus, HideBattleGameOver, the party hidden; FadeIn at once)
        gameOver.hidden = true, gameOver.stage = 7, gameOver.start = battleClock;
        for (int k = 0; k < 2; ++k)
            if (BattleUnit* u = unitOf(k)) u->hidden = true;
        battleFade = BattleFade();
        announceHeld = false, commandShown.clear();
    } else if (gameOver.stage == 7 && t > 0.5f) {
        gameOver.stage = 8, gameOver.start = battleClock;
    } else if (gameOver.stage == 8 && !inputScript.empty() && (inputScript.front() == "yes" || inputScript.front() == "no") && t > 0.5f) {
        const bool yes = inputScript.front() == "yes";
        inputScript.pop_front();
        gameOverAnswer(yes);
    }
}

void Viewer::gameOverAnswer(bool yes) {
    if (gameOver.stage != 8) return;
    audio.playSe(yes ? 0xb4 : 0xb5, false);
    if (!yes) return endGameOver(true);
    gameOver = GameOverState();
    battleFade = BattleFade();
    leaveBattle();
    titleMenu.action = saveExists("cont.txt") ? 3 : 2; // (Cr3Util_Load_ContData, else Cr3Util_Load_GameData: the main loop loads it)
    SDL_Log("game over: try again (%s)", titleMenu.action == 3 ? "the continue data" : "the save");
}

void Viewer::drawGameOver(const Mat4& view, int w, int h) {
    if (gameOver.stage == 8) return drawAsk(announce(0x87), battleClock - gameOver.start, view, w, h, 7701, 7702);
    if (!gameOver.over || gameOver.hidden) return;
    UiFig& d = uiFig("dead"); // (BattleGameOverMenu: `dead` on its animation)
    d.fig.place = cr3::Mat34();
    poseUi(d, "dead", battleClock - gameOver.start);
    drawFigure(d.fig, view.data());
}

void Viewer::updateResult() {
    if (result.stage == 0 || result.stage >= 4) return;
    const bool tapped = result.tapped;
    result.tapped = false;
    const float t = battleClock - result.start;
    if (result.stage == 1 && t >= 1.2f) { // (SetVictoryMotion / SetVictoryCamera)
        result.stage = 2, result.start = battleClock;
        SDL_Log("result: victory at %.1f s", battleClock);
        commandShown = announce(6), commandShownStart = battleClock, announceHeld = true;
        for (int k = 0; k < 2; ++k)
            if (battleRules->fighter(k).alive && fighterUnit[k] >= 0) playMotion(battleUnits[size_t(fighterUnit[k])], "bb03", battleClock, "bb03");
        const bool both = battleRules->fighter(0).alive && battleRules->fighter(1).alive;
        setBattleCamera(fighterUnit[1] < 0 ? "Cam_Win_PCsolo_00"
                        : battleRules->fighter(0).partner >= 0 && both ? "Cam_Win_PCpair_00"
                        : battleRules->fighter(0).alive ? "Cam_Win_PCfighter_00" : "Cam_Win_PCpartner_00");
    } else if (result.stage == 2 && (tapped || t > 5.0f)) { // (ShowResult: the HUD gone, the window)
        result.stage = 3, result.start = result.windowStart = battleClock;
        SDL_Log("result: the window at %.1f s", battleClock);
        announceHeld = false, commandShown.clear();
        UiFig& seat = uiFig("seatrslt_io");
        poseUi(seat, "seatrslt_io", 0);
        result.transferStart = battleClock + (seat.fig.animLoaded ? seat.fig.anim.duration() : 0.5f);
        result.transferEnd = result.transferStart + kTransfer;
    } else if (result.stage == 3) {
        if (tapped && battleClock < result.transferEnd) result.transferStart = result.transferEnd = battleClock; // (all at once)
        else if (battleClock >= result.transferEnd && result.message < 0 && (tapped || battleClock - result.transferEnd > 5.0f)) {
            finishResult();
            result.message = 0, result.messageAt = battleClock;
        } else if (result.message >= 0 && (tapped || battleClock - result.messageAt > 3.0f)) {
            ++result.message, result.messageAt = battleClock;
        }
        if (result.message >= int(result.messages.size()) && result.message >= 0) {
            result.stage = 4;
            SDL_Log("result: EXP %d, SP %d, Oz %d", result.exp, result.sp, result.oz);
        }
    }
}

// a number at a fighter's DummyDP01 (its bind pose's), as the hits' (DamageInfo00)
void Viewer::showNumber(int f, int value, int set) {
    const int u = f >= 0 && f < 5 ? fighterUnit[f] : -1;
    if (u < 0) return;
    const BattleUnit& bu = battleUnits[size_t(u)];
    float p[3] = {bu.fig.place.m[3], bu.fig.place.m[7] + 0.15f, bu.fig.place.m[11]};
    const int dj = bu.fig.skeleton.joint(cr3::nameHash("DummyDP01"));
    if (dj >= 0) {
        const std::vector<cr3::Mat34> ww = bu.fig.skeleton.world(bu.fig.skeleton.basePose, bu.fig.place);
        if (size_t(dj) < ww.size()) p[0] = ww[size_t(dj)].m[3], p[1] = ww[size_t(dj)].m[7], p[2] = ww[size_t(dj)].m[11];
    }
    DamageShown ds;
    ds.value = value, ds.set = set, ds.start = battleClock;
    for (int c = 0; c < 3; ++c) ds.at[c] = p[c];
    damageShown.push_back(ds);
}

void Viewer::beginShowAction(const ShowAction& a) {
    showStart = battleClock, showHitDone = false;
    const cr3::BattleData::Command* c = battleData.command(a.command);
    if (c && c->voice > 0) battleVoice(a.actor, c->voice, float(c->voiceDelay) * 0.05f, true); // (PlayVoiceCommand: +0x68 after +0x6a)
    if (!a.hits.empty() && a.hits.front().failed) { // (ActionDontAction*: its announcement alone, a moment)
        for (Move& m : showMoves) m = Move();
        showHitAt = 0, showCamera2 = -1;
        showEnd = battleClock + 1.5f;
        if (c) SDL_Log("  %s: %s - not done", battleRules->fighter(a.actor).name.c_str(), c->name[1].c_str());
        return;
    }
    if (a.actor >= 0 && a.actor < 2 && a.command == cr3::CommandInput::kDefend && !(!a.hits.empty() && a.hits.front().counter)) {
        for (Move& m : showMoves) m = Move();
        showHitAt = 0, showCamera2 = -1;
        showEnd = battleClock; // (action_table_empty: no announcement, no motion - the stance since the choice)
        SDL_Log("  %s: Defend (its stance)", battleRules->fighter(a.actor).name.c_str());
        return;
    }
    for (int f : {a.actor, a.partner}) // (SetAttackMotion: HideAttachObject)
        if (f >= 0 && f < 2 && guardObjects[f]) guardObjects[f]->shown = false, guardReturn[f] = true;
    if (c && startScene(a, *c)) { // (an awakening's, an advent's, a monster's special: its scene)
        SDL_Log("  %s: %s", battleRules->fighter(a.actor).name.c_str(), c->name[1].c_str());
        return;
    }
    std::string motion = c ? c->motion : "bb01";
    if (a.partner >= 0 && a.actor < 2) motion = "bp01"; // (the pair's command: +0x46, bp01)
    float length = 0;
    for (Move& m : showMoves) m = Move();
    const int target = a.hits.empty() ? -1 : a.hits.front().target;
    int slot = 0;
    // (ActionAttackPairWaitDelayTime: the pair's second after GetPairAttackDelayTime - by the two
    // characters, the first's row, in frames of 0.05 s; the shadows' pair by their numbers 205 ..)
    static const unsigned char kPairDelay[9][9] = {{0, 6, 5, 4, 3, 6, 0, 0, 0},       {8, 0, 8, 6, 4, 10, 4, 0, 0},
                                                   {10, 8, 0, 7, 6, 10, 2, 0, 0},     {13, 12, 13, 0, 7, 14, 7, 0, 0},
                                                   {12, 11, 11, 11, 0, 13, 7, 0, 0},  {11, 10, 10, 8, 7, 0, 5, 0, 0},
                                                   {23, 20, 20, 20, 15, 21, 0, 0, 0}, {24, 24, 24, 24, 0, 24, 24, 0, 0},
                                                   {24, 24, 24, 24, 0, 24, 24, 0, 0}};
    auto pairDelay = [&](int first, int second) {
        const cr3::Fighter &x = battleRules->fighter(first), &y = battleRules->fighter(second);
        int i = x.id - 1, j = y.id - 1; // (cr3_common_data's: from 0)
        if (first >= 2) i = x.id - 205, j = y.id - 205;
        return i >= 0 && i < 9 && j >= 0 && j < 9 ? float(kPairDelay[i][j]) / 20.0f : 0.0f;
    };
    for (int f : {a.actor, a.partner}) {
        const int u = f >= 0 ? fighterUnit[f] : -1;
        if (u < 0) continue;
        BattleUnit& bu = battleUnits[size_t(u)];
        const float delay = f == a.partner && a.partner >= 0 ? pairDelay(a.actor, a.partner) : 0.0f;
        playMotion(bu, motion, battleClock + delay);
        length = std::max(length, motionLength(bu, motion) + delay);
        cr3::MotionTimes t;
        cr3::motionTimes(bu.figure, cr3::motionIndex(motion), t);
        Move& m = showMoves[slot++];
        m.unit = u;
        if (target >= 0 && t.moveStart >= 0 && t.moveEnd >= t.moveStart && aposPlace(aposName(f, target), m.to)) {
            m.active = true;
            m.start = t.moveStart + delay, m.end = t.moveEnd + delay;
            m.back = t.move2Start + delay, m.backEnd = t.move2End + delay;
        }
        if (f == a.actor) showHitAt = t.hit[0] >= 0 ? t.hit[0] : length * 0.5f;
    }
    showEnd = battleClock + std::max(length, 0.5f) + 0.3f;
    // its effects as it begins (ActionAttackWaitAttackMotion / ActionSopia / ItemStandby): a
    // shot (CreateShotEffect: the command's +0x40 3 - its plain attack's when it has no motion
    // of its own - +0x28 from the motion's effect place to the one hit, there at the hit, and
    // +0x30 on that one), then its launch effect (+0x20) on the one acting
    if (c && target >= 0) {
        const cr3::BattleData::Command* shooter = c;
        if (c->motion.empty() && a.actor >= 0 && a.actor < 2) shooter = battleData.command(battleRules->fighter(a.actor).attack);
        if (shooter && shooter->shot == 3 && !c->shotEffect.empty()) {
            SceneEffect fx;
            fx.fig = std::make_unique<ViewFigure>();
            std::string err;
            const float keep = time;
            if (loadFigure(c->shotEffect + ".geom", *fx.fig, &err)) {
                const auto it = std::find(fx.fig->anims.begin(), fx.fig->anims.end(), c->shotEffect + ".anim");
                if (it != fx.fig->anims.end()) loadAnim(*fx.fig, size_t(it - fx.fig->anims.begin()));
                time = keep;
                fx.set = &cr3::effectSetting(c->shotEffect);
                fx.fig->absolute = true;
                fx.shot = true, fx.path = fx.set->shotPath, fx.start = battleClock, fx.flight = std::max(showHitAt, 0.1f);
                cr3::MotionTimes mt;
                const BattleUnit* au = unitOf(a.actor);
                const BattleUnit* tu = unitOf(target);
                if (au && tu) {
                    cr3::motionTimes(au->figure, cr3::motionIndex(motion), mt);
                    cr3::Mat34 j = au->fig.place;
                    if (mt.effectLocation != 9) jointPlace(au->fig, cr3::tposJoint(mt.effectLocation), j);
                    cr3::Mat34 k = tu->fig.place;
                    const short tl = fx.set->locator == 7 || fx.set->locator == 9 ? 6 : fx.set->locator;
                    jointPlace(tu->fig, cr3::tposJoint(tl), k);
                    for (int c3 = 0; c3 < 3; ++c3) fx.from[c3] = j.m[c3 * 4 + 3], fx.to[c3] = k.m[c3 * 4 + 3];
                    commandEffects.push_back(std::move(fx));
                }
            } else {
                time = keep;
                SDL_Log("shot %s: %s", c->shotEffect.c_str(), err.c_str());
            }
            if (!c->shotMark.empty()) commandEffect(c->shotMark, target, a.partner >= 0);
        }
        // (j007's and m507's: CreateWarpEffect - a pair's, eff110 twice on the one acting -
        // and CreateAttackObject - an attack's, its plain or pair attack's launch effect)
        if (const BattleUnit* au = unitOf(a.actor); au && (au->figure.rfind("j007", 0) == 0 || au->figure.rfind("m507", 0) == 0)) {
            if (a.partner >= 0) commandEffect("eff110", a.actor, true), commandEffect("eff110", a.actor, true);
            const cr3::Fighter& af = battleRules->fighter(a.actor);
            int plainId = a.partner >= 0 ? af.pairAttack : af.attack;
            if (a.actor >= 2)
                if (const cr3::BattleData::Monster* mo = battleData.monster(af.id)) plainId = a.partner >= 0 ? mo->pairAttack : mo->counter;
            const cr3::BattleData::Command* pc = battleData.command(plainId);
            if (a.command == plainId && pc && !pc->launchEffect.empty() && cr3::effectSetting(pc->launchEffect).pairLocator != 7) // (IsAttackAction: the attack's flow)
                commandEffect(pc->launchEffect, a.actor, a.partner >= 0);
        }
        // (ActionSopiaMirror / CreateMirrorEffect: on each whose magic barrier turns it back,
        // eff227 - at its side's middle when its setting says 7 - and SE 0x92)
        for (int m : a.hits.empty() ? std::vector<int>() : a.hits.front().mirrors) {
            const cr3::EffectSetting& ms = cr3::effectSetting("eff227");
            commandEffect("eff227", m, false, ms.locator == 7 ? (m < 2 ? 0 : 1) : -1);
            battleSe(0x92);
        }
        if (!c->launchEffect.empty()) {
            const cr3::EffectSetting& ls = cr3::effectSetting(c->launchEffect);
            if (ls.pairLocator != 7) commandEffect(c->launchEffect, a.actor, a.partner >= 0); // (ShowCommandLaunchEffect: its +0x32 7 left out - every caller passes true)
        }
    }
    // its cameras (BtlActionCtrl::SetAttackCamera1 / 2): by the party - its kind (0 / 1 the
    // one, 2 alone, 3 a pair), the pattern's base and the target, one of two at random, the
    // next at the hit; by a monster - the base, which it is and its target (3 a pair)
    showCamera2 = -1;
    if (target >= 0) {
        const int base = cr3::cameraPattern(battle.pattern);
        const bool targetPair = battleRules->fighter(target).partner >= 0;
        if (a.actor < 2) {
            const bool alone = fighterUnit[1 - a.actor] < 0;
            const int kind = a.partner >= 0 ? 3 : alone ? 2 : a.actor;
            const int n = battle.pattern == 11 && targetPair ? 21 : base + target - 2;
            int cam = cr3::cameraAttackToEnemy(kind, n);
            if (cam >= 0) {
                cam += int(battleClock * 1000.0f) % 2 * 2; // (BtlRand(2): either)
                setBattleCamera(cam);
                showCamera2 = cam + 1;
            }
        } else {
            const int cam = cr3::cameraAttackFromEnemy(base + a.actor - 2, targetPair ? 3 : target);
            if (cam >= 0) setBattleCamera(cam);
        }
        SDL_Log("    camera %s", battleCamera.c_str());
    }
    if (c)
        SDL_Log("  %s%s%s: %s", battleRules->fighter(a.actor).name.c_str(), a.partner >= 0 ? " + " : "",
                a.partner >= 0 ? battleRules->fighter(a.partner).name.c_str() : "", c->name[1].c_str());
    if (c) commandShown = c->name[1], commandShownStart = battleClock;
    if (!a.hits.empty() && a.hits.front().counter) commandShown = announce(0x47), commandShownStart = battleClock; // ("Counter")
}

void Viewer::poseBattleShow() {
    if (!battleRules || battleFight == 0 || showIndex >= showActions.size()) return;
    const float s = battleClock - showStart;
    // the attackers' way there and back
    for (const Move& m : showMoves) {
        if (m.unit < 0) continue;
        BattleUnit& bu = battleUnits[size_t(m.unit)];
        if (!m.active) {
            bu.fig.place = bu.home;
            continue;
        }
        float k = 0;
        if (s >= m.start) k = m.end > m.start ? std::min((s - m.start) / (m.end - m.start), 1.0f) : 1.0f;
        if (m.back >= 0 && s >= m.back) k = m.backEnd > m.back ? 1.0f - std::min((s - m.back) / (m.backEnd - m.back), 1.0f) : 0.0f;
        if (m.back < 0 && s >= showEnd - showStart - 0.3f) k = 0;
        cr3::Mat34 p = k < 0.5f ? bu.home : m.to; // (the turn: at the move's middle)
        for (int r = 0; r < 3; ++r) p.m[r * 4 + 3] = bu.home.m[r * 4 + 3] + (m.to.m[r * 4 + 3] - bu.home.m[r * 4 + 3]) * k;
        bu.fig.place = p;
    }
}

void Viewer::updateBattleShow() {
    if (!battleRules || battleFight == 0) return;
    if (!announceQueue.empty() && battleClock - announceAt > 1.5f) { // (each 1.5 s on the banner)
        commandShown = announceQueue.front(), commandShownStart = announceAt = battleClock;
        SDL_Log("    announce: %s", commandShown.c_str());
        announceQueue.pop_front();
    }
    stepBattleShow();
    if (showIndex >= showActions.size()) return;
    updateScene();
    const ShowAction& a = showActions[showIndex];
    const float s = battleClock - showStart;
    // the hits
    if (!scene.active && !showHitDone && s >= showHitAt) {
        showHitDone = true;
        if (showCamera2 >= 0) setBattleCamera(showCamera2);
        landHits(a, true);
    }
}

// A blow's reaction (BtlActionCtrl::CheckThrowBack): one of the party blocking it, or any one
// guarded by Auto Defend, bg01 once; one guarding (Defend) bg02 - where its table has it - back
// to bg01; in time stopped (21) none; in the Executioner's Pose (37) bx03, back to bx02; else
// bd01 (a monster blocking it: no damage, none)
void Viewer::throwBack(BattleUnit& bu, const cr3::BattleRules::Event& e) {
    if (e.blocked && e.target < 2 && !bu.guarding) return playMotion(bu, "bg01", battleClock);
    if (e.autoGuard) return playMotion(bu, "bg01", battleClock);
    if (e.damage <= 0 && e.blocked) return;
    if (bu.guarding) {
        if (motionLength(bu, "bg02") > 0) playMotion(bu, "bg02", battleClock);
        return;
    }
    const cr3::Fighter& f = battleRules->fighter(e.target);
    if (f.hasState(21)) return;
    if (f.hasState(37) && f.alive) return playMotion(bu, "bx03", battleClock);
    playMotion(bu, "bd01", battleClock);
}

void Viewer::landHits(const ShowAction& a, bool first) {
    if (first) groupShown.clear();
    for (const auto& e : a.hits) {
        const int u = fighterUnit[e.target];
        if (u < 0) continue;
        BattleUnit& bu = battleUnits[size_t(u)];
        const bool restore = e.heal > 0 || e.healMp > 0 || e.revived;
        const cr3::BattleData::Command* sc = battleData.command(e.command);
        const bool statusOnly = e.failed || (sc && (sc->kind == 1 || sc->kind == 3 || sc->kind == 26 || sc->kind == 28)); // (no number, no blow)
        if (first)
            for (const auto& [id, who] : e.notes) // (the announcements: on the banner, in turn - BtlAnnounce)
                announceQueue.push_back(formatAnnounce(announce(id), {battleRules->fighter(who).name}));
        if (first && e.stealNote) // (CheckSteal's: after the rest)
            announceQueue.push_back(e.stealNote == 63 ? formatAnnounce(announce(63), {messageText("itemname", e.stolen)}) : announce(e.stealNote));
        if (e.hit && !restore && !statusOnly) throwBack(bu, e);
        // its sounds (BtlSound::PlayHitSE: the command's +0x74 - -1 one of 12..14 -, 15 on a
        // critical, none on a miss or a block; PlayVoiceAttack - the actor's 11 a blow, 12 a
        // critical, 13 a kill, 15 the battle's last, 14 nothing done -; the one struck's
        // PlayVoiceDamage 3 / CriticalDamage 4 (a quarter of its HP left or less), NoDamage 7,
        // Dead 5; the healed's HealHp 8, revived's Revive 10)
        if (!statusOnly && !restore && e.actor >= 0) {
            const cr3::BattleData::Command* hc0 = battleData.command(a.command);
            if (e.hit && !e.blocked) {
                int se = hc0 ? hc0->hitSe : -1;
                if (se == -1) se = e.critical ? 15 : 12 + int(battleClock * 1000.0f) % 3;
                else if (e.critical && se != 0) se = 15;
                if (hc0 && hc0->magic == 0 && e.burst >= 1 && e.burst <= 3) se = 0x8e + e.burst; // (HitEffect: eff218 .. 220's 0x8f .. 0x91)
                if (se > 0) battleSe(se);
            }
            if (!first) continue; // (a later hit: its blow and sound alone)
            if (&e == &a.hits.front())
                battleVoice(e.actor, !e.hit || e.blocked ? 14 : e.defeated ? (battleRules->outcome() == 1 ? 15 : 13) : e.critical ? 12 : e.damage > 0 ? 11 : 14);
            const cr3::Fighter& tf = battleRules->fighter(e.target);
            if (e.hit && !e.blocked)
                battleVoice(e.target, e.defeated ? 5 : e.damage <= 0 ? 7 : tf.hp * 100 / std::max(tf.maxHp, 1) < 26 ? 4 : 3, 0.5f);
        } else if (restore && first) {
            battleVoice(e.target, e.revived ? 10 : 8, 0.3f);
        }
        if (!first) continue;
        if (e.revived) bu.hidden = false;
        const cr3::BattleData::Command* hc = battleData.command(e.command);
        if (!statusOnly && (e.hit || restore || !(hc && hc->kind == 2))) { // (the number at its DummyDP01 - else its middle - on the screen, in UI units; none for a guard)
            float p[3] = {bu.fig.place.m[3], bu.fig.place.m[7] + 0.15f, bu.fig.place.m[11]};
            const int dj = bu.fig.skeleton.joint(cr3::nameHash("DummyDP01"));
            if (dj >= 0) {
                std::vector<cr3::JointTransform> pp = bu.fig.skeleton.basePose;
                const std::vector<cr3::Mat34> ww = bu.fig.skeleton.world(pp, bu.fig.place);
                if (size_t(dj) < ww.size()) p[0] = ww[size_t(dj)].m[3], p[1] = ww[size_t(dj)].m[7], p[2] = ww[size_t(dj)].m[11];
            }
            // the hit's effect on it (BtlActionCtrl::HitEffect / ShowCommandHitEffect: the
            // command's +0x18 - an attack's (+0x49 0) whose element beat the target's, eff218 ..
            // eff220; a critical with the plain attack eff106 -, none here for a group's - its
            // setting's locator 7: HitGroupEffect, once)
            if (e.hit && !restore) {
                const cr3::BattleData::Command* ec = battleData.command(a.command);
                bool plain = e.actor >= 0 && e.actor < 2 && a.command == battleRules->fighter(e.actor).attack; // (a monster's: its +0x4c)
                if (e.actor >= 2)
                    if (const cr3::BattleData::Monster* en = battleData.monster(battleRules->fighter(e.actor).id)) plain = a.command == en->counter;
                std::string fx = ec ? ec->hitEffect : "";
                if (ec && ec->magic == 0 && e.burst >= 1 && e.burst <= 3) fx = "eff21" + std::to_string(7 + e.burst);
                if (e.critical && plain) fx = "eff106";
                if (!fx.empty()) {
                    const cr3::EffectSetting& set = cr3::effectSetting(fx);
                    const bool pairAct = a.partner >= 0;
                    if ((pairAct ? set.pairLocator : set.locator) != 7) commandEffect(fx, e.target, pairAct);
                    else if (std::find(groupShown.begin(), groupShown.end(), e.target < 2 ? 0 : 1) == groupShown.end())
                        groupShown.push_back(e.target < 2 ? 0 : 1), commandEffect(fx, e.target, pairAct, e.target < 2 ? 0 : 1);
                }
            }
            {
                DamageShown ds;
                ds.value = e.damage, ds.set = e.weak ? 40 : 20, ds.start = battleClock; // (the restores: HP green 50, MP 60)
                if (restore) ds.value = e.heal > 0 || e.healMp == 0 ? e.heal : e.healMp, ds.set = e.heal > 0 || e.healMp == 0 ? 50 : 60;
                else if (!e.hit) ds.value = -1, ds.mark = 0;
                else if (e.critical) ds.mark = 2;
                for (int c = 0; c < 3; ++c) ds.at[c] = p[c];
                damageShown.push_back(ds);
            }
        }
        {
            const cr3::BattleData::Command* ec = battleData.command(e.command);
            const std::string what = ec && ec->kind == 2 ? std::string("defends")
                                     : restore ? (e.revived ? "revived, " : "") + std::string("+") + std::to_string(e.heal) + " HP" + (e.healMp ? " +" + std::to_string(e.healMp) + " MP" : "")
                                     : e.hit ? std::to_string(e.damage) + (e.critical ? " critical" : "") + (e.defeated ? ", down" : "") : "missed";
            SDL_Log("    %s -> %s: %s (%.1f s)", battleRules->fighter(e.actor).name.c_str(), battleRules->fighter(e.target).name.c_str(), what.c_str(), battleClock);
        }
    }
}

auto Viewer::unitOf(int f) -> BattleUnit* {
    const int u = f >= 0 && f < 5 ? fighterUnit[f] : -1;
    return u >= 0 ? &battleUnits[size_t(u)] : nullptr;
}

void Viewer::unscaled(cr3::Mat34& m) {
    for (int c = 0; c < 3; ++c) {
        const float l = std::sqrt(m.m[c] * m.m[c] + m.m[4 + c] * m.m[4 + c] + m.m[8 + c] * m.m[8 + c]);
        if (l > 0) m.m[c] /= l, m.m[4 + c] /= l, m.m[8 + c] /= l;
    }
}

// a figure's joint in the world as it was last posed (false: none)
bool Viewer::jointPlace(const ViewFigure& f, const char* joint, cr3::Mat34& out) {
    const int j = joint ? f.skeleton.joint(cr3::nameHash(joint)) : -1;
    if (j < 0) return false;
    const std::vector<cr3::Mat34> w = f.skeleton.world(f.posed.empty() ? f.skeleton.basePose : f.posed, f.place);
    if (size_t(j) >= w.size()) return false;
    out = w[size_t(j)];
    unscaled(out);
    return true;
}

// ActionSpecialGetPosition / GetRotation: a place a scene names - `target` 1 its actor, 2 its
// target (0 none; 3 the party's middle, 4 the monsters' - BtlWork +0xad0 / +0xae0, the latter
// CalcMonstersCenterPosition's sum over the party's count); by `apos` the aPos joint the one
// goes to to strike the other, by `dpos` its battledpos place, else its joint `tpos`
// (BtlTposTable's; 9 the model's own place)
cr3::Mat34 Viewer::scenePlace(int target, bool apos, bool dpos, int tpos) {
    cr3::Mat34 out;
    const int a = scene.actor, b = scene.target;
    if (apos) {
        cr3::Mat34 m;
        if (a >= 0 && b >= 0 && (target == 1 || target == 2) && aposPlace(target == 1 ? aposName(a, b) : aposName(b, a), m)) out = m;
        return out;
    }
    BattleUnit* u = target == 1 ? unitOf(a) : target == 2 ? unitOf(b) : nullptr;
    if (dpos) {
        if (u) out = u->home;
        return out;
    }
    if (target == 3 || target == 4) {
        int party = 0;
        float sum[3] = {0, 0, 0};
        for (int f = 0; f < 5; ++f) {
            BattleUnit* fu = unitOf(f);
            if (f < 2 && fu) ++party;
            if (!fu || (f < 2) != (target == 3)) continue;
            for (int c = 0; c < 3; ++c) sum[c] += fu->fig.place.m[c * 4 + 3];
        }
        for (int c = 0; c < 3; ++c) out.m[c * 4 + 3] = party > 0 ? sum[c] / float(party) : 0.0f;
        return out;
    }
    if (!u) return out;
    if (tpos == 9 || !jointPlace(u->fig, cr3::tposJoint(tpos), out)) {
        out = u->fig.place;
        unscaled(out);
    }
    return out;
}

cr3::Mat34 Viewer::turnY(float degrees) {
    const float r = degrees * 3.14159265f / 180.0f, c = std::cos(r), s = std::sin(r);
    cr3::Mat34 m;
    m.m[0] = c, m.m[2] = s, m.m[8] = -s, m.m[10] = c;
    return m;
}

void Viewer::setRotation(cr3::Mat34& m, const cr3::Mat34& from) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) m.m[r * 4 + c] = from.m[r * 4 + c];
}

// the action's scene, if its command has one (SetActionTable), begun: its steps (those of
// BuildAwakeFromMotionTable - the motion table's hits, the dash and the way back - in its
// place); false none
bool Viewer::startScene(const ShowAction& a, const cr3::BattleData::Command& c) {
    bool advent = false;
    const cr3::SpecialScene* sc = cr3::specialScene(a.command, c.awaken != 0, &advent);
    if (!sc) return false;
    endScene();
    scene.active = true, scene.advent = advent, scene.skippable = true;
    scene.actor = a.actor, scene.partner = a.partner, scene.target = a.hits.empty() ? -1 : a.hits.front().target;
    scene.steps.clear(), scene.hits = 0;
    for (int k = 0; k < sc->count; ++k) {
        const cr3::SpecialStep& st = sc->steps[k];
        if (st.op != cr3::kSpecialMotionTable) {
            scene.steps.push_back(st);
            continue;
        }
        cr3::MotionTimes t;
        if (!st.s || !cr3::motionTimes(st.s, st.i[0], t)) continue;
        auto add = [&](short op, float at, float f0, int i0) {
            cr3::SpecialStep x{};
            x.op = op, x.time = at, x.f[0] = f0, x.i[0] = i0;
            scene.steps.push_back(x);
        };
        for (int h = 0; h < 10; ++h) // (each a hit, the last whose next is none - the 10th's "next" the turn's start)
            if (t.hit[h] > 0) add(cr3::kSpecialHit, t.hit[h], 0, !((h < 9 ? t.hit[h + 1] : t.turnStart) > 0));
        if (t.moveStart > 0) add(cr3::kSpecialDash, t.moveStart, t.moveEnd - t.moveStart, 0);
        if (t.move2Start > 0) add(cr3::kSpecialReturn, t.move2Start, t.move2End - t.move2Start, 0);
        if (t.turnStart > 0) add(cr3::kSpecialReturnDirection, t.turnStart, t.turnEnd - t.turnStart, 0);
    }
    scene.done.assign(scene.steps.size(), false);
    const float lead = advent ? 2.25f : 0.0f;
    scene.start = showStart = battleClock + lead;
    showHitDone = false, showCamera2 = -1, showEnd = 1e9f; // (till it is over)
    showHitAt = 0;
    for (Move& m : showMoves) m = Move();
    if (advent) { // (the pair calls it: bm01, its launch sound, one of the two's voice; then gone)
        for (int f : {a.actor, a.partner})
            if (BattleUnit* bu = unitOf(f)) {
                playMotion(*bu, "bm01", battleClock);
                bu->alpha.begin(battleClock + 1.25f, 1, 0, 1.0f);
            }
        if (c.launchSe > 0) battleSe(c.launchSe);
        if (!c.launchEffect.empty() && cr3::effectSetting(c.launchEffect).pairLocator != 7) // (ActionAdventLaunch: ShowCommandLaunchEffect on the one calling it - the advents' eff302, 7, left out as there)
            commandEffect(c.launchEffect, a.actor, true);
    } else if (BattleUnit* bu = unitOf(a.actor)) { // (ActionAwakeStandby: its command's motion)
        playMotion(*bu, c.motion, battleClock);
    }
    SDL_Log("  scene: %s %d steps", advent ? "advent" : "awakening", int(scene.steps.size()));
    return true;
}

// its effect in slot `id` (null: none)
auto Viewer::sceneEffect(int id) -> SceneEffect* {
    const auto it = scene.effects.find(id);
    return it != scene.effects.end() && it->second.fig ? &it->second : nullptr;
}

void Viewer::runStep(const cr3::SpecialStep& st) {
    using namespace cr3;
    BattleUnit* actorUnit = unitOf(scene.actor);
    const bool party = scene.actor >= 0 && scene.actor < 2;
    switch (st.op) {
    case kSpecialSetMotion:
        if (actorUnit) playMotion(*actorUnit, motionName(st.i[0]), battleClock);
        break;
    case kSpecialSetPosition: // (SetPosRot: back at its dpos)
    case kSpecialSetRotation:
        for (Move& m : showMoves) m.active = false;
        if (actorUnit) actorUnit->fig.place = actorUnit->home;
        break;
    case kSpecialSetVisibility: { // (0 its side, 1 the other's - the monsters' standing -, 3 its partner, 4 the map)
        const bool v = st.i[1] != 0;
        if (st.i[0] == 4) battleMapHidden = !v;
        for (int f = 0; f < 5; ++f) {
            BattleUnit* u = unitOf(f);
            if (!u) continue;
            const bool own = (f < 2) == party;
            const bool pick = st.i[0] == 0 ? own : st.i[0] == 1 ? !own : st.i[0] == 3 ? own && f != scene.actor : false;
            if (pick && (f < 2 || battleRules->fighter(f).alive)) u->sceneHidden = !v;
        }
        break;
    }
    case kSpecialSetAlpha: // (0 the party, else the monsters standing)
        for (int f = 0; f < 5; ++f)
            if (BattleUnit* u = unitOf(f); u && (f < 2) == (st.i[0] == 0) && (f < 2 || battleRules->fighter(f).alive))
                u->alpha.begin(battleClock, st.f[0], st.f[1], st.f[2]);
        break;
    case kSpecialSetColor: // (3 the party, 4 the monsters, 7 the map)
        if (st.i[0] == 7) mapColour.begin(battleClock, st.f[0], st.f[1], st.f[2]);
        for (int f = 0; f < 5; ++f)
            if (BattleUnit* u = unitOf(f); u && ((st.i[0] == 3 && f < 2) || (st.i[0] == 4 && f >= 2)))
                u->colour.begin(battleClock, st.f[0], st.f[1], st.f[2]);
        break;
    case kSpecialSetEffect: { // (CreateEffect: its settings' delay on its start; connect: the actor's model)
        if (!st.s) break;
        SceneEffect fx;
        fx.fig = std::make_unique<ViewFigure>();
        std::string err;
        const float keep = time;
        if (!loadFigure(std::string(st.s) + ".geom", *fx.fig, &err)) {
            SDL_Log("scene effect %s: %s", st.s, err.c_str());
            break;
        }
        const auto it = std::find(fx.fig->anims.begin(), fx.fig->anims.end(), std::string(st.s) + ".anim");
        if (it != fx.fig->anims.end()) loadAnim(*fx.fig, size_t(it - fx.fig->anims.begin()));
        time = keep;
        fx.set = &effectSetting(st.s);
        fx.fig->absolute = fx.set->kind != 2; // (a summoned one's motions over its pose)
        fx.shown = st.i[1] != 0;
        fx.follows = st.i[2] ? scene.actor : -1;
        fx.start = battleClock + st.f[1] + float(fx.set->delay) * 0.05f;
        fx.end = st.f[0] > 0 ? battleClock + st.f[0] : -1;
        if (const auto old = scene.effects.find(st.i[0]); old != scene.effects.end() && old->second.fig) unload(*old->second.fig);
        scene.effects[st.i[0]] = std::move(fx);
        break;
    }
    case kSpecialSetEffectTarget:
        if (SceneEffect* fx = sceneEffect(st.i[0])) fx->follows = st.i[1] == 1 ? scene.actor : st.i[1] == 2 ? scene.target : fx->follows;
        break;
    case kSpecialSetEffectPosition:
        if (SceneEffect* fx = sceneEffect(st.i[0])) {
            const Mat34 p = scenePlace(st.i[1], st.i[2] != 0, st.i[3] != 0, st.i[4]);
            for (int c = 0; c < 3; ++c) fx->place.m[c * 4 + 3] = p.m[c * 4 + 3] + st.v[c];
        }
        break;
    case kSpecialSetEffectRotation:
        if (SceneEffect* fx = sceneEffect(st.i[0])) setRotation(fx->place, scenePlace(st.i[1], st.i[2] != 0, st.i[3] != 0, st.i[4]) * turnY(st.f[0]));
        break;
    case kSpecialSetEffectAnimation:
        if (SceneEffect* fx = sceneEffect(st.i[0]); fx && st.s) {
            std::string base = fx->fig->name;
            if (base.size() > 5) base.resize(base.size() - 5); // (".geom")
            const auto it = std::find(fx->fig->anims.begin(), fx->fig->anims.end(), base + st.s + ".anim");
            if (it != fx->fig->anims.end()) {
                const float keep = time;
                loadAnim(*fx->fig, size_t(it - fx->fig->anims.begin()));
                time = keep;
                fx->start = std::max(fx->start, battleClock);
            }
        }
        break;
    case kSpecialSetEffectAlpha:
        if (SceneEffect* fx = sceneEffect(st.i[0])) fx->alpha.begin(battleClock, st.f[0], st.f[1], st.f[2]);
        break;
    case kSpecialLoadCamera: {
        if (!st.s) break;
        std::string n = st.s;
        for (char& ch : n) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        CameraFigure c;
        std::vector<uint8_t> d;
        if (archive.read(n + ".geom", d)) cr3::parseGeom(d, c.geom);
        if (archive.read(n + ".skel", d)) cr3::parseSkeleton(d, c.skel);
        c.animated = archive.read(n + ".anim", d) && c.anim.parse(d) && c.anim.joints() == int(c.skel.basePose.size());
        if (c.geom.cameras.empty()) SDL_Log("scene camera %s: none", st.s);
        sceneCameras.emplace_back(st.s, std::move(c));
        break;
    }
    case kSpecialSetCamera: // (BtlCameraCtrl::Start: from its start, in place; "GetSet..." the pattern's own)
        if (!st.s) break;
        battleCamera.clear();
        if (std::strncmp(st.s, "GetSet", 6) == 0) setBattleCamera(battle.camera);
        else setBattleCamera(std::string(st.s));
        break;
    case kSpecialSetCameraPosition: {
        const Mat34 p = scenePlace(st.i[0], st.i[1] != 0, st.i[2] != 0, st.i[3]);
        for (int c = 0; c < 3; ++c) battleCameraPlace.m[c * 4 + 3] = p.m[c * 4 + 3] + st.v[c];
        break;
    }
    case kSpecialSetCameraRotation:
        setRotation(battleCameraPlace, scenePlace(st.i[0], st.i[1] != 0, st.i[2] != 0, st.i[3]) * turnY(st.f[0]));
        break;
    case kSpecialSetTargetCamera:
        targetCameraFighter = -1;
        setTargetCamera(scene.target);
        break;
    case kSpecialHit: // (Hit: the next of the action's hits - the numbers at the first)
        if (showIndex < showActions.size()) {
            if (scene.hits == 0) showHitDone = true, showHitAt = battleClock - showStart;
            landHits(showActions[showIndex], scene.hits == 0);
        }
        ++scene.hits;
        break;
    case kSpecialDash: // (SetMovePosition: to its aPos by the target over the time)
        if (actorUnit && scene.target >= 0 && aposPlace(aposName(scene.actor, scene.target), showMoves[0].to)) {
            Move& m = showMoves[0];
            m.unit = fighterUnit[scene.actor], m.active = true;
            m.start = st.time, m.end = st.time + std::max(st.f[0], 0.0f), m.back = -1, m.backEnd = -1;
        }
        break;
    case kSpecialReturn:
        if (showMoves[0].active) showMoves[0].back = st.time, showMoves[0].backEnd = st.time + std::max(st.f[0], 0.0f);
        break;
    case kSpecialPlaySE: { // (-1 the command's launch sound, -2 its hit's, -3 its shot's, -4 the post's; a delay: later)
        const BattleData::Command* c = showIndex < showActions.size() ? battleData.command(showActions[showIndex].command) : nullptr;
        int n = st.i[0];
        if (n == -1) n = c ? c->launchSe : 0;
        else if (n == -2) n = c && c->hitSe > 0 ? c->hitSe : 12;
        else if (n == -3) n = c ? c->shotSe : 0;
        else if (n == -4) n = c ? c->postSe : 0;
        const int clip = n > 0 ? SoundData::battleSe(n) : -1;
        if (clip < 0) break;
        if (st.f[0] > 0) seJobs.push_back({battleClock + st.f[0], clip});
        else audio.playSe(clip, false);
        break;
    }
    case kSpecialPlayEventSE: // (Cr3UtilSoundPlaySE: the clip itself)
        audio.playSe(st.i[0], false);
        break;
    case kSpecialFade: { // (Cr3Fade: 0 in from white, 1 out to white, 2 in from black, 3 out to black; frames of 0.05 s)
        const bool in = st.i[0] == 0 || st.i[0] == 2;
        battleFade.colour = st.i[0] < 2 ? 1 : 0;
        battleFade.alpha.begin(battleClock, in ? 1.0f : 0.0f, in ? 0.0f : 1.0f, float(st.i[1]) * 0.05f);
        break;
    }
    case kSpecialSetSkipEnable: // (ActionSpecialSetSkipEnable: its "enable")
        scene.skippable = st.i[0] != 0;
        break;
    case kSpecialDarwin3Standby: // (BtlMap::LoadSpecialMap: d1154b, not yet drawn)
        if (specialMap.meshes.empty()) {
            std::string err;
            const float keep = time;
            if (!loadFigure("d1154b.geom", specialMap, &err)) SDL_Log("d1154b: %s", err.c_str());
            time = keep;
        }
        specialMapStart = battleClock;
        break;
    case kSpecialGlassFragment: // (the map alone caught at the next frame, broken away over d1154b)
        glassPending = true;
        specialMapShown = !specialMap.meshes.empty();
        break;
    case kSpecialDarwin3ResetMap:
        specialMapShown = false;
        break;
    case kSpecialDarwin3DarwinOffset:   // (Darwin by the one struck: its place, its figure's offset)
    case kSpecialDarwin3CameraOffset: { // (the camera's place by the same)
        BattleUnit* tu = unitOf(scene.target);
        if (!tu) break;
        std::string key = tu->figure;
        const bool flip = scene.target >= 2 && !key.empty() && key[0] == 'j'; // (a monster with a party figure: m5..)
        if (flip && st.op == kSpecialDarwin3DarwinOffset && key.size() >= 2) key[0] = 'm', key[1] = '5';
        const float* off = darwinFigureOffset(key);
        if (!off) break;
        if (st.op == kSpecialDarwin3DarwinOffset) {
            if (actorUnit)
                for (int c = 0; c < 3; ++c) actorUnit->fig.place.m[c * 4 + 3] = tu->home.m[c * 4 + 3] + off[c] * 0.01f;
        } else {
            const bool paired = battleRules->fighter(scene.target).partner >= 0;
            for (int c = 0; c < 3; ++c) {
                const float from = paired ? tu->home.m[c * 4 + 3] : battleCameraPlace.m[c * 4 + 3];
                battleCameraPlace.m[c * 4 + 3] = from + (flip ? -off[c] : off[c]) * 0.01f;
            }
        }
        break;
    }
    case kSpecialDarwin3CameraOffset2: { // (the camera at the size pattern's offset, Darwin moved from his place by it)
        if (scene.target < 0) break;
        const int key = scene.target < 2 ? (battleRules->fighter(scene.target).partner >= 0 ? 14 : 13) : battle.pattern;
        const float* off = darwinPatternOffset(key);
        if (!off) break;
        const float o[3] = {-off[0] * 0.01f, off[1] * 0.01f, -off[2] * 0.01f};
        for (int c = 0; c < 3; ++c) battleCameraPlace.m[c * 4 + 3] = o[c];
        if (actorUnit)
            for (int c = 0; c < 3; ++c) actorUnit->fig.place.m[c * 4 + 3] = actorUnit->home.m[c * 4 + 3] + o[c];
        break;
    }
    case kSpecialLesica1Standby: // (back at its place, its first pose; its partner - 1 - itself, or 5 - itself - hidden)
    case kSpecialLesica2Standby: {
        for (Move& m : showMoves) m.active = false;
        if (actorUnit) actorUnit->fig.place = actorUnit->home;
        if (BattleUnit* p = unitOf(scene.actor < 2 ? 1 - scene.actor : 5 - scene.actor);
            p && battleRules->fighter(scene.actor < 2 ? 1 - scene.actor : 5 - scene.actor).alive)
            p->sceneHidden = true;
        break;
    }
    case kSpecialRuleFixPosition: { // (the party paired - BtlWork +0x523 -: the camera's place and the effect at dPos_pSoloC)
        float p[3];
        if (battleRules->fighter(0).partner < 0 || !dposPosition("dPos_pSoloC", p)) break;
        for (int c = 0; c < 3; ++c) battleCameraPlace.m[c * 4 + 3] = p[c];
        if (SceneEffect* fx = sceneEffect(4))
            for (int c = 0; c < 3; ++c) fx->place.m[c * 4 + 3] = p[c];
        break;
    }
    default: break; // (WaitTime)
    }
}

void Viewer::updateScene() {
    if (!scene.active) return;
    const float t = battleClock - scene.start;
    bool all = true;
    for (size_t k = 0; k < scene.steps.size(); ++k) {
        if (scene.done[k]) continue;
        if (t < scene.steps[k].time) {
            all = false;
            continue;
        }
        scene.done[k] = true;
        runStep(scene.steps[k]);
    }
    if (all) endScene();
}

// A tap during a scene (ActionAwakeMain / ActionAdventMain: while it may be skipped - but for
// coupling 282's command 123 and 221's / 271's 352 -, BtlMain::IsEnabled always so):
// ActionAwakeSkip / ActionAdventSkip - the moves, voices and sounds stopped, its effects gone,
// all at their places, then each of its hits not yet landed landed at once, and its end
bool Viewer::skipScene() {
    if (!scene.active || !scene.skippable || battleClock < scene.start) return false;
    const int command = showIndex < showActions.size() ? showActions[showIndex].command : 0;
    if ((battle.coupling == 0x11a && command == 0x7b) || ((battle.coupling == 0xdd || battle.coupling == 0x10f) && command == 0x160)) return false;
    SDL_Log("  scene skipped");
    cancelSceneAudio();
    for (Move& m : showMoves) m.active = false;
    for (BattleUnit& u : battleUnits) u.fig.place = u.home;
    for (size_t k = 0; k < scene.steps.size(); ++k) {
        if (scene.done[k]) continue;
        scene.done[k] = true;
        if (scene.steps[k].op == cr3::kSpecialHit) runStep(scene.steps[k]);
    }
    endScene();
    return true;
}

// ActionAwakeCleanup / ActionAdventCleanup: its effects and cameras gone, all shown again at full
// opacity, the HUD back, the battle's camera
void Viewer::endScene() {
    if (!scene.active) return;
    scene.active = false;
    for (auto& [id, fx] : scene.effects)
        if (fx.fig) unload(*fx.fig);
    scene.effects.clear();
    sceneCameras.clear();
    for (BattleUnit& u : battleUnits) u.sceneHidden = false, u.alpha = Ramp(), u.colour = Ramp();
    battleMapHidden = false, mapColour = Ramp();
    specialMapShown = false;
    battleFade = BattleFade();
    for (Move& m : showMoves) m.active = false;
    if (!scene.hits && showIndex < showActions.size()) { // (one without a hit of its own)
        showHitDone = true, showHitAt = battleClock - showStart;
        landHits(showActions[showIndex], true);
    }
    showEnd = battleClock + 0.3f;
    battleCamera.clear();
    setBattleCamera(battle.camera);
}

// an effect's frame (BtlEffect::Pose): false once it is over (unloaded)
bool Viewer::drawEffect(SceneEffect& fx, const cr3::Mat34& camera, const Mat4& vp) {
    const float t = battleClock - fx.start;
    const bool over = fx.kept ? false : fx.end >= 0 ? battleClock >= fx.end : !fx.set->loop && fx.fig->animLoaded && t > fx.fig->anim.duration();
    if (over) {
        unload(*fx.fig);
        return false;
    }
    if (!fx.shown || t < 0) return true;
    cr3::Mat34 place = fx.place;
    // BtlEffect::Pose: following its model's locator (9 its place, 7 none) and its shift,
    // its turn; turned to the camera
    const short locator = fx.locator >= 0 ? fx.locator : fx.set->locator;
    if (BattleUnit* u = unitOf(fx.follows); u && fx.set->constrain & 3) {
        cr3::Mat34 j = u->fig.place;
        unscaled(j);
        if (locator == 7) j = cr3::Mat34();
        else if (locator != 9) jointPlace(u->fig, cr3::tposJoint(locator), j);
        if (fx.set->constrain & 1)
            for (int c = 0; c < 3; ++c) place.m[c * 4 + 3] = j.m[c * 4 + 3] + fx.set->shift[c];
        if (fx.set->constrain & 2) setRotation(place, j);
    }
    if (fx.shot) {
        if (t > fx.flight) {
            unload(*fx.fig);
            return false;
        }
        const float k = fx.flight > 0 ? t / fx.flight : 1;
        for (int c = 0; c < 3; ++c) place.m[c * 4 + 3] = fx.from[c] + (fx.to[c] - fx.from[c]) * k;
        if (fx.path == 1 && fx.flight > 0) place.m[7] += 2.0f * t - 2.0f / fx.flight * t * t; // (CalcFallMotion: back down at its end)
    }
    if (fx.set->billboard) setRotation(place, camera);
    fx.fig->place = place;
    poseView = camera;
    fx.fig->tint[3] = fx.alpha.at(battleClock);
    pose(*fx.fig, t, fx.set->loop);
    drawFigure(*fx.fig, vp.data());
    return true;
}

void Viewer::drawSceneEffects(const cr3::Mat34& camera, const Mat4& vp) {
    for (auto it = scene.effects.begin(); it != scene.effects.end();)
        it = drawEffect(it->second, camera, vp) ? std::next(it) : scene.effects.erase(it);
    for (size_t k = 0; k < commandEffects.size();)
        if (drawEffect(commandEffects[k], camera, vp)) ++k;
        else commandEffects.erase(commandEffects.begin() + std::ptrdiff_t(k));
    for (auto& g : guardObjects)
        if (g) drawEffect(*g, camera, vp);
}

void Viewer::guardObject(int f, float delay) {
    guardObjects[f].reset();
    SceneEffect fx;
    fx.fig = std::make_unique<ViewFigure>();
    std::string err;
    const float keep = time;
    if (!loadFigure("obj303.geom", *fx.fig, &err)) {
        SDL_Log("obj303: %s", err.c_str());
        return;
    }
    const auto it = std::find(fx.fig->anims.begin(), fx.fig->anims.end(), std::string("obj303.anim"));
    if (it != fx.fig->anims.end()) loadAnim(*fx.fig, size_t(it - fx.fig->anims.begin()));
    time = keep;
    fx.set = &cr3::effectSetting("obj303");
    fx.fig->absolute = fx.set->kind != 2;
    fx.start = battleClock + delay, fx.follows = f, fx.kept = true;
    guardObjects[f] = std::make_unique<SceneEffect>(std::move(fx));
}

void Viewer::dropGuardObject(int f) {
    if (guardObjects[f]) unload(*guardObjects[f]->fig);
    guardObjects[f].reset(), guardReturn[f] = false;
}

void Viewer::setGuard(int f, bool on) {
    BattleUnit* u = unitOf(f);
    if (!u || u->guarding == on) return;
    u->guarding = on;
    u->motion = on ? "bg01" : "bb01", u->then = u->motion, u->loop = true, u->hold = false, u->motionStart = battleClock;
    if (on && battleRules->fighter(f).id == 6) guardObject(f, 0.2f);
    else if (!on) dropGuardObject(f);
}

// the stances by the orders chosen so far
void Viewer::updateGuards() {
    bool want[2] = {false, false};
    for (const cr3::BattleRules::Order& o : commandInput.orders())
        if (o.command == cr3::CommandInput::kDefend && o.actor >= 0 && o.actor < 2) {
            want[o.actor] = true;
            if (commandInput.pairTurn()) want[0] = want[1] = true;
        }
    for (int f = 0; f < 2; ++f)
        if (battleRules->fighter(f).alive) setGuard(f, want[f]);
}

void Viewer::commandEffect(const std::string& name, int target, bool pair, int side) {
    SceneEffect fx;
    fx.fig = std::make_unique<ViewFigure>();
    std::string err;
    const float keep = time;
    if (!loadFigure(name + ".geom", *fx.fig, &err)) {
        SDL_Log("effect %s: %s", name.c_str(), err.c_str());
        return;
    }
    const auto it = std::find(fx.fig->anims.begin(), fx.fig->anims.end(), name + ".anim");
    if (it != fx.fig->anims.end()) loadAnim(*fx.fig, size_t(it - fx.fig->anims.begin()));
    time = keep;
    fx.set = &cr3::effectSetting(name);
    fx.fig->absolute = fx.set->kind != 2;
    fx.start = battleClock + float(fx.set->delay) * 0.05f;
    if (side >= 0) { // (the side's middle: its standing ones' places)
        float m[3] = {0, 0, 0};
        int n = 0;
        for (int f = 0; f < 5; ++f)
            if (BattleUnit* u = unitOf(f); u && (f < 2 ? 0 : 1) == side && battleRules->fighter(f).alive)
                for (int c = 0; c < 3; ++c) m[c] += u->home.m[c * 4 + 3], n += c == 0;
        if (n == 0) return;
        for (int c = 0; c < 3; ++c) fx.place.m[c * 4 + 3] = m[c] / float(n) + fx.set->shift[c];
    } else {
        fx.locator = pair ? fx.set->pairLocator : fx.set->locator;
        fx.follows = target;
        if (!(fx.set->constrain & 1)) { // (BtlEffect::SetTargetPosition: put there once)
            if (BattleUnit* u = unitOf(target)) {
                cr3::Mat34 j = u->fig.place;
                unscaled(j);
                if (fx.locator != 9 && fx.locator != 7) jointPlace(u->fig, cr3::tposJoint(fx.locator), j);
                for (int c = 0; c < 3; ++c) fx.place.m[c * 4 + 3] = j.m[c * 4 + 3] + fx.set->shift[c];
            }
            fx.follows = -1;
        }
    }
    commandEffects.push_back(std::move(fx));
}

void Viewer::drawBattle(int w, int h, bool mapOnly) {
    cr3::Mat34 cw;
    float fov = 45, zn = 0.1f, zf = 100;
    if (!battleView(cw, fov, zn, zf)) return;
    dev->setCamera(zn, zf, fov * 3.14159265f / 180);
    const Mat4 world = cr3::mat4FromMat34(cw);
    const Mat4 vp = cr3::mat4Multiply(perspective(fov * 3.14159265f / 180, float(w) / float(std::max(h, 1)), zn, zf),
                                      cr3::mat4RigidInverse(world));
    cr3::SceneLighting l = lighting;
    const float axis[3] = {cw.m[2], cw.m[6], cw.m[10]}; // (the light from the camera)
    const float n = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (n > 0)
        for (int c = 0; c < 3; ++c) l.lightDir[c] = axis[c] / n;
    l.camera[0] = world[12], l.camera[1] = world[13], l.camera[2] = world[14];
    l.time = time;
    dev->setLighting(l);
    battleVP = vp;
    for (DamageShown& d : damageShown) // (the numbers made since the last frame: on the screen by this one's camera)
        if (!d.placed) {
            d.placed = true;
            if (!battleUi(d.at, d.x, d.y)) d.start = -100; // (behind it: not shown)
        }
    if (mapOnly) { // (PostEffectGlassFragments: BtlMap's passes alone, before d1154b replaces it)
        drawFigure(battleMap, vp.data());
        if (!mapEffect.meshes.empty()) drawFigure(mapEffect, vp.data());
        return;
    }
    if (!battleMapHidden) drawFigure(specialMapShown ? specialMap : battleMap, vp.data()); // (BtlMap::Draw: d1154b in its place)
    if (!battleMapHidden && !mapEffect.meshes.empty()) drawFigure(mapEffect, vp.data());
    for (const BattleUnit& u : battleUnits)
        if (!u.hidden && !u.sceneHidden) drawFigure(u.fig, vp.data());
    drawEffects(cw, vp);
    drawSceneEffects(cw, vp);
    drawGlass();
}

// The screen's half size in the game's pixels (RenderContext's screen: its height taken as
// the iPhone's 640 - the shards' speeds are in its pixels)
void Viewer::glassSize(int& hw, int& hh) const {
    hh = 320;
    hw = std::max(int(320.0f * float(frameW) / float(std::max(frameH, 1)) + 0.5f), 1);
}

// BtlMain::PostEffectGlassFragments: the map alone (passes 2, 4, 8) drawn into render target
// 5, its texture the shards' (BtlPostEffectGlassFragment::Setup)
void Viewer::grabGlass(int fw, int fh) {
    glassPending = false;
    dev->beginFrame(fw, fh);
    placeScreen(fw, fh);
    drawBattle(frameW, frameH, true);
    dev->endScene();
    dev->endFrame(false);
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (!dev->readPixels(px, w, h) || frameW <= 0 || frameH <= 0 || frameX + frameW > w || frameY + frameH > h) {
        SDL_Log("the screen breaks away: not caught (%dx%d of %dx%d)", frameW, frameH, w, h);
        return;
    }
    cr3::Image img;
    img.width = frameW, img.height = frameH;
    img.rgba.resize(size_t(frameW) * frameH * 4);
    for (int y = 0; y < frameH; ++y) { // (bottom-up: a render target's v = 0 its bottom)
        const uint8_t* row = &px[(size_t(frameY + frameH - 1 - y) * w + frameX) * 4];
        std::memcpy(&img.rgba[size_t(y) * frameW * 4], row, size_t(frameW) * 4);
    }
    for (size_t k = 3; k < img.rgba.size(); k += 4) img.rgba[k] = 255;
    if (glassTexture.id) dev->destroy(glassTexture);
    glassTexture = dev->upload(img);
    dev->resetHistory();
    int hw = 0, hh = 0;
    glassSize(hw, hh);
    glass.build(hw, hh, glassRng);
    glassStart = battleClock;
    SDL_Log("the screen breaks away: %zu shards (%d x %d)", glass.shards.size(), hw * 2, hh * 2);
}

// BtlPostEffectGlassFragment::Draw: the shards (src alpha, one minus), the whole's alpha falling
void Viewer::drawGlass() {
    if (!glass.running() || !glassTexture.id) return;
    Mat4 proj = cr3::mat4Identity();
    proj[0] = 1.0f / float(glass.w), proj[5] = 1.0f / float(glass.h);
    const float rgba[4] = {1, 1, 1, glass.alpha()};
    for (const cr3::GlassShard& s : glass.shards) {
        float v[24];
        for (int k = 0; k < 6; ++k) { // (the triangle, then its last corner thrice: nothing)
            const int c = k < 3 ? k : 2;
            v[k * 4 + 0] = s.x[c], v[k * 4 + 1] = s.y[c], v[k * 4 + 2] = s.u[c], v[k * 4 + 3] = s.v[c];
        }
        dev->drawQuad(glassTexture, v, proj.data(), rgba);
    }
}

void Viewer::spawnEffect(const std::string& name, const float at[3]) {
    BattleEffect fx;
    fx.fig = std::make_unique<ViewFigure>();
    std::string err;
    const float keep = time;
    if (!loadFigure(name + ".geom", *fx.fig, &err)) {
        SDL_Log("effect %s: %s", name.c_str(), err.c_str());
        return;
    }
    const auto it = std::find(fx.fig->anims.begin(), fx.fig->anims.end(), name + ".anim");
    if (it != fx.fig->anims.end()) loadAnim(*fx.fig, size_t(it - fx.fig->anims.begin()));
    time = keep;
    fx.fig->absolute = true;
    fx.start = battleClock, fx.length = fx.fig->animLoaded ? fx.fig->anim.duration() : 0.5f;
    for (int c = 0; c < 3; ++c) fx.at[c] = at[c];
    battleEffects.push_back(std::move(fx));
}

void Viewer::drawEffects(const cr3::Mat34& camera, const Mat4& vp) {
    for (size_t k = 0; k < battleEffects.size();) {
        BattleEffect& fx = battleEffects[k];
        const float t = battleClock - fx.start;
        if (t > fx.length || t < 0) {
            unload(*fx.fig);
            battleEffects.erase(battleEffects.begin() + std::ptrdiff_t(k));
            continue;
        }
        cr3::Mat34 place = camera;
        for (int c = 0; c < 3; ++c) place.m[c * 4 + 3] = fx.at[c];
        fx.fig->place = place;
        pose(*fx.fig, t, false);
        drawFigure(*fx.fig, vp.data());
        ++k;
    }
}

// one of a model's figures: its motion by the figure's own name (a parts model's j001b01_...,
// j001h01_... for the model's j001_...)
void Viewer::poseEventFigure(cr3::EventRuntime& ev, const cr3::EventModel& m, ViewFigure& f) {
    if (f.meshes.empty()) return;
    const std::string own = f.name.size() > 5 ? motionBase(f.name.substr(0, f.name.size() - 5)) : m.figure; // (its .geom's name - a costume's motions the b01 body's)
    auto mine = [&](const std::string& a) { // (the model's name for an animation, as this figure's)
        return own != m.figure && a.compare(0, m.figure.size(), m.figure) == 0 ? own + a.substr(m.figure.size()) : a;
    };
    // its motion (ChangeAnimator: relative to the base pose), else its own animation alone
    // in slot 0 (absolute: an effect's)
    const std::string want = (m.animation.empty() ? (f.name.size() > 5 ? f.name.substr(0, f.name.size() - 5) : own) : mine(m.animation)) + ".anim";
    const auto it = std::find(f.anims.begin(), f.anims.end(), want);
    if (SDL_getenv("CR3_EVTANIM") && (!f.animLoaded || f.anims[f.animIndex] != want)) // (debugging)
        SDL_Log("event figure %s: %s %s", own.c_str(), want.c_str(), it == f.anims.end() ? "MISSING" : "ok");
    if (it != f.anims.end() && (!f.animLoaded || f.anims[f.animIndex] != want)) {
        const float keep = time;
        loadAnim(f, size_t(it - f.anims.begin()));
        time = keep;
    }
    f.absolute = m.animation.empty();
    // a ChangeAnimatorBlend's fade from the motion before
    f.blendWeight = 1;
    if (m.blendSeconds > 0 && m.animTime + eventLead() < m.blendSeconds && !m.blendFrom.empty()) {
        const std::string from = mine(m.blendFrom) + ".anim";
        if (from != f.blendName) {
            std::vector<uint8_t> d;
            f.blendName = from;
            f.blendLoaded = archive.read(from, d) && f.blendAnim.parse(d) && f.blendAnim.joints() > 0 && f.blendAnim.joints() <= int(f.skeleton.basePose.size());
        }
        f.blendAbsolute = m.blendFrom == m.figure;
        f.blendTime = m.blendFromTime + m.animTime + eventLead();
        f.blendWeight = (m.animTime + eventLead()) / m.blendSeconds;
    }
    // EvtModel::ChangeTexture_Face: the j<NNN>face material's colour texture
    const auto face = ev.faces().find(m.face);
    const uint32_t faceMaterial = cr3::nameHash(("j" + m.figure.substr(1, 3) + "face").c_str());
    // ChangeModelColor / ChangeVariation: its variant's textures on every material
    const std::string colour = m.color < 0 || m.figure.size() < 4 ? std::string() : "m" + m.figure.substr(1, 3) + "abcd"[m.color] + "01";
    cr3::Texture* ct = colour.empty() ? nullptr : texture(colour);
    cr3::Texture* cn = colour.empty() ? nullptr : texture(colour + "n");
    figureVariant = m.variation;
    for (ViewMesh& vm : f.meshes) {
        vm.texture = face != ev.faces().end() && vm.baseMaterial.hash == faceMaterial ? texture(face->second)
                     : ct                                                              ? ct
                                                                                       : texture(variantOf(vm.baseMaterial.diffuse()));
        vm.normalMap = cn ? cn : texture(variantOf(vm.baseMaterial.texture(0x35))); // (else its own)
    }
    figureVariant.clear();
    f.place = ev.scene().placeAt(m, eventLead());
    for (int k = 0; k < 3; ++k) f.tint[k] = m.tint[k]; // (ChangeColor)
    f.tint[3] = m.alpha(eventLead()); // (ChangeAlpha)
    pose(f, m.motionTime(eventLead()), m.loop);
}

auto Viewer::uiFig(const std::string& name) -> UiFig& {
    UiFig& u = uiFigs[name];
    if (!u.loaded) {
        u.loaded = true;
        std::string err;
        const float keep = time;
        loadingUi = true; // (the menus' parts as built: Smooth Models is the world's)
        if (!loadFigure(name + ".geom", u.fig, &err)) SDL_Log("ui %s: %s", name.c_str(), err.c_str());
        loadingUi = false;
        time = keep;
        std::vector<uint8_t> d;
        if (archive.read(name + ".name", d)) u.joints = cr3::parseJointNames(d), u.materials = cr3::parseMaterialNames(d);
        std::vector<uint32_t> hashes; // (the materials' names by their hashes, not the file's order)
        for (const ViewMesh& vm : u.fig.meshes)
            if (vm.source.material < 4096) {
                if (hashes.size() <= vm.source.material) hashes.resize(vm.source.material + 1);
                hashes[vm.source.material] = vm.baseMaterial.hash;
            }
        if (!u.materials.empty() && !hashes.empty()) u.materials = cr3::orderNamesByHash(u.materials, hashes);
    }
    return u;
}

void Viewer::poseUi(UiFig& u, const std::string& anim, float t, bool loop) {
    // (the figure's own animation - SetParameterDataBase's, alone in slot 0 - is absolute;
    // the animators added beside it relative)
    u.fig.absolute = anim + ".geom" == u.fig.name;
    const auto it = std::find(u.fig.anims.begin(), u.fig.anims.end(), anim + ".anim");
    if (it != u.fig.anims.end() && (!u.fig.animLoaded || u.fig.anims[u.fig.animIndex] != *it)) {
        const float keep = time;
        loadAnim(u.fig, size_t(it - u.fig.anims.begin()));
        time = keep;
    }
    pose(u.fig, t, loop);
}

// the right edge of a posed figure's meshes (UI units: their vertices as last posed)
float Viewer::uiRight(const UiFig& u) {
    float r = -1e9f;
    for (const ViewMesh& vm : u.fig.meshes) {
        if (vm.hidden) continue;
        for (size_t o = 0; o + 12 <= vm.vertices.size(); o += cr3::kVertexStride) {
            float x;
            std::memcpy(&x, &vm.vertices[o], 4);
            r = std::max(r, x);
        }
    }
    return r;
}

// a posed joint's place in UI units (false: none)
bool Viewer::uiJoint(const UiFig& u, const std::string& joint, float& x, float& y) {
    const auto it = std::find(u.joints.begin(), u.joints.end(), joint);
    if (it == u.joints.end() || !u.fig.skinned) return false;
    std::vector<cr3::JointTransform> p = u.fig.skeleton.basePose;
    if (!u.fig.posed.empty()) p = u.fig.posed; // (where its animation has it now)
    else if (u.fig.animLoaded) p = u.fig.anim.poseOver(u.fig.skeleton.basePose, 0);
    const std::vector<cr3::Mat34> w = u.fig.skeleton.world(p, u.fig.place);
    const size_t j = size_t(it - u.joints.begin());
    if (j >= w.size()) return false;
    x = w[j].m[3], y = w[j].m[7];
    return true;
}

void Viewer::loadDotNum() {
    if (!dotNumGlyphs.empty()) return;
    // (DotNumParamPackTable: tools/gen_dotnum_table.py -> gfx/dotnumtable.inc)
    for (const auto& g : kDotNum) dotNumGlyphs.push_back({g[0], g[1], g[2], g[3], g[4], g[5]});
}

void Viewer::drawDigits(int value, int set, int digits, float x, float y, const Mat4& proj, float alpha, float scale, bool zeros) {
    loadDotNum();
    cr3::Texture* tex = texture("hud_dotnum", false);
    if (!tex) return;
    int place = 1;
    for (int i = 0; i < digits; ++i, place *= 10) {
        const int d = (i == 0 && value == 0) || zeros ? value / place % 10 : value < place ? -1 : value / place % 10; // (Cr3UtilGetNumberDisplay; SetZeroNumber: padded)
        if (d < 0) continue;
        if (size_t(set + d) >= dotNumGlyphs.size()) continue;
        const Glyph& g = dotNumGlyphs[size_t(set + d)];
        const float w = g.w / 1000.0f * scale, h = g.h / 1000.0f * scale;
        const float cx = x - float(i) * w;
        const float x0 = cx - w / 2, x1 = cx + w / 2, y0 = y - h / 2, y1 = y + h / 2;
        const float v[24] = {x0, y0, g.u0, g.v1, x1, y0, g.u1, g.v1, x1, y1, g.u1, g.v0,
                             x0, y0, g.u0, g.v1, x1, y1, g.u1, g.v0, x0, y1, g.u0, g.v0};
        const float rgba[4] = {1, 1, 1, alpha};
        dev->drawQuad(*tex, v, proj.data(), rgba);
    }
}

// PartsBase::SetVisible(joint, v): the joint's meshes (their palette's first)
void Viewer::showJoint(UiFig& u, const std::string& joint, bool visible) {
    for (ViewMesh& vm : u.fig.meshes) {
        const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
        if (j < u.joints.size() && u.joints[j] == joint) vm.hidden = !visible;
    }
}

void Viewer::hideJoint(UiFig& u, const std::string& joint) { showJoint(u, joint, false); }

// a circle button drawn at `id`: lit if it has the focus (else as its material is)
void Viewer::focusLook(UiFig& c, int id) {
    const bool on = focusScreen() && id == keyFocus;
    for (ViewMesh& vm : c.fig.meshes) {
        const uint32_t mi = vm.source.material;
        if (mi >= c.materials.size() || c.materials[mi] != "PRG_shiftR") continue;
        vm.material.params[0x5e] = on ? std::vector<float>{0.5f, 0.0f} : vm.baseMaterial.params[0x5e];
        if (vm.material.params[0x5e].empty()) vm.material.params.erase(0x5e);
    }
}

// BattleWindowEnchant2::SetSign: sign n (EnchantParamPackTable's u0, v0, u1, v1 of
// hud_enchant) a 0.048-unit square at (x, y) - 1 white "i" (no element), 2 red, 3 cyan, 4 green
// (the elements 1..3), 25 the clock, the others the states
void Viewer::drawEnchant(int sign, float x, float y, const Mat4& proj) {
    static const float kEnchant[26][4] = {
        {0, 0, 0, 0},
        {0.188f, 1, 0.375f, 0.813f}, {0.375f, 1, 0.563f, 0.813f}, {0.563f, 1, 0.75f, 0.813f}, {0.75f, 1, 0.938f, 0.813f},
        {0, 0.813f, 0.188f, 0.625f}, {0.188f, 0.813f, 0.375f, 0.625f}, {0.375f, 0.813f, 0.563f, 0.625f},
        {0.563f, 0.813f, 0.75f, 0.625f}, {0.75f, 0.813f, 0.938f, 0.625f},
        {0, 0.625f, 0.188f, 0.438f}, {0.188f, 0.625f, 0.375f, 0.438f}, {0.375f, 0.625f, 0.563f, 0.438f},
        {0.563f, 0.625f, 0.75f, 0.438f}, {0.75f, 0.625f, 0.938f, 0.438f},
        {0, 0.438f, 0.188f, 0.25f}, {0.188f, 0.438f, 0.375f, 0.25f}, {0.375f, 0.438f, 0.563f, 0.25f},
        {0.563f, 0.438f, 0.75f, 0.25f}, {0.75f, 0.438f, 0.938f, 0.25f},
        {0, 0.25f, 0.188f, 0.062f}, {0.188f, 0.25f, 0.375f, 0.062f}, {0.375f, 0.25f, 0.563f, 0.062f},
        {0.563f, 0.25f, 0.75f, 0.062f}, {0.75f, 0.25f, 0.938f, 0.062f},
        {0, 1, 0.188f, 0.813f}};
    cr3::Texture* tex = texture("hud_enchant", false);
    if (!tex || sign <= 0 || sign >= 26) return;
    const float* g = kEnchant[sign];
    const float s = 0.024f, x0 = x - s, x1 = x + s, y0 = y - s, y1 = y + s;
    const float v[24] = {x0, y0, g[0], g[3], x1, y0, g[2], g[3], x1, y1, g[2], g[1],
                         x0, y0, g[0], g[3], x1, y1, g[2], g[1], x0, y1, g[0], g[1]};
    const float rgba[4] = {1, 1, 1, 1};
    dev->drawQuad(*tex, v, proj.data(), rgba);
}

// StatusId2::SetSign: label n of hud_statusAI (StatusIdParamPackTable: its width and height in
// pixels of a 1000-a-unit layout, u0, v0, u1, v1), from (x, y) on - 1 "HP", 2 "MP", 3 / 7 the
// status window's others
void Viewer::drawStatusSign(int n, float x, float y, const Mat4& proj) {
    static const float kStatus[23][6] = {
        {0, 0, 0, 0, 0, 0}, {40, 24, 0, 1, 0.156f, 0.906f}, {40, 24, 0, 0.906f, 0.156f, 0.813f},
        {72, 24, 0, 0.813f, 0.281f, 0.719f}, {72, 24, 0, 0.719f, 0.281f, 0.625f},
        {24, 24, 0.438f, 1, 0.531f, 0.906f}, {24, 24, 0.438f, 0.906f, 0.531f, 0.813f},
        {48, 24, 0.313f, 0.813f, 0.5f, 0.719f}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
        {48, 24, 0.188f, 0.531f, 0.375f, 0.437f}, {112, 24, 0.063f, 0.438f, 0.5f, 0.344f},
        {96, 24, 0.094f, 0.344f, 0.469f, 0.25f}, {60, 24, 0.648f, 1, 0.883f, 0.906f},
        {120, 24, 0.531f, 0.906f, 1, 0.813f}, {120, 24, 0.531f, 0.813f, 1, 0.719f},
        {120, 24, 0.531f, 0.719f, 1, 0.625f}, {120, 24, 0.531f, 0.625f, 1, 0.531f},
        {120, 24, 0.531f, 0.531f, 1, 0.438f}, {120, 24, 0.531f, 0.438f, 1, 0.344f},
        {120, 24, 0.531f, 0.344f, 1, 0.25f}, {112, 32, 0.313f, 0.25f, 0.75f, 0.125f},
        {256, 32, 0, 0.125f, 1, 0}};
    cr3::Texture* tex = texture("hud_statusAI", false);
    if (!tex || n <= 0 || n >= 23 || kStatus[n][0] <= 0) return;
    const float* g = kStatus[n];
    const float x0 = x, x1 = x + g[0] / 1000.0f, y0 = y - g[1] / 2000.0f, y1 = y + g[1] / 2000.0f;
    const float v[24] = {x0, y0, g[2], g[5], x1, y0, g[4], g[5], x1, y1, g[4], g[3],
                         x0, y0, g[2], g[5], x1, y1, g[4], g[3], x0, y1, g[2], g[3]};
    const float rgba[4] = {1, 1, 1, 1};
    dev->drawQuad(*tex, v, proj.data(), rgba);
}

// a fighter's element sign (BtlInterfaceCtrl::GetCharaUIState: elements 1..3 bits 1..3, any
// other bit 0 - SetStatusIcon's bit k sign k + 1)
int Viewer::elementSign(const cr3::Fighter& f) { return f.element >= 1 && f.element <= 3 ? f.element + 1 : 1; }

// its states' signs after the element's (GetCharaUIState: 0x536648's states as bits - poison 4,
// blind 5, seal 6, 12 7, 35 9, 30 10, 29 11, 33 12, raised Attack .. Luck 14..18, lowered 19..23,
// time stopped 24; 11 or 32 bit 8 - the sign the bit + 1, in the bits' order)
std::vector<int> Viewer::stateSigns(const cr3::Fighter& f) {
    static const int kStates[19] = {0, 1, 2, 12, 35, 30, 29, 33, 5, 6, 7, 8, 9, 13, 14, 15, 16, 17, 21};
    static const int kBits[19] = {4, 5, 6, 7, 9, 10, 11, 12, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    uint32_t bits = 0;
    for (int k = 0; k < 19; ++k)
        if (f.hasState(kStates[k])) bits |= 1u << kBits[k];
    if (f.hasState(11) || f.hasState(32)) bits |= 1u << 8;
    std::vector<int> out;
    for (int b = 4; b < 32; ++b)
        if (bits >> b & 1u) out.push_back(b + 1);
    return out;
}

float Viewer::uiTextWidth(const std::string& text, float size, int w, int h) {
    if (text.empty() || !font.loaded()) return 0;
    const float k = std::min(float(w) / 0.96f, float(h) / 0.64f);
    const int px = std::max(1, int(size * uiTextScale / 1000.0f * k + 0.5f));
    int tw = 0, th = 0;
    const std::string key = text + "\n" + std::to_string(px);
    if (auto it = uiTexts.find(key); it != uiTexts.end()) return it->second.w;
    font.render(text, px, tw, th);
    return float(tw) * (size * uiTextScale / 1000.0f / float(px));
}

// (its width, UI units: 0 none drawn)
float Viewer::drawUiText(const std::string& text, float size, float x, float y, int w, int h, bool left, float shade, float maxWidth, float border) {
    if (text.empty() || !font.loaded()) return 0;
    const float k = std::min(float(w) / 0.96f, float(h) / 0.64f);
    const int px = std::max(1, int(size * uiTextScale / 1000.0f * k + 0.5f));
    const std::string key = text + "\n" + std::to_string(px);
    auto it = uiTexts.find(key);
    if (it == uiTexts.end()) {
        TextQuad q;
        int tw = 0, th = 0;
        cr3::Image img = font.render(text, px, tw, th);
        if (img.rgba.empty()) return 0;
        q.tex = dev->upload(img);
        const float unitsPerPx = size * uiTextScale / 1000.0f / float(px);
        q.w = float(tw) * unitsPerPx, q.h = float(th) * unitsPerPx, q.below = float(img.height - th) * unitsPerPx;
        it = uiTexts.emplace(key, q).first;
    }
    it->second.used = ++uiTextUses;
    cr3::trimLeastUsed(uiTexts, kUiTextLimit, [this](TextQuad& t) { dev->destroy(t.tex); });
    const TextQuad& q = it->second;
    const float qw = maxWidth > 0 ? std::min(q.w, maxWidth) : q.w;
    const float x0 = left ? x : x - qw / 2, x1 = x0 + qw, y0 = y - q.h / 2 - q.below, y1 = y + q.h / 2;
    const float v[24] = {x0, y0, 0, 1, x1, y0, 1, 1, x1, y1, 1, 0, x0, y0, 0, 1, x1, y1, 1, 0, x0, y1, 0, 0};
    const Mat4 proj = uiProjection(w, h);
    if (border > 0) { // ProvisionalMenuText type 1: eight black copies around white text.
        static const float directions[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
            {0.7071f, 0.7071f}, {-0.7071f, 0.7071f}, {0.7071f, -0.7071f}, {-0.7071f, -0.7071f}};
        const float black[4] = {0, 0, 0, 1};
        for (const auto& d : directions) {
            float outline[24];
            for (int k = 0; k < 24; k += 4) {
                outline[k] = v[k] + d[0] * border, outline[k + 1] = v[k + 1] + d[1] * border;
                outline[k + 2] = v[k + 2], outline[k + 3] = v[k + 3];
            }
            dev->drawQuad(q.tex, outline, proj.data(), black);
        }
    }
    const float rgba[4] = {shade, shade, shade, 1};
    dev->drawQuad(q.tex, v, proj.data(), rgba);
    return qw;
}

// a point of the battle on the screen in UI units (false: behind the camera)
bool Viewer::battleUi(const float* p, float& x, float& y) const {
    const float* mm = battleVP.data();
    const float cw = mm[3] * p[0] + mm[7] * p[1] + mm[11] * p[2] + mm[15];
    if (cw <= 0) return false;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    x = (mm[0] * p[0] + mm[4] * p[1] + mm[8] * p[2] + mm[12]) / cw * float(frameW) * 0.5f / k;
    y = (mm[1] * p[0] + mm[5] * p[1] + mm[9] * p[2] + mm[13]) / cw * float(frameH) * 0.5f / k;
    return true;
}

// the command menu (BattleFirstMenu / BattleSoloMenu / BattleSelectMenu: btlBtn and its
// animator, a circleBtn_fr at each call_circleBtn_frNN joint - its frame NN)
// `shown`: the buttons to draw (an empty list: every one); each drawn button's tap area kept
// (tapAreaS01 / tapAreaE01) for clicks; `labels` text at a button's typeM_C01 (the targets')
void Viewer::drawCommandMenu(const std::string& anim, float t, const Mat4& view, const std::vector<int>& shown,
                     const std::map<int, std::string>& labels, int w, int h, int frameOf,
                     const std::string& figure) {
    UiFig& b = uiFig(figure);
    b.fig.place = cr3::Mat34();
    poseUi(b, anim, t);
    drawFigure(b.fig, view.data());
    for (const std::string& j : b.joints) {
        if (j.rfind("call_circleBtn_fr", 0) != 0) continue;
        const int n = std::atoi(j.c_str() + 17);
        if (const char* only = SDL_getenv("CR3_MENU_ONLY"); only && std::atoi(only) != n) continue; // (debugging)
        if (!shown.empty() && std::find(shown.begin(), shown.end(), n) == shown.end()) continue;
        float x, y;
        if (!uiJoint(b, j, x, y)) continue;
        UiFig& c = uiFig("circlebtn_fr");
        c.fig.place = cr3::Mat34();
        c.fig.place.m[3] = x, c.fig.place.m[7] = y;
        poseUi(c, "circlebtn_fr", float(frameOf ? frameOf : n) / 20.0f);
        // CircleBtn::ChangeTapButtonColor: focused (2) PRG_shiftR's texture half across
        // (OffsetSet1 U 0.5), not selectable (0.5) every material's colour at half
        const int look = buttonLook ? buttonLook(n) : focusScreen() && n == keyFocus ? 2 : 1;
        for (ViewMesh& vm : c.fig.meshes) {
            const uint32_t mi = vm.source.material;
            if (mi < c.materials.size() && c.materials[mi] == "PRG_shiftR") vm.material.params[0x5e] = {look == 2 ? 0.5f : 0.0f, 0.0f};
            vm.material.params[0x33] = look == 0 ? std::vector<float>{0.5f, 0.5f, 0.5f, 1.0f} : vm.baseMaterial.params[0x33];
            if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
        }
        drawFigure(c.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) tapRects.push_back({n, sx, sy, ex, ey});
        const auto lt = labels.find(n);
        float tx, ty;
        if (lt != labels.end() && w > 0 && uiJoint(c, "typeM_C01", tx, ty)) drawUiText(lt->second, 24, tx, ty, w, h);
        float ax, ay;
        if (buttonLevel && w > 0 && uiJoint(c, "call_dotNum_id30", ax, ay)) drawDigits(buttonLevel(n), 120, 3, ax, ay, uiProjection(w, h));
        if (buttonFighter) { // a target's (CircleBtn::SetLevelNumbers / SetHPGauge)
            const int f = buttonFighter(n);
            if (f >= 0 && w > 0) {
                const cr3::Fighter& fi = battleRules->fighter(f);
                float lx, ly;
                if (uiJoint(c, "call_dotNum_id30", lx, ly)) drawDigits(fi.level, 120, 3, lx, ly, uiProjection(w, h));
                if (uiJoint(c, "call_enchant_id00", lx, ly)) drawEnchant(elementSign(fi), lx, ly, uiProjection(w, h)); // (CircleBtn::SetStatusIcon's first)
                if (uiJoint(c, "call_enchant_id01", lx, ly)) { // (its states from id01 on, 0.04 apart)
                    const std::vector<int> signs = stateSigns(fi);
                    for (size_t k = 0; k < signs.size() && k < 15; ++k) drawEnchant(signs[k], lx + 0.04f * float(k), ly, uiProjection(w, h));
                }
                float hx, hy;
                if (uiJoint(c, "call_hpBar_per20", hx, hy)) {
                    UiFig& bar = uiFig("btlhpbar_per");
                    bar.fig.place = cr3::Mat34();
                    bar.fig.place.m[3] = hx, bar.fig.place.m[7] = hy;
                    poseUi(bar, "btlhpbar_per", fi.hp > 0 ? std::max(float(fi.hp) * 5.0f / float(std::max(fi.maxHp, 1)), 0.07f) : 0.0f);
                    drawFigure(bar.fig, view.data());
                }
            }
        }
    }
}

// the rows centred on the focused one (ScrollListMenu::PartsPose: two either side shown; the
// row's own place the one above it), the list's tab (ListTag: tabWin_io, its tabName_id at
// call_tabName_id posed at the list's frame / 20 s)
// `tagFrame` / `costIcon`: BattleSkillListMenu::SetListItemParam's by the list - the sopia 9
// ("Sopia") / 12 ("MP"), the awakenings 11 ("Awake") / 13 ("Charge Lv."), the advents 13 / 13
// (as the frames read). A row's icon is its command's +0x72 - mainicon_id at icon / 20 s, but
// for two pairs whose frames lie swapped: 28 / 29, and 70 (the awakenings': its frame "Awake",
// as the reference video shows) / 71 (the advents')
void Viewer::drawSkillList(const std::vector<ListRow>& rows, int focused, float t, const Mat4& view, int w, int h, int tagFrame, int costIcon) {
    UiFig& bar = uiFig("scrlbar");
    bar.fig.place = cr3::Mat34();
    poseUi(bar, "scrlbar_io", t);
    drawFigure(bar.fig, view.data());
    int fi = 0;
    for (size_t k = 0; k < rows.size(); ++k)
        if (rows[k].id == focused) fi = int(k);
    // its thumb (scrlMtr_per, in place: SetScrollBarLength - posed at the part of the list
    // shown, 5 rows, / 20 s by percent; SetScrollBarPosition - lower by the rows above the first
    // shown, of those that do not fit, between the bar's barLimitUp and barLimitLow)
    float ux, uy, lx, ly;
    if (uiJoint(bar, "barLimitUp", ux, uy) && uiJoint(bar, "barLimitLow", lx, ly)) {
        const int n = std::max(int(rows.size()), 1), first = std::clamp(fi - 2, 0, std::max(n - 5, 0));
        const float shown = std::min(5.0f / float(n), 1.0f);
        UiFig& th = uiFig("scrlmtr_per");
        th.fig.place = cr3::Mat34();
        th.fig.place.m[7] = n > 5 ? -(uy - ly) * (1.0f - shown) * float(first) / float(n - 5) : 0.0f;
        poseUi(th, "scrlmtr_per", shown * 100.0f / 20.0f);
        drawFigure(th.fig, view.data());
    }
    // (ScrollItemInitialize / SetLIstBottomItem: four rows or more between two end items - before
    // the first scrlLow, its tap the last row; after the last scrlTop, its tap the first -,
    // fewer without them)
    const int n = int(rows.size());
    const bool ends = n >= 4;
    for (int d = -2; d <= 2; ++d) {
        const int k = fi + d;
        if (k < (ends ? -1 : 0) || k > (ends ? n : n - 1)) continue;
        if (k < 0 || k >= n) {
            const std::string fig = k < 0 ? "scrllow" : "scrltop";
            UiFig& e = uiFig(fig);
            e.fig.place = cr3::Mat34();
            e.fig.place.m[7] = -0.08f * float(d);
            poseUi(e, fig + "_io", t);
            drawFigure(e.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(e, "tapAreaS01", sx, sy) && uiJoint(e, "tapAreaE01", ex, ey)) tapRects.push_back({1000 + (k < 0 ? n - 1 : 0), sx, sy, ex, ey});
            continue;
        }
        const ListRow& r = rows[size_t(k)];
        UiFig& b = uiFig("scrlbtn");
        b.fig.place = cr3::Mat34();
        b.fig.place.m[7] = -0.08f * float(d);
        poseUi(b, "scrlbtn_io", t);
        for (ViewMesh& vm : b.fig.meshes) {
            vm.material.params[0x33] = !r.selectable ? std::vector<float>{0.5f, 0.5f, 0.5f, 1.0f} : vm.baseMaterial.params[0x33];
            if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
        }
        drawFigure(b.fig, view.data());
        float x, y;
        if (uiJoint(b, "typeS_L00", x, y)) drawUiText(r.name, 24, x, y, w, h, true, r.selectable ? 1.0f : 0.5f);
        if (r.number >= 0 && uiJoint(b, "call_dotNum_id01", x, y)) drawDigits(r.number, 90, 4, x, y, uiProjection(w, h), r.selectable ? 1.0f : 0.5f);
        if (r.icon > 0 && uiJoint(b, "call_mainIcon_id11", x, y)) {
            UiFig& ic = uiFig("mainicon_id");
            ic.fig.place = cr3::Mat34();
            ic.fig.place.m[3] = x, ic.fig.place.m[7] = y;
            poseUi(ic, "mainicon_id", r.icon == 0x1c ? 1.45f : r.icon == 0x1d ? 1.4f : r.icon == 70 ? 3.55f : r.icon == 71 ? 3.5f : float(r.icon) / 20.0f);
            drawFigure(ic.fig, view.data());
        }
        if (r.seal >= 0 && uiJoint(b, "call_seal_id12", x, y)) {
            UiFig& se = uiFig("seal_id");
            se.fig.place = cr3::Mat34();
            se.fig.place.m[3] = x, se.fig.place.m[7] = y;
            poseUi(se, "seal_id", float(r.seal) / 20.0f);
            drawFigure(se.fig, view.data());
        }
        if (costIcon >= 0 && uiJoint(b, "call_cost_id13", x, y)) { // (the number's mark: "MP" ...)
            UiFig& co = uiFig("cost_id");
            co.fig.place = cr3::Mat34();
            co.fig.place.m[3] = x, co.fig.place.m[7] = y;
            poseUi(co, "cost_id", float(costIcon) / 20.0f);
            drawFigure(co.fig, view.data());
        }
        if (k == fi && uiJoint(b, "call_sBtnSlct14", x, y)) {
            UiFig& sl = uiFig("sbtnslct");
            sl.fig.place = cr3::Mat34();
            sl.fig.place.m[3] = x, sl.fig.place.m[7] = y;
            poseUi(sl, "sbtnslct", t);
            drawFigure(sl.fig, view.data());
        }
        float sx, sy, ex, ey; // (a tap: the focused row's pick, another's focus - its id + 1000)
        if (uiJoint(b, "tapAreaS01", sx, sy) && uiJoint(b, "tapAreaE01", ex, ey))
            tapRects.push_back({k == fi ? (r.selectable ? r.id : -1) : 1000 + k, sx, sy, ex, ey});
    }
    // the focused row's help (scrlWin: its lines at typeS_L01, typeS_L02)
    if (fi < int(rows.size()) && !rows[size_t(fi)].help.empty()) {
        UiFig& win = uiFig("scrlwin");
        win.fig.place = cr3::Mat34();
        poseUi(win, "scrlwin_io", t);
        drawFigure(win.fig, view.data());
        // (BattleSkillListMenu::Update: SetStringData - near black, 0.01; within the window)
        std::string s = rows[size_t(fi)].help; // (its break the text's "\\n")
        const float right = uiRight(win) - 0.03f;
        float x, y;
        // (CR3-U's own longer lines - the settings' -: broken at the last word that fits the window)
        static std::map<std::string, std::string> wrapped; // (by the text and the screen's size)
        if (s.find("\\n") == std::string::npos && uiJoint(win, "typeS_L01", x, y)) {
            const std::string key = s + "\n" + std::to_string(w) + "x" + std::to_string(h);
            auto it = wrapped.find(key);
            if (it == wrapped.end()) {
                std::string out = s;
                if (uiTextWidth(s, 26, w, h) > right - x) {
                    size_t cut = std::string::npos; // (the space whose two lines' wider is the narrowest)
                    float best = 1e9f;
                    for (size_t at = s.find(' '); at != std::string::npos; at = s.find(' ', at + 1)) {
                        const float wider = std::max(uiTextWidth(s.substr(0, at), 26, w, h), uiTextWidth(s.substr(at + 1), 26, w, h));
                        if (wider < best) best = wider, cut = at;
                    }
                    if (cut != std::string::npos) out = s.substr(0, cut) + "\\n" + s.substr(cut + 1);
                }
                it = wrapped.emplace(key, out).first;
            }
            s = it->second;
        }
        const size_t br = s.find("\\n");
        if (uiJoint(win, "typeS_L01", x, y)) drawUiText(s.substr(0, br), 26, x, y, w, h, true, 0.01f, right - x);
        if (br != std::string::npos && uiJoint(win, "typeS_L02", x, y)) drawUiText(s.substr(br + 2), 26, x, y, w, h, true, 0.01f, right - x);
    }
    UiFig& tag = uiFig("tabwin_io");
    tag.fig.place = cr3::Mat34();
    poseUi(tag, "tabwin_io", t);
    if (tagFrame >= 0) drawFigure(tag.fig, view.data()); // (none for the manual's)
    float x, y;
    if (tagFrame >= 0 && uiJoint(tag, "call_tabName_id01", x, y)) {
        UiFig& tn = uiFig("tabname_id");
        tn.fig.place = cr3::Mat34();
        tn.fig.place.m[3] = x, tn.fig.place.m[7] = y;
        poseUi(tn, "tabname_id", float(tagFrame) / 20.0f);
        drawFigure(tn.fig, view.data());
    }
    UiFig& bk = uiFig("scrlback");
    bk.fig.place = cr3::Mat34();
    poseUi(bk, "scrlback_io", t);
    drawFigure(bk.fig, view.data());
    float sx, sy, ex, ey;
    if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
}

void Viewer::drawChoosing(const Mat4& view, int w, int h) {
    using CI = cr3::CommandInput;
    tapRects.clear();
    const float t = battleClock - menuStart;
    const std::vector<int> c = menuChoices();
    const int cursor = c.empty() ? -1 : c[size_t(std::clamp(menuCursor, 0, int(c.size()) - 1))];
    if (commandInput.menu() == CI::Menu::Target) {
        // BattleSelectMenu: a target button a target, its name on it - n of them the last n of
        // the five places (43 + 5 - n ..)
        std::vector<int> ids;
        std::map<int, std::string> names;
        const int n = std::min(int(commandInput.targets().size()), 5), first = CI::TargetFirst + 5 - n;
        for (int k = 0; k < n; ++k) {
            const int f = commandInput.targets()[size_t(k)];
            ids.push_back(first + k);
            names[first + k] = battleRules->fighter(f).name;
        }
        buttonLook = [&](int id) { return commandInput.targets()[size_t(id - first)] == cursor ? 2 : 1; };
        buttonFighter = [&](int id) { return commandInput.targets()[size_t(id - first)]; };
        drawCommandMenu("btlbtn3rd_io", t, view, ids, names, w, h, CI::TargetFirst);
        buttonLook = nullptr, buttonFighter = nullptr;
        for (TapRect& r : tapRects) r.id = commandInput.targets()[size_t(r.id - first)]; // (a click: the fighter)
    } else if (commandInput.menu() == CI::Menu::Items) {
        // the items (BattleItemListMenu: its icon, its count, the tab's frame 7)
        std::vector<ListRow> rows;
        for (const auto& e : commandInput.items()) {
            const cr3::BattleData::Item* it = battleData.item(e.item);
            rows.push_back({e.item, it ? it->name[1] : "?", e.count, it ? it->icon : 0, e.selectable, it ? it->text[1] : ""});
        }
        listRows = rows;
        drawSkillList(rows, cursor, t, view, w, h, 7, -1);
        return;
    } else if (commandInput.menu() == CI::Menu::Advent) {
        // the advents (SetCommandAdventList / AddCommandAdvent: the charge level its number, the
        // command's +0x72 its icon) on the skill list - "Advent", "Charge Lv."
        std::vector<ListRow> rows;
        for (const auto& a : commandInput.advents()) {
            const cr3::BattleData::Command* c = a.command > 0 ? battleData.command(a.command) : nullptr;
            rows.push_back({a.command, c ? c->name[1] : "-", a.level, c ? c->icon : 71, a.selectable, c ? c->text[1] : ""});
        }
        listRows = rows;
        drawSkillList(rows, cursor, t, view, w, h, 13, 13);
        return;
    } else if (commandInput.menu() == CI::Menu::Awake || commandInput.menu() == CI::Menu::Sopia) {
        // the awakenings (AddCommandAwake: the level its number) or the sopia (AddCommandSopia:
        // its MP) on the skill list
        std::vector<ListRow> rows;
        if (commandInput.menu() == CI::Menu::Awake)
            for (const auto& a : commandInput.awakenings()) {
                const cr3::BattleData::Command* c = battleData.command(a.command);
                rows.push_back({a.command, c ? c->name[1] : "?", a.level, c ? c->icon : 70, a.selectable, c ? c->text[1] : ""});
            }
        else
            for (const auto& sc : commandInput.sopiaCommands()) {
                const cr3::BattleData::Command* c = battleData.command(sc.command);
                rows.push_back({sc.command, c ? c->name[1] : "?", sc.mp, c ? c->icon : 0, sc.selectable, c ? c->text[1] : ""});
            }
        listRows = rows;
        const bool sopia = commandInput.menu() == CI::Menu::Sopia;
        drawSkillList(rows, cursor, t, view, w, h, sopia ? 9 : 11, sopia ? 12 : 13);
        return; // (its own back button)
    } else {
        std::vector<int> ids;
        for (const auto& b : commandInput.buttons()) ids.push_back(b.id);
        buttonLook = [&](int id) {
            for (const auto& b : commandInput.buttons())
                if (b.id == id) return !b.selectable ? 0 : id == cursor && keysUsed ? 2 : 1;
            return 1;
        };
        drawCommandMenu(commandInput.menu() == CI::Menu::First ? "btlbtn1st_io" : "btlbtn2nd_io", t, view, ids);
        buttonLook = nullptr;
        for (TapRect& r : tapRects) // (the ones not selectable take no tap)
            for (const auto& b : commandInput.buttons())
                if (b.id == r.id && !b.selectable) r.id = -1;
    }
    // the back button (btlBack, fit 3) past the first menu
    if (commandInput.menu() != CI::Menu::First) {
        UiFig& bk = uiFig("btlback");
        bk.fig.place = cr3::Mat34();
        poseUi(bk, "btlback_in", t);
        drawFigure(bk.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
    }
}

void Viewer::drawStatusWindows(const StatusShown* rows, int count, float t, const Mat4& view, const Mat4& proj, bool twoAlways) {
    const bool one = count == 1 && !twoAlways;
    UiFig& win = uiFig("campwin");
    win.fig.place = cr3::Mat34();
    poseUi(win, one ? "campwin1_io" : "campwin2_io", t);
    for (ViewMesh& vm : win.fig.meshes) { // (the parts the window does not use: their joints' meshes)
        const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
        const std::string jn = j < win.joints.size() ? win.joints[j] : "";
        vm.hidden = one ? jn == "GEO_campWin1" || jn == "GEO_campWin2" : jn == "GEO_campWin3";
    }
    drawFigure(win.fig, view.data());
    auto at = [&](const std::string& joint, const std::string& fig, float pose, bool draw = true) -> UiFig* {
        float x, y;
        if (!uiJoint(win, joint, x, y)) return nullptr;
        UiFig& c = uiFig(fig);
        c.fig.place = cr3::Mat34();
        c.fig.place.m[3] = x, c.fig.place.m[7] = y;
        poseUi(c, fig, pose);
        if (draw) drawFigure(c.fig, view.data());
        return &c;
    };
    float x, y;
    for (int k = 0; k < count && k < 2; ++k) {
        const StatusShown& r = rows[k];
        const std::string b = one ? "900" : k == 0 ? "0" : "5";
        if (UiFig* face = at(one ? std::string("call_cmpFace_id9000") : "call_cmpFace_id" + b + "00", "campface", 0, false)) {
            const std::string id = std::to_string(r.id);
            for (ViewMesh& vm : face->fig.meshes) vm.texture = texture("cmpFaceC00" + id), vm.extraMap = texture("cmpFaceA00" + id);
            drawFigure(face->fig, view.data());
        }
        const std::string p = one ? "90" : b; // (the lone one's joints: id9021, fr9001 ...)
        at("call_pcNameEN_id" + p + "21", "pcnameen_id", float(r.id) / 20.0f);
        for (int s : {1, 2, 3, 7})
            if (uiJoint(win, "call_status_fr" + p + "0" + std::to_string(s), x, y)) drawStatusSign(s, x, y, proj);
        const float dx = k == 0 ? 0.0f : 0.01f;
        auto num = [&](const std::string& id, int value, int set, int digits, float off) {
            float nx, ny;
            if (uiJoint(win, "call_dotNum_id" + id, nx, ny)) drawDigits(value, set, digits, nx + off, ny, proj);
        };
        if (one) { // (its level, HP and MP alone)
            num("9031", r.level, 130, 3, 0);
            num("9041", r.hp, 110, 5, 0);
            num("9061", r.mp, 110, 4, 0);
        } else {
            const char* const ids[2][8] = {{"031", "041", "100", "051", "061", "200", "071", "081"},
                                           {"531", "541", "500", "551", "561", "600", "571", "581"}};
            num(ids[k][0], r.level, 130, 3, 0);
            num(ids[k][1], r.hpMax, 110, 5, dx); // (SetParameterNumberID: the first the most, on the right)
            num(ids[k][2], 0, 164, 1, dx);
            num(ids[k][3], r.hp, 110, 5, dx);
            num(ids[k][4], r.mpMax, 110, 4, dx);
            num(ids[k][5], 0, 164, 1, dx);
            num(ids[k][6], r.mp, 110, 4, dx);
            num(ids[k][7], r.next, 110, 6, dx);
        }
        // the bars (their joints in order: the first's, the second's; the lone one's its own)
        const float hpT = r.hp > 0 ? std::max(float(r.hp) * 5.0f / float(std::max(r.hpMax, 1)), 0.07f) : 0.0f;
        const float mpT = r.mpMax > 0 ? float(r.mp) * 5.0f / float(r.mpMax) : 0.0f;
        const struct { const char* prefix; const char* fig; float pose; } bars[3] = {
            {"call_cmpHpBar_per", "cmphpbar_per", hpT}, {"call_cmpMpBar_per", "cmpmpbar_per", mpT}, {"call_cmpNxBar_per", "cmpnxbar_per", float(r.percent) / 20.0f}};
        for (const auto& bar : bars) {
            int seen = 0;
            for (const std::string& j : win.joints) {
                if (j.rfind(bar.prefix, 0) != 0) continue;
                const bool lone = j.size() > std::strlen(bar.prefix) && j[std::strlen(bar.prefix)] == '9';
                if (lone != one) continue;
                if (seen++ == (one ? 0 : k)) at(j, bar.fig, bar.pose);
            }
        }
    }
}

void Viewer::drawResult(const Mat4& view, const Mat4& proj, int w, int h) {
    const float t = battleClock - result.windowStart;
    auto part = [&](const std::string& fig, const std::string& anim, const char* hide = nullptr) -> UiFig& {
        UiFig& u = uiFig(fig);
        u.fig.place = cr3::Mat34();
        poseUi(u, anim, t);
        if (hide) // (a part the window does not use: its joint's meshes)
            for (ViewMesh& vm : u.fig.meshes) {
                const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
                vm.hidden = j < u.joints.size() && u.joints[j] == hide;
            }
        drawFigure(u.fig, view.data());
        return u;
    };
    part("menu", "menurslt_io");
    UiFig& seat = part("seatrslt_io", "seatrslt_io");
    float x, y;
    if (battleClock >= result.transferStart) {
        if (uiJoint(seat, "call_dotNum_id01", x, y)) drawDigits(result.exp, 90, 6, x, y, proj);
        if (uiJoint(seat, "call_dotNum_id11", x, y)) drawDigits(result.sp, 90, 4, x, y, proj);
    }
    StatusShown rows[2];
    int count = 0;
    for (int k = 0; k < 2; ++k) {
        const ResultMember& r = result.member[k];
        if (r.ch < 0) continue;
        const ResultNow n = resultNow(r);
        rows[count++] = {r.id, n.level, n.hp, r.hpMax, r.mp, r.mpMax, n.next, n.percent};
    }
    drawStatusWindows(rows, count, t, view, proj, true);
    UiFig& sys = part("syswin_io", "syswin_io");
    const std::string& text = result.message >= 0 && result.message < int(result.messages.size()) ? result.messages[size_t(result.message)] : announce(16);
    if (uiJoint(sys, "typeM_L01", x, y)) drawUiText(text, 24, x, y, w, h, true);
}

void Viewer::drawBattleHud(int w, int h) {
    if (!(gameOver.stage > 0 && gameOver.hidden)) drawBattleHudParts(w, h);
    if (gameOver.stage == 0 || !battleMode || !battleRules) return;
    const Mat4 proj = uiProjection(w, h);
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f;
    const Mat4 view = cr3::mat4Multiply(proj, camera);
    tapRects.clear();
    drawGameOver(view, w, h);
}

void Viewer::drawBattleHudParts(int w, int h) {
    if (!battleMode || !battleRules) return;
    drawBattleHudMain(w, h);
    if (breakShown) { // (the turn break's figure, once)
        const Mat4 proj = uiProjection(w, h);
        Mat4 camera = cr3::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr3::mat4Multiply(proj, camera);
        const char* name = breakShown == 1 ? "turnbrk" : "turnbrkenemy";
        UiFig& f = uiFig(name);
        f.fig.place = cr3::Mat34();
        const float t = battleClock - breakShownAt;
        poseUi(f, name, t);
        if (!f.fig.animLoaded || t > f.fig.anim.duration()) breakShown = 0;
        else drawFigure(f.fig, view.data());
    }
}

void Viewer::drawBattleHudMain(int w, int h) {
    if (battleScript && !battleScript->statusShown()) return; // (its talk on)
    const Mat4 proj = uiProjection(w, h);
    Mat4 camera = cr3::mat4Identity();
    camera[14] = -0.881f;
    const Mat4 view = cr3::mat4Multiply(proj, camera);
    cr3::SceneLighting l;
    dev->setLighting(l);
    if (result.stage >= 3) { // (BtlInterfaceCtrl::HideBattleStatus: the window alone)
        drawResult(view, proj, w, h);
        return;
    }

    if (choosing && manualStage == 1) {
        drawManualList(view, w, h);
        return;
    }
    if (choosing && manualStage == 2) return; // (the tutorial's picture and window alone)
    if (choosing && settingsOpen) {
        drawSettings(view, w, h);
        return;
    }
    if (choosing && optionsOpen) { // (StepOption: SetBattleStatusDisplay off)
        drawOptions(view, proj, w, h);
        return;
    }
    if (choosing && optionsAsk) {
        drawOptionsAsk(view, w, h);
        return;
    }
    for (int k = 0; k < 2 && !scene.active; ++k) { // (a scene's: SetBattleStatusDisplay off)
        const cr3::Fighter& f = battleRules->fighter(k);
        if (!f.present) continue;
        const std::string n = k == 0 ? "pc01" : "pc02";
        UiFig& win = uiFig(n + "win");
        win.fig.place = cr3::Mat34();
        poseUi(win, n + "win", 0);
        drawFigure(win.fig, view.data());
        auto child = [&](const std::string& joint, const std::string& figure, float t, bool loop) -> UiFig* {
            float x, y;
            if (!uiJoint(win, joint, x, y)) return nullptr;
            UiFig& c = uiFig(figure);
            c.fig.place = cr3::Mat34();
            c.fig.place.m[3] = x, c.fig.place.m[7] = y + 1.0f / float(std::max(h, 1)); // (+ a pixel)
            poseUi(c, figure, t, loop);
            return &c;
        };
        const int hp = shownHp(k);
        const float hpT = hp > 0 ? std::max(float(hp) * 5.0f / float(std::max(f.maxHp, 1)), 0.07f) : 0.0f;
        const float mpT = f.maxMp > 0 ? float(f.mp) * 5.0f / float(f.maxMp) : 0.0f;
        if (UiFig* c = child("call_btlHpBar_per", "btlhpbar_per", hpT, false)) drawFigure(c->fig, view.data());
        if (UiFig* c = child("call_btlMpBar_per", "btlmpbar_per", mpT, false)) drawFigure(c->fig, view.data());
        if (UiFig* c = child("call_btlFace", "btlface", time, true)) {
            const std::string id = std::to_string(f.id);
            for (ViewMesh& vm : c->fig.meshes) {
                vm.texture = texture("btlFaceC00" + id);
                vm.extraMap = texture("btlFaceA00" + id);
            }
            drawFigure(c->fig, view.data());
        }
        // the labels (call_status_fr01 "HP", fr02 "MP")
        for (int id = 1; id <= 2; ++id) {
            float x, y;
            char jn[24];
            std::snprintf(jn, sizeof jn, "call_status_fr%02d", id);
            if (uiJoint(win, jn, x, y)) drawStatusSign(id, x, y, proj);
        }
        // the signs (BattleStatusMenu::SetPlayerStatusIcon: pcNNinfo's call_enchant_id00 the
        // first - the element -, id01 / id07 six states each, id13, id14)
        UiFig& info = uiFig(n + "info");
        info.fig.place = cr3::Mat34();
        poseUi(info, n + "info", 0);
        float ex, ey;
        if (uiJoint(info, "call_enchant_id00", ex, ey)) drawEnchant(elementSign(f), ex, ey, proj);
        { // (SetPlayerStatusIcon: the states in turn - six at id01, six at id07, one at id13, two at id14)
            const std::vector<int> signs = stateSigns(f);
            static const struct { const char* joint; int first, count; } kSlots[4] = {{"call_enchant_id01", 0, 6}, {"call_enchant_id07", 6, 6}, {"call_enchant_id13", 12, 1}, {"call_enchant_id14", 13, 2}};
            for (const auto& sl : kSlots)
                if (uiJoint(info, sl.joint, ex, ey))
                    for (int k = 0; k < sl.count && sl.first + k < int(signs.size()); ++k) drawEnchant(signs[size_t(sl.first + k)], ex + 0.04f * float(k), ey, proj);
        }
        for (const char* what : {"hpNum", "mpNum"}) {
            UiFig& num = uiFig(n + (std::string(what) == "hpNum" ? "hpnum" : "mpnum"));
            num.fig.place = cr3::Mat34();
            float x, y;
            if (uiJoint(num, "call_dotNum_num1", x, y))
                drawDigits(std::string(what) == "hpNum" ? hp : f.mp, 90, std::string(what) == "hpNum" ? 5 : 4, x, y, proj);
        }
    }
    if (!scene.active) { // (the gauges, the menus, the banner: not over a scene)
        // the charge gauge (BreakWindowMenu::Update -> ChargeGaugeWindow::SetChargeGaugeParameter
        // with charge / 10, 0..30: chggauge played towards that / 20 s - 1.5 s full - forwards or
        // back, at its own speed)
        const float chargeT = float(std::clamp(shownCharge(), 0, 300) / 10) / 20.0f;
        const float step = std::max(battleClock - chargeShownAt, 0.0f);
        chargeShownAt = battleClock;
        chargeShown = chargeShown < chargeT ? std::min(chargeShown + step, chargeT) : std::max(chargeShown - step, chargeT);
        UiFig& cg = uiFig("chggauge");
        cg.fig.place = cr3::Mat34();
        poseUi(cg, "chggauge", chargeShown);
        drawFigure(cg.fig, view.data());
        // the break gauge
        UiFig& bw = uiFig("brkwin");
        bw.fig.place = cr3::Mat34();
        poseUi(bw, "brkwin", 0);
        drawFigure(bw.fig, view.data());
        UiFig& bg = uiFig("brkgauge");
        bg.fig.place = cr3::Mat34();
        poseUi(bg, "brkgauge", float(shownGauge() + 4) * 0.5f);
        drawFigure(bg.fig, view.data());
        UiFig& bi = uiFig("brkinfo");
        bi.fig.place = cr3::Mat34();
        const int g = shownGauge();
        poseUi(bi, g > 0 ? "brkinfopl_io" : g < 0 ? "brkinfoen_io" : "brkinfoev_io", 5.0f);
        drawFigure(bi.fig, view.data());
        if (const char* m = SDL_getenv("CR3_MENU")) drawCommandMenu(std::string("btlbtn") + m + "_io", 10.0f, view, {}); // (debugging)
        if (choosing) drawChoosing(view, w, h);
        // the command's name
        if (!commandShown.empty() && (battleClock - commandShownStart < 3.0f || announceHeld)) {
            UiFig& m = uiFig("btlmsg");
            m.fig.place = cr3::Mat34();
            poseUi(m, "btlmsg_in", battleClock - commandShownStart);
            drawFigure(m.fig, view.data());
            float x, y;
            if (uiJoint(m, "typeM_C01", x, y)) drawUiText(commandShown, 28, x, y, w, h);
        }
    }
    // the damage
    for (const DamageShown& d : damageShown) {
        if (!d.placed) continue;
        const float age = battleClock - d.start;
        if (age < 0 || age > (d.mark >= 0 ? 1.45f : 1.2f)) continue;
        if (d.mark >= 0) {
            static const char* const kMarks[5] = {"bonus_miss", "bonus_block", "bonus_crtcl", "bonus_blow", "bonus_crtcl"};
            UiFig& bm = uiFig("bonus");
            bm.fig.place = cr3::Mat34();
            bm.fig.place.m[3] = d.x, bm.fig.place.m[7] = d.y;
            poseUi(bm, kMarks[d.mark], age);
            drawFigure(bm.fig, view.data());
        }
        if (d.value < 0) continue;
        int digits = 1;
        for (int v = d.value; v >= 10; v /= 10) ++digits;
        UiFig& bn = uiFig("btlnum");
        bn.fig.place = cr3::Mat34();
        bn.fig.place.m[3] = d.x, bn.fig.place.m[7] = d.y;
        poseUi(bn, "btlnum_fgr" + std::to_string(std::min(digits, 5)), age);
        for (int i = 0; i < digits && i < 5; ++i) {
            float x, y;
            char jn[24];
            std::snprintf(jn, sizeof jn, "call_dotNum_id%02d", i + 1);
            if (!uiJoint(bn, jn, x, y)) continue;
            int place = 1; // (id01 the ones)
            for (int k = 0; k < i; ++k) place *= 10;
            drawDigits(d.value / place % 10, d.set, 1, x, y, proj);
        }
    }
}

// (the quad's width in its own units: 0 when it isn't one)
float Viewer::sliceUiQuad(const std::string& name, ViewFigure& f, float cap) {
    SlicedQuad& sq = slicedQuads[name];
    if (sq.sliced && f.meshes.size() == 1 && f.meshes[0].source.vertexCount == 8) return sq.x[3] - sq.x[0];
    sq = SlicedQuad(); // (first seen, or its figure loaded again)
    sq.sliced = true;
    if (f.meshes.size() != 1 || f.meshes[0].source.vertexCount != 4) return 0;
    ViewMesh& vm = f.meshes[0];
    cr3::GeomMesh& m = vm.source;
    float lo = m.position[0], hi = m.position[0];
    for (int i = 0; i < 4; ++i) lo = std::min(lo, m.position[size_t(i) * 3]), hi = std::max(hi, m.position[size_t(i) * 3]);
    if (hi - lo <= 0) return 0;
    std::vector<int> left, right;
    for (int i = 0; i < 4; ++i) (m.position[size_t(i) * 3] < (lo + hi) / 2 ? left : right).push_back(i);
    if (left.size() != 2 || right.size() != 2) return 0;
    auto yOf = [&](int i) { return m.position[size_t(i) * 3 + 1]; };
    if (std::fabs(yOf(right[0]) - yOf(left[0])) > std::fabs(yOf(right[1]) - yOf(left[0]))) std::swap(right[0], right[1]); // (each row's left with its right)
    const std::vector<uint32_t> was = m.triangles();
    auto winding = [](const std::vector<float>& pos, uint32_t a, uint32_t b, uint32_t c) {
        const float* A = &pos[a * 3]; const float* B = &pos[b * 3]; const float* C = &pos[c * 3];
        return (B[0] - A[0]) * (C[1] - A[1]) - (B[1] - A[1]) * (C[0] - A[0]);
    };
    const bool ccw = was.size() >= 3 && winding(m.position, was[0], was[1], was[2]) > 0;
    cr3::GeomMesh out = m;
    out.vertexCount = 8;
    const float ts[4] = {0, cap, 1 - cap, 1};
    auto lerpInto = [](std::vector<float>& to, const std::vector<float>& from, int n, int a, int b, float t) {
        if (from.empty()) return;
        for (int k = 0; k < n; ++k) to.push_back(from[size_t(a * n + k)] + (from[size_t(b * n + k)] - from[size_t(a * n + k)]) * t);
    };
    out.position.clear(), out.normal.clear(), out.tangent.clear(), out.binormal.clear(), out.uv.clear(), out.uv1.clear(), out.colour.clear();
    out.positionW.clear(), out.weights.clear(), out.joints.clear();
    sq.column.clear();
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 4; ++c) { // (vertex r * 4 + c)
            const int a = left[size_t(r)], b = right[size_t(r)];
            lerpInto(out.position, m.position, 3, a, b, ts[c]);
            lerpInto(out.normal, m.normal, 3, a, b, ts[c]);
            lerpInto(out.tangent, m.tangent, 3, a, b, ts[c]);
            lerpInto(out.binormal, m.binormal, 3, a, b, ts[c]);
            lerpInto(out.uv, m.uv, 2, a, b, ts[c]);
            lerpInto(out.uv1, m.uv1, 2, a, b, ts[c]);
            lerpInto(out.colour, m.colour, 4, a, b, ts[c]);
            lerpInto(out.positionW, m.positionW, 1, a, b, 0);
            lerpInto(out.weights, m.weights, 4, a, b, 0);
            for (size_t k = 0; k < 4 && !m.joints.empty(); ++k) out.joints.push_back(m.joints[size_t(a) * 4 + k]);
            sq.column.push_back(c);
        }
    out.list = true, out.strip.clear();
    for (uint32_t c = 0; c < 3; ++c) {
        uint32_t q[6] = {c, c + 1, c + 5, c, c + 5, c + 4};
        if ((winding(out.position, q[0], q[1], q[2]) > 0) != ccw) std::swap(q[1], q[2]), std::swap(q[4], q[5]);
        out.strip.insert(out.strip.end(), q, q + 6);
    }
    for (int c = 0; c < 4; ++c) sq.x[c] = out.position[size_t(c) * 3];
    m = out;
    static const float kWhite[4] = {1, 1, 1, 1}, kZero[3] = {0, 0, 0}, kNoUv[2] = {0, 0};
    vm.vertices.assign(size_t(8) * cr3::kVertexStride, 0);
    for (uint32_t i = 0; i < 8; ++i) {
        auto at3 = [&](const std::vector<float>& v) { return v.empty() ? kZero : &v[size_t(i) * 3]; };
        packVertex(&vm.vertices[size_t(i) * cr3::kVertexStride], &m.position[size_t(i) * 3], at3(m.normal),
                   m.uv.empty() ? kNoUv : &m.uv[size_t(i) * 2], m.colour.empty() ? kWhite : &m.colour[size_t(i) * 4],
                   at3(m.tangent), at3(m.binormal), m.uv1.empty() ? kNoUv : &m.uv1[size_t(i) * 2]);
    }
    const int tex = vm.gpu.texture;
    dev->destroy(vm.gpu);
    vm.gpu = dev->upload(vm.vertices, m.triangles(), tex);
    return sq.x[3] - sq.x[0];
}

void Viewer::widenUiQuad(const std::string& name, ViewFigure& f, float widen) {
    SlicedQuad& sq = slicedQuads[name];
    if (!sq.sliced || sq.column.size() != 8 || f.meshes.size() != 1 || f.meshes[0].source.vertexCount != 8 || sq.widen == widen) return;
    sq.widen = widen;
    std::vector<float>& pos = f.meshes[0].source.position;
    for (size_t i = 0; i < 8; ++i) {
        const int c = sq.column[i];
        pos[i * 3] = sq.x[c] + (c < 2 ? -widen : widen);
    }
}

// a list row's place (comListMenu::Pose, PresentGetMenu::Pose): the selected one 0.25 to the
// left, those after it 0.076 lower
float Viewer::cr3ListRowX(float x, int k, int focus) { return x - (k == focus ? 0.25f : 0.0f); }

float Viewer::cr3ListRowY(float y, int k, int top, int focus) { return y - 0.76f * float(k - top) - (focus >= top && k > focus ? 0.076f : 0.0f); }

// a part placed and posed, not drawn (its joints where they'd be)
auto Viewer::cr3UiPose(const std::string& name, const std::string& anim, float t, float x, float y, bool loop) -> UiFig& {
    UiFig& u = uiFig(name);
    u.fig.place = cr3::Mat34();
    u.fig.place.m[0] = u.fig.place.m[5] = u.fig.place.m[10] = cr3UiScale;
    u.fig.place.m[3] = x, u.fig.place.m[7] = y, u.fig.place.m[11] = cr3UiZ;
    poseUi(u, anim.empty() ? name : anim, t, loop);
    return u;
}

auto Viewer::cr3UiPut(const std::string& name, const std::string& anim, float t, float x, float y, const Mat4& view, float alpha, bool loop) -> UiFig& {
    UiFig& u = cr3UiPose(name, anim, t, x, y, loop);
    u.fig.tint[0] = cr3UiTint[0], u.fig.tint[1] = cr3UiTint[1], u.fig.tint[2] = cr3UiTint[2];
    u.fig.tint[3] = alpha;
    if (cr3UiAlphaSet) {
        for (ViewMesh& vm : u.fig.meshes) {
            std::vector<float>& dc = vm.material.params[0x33];
            if (dc.size() < 4) dc.resize(4, 1.0f);
            dc[3] = alpha;
        }
        u.fig.tint[3] = 1;
    }
    std::vector<std::pair<ViewMesh*, std::vector<float>>> kept; // (a material's tint: for this drawing alone)
    if (!cr3UiTintMaterial.empty())
        for (ViewMesh& vm : u.fig.meshes) {
            const uint32_t mi = vm.source.material;
            if (mi >= u.materials.size() || u.materials[mi] != cr3UiTintMaterial) continue;
            std::vector<float>& dc = vm.material.params[0x33];
            kept.push_back({&vm, dc});
            if (dc.size() < 4) dc.resize(4, 1.0f);
            for (int c = 0; c < 3; ++c) dc[size_t(c)] *= cr3UiMaterialTint[c];
        }
    const bool stretchEquipFtr = name == "comftrequipmenu" && -2.0f * x > 10.24f;
    if (stretchEquipFtr) {
        u.fig.place.m[0] = (-2.0f * x / 10.24f) * cr3UiScale;
        poseUi(u, anim.empty() ? name : anim, t, loop);
    }
    drawFigure(u.fig, view.data());
    if (stretchEquipFtr) {
        u.fig.place.m[0] = cr3UiScale;
        poseUi(u, anim.empty() ? name : anim, t, loop);
    }
    for (auto& [vm, dc] : kept) {
        if (dc.empty()) vm->material.params.erase(0x33);
        else vm->material.params[0x33] = dc;
    }
    return u;
}

// a part posed by an animator over its own animation's first frame (the colours it rests at -
// mpListCglMenu's all 0 - the animator's added to those, not to the file's material) and drawn
auto Viewer::cr3UiPutOverOwn(const std::string& name, const std::string& anim, float t, float x, float y, const Mat4& view, bool loop) -> UiFig& {
    UiFig& u = uiFig(name);
    u.fig.place = cr3::Mat34();
    u.fig.place.m[0] = u.fig.place.m[5] = u.fig.place.m[10] = cr3UiScale;
    u.fig.place.m[3] = x, u.fig.place.m[7] = y, u.fig.place.m[11] = cr3UiZ;
    poseUi(u, name, 0);
    std::vector<std::vector<float>> own;
    for (const ViewMesh& vm : u.fig.meshes) {
        const auto it = vm.material.params.find(0x33);
        own.push_back(it != vm.material.params.end() ? it->second : std::vector<float>());
    }
    poseUi(u, anim, t, loop);
    for (size_t k = 0; k < u.fig.meshes.size() && k < own.size(); ++k) {
        ViewMesh& vm = u.fig.meshes[k];
        const auto fb = vm.baseMaterial.params.find(0x33);
        auto it = vm.material.params.find(0x33);
        if (it == vm.material.params.end() || own[k].empty()) continue;
        for (size_t c = 0; c < it->second.size() && c < own[k].size(); ++c) {
            const float file = fb != vm.baseMaterial.params.end() && c < fb->second.size() ? fb->second[c] : 1.0f;
            it->second[c] += own[k][c] - file;
        }
    }
    u.fig.tint[0] = u.fig.tint[1] = u.fig.tint[2] = u.fig.tint[3] = 1;
    drawFigure(u.fig, view.data());
    return u;
}

// its first joint named so, posed (fitted where it says _fitN: CRXPartsBase's)
bool Viewer::cr3UiCall(const UiFig& u, const std::string& prefix, float& x, float& y, int w, int h) {
    for (const std::string& j : u.joints)
        if (j.compare(0, prefix.size(), prefix) == 0 && uiJoint(u, j, x, y)) {
            if (w > 0) {
                float fx = 0, fy = 0;
                fitOffset(j, w, h, 10, fx, fy);
                x += fx, y += fy;
            }
            return true;
        }
    return false;
}

// a figure's meshes drawn with another texture (a portrait's, an icon's)
void Viewer::cr3UiTexture(UiFig& u, const std::string& image, const std::string& mask) {
    cr3::Texture* t = texture(image);
    if (!t) return;
    cr3::Texture* a = mask.empty() ? nullptr : texture(mask);
    for (ViewMesh& m : u.fig.meshes) {
        m.texture = t;
        if (a) m.extraMap = a; // (its alpha mask: the material's second sampler)
    }
}

// SpriteNumId (set 1: SpriteNumberParamPackTable's - com_num_01's digits 0.046875 apart from
// u 0.00195, v 0.46875; 0.244 x 0.264 units, 0.193 apart): `places` digits from (x, y) on, the
// last the ones, leading zeros blank
void Viewer::cr3SpriteNum(int value, int places, float x, float y, int w, int h, const float rgb[3]) {
    cr3::Texture* t = texture("com_num_01");
    if (!t) return;
    const float gw = 0.046875f / 0.192f, gh = 0.050781f / 0.192f, adv = 0.037109f / 0.192f;
    const Mat4 proj = uiProjection(w, h, 10);
    const float rgba[4] = {rgb[0], rgb[1], rgb[2], 1};
    for (int s = 0; s < places; ++s) {
        int p = 1;
        for (int k = 0; k < places - 1 - s; ++k) p *= 10;
        if (value < p && s != places - 1) continue; // (blank)
        const int d = value / p % 10;
        // (its v from the sheet's foot, GL's: the image's rows 1 - v - dv .. 1 - v, the top the last)
        const float u0 = 0.0019531f + 0.046875f * float(d), u1 = u0 + 0.046875f, vb = 1.0f - 0.46875f - 0.050781f, vt = 1.0f - 0.46875f;
        const float x0 = x + adv * float(s) - gw / 2, x1 = x0 + gw, y0 = y - gh / 2, y1 = y + gh / 2;
        const float v[24] = {x0, y0, u0, vb, x1, y0, u1, vb, x1, y1, u1, vt, x0, y0, u0, vb, x1, y1, u1, vt, x0, y1, u0, vt};
        dev->drawQuad(*t, v, proj.data(), rgba);
    }
}

std::string Viewer::cr3MonsterName(int id) {
    cr3::MbeFile::Row r;
    std::vector<uint8_t> d;
    static cr3::MbeFile names;
    static int readIn = -1; // (the language it was read in: textGeneration)
    if (readIn != textGeneration) names = cr3::MbeFile(), readIn = textGeneration;
    static bool loaded = false;
    if (!loaded) loaded = archive.read("data\\mon_name.mbe", d) && names.parse(d);
    if (!names.find("MonsterName", id, "ipp", r)) return {};
    const std::string en = r.s(2), jp = r.s(1);
    return optionLanguage == 0 || en.empty() ? jp : en; // (the text slot: 0 Japanese)
}

// a round tap area (CRXPartsBase::ProvisionalCheckHitCollision's second kind: tapAreaCP<n> its
// centre, tapAreaAP<n> a point on its edge - within the distance between them), as the square
// about it the taps are tested against
auto Viewer::circleTap(float cx, float cy, float ax, float ay, int id) -> Cr3GeneLabTap {
    const float r = std::hypot(ax - cx, ay - cy);
    return {cx - r, cy - r, cx + r, cy + r, id};
}

// a value in a panel's numbered digit joints (call_<prefix>_001 ..): InterfaceMain::SetViewNumbers
void Viewer::cr3UiDigits(const UiFig& panel, const std::string& prefix, const std::string& figure, long value, int places, int offset,
                 const Mat4& view, bool hideLead) {
    long most = 1;
    for (int k = 0; k < places; ++k) most *= 10;
    value = std::clamp(value, 0L, most - 1);
    bool shown = !hideLead;
    for (int k = 0; k < places; ++k) {
        most /= 10;
        const int d = int(value / most % 10);
        if (d > 0 || k == places - 1) shown = true;
        if (!shown) continue;
        char joint[64];
        std::snprintf(joint, sizeof joint, "%s_%03d", prefix.c_str(), k + 1);
        float x, y;
        if (cr3UiCall(panel, joint, x, y)) cr3UiPut(figure, "", float(d + offset) / 30, x, y, view);
    }
}

// a number's digits on a panel's numbered joints, `fig` posed at each (frame digit + offset)
// - its leading zeros left out; "MAX" (frames 40, 41, 42 / 10, 11, 12) where `max` and 3 places
void Viewer::cr3UiNumber(const UiFig& panel, const std::string& prefix, const std::string& fig, long value, int places, int offset, const Mat4& view,
                 float alpha, int maxFrame, int suffix) {
    long most = 1;
    for (int k = 0; k < places; ++k) most *= 10;
    value = std::clamp(value, 0L, most - 1);
    bool shown = false;
    for (int k = 0; k < places; ++k) {
        most /= 10;
        const int d = int(value / most % 10);
        if (d > 0 || k == places - 1) shown = true;
        if (!shown && maxFrame < 0) continue;
        char joint[64];
        std::snprintf(joint, sizeof joint, "%s_%0*d", prefix.c_str(), suffix, k + 1);
        float x, y;
        if (cr3UiCall(panel, joint, x, y)) cr3UiPut(fig, "", float(maxFrame >= 0 ? maxFrame + k : d + offset) / 30, x, y, view, alpha);
    }
}

// comListIcon::SetIconPattern: icon n is frame n % 36 of icon_panel_<n / 36 + 1> (its alpha
// ..A where the release keeps one apart)
float Viewer::cr3UiIcon(UiFig& u, int icon) {
    icon = std::max(icon, 0);
    char name[24];
    std::snprintf(name, sizeof name, "icon_panel_%02d", icon / 36 + 1);
    cr3::Texture* t = texture(name);
    cr3::Texture* a = texture(std::string(name) + "A", false);
    for (ViewMesh& vm : u.fig.meshes) {
        const uint32_t mi = vm.source.material;
        if (mi >= u.materials.size() || u.materials[mi] != "mat_icon_panel_01") continue;
        if (t) vm.texture = t;
        if (a) vm.extraMap = a;
    }
    return float(icon % 36) / 30;
}

void Viewer::cr3UiMaterial(UiFig& u, const std::string& material, const std::string& image) {
    std::vector<cr3::Texture*>& own = uiOwnTextures[u.fig.name];
    if (own.size() != u.fig.meshes.size()) {
        own.clear();
        for (const ViewMesh& vm : u.fig.meshes) own.push_back(vm.texture);
    }
    cr3::Texture* t = texture(image);
    for (size_t k = 0; k < u.fig.meshes.size(); ++k) {
        const uint32_t mi = u.fig.meshes[k].source.material;
        if (mi < u.materials.size() && u.materials[mi] == material) u.fig.meshes[k].texture = t ? t : own[k];
    }
}

// CardModel: card_default with the gene's picture (card_<series>), rarity (card_rar_<grade>)
// and element (card_att_<kind>) on those materials, star_1 .. star_<stars> lit and the rest
// at a tenth (SetStarBrightness), the sheen card_eff_<grade, 2 at most> over it, looping -
// at (x, y, z), `scale` x; dimmed to half where it can't be chosen (SetSelectabile false)
void Viewer::drawCr3Card(int master, float x, float y, float z, float scale, const Mat4& view, float alpha, bool selectable, float roll, float spin) {
    const cr3::Cr3OfflineDb::GeneInfo* gi = game.cr3Db ? game.cr3Db->geneInfo(master) : nullptr;
    if (!gi) return;
    UiFig& u = uiFig("card_default");
    u.fig.place = cr3::Mat34();
    u.fig.place.m[0] = u.fig.place.m[5] = u.fig.place.m[10] = scale;
    if (roll != 0 || spin != 0) { // (CardModel::SetRotation: about its face's normal by roll, after about its upright by spin)
        const float cr = std::cos(roll), sr = std::sin(roll), ca = std::cos(spin), sa = std::sin(spin);
        const float r[9] = {cr * ca, -sr, cr * sa, sr * ca, cr, sr * sa, -sa, 0, ca};
        for (int row = 0; row < 3; ++row)
            for (int c = 0; c < 3; ++c) u.fig.place.m[row * 4 + c] = r[row * 3 + c] * scale;
    }
    u.fig.place.m[3] = x, u.fig.place.m[7] = y, u.fig.place.m[11] = z;
    poseUi(u, "card_default", 0);
    char name[32];
    std::snprintf(name, sizeof name, "card_%03d", gi->series);
    cr3UiMaterial(u, "picture", name);
    std::snprintf(name, sizeof name, "card_rar_%02d", gi->grade);
    cr3UiMaterial(u, "rarity", name);
    std::snprintf(name, sizeof name, "card_att_%02d", gi->kind);
    cr3UiMaterial(u, "attribute", name);
    const float dim = selectable ? 1.0f : 0.5f;
    for (ViewMesh& vm : u.fig.meshes) {
        const uint32_t mi = vm.source.material;
        const std::string mn = mi < u.materials.size() ? u.materials[mi] : "";
        float b = mn == "picture" || mn == "rarity" || mn == "attribute" ? dim : -1;
        if (mn.compare(0, 5, "star_") == 0) b = (std::atoi(mn.c_str() + 5) <= gi->stars ? 1.0f : 0.1f) * dim;
        if (b < 0) continue;
        vm.material.params[0x33] = {b, b, b, 1};
    }
    u.fig.tint[3] = alpha;
    drawFigure(u.fig, view.data());
    if (!selectable) return;
    std::snprintf(name, sizeof name, "card_eff_%02d", std::min(gi->grade, 2));
    UiFig& e = uiFig(name);
    e.fig.place = u.fig.place;
    poseUi(e, name, uiClock, true);
    e.fig.tint[3] = alpha;
    drawFigure(e.fig, view.data());
}

// the header (HeaderMenu, interface 0x42: comHeMenu - its title comHeTitle_fr00 at the
// screen's frame (SetHeaderTitle: 1 Items, 2 Equipment, 3 Gene Swap, 4 Fuse Gifts, 5 Quests ..,
// 30 .. the gene lab's), the sentence at typeM_L01 -, comHeIconMenu's battery, comHeInfoAMenu's
// gold, clock and coins)
void Viewer::drawCr3Header(float t, int title, const std::string& sentence, int w, int h, const Mat4& view) {
    float x, y;
    UiFig& head = cr3UiPut("comheposition", "", t, 0, 0, view); // (HeaderMenu::InterfaceOpenAnimeSet: its own animation - the io animator closes it)
    if (cr3UiCall(head, "call_comHeMenu", x, y, w, h)) {
        UiFig& menu = cr3UiPut("comhemenu", "", 0, x, y, view);
        float tx, ty;
        if (cr3UiCall(menu, "call_comHeTitle_fr00", tx, ty)) cr3UiPut("comhetitle_fr00", "", float(title) / 30, tx, ty, view);
        drawGeneLabSentence(menu, sentence, w, h);
    }
    if (cr3UiCall(head, "call_comHeIconMenu", x, y, w, h)) {
        UiFig& icon = cr3UiPut("comheiconmenu", "", 0, x, y, view);
        float bx, by;
        if (cr3UiCall(icon, "call_comHeBattery_per", bx, by)) cr3UiPut("comhebattery_per", "", 100.0f / 30, bx, by, view);
        if (cr3UiCall(icon, "call_comHeBatteryC_fr00", bx, by)) cr3UiPut("comhebatteryc_fr00", "", 0, bx, by, view);
    }
    if (cr3UiCall(head, "call_comHeInfoAMenu", x, y, w, h)) {
        UiFig& info = cr3UiPut("comheinfoamenu", "", 0, x, y, view);
        float px, py;
        if (cr3UiCall(info, "call_comHeMlPanel", px, py)) {
            UiFig& ml = cr3UiPut("comhemlpanel", "", 0, px, py, view);
            cr3UiDigits(ml, "call_comHeNum_fr00", "comhenum_fr00", game.cr3Money, 7, 0, view);
        }
        if (cr3UiCall(info, "call_comHeTimePanel", px, py)) {
            UiFig& clock = cr3UiPut("comhetimepanel", "", t, px, py, view, 1, true);
            const std::time_t now = std::time(nullptr);
            std::tm lt{};
#ifdef _WIN32
            localtime_s(&lt, &now);
#else
            localtime_r(&now, &lt);
#endif
            const int hhmm = lt.tm_hour * 100 + lt.tm_min;
            for (int k = 0; k < 4; ++k) {
                static constexpr int kDiv[4] = {1000, 100, 10, 1};
                if (k == 0 && hhmm < 1000) continue; // (SetHourTime: the hours' tens hidden)
                char joint[40];
                std::snprintf(joint, sizeof joint, "call_comHeNum_fr00_%03d", k + 1);
                float dx, dy;
                if (cr3UiCall(clock, joint, dx, dy)) cr3UiPut("comhenum_fr00", "", float(hhmm / kDiv[k] % 10 + 30) / 30, dx, dy, view);
            }
        }
        if (cr3UiCall(info, "call_comHeCoinPanel", px, py)) {
            UiFig& coin = cr3UiPut("comhecoinpanel", "", 0, px, py, view);
            cr3UiDigits(coin, "call_comHeNum_fr00", "comhenum_fr00", game.cr3Coins, 5, 20, view);
        }
    }
}

// (SetNumbers: a number from a panel's call_comListNum_fr00_<slot>, its digits 0.19 apart, the
// leading zeros blank)
void Viewer::cr3UiSlotNumber(const UiFig& panel, int slot, long value, int places, int offset, const Mat4& view, bool top) {
    char joint[40];
    std::snprintf(joint, sizeof joint, "call_comListNum_fr00_%03d", slot);
    float nx, ny;
    if (!cr3UiCall(panel, joint, nx, ny)) return;
    long most = 1;
    for (int k = 0; k < places; ++k) most *= 10;
    value = std::clamp(value, 0L, most - 1);
    bool shown = top;
    for (int k = 0; k < places; ++k, nx += 0.19f) {
        most /= 10;
        const int d = int(value / most % 10);
        if (d > 0 || k == places - 1) shown = true;
        if (shown) cr3UiPut("comlistnum_fr00", "", float(top ? 40 + k : d + offset) / 30, nx, ny, view); // ("MAX": SetMaxNumbers)
    }
}

// a list row's number (comListNum*_num00: comListNum_fr00 digits right-aligned in its slots
// call_comListNum_fr00_001 ..)
void Viewer::drawCr3ListDigits(const UiFig& num, int value, float alpha, const Mat4& view) {
    const std::string digits = std::to_string(std::max(value, 0));
    int slots = 0;
    for (const std::string& j : num.joints) slots += j.rfind("call_comListNum_fr00_", 0) == 0;
    for (int d = 0; d < int(digits.size()) && d < slots; ++d) {
        float dx, dy;
        if (cr3UiCall(num, "call_comListNum_fr00_00" + std::to_string(slots - int(digits.size()) + d + 1), dx, dy))
            cr3UiPut("comlistnum_fr00", "", float(digits[size_t(d)] - '0') / 30, dx, dy, view, alpha);
    }
}

// A comListMenu list (comListMenu::Pose, AddListItem): its rows behind two at each end -
// DummyAddListItem's blank (type 1, never shown) and an arrow row (type 2 / 3: comListArrowPanel,
// comListArrow_fr00 frame 0 up / 1 down - shown only with four rows or more); the focused row
// held at the third place (top = focus - 2), the list moving past it, five places in its mask
// (those after the focused 0.076 lower). The arrows here; its bar (comListMenu::AutoScroll:
// from two rows - the barLimitLow 0.15 lower - as far as the focus) by drawCr3ListBar(menu,
// false, focus, n, 1, view, 0.15f)
void Viewer::drawCr3ListEnds(float lx, float ly, int n, int focus, const Mat4& view) {
    if (n < 4) return;
    for (const int k : {-1, n}) {
        if (k < focus - 2 || k > focus + 2) continue;
        const float x = cr3ListRowX(lx, k, focus), y = cr3ListRowY(ly, k, focus - 2, focus);
        cr3UiPut("comlistarrowpanel", "", 0, x, y, view);
        cr3UiPut("comlistarrow_fr00", "", k < 0 ? 0.0f : 1.0f / 30, x + 1.475f, y - 0.135f, view); // (comListItem::Pose: its arrow 1.475 across, 0.135 down)
    }
}

// A list's scroll bar (comListBarPanel / the short list's comListBarSPanel at the menu's call): its
// comListBarCursor at rest at its call while the list fits, else (AutoScroll) 0.2 up and down
// the bar - barLimitUp to barLimitLow - as far as the list is scrolled
void Viewer::drawCr3ListBar(const UiFig& menu, bool shortList, int top, int n, int visible, const Mat4& view, float lowExtra) {
    float bx, by, cx, cy, ux, uy, lx, ly;
    if (!cr3UiCall(menu, shortList ? "call_comListBarSPanel" : "call_comListBarPanel", bx, by)) return;
    UiFig& bar = cr3UiPut(shortList ? "comlistbarspanel" : "comlistbarpanel", "", 0, bx, by, view);
    if (!cr3UiCall(bar, "call_comListBarCursor", cx, cy)) return;
    if (n > visible && cr3UiCall(bar, "barLimitUp", ux, uy) && cr3UiCall(bar, "barLimitLow", lx, ly))
        cy += 0.2f + std::clamp(float(top) / float(n - visible), 0.0f, 1.0f) * (ly + lowExtra - uy);
    cr3UiPut("comlistbarcursor", "", 0, cx, cy, view);
}

void Viewer::cr3UiLabel(const UiFig& panel, const char* joint, const std::string& text, int w, int h, float shade) {
    float tx, ty;
    if (text.empty() || !cr3UiCall(panel, joint, tx, ty)) return;
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
    const float k = cr3UiFocus ? 1.1f : 1.0f, o = k * uiTextScale;
    drawUiText(text, 26 * k, (tx + 0.075f * o) / 10, (ty - 0.15f * o) / 10, w, h, true, cr3UiFocus ? 1.0f : cr3UiDim ? shade * 0.5f : shade);
}

// a posed figure's joint's whole transform (its place in the world)
bool Viewer::cr3JointMatrix(const UiFig& u, const std::string& joint, cr3::Mat34& out) {
    const auto it = std::find(u.joints.begin(), u.joints.end(), joint);
    if (it == u.joints.end()) return false;
    std::vector<cr3::JointTransform> p = u.fig.skeleton.basePose;
    if (!u.fig.posed.empty()) p = u.fig.posed;
    const std::vector<cr3::Mat34> w = u.fig.skeleton.world(p, u.fig.place);
    const size_t j = size_t(it - u.joints.begin());
    if (j >= w.size()) return false;
    out = w[j];
    return true;
}

float Viewer::cardUV(int series, int k) {
    if (cardUVs.empty()) {
        std::vector<uint8_t> d;
        if (archive.read("UI_cardUV.dat", d)) {
            cardUVs.resize(d.size() / 4);
            std::memcpy(cardUVs.data(), d.data(), cardUVs.size() * 4);
        }
        if (cardUVs.empty()) cardUVs.push_back(0);
    }
    const size_t i = size_t(std::max(series, 0)) * 6 + size_t(k);
    return i < cardUVs.size() ? cardUVs[i] : 0.0f;
}

} // namespace cr3_viewer
