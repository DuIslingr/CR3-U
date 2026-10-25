# CR3-U v0.1.0

A from-scratch reimplementation of *Chaos Rings III* for Windows, macOS, Linux and Android.
**First release.**

## What's in it

- **Playable from the title screen** through the prologue, the first chapter and the first
  dungeons: story scenes with voices, towns and dungeons, battles, the camp, the gene lab, shops,
  quests and the VS mode.
- **Online features work offline.** CR3-U never contacts Square Enix's servers.
- **Any one release is enough:** iOS 1.2.2, Android 1.1.3, or the Vita (`PCSG00500`, or the
  Prequel Trilogy's `PCSG00457`). On the Japanese Vita release, the fan translation can be added
  as an English language option.
- **Easy first start:** with no game found, it asks for your copy (an iOS `.ipa`, an Android
  `.apks`, or a Vita / Vita3K folder, including NoNpDrm dumps) and copies what it needs next to
  the program.
- **Options:** a Graphics tab (resolution, frame rate, FSR 3.1 / MetalFX upscaling, MSAA, texture
  filtering), Field Speed and Battle Speed (2x to 5x), Smooth Models, HD textures and models, and
  the add-on outfits and weapons.

**Not done yet:** later parts of the story are untested, and some battle effects are missing.

## Downloads

| File | Platform |
|---|---|
| `CR3-U-windows-x64.zip` | Windows 10+ x64 |
| `CR3-U-macos-universal.zip` | macOS 11+ (Apple silicon and Intel) |
| `CR3-U-x86_64.AppImage` / `CR3-U-linux-x86_64.tar.gz` | Linux x86-64 |
| `CR3-U-android.apk` | Android 7+ |

## Getting started

1. Download the file for your system and unzip it.
2. Start `cr3view`. If it can't find your copy of the game, it asks you for one and sets it up.
3. Play.

- **Windows:** if SmartScreen warns about an unsigned program, choose *More info*, *Run anyway*.
- **macOS:** right-click `cr3view.app` and choose *Open* (it isn't signed).
- **Linux:** `chmod +x` the AppImage, then run it.
- **Android:** install the APK, then copy your game files over USB into
  `Android/data/local.preservation.chaosrings3/files/`, in an `ios/`, `android/` or `vita/` folder.

The README's "Your copy" section lists exactly which files each release needs.

## Good to know

- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS and Android builds are made by CI
  but haven't been run on real hardware yet. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Platforms).
- Licence GPL-2.0-or-later (the macOS app is GPL-3.0 because of MoltenVK). A fan preservation
  project, not affiliated with Square Enix or Media.Vision.
