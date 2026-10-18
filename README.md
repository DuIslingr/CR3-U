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

## Status: early

This is the start of the port. What works today:

- **Data, all three releases standalone**: a release is found from its folder and every one of its
  archives indexed together (`engine/src/assets/mvgl.hpp`, `Database`) - the iPhone's ~170
  `CRDB*.ios.mvgl` and loose movies, Android's `main.10007.android.mvgl` (read straight out of the
  APK or `.apks`, no unpacking) and its asset packs, the Vita's ~236 `CR3/resources/**.psp2.mvgl`.
  Every file of all three decodes (`cr3tool check`); every texture, mesh, skeleton and animation
  parses (`cr3tool images` / `models` / `animations`) - including CR3's new 64-bit mesh and
  animation layouts (geom version 0x64, iOS / Android) and the Vita's triangle lists
- **Languages**: Japanese and English on iOS / Android; the Vita's Japanese - and the **fan
  translation** ([Chaos Rings III Translation Project](https://gbatemp.net/threads/chaos-rings-iii-translation-project.443742/))
  as an *option*: given its `rePatch` folder next to an unpatched Vita copy, "English (fan
  translation)" is a language of its own and the Japanese stays (`--patch`, `assets/language.hpp`).
  A Vita copy with the patch already installed over it shows that English
- **Models**: `cr3view --data <release> --model c101.geom` draws any figure skinned, textured and
  animated, the same from each release (`--capture` to a BMP)

The rest - fields, events, battles, menus, sound - is CR2-U's game layer still, and does not run
CR3 yet: CR3's own (its `Fld2` field, the gene system, its scripts' natives) is the work ahead.

## Quick start

```sh
cd engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
build/cr3tool release "<your copy>"
build/cr3view --data "<your copy>" --model c101.geom
```

| Your copy | `<your copy>` |
|---|---|
| iPhone / iPad 1.2.2 | the unpacked `.ipa` (or its `Payload/ChaosRings3_FINAL_ENG.app`) |
| Android 1.1.3 (`com.square_enix.chaosrings3gp`) | the `.apk` / `.apks`, or a folder with the APK unpacked; the asset packs (`CRDB{bgm,voice,se,mov}.android.mvgl`) anywhere under it |
| PS Vita PCSG00500 / PCSG00457 | the installed (decrypted) title's folder, or its `CR3/resources` |
| + the fan translation | `cr3tool --patch <its rePatch folder> ...` over an unpatched Vita copy |

An encrypted Vita dump (NoNpDrm, still PFS-encrypted) has to be installed first (Vita3K does it).

## Layout

| Path | Purpose |
|---|---|
| `engine/` | the C++17 / SDL3 engine (`cr3core`), `cr3view` and `cr3tool` |
| `docs/` | engine and format notes (`docs/FORMATS.md`: CR3's differences first) |
| `tools/` | analysis / extraction tools (`re_setup.sh`, `mvgl.py`, `msg.py`, ...); they work on your own copy |
| `android/`, `packaging/` | the Android project; the icons, desktop entry and release notes |
| `re/`, `extracted/` | (not in the repository) local RE output and decoded assets - never commit or share |
