# CR3-U v0.3.0

A from-scratch reimplementation of *Chaos Rings III* for Windows, macOS, Linux and Android.

## What's new

- **Camera:** the field's camera can be turned again, as in the iPad and Android releases. On a touch
  screen, Two-Handed (the default) walks on the left half and turns the camera on the right;
  One-Handed walks on the bottom and turns on the top, both at once with two fingers, with the
  game's camera pad. On a PC, hold the right mouse button. The Options' Controls choice now does
  this and is remembered.
- **Minimap:** dungeons show the game's minimap at the top left: the area's map, its warps and
  objects, where you are and which way the camera looks.
- **Field buttons:** they start folded behind the top-right button, as in the game; tap it (or Tab,
  the controller's triangle) to open them. Battle and the encounters' switch show in dungeons once
  the story gives them.
- **Fixed:** the Camp button opened the Special Shop, and My Page and SpShop were swapped the same
  way - the game's names for those three buttons don't match their pictures.

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
