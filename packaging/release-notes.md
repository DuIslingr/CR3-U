# CR3-U v0.2.0

A from-scratch reimplementation of *Chaos Rings III* for Windows, macOS, Linux and Android.

## What's new

- **New Game Plus:** completed saves are marked CLEARED. Open one and choose Continue, New Game Plus, or Back. New Game Plus lets you choose an empty destination slot, keeps the original save, and carries levels, genes, equipment, items, currency, collections and records into a new story run. Story, quests and VS progression restart; the protagonist keeps the equipped outfit from the opening tower scene onward.
- **12 save slots:** four pages of three native file panels, with Copy and Delete across pages. Page controls sit centered below the save buttons.
- **Save-menu readability:** restored the native silver NEW GAME panels for empty slots and improved the text on existing saves, including the added slots.
- **Vita save importing:** import decrypted Vita saves from title-screen Options → Other → Vita Saves → Import. Automatic backups, an optional command-line converter and instructions are included.
- Save/load fixes preserve each slot's story flags, encounter setting, treasure-box state and field position.

Later parts of the story remain untested end to end, and some battle effects are still missing.

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
