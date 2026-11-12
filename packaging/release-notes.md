# CR3-U v0.5.0

A from-scratch reimplementation of *Chaos Rings III* for Windows, macOS, Linux and Android.

This release is a parity pass: the game's own code was read, system by system, against what the
port did, and the differences fixed. `docs/OPENING_AUDIT.md` lists each one - the original, the port
before, the port now.

## What's new

### Dungeons and towns

- **Footsteps:** each kind of floor has its own sounds (one of three each step), some a puff of
  dust. Riding has its own.
- **Falls:** walking off a ledge plays the fall and the landing.
- **Ladders** work: the "!" at either end, climb up or down a rung at a time, off at the other end.
  Not while riding.
- **Riding:** the animal in the dungeons where you ride it, its own speed, camera and motions, and
  the goods button to get off (not on every floor, as in the original).
- **The toy camera** (goods): turn it with a drag or the right stick, the shutter's iris, photos of
  the spots that count, and the taller frame on iPad-shaped screens.
- **The 2D scenes** (the castle picture's pan) play.
- **Walking and running** used each other's animation; fixed.
- **People's faces move:** expressions in the story scenes and on people in towns.
- **Story scripts:** the event boxes and things you check switch on and off as the story says, the
  player is walked to places, and the field freezes as a battle starts.

### Story scenes

- **Blurs** - the motion, zoom and "special" blurs in hundreds of scenes - and the **glow** some
  scenes turn on.
- **The screen shakes** at the right size and speed, in battles too.
- **A full gene box no longer loses a story reward:** you choose one to let go first, as in the
  original, then see the new gene's stats.
- **The party window** some scenes open works, and asks "Are you satisfied with this party?" (or
  warns when someone has no gene) before the story goes on. Locked party members show greyed.
- **Dialogue voices** play by themselves where the game cues them, and stop with the window.
- **The blank dialogue bar** (issue #5) with an Android copy from a split bundle: the game's font is
  found inside it, and a system font is used if it still can't be.

### Battles

- **Finishers:** Last Attacks and Finest Arts happen as in the original - after a combo, with the
  attacker's own camera and the white flash - and a Finest Arts sends the break gauge back to half.
- **Boss deaths** play their own camera, light pillars and shake; scripted battles' cameras, effects
  and holds work (battle 7 no longer locks with the party held).
- **Ranged attacks** get their own cameras; random-target attacks hit one foe per strike.
- **Timing, voices and spells:** CR3's own attack and effect timings, shots you can see in flight,
  and the gene-card overlay on spells.
- **Status effects:** the "can't act" effects and the two transformations (the pig form's Black
  Impact) are checked the way the game does.

### Battle Mode (VS)

- **The tutorial plays:** the starter gene cards, the party window, the first tournaments.
- **The commentators** talk: at the start, each turn, on a Finest Arts, at a win or a loss.

### Camp

- **Gene Swap** shows only with a gene in the box to swap. On the Vita controller the focused panel
  has the game's own frame instead of a colour tint.

### Graphics

- **Bloom** is on where the story turns it on, the same on Vulkan and OpenGL.

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

- **Android, updating:** installs over v0.4.0. From a version before v0.4.0, uninstall the old one
  first (each was signed with a different key); that deletes the game files you copied in and your
  saves, so keep a copy of them.
- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine), Android (the emulator). The macOS build is
  made by CI but hasn't been run on real hardware yet. The Vita controls were tested with a virtual
  controller, not a physical one. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Building from source).
- Licence GPL-2.0-or-later (the macOS app is GPL-3.0 because of MoltenVK). A fan preservation
  project, not affiliated with Square Enix or Media.Vision.
