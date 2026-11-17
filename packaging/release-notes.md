# CR3-U v0.7.3

Touch play in battle as the original has it, the port's own menus in nine languages, and fan
translations as loose files. Since [v0.7.2](https://github.com/DuIslingr/CR3-U/releases/tag/v0.7.2);
everything in its notes still applies.

## New

- **Menu Language** (Options > Other): the port's own text - its Options rows and help lines, the first
  start's setup, the Vita save import - in English, Japanese, French, German, Spanish, Simplified or
  Traditional Chinese, Korean or Russian. *Auto* follows the game text's language; until you pick one,
  your system's language is used. The game's own text still comes from your copy. The translations are
  machine translations (the Russian Options wording is metastigma's), not yet reviewed by native
  speakers: corrections are welcome, and a `lang/<code>.po` beside the game can be tried without
  building (`engine/lang/README.md`).
- **Fan translations as loose files:** a translation's files now also work taken out of its
  `.psp2.mvgl` archives, in `translation/<code>/` - its message files straight in the folder, or in the
  archives' own folders (`message/`, `images/` ...). Beside the archives, loose files are read over
  the archives' copies, so a few corrected files can sit beside the whole patch.

## Fixed

- **Battle by touch:** tapping a skill or item that isn't highlighted now highlights it and shows its
  description; a second tap uses it, as in the original. Before, the first tap used it, so the
  descriptions couldn't be read.
- **A back button in battle:** while choosing commands, skills or targets, the original's back button is
  in the bottom-right corner. From the targets it goes back to the skill list with that skill still
  highlighted, from the list to the commands, from the commands to the previous character or the first
  menu. Backspace, the Android back key and the pad's × now go back the same way.
- **Long descriptions:** a help line too long for its box no longer runs off the screen; after a
  moment it scrolls sideways inside the box, as the original's do.
- **Linux, AMD graphics:** FSR 4 is a little faster at 4K (about 0.3 ms a frame).

### Thanks

- [ferrervicente14-creator](https://github.com/ferrervicente14-creator) reported the battle's skill
  descriptions and the missing back button ([#7](https://github.com/DuIslingr/CR3-U/issues/7)).
- [metastigma](https://github.com/metastigma) asked for the menus in every language
  ([CRO-U #10](https://github.com/DuIslingr/CRO-U/issues/10)) and for loose translation files
  ([CRO-U #11](https://github.com/DuIslingr/CRO-U/issues/11)), and wrote the Russian Options wording.

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
