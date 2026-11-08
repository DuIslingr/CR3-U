# CR3-U v0.4.0

A from-scratch reimplementation of *Chaos Rings III* for Windows, macOS, Linux and Android.

## What's new

### Android and phones

- **Sound and text from an `.apks`:** a repacked `.apks` gave no sound effects or voices, and on a
  phone no text. Its compressed asset packs are now unpacked once on the first start, into
  `assetpacks/` beside it (**about 1.3 GB of free space**; that first start takes a while), and the
  game's font is read from inside the `.apks`.
- **`.apks` and `.xapk` with compressed APKs** (as some download sites pack them) work: they are
  unpacked once into `unpacked/` (**about 3.7 GB with the asset packs**). Before, the game stopped
  at once with "no Chaos Rings III data".
- **The first start's setup runs on the phone:** pick your `.apks`, `.ipa` or Vita archive and the
  files are copied in. A Vita dump copied into `vita/` over USB is decrypted on the next start.
- **The screen fits:** the game runs fullscreen with the system bars hidden. The picture fills the
  whole screen, round the camera hole and into the corners, while menus, windows and buttons stay
  inside the safe area. On 19.5:9 and 20:9 phones the message band now reaches the screen's sides,
  and the speaker's name no longer hangs off its corner.
- **No more green frame** round the screen after using a controller or keyboard.
- **Updates install over the last:** the APK is now signed with the ports' release key (see Good
  to know).

### Battles and story

- **The first battle no longer freezes.** Every battle started from the field (an event's or an
  encounter's) stopped on its opening screen. It now plays through and returns to the field.

### Controller, as on the Vita

With the Vita copy as the game, the controller now follows the Vita release's own buttons:

- **Field buttons:** △ opens them with a cursor on Camp; the d-pad moves it, ○ presses, × or △
  closes. L swings the camera behind you; R cycles the goods in reach and □ uses them.
- **Camera:** the right stick turns the field's camera.
- **Event choices and Yes / No questions:** a cursor starts on the first option; Up / Down move it,
  ○ answers, × is No.
- **L / R in menus:** the gene lab's, options' and Gene Index's tabs, the shop's Buy / Sell, the
  gifts' and quests' categories, the Lucky Box's boxes, the tutorials' pages.
- **Battles:** the d-pad works the commands, item lists and targets, L / R switch Items and Gifts,
  flexible-target commands can switch sides, and ○ shortens the victory camera and the result.
- **A lost battle's Continue:** Up / Down choose Give Up or Revive, ○ presses. Before, it took only
  clicks.
- **Every other screen** has a d-pad cursor too: the camp and party order, item quantities, gene
  change (sorts, lock), the gene lab, gifts, options, Gene Index, premium shop, My Page, save files,
  world select, the VS mode and the lunches.
- **Movies and Vista scenes:** △ pauses and resumes picture and sound together; □ stops a movie.
- **START** taps the title.
- `docs/CONTROLS.md` lists every screen's buttons. With the iPhone or Android copy the controller
  works as before.

### Fan translations

- **Spanish:** Joseas's Spanish translation of the Vita release works like the English one, with its
  own font for á, ñ, ¿ and ¡. Put its rePatch folder, or the `.zip` it comes in, into `translation/`.
- **Both at once:** the English and Spanish translations can sit side by side (`translation/en/`,
  `translation/es/`; an archive dropped into `translation/` is unpacked into the right one).
- **Options > Other > Language** switches between Japanese, English and Spanish **while you play**,
  pictures included (the title's "Tap to play"), and remembers the choice.
- **Long choices fit:** a choice too long for its bubble gets a wider one; on 3:2 and 4:3 screens it
  is centred and narrowed slightly.

### Graphics

- **Fewer hitches with Vulkan:** compiled shaders are kept between runs (`vulkan-pipelines.bin` in
  the profile folder), so the start and each new screen are smoother.
- **Sharp menus with upscalers:** FSR, DLSS and MetalFX now upscale the 3D scene only; menus and
  text are drawn afterwards at full resolution.
- **FSR 4** (AMD RX 6000 or later, Linux and Windows): a new Upscaler choice. AMD's files can't be
  included; put them in `fsr4/` (its `README.txt` lists them). On Linux they run natively through
  ffx-vk-linux, without Wine.
- **FSR 3.1.6:** the built-in FSR is updated to AMD's FSR SDK 2.4.

### Vita copies

- **Prequel Trilogy:** CR3-U no longer takes the other games' archives in a Trilogy folder for its
  own, which left it without sound or models.

The story still hasn't been played through end to end.

## Downloads

| File | Platform |
|---|---|
| `CR3-U-windows-x64.zip` | Windows 10+ x64 |
| `CR3-U-macos-universal.zip` | macOS 11+ (Apple silicon and Intel) |
| `CR3-U-x86_64.AppImage` / `CR3-U-linux-x86_64.tar.gz` | Linux x86-64 |
| `CR3-U-android.apk` | Android 7+ (64-bit) |

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

The README's "What you need" section lists which releases of the game work and what each needs.

## Good to know

- **Android, updating:** from this version on, each release installs over the one before it.
  Earlier versions were each signed with a different key, so uninstall the old one first - one
  last time. That deletes the game files you copied in and your saves, so keep a copy of them.
- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine), Android (the emulator). The macOS build is
  made by CI but hasn't been run on real hardware yet. The Vita controls were tested with a virtual
  controller, not a physical one. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Building from source).
- Licence GPL-2.0-or-later (the macOS app is GPL-3.0 because of MoltenVK). A fan preservation
  project, not affiliated with Square Enix or Media.Vision.
