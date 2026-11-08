# CR3-U - Chaos Rings III, native

CR3-U is a from-scratch native port of *Chaos Rings III* (Square Enix / Media.Vision, 2014) for
Windows, macOS, Linux and Android. It runs the game from the files of **your own copy**: nothing of
the game is included.

It is new code, written by reverse-engineering the Android release (whose 64-bit library keeps its
function names) and checked against the iPhone / iPad and Vita releases and reference playthroughs.
Chaos Rings III runs on a later revision of the engine behind *Chaos Rings II* (Media.Vision's MVGL,
with compiled Squirrel scripts), so CR3-U starts from [CR2-U](https://github.com/DuIslingr/CR2-U)'s
engine and platform layers. Its siblings are [CR1:U](https://github.com/DuIslingr/CR1-U) and
[CRO-U](https://github.com/DuIslingr/CRO-U).

**[Download the latest release](https://github.com/DuIslingr/CR3-U/releases/latest)**

**Status: playable, in progress.** The title, the prologue and the first chapter play through to
the first real dungeon: towns and dungeons, random encounters, battles, the camp, the gene lab,
shops, quests, saving, movies and every voice. Much of the later story hasn't been tested yet, and
the battles' particle effects, some spells' staging and a few menus' rarer screens are still missing. The game's online services
work offline, as the Vita's own rules for them do; CR3-U never contacts Square Enix's servers.
Details: [docs/STATUS.md](docs/STATUS.md).

## What you need

A copy of the game from one of these releases - any one of them is enough on its own:

| Release | What to give the port |
|---|---|
| **Android** 1.1.3 (`com.square_enix.chaosrings3gp`) | the `.apks` (or `.xapk`), or the `base.apk` together with its asset packs (`CRDBbgm`, `CRDBvoice`, `CRDBse`, `CRDBmov`). **The asset packs are needed**: without them there is no sound and the text and movies are missing. |
| **iPhone / iPad** 1.2.2 | the `.ipa` (or its unpacked `ChaosRings3_FINAL_ENG.app`) |
| **PlayStation Vita** (PCSG00500) | the installed game (Vita3K's folder, or the `PCSG00500` folder), or a NoNpDrm dump - a folder or a `.zip` / `.rar` / `.7z` / `.vpk`. The Prequel Trilogy's Chaos Rings III (PCSG00457) works too. |

The Android and iOS releases have English and Japanese. The Vita release is Japanese; the fan
translations add English and Spanish to it (see [Fan translations](#fan-translations)). With the
Vita copy, a controller also works the game the way the Vita's own buttons do.

A PC needs Vulkan 1.3 or OpenGL 3.3; a phone, Android 7 or later (64-bit).

## Install and first start

1. Download the file for your system from the [releases page](https://github.com/DuIslingr/CR3-U/releases)
   and unzip it.
2. Start `cr3view`.
3. If it doesn't find a copy of the game, it asks for one: pick the file or folder, and the port
   copies what it needs into its game folder (or your profile folder if that can't be written).
   You can delete or move the original afterwards.

A Vita dump is decrypted during setup, keeping only Chaos Rings III's files. Over an untranslated
Vita copy the setup also asks for a fan translation (optional).

| System | Notes |
|---|---|
| Windows 10+ | If SmartScreen warns about an unsigned program, choose *More info*, *Run anyway*. |
| macOS 11+ | Right-click `cr3view.app` and choose *Open* (it isn't signed). |
| Linux | `chmod +x` the AppImage and run it, or use the `.tar.gz`. |
| Android 7+ | Install the APK and start it. The first start offers the same setup: pick your `.apks` (or APK and asset packs), the `.ipa` or a Vita archive. Folders can't be picked on Android; copy them over USB instead (below). |

**Android `.apks` files:** a repacked `.apks` that holds its asset packs compressed has them
unpacked once on the first start, into `assetpacks/` beside it (about **1.3 GB** of free space; that
first start takes a while). An `.apks` or `.xapk` that holds its APKs themselves compressed (as some
download sites pack them) has those unpacked once into `unpacked/` (about **3.7 GB** with the asset
packs). Later starts reuse them.

### Your copy

You can also copy your game into the **game folder** yourself; the port finds it on the next start.
The game folder is the folder holding `cr3view` (or `cr3view.exe`, `cr3view.app`, the AppImage). On
Android it is `Android/data/local.preservation.chaosrings3/files/`, reached over USB.

```
<game folder>/
├── android/      the 1.1.3 .apks - or its base.apk with the asset packs' CRDB{bgm,voice,se,mov}.android.mvgl
├── ios/          the 1.2.2 app (ChaosRings3_FINAL_ENG.app, or Payload/ChaosRings3_FINAL_ENG.app as unzipped)
├── vita/         PCSG00500/ (or PCSG00457/) as installed - or a NoNpDrm dump as dumped
├── translation/  optional: en/ and es/, the fan translations (over a Vita copy)
├── movies/       optional: story movies your release lacks (the iPad's lacks c04..c11)
├── artwork/      optional: card archives for Original Card Art
├── fsr4/         optional: AMD's FSR 4 files (see Settings)
├── textures/     optional: export/ and hd/ for HD textures
└── models/       optional: export/, hd/ and addons/ for HD models and add-ons
```

- Any one release folder is enough. With several, the port uses the first of `vita/`, `android/`,
  `ios/`; `--data <folder, .apks or archive>` (or `CR3_DATA`) picks another, or a copy elsewhere.
- A release can sit one or two folders down, as it was unpacked; folder names below `ios/`,
  `android/` and `vita/` don't matter.
- A still-encrypted Vita dump in `vita/` (its `app/`, and `addcont/` for the Prequel Trilogy) is
  decrypted on the next start, on a phone too, once there is room for the decrypted copy. Delete
  the dump afterwards.
- The release downloads already contain these folders, each with a `README.txt` saying what goes
  in it.
- Older layouts still work: `ipad/`, `PCSG00500/`, `PCSG00457/`, `ChaosRings3_FINAL_ENG.app/`,
  `Payload/` or `data/` straight in the game folder.

If nothing is found, the port says where it looked. Every layout, the command-line tools and the
environment variables: [docs/GAME_FILES.md](docs/GAME_FILES.md).

### Fan translations

Over an **unpatched** Vita copy, the [Chaos Rings III Translation Project](https://gbatemp.net/threads/chaos-rings-iii-translation-project.443742/)'s
English and Joseas's Spanish (with its own font for á, ñ, ¿, ¡) are each a language of their own,
both at once too. Put each one's rePatch folder, or the archive it comes in, into
`translation/en/` or `translation/es/` (an archive dropped straight into `translation/` is unpacked
into the right one on the next start). **Options > Other > Language** switches between Japanese
and the translations while you play, and the choice is kept. A Vita copy with a translation already
patched into it shows that translation as it is. `--patch <rePatch folder>` gives one from
elsewhere.

### Saves and settings

Saves and `settings.ini` go to your profile folder, not the game folder:

| System | Profile folder |
|---|---|
| Linux | `~/.local/share/DuIslingr/CR3-U/` |
| Windows | `%APPDATA%\DuIslingr\CR3-U\` |
| macOS | `~/Library/Application Support/DuIslingr/CR3-U/` |

With Vulkan it also keeps `vulkan-pipelines.bin`, the driver's compiled shaders, so later starts
and new screens are smoother (safe to delete; a new GPU or driver starts it afresh).

- **Save slots:** the title's save menu has **12 slots**, three to a page across four pages. The
  on-screen arrows, Left / Right, Page Up / Page Down or L / R change the page; Up / Down pick a
  slot. Copy and Delete work across pages. The game also saves by itself when you enter an area and
  after battles, into the slot you're playing.
- **New Game Plus** (the port's addition): a cleared save is marked **CLEARED**; open it to choose
  Continue, New Game Plus or Back. New Game Plus asks for an empty slot and starts a new prologue
  carrying over team level, genes and their upgrades, equipment, items, money, collections, names
  and records. Story flags, quests, VS progress and recruitment start over. The original save is
  kept.
- **Vita saves:** on the title, open **Options > Other > Vita Saves > Import** and pick the folder
  of your decrypted Vita saves. Existing saves are backed up first. Details:
  [docs/VITA_SAVES.md](docs/VITA_SAVES.md).

## Controls

The iPhone and Android releases are touch only, so a finger or the mouse works everywhere. On the
field, press and drag to walk (drag further to run); let go without moving to tap - turn a page,
check what's in front of you. With the follow camera the screen is split by **Options > Controls**:
Two-Handed (the default) walks on the left half and turns the camera on the right; One-Handed walks
on the bottom half and turns on the top. With a mouse, the left button walks and the right button
held turns the camera anywhere.

### Controller

Buttons are by position: on an Xbox pad ○ is B, × is A, △ is Y and □ is X. ○ confirms, as on the
Vita. To confirm with the bottom button instead, turn on **Swap ○ and ×** in Options > Graphics.

| Button | Field | Menus, battles, messages |
|---|---|---|
| Left stick / D-pad | walk (push the stick fully to run) | move the cursor (holding repeats) |
| Right stick | turn the camera (Vita copy) | - |
| ○ | talk, check | confirm, turn the page |
| × | close the field buttons (Vita copy) | back, cancel; No in a question |
| □ (hold) | - | fast-forward a scene's messages |
| △ | open / close the field buttons (Camp, My Page, the shop, Save, Battle, the way back to town) | - |
| L / R | camera behind you, cycle goods (Vita copy) | the save files' pages; tabs, categories and pages (Vita copy) |
| START | - | the title's tap (Vita copy) |

**With the Vita copy as the game**, the controller follows the Vita release's own button code on
every screen where the touch releases have nothing to follow:

- **Field buttons:** △ opens them with a cursor on Camp; the d-pad moves it round, ○ presses, × or
  △ closes. L swings the camera behind you; R cycles the goods in reach, □ uses them.
- **Event choices and Yes / No questions:** a cursor starts on the first option; Up / Down move it,
  ○ answers, × is No.
- **Battles:** the d-pad moves over the commands, lists and targets (greyed-out ones refuse with a
  sound); L / R switch Items and Gifts; flexible-target commands can switch to the other side. ○
  shortens the victory camera and the result tally.
- **A lost battle:** Up / Down choose Give Up or Revive, ○ presses.
- **Menus:** L / R switch the gene lab's, options' and Gene Index's tabs, the shop's Buy / Sell, the
  gifts' and quests' categories and the Lucky Box's boxes. The camp, items (the quantity reels), gene
  change (□ sorts, lock), gene lab, gifts, options, Gene Index, premium shop, My Page, the save
  files, the world select, the VS mode and the lunches all have a d-pad cursor.
- **Movies and Vista scenes:** △ pauses and resumes (picture and sound together), □ stops a movie.

With the iPhone or Android copy, the stick and d-pad walk and move the cursors, and the buttons
stand in for keys (○ Return, × Backspace, △ Tab, □ held F). Full details, screen by screen, with the original's
code references: [docs/CONTROLS.md](docs/CONTROLS.md).

### Keyboard

| Key | Action |
|---|---|
| Arrow keys / W A S D | run on the field (hold Shift to walk); move the cursor in menus |
| Return | talk, check, turn a page, confirm |
| Backspace (Android: Back) | back, cancel |
| Tab | the field buttons; the next tab in menus |
| C | Camp |
| 1 / 2 / 3, Y / N | answer a choice, a question |
| Page Up / Page Down | as L / R: pages and tabs |
| F (hold) | fast-forward a scene's messages |
| Q / E, R / V | turn, tilt the field's camera |
| O | Options, on the title; R there resets the open tab |
| Space | pause |
| F1 / F2 | cycle output resolution / upscaler mode |
| Esc | back in menus; quits from the field |

## Settings

The title's **Options** (lower right, or O), the camp's and a battle's have the game's own tabs
plus CR3-U's **Graphics** tab, saved in `settings.ini`. On a desktop the tabs and rows get smaller
as the window grows, so more rows fit. **Reset** (the round button left of Back, or R) puts the open
tab back to its defaults. Most settings also have a command-line flag for a single run.

| Setting | What it does |
|---|---|
| Display, Resolution, VSync, Frame Rate | window / fullscreen / borderless, output size (up to 3840 x 2160), frame cap |
| Renderer | Vulkan (default) or OpenGL; takes effect on the next start |
| Upscaler, Upscale Mode, Sharpness | FSR 3.1, FSR 4, DLSS or MetalFX. The 3D scene renders smaller and is upscaled; menus and text stay sharp. |
| Anti-aliasing, Anisotropic / Texture Filtering | MSAA, sharper textures at an angle, sharp or smooth texels |
| HD Textures, HD Models | your own replacement textures and models (below) |
| Smooth Models | rounds out every 3D model (4x or 9x triangles), keeping hard edges |
| Original Card Art | Gene Index cards 80, 81 and 86 with their original artwork (below) |
| Field Speed, Battle Speed | Off or 2x to 5x: walking the field and the battles that many times as fast |
| Swap ○ and × | which controller button confirms |
| Lock Orientation | keep the current landscape direction (phones) |

The **Other** tab has the **Language** (where the copy has more than one) and the Vita save import.

**FSR 4** (AMD RX 6000 or later, on Linux and Windows) needs AMD's own files, which can't be
included. Put these in `fsr4/` in the game folder (its `README.txt` has the links):

- `amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_upscaler_vk.dll` from AMD's
  [FSR SDK 2.4](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.4.0)
  (`Kits/FidelityFX/signedbin/`)
- `amdvkffx64.dll` from the AMD Software: Adrenalin Edition 26.10.1 (or later) driver package
- on Linux also `libffx_vk_linux.so` from [ffx-vk-linux](https://github.com/DuIslingr/ffx-vk-linux),
  which runs AMD's DLLs natively, without Wine

Without them the option shows "(n/a)" with the reason, and FSR 3.1 is used. `CR3_FSR4_DIR` points
elsewhere.

**DLSS** isn't in the release builds: NVIDIA's library can't ship with GPL code. You can build it
yourself (see [Building from source](#building-from-source)).

## Extras

### Movies

The story's movies play full screen with subtitles in the game's language; tap once for **Skip**,
and again within three seconds to skip. The Vita and Android releases hold them all; the iPad's
lacks the story's c04..c11 (the game downloaded those). Put the ones yours lacks in `movies/` (for
example from a Vita copy's `CR3/resources/movie/`); a movie found nowhere is skipped.

### Original card artwork

**Options > Graphics > Original Card Art** shows Gene Index cards **80, 81 and 86** with their
original artwork and matching names - Little Red Riding Hood, Aphrodite and Adam - in menus, gene
icons and full-screen views. Stats, rarities and saves don't change. The Vita copy already uses the
originals and iOS holds all three; Android holds two (the option shows **On (2/3)**). For the
missing one, copy both card archives from a decrypted Vita or iOS copy into `artwork/`
(`CRDBcard01` and `CRDBcardhd`, `.psp2.mvgl` or `.ios.mvgl`). Details:
[docs/GAME_FILES.md](docs/GAME_FILES.md#original-card-artwork).

### HD textures

Upscale the game's textures yourself, from your own copy:

1. `cr3view --export-textures` writes every picture the game draws to `textures/export/` as
   ordinary upright PNGs.
2. Upscale them into `textures/hd/` with the same file names. `python3 tools/hd_textures.py <game
   folder>` does it with waifu2x and Real-ESRGAN (needs Python 3, NumPy and Pillow; `--help` for
   options), or use any upscaler or image editor.
3. Start the game. With `textures/hd/` present, HD Textures switches on by itself.

Keep each picture's proportions and orientation (one scale both ways, at least the original size,
at most 8192 a side). A replacement that doesn't fit is skipped and the log says why.

### HD models

1. `cr3view --export-models` writes every model to `models/export/` as glTF (`.glb`) with its
   skeleton, textures and animations (`--no-animations` keeps the files small). Blender imports
   them directly.
2. Edit it, keeping the armature (bones named `node_<n>...`) and material names (`cr_mat_<n>...`).
   New textures can be packed into the `.glb`.
3. Export as glTF to `models/hd/<model>.glb` (for example `j001b01.glb`) and turn on HD Models in
   Options > Graphics. It's used from the next scene.

### Add-ons

New outfits and weapons of your own, sold in the premium shop beside the game's: a `.glb` made from
an exported model and a small `.ini` manifest (its id, kind, character, name, price) in
`models/addons/`. `cr3tool addons` checks them first.

Exported textures and models are made from the game's art: keep them, and anything made from them,
for personal use. The full steps, limits and the add-on manifest:
[docs/HD_ASSETS.md](docs/HD_ASSETS.md).

## Troubleshooting

- **Android: no sound, missing or broken text:** the asset packs are missing. Give the port the
  whole `.apks` (or `.xapk`), or the `base.apk` together with its `CRDB*.android.mvgl` asset packs
  - not the `base.apk` alone. A repacked `.apks` needs about 1.3 GB free on its first start (more
  for an `.xapk`); a pack that can't be unpacked is named in a message.
- **"No copy found":** the port lists every place it looked. Check the folder names above, or give
  the copy with `--data`.
- **Vulkan won't start:** the port falls back to OpenGL by itself; `--renderer opengl` forces it.
- **Menus or pictures look wrong with HD textures:** try with HD Textures off (Options > Graphics,
  or `--no-hd-textures`). The log names any replacement it skipped.
- **Android, updating from an older version:** v0.3.0 and earlier were each signed with a different
  key. Uninstall the old version once first. This deletes the game files and saves you copied in,
  so keep a copy of them. From v0.4.0 on, each release installs over the last.
- **Tested on:** Linux (played), Windows (under Wine), Android (the emulator). The macOS build comes
  from CI and hasn't been tried on real hardware, nor the Android one on many phones. Reports are
  welcome in the [issues](https://github.com/DuIslingr/CR3-U/issues): please say which copy of the
  game you use (Android, iOS or Vita, and how you got the files), your system, and what you did.

## Building from source

```sh
cd engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
build/cr3view
```

Needs a C++17 compiler, CMake and Ninja. SDL3, FreeType and zlib are used from the system or
downloaded and built by CMake. Vulkan needs the Vulkan loader and `glslc`; without them the build
is OpenGL only. `ctest --test-dir build` runs the tests; none need the game's files, and the two
that drive the Vita controls headless (`viewer-pad-vita`, `viewer-vista-pause`) run only with
`CR3_VITA_DATA` set to a decrypted Vita copy.

| Platform | How | Renderers and upscalers |
|---|---|---|
| Linux | as above | Vulkan, OpenGL 3.3; FSR 3.1, FSR 4, DLSS (with the SDK) |
| Windows 10+ x64 | MSVC and Ninja from a Developer prompt, with the [Vulkan SDK](https://vulkan.lunarg.com/); or from Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw): `-DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw-x86_64.cmake` | Vulkan, OpenGL 3.3; FSR 3.1, FSR 4, DLSS (MSVC, with the SDK) |
| macOS 11+ | Xcode command-line tools, Ninja and the [Vulkan SDK](https://vulkan.lunarg.com/) (MoltenVK is copied into the `.app`); `"-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"` for a universal build | Vulkan (MoltenVK), OpenGL 3.3; FSR 3.1, MetalFX (macOS 13+) |
| Android 7+ (arm64, x86-64) | Android SDK and NDK: `git clone -b release-3.2.0 https://github.com/libsdl-org/SDL android/SDL`, then `gradle assembleRelease` in `android/` | OpenGL ES 3.0 |

- `-DCR3_GLES=ON` builds the desktop version on OpenGL ES 3.0, as Android runs it.
- **DLSS:** add `-DCR_DLSS_SDK=<NVIDIA's DLSS SDK checkout>` (or put it in
  `engine/third_party/dlss/`) and copy its runtime next to the game. NGX is proprietary, so a DLSS
  build is for your own use only (see `THIRD_PARTY.md`).
- Every push is built for all four platforms by `.github/workflows/build.yml`; a `v*` tag publishes
  a release.

Build options, `cr3view`'s debug modes and `cr3tool`'s checks: [docs/ENGINE.md](docs/ENGINE.md).

## Documentation

| | |
|---|---|
| [docs/STATUS.md](docs/STATUS.md) | what the port plays today, system by system |
| [docs/GAME_FILES.md](docs/GAME_FILES.md) | where the port looks for your copy, every layout, the tools' `<your copy>`, movies, card artwork |
| [docs/CONTROLS.md](docs/CONTROLS.md) | touch, keyboard and the Vita's buttons, screen by screen, with the original's code references |
| [docs/HD_ASSETS.md](docs/HD_ASSETS.md) | HD textures, HD models, smooth models, FSR 4 and add-ons in full |
| [docs/VITA_SAVES.md](docs/VITA_SAVES.md) | importing Vita saves |
| [docs/ENGINE.md](docs/ENGINE.md) | building in detail, `cr3view`'s modes, `cr3tool`'s checks, the tools |
| [docs/FORMATS.md](docs/FORMATS.md) | the game's file formats, CR3's differences from CR2 first |
| `docs/*.md` (others) | audits: saves, input, timing, scene audio, mobile, graphics recovery |

| Path | Contents |
|---|---|
| `engine/` | the C++17 / SDL3 engine (`cr3core`), `cr3view` and `cr3tool` |
| `tools/` | analysis and extraction tools (`re_setup.sh`, `mvgl.py`, `msg.py`, ...); they work on your own copy |
| `android/`, `packaging/` | the Android project; icons, desktop entry, release notes |
| `re/`, `extracted/` | not in the repository: local reverse-engineering output and decoded assets - never commit or share |

## Licence and credits

- CR3-U is GPL-2.0-or-later (`LICENSE`). The macOS app is GPL-3.0, because of the bundled
  MoltenVK. Bundled libraries keep their own licences: see `THIRD_PARTY.md`.
- **No game data is in this repository or the downloads** - no art, audio, text, scripts or code
  of the game, nor anything generated from them. You need your own copy of the game.
- The fan translations are the Chaos Rings III Translation Project's (English) and Joseas's
  (Spanish); CR3-U reads them from your own copy of their patches.
- *Chaos Rings III* is by Square Enix and Media.Vision. CR3-U is a fan preservation project, not
  affiliated with or endorsed by them.
