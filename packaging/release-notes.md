# CR3-U v0.7.0

A from-scratch reimplementation of *Chaos Rings III* for Windows, macOS, Linux, Android and now iOS.

This release carries the parity audit to its end: system by system, the game's own code was read
against the port and the differences fixed (`docs/OPENING_AUDIT.md`, rounds 19 to 32). What's still
open needs footage of the original to settle, is online-only, or is code nothing reaches. It also
brings an iOS build, a first start that no longer looks frozen, and a Cheats page.

## What's new

### Starting the game

- **No more "frozen" first start** (issue #6). A repacked Android `.apks` has about 1.3 GB of sound,
  voices and movies to unpack the first time; that used to happen before any window opened, so the
  game looked hung (and closing it started the unpacking over). A progress window shows it now, and
  for an `.apks` that holds its APKs compressed too. Later starts skip it.
- **Copy your game in, any way:** an `.ipa` dropped into `ios/` is unpacked, and a Vita dump's
  `.zip`, `.rar`, `.7z` or `.vpk` dropped into `vita/` is unpacked and decrypted, with a progress bar.
- **"Look again":** if no copy is found, the message has a *Look again* button - copy your files in
  and carry on, no restart.
- **A log file**, `cr3view.log` in your profile folder (Documents on iOS), the last start's kept as
  `cr3view.prev.log`. Attach it to a bug report.
- **Your own font:** put a `.ttf` / `.otf` / `.ttc` in the game folder's `font/` and the game's text
  uses it.
- The Prequel Trilogy's NoNpDrm `.rar` now unpacks on every platform (a newer libarchive).

### Battles

- **Gene skills** fire the way the game does: when their trigger and condition hold, with their uses
  per battle, turn and command. Critical x2, Critical First Attack, Narrow Escape, Emergency Guard,
  Afterthought Guard, Counter Ignition and Adam's element skills now work; several never fired before.
- **Status effects** wait for their moment and get used up - Mental Wall's reflect, Moonlight Shadow's
  evade, Rally, Concentrate, Lock On, the counters, Regenerate - and their icons show on the party
  panels and the target list.
- **The monsters think** as in the original: their own command choices and targets, Call for Help,
  stealing HP / MP / gold, Reverse Gravity, the Mimic's escape, Avatar summons, Gene Switch, Cover.
- **Auto Battle and Mass Attack** tabs on the Battle button.
- **Assists** from your helpers by cheer points ("Thanks to Patty's help...").
- **Analyze** shows a monster's name, HP and description.
- **Break Reset**, finishers after the turn's last command, counters in their right place in the
  turn, cooperation names (" II", " III"), damage totals on the party, fixed-damage hits and blocks.
- **The result screen:** gene level-ups with their windows and learned skills, drops in the
  Information bar, the VIP bonus panels, steal gold back from a fallen thief.
- **Hit effects** land at varied angles, offsets and sizes; fire bullets leave their embers behind.

### Battle Mode (VS)

- **The commentators** say all their lines - each turn, on a joint attack, a gene command, after
  damage or a miss, on continuing, retiring and a perfect run - and greet you as you enter VS.
- **The score panel** and **the VS footer** as the game shows them (digits the right way round).
- **Prizes and rewards:** the season token, the gene prize going to the stock when the box is full.
- **Retiring** is a loss without the defeat lines.

### Dungeons, towns and menus

- **Gems** to pick up (99 trade for a Continue Gem), **doors** that open, **gold chests** with their
  collected counts.
- **The title:** the Square Enix / Media.Vision logos and the CHAOS RINGS emblem, and the Story / VS
  choice after the story unlocks it.
- **Camp:** the gene list's Skills view; **Options:** Credits, Copyright, Title Screen and Manual.
- **Space Port:** its backdrop, the Lucky Box's five panels and slide (swipe too), Quest Clear's
  stamp, NEW badges, the goods button's dots and names, the VS header's "SAVING.." icon.
- **The late dungeons' glow** (d12xx-d14xx's skies), on Vulkan and OpenGL.
- **A safety net:** falling through the floor puts you back on the last safe ground.
- **The talk mark** over what you can talk to, and the battle lists' skill icons (from a player's video).

### Controls and display

- **F11 / Alt+Enter** switch between window and fullscreen.
- **Controllers** can be swapped while playing: another connected pad takes over when one is removed.
- **Phones:** no Resolution or Display options (the whole screen is used), and a **Camera Cutout**
  option - *Avoid* keeps the menus clear of the punch hole, *Ignore* uses the whole screen.

### Cheats

- **Options > Graphics > Cheats**, every cheat off unless you turn it on: infinite HP, infinite MP,
  one-hit kills, an EXP multiplier (1x to 10x), always first strike, no random battles, max money,
  full HP / MP outside battle - and CR3's own: an **always-full Break gauge** (Finest Arts ready),
  always co-op, always drop, and full cheer for assists. Battle cheats don't apply in Battle Mode,
  so its scores and prizes stay a fair contest.

### iOS (new)

- **An iOS build** (unsigned `.ipa`, to sideload), feature-complete with Android: the first-start
  setup with the document picker and progress bars, your files in the app's folder in the Files app
  or Finder, 120 Hz on ProMotion screens, and full-screen on the iPhone Duo's inner and cover screens.
- Switching to another app and back no longer leaves the picture frozen.
- The message band reaches the screen's edges past a camera cutout, its text kept clear of it.

### Thanks to metastigma

Thanks to [metastigma](https://github.com/metastigma), whose fork of the sister port
([CRO-U-metastigma](https://github.com/metastigma/CRO-U-metastigma)) led to two of this release's
changes, brought over from CRO-U: the controller hot-plug, after their Windows XInput work, and
F11 / Alt+Enter, which their fork had first
([1ec1f11](https://github.com/DuIslingr/CR3-U/commit/1ec1f11); in CRO-U
[0571f0f](https://github.com/DuIslingr/CRO-U/commit/0571f0f) and
[81a71a1](https://github.com/DuIslingr/CRO-U/commit/81a71a1)).

### Under the hood

- The Vita decryption is the project's own code now (the library it used had no licence), and a bug
  that made it give garbage on macOS - and likely iOS and ARM Android - is fixed.
- SDL 3.4.20, MoltenVK 1.4.2.

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
