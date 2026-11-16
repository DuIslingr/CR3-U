# CR3-U v0.7.2

A fix release for [v0.7.1](https://github.com/DuIslingr/CR3-U/releases/tag/v0.7.1): a controller that
still did nothing, and the player sliding in some scenes. Everything in v0.7.1's notes still applies.

## Fixed

- **Flydigi controllers, take three:** v0.7.1's SDL update found the Vader 4 Pro, but it shows up as
  two "Xbox 360 Controller" devices and its input comes on the second, which CR3-U never opened. Now
  every controller is opened, and the one you press or move takes over, with any make of controller.
- **The player sliding in scenes:** when a scene walks the player to a spot (such as turning you back
  at an area's edge), the player slid there in a standing pose. They now walk.

### Thanks to metastigma

[metastigma](https://github.com/metastigma) sent the log that showed the Vader 4 Pro's second device
([CRO-U #4](https://github.com/DuIslingr/CRO-U/issues/4)).

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
