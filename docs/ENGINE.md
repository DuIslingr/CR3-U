# CR3-U's engine: building and tools

> Inherited from CR2-U with its names changed; it describes CR2-U's engine and game layer, which CR3-U still carries while CR3's own is written.

## Building

```sh
cd engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

C++17; SDL3 (the system's, else downloaded and built by CMake), FreeType for the text (else built
here: `CR_FETCH_DEPS`), zlib for the APK (the same), and for Vulkan the Vulkan loader + glslc -
without them OpenGL only. The build is Ninja's: `ninja -C build` (`cmake --build build` runs it).
Options: `-DCR3_GLES=ON` (the GL device on OpenGL ES 3.0, as Android has it), `-DCR_DLSS_SDK=<the
DLSS SDK>` (DLSS: README, Platforms), `-DCR3_BUNDLED_FAAD2=ON` (the bundled FAAD2 over the
system's), `-DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw-x86_64.cmake` (Windows from Linux).

## Tools

| | |
|---|---|
| `cr3view --data <archive> [--model name] [--anim name --time s] [--lights figure\|none] [--renderer vulkan\|opengl] [--capture out.bmp]` | the model viewer: drag to turn, the wheel to zoom, Left / Right for the other models, Up / Down for the figure's animations, Space to pause |
| `cr3view --data <archive> --event <function> [--vista map] [--page s]` | an event: Return or a click turns a message's page |
| `cr3view ... [--resolution WxH] [--upscaler off\|fsr3] [--quality native\|quality\|balanced\|performance\|ultra] [--sharpness 0..1] [--size WxH] [--fps n] [--run s]` | the output in any mode: 60 frames a second (`--fps`), frames of a resolution up to 3840 x 2160 (F1 steps through the window's size and 720p .. 2160p), FSR 3.1 (F2 steps through its modes), the window's size; a capture `--run`s that long first |
| `cr3view --data <archive> --field <map> [--model c001] [--at point] [--binary file] [--walk sx,sy,s]` | a field: the arrow keys run the character (Shift walks) on the floors and against the walls; the maps' table comes from the executable beside the iPad's / Vita's archive or the APK beside Android's OBB, or `--binary` |
| `cr3view --data <archive> --play [map] [--call function] [--tap] [--skip-battles] [--binary file]` | a new game (or from a map, `--call` one of its events in place of its entry - without what would call it, so no fade back in after): the field's scripts, events and vistas one after another as the game chains them; Return or a click turns a page or talks to what has a balloon (`--tap`: once, after `--walk`); the battles it reaches fought by the rules, the party attacking as a pair (`--skip-battles`: taken as won) |
| `cr3view --data <archive> --game-save <game.dat>` | as `--continue`, from the game's own save (its `system.dat` beside it read too); `cr3tool save-import <game.dat> [system.dat]` prints it as a CR3-U save, `cr3tool save-export <archive> <save> <game.dat>` writes one |
| `cr3view --data <archive> --continue` | as `--play`, from the continue data each area's entry writes (`cont.txt`), else the save the field's Save wrote (CR3-U's own format: the story's state, the map and the place, the options - in the user's app folder, `DuIslingr/CR3-U/save1.txt`) |
| `cr3view --data <archive> --battle <coupling> [--party a,b] [--fight solo\|pair] [--seed n] [--state f:s] [--charge n] [--equip c:slot:id] [--time s]` | a battle's stage: its map, the party (characters a and b: Darwin 1, Orlando 2, Marie 3, ...) and the monsters, through the commands' camera; `--fight`: played by its rules, the party attacking each turn; `--state`: fighter f (0, 1 the party, 2.. the monsters) holding state s to begin with, one use; `--charge`: the gauge to begin with; `--equip`: character c's equipment slot (3..7 its sopia); `--item id:count`, `--difficulty n` (the world select's level - 1), `--members a,b` (the field's two, from 0: Darwin 0, Orlando 1); the field's `--input` also takes `tap`, `change`, `unlock`, `unlockfail` (tests) |
| `cr3tool check / scripts / run / images / models / compare-models / animations / pose / shaders / walk / exits / fieldscript / battle / fight` | checks on an archive: every file decodes, every script loads and runs its `main`, a script run with the natives stubbed, every texture / mesh decodes, two releases' meshes compared, the shader keys against their GLSL, the field player on every map, every map's exits, every map's entry script, every encounter's set-up, a battle played by its rules (an archive may be given as its folder; `walk` / `exits` / `fieldscript` take the executable, or `-` for the release's own beside the archive) |
| `tools/mvgl.py` | `list` / `stats` / `check` / `extract` an MDB1 archive |
| `tools/nut.py` | disassemble a compiled Squirrel script (a file, or out of an archive); `names`: the keys / calls the scripts use |
| `tools/geom.py` | a mesh file's meshes and materials, a check of them all, a flat-shaded preview |
| `tools/msg.py` | the text: `check` every message, `show` one in your terminal |
| `tools/anim.py` | Edge animations: evaluate a pose at any frame; `check` every animation |
| `tools/skel.py` | Edge skeletons: base pose, hierarchy, world matrices; `check` against the inverse binds |
| `tools/gen_camera_table.py` | the battle cameras' table (BtlCameraTable and their names) from the decompilation and the library, as `engine/src/battle/cameratable.inc` |
| `tools/gen_dotnum_table.py` | the interface's digits (DotNumParamPackTable: each glyph's size and place in hud_dotnum) from the library, as `engine/src/gfx/dotnumtable.inc` |
| `tools/gen_level_table.py` | the EXP tables (BtlData's lvup_exp and the sopia's) from the library, as `engine/src/battle/leveltable.inc` |
| `tools/gen_awake_table.py` | the characters' awakenings (their commands, the flags they need, their charge costs) and the pair's advents (their flags, sopia, charge and commands by level) from the decompilation and the library, as `engine/src/battle/awaketable.inc` |
| `tools/gen_special_table.py` | the special scenes (BtlSpecialCommandList::BuildAwake / BuildAdvent: their steps, by an ARM interpreter over the library) and which command shows which (SetActionTable, compiled from the decompilation), as `engine/src/battle/specialtable.inc` |
| `tools/gen_effect_table.py` | the battle effects' settings (BtlEffectTable's static initializer) from the decompilation, as `engine/src/battle/effecttable.inc` |
| `tools/gen_motion_table.py` | the battle motions' timing table (BtlMotionTable, built in the game's code) from the decompilation, as `engine/src/battle/motiontable.inc` |
| `tools/gen_box_table.py` | the treasure boxes' items by table (Cr3TreasureBox::GetItemID's: the normal, key and fixed boxes', the fixed ones' by character) from the decompilation and the library, as `engine/src/field/boxtable.inc` |
| `tools/sq_bindings.py` | the scripts' native API, from the Android library's decompilation |
| `tools/re_setup.sh` | Ghidra groundwork in `re/` (never committed): the Android library (x86 and armv7, named), the iPad and Vita builds named from it |
| `tools/vita_self.py` | the Vita's eboot (a fake-signed SELF) to an ELF plus its imports |
| `tools/ghidra/` | headless scripts: export decompilation and features, match functions across builds, apply names |

### CR3 save slots

The title's native save screen offers 12 slots (`save1.txt` through `save12.txt`),
with three panels per page. Left/Right, Page Up/Page Down, the native arrow buttons,
and controller L/R change pages; Up/Down crosses page boundaries and wraps around.
Copy can take a source on one page and an empty destination on another. Delete still
asks twice. Selecting a slot for a new or existing game makes all subsequent manual
saves and autosaves use that slot. `--continue` defaults to slot 1.

For capture automation, `--input "title:tap save:12 save:12 save:yes"` loads slot 12;
`save:next` / `save:prev` change pages, and `save:up` / `save:down` / `save:enter`
exercise keyboard navigation. Importing Vita saves fills the original slots 1–3.

### New Game Plus

A save with main-story clear flag 6016 is marked CLEARED. Opening it adds a
New Game Plus choice alongside Continue and Back. Chapter/epilogue flag 733 alone
does not qualify. With an empty destination available, New Game Plus takes that
cleared file as its source and asks for a slot on any page. `save:plus` chooses the
option only while the cleared-file choices are open; for example,
`--input "title:tap save:1 save:1 save:plus save:12 save:yes"` creates a run from
completed slot 1 in empty slot 12. The final confirmation defaults to No.

`GameState::makeNewGamePlus` starts from fresh state and explicitly copies earned
progression: team level, genes/UIDs/equipment links/skills/upgrades and storage,
equipment and attachments, all held items, currency, gene collections, treasure
sheets, presents, names and records. Daily reward claims, VIP state and login date
survive; normal date-based login processing still runs. Held VS boosts survive,
but standings, tournaments and missions restart. Story/global-work flags (except equipment initialization),
quests, party membership, MP overrides and field transients reset. The prologue
recruits the party normally; MP starts full. Source playtime and options carry.
The `PartyEquip` kit-initialization flags 1021–1025 survive for initialized heroes:
otherwise the opening tower's `PartyEquip(1)` replaces the carried outfit, weapon,
accessory and gene with starter gear. Field, battle and vista models continue to
resolve the carried armour through the normal native model tables.

The source is reread and eligibility rechecked when confirming. Existing files,
including invalid saves, cannot be chosen as destinations. The native importer
transaction writer installs the new save before the ordinary title load starts
`t0108` at its default spawn; no source file is changed. Installation failures
show a native alert and leave the menu available for retry. `new-game-plus-tests`
covers carryover, resets, source preservation, serialization and repeated runs.

## Original card art selection

`assets/card_art` selects texture/full-screen pairs without editing gene_para or collection
records. The mobile display series 131 and 124 use retained 42 and 49; mobile series 119 uses
retained iOS series 160, or a local Vita 119 / iOS 160 pair from the two card archives.
Only exact `card_NNN` texture names are intercepted, so rarity, element and other UI atlases
are untouched. The normal texture cache and HD texture lookup use the selected image's pixels.
Changing the toggle refreshes cached textures in place and reopens an active full-screen card.

The default is `original_card_art=0` in settings.ini. A partial source keeps the release's
picture in both views. Assets are decoded as a pair before use; missing/corrupt donor archives
are skipped. Asset-only MvglFile readers preserve the primary release's animation layout,
language, scripts and database. No game assets are written or bundled.

`ctest --test-dir engine/build --output-on-failure -R card-art` uses generated color swatches
in synthetic MDB1 archives: immediate On/Off, partial sources, nested Vita/iOS donors,
malformed assets, source preference, re-enabling after files are copied, untouched primary
data and both card views. Real-data checks use the developer's copies separately.

Verified with the available Android 1.1.3, iOS 1.2.2 and Vita files: Android alone (2/3), Android
with only the two Vita card archives (3/3), Android with iOS archives (3/3), iOS alone (3/3),
and Vita alone (3/3). The original selection matches the expected decoded menu atlas and exact
full-screen image bytes; gene parameters and the primary animation layout stay unchanged.
Borrowed textures are converted to the primary release's row order before upload.

Headless viewer checks exercised the native settings row and 16 card captures: all three cards
in menu and full-screen views, plus live On/Off for card 86 after GPU textures were already
loaded. Live changes matched starting with the corresponding option in both views. Persistence,
Linux and Android arm64 builds, the full native test suite, and ASan/UBSan checks passed.
Physical Android interaction remains untested.

The same option also selects the original identities: gene IDs 328–330 (Little Red Riding
Hood), 349–351 (Aphrodite), and 448–450 (Adam). Names are intercepted in the viewer's shared
GeneName lookup; N/R/SR profiles are selected before the existing Gene Index line parser.
`card_art_text.cpp` preserves the unpatched Japanese PCSG00457 wording and supplies English
translations, since slot 1 contains name placeholders rather than translated profiles. The
fan-English Vita files reuse the mobile identities, so their labels are not used as originals.
Language follows LanguageChoice::id, including FanEnglish at message slot 0. This avoids
mistaking a fan translation's slot number for Japanese. Text switches only when the matching
original atlas/picture pair is available; Android alone retains Gilgamesh's name/profile for
card 86. Other names, messages, stats and saves stay with the selected gameplay release.

## Scene reset regression check

`ctest --test-dir engine/build -R event-reset --output-on-failure` checks that a
new scene resumes map animation, drops old skip/tap input and allows fresh dialogue
to advance. CR3 also checks secondary maps and temporary menus/overlays. The check
uses synthetic Squirrel calls and needs no game files. See
[the scene reset audit](SCENE_RESET_AUDIT.md) for original-code evidence and the
bounded Android/iOS/Vita asset replay results.

## Interrupted input checks

`ctest --test-dir engine/build -R input-cancel --output-on-failure` checks focus,
cancelled-touch and lifecycle-thread cancellation without game files. CR2/CR3
include the viewer cleanup check in that selection. CR1/Omega additionally use
`CR_SELFTEST_CANCEL=1 chaosrings --selftest` with local data to exercise their SDL
event path and an interrupted psychokinesis drag. See
[interrupted input handling](INPUT_CANCEL.md) for behavior and validation limits.

OpenGL / Android resume recovery is documented in [GRAPHICS_RECOVERY.md](GRAPHICS_RECOVERY.md).
The GL device retains live resource backups and rebuilds them in SDL's replacement
context while preserving scene handles; `gl-context-recovery` exercises actual context loss.

Mobile suspension timing is documented in [LIFECYCLE_TIMING.md](LIFECYCLE_TIMING.md).
Lifecycle clocks exclude background time from scene updates and fades; the
`lifecycle-clock` test checks long pauses, lifecycle threads and resuming.

Scene-owned speech, delayed battle audio, title transitions and unavailable
audio sources are covered by [SCENE_AUDIO.md](SCENE_AUDIO.md). CR2/CR3 include
`audio-player` and `viewer-scene-audio` CTests; CR1/Omega also have
`CR_SELFTEST_SCENE_AUDIO=1 chaosrings --selftest` with local game data.

Title-entry cleanup and the pending-save regression are documented in
[SESSION_UI.md](SESSION_UI.md).

Save-position restoration and load-confirmation follow-up:
[LOAD_STATE.md](LOAD_STATE.md).

New-game and retry state follow-up: [NEW_SESSION.md](NEW_SESSION.md).

Checked save replacement and failure recovery: [STORAGE_SAFETY.md](STORAGE_SAFETY.md).

Unreadable save protection: [BAD_SAVE_HANDLING.md](BAD_SAVE_HANDLING.md).
