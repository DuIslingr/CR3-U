# CR3-U v0.3.2

A from-scratch reimplementation of *Chaos Rings III* for Windows, macOS, Linux and Android.

## What's new

- **The first battle no longer freezes.** Every battle started from the field (an event's, or an
  encounter's) stopped on its opening screen. It now plays through, and the game returns to the
  field afterwards.
- **Spanish fan translation:** Joseas's Spanish translation of the Vita release works like the
  English one, with its own font for á, ñ, ¿ and ¡. Put its rePatch folder, or the `.zip` it comes
  in, into `translation/`.
- **Both translations at once:** the English and Spanish translations can be installed side by
  side (`translation/en/` and `translation/es/` - an archive dropped into `translation/` is unpacked
  into the right one on the next start). The in-game **Options > Language** switches between
  Japanese, English and Spanish **while you play**, and the choice is remembered.
- **Long choices fit:** a choice line too long for its bubble (common in the translations) now gets
  a wider bubble instead of running past its edge. On a 3:2 or 4:3 screen, where there's no room to
  widen it far enough, the line is centred and narrowed slightly.
- **Android:**
  - A repacked `.apks`: sound effects, voices and text work. Its compressed asset packs are unpacked
    once, on the first start, into `assetpacks/` beside it (**about 1.3 GB of free space**; that
    first start takes a while).
  - The first start lets you pick your game files or folder on the phone, and a Vita dump copied
    into `vita/` is decrypted there, as on a PC.
  - Fullscreen, with the status and navigation bars hidden.
- **Phones:** the picture now fills the whole screen, under the camera cutout and into the rounded
  corners; only the menus, windows and buttons keep clear of them.
- **Upscalers** (FSR, DLSS, MetalFX) now apply to the 3D scene only; menus and text are drawn
  afterwards at full resolution and stay sharp.
- **Vita, the Prequel Trilogy:** CR3-U no longer mistakes another game's archives in a Trilogy
  folder for its own.

The story still hasn't been played through end to end. The Android build has been run on the
emulator, not yet on a real phone; a real Windows PC without Vulkan is still untested.

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

- **Android, updating:** from this version on, each release installs over the one before it. Every
  earlier version was signed with a different key, so coming from one, uninstall it first - one
  last time. That deletes the game files you copied in and your saves: keep a copy of the game
  files to copy back.
- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine), Android (the emulator). The macOS build is
  made by CI but hasn't been run on real hardware yet, nor the Android one on a phone. Reports are
  welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Platforms).
- Licence GPL-2.0-or-later (the macOS app is GPL-3.0 because of MoltenVK). A fan preservation
  project, not affiliated with Square Enix or Media.Vision.
