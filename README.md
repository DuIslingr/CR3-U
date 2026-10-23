# CR3-U — a Chaos Rings III reimplementation

A from-scratch native port of *Chaos Rings III* (Square Enix / Media.Vision, 2014), built by
reverse-engineering the Android release (1.1.3, whose 64-bit library keeps its names), with the
iPhone/iPad release (1.2.2) and the PlayStation Vita release (PCSG00500, or the CR3 of the Prequel
Trilogy, PCSG00457) - **any one of the three is enough on its own**. It follows
**[CR2-U](https://github.com/DuIslingr/CR2-U)**: Chaos Rings III runs on a later revision of the
same engine, Media.Vision's MVGL (its archives, figures, Edge animations and Squirrel scripts), so
CR3-U starts from CR2-U's engine and platform layers. It loads the game from **your own copy** at
runtime.

**No game data is in this repository or distributed with it** - no art, audio, text, scripts or
code of the game, and nothing generated from them. You need your own copy of the game.

Licence: GPL-2.0-or-later (`LICENSE`); the bundled libraries keep their own (`THIRD_PARTY.md`).
Not affiliated with or endorsed by Square Enix or Media.Vision.

## Status: playable, in progress

Start `cr3view` with your copy in place and the game's title comes up; a tap starts a new game -
or, with a save, carries on from it. What works today:

- **The story's start**: the title (its camera reel and logo), a new game through the prologue
  and into the first chapter - its battles, the scenes after them and the town's meals (talking
  with the party by their balloons) - as the reference playthrough shows them: message windows,
  cut-ins, voices
- **Towns and dungeons** (CR3's `Fld2` field): walking by keys, pad, mouse or touch, the follow
  camera, doors, the ways between a town's areas, event boxes and people, the area scripts, map
  names, the field's buttons; **random
  encounters** with first strikes, surprise attacks and ace monsters; **saving** with the field's
  Save button and where the game saves by itself - entering areas, after battles (one save, in
  the profile folder)
- **Battles**: the glass break, the get-set menu, commands, genes and items, the break gauge,
  criticals, downs and combos, status effects, hit effects, the battle scripts, announcements,
  escape, the result - EXP into the worn genes, money, monster drops - and the continue and game over
- **Menus**: the camp (items, equipment, status, Gene Swap, gifts), the gene lab (combine, enhance,
  sell, the repository), town shops, the Space Port's quests, the world select, My Page, the Gene
  Index, the VS mode - with the game's online services **offline** (the Vita's own DBEmu rules,
  every release; CR3-U never contacts Square Enix's servers)
- **Sound**: the 54 music tracks with their loops, the 1,033 effects and every voice, in each
  release's own format - iOS AAC in MP4, Android Ogg Vorbis, the Vita's ATRAC9 and HE-VAG
- **Data, all three releases standalone**: a release is found from its folder and every one of its
  archives indexed together - the iPhone's ~170 `CRDB*.ios.mvgl` and loose movies, Android's
  `main.10007.android.mvgl` (read straight out of the APK or `.apks`) and its asset packs, the Vita's
  ~236 `CR3/resources/**.psp2.mvgl`; every file, texture, mesh, skeleton and animation of all three
  decodes (`cr3tool check`, `images`, `models`, `animations`)
- **Languages**: Japanese and English on iOS / Android; the Vita's Japanese - and the **fan
  translation** ([Chaos Rings III Translation Project](https://gbatemp.net/threads/chaos-rings-iii-translation-project.443742/))
  as an *option*: given its `rePatch` folder next to an unpatched Vita copy, "English (fan
  translation)" is a language of its own (`--patch`, `assets/language.hpp`)
- **Scripts**: all 194 of CR3's compiled scripts in the game's own Squirrel 2.2.4, with its 600
  natives bound; every one of its 4,127 events plays through to its end (`cr3tool events`)

Not yet: much of what's later in the story is untested; the battles' particle effects and some
spell directions, a few menus' rarer screens; see `docs/FORMATS.md` for each system's notes and
what each still leaves out.

## Quick start

```sh
cd engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
build/cr3view --model c101.geom
build/cr3tool release "<your copy>"
```

`cr3view` finds your copy by itself in the game folder - for this build `engine/build/`, or the
folder you start it from - as `vita/`, `android/` or `ios/` ([Your copy](#your-copy)); `cr3tool`
(and `cr3view --data`) take one wherever it is:

| Your copy | `<your copy>` |
|---|---|
| iPhone / iPad 1.2.2 | the unpacked `.ipa` (or its `Payload/ChaosRings3_FINAL_ENG.app`) |
| Android 1.1.3 (`com.square_enix.chaosrings3gp`) | the `.apk` / `.apks`, or a folder with the APK unpacked; the asset packs (`CRDB{bgm,voice,se,mov}.android.mvgl`) anywhere under it |
| PS Vita PCSG00500 / PCSG00457 | the installed (decrypted) title's folder, or its `CR3/resources` |
| + the fan translation | `cr3tool --patch <its rePatch folder> ...` over an unpatched Vita copy (`cr3view`: `translation/` in the game folder) |

An encrypted Vita dump (NoNpDrm, still PFS-encrypted) has to be installed first (Vita3K does it).

## Your copy

Nothing has to be given on the command line: put your copy in the **game folder** and start the
program - it finds it by itself. The game folder is:

| Platform | The game folder |
|---|---|
| Windows, Linux | the folder holding `cr3view.exe` / `cr3view` |
| Linux AppImage | the folder holding the `.AppImage` |
| macOS | the folder holding `cr3view.app` (its `Contents/Resources/` works too) |
| Android | `Android/data/local.preservation.chaosrings3/files/` on the shared storage (copy your files there over USB) |

(then the working directory too). In it, a folder for each release you have - any one is enough:

```
<game folder>/
├── cr3view                          (or cr3view.exe, cr3view.app, the .AppImage)
├── ios/
│   └── ChaosRings3_FINAL_ENG.app/      the iPhone / iPad 1.2.2's .app: its ~170 CRDB*.ios.mvgl and movies
│                                       - or Payload/ChaosRings3_FINAL_ENG.app/ (the .ipa unpacked as it is)
├── android/
│   └── <the 1.1.3 .apks>               (or its base.apk with the asset packs' CRDB{bgm,voice,se,mov}.android.mvgl
│                                        anywhere under android/)
├── vita/
│   └── PCSG00500/                      the installed title: CR3/resources/**.psp2.mvgl (or the Prequel Trilogy's PCSG00457)
├── translation/
│   └── rePatch/                        the fan translation's rePatch folder (rePatch/PCSG00500/CR3/resources/*.psp2.mvgl):
│                                       laid over an unpatched Vita copy as the language "English (fan translation)"
└── textures/
    ├── export/                         written by --export-textures: the pictures to upscale (<hash>.crraw)
    └── hd/                             read by the game: the upscaled pictures (<hash>.crhd)
```

A release's folder may hold the release itself or have it one or two folders down, as above
(`ios/Payload/....app`, `vita/PCSG00xxx/`), so a copy can be dropped in as it was unpacked or
installed; the names of the folders below `ios/`, `android/` and `vita/` don't matter. A Vita copy
has to be the installed, decrypted title (an encrypted NoNpDrm dump: install it first, e.g. with
Vita3K). The release downloads come with these folders made, each with a `README.txt` saying what
goes in it.

CR3-U plays one release at a time: with several, **the first of `vita/`, `android/`, `ios/`** (the
same order as the other ports take one alone); `--data` picks another. `translation/` is used only
with the Vita release (the iOS and Android ones have English of their own); a Vita copy with the
patch already installed shows that English without it.

If nothing is found the program says where it looked. The older places still work: `ipad/`, `PCSG00500/`, `PCSG00457/`, `ChaosRings3_FINAL_ENG.app/`, `Payload/` or `data/` straight in the game folder.
To use a copy elsewhere: `--data <a release: its folder, .apks, archive>` (or `CR3_DATA`), `--patch <the rePatch folder>`. Settings and saves go to the profile folder
(`~/.local/share/DuIslingr/CR3-U/` on Linux; the same folders under `%APPDATA%\` on Windows and
`~/Library/Application Support/` on macOS), not the game folder. All four ports (CR1:U, CRO-U, CR2-U, CR3-U) look for their copies the same way.

## HD textures

Upscaled textures you make yourself, from your own copy - waifu2x or Real-ESRGAN, drawn in place of
the originals:

1. `cr3view --export-textures` writes every picture the game draws to `<game folder>/textures/export/`
   (`<hash>.crraw`; the profile folder's `textures/export/` if the game folder can't be written;
   `--export-textures <folder>` elsewhere). Each release exports its own pictures.
2. `python3 tools/hd_textures.py <game folder>` upscales them into `<game folder>/textures/hd/`
   (`<hash>.crhd`): waifu2x (`waifu2x-ncnn-vulkan` on the PATH, or `--waifu2x <it>`) for the
   lossless and block-compressed pictures, Real-ESRGAN (`--esrgan <the folder with
   realesrgan-ncnn-vulkan and models/>`) for the 16-bit ones - or `--engine waifu2x` for all. It
   needs Python 3 with NumPy and Pillow; `--help` lists the rest (`--scale`, `--noise`, `--only` ...).
3. Start the game: with `textures/hd/` there, HD Textures is on by itself (Options, Graphics, HD Textures switches
   it; `--no-hd-textures` for a run without). One `textures/hd/` can hold every release's.

`CR3_HD_TEXTURES=<folder>` reads them from elsewhere; the older `hd-textures/` beside the program or in
the profile folder is still read while there is no `textures/hd/`. The `tools/hd_textures.py` is the
same in all four ports. Personal use: the outputs are made from the game's art - keep them out of
anything shared.

## Layout

| Path | Purpose |
|---|---|
| `engine/` | the C++17 / SDL3 engine (`cr3core`), `cr3view` and `cr3tool` |
| `docs/` | engine and format notes (`docs/FORMATS.md`: CR3's differences first) |
| `tools/` | analysis / extraction tools (`re_setup.sh`, `mvgl.py`, `msg.py`, ...); they work on your own copy |
| `android/`, `packaging/` | the Android project; the icons, desktop entry and release notes |
| `re/`, `extracted/` | (not in the repository) local RE output and decoded assets - never commit or share |
