# CR3-U v0.7.1

A fix release for [v0.7.0](https://github.com/DuIslingr/CR3-U/releases/tag/v0.7.0): a controller Windows
stopped finding, and missing text on some Android phones. Everything in v0.7.0's notes still applies.

## Fixed

- **Flydigi controllers on Windows:** a Vader 4 Pro, wired or through its receiver, wasn't found
  since v0.7.0's SDL update. The Windows build now uses SDL's development version from 2026-11-14,
  which has SDL's later Flydigi fixes - the setup metastigma's fork uses and its user confirmed works
  with this controller. Other platforms keep SDL 3.4.20. Every controller SDL finds is now written to
  `cr3view.log`, so if one still isn't picked up, the log shows why.
- **No text on some Android phones:** on phones whose maker ships its own fonts, without Android's
  usual font files where CR3-U looked, text the game's own font doesn't cover - and all text, when the
  game's font couldn't be read - went missing. CR3-U now reads the phone's own font list and uses the
  fonts it names (Japanese first, then Chinese and Korean), then the phone's default font.
- **Cheats in story battles you're meant to lose:** Infinite HP no longer leaves you stuck in a battle
  you can't win - once no foe can fall, it lets the party fall and the story goes on - and winnable
  story battles can still be won with the cheats on, for their rewards.
- **Menus on phones wider than 16:9:** on a 20:9 phone the menus' help line, title bar and footer
  stopped short of the screen's right edge (VS mode's header and footer short of both). They now reach
  the edges; with Camera Cutout set to Avoid, the bars still run to the edges while their buttons stay
  inside the safe area.
- **Options on 4:3 screens:** the Reset button covered the last row's arrow, and Back covered the
  Other tab's Copyright Notice. Reset now sits immediately left of Back, and every tab's list stops
  above them - the rows below a scroll away (▼, the wheel, a swipe or the pad). The empty Download
  panel is gone from the Options footer.

### Thanks to metastigma

[metastigma](https://github.com/metastigma) reported the Flydigi problem and investigated it in their
fork of the sister port ([CRO-U-metastigma#42](https://github.com/metastigma/CRO-U-metastigma/pull/42),
[#43](https://github.com/metastigma/CRO-U-metastigma/pull/43),
[#45](https://github.com/metastigma/CRO-U-metastigma/pull/45)); the Windows build takes the SDL
version their fork showed works.

## Changed

- Cheats moved to its own tab in Options (it was under Graphics in v0.7.0).

## Downloads

| File | Platform |
|---|---|
| `CR3-U-windows-x64.zip` | Windows 10+ x64 |
| `CR3-U-macos-universal.zip` | macOS 11+ (Apple silicon and Intel) |
| `CR3-U-x86_64.AppImage` / `CR3-U-linux-x86_64.tar.gz` | Linux x86-64 |
| `CR3-U-android.apk` | Android 7+ (64-bit) |
| `CR3-U-ios-unsigned.ipa` | iOS 15+ (sideload; not signed) |

## Getting started

1. Download the file for your system and unzip it.
2. Start `cr3view`. If it can't find your copy of the game, it asks you for one and sets it up.
3. Play.

- **Windows:** if SmartScreen warns about an unsigned program, choose *More info*, *Run anyway*.
- **macOS:** right-click `cr3view.app` and choose *Open* (it isn't signed).
- **Linux:** `chmod +x` the AppImage, then run it.
- **Android:** install the APK and start it; the setup asks for your game files. Or copy them over
  USB into `Android/data/local.preservation.chaosrings3/files/`, in an `ios/`, `android/` or `vita/`
  folder. An Android copy needs its asset packs: give the whole `.apks`, not the `base.apk` alone.
- **iOS:** sideload the `.ipa` (AltStore, Sideloadly or similar) and start it; the setup asks for your
  files. Or copy them into *On My iPhone > CR3-U* in the Files app (or the device's Files in Finder),
  in an `ios/`, `android/` or `vita/` folder, and choose *Look again*.

The README's "What you need" section lists which releases of the game work and what each needs.

## Good to know

- **The first start with a repacked Android `.apks` takes a while** (about 1.3 GB unpacked once, with
  a progress window); make sure there's room beside it.
- **Android, updating:** installs over v0.4.0 and later.
- **No game data is included.** You need your own copy; the port reads it at run time. The Vita
  decryption uses console keys the Vita scene published years ago (`THIRD_PARTY.md`).
- **Tested on:** Linux (played), Windows (under Wine), Android (the emulator), iOS on an iPhone Duo
  (the companion ports' builds). The macOS build is made by CI but hasn't been run on real hardware.
  Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Building from source).
- Licence GPL-2.0-or-later (the macOS and iOS apps are GPL-3.0 because of MoltenVK). A fan
  preservation project, not affiliated with Square Enix or Media.Vision.
