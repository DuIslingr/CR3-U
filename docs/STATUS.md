# CR3-U: what the port plays

Moved from the README (v0.4.0). Each system's notes, and what each still leaves out, are in
`docs/FORMATS.md`.

Start `cr3view` with your copy in place and the game's title comes up; a tap starts a new game -
or, with a save, carries on from it. What works today:

- **The story's start**: the title (its camera reel and logo), a new game through the prologue
  and the first chapter to the first real dungeon - its battles, the scenes after them, the
  village's meals (talking with the party by their balloons) and the town - as the reference
  playthrough shows them: message windows, cut-ins, voices
- **Towns and dungeons** (CR3's `Fld2` field): walking by keys, pad, mouse or touch, the follow
  camera (and the village's rail and the houses' look-down ones), doors, the ways between a town's
  areas, the town map, the townspeople (standing and talked to), event boxes, the area scripts, map
  names, the field's buttons; **random encounters** with first strikes, surprise attacks and ace
  monsters; **saving** with the field's Save button and where the game saves by itself - entering
  areas, after battles (12 slots, in the profile folder)
- **Battles**: the glass break, the get-set menu, commands, genes and items, the break gauge,
  criticals, downs and combos, status effects, hit effects, the battle scripts, announcements,
  escape, the result - EXP into the worn genes, money, monster drops - and the continue and game over
- **Menus**: the camp (items, equipment, status, Gene Swap, gifts), the gene lab (combine, enhance,
  sell, the repository), town shops, the Space Port's quests, the world select, My Page, the Gene
  Index, the VS mode - with the game's online services **offline** (the Vita's own DBEmu rules,
  every release; CR3-U never contacts Square Enix's servers)
- **Sound**: the 54 music tracks with their loops, the 1,033 effects and every voice, in each
  release's own format - iOS AAC in MP4, Android Ogg Vorbis, the Vita's ATRAC9 and HE-VAG
- **Data, all three releases standalone**: a release is found from its folder and every one of its
  archives indexed together - the iPhone's ~170 `CRDB*.ios.mvgl` and loose movies, Android's
  `main.10007.android.mvgl` (read straight out of the APK or `.apks`) and its asset packs, the Vita's
  ~236 `CR3/resources/**.psp2.mvgl`; every file, texture, mesh, skeleton and animation of all three
  decodes (`cr3tool check`, `images`, `models`, `animations`)
- **Languages**: Japanese and English on iOS / Android; the Vita's Japanese - and the **fan
  translations** ([Chaos Rings III Translation Project](https://gbatemp.net/threads/chaos-rings-iii-translation-project.443742/)'s
  English, Joseas's Spanish) as *options*: each one given next to an unpatched Vita copy is a
  language of its own, both at once too. The in-game Options' Language switches between them live
  (the text, the pictures they translate, the Spanish one's font) and the choice is kept
  (`--patch`, once for each; `assets/language.hpp`)
- **Scripts**: all 194 of CR3's compiled scripts in the game's own Squirrel 2.2.4, with its 600
  natives bound; every one of its 4,127 events plays through to its end (`cr3tool events`)

Not yet: much of what's later in the story is untested; the battles' particle effects and some
spell directions, a few menus' rarer screens; see `docs/FORMATS.md` for each system's notes and
what each still leaves out.

# The port's own features in full

Moved from the README (v0.4.0), which has the short version.

## Field and battle speed

Options (the camp's; the title's Options button, lower right - or O; a battle's get-set Options),
Graphics (CR3-U's own tab - graphics, HD, controls - rows as the Other tab's, scrolled with the
arrows right of them, the mouse wheel or Up / Down): **Field Speed** and **Battle Speed**, each Off or 2x .. 5x - walking the
field (its scenes, talk and menus as they are) and the battles that many times as fast. Kept in
settings.ini (`field_speed`, `battle_speed`); `--field-speed n` / `--battle-speed n` for one run.

On a desktop the Options' tabs and rows get smaller as the window grows, so more of the Graphics
rows fit at once (a phone or tablet keeps the game's size and four rows). **Reset**, the round
button left of the back (or R), asks, then puts the open tab's settings back to their defaults (Graphics keeps the window's type and resolution).

## Save slots

The native title-screen save menu has **12 slots**, shown three at a time across
four pages. Use the on-screen arrows, Left/Right, Page Up/Page Down, or controller
L/R to change pages. Up/Down moves between slots. Copy and Delete work across pages;
playing a slot keeps manual saves and autosaves in that slot.

## New Game Plus

Cleared saves are marked **CLEARED**. Open one in the title's save menu to choose
**Continue**, **New Game Plus**, or **Back**. New Game Plus asks for an empty
slot on any page, then confirms. The original save stays intact.

This port feature carries team level, genes and their upgrades, equipment, items,
currency, collections, names and records into a new prologue. The protagonist keeps
the equipped outfit from the opening tower scene onward. Story flags, quests,
VS progression and party recruitment restart; MP is restored. Playtime accumulates,
and daily rewards keep their claim history. Clear the story again to begin another run.

## Vita save importing

Import decrypted Vita saves from title-screen **Options**, using **Other → Vita Saves → Import**.
Choose the exported save folder and confirm; existing saves are backed up automatically.
No Python is needed. See [Vita save importing](VITA_SAVES.md) for supported state,
profile locations and the optional command-line converter.

