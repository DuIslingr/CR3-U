# CR3-U v0.2.2

A from-scratch reimplementation of *Chaos Rings III* for Windows, macOS, Linux and Android.

## What's new

- **Original Card Art:** a saved toggle in **Options → Graphics** restores the original artwork for Gene Index entries **80, 81 and 86**, in menu cards, gene icons and full-screen views. It is Off by default and applies immediately.
- **Matching names and profiles:** restored cards use **Little Red Riding Hood, Aphrodite and Adam**, with their original N/R/SR descriptions. English and fan-English modes use English translations of the Japanese originals; Japanese mode retains the original text. Turning the option Off restores the selected release's art and text.
- **Local artwork sources:** iOS and Vita already contain all three originals. Android contains the originals for 80 and 81 and shows **On (2/3)** until card 86's original is supplied. Missing art keeps its release name and profile too.
- **Cross-platform artwork:** correct the row order when borrowing Vita textures for mobile data, refresh cached cards when toggling, and fit the longer original name within its native profile banner. Gene IDs, stats, rarities and saves remain unchanged.

### Supplying card 86's original for Android

Copy both card archives from your own decrypted Vita or iOS copy into the game's `artwork/` folder:

- Vita: `CRDBcard01.psp2.mvgl` and `CRDBcardhd.psp2.mvgl`.
- iOS: `CRDBcard01.ios.mvgl` and `CRDBcardhd.ios.mvgl`.

Subfolders work; no main archive is required. These files supply pictures without switching the gameplay release. On Android, use `Android/data/local.preservation.chaosrings3/files/artwork/`. If the game is running, turn the option Off and On after copying. **No game artwork is included in the port.**

Checked with Android, iOS and Vita data, including Japanese and fan-English text, live art switching, missing/corrupt sources and saved settings. Desktop and Android native builds and regression tests pass locally. Physical Android interaction remains untested.

Resolves [issue #1](https://github.com/DuIslingr/CR3-U/issues/1). Later parts of the story remain untested end to end, and some battle effects are still missing.

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
