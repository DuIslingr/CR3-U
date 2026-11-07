# CR3-U v0.3.1

A from-scratch reimplementation of *Chaos Rings III* for Windows, macOS, Linux and Android.

## What's new

- **Android, a repacked `.apks`:** sound effects, voices and text work. Its asset packs are stored
  compressed, and the port couldn't read them that way: they are now unpacked once, on the first
  start, into `assetpacks/` beside the `.apks` - **about 1.3 GB of free space**, and that first start
  takes a while. If they can't be unpacked, the game says so. The font is read from inside the
  `.apks` too, so text shows on a phone.
- **Android:** the game runs fullscreen with the status and navigation bars hidden, and keeps clear
  of the camera cutout.

The story still hasn't been played through end to end. Physical Android devices and a real Windows
PC without Vulkan are still untested.

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
