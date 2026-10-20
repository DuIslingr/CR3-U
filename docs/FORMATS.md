# Chaos Rings III: file formats

Notes on CR3's data as read so far: where it differs from Chaos Rings II's (whose notes follow,
from CR2-U - the archive, textures, skeletons, Edge animations, materials and messages are the same
formats). "iOS" is 1.2.2 (`ChaosRings3_FINAL_ENG.app`), "Android" 1.1.3
(`com.square_enix.chaosrings3gp`), "Vita" PCSG00500 (and the Prequel Trilogy's PCSG00457, whose
`CR3/resources` hold the same files).

## The releases

| | iOS 1.2.2 | Android 1.1.3 | Vita PCSG00500 |
|---|---|---|---|
| archives | 167 `CRDB*.ios.mvgl` in the `.app` (41,940 names) | `assets/main.10007.android.mvgl` in the APK (stored, read in place) + the asset packs' `CRDBbgm` / `CRDBvoice` / `CRDBse` / `CRDBmov.android.mvgl` (44,274 names, none twice) | 236 `CR3/resources/**/CRDB*.psp2.mvgl`: `bgm/`, `se/`, `voice/{c,j,m,q,s0..s4}/` (48,977 names) |
| loose | `c01.mp4` .. (c04..c11 missing: an on-demand download), `c01_en.srt`, `c01_jp.srt`, `F0001.bin` (font) | `assets/F0001.bin`, `F0002.bin` (fonts) | `movie/c01.mp4` .., `movie/c01_jp.srt`, `font/TBGB_0KP.TTC` |
| code | arm64 Mach-O, decrypted (cryptid 0), 604 symbols | `lib/arm64-v8a/libcrx.so`: 45,177 exported names | `eboot.bin`: fake-signed SELF (Thumb-2, at 0x81000000); PCSG00457's `CR3.self` is still NPDRM-encrypted |
| scripts | `script\*.nut` (32-bit) and `script64\*.nut` (64-bit), 194 each | the same | `script\*.nut` |
| geom | version 0x64 | 0x64 | 0x50 |
| music | `CRDBbgm_mNN`: `sound\mNN.snd` = MP4 / AAC | `CRDBbgm`: `sound\mNN.snd` = Ogg Vorbis | `bgm/CRDBbgm_mNN`: `sound\mNN.snd1` (SXDF) + `.snd2` (SXDS) |
| effects | `sound\*.sndl` / `.sndm` / `.snds`: raw 16-bit PCM | the same | `sound\*.snd3` (SXDF) |
| voices | `sound\*.snd` MP4 / AAC (s000..s180 only) | Ogg Vorbis | `.snd1` + `.snd2` |
| tables | `data\*.mbe` (EXPA / CHNK), `data\*.bson`, `data\*.csv`, `data\*.json` | the same | the same |

The same name in two of a release's archives is the same file almost always (iOS 2,346 of 2,433;
Vita 2,175 of 2,189); `Database` keeps the main `CRDB`'s, then the archives' by name.

A NoNpDrm Vita dump is still PFS-encrypted (`sce_pfs/`, no `MDB1` at the archives' start): it must be
installed (Vita3K decrypts it on install) before it can be read.

## Messages (`message\*.dat`)

As CR2's, but two languages: a record is `(strings - records) / count` bytes - CR3's 8 (slot 0
Japanese, 1 English), CR2's 0x20 (8 languages). iOS and Android have both. The Vita's slot 1 holds
short placeholders, not a translation.

Placeholders: the message windows run each line and the speaker's name through
utils::FilterMessage, which replaces `{name}`, `{team}`, `{pet}` and `{food}` with the save's
entered names and `{goldkey}` with a count (`{name}`: 1,165 in slot 0, 1,088 in slot 1). Before
any entry, GameMain::SM_Startup's utils::SetDefaultName / SetDefaultTeamName set "プレイヤー" and
"ケイオスチーム" - in every release and language (iOS's the same bytes); the pet and food start
empty. ShowInputDialog(kind) starts its dialog with the current value (0 name, 1 team, 3 pet, else
food). The other `{...}` (`{target}`, `{gold}`, `{item_cnt}`, ...) are the menus' own.

### The fan translation

The Chaos Rings III Translation Project's patch (`rePatch/PCSG00500/CR3/resources`) replaces whole
files: `CRDB.psp2.mvgl` (2,818 message files with the iOS English in slot **0**, re-wrapped with
`\r` / spaces for the Vita's font; 13 `data\*.mbe`; 20 scripts), `CRDBadv01` / `CRDBboot` /
`CRDBeff01` / `CRDBui01.psp2.mvgl` (289 textures, 4 announcement texts) and `movie/c01..c11_jp.srt`.
CR3-U opens it as an overlay over an unpatched copy (`Database::addOverlay`): those files are read
from it only while the language is "English (fan translation)". Whether a copy has it installed
already: slot 0 of `message\texts.dat` mostly ASCII.

## Textures

As CR2's; the iOS release adds PVR v3 files with PVRTC 4bpp (pixel format 2 / 3) besides its PVR v2
ones. Android: PVR v3 (ETC1, RGBA8888, ...); Vita: GXT.

## Figures (`.geom`)

Version 0x50 (the Vita) is CR2's layout, with two changes: the materials' texture entries hold an
**index** into a table of texture names (header +0x4c, 0x20 bytes a name, +0x12 u16 their bytes)
where CR2's held the name; and meshes may be **triangle lists** (primitive 1; 0 a strip).

Version 0x64 (iOS, Android: the 64-bit builds) is the same data with 8-byte offsets:

| Header | 0x50 | 0x64 |
|---|---|---|
| meshes | +0x30 u32 | +0x30 u64 |
| materials | +0x34 | +0x38 |
| lights | +0x38 | +0x40 |
| cameras | +0x3c | +0x48 |
| inverse binds (3x4) | +0x40 | +0x50 |
| texture names | +0x4c (bytes +0x12 u16) | +0x60 (bytes +0x10 u32) |

| Mesh record | 0x50 (0x54 bytes) | 0x64 (0x68 bytes) |
|---|---|---|
| vertices, indices, palette, -, attributes | +0x24, +0x28, +0x2c, +0x30, +0x34 (u32) | +0x00, +0x08, +0x10, +0x18, +0x20 (u64) |
| palette count .. +0x23 (attributes, stride, index type, primitive, hash, material, vertices, indices) | +0x00 | +0x28 |
| bounding sphere: centre, half extents, radius | +0x38, +0x44, +0x50 | +0x50, +0x5c, +0x4c |

Primitives: GL 5 strip / 4 list (iOS, Android), 0 strip / 1 list (Vita). A material entry is
0x14 bytes (id, kind, -, 0xff, 16 bytes of value) in 0x50 and 0x18 in 0x64 (the 16-byte value
first, then id, kind, -, 0xff, 4 bytes padding). (`MVGL::Draw::Figure::Build` at 0xa7f370 in
`libcrx.so` relocates the eight header offsets when the version is 100.)

## Animations (`.anim`, EA04)

Byte for byte the Vita's, but in the 64-bit builds the header's offsets sit one word earlier:
frame sets' DMA list, set info, constant R / T / S / user at +0x30 .. +0x44 (the Vita's +0x34 ..
+0x48), the packed flag at +0x48 (+0x4c). Nothing in the file marks which: the release decides
(`gWideAnimations`), and 13 of the iOS release's are the 32-bit form (the reader tries the other
when one fails). Skeletons (ES02) are identical.

## Skeletons: scale compensation

A link's 0x8000 (the parent field's top bit) marks scale compensation - the parent's own scale
undone for the child's rotation (Maya's segment scale compensate); without it a child inherits its
parent's scale. CR2-U had it the other way, unseen until CR3's effects: `ee101aura01` hangs its
globe on children of a 0.183-scaled root, small in the priestess's hands only this way round.

## Render states

The materials' render states (kind 100) follow MVGL's shader-parameter table, which grew: CR3's
ids start at 0xa0 (CR2's 0x78) - 0xa0 alpha_func, 0xa1 alpha_test_enable, 0xa2 blend_func, 0xa3
blend_equation, 0xa4 blend_enable, 0xa5 cull_face, 0xa6 cull_face_enable, 0xa7 depth_func, 0xa8
depth_mask, 0xa9 depth_test_enable, ... 0xb0 stencil_test_enable (the names' table at 0x111f424 in
`libcrx.so`, 0x5c bytes an entry; the parameters' at 0x111fa40, 0x38 bytes, ColorSampler 0x32 as
before). The Vita's figures (geom version 0x50) keep CR2's numbering, 0x78..; read, they are moved
up by 0x28 to the one numbering.

## Event scenes

As CR2's: `<event>.geom` (locators), `<event>_camera`, `<event>_light`. New in CR3:
- the camera's field of view is animated: a user channel of the camera figure bound to the camera's
  own hash (MVGL::Draw::Camera +0x258) carries it in degrees (the prologue zooms 20.9 -> 35.5)
- motions: `EvtScene.LoadMotion(model, name)` loads `<figure>_<name>` (a body / head model's
  `<figure>b01_` / `h01_`; CrxModel::LoadAnimation), `SetMotion(model, loop)` /
  `SetBlendMotion(model, loop, frames)` play it; the face's the same in slot 1
- draw layers (`SetModelLayer`) and full-screen colour sprites between them (`CreateSprite`,
  `SetSpriteLayer`, `SetSpriteColor`, `SetSpriteAlpha`, `SetSpriteAlphaAnimation`)
- timing: 30 frames a second, not CR2's 20. The scene's animations are 30 Hz (the ship's camera:
  2,001 frames, 66.7 s) and the scripts' frame numbers (`vCameraMove(0, 185)`, `vMove_`) count
  them; `WaitFrame(n)` waits n / 30 s of elapsed time (WaitProcessor::StartWait / ProcessWait),
  whatever the display's rate. Text types one character per drawn frame
  (ProvisionalMenuText::Step, speed 1 from EvtWindow::ShowMessage) at the game's 60 fps target
  (nothing calls SetTargetFPS). With this the ship event (`s000_d01_00301_evt`) matches the
  playthrough shot for shot, offset by its start.
- the map: an event's caller names it - `X()` calls `SetVistaEvent("X_evt", "<map>")`
  (Cr3System.SetVistaFuncName / SetVistaMapName); "" or "null" none. An event can hand over at
  its end the same way (the prologue's `s000_t01_00101_evt` to `s000_t01_00103_evt` in `e0003f`).
- a second map (VistaMain): `VistaEvent.LoadSecondMap(name)` builds it (not for "" / "null");
  `ChangeMap()` flips which is drawn (OnDraw: the first while unflipped, the second while
  flipped). `s000_t01_00103_evt` loads "" and flips: no map at all, the scene's own sky
  (`t0100star01` / `03`) showing.

## The title (CR3)

GameMain boots BootCheck (mode 0x13), then TitleMain (5): its interface 0xd is TitleMenuMain -
the logos (LogoMenu: `ttlLogoIncMenu`), then the title (CreateTitleMenu: TitleMenu, `ttlLogoMenu`
- the logo, `textTouch` "Tap to play", `textInfo` the copyright - at `ttlLogoPositon`'s
`call_ttlLogoMenu_fit0`, its own animation looping; music 33), behind it Fld2CameraDemo's reel: six
shots (a table in the executable: seconds, map, camera, the fade in / out colour - 0 black, 1
white, -1 none) of `t0209f`, `d0304f`, `d0704f`, `d0405f` through the camera figures
`f0000title01`..`06`, each played from its start, fading over half a second. A tap (SE `com_001`)
ends it; with saves it goes on to the save files (SaveFileMenu, `ttlData*`), the news, the mode
select (ModeSelectMenu: `ttlModeMain` / `VS` / `Back`) and a yes / no; a first game goes through
white into the prologue - TitleMain loads the save, runs `t01`'s `Login`, and the new game's first
vista is `s000_t01_00101_evt`. From there the events hand over (SetVistaEvent - a name's second
part its script: `s000_d01_00301_evt` is d01's) through `00103`, `00201` (the opening) and the ship
(`s000_d01_00301_evt`), whose SetVistaToField returns to the field of its map: `d0101`.

The prologue's story on (part 1 of the reference, 354 - 712 s): d0102's event box `evt101_00`
(flag 2001 on, 2002 off: sets 2002) plays `s000_d01_00401_evt` - the first battle, then
`00601_evt`; SetVistaToField (Cr3System.SetComeFromVistaToField: Cr3FieldData +0x10 = 2) puts the
player back where it stepped in (Fld2Main::ChangeArea: come-from 1, 2 or -2 - the position and
rotation Fld2SaveData kept, Cr3FieldData +0x20 / +0x30), and d01's `evt000` shows the area's
name (flag 1007), the controls and mini-map tutorials once (flags 71, 106) and, with 2002 on, the
HP / MP one (100). d0103's `evt201_00` (2012) is the Heterocera battle (551); d0104's `evt102_00`
sets 2003 and plays `00701_evt` - the spider, battle 2 -; entering d0105 then (`00901_set`: 2003 on,
2004 off, area 4) plays `00901_evt`, clears 5051 and goes on through `s000_t01_01001_evt` (e0019f),
`01101_evt` (e0009f) and `s001_t02_00101_evt` (t0201f) to the town's field, t0201. An event's model
that is a parts model (the heroes' j00N: no `j001.geom`) is its body and head `j001h01`, each
posed by its own animation of the event's suffix (`j001b01_s001_t02_00101_03`). The body is the
character's armour's (CrxModel::GetBodyModelNo: Armor +0x30, `%sb%02d`; b01 without one). The
starting kits are the scripts': test_equip.i's `PartyEquip(chr)` (once each, flags 1021 - 1025)
gives and puts on (Cr3System.ItemSet = utils::SetItem(item, character): 1xxxxx the weapon slot,
2xxxxx armour, 3xxxxx accessory; ItemGetAndSet the item given first) a gene, weapon, armour and
accessory - char_para's +0x10 / +0x14 / +0x18 for all but j001, whose armour is 83, not the row's
1 (the Black Jacket, b01): 83 is body 2, the white vest the reference shows, its numbers the
jacket's. `s001_t02_00101_evt` runs `PartyEquip(1)` first thing; the prologue before it (the battles
are the hooded guest's, char 6) shows j001 in his new save's - an iPhone's from the server
(resetPlayer / loadPlayer), the Vita's built on the device (DBEmu stands in for the server;
CrxGameData::Init leaves every slot empty) -, so the port gives him 83 from the start, as the
reference. DBSystem::SM_SkipVSTuto (the VS tutorial skipped) sets the same kits for 1 - 3. A
costume body (j001b02: b01's 31 joints and a DirLamp01) moves by the b01 body's animations over its
first joints.
`cr3view --input box:<event box>` puts the player in an area's box (tests); `--walk x,y,s` in a
CR3 area walks for those seconds only.

English textures: GameSystem::InitLang adds the texture folder `images_eng` for any language but
Japanese, looked in before `images` - 314 of the interface's, the title's "Tap to play" among them.

`cr3view --title` (no arguments: the same) plays it; `--time t --tap --run s` for captures.

## Fields (CR3)

Fld2, not CR2's painted FldMap: every area is 3D. Fld2TaskMap::CreateArea(n) takes its name from
a table of 12-byte names in the executable (`t0101` first: 41 towns, then `d0101` at 41 ...; 463
in all, 0x5e.. and 0x153.. the random floors) and builds `<area>c` (not drawn: its lights, its
joints - `defaultPos` where the player starts - and `<area>c.phys`, the walls and floors; bodies
named `...camhit` are the camera's alone), `<area>f` (the field, its animation looped), `<area>s`
(0.01 above it) and `<area>m1` (if there is one) - drawn f, s, m1 (OnDraw's passes 6-8, 8, 10).

The player (Fld2TaskPlayer::CreatePlayer): a Fld2CharaNameTbl model - `j001`..`j005` (the heroes:
body `j<NNN>b01` + head, motions `j<NNN>b01_<motion>`) or `c101` (`c101_<motion>`) - in the towns
always j001 (areas below 41). Motions (Fld2AnimeNameTbl): `fn01` idle, `ff01` run, `ff02` walk,
`fn02`, `ff03`, `fg31`, `fh01`..`fh07`, `fr01`..`fr03`; switched with a 1/6 s blend. Moving
(SM_Walk / SM_Run): the stick's direction in the camera's frame at 1.2 (walk) or 4.8 (run) a
second as far as it's pushed, a sphere of 0.2 (CreateGhost; Fld2CharaCtrl::Setup 0.1 the step,
SetMaxSlope 45 degrees).

The camera (Fld2CameraGom, MVGL's default 45 degrees, near 0.1, far 5000): it looks at the player
1.2 up; a spring holds it 3 away (accelerating 0.005 a 1/60 s step, braking to stop there), 0.5
above that point and never nearer than 2 across; the view pitched 0.3 rad (at least 3 sin 0.3 up)
and turned by the player's own turning; moving 30..150 degrees across the view, it swings round
behind the player at (1 - angle / 180) x 1.5 x 60 degrees a second (GomRotate); a tap swings it
behind over 0.3 s (GomBehind).

PHYS (`.phys`) in the 64-bit builds: the counts as before (bodies, shapes, surface names at 8),
then u64 offsets from 0x18 - bodies (0x38, right after the header; 0x4c bytes each as before),
shapes (a u64 each), names (64 bytes each) and the scene's name. A mesh: kind 2, its vertex and
triangle counts at +8 / +0xc, its bounds, then u64 offsets to its triangles (3 u32), vertices (3
floats) and two u16s a triangle - its surface, and 1 on camhit bodies.

Entering an area runs its script's `evt000` (the area's first three letters: d0101's d01): the
gimmicks, the music, the map name (EvtIntroduction - MapNameMenu: `feNameMenu` at
`feNamePosition`'s `call_feNameMenu_fit3`, its `_in` animator 0..0.967 s in, 2 s held, on to 1.333 s
out; `feNameList_fr00` the dungeon's name at frame n and six `feNameNum_fr00` digits - the level's
range, (lv - 1) x 10 + 1 to lv x 10 - each turned 30 degrees about z; faded in over 0.667 s, out
over 0.1), then the tutorials (InterfaceWindow.CreateTutorialMenuNum(n): TutorialMenu -
`ttlMnlMenu` at `ttlMnlPositon`'s `call_ttlMnlMenu_fit8`, announcement n of
`message\AnnounceTutorialTxt.dat` at `typeM_L01`..`L04` (30, white, left), paged as a window's;
SetPictureTutorialMenu(id): `ttlMnlImg_fr00` at `call_ttlMnlImg_fr00` with `tuto_ss<(id - 1) / 8 +
1>`, frame (id - 1) % 8; EndCheckTutorialMenu true once read). The player stays where the area put
him after a vista (Fld2Main::Setup moves him only coming from a battle or a pop point: the field's
saved place, Fld2SaveData's, as he left it). The camera stops at the first wall from the target
(CheckCollision: camhit bodies alone where there are some within 20 below the target, else the walls
too), raised by 2 - d within 2 across and the player drawn at 0.3 within 1.

Gimmicks (Fld2TaskGimmick::SetGimmick, by `<area>p`'s joint names `ragdoll_<kind>..`): `ac`,
`ldr` (ladders), `krock`, `tbox` / `kbox` / `gbox` (boxes), `fswc` / `bswc` (switches), `blk`
(blocks), `jem`, `cobj` (check objects, 10), `door` (11), `fmove` (12), `cam` (18), `minimap` (22),
`j` / `c` / `m` (people, 13), `evt` (event boxes, 14), `vc` (20), `adv_` (21). An event box
(Fld2Event::Create) is a box the joint's scale x 0.05 each way at it; the player's gimmick ghost
(CreateGhost's second: a sphere of 1.0) touching it queues its function (`ragdoll_evt103_00`:
`evt103_00`) - Fld2TaskGimmick::SetNextEvent, run as the area script's. A check object (cobj) is
the door itself, its function on a tap while the "!" shows: `cobj_00` is `DoorJump("fe_040",
"d0102_p1")` - SetPopPoint, the fade out; the field then changes area to the pop point's (its
first five letters) at that joint of `<map>p`. CR3-U takes "near" as within 1.3 + the box across
(an approximation of the ghost against the box: the walls stop the player short of it). Leaving
for a vista (an event box's `s000_d01_00401`, ...) keeps the player's place (Fld2SaveData) for the
return. The ship (d01): d0101 -> d0102 (`evt101_00`) -> d0103 (`evt201_00`: BattleIn 551, two
Heterocera) -> d0104 (`evt102_00`, `evt202_00`: BattleIn 552) -> d0105. The story's first battle is
earlier, the vista `s000_d01_00401_evt`'s (d0102f) BattleIn2 of coupling 1 - see "Battles (CR3)".

`cr3view --area <area> [--player <model>]` walks one (WASD / the stick, Shift a walk; Q / E turn
the camera, R / V tilt it); `--walk x,y,s --run s` for captures.

## Interface (CR3)

The menus (CRXPartsBase) are laid out in a 960 x 640 space at 100 pixels a unit - ten times CR2's
units, the same HUD camera. A menu figure is placed at a Position figure's `call_<menu>_fitN`
joint: N a numpad anchor (1..3 the foot, 7..9 the top, 1 4 7 left, 3 6 9 right) moved by the
screen's room beyond 9.6 x 6.4 (16:9: +-0.88 across; 4:3: +-0.4 up and down -
CRXPartsBase::GetFitX/YOffset). Text sizes (SearchTextPosition): `typeM` 30, `typeS` 28, else 64 -
but the windows' SetStringData ask their own: the vista's lines 32, its name 28; the
conversation's lines 32, its name 30. The joint's letter after `type?_` aligns: L 0, C 1, R 2,
else 3. Placing a line (the 64-bit ProvisionalMenuText, s its size in pixels, 200 half-pixels a
unit): the window's Pose moves the joint (s / 2) / 200 right and down; SetTextOffsetData /
SetPosition centre a quad W = width + s wide, the text at its left - L at the joint + (W - s) / 200
+ 3s / 1600, C on it, R at (s - W) / 200 - its middle (s - height) / 200 - 3s / 1600 off the joint;
a bordered text (Initialize type 2: the vista's name) 0.01 back up and left. The vista's two lines
are one block: the narrower's +0x4c8 is minus half the difference, so both start where the wider
does. Against the playthrough (which is scaled 1% about its centre) lines and names land within
~2 px. The event windows (EvtSystem::CreateWindows, CR2's order): 1 the vista's `comMsgVMenu` (two
lines at `typeM_C01` / `C02`) at `comMsgVPosition` - 0.03, its speaker's name in
`comMsgVNameMenu`; messages `comMsgMenu`, narration `comMsgNMenu`, system `comMsgSMenu`. The page
cursor (PageSendCursorButton, the vista's): `comMsgPBtnMenu` looping `comMsgPBtnMenu_loop` (slot
1) at `comMsgPBtnVPosition`'s `call_comMsgPBtnMenu_fit3`, reopened once the page is typed and faded
in over 1/6 s. The conversation's button `comMsgBtn` at `comMsgPosition`'s `call_comMsgBtn_fit3`,
rendered while the page waits for a tap (MessageWindowMenu +0x62), typing or not. Speaker tags: 144 (CR2: 21), in
EvtWindowMessage::GetNameTagNumber's order (`tools/gen_nametags.py`). The font: `F0001.bin` (iOS,
Android) / `font/TBGB_0KP.TTC` (Vita) - TB Gothic B, the proportional face (1) for English.

### Cut-ins (CR3)

`EvtFace.Create(n)` makes interface 0x3a - n (InterfaceMain::SetCutInMenu): 0 the right portrait,
1 the centre (CutInCenterMenu), 2 the left. CutInMenu: a Position figure
(`comMsgCut<R|L>Position`) slid in by its `_io` animator in slot 1 - relative to its base (the
PartsBase's +0xc4 = 1: ChangeAnime puts it in the figure's slot 1 rather than 0) - so the right
anchor ends at 10.24 - 10.56 = -0.32, the left at -10.24 + 5.44 = -4.8, then fitted (`_fit6` /
`_fit4`); `comMsgCut<R|L>Menu` at its `call_` joint; the portrait `comMsgCut<R|L>_id00` (a 5.08
square, its material the face: `EvtFace.SetTexture(image, id)`, `ChangeTexture(n, id)`) posed by its
own animation, which for the right one turns the image joint 180 degrees about y - the left-facing
faces mirrored. `SetColor(n, v)` its brightness. The conversation's box (EvtCutinWindow:
MessageWindowMenu) is `comMsgMenu` at `comMsgPosition`'s `call_comMsgMenu_fit2`, its panel's parts
placed by its own animation (absolute, played as it opens), the name at `typeM_L01`, three lines at
L02..L04, drawn dark on the light panel. Both portraits fit the playthrough at the same 1.01 scale
(the video's own) and mirrored 4 px offsets.

The centre one (CutInCenterMenu, scripts' `Cutin_Start_C`: an item's picture): `comMsgCenterPosition`
places `comMsgCenterMenu` at `call_comMsgCenterMenu_fit0` (0, 0.88); InterfaceOpenAnimeSet plays its
`comMsgCenterMenu_in` (absolute: its user channels flash the screen-sized `flash` mesh, and hold
`call_comMsgCenter_id00` at scale 1 - the base pose has 3); the portrait `comMsgCenter_id00` (a 2.52
square, `mat_cutIn001_C01` the picture) sits at that joint at its scale (GetScaleSize), hidden for
the menu animation's first 1/6 s, then faded in over the next 1/6; `comMsgeff_01`, ten times its
size, bursts over it from 0.367 s until its animation ends. Closing takes four Poses, a quarter of
its alpha each.

### The gene lab (CR3)

Talking to the clerk (t0108's `c115_00` - a person is a `ragdoll_c<id>_<nn>` joint of `<map>p`,
its function so named) runs `c115_GeneSynthesis`: InterfaceWindow.CreateGeneSynthesisMenu(the
clerk's cut-in image: `cutIn115_L06`, `_L99` before flag 2401), then a loop while
CheckEndGeneSynthesisMenu, running `c115_talk` when CheckSelectTalkGeneSynthesisMenu says Talk was
chosen (back with EndTalkNextGeneSynthesisMenu). InterfaceMain::CreateGeneSynthesisMain makes
GeneSynthesisMain (0x9f: `shBgMenu` at `shBgPositon`'s fit0 - a faint light wash over the field),
the clerk as the left cut-in (SetClerkCutIn: CutInMenu +0x40 set - ChangeCutInPosition(true) -
puts the portrait 0.87 lower), HeaderMenu (0x42), GeneSynthesisFutter (0xa0) and comBtnMenu (0x2c).

- HeaderMenu: `comHePosition` opening on its own animation (the `_io` animator closes it),
  `comHeMenu` at its fit7 with `comHeTitle_fr00` at frame 30 (37 in VS: the area's name),
  `comHeIconMenu` (battery), and - its content 0 - `comHeInfoAMenu` (fit9): money in
  `comHeMlPanel`'s seven `comHeNum_fr00` digits (frames 0..9), the clock in `comHeTimePanel`'s four
  (HHMM, frames 30.., the hours' tens hidden under 10 - SetHourTime), Oz coins in
  `comHeCoinPanel`'s five (frames 20..); content 1 has `comHeInfoBMenu` with GP instead of the clock.
  InterfaceMain::SetViewNumbers: the value capped at its places, the leading zeros hidden.
- comBtnMenu: the arc's panels 004 .. 007 (the slots 4 .. 7 of its scrolling list) - Talk (17),
  Combine Genes (28), Sell (21), Gene Repository (29), the last three dimmed while flags 3613 / 3614
  / 3615 are set (SetGeneSynthesisTopButton); `comBtnText_fr00` / `comBtnIcon_fr00` at the id's
  frame, `comBtnBg_fr00` at the slot's.
- GeneSynthesisFutter: `comFtrMixMenu` at `comFtrPosition`'s fit4; content 0 the genes held of 50
  (`comFtrMixGenesPanel`) and the stock of its room (`comFtrMixBoxPanel`) - `comListNum_fr00`
  digits 0.19 apart from `call_comListNum_fr00_001` / `_002`; content 1 (a list) the tabs
  `comFtrMixTabTap_fr00` frames 0 .. 3 (ALL, N, R, SR; + 6 the one chosen - a radio, ALL to begin
  with) instead. Its back button (ComBackButton: `comBackMenu` at `call_comBackMenu_001_fit6`)
  closes the lab.
- The help line: SetHeaderSentence's text at `comHeMenu`'s `typeM_L01` - Cr3UtilGetMnlCmpAnnounceData,
  `message\AnnounceMenuName.dat` (u32 the records' offset, the texts', the count; a record two
  u32s, the Japanese text's and the English's, by number from 1; `\r\n` a line break).

Combine Genes (GeneSynthesisMain 9 ..): its own two buttons - Combine (30) and Enhance (31), Friends
(32) only with friends - from slot 004; Combine takes the clerk away (EndCutInMenu(2)) and the
footer, and:

- The base gene (GeneChangeMenu 0xa5; title frame 31, help 0x18e): `geneCngStsMenu` at
  `geneCngStsPositon`'s `call_geneCngStsMenu` (-4.59, 2.19; its own animation slides it in from
  2.8 to the right) - the eight commands and skills at `typeM_L01` .. `L08` (those its levels still
  teach after them, dimmed), the element `btStAtbIcon_fr00` (frame its kind), the lock
  `geneCnglockBtn_fr00` (1 locked), `camEquJenePanel` at `call_ccamEquJenePanel` with the name
  (`typeM_L01`), the level (`camEquNumLv_num00`: `comNum04_fr00` x 3, "MAX" frames 10 .. 12), the six
  numbers (`geneCngStsNum_num00` at `call_camEquNumSts_num00_000` .. `005`: `comNum00_fr00` x 5 - HP,
  MP, attack, defence, speed, luck: gene_para's base + (level - 1) x growth / 100, the items'
  additions to the last four) and the HP / MP bars. The cards: `geneCngListMenu` at
  `geneCngListPosition`'s fit6, its nine `call_geneCngListPanel` joints an arc - the card in focus
  at the fifth (the arc's left-most) at 4 x, the others at 3 x, (x, y + 0.25); the focus and count
  in `geneCngListNum_num00`'s `geneCngNum_fr00` digits. Tapping the card in focus chooses it.
- CardModel: `card_default` with materials `picture` (`card_<series:03>`), `rarity`
  (`card_rar_<grade:02>`), `attribute` (`card_att_<kind:02>`), `star_1` .. `star_5` (the first
  `stars` at full diffuse, the rest at a tenth); its sheen `card_eff_<min(grade, 2):02>` (a looping
  figure) at its place and scale; half bright where it can't be chosen. gene_para Gene +0x58 is the
  stars.
- The material (state 0x10 ..; help 0x177): GeneSynthesisListMenu (`comListMenu` at
  `comListPosition`, its `_io` in) - the merges PreviewMergeGene offers with genes neither stocked
  nor locked, by the footer's tab (the result's grade), three rows 1.24 apart from
  `call_comListPanel`: `comListLPanel`, at its `call_comListLMixing` a `comListLMixing` with the
  result's picture (`comListGeneIcon_id00`'s `mat_card_001`), element, grade (`comListRare_fr00`),
  stars (`mixStar_fr00`, frame stars - 1), dark name (`typeM_L01`), level (`comListLv_txt`'s
  `comListNumUn_num00`: `comListNum_fr00` + 10, "MAX" 40 ..) and gold (`comListNumUn_num00` at `_001`:
  `comListUnIcon_fr00` frame 2, seven digits + 30) - dimmed where unaffordable. NormalSynthesisMenu
  beside it: `mixMtrPositon`'s `call_mixMtrMenu_001` / `_002` a MaterialGenePanel each (`mixMtrMenu`:
  `mixMtrText_fr00` 0 / 1, `mixMtrPanel` with picture, element, grade and name at `typeM_L00`,
  `mixMtrLvPanel`'s level, `mixStar_fr00`) - the base and the row's partner; `mixBtn_fr00` at
  `call_mixBtn_fr00` swaps them for the result's card (9 x, at `call_GeneCard` - (0.25, 0.02)).
- Choosing a row (again) asks: the list and footer gone, the two at `mixOkMtrPositon`'s joints, the
  system window (`comMsgSMenu` at `comMsgSPosition`'s fit2, lines at `typeM_L05` .. `L07`:
  AnnounceMenuName 0x175) and YNChoiceButton (interface 0xf: `comBtnMenu`'s buttons from its joint
  10 on - slots 005 / 006 - OK, icon and text 10, and Cancel, 11). OK merges (DBEmu MergeGene: 100
  gold a level of the two) - the header away, GeneSynthesizeEffect (`ue002jene01`; CR3-U a white
  flash for now), then the new gene.

Combine and Sell want more than five genes held (CheckSynthesisGeneCount(0); else AlertwindowMenu -
`comMsgSMenu` with `comMsgBtn`, gone by itself - says AnnounceMenuName 0x31e), and each gene picked
must leave five.

- Enhance (31; title 32, help 0x176): the base from the cards (those at their top level can't be
  chosen - SetLevelLockSynthesisListMenu), then the genes to feed it (help 0x177): the rows the held
  genes but the base and the locked, by grade (InitializeL 2: the level's bar `comListLv_per` at its
  share of the top level and `comListNumL_num00`'s digits + 10), a tap checking one
  (`comListCheck`; 20 at most); IntensificationSynthesisMenu (0xaa) at `mixMtrPositon`'s
  `call_mixUpMenu`: `mixUpMenu` - Components, Additional EXP, Total Cost (0x184 .. 0x186) at
  `typeM_L01` .. `L03`, the count and 20 in `call_comListNum_fr00_001` / `_002`, the EXP in `_003`,
  the gold in `_004` (SetNumbers: 0.19 apart from the joint, leading zeros blank) - the base's
  `mixMtrPanel`, `mixMtrLvwPanel` (the level now and after; "MAX" 40 ..) and stars; the mix button
  shows its card. The footer's second ComBackButton (`call_comBackMenu_002_fit6`, `comBack_fr00`
  frame 1) is OK, dim with nothing checked. Asked (0x178, {basegene} the base's name) with the cards
  over it (SynthesisCardListMenu: `mixOkCardMenu` at `mixOkCardPositon`, 3.2 x and 1.1 apart from its
  `call_GeneCard`), then DBEmu EnhanceGene and the base's card.
- Sell (21; title 34, help 0x273): the rows the held genes but the locked, with their price
  (gene_para price + (level - 1) x its rise); GeneSellPanel at `call_mixSelMenu`: `mixSellMenu` -
  Genes to Sell, Additional ML (0x17a, 0x17b), the count / 20, the gold (+ 30). Asked (0x182) with
  the cards, then DBEmu SellGenes and the lab's buttons.

`cr3view --area t0108 --flag 2001 --input call:c115_00,lab:talk|combine|sell|repository|back`
(call:<function> runs an area function as if tapped); after Combine `lab:enhance`, and `lab:normal`, `lab:card<n>`,
`lab:row<n>`, `lab:tab<n>`, `lab:mix`, `lab:ok`, `lab:yes` / `lab:no`, `lab:up` / `lab:down`. `--gene
<id>[:<level>]` and `--gold <n>` give genes and gold to begin with.

## Battles (CR3)

The battle reservation: `Cr3System.BattleIn(coupling, field, level, music)` /
`BattleIn2(.., music after)` (FldUtilReserveBattleIN; the scripts' wrappers name them `cup, lv,
map, bgm` but pass map second) become the BtlBootParam - +0 .. +8 the party (utils::SetParty's
slots: char_para ids), +0x10 the battle field, +0x14 the coupling, +0x18 the monsters' level, +0x1c
the music. `SetBattleNext(14)` with `SetVistaFuncName2` / `SetVistaMapName` (the scripts'
`SetBattleToVista`) sends the battle on to a vista; else back to the field, whose `evt100` runs
(SetComeFromBattleToField). The ship's: `s000_d01_00401_evt` -> coupling 1 on field 44 at level 3
-> `s000_d01_00601_evt`.

BtlSystem::Init makes the units: the party (createParty: the characters' models - a party
member's CrxModel, GameMain::BuildUnitModel), then the coupling's monsters (CreateMonster:
`mon_cop.mbe` Coupling - id, then three (count, pointer to that many monster ids) slots, each
filled by one of its ids at random; empty where the first id is -1). `mon_para.mbe` Monster: id,
model, texture variation. `model.mbe` Model (64-bit layout `iippffiffii`): id, -, name, sub, width
and depth (/ 10), camera size (MbCameraSize), scale, -, -, flags - bit 0 a parts model, drawn as
two figures (CrxModel::Build): `<name>b<costume>` ("%sb%02d", b01 without one) and `<name>h01`,
each with its own animations. A texture variation replaces the textures' `a` with its letter
where that image exists (CrxModel::GetVariationTexture: `m234a00` -> `m234c00`).

Placement (BtlUnitList::Align, BtlUnit::initializeHome): each side along x in the order made, side
by side by width x scale, the side centred on 0; the party at z = Battle +0 / 10 + half its depth,
turned 180 degrees (facing -z), the monsters at Battle +4 / 10 - half theirs, facing +z (`battle.mbe`
Battle: 21.5, -21.5). The field: `battle_field.mbe` BattleField (id, figure, four floats) - its
figure, its animation looped, its `DirLamp02` the shadow light (BtlField). The units stand in their
type-0 motion, `bb01`, looped (BtlUnit::PlayStayAnimation), the monsters' started apart
(BtlUnitList::Standby: 0 / 1 / 2 s, plus up to 0.5 at random).

The cameras (BtlCameraSystem): every battle camera is a joint of one figure, `Cam_bt` (in
CRDBcam01; 90 cameras, one 30 s animation they all run on from 0) - or a figure of its own where
the name lacks "Cam_bt" (`Cam_human_getset`, `Cam_colosseum_getset`). BtlCameraMotion poses the
figure at its time and takes the named joint's world position (x its scale, 1) and rotation, and
the field of view of the camera the figure holds on that joint (GetContaindCamera); BtlCamera's
own planes are 0.1 and 7000. BtlCameraActor puts it on an actor: position + rotation x the
motion's position, rotation x the motion's rotation. The camera set is the largest unit's camera
size's `model.mbe` Camera row (size, the get-set's end time, then ten names: 0 the get-set, 1 the
command, 2 the party's attack, 3 the monsters', 4 all-attack, 5 / 6 combos, 7 all, 8 chain, 9
up; a name list is separated by newlines).

The start (BtlSysStateStart): phaseStart plays the glass break and PlayBattleIn - the get-set
camera at the origin over the row's end time (size 4: `Cam_bt_getset_M_00`, 2 s); phaseBattleIn
waits for the glass and that camera (or a tap), then PlayGetSet - the same camera seeked to its
end and held - and the battle's start script. The command camera (PlayCommandSelect) is
`Cam_bt_command_PC_00` looped over 26.67 s (one turn round the unit) on the unit's camera point -
its figure's `DummyCP` joint (BtlUnit::VGetCameraPoint) - turned as the unit is, and the picture
moved left by a sixth of the screen (BtlCamera::SetScreenOffsetX(-width / 3): Camera::Render
adds offset / width to clip x).

The way in (GameMain::SetGlassEffect at the reservation, StartGlassEffect as the battle starts:
Framework::PostEffectGlassFragment): the frame as it was caught and held, then broken -
GlassFragmentParam pattern 0 (RadialCenter, what the scripts' BattleIn sets; the patterns' table at
lib 0xf3ca78): the screen's four corners and 230 points at random on its half size, Delaunay
triangles (fixEdge only snaps hull points between corners to the border), each flying from the
middle at 8.7 + 0.06 d pixels a frame (d its distance from the middle), gaining 1.1 + 0.4 d a
second, turning (0.18 + 0.0003 d) x random about each axis, growing from 1 + up to 0.004 by 0.08 a
second; the whole fading out over 0.8 s (+0xe0), SE 0x253. CR2's had other gains (0.55 + 0.02 d,
0.004) and lasted 1.5 s.

The scripts (BtlScriptSystem): `battle_<coupling>` where the coupling's +0x44 bit 0 is set, else
`battle_0000`, run through the event system: Battle_Boot_Base as it boots (battle_0001's gives the
hero status effects 40, 65, 66, 67), Battle_Start_Base once the get-set camera holds (battle_0001's
Battle_Tuto_051: the combat tutorial in the field's tutorial menu - the reference's 286-296 s),
Battle_Victory_Base when won. battle_2006's Battle_Start_Base calls a Battle_Start it lacks (the
game's call fails the same way). 470 of the 471 boot / start / victory calls end
(`cr3tool battlescripts <release>`).

The rules (battle/cr3rules.hpp): a character's numbers from char_para (+0x48 HP, +0x4c MP, +0x50
attack | defence, +0x54 speed | luck as s16 pairs, +0x58.. their growth: base + (level - 1) x
growth / 100; +0x0c bit 0 a guest; +0x10 / +0x14 / +0x18 its weapon, armour and accessory, +0x1c
its gene - a guest's GuestGene row -, +0x20 its attack command: BtlCharacterStatus::
VGetAttackCommandId), a monster's from mon_para (+0x14 its level where set - Gunner's and Axer's
50, the target list's "50" -, else the reservation's; +0x18 its element; +0x1c HP .. +0x28 speed |
luck, +0x2c.. growth; +0x3c / +0x40 EXP and money, +0x44 / +0x46 their growth).

Equipment (CrxCharacter::initialize): a guest's is its char_para row's; anyone else's the save's
(CR3-U, keeping no equipment yet, gives them the row's too). item_para Weapon: +0x38 the constant
(VGetWeaponConstant), +0x3c the hits (VGetWeaponHitNum), +0x44 the MP consumption %, +0x48 / +0x4c
lowered physical / magic resistance, +0x50 + 4e an element's. Armor (64-bit layout 13 ints, `_p`,
11 ints, `_p`): +0x44 physical, +0x48 magic, +0x4c + 4e an element's resistance. Accessory: +0x30
HP, +0x34 MP, +0x38 attack, +0x3c defence, +0x40 speed, +0x44 luck, added. A guest's gene
(CrxGene(int)): GuestGene +4 the gene, +8 its level, then eight (type, id) pairs from +0x0c - 0 a
command, 1 a skill; gene_para Gene adds +0x34 HP, +0x38 MP, +0x3c.. s16 attack, defence, speed,
luck at the gene's level (+0x44.. their growth). The prologue hero: weapon 501 (constant 90, 1 hit,
200%), armour 202 (20% physical), accessory 100 (+1 defence), GuestGene 1 - gene 453 (nothing
added) at level 50; attack 400, defence 291.

A command (command_para Command, 64-bit layout `iiiiiiffiii` + `_ip` x 7 + 15 ints + `_pi`): +0x04
its type (0 an attack, 1 a physical skill, 2 magic, 3 support, 4 special, 7 a guard), +0x10 its
target kind (BtlStatusList's collectCommandTarget table: 0 self, 1 an ally, 2 all allies, 3 an
enemy, 4 all enemies, 5 everyone, 6 a random enemy, ...), +0x14 power (below 0: the unit's weapon
constant - VGetWeaponConstant; a monster's its attack command's), +0x18 multiplier, +0x20 its
attacks (the "magic number": Rakshasa Lotus 3), +0x21 each one's hits (below 0: the weapon's -
VGetWeaponHitNum), +0x24 its attack element, +0xa8 spread (a percentage either way), +0xac / +0xb0
HP cost, +0xb4 / +0xb8 MP cost (MbCommandInfo::GetMpCost - kind 0 the value, 1 / 2 a percentage
of the max MP, 3 x the weapon's consumption factor, Weapon +0x44 / 100, at least 1: Rakshasa
Lotus's 20 is 40 with weapon 501, as the reference's MP shows at 505 - 509 s), +0xc0 its formula
row (CommandFomula: action, damage, chain - BtlCalculator's table: 1 physical, 2 magic, 3 / 4 their
chain bonuses, 5 an HP heal, ...); its direction (direction.mbe CommandDirection: +4 the kind, +8
the motion - an AnimCommon type: 10 bi01, 15 bs01). The prologue hero's gene commands: Rakshasa
Lotus, Augmented Electra, Augmented Aquinas, Augmented Healing (All) - 40 / 18 / 36 / 24. A
monster picks among its mode-0 MonsterCommand row's eight (command, ratio, count) by ratio; its
skills are the row's eight ids at +0x80 (Gunner's, Axer's and Heterocera's 304: element 4
resistance -50).

Skills (skill_para Skill): +0x20 the condition (0 none), +0x2c the chance, then two (kind,
constant) effects at +0x30 - at +0x40 / +0x44 with the gauge's advantage (MbSkillInfo). Kinds: 1
physical resistance, 2 lowered physical, 4 magic, 5 lowered magic, 3 + 4e / 4 + 4e an element's
resistance / lowered one (BtlElement's table: element 4's 19 and 20), 0x23 percent damage
resistance, 0x24 attack multiplier, ... (BtlSkillUtility).

Damage (calculateBasicDamage, after BtlBasicDamageParam's bonus, status multiplier, the target's
resistance and the attacker's lowered one - calculatePhysicalDamage / calculateMagicDamage), a
hit's: clamp(attack / defence, 0.2, 5) x (power + bonus) x 0.5, then "Status" (x 1.5 with the
gauge's advantage), "Element" (battle.mbe Element: the attack element's row, the target element's
column; + the same-element and status-effect corrections), "Resistance" ((r + 100) / 100, r = the
attacker's lowered element resistance - (the target's physical or magic resistance + its element
resistance) + the attacker's lowered physical or magic one, at least -100: each its skills' and
its equipment's), "Level" (the attacker's gene level / 20 + 1 - VGetGeneLevel: a monster's its
level, a character's its gene's), "Command" (+0x18), "Status Effect" (the target's kind 34),
Team Play (MbBattleInfo's cooperation scales 0 / 1 / 3 / 5 by how many act together), "Damage
Multi", "Target Skill", the potions' buffs, "Critical Damage Percent"; (int), x 10 where the
coupling's +0x44 bit 3 says; then the spread (calculateHpFluctuation: x ((random - 0.5) x spread
x 2 + 100) / 100 - the low end exactly for an automatic command) and the chain bonus (magic: x (1
+ the target's combo value x 0.001); physical: also x the attacker's +0x78 virtual). Attack and
defence carry status-effect multipliers (kinds 36 / 38: GetAttackMultipliers,
GetGuardMultipliers); physical / magic damage the attacker's kinds 54 / 55. A heal (formula 5,
calculateHpHeal): clamp(defence / attack, 0.5, 2) x power x (gene level / 20 + 1) x (1 + the
skills' heal correction).

Criticals and downs: calculateCriticalHit (only where the command's +0xbc bit 4 allows, always
with bit 5): a random byte under the attacker's luck / the target's mapped 8 / 13 / 18 / 24 / 29 /
34 / 39 at 0.5 / 0.7 / 0.9 / 1.1 / 1.3 / 1.5 (+ skills, status effects, the target's bonus); a
critical adds 0.5 to "Status". calculateDown: a hit downs its target where the element's rate is
above 1, on a critical, where the command says (bit 6) or where the target is already down (its
combo chance - BtlBasicStatus +0x6d, set by TakeDamage from a downing hit); a down adds 0.1 to
"Status" and counts the target's combo (+1: the magic chain bonus, x (1 + combo x 0.001)). The
downs last while one side acts (BtlCommandManager::resetChance: the other side acting clears all -
ResetAllChance; the same side's next keeps them unless the last action began with one and did not
carry it on); every hit of an action plays, past the target's death.

The break gauge (BtlBreakGaugeUI, a BtlBreakPoint at -1000 .. 1000: above 0 the party's advantage
- "Player" -, below the monsters' - "Enemy" -, 0 "Even"; HaveAdvantage gives "Status" x 1.5 and
the bigger break bonus): each hit adds its points after its action is calculated
(BtlDamageChunk::AddBreakPointToCalculation, a damage record's +0x50). A hit's points
(calculateBreakPoint, BtlDamageTarget::SetBreakPoint): the command's +0x22 (s16; below 0 none) -
with +0xbc bit 1, the unit's weapon break constant instead (Weapon +0x40: 501's 100; a monster's
its attack command's +0x22) and +0x22 into the bonus -, plus a share of the action's bonus
(battle.mbe Battle +0x60 without the advantage: 100, a critical 50, a down 50; +0x50 with it: 0,
25, 25; status effects' bonus), split over its targets, then each target's landed hits; a kill
adds +0x6c / +0x5c (100 / 50: BtlDamageChunk::addKillBreakPointBonus) to its target's last; a
monster's negative. BtlBreakPoint::Add: the battle's first nonzero points jump to
+-500 whatever they are; from above 0 to 0 or below another -500 first (a break), likewise from
below; held within +-1000. Even() resets it only after a Continue. The side with the advantage acts
first. The HUD (btBrkGauge posed at ((v + 1000) / 20) / 30 s; btBrkName_fr00 0 Player, 1 Enemy, 2
Even, 3 at +-1000) follows the hits as they land.

Misses (calculateHit): a target with a kind 123 effect always evades; +0xbc bit 2 always hits;
else a random byte under +0x1c x the speed ratio's 192 / 213 / 233 / 251 / 269 / 290 (at 0.5 /
0.9 / 1.1 / 1.3 / 1.5; + the target's taken bonus) x the attacker's kind 48 multipliers, less
evasion. A miss does no damage, no points, no down; its popup is MISSED!.

Status effects (skill_para StatusEffect: +0x14 / +0x18 its turns - one between, -1 until removed -,
+0x1c its stack, +0x30 / +0x34 its chance - with the advantage -, four (kind, constant) from +0x38;
BtlStatusEffectList, the visitors multiplying constant / 100 or summing): given by a script
(BattleStatus.GiveStatusEffect - battle_0001's Battle_Boot gives the hero 40, 65, 66, 67) or by a
landed command (its +0x40 / +0x48 list: Guard's 51 gives 2 - kind 34 at 50, damage taken x 0.5
for the turn) where a random byte is under its chance + the luck ratio's -52 / -26 / -13 / 0 / 13
/ 26 / 52 - the target's resistance (a monster's s16 at mon_para +0x48 + 2i, i its index in
battle.mbe Battle +0x70 / +0x78's list: 13, 14, 15, 16, 1; Gunner's 999) - always with +0xbc bit 3.
Kinds: 31 no action, 34 damage taken, 36 / 38 / 40 / 42 attack / defence / speed / luck, 45 / 46
HP healed / lost a turn (a percentage of the max HP: calculateStatusEffectHpChange), 48 hit chance,
54 / 55 physical / magic damage dealt, 123 always evades; the menu's (BtlStatus::OpenMenuInterface:
140 no Attack, 47 no Gene, 66 no Item, 65 no Guard - the first battle's tutorial leaves Attack
alone, battle_0551's Gene; a button forbidden is drawn with diffuse (0.549, 0.549, 0.655) -
comBtn::SelectableSet). At the turn's end each loses a turn (BtlStatusEffect::Update).

A downed unit's command still to come waits (BtlCommandManager::delayCommandByDown: to the back).
The popups (BtlDamageUI::Draw, DamageInfoMes: btBonusMenu with its in, btBonus_fr00 at
call_btBonus_fr on frame kind - 1, btBonusBg_fr00 at call_btBonusBg_fr - frame 1 for kinds 4 / 5;
kinds 1, 3, 6 raised 1.2): 1 MISSED! (the damage's +0x6b), 3 blocked (+0x6e, SE bt_093), 4 (+0x6d),
5 CRITICAL! (+0x6c), after its number.

CR3-U has the base, element, resistances (skills without conditions, equipment), level, command
multiplier, spread, criticals, downs and combos, misses, status effects (their numbers, turns,
menu, no action, HP a turn), the delay of the downed, the break gauge and heals; not yet the
status effects' triggers and end timings (+0x20 / +0x24: those of a moment, counters), their
stacks and icons, conditional skills, evasion, items' effects, Team Play.
Against the reference (part 1): the hero's
first hit 352 at Even (CR3-U: 400 / 170 x 90 x 0.5 x 3.5 = 371, spread 352 - 389), a later one
576 at Player (x 1.5: 528 - 584); Gunner's four 34 and Axer's 40 (32 - 36 and 38 - 42: the
armour's 20%); against a Heterocera (436 - 450 s) Rakshasa Lotus 758 for two - one a critical, 284
+ 474 (CR3-U 282 - 311 and 450 - 498) - then 331 on the downed target (x 1.1: 310 - 342), and at
Player Augmented Electra (multiplier 0.95) 265 / 256 / .. / 265, 1049 for four (CR3-U 251 - 277
each); Heterocera's command 242, 12 - 13 a hit (CR3-U 12 - 14).

Scripted battles: the battle's script (battle_NNNN) runs Battle_Direction_End_Base after each
action's direction and Battle_Turn_End_Base at a turn's end; BattleStatus.GetHp reads the HP as
shown (the direction status, BtlStatus +0x88 - the turn's sums are ahead of it). The spider
(battle_0002): Battle_Boot gives both sides status effect 40 (kind 53, 1: an endure - TakeDamage
never below the least HP, BtlStatusEffectUtility::GetLeastHp: kind 53 a number, kind 121 a
percentage) and the hero AddExtraSkill 430 (condition 28 - every foe's HP at or under 1 -, effect
kind 100: add command 126, all foes, power 80, 3 hits, sure); Battle_Direction_End plays
s000_d01_00801's cut-in once the spider's HP shows 1, then the hero's added command finishes it
(CR3-U lets a skill's added command past the endure - the reference's 1276 kills it; how the game
does is not found).

A turn (BtlSysStateSelect): the get-set menu (EntryGetSet: Battle, Escape - not where the
coupling's +0x44 bit 1 forbids it -, Options; SE bt_001 the first time; the get-set camera held),
then each party member's command (6 Attack - its target -, 7 Gene - the gene's commands, a list -,
8 Item - the items, a list -, 9 Guard - command 51 on itself), then the party's actions and the
monsters' (each side's list in order: BtlCommandManager - the side ahead on the gauge first, the
party at even). Escaping (BtlSystem::Escape): sure with the gauge's advantage, else a random byte
under 240 (the party's level below the monsters'), 244, or 252 (twenty levels above). The win
(BtlSysStateAction::phaseVictory): the result music, the victory, a half-second fade, then the
result - unless story flag 5051 (byte 0x14cfb67 bit 3 of Cr3FlagWork at 0x14cf8f0) is on:
s000_d01_00301_evt sets it, so the ship's battles go straight on.

The interface (BtlInterfaceSystem, CRXParts at 100 pixels a unit): the party's status (btStMenu at
btStPosition's call_btStMenu_fit1: a btStPanel each - the portrait btStChara_id00 with
bt_chaIcon_j<char>, the HP / MP bars btStHp_per / btStMp_per posed at percent / 30 s, the numbers
SpriteNumId - set 1 of SpriteNumberParamPackTable: com_num_01's digits, v from the sheet's foot -,
HP reddened under 20%); the break gauge (btBrkMenu at call_btBrkMenu_fit7: btBrkWinPanel looping,
btBrkGauge at ((v + 1000) / 20) / 30 s, btBrkName_fr00's frame 0 Player, 1 Enemy, 2 Even, 3 a full
break); the commands (comBtnMenu, InterfaceMain interface 0x2c - comBtnPosition's
call_comBtnMenu_fit6, comBtnMenu_io as it opens; EntryCommandSelect's four buttons 6 Attack, 7
Gene, 8 Item, 9 Guard on panels 004..007, each comBtnPanel with comBtnBg_fr00 (its slot), and
comBtnIcon_fr00 / comBtnText_fr00 at the button's frame - the labels images, images_eng's in
English); the targets (btTgtMenu, btTgtMenu_in, a btTgtPanel each: the name at typeM_C01, the bar
btTgtBar_per, the icon bt_targIcon_<model><variant> masked by bt_targIcon_<model>_a); the damage
(DamageInfo: btNumMenu with btNumMenu_in over 0.53 s, the layout btNum_num0N for N digits, a
btNum_fr00 frame each - digit + 10 x the set); the lists (comListMenu, interface 0x2d:
comListPosition with its io, comListCategoryPanel - comListCBg / CIcon / Ctext at frame 0 Genes,
1 Items -, rows of comListPanel 0.76 apart from call_comListPanel - the name at typeM_L01, the cost
in comListNumM_num00 - and comListCursor on the focused; the focused one's help across the top in
btHelpMenu at btHelpPosition's fit8, command_help.mbe's); the result (BattleResultV2Menu:
btRsltPosition with its in, btRsltBgMenu, the titles, btRsltPointMenu - two btRsltPointPanel_numNN,
the EXP's and the money's, btRsltPointNum_fr00 digits -, btRsltInfoMenu, btRsltCharaMenu's
btRsltCharaPanel a member - its shadow, call_GeneCard, btRsltCharaIcon_fr00, btRsltLv_per and
btRsltLvPanel_num00's three btRsltLvNum_fr00 places. The background and the position's own meshes
stand at z 0.03 .. 0.05, in front of the menus' 0 .. 0.02, so the parts are drawn in order over it
- with depth tested they vanish. CR3-U draws all but the gene card and the level bar's progress;
no reference shows a result screen yet).

The cameras of an action (BtlCameraSystem::PlayPlayerMelee: the target's size's attack_PC riding
(target x, 0, 0) unturned, turned 0.2 of the way toward its actor target; PlayEnemyMelee: the
attacker's size's attack_<size> at the target's front (x, 0, Battle +0 / 10), 0.4 of the way).

`cr3view --battle <coupling> [--battle-field <n>]` (44 unless given) plays one: Up / Down, Return
(or a click on a button), Backspace back from the targets; `--input attack,t0,guard,...` chooses
for captures. `cr3tool cr3battle <release> <coupling> <field> [party]` prints its set-up and
simulates it; `--input battle,escape,attack,gene<n>,item<n>,t<n>,guard,result` chooses for
captures. Not in yet: Options (CR3's options screen), Emperor Mode and other specials, the gauge's
play, the result screen's gene card and EXP, drops, a lost battle's game over, the approach /
return of melee attacks, effects (eff### figures), voices, the list's icons and scrolling.

## Parameter tables (`data\*.mbe`, EXPA)

`MbParameterManager::structParam`: `EXPA`, u32 table count; per table a u32 name length, the name
(NUL-padded to it), u32 row size, u32 rows, then the rows. Then `CHNK`, u32 count, and per entry a
u32 file offset, u32 size and the bytes (a NUL-terminated string): the pointer slot at that offset
is pointed at them (`0xcccccccc` until then). The 64-bit builds start each table's rows and each row
8-byte aligned (4 bytes skipped where not) and their pointer slots are 8 bytes; the Vita's are
packed, 4-byte slots (`char_name.mbe`: rows 0x18 against 0xc). The columns' types are not in the
file - the game reads each table with its own code, by its name; the CHNK entries show which slots
are strings - or arrays: mon_cop's slots point at their monster ids. A table whose rows are a
multiple of 8 long reads either way, so the layout is the release's (64-bit for iOS / Android),
the other only if that fails. 63 files, 109 tables, 27,884 rows, the same in every release
(`tools/mbe.py`; in the engine `assets/mbe.hpp`, rows read by a column layout - `i`, `f`, `p` a
pointer slot, `_` the pointer alignment alone).

## The game database, offline (`data\*.bson`; db/cr3db)

CR3 was an online game: DBSystem::Request(id) queues a request, its SM_<name> sends JSON to the
game server and reads the answer into CrxGameData. Every release also carries the server's rules
(DBEmu) and its tables (48 `data\*.bson`; the iPhone's and Android's byte-identical, the Vita's a
little different). The Vita turns emulation on (GameSystem +0x1af; Android's +0x1b3 is a debug
menu switch, "Emulation"): DBSystem::AddRequest queues only the requests CheckEmulation lists, and
each handler calls its DBEmu function and finishes with the code it returns (FinishRequest, +0x1bc
= utils::GetSyncResult: 200 done, 0x192 full, 500 none such, 1000 an error); the rest are dropped
unqueued. The port plays every release that way and never contacts a server.

The requests: ids 0 .. 0xa3, named by the Vita eboot's table after the handlers (0x815e8a80: 5
state-machine entries, then a handler per id; the names 165 x 8 + 19 x 4 on). 77 are played
offline (`cr3Request(id).offline`): Auth, Save / LoadPlayer, AddItem, the genes (Add, Remove, Move,
Enforce, Enhance, Merge, Sell), the boxes (PickTreasureBox, OpenTreasureBox, OpenGBox,
OpenItemSheet, OpenPrizeBox, the GP / lucky / gene boxes), the presents, quests, tickets, gifts,
BuyGoods / SellItem / UseItem, the VS tournaments, missions and battles, VIP, Sync / Sync2. The
dropped: friends, visits, messages, the bazaar, colosseum matching, research, rankings, payments,
EnterDungeon (the server's gems), and the debug DB tools.

MVGL "BSON" is not MongoDB's: `BSON\n` and 3 bytes, then chunks - a type byte, the size (byte 1 x
0xffff + the u16 at 2, its 4 header bytes counted), the name at +4 (an offset into the text
chunk), the value at +8. The first chunk is the root; type 0x20 the text (NUL-ended strings from
+4 - its size falls short of the last few, which the game reads to their NULs regardless); 3 ends
a list. 1 an object, 2 an array: the count at +8, the chunks from +0xc to the list's
own end (an empty one is followed directly by the next chunk); 0x10 an i32, 0x11 an f32 (both read
as either: BsonMarshaller::GetI32 / GetF32), 0x14 a string (+8: its text offset). No other types
occur. CrxGameTable::Load reads them by Declaration tables of (type, count, name, offset): the
constants (`constant`'s one row, 162 fields - GOLDKEY_MASTER_ID 400005, GOLD_MAX, the seeds'
monthly ids ...) from libcrx's 0x011aa158.

Full item ids (CrxGameData::AddItem_Sub): an Item its own id (item_para Item 1 .. ); 1xxxxx a
weapon, 2xxxxx armour, 3xxxxx an accessory (100000 x the category + the table's id); 4xxxxx,
5xxxxx other goods (tickets, the gold key 400005, gems 500001), 6xxxxx a gift. 99 of one at most:
dbutils::EarnItemPrize sends the rest to the presents box (SendPresent, from PRESENT_FROM_SERVICE).

Item sheets (`bingos`, CrxItemSheetTable 0x150 bytes): a dungeon's treasure prizes - `_id` (d02
..), up to 17 `items` (category "gene" a gene, else an item by its full id; item_id) and the
`result` for winning them all. DBEmu::DoOpenItemSheet draws from the entries not won yet (all of
them once the sheet is done, or with `all`), shuffled, the first won; with a key one gold key is
spent, with a flag that flag set; the first time every entry has been won the sheet's result is
given too. MarkItemSheet / UpdateItemSheet keep each entry's "new" mark (won since the last
open) and the sheet's newly-done flag; _PickTreasureBox draws n entries not marked (any, with all
or the sheet newly done; any at random once they run out). The scripts: Cr3GimmickData.
OpenItemSheetRequest(sheet) (Fld2OpenItemSheetRequest: OpenItemSheet, no key, no flag),
SetItemSheetName, GetOpenItemSheetItemNo / Type (the prize: its id, 1 a gene). `cr3tool db
<release> [sheet]` lists the tables and plays each rule once from a new game.

The rest of DBEmu (libcrx 0x71b350 .. ; the Vita's Sync2 at 0x8107a1b0 reads cleanest), each a
`Cr3OfflineDb` call that finishes the request with its code:

- Login (Auth offline): CrxGameData::CheckDateChanged, then GetThisMonthMaster. A new day (it
  turns at DATE_LINE_HOUR): the day's marks cleared (CrxGameData +0x2869c: bit 0 the ticket, 2 the
  quests drawn, 3 the VS mission), a LOGIN_BONUS_COIN_MASTER_ID present, DATE_CHANGED_GP_BONUS GP,
  DATE_CHANGED_CHEERPOINT_BONUS cheer, DATE_CHANGED_CLEAR_FLAGS cleared. GetThisMonthMaster: rows
  whose `sdate` .. `edate` (MMDD, this year, the end day whole, never wrapping past December: a
  0701 .. 0531 row never holds) hold today - `vipbonus` (exp / drop / gold bonus), up to five
  `geneboxschedules`, `flags` (their flag set), the VS reward sheets.
- Money: gold (+0x1ebc4), Oz coins and GP (+0x1e76c / +0x1e774, each with a spent count after
  it), the gene stock's room (+0x1e768) - a new game's all 0. utils::GetGold / GetCoin add
  (GoldGet / CoinGet), SetGold sets, Check* read.
- Shops (`shops`: _id "T01_1" .., `goods` - _id, gold, GP, coin, up to 3 items, flag): BuyGoods
  (coins short 0x3f2, GP 0x3f3, gold 0x3f1); SellItem pays item_para's +0x18 (column 6: a fifth of
  the buying price); UseItem; UseContinue (CONTINUE_GEM_MASTER_ID); ExchangeGem (99 GEM_MASTER_ID
  for a continue gem); BuyGeneStock (GENE_STOCK_PRICE coins, the room + GENE_STOCK_CAPACITY_ADD to
  _MAX); GetDailyItem (`dailyitems`, once: its flag; 0x193 had).
- Boxes: OpenGPBox(n, GP) - a sheet won from (+0x14a) at random, n of its entries, any;
  OpenLuckyBox(sheet, n, coins); OpenGeneBox(box, n, coins) - `geneboxes`' baskets by `prob`, a gene
  of the basket at random (masterId, exp, commands); OpenPrizeBox("ocean") - 16 (item, count) at
  libcrx 0xf30fe0 by the draw's low 4 bits, flag 4026 set.
- Genes held (CrxGeneData 0x74: masterId, exp, 16 command slots, +0x5c its id, +0x70 locked): 50
  (GENE_CAPACITY) plus the stock's room, then the presents box. utils::GetGene(id, level) - the
  first free of the 50 -, CheckGene (a count), CheckNewGene (never had: the gene book).
- Presents (100, the oldest dropped): ReceivePresent(s) (a login-bonus coin is LOGIN_BONUS_COIN
  coins; some left 0xce), Reject / DropPresent, EarnPresent(1 .. 4: genes 10, 13, 11 at 1239, 2869,
  27674 EXP; a gold key), ClearPresents.
- Gifts: MergeGift (`mergegifts`: its gene_id for the gifts given), ItemizeGift (`itemizegifts`:
  the gift for its item).
- Quests (`quests`: the Declaration's _id, `_no` - absent, read at text offset 0, which is `no` -,
  stime, etime, flags x 6 (< 0 one clear, > 0 one set; their `expr` JavaScript is not read), lv,
  rank, item_id, item_count): FindQuests - rank 0 all listed (20 at most); ranks 1 .. 4 shuffled
  into ten slots, 4's up to 10 from slot 0, 3's up to 6 from 4, 2's up to 3 from 7, 1's up to 10
  from 9 downward, the later over the earlier, once a day; then 5 / 6's up to 10. Begin / End /
  SetQuest; EarnQuestRewards (`questprizes`: item, gold, genebox, luckybox, else a gene; taken
  already 0x3f9). utils::SelectQuest is a menu (DBSelectQuestTask) the port has not yet: none chosen.
- Tickets: GotTicket (today's had), ReceiveTicket (item 118, once a day; 0x3ef).
- Sync2(event): an event's gifts - the tutorials' genes, quest56's, s100_t01_00702's two gold keys,
  s272_t01_00401's pairs of gifts, c140's, t02_kbox_03's item 59.
- Genes (gene_para.mbe Gene, 13 ints a row the same on all three: id, series, grade, top level,
  EXP curve, rank, type, -, worth at level 1, worth a level more, price likewise, kind; ExpTable:
  a level, then each of 5 curves' EXP, -1 past its end - level 1 has none, 0; gene_command_skill:
  gene, level, type 0 a command / 1 a skill, id). A gene's level is the highest its EXP reaches
  (MbGeneInfo::GetLevel); its slots 0 .. 5 its own, learnt in the rows' order as it levels (a free
  slot, else over 0 .. 5 in turn), 6 and 7 inherited (CrxGameData::InheritSkill - the Vita's: 6,
  else 7; Android's decompiles as both). InitGene relearns a gene with no slots from its level.
  Rarity: a gene's place in its collection_book line.
- MergeGene: the pair in mergeGene2 (ids a, b, result), else - one series - the lower grade's,
  else the gene of kind kMergeKind[a][b] (libcrx 0xf31060, 7 x 7), the lower grade of the two, not
  type 1, whose rank is the least above half the two's (else the most no higher than half + 1);
  both at their top level and its rarity under 2: the next gene. EXP: (a's worth + its per-level x
  (level - 1) + b's likewise) / 4, to its top. Two of the parents' commands and skills (slot by
  slot, a's command, b's, a's skill, b's; once each; none the child's own) at random, kept as
  inherited. 100 gold a level of the two.
- EnhanceGene (InterfaceMain::StartIntensificationSynthesis): the fed genes' worth at their level,
  1% more for each of the target's kind; 100 gold a fed gene x the target's level before.
  EnforceGene: mode 1 a seed (*_SEED_MASTER_ID, 27, and its *_AMOUNTS x n to attack / guard /
  speed / luck; GENE_ADD_MAX in all; the seed not used up here), 2 the EXP, 4 the slots the menu
  worked out (items: MbItemInfo's field effects 1 .. 4 stats, 5 levels, 7 / 8 skills), 2 .. 8 use
  n. SellGene: price + per-level x (level - 1). MoveGene: the 50 and the stock.
- EnforceItem(item, material, level, gold): the material attached to the equipment at that level
  (CrxItemData +8 / +0xc), used up. BuyMP: VS_MP_PRICE coins. BuyVIP: VIP_PRICE coins for VIP_DAYS
  (to VIP_DAYS_MAX); BuyVIPTrial: VIP_TRIAL_DAYS, once. A new day uses a VIP day, then (still VIP,
  daily bit 4) dbutils::SendVIPLoginBonus: a gold key and item 106 as presents. PollMessage: the
  `news` rows whose dates hold today (their text ids); the emergencies are the server's (the table
  is empty in every release).
- Not yet: the VS mode's rules (GetVSTournaments - `vsTournaments_cli`, 36 ints: _id, mode, ghost,
  icon, level, bgm, flags x 3, clear_flag, exp, mapId, coupling x 3, count, rule, prize_gold /
  _icon / _category / _id / _count, season_item_id, season_prob, sdate x 5, edate x 5 (MMDD
  windows), perfect_flag, banner -; Begin / EndVSTournament, EndVSBattle(score, mission), the
  weekly score from Monday, the daily mission, seasons, boost items, PickWildcard): their state is
  a block of the save (CrxGameData +0x28550 ..: the tournament's and the week's score, the mission,
  the two rating ranks at +0x28580, the season's points and level, the boosts), to be ported with
  the VS mode itself; and the menus.

## Scripts (`script\*.nut`, `script64\*.nut`)

Compiled Squirrel 2.2.4, as CR2's. Every release has the 32-bit `script\` set (194 files); iOS and
Android also carry a 64-bit `script64\` set of the same scripts, which a 32-bit VM can't read -
CR3-U runs `script\` everywhere. iOS and Android's are identical; the Vita's differ in 36 (the
dungeon and town scripts, `chapter_jump`), and the fan translation replaces 20 of those.

Unlike CR2 (a script per event), CR3 keeps an area's events in one script: `t01.nut` defines 1,946
event functions, named for their message files (`s212_d08_01501`, `q066_t01_00201`,
`c111_s102_t01_00510`), with the shared helpers (`Msg`, `Cutin_Start`, `FadeIn`, `PlayBGM`, ...)
before them. Also `battle_NNNN` (per battle), `vs`, `chapter_jump` and debug `test_*` scripts.

The natives: 600 functions in 32 tables, from the Android library's `Cr3Bind*` functions (arm64:
`Sqrat::Object::BindFunc(table, "Name", &fn, 8, closure, false)`; `tools/sq_bindings.py --base
0x100000`, `tools/gen_native_api.py` -> `engine/src/script/native_api.inc`). Beside CR2's tables
(Cr3System 127, EvtScene 50, Cr3GimmickData 59, EvtWindow 26, ...) CR3 has `InterfaceWindow` (106),
the battle classes (`Battle`, `BattleUnit`, `BattleStatus`, ...) and `LunchEvent`.
`cr3tool events <release> [script | all] [seconds]` plays every event from its start through the
event runtime: all 4,127 end within 600 s of game time on iOS, the Vita, and the Vita with the fan
translation (Android's scripts are iOS's). `vs.nut` repeats 731 of the areas' events without the
helpers they call (`NPCTalkReset`, `quest_complete`): the game loads it only to run `VStuto_Clear`
(`DBSystem::SM_SkipVSTuto`) and runs every event from its own area's script (`EvtSystem::LoadScript`
with the area's name, even the debug event list), so those copies are not counted.

CR3 natives the runtime answers so far (`engine/src/event/runtime.cpp`, `script/gamestate.cpp`):
`EvtFace.CheckInScreen` (a cut-in on screen: `!InterfaceMain::CheckCutInNoScreen`),
`Cr3System.WaitSync` / `GetSyncResult` (`DBSystem::IsIdle` and its last request's result: the port
has no server - idle, 200), `Cr3FieldMapCamera.SetPos` / `CalcParameter` / `GetPos_X` / `GetPos_Y`
(the field camera as an event moves it), the gift synthesis effect (over when its figure
`ue003gift01`'s animation ends), the staff roll's "to be continued" (true with no staff roll menu,
as `InterfaceMain` answers - its scrolling is the UI's to come), the two- and three-way choice
menus (`GetTapSelect...ChoiceMenu` -1 until tapped) and the text entry (`ShowInputDialog`,
`CheckInputDialog`, `GetInputDialogText`, `Set...NameFromInputDialogText`). Headless runs answer
questions as a player confirming would (`askYesNo` with `autoPage`): yes, the first choice - then
the next each time the same menu is asked again (the quizzes) - and the text entry as given.

## Sound

Files `sound\<name>.<ext>` in the sound archives (iOS `CRDBbgm_mNN` / `CRDBse_*` / `CRDBvo_*`,
Android's asset packs, the Vita's `bgm/`, `se/`, `voice/*/`). `audio/cr3sound.hpp`.

| | iOS | Android | Vita |
|---|---|---|---|
| music (54, m01..m54) | `.snd`: AAC in MP4, 44.1 kHz stereo | `.snd`: Ogg Vorbis, 44.1 kHz | `.snd1` (an SXD header, its wave streamed) over `.snd2` (`SXDS`, ATRAC9 48 kHz) |
| voices | `.snd` AAC in MP4 (3,550: s000..s180 only) | `.snd` Ogg Vorbis (5,877) | `.snd1` / `.snd2`, ATRAC9 48 kHz mono (5,877) |
| effects (1,033) | `.sndl` s16 mono 22,050 Hz; `.sndm` s16 stereo 22,050 Hz; `.snds` s16 stereo 44,100 Hz | the same | `.snd3`: one SXD each (HE-VAG), its own rate, channels and loop |

- `data\bgm.csv` (name, volume, pitch1, pitch2): "pitch1" / "pitch2" are each track's loop start and
  end in 48 kHz frames - the Vita's own WAVE entries hold the same numbers; the iOS and Android
  streams are the same length at 44.1 kHz, so the points scale by 44.1 / 48 (the loop's end lines up
  with its start at offset 0, +-1 frame, on 53 of 54 tracks in every release; m53 loops whole).
  `data\se.csv` gives the effects' volumes, `data\voice.csv` only a master volume. The first row of
  each, `master`, is the kind's master volume.
- MP4: the first track's `stsd` (`mp4a` + `esds`: the AudioSpecificConfig), `stsz` / `stsc` /
  `stco`; the encoder's priming (2,112) and the valid frames are in iTunes' `iTunSMPB` tag (the edit
  list says nothing).
- SXD (CR2's, docs below): CR3's files end with an `SXDS` block holding the streams - not a chunk;
  the chunk walk stops there. A streamed wave's offset is into the `.snd2`.
- 6 Vita voices (`j004_147`, `s203_t01_00201001`, `..._003`, `s250_t01_00201001`,
  `s250_t01_00301002`, `s271_t01_00101009`) have no ATRAC9 config in their WAVE entry (1 or 3);
  their streams are the same mono 48 kHz ~72 kbit/s as the other 5,871 (config `fe 70 05 f0`), and
  decode with it to the Android voices' lengths.

---

# Chaos Rings II formats (CR2-U's notes, kept as the base CR3 builds on)

Notes on the game's data as read so far. "iPad" is 2.1.0 (`ChaosRings2enHD.app`), "Android"
2.1.2 (`com.square_enix.chaosrings2gp`), "Vita" PCSG00498.

## The releases

| | iPad 2.1.0 | Android 2.1.2 | Vita |
|---|---|---|---|
| data | `CRDB.ios.mvgl` (371,582,259 B) | `main.212.com.square_enix.chaosrings2gp.obb` (391,618,202 B used of 1,011,429,376) | `CR2/resources/CRDB.psp2.mvgl` (567,313,685 B) |
| files | 12,001 | 11,711 | 12,233 |
| sound | `Sound.ios/sound/`: `bgm01..` `.caf`, `sndeff.bin`, `voice.bin` | (in the OBB / APK: not yet looked at) | `Sound.psp2/sound/`: `sndeff.sxd`, `voice.sxd2` |
| code | `ChaosRings2enHD`: fat armv7 / armv7s Mach-O, decrypted (cryptid 0), 463 symbols (Squirrel's) | `lib/{armeabi-v7a,x86}/libChaosRings2.so`: ~18,400 exported names | `eboot.bin`: fake-signed SELF |
| text | `*.lproj/Localizable.strings` (8 languages: system dialogs) | | |

The OBB is itself an MDB1 archive; its tail beyond the archive's own size is not read.

## MDB1 (`.mvgl`)

Media.Vision's archive (MVGL's `Utilities::Database`). Little-endian.

| Offset | Size | |
|---|---|---|
| 0x00 | 4 | `MDB1` |
| 0x04 | 2 | tree entries (T) |
| 0x06 | 2 | names (= T) |
| 0x08 | 4 | data entries (D = T - 1) |
| 0x0c | 4 | data start |
| 0x10 | 4 | the archive's total size |
| 0x14 | 8 T | tree: i16 compare bit, u16 data id, u16 left, u16 right - a Patricia trie over the names for lookup; entry 0 is the root (data id 0xffff) |
| | 64 T | names: a 4-byte extension (NUL or space padded: `img `, `nut `) then a 60-byte name; tree entry i's name is name i |
| | 12 D | data: u32 offset from the data start, u32 size, u32 stored size |

Names use `\` for folders: `images\`, `message\`, `shaders\`, `script\`, `data\`; most files
are at the root.

A file whose stored size is below its size is **doboz**-compressed (Attila Afra's LZ77 variant):

- a header byte: bits 3..5 = the size fields' width - 1 (so 1, 2 or 4 bytes), bit 7 = stored as is;
  then the size and the compressed size, each that wide;
- then 32-bit control words, read low bit first, 31 flags each (the top set bit marks the end):
  a flag of 0 is one literal byte, 1 a match;
- a match is 1..4 bytes picked by the low 3 bits of the next 32-bit word:

| low bits | bytes | offset | length |
|---|---|---|---|
| x00 | 1 | (w & 0xff) >> 2 | 3 |
| x01 | 2 | (w & 0xffff) >> 2 | 3 |
| x10 | 2 | (w & 0xffff) >> 6 | 3 + ((w >> 2) & 15) |
| 011 | 3 | (w & 0xffffff) >> 8 | 3 + ((w >> 3) & 31) |
| 111 | 4 | w >> 11 | 3 + ((w >> 3) & 255) |

The offset counts back from the end of the output so far (no -1). Every file of the three
archives decodes to its size (`tools/mvgl.py check`).

## File types

Counts are the iPad's (Android / Vita alike unless noted).

| Type | Files | What it is |
|---|---|---|
| `anim` | 3514 | an Edge Animation animation; magic `EA04` (`40AE` as bytes) |
| `img` | 1873 (Android 1605, Vita 1950) | a texture: the iPad a legacy PVR file, Android PVR v3, the Vita GXT (below) |
| `geom` | 1724 | a figure's meshes and materials (below) |
| `skel` | 1724 | an Edge Animation skeleton; magic `ES02` (below) |
| `name` | 1259 | a figure's names: u32 joints, u32 materials, then an offset per name (not terminated: each runs to the next) - the joints' and materials' names, whose hashes the figure uses |
| `dat` | 880 | tables: `message\*.dat` (874) the text (below), `data\*.dat` (6) |
| `shad` | 466 | a GLSL ES shader's source (`shaders\<4 hex words>_fp` / `_vp`) |
| `phys` | 419 | a Bullet scene: the fields' walls and floors, their triggers (below); magic `SYHP` ("PHYS" as a u32) |
| `nut` | 139 | a compiled Squirrel script: `0xfafa`, `SQIR`, char / int / float sizes, then `PART`-separated parts; the source name (`D12_01_0206_evt.i`) and `main` |
| `emta` | 3 (Android 2) | `eff267`, `eff280`, the two figures with a particle system (below): u32 1, f32 2, then a short list of values. Nothing in the game reads them (no `emta` in the library, as text or as a word) |

## Textures (`img`)

`engine/src/assets/image.cpp` decodes all of them (`cr2tool images`: 1873 / 1605 / 1950, none
failing); the same texture decoded from the three releases differs by 0 to 5 levels in 255 on
average (each release's own lossy compression), orientation and alpha alike.

| Release | Container | Formats (count) |
|---|---|---|
| iPad | legacy PVR: 52-byte header (u32 0x34, height, width, mips, flags, data size, bpp, masks, `PVR!`, surfaces); flags' low byte the OGL pixel type, 0x10000 = rows bottom-up | PVRTC 4bpp 0x19 (1629), RGBA4444 0x10 (64), RGBA8888 0x12 (54), RGBA5551 0x11 (50), AI88 0x17 (34), A8 0x1b (33), I8 0x16 (5), RGB888 0x15 (3), RGB565 0x13 (1) |
| Android | PVR v3 (`PVR\x03`, u64 pixel format: 4 channel names + 4 bit counts, or an enum when the high half is 0; metadata key 3 = orientation) | ETC1 = enum 6 (836), rgba 5.5.5.1 (518), 4.4.4.4 (153), la 8.8 (33), 8.8.8.8 (22); legacy PVR A8 / I8 / AI88 / RGB888 (43) |
| Vita | Sony GXT (as CR1 / Omega's Vita releases: data offset 0x20, type 0x30, format 0x34, size 0x38), swizzled (Morton order, y bit lowest, square tiles of the smaller side; per 4x4 block for the BC formats) | UBC2 / DXT3 0x86000000 (1858), U8U8 GRRR 0x07002000 = luminance + alpha (44), U8 R000 0x00006000 = alpha (38), UBC1 / DXT1 0x85000000 (5), U8U8U8U8 ARGB 0x0c001000 (2); legacy PVR (3) |

PVR v3's channels of 8 bits each are stored byte by byte in order; its packed ones (4444, 5551)
from the top bit down.

Row order: every Android PVR and all but 2 of the iPad's are stored bottom row first (the GL
releases upload the rows as stored, so texture v = 0 is the picture's bottom); every GXT top row
first. The meshes' texture coordinates are the same in all three releases, so the decoder turns
the GXTs bottom-up: the same coordinates then reach the same texels everywhere.

How a texture is sampled is the material's (its texture entry's flags, below), not the file's.

## Meshes (`geom`)

`tools/geom.py` (`info`, `check`, `render`) and `engine/src/assets/geom.cpp` (`cr2tool models`):
every file parses in all three releases, all indices within their meshes' vertices; the textures
the materials name are all in the archive but 10 references on the iPad (3 names). From `MVGL::Draw::Figure::Build`, `BuildMesh` and `DrawMesh`.

The iPad's files are version 0x3c, Android's and the Vita's 0x50: the newer header is 0x10 bytes
longer, so every offset moves by 0x10; the records are alike. The Vita's keep Android's layout
but in GXM terms: index type 0 (U16) and primitive 0 (the indices are still the same strips),
attribute types SceGxmAttributeFormat (0 U8, 1 S8, 2 U16, 3 S16, 4..7 their normalised forms, 8
F16, 9 F32): positions F32, normals / tangents / binormals / texcoords / weights F16 (texcoords
final, not 1024ths), colours and joints U8. Some say a vertex stride 2 bytes over what their
attributes use and pack their vertices at the used size - the 427 proxy meshes (`p` files: 24 for
22) and one more - while 347 others are padded to the stride they say; only the positions tell
(read at the wrong stride they leave the mesh's bounding sphere). Decoded (`cr2tool
compare-models`), the iPad's and Android's agree exactly in the 1716 files where both have the
same meshes, Android's and the Vita's to 0.0005 in normals; Android's texcoords wrap at the 16-bit
range (±32) where the Vita's don't (a whole number of repeats: the same sampling).

| Header | |
|---|---|
| +0x00 u32 | version (0x3c / 0x50) |
| +0x04 u16 | meshes |
| +0x06 u16 | materials |
| +0x08 u16 | lights (below) |
| +0x0a u16 | cameras (below) |
| +0x10 u16 | relocations (the list at +0x48: offsets of pointers to make absolute) |
| +0x18 | the bounds |
| +0x30 u32 | the meshes (0x54 bytes each) |
| +0x34 u32 | the materials |
| +0x38 u32 | the lights (0x34 bytes each) |
| +0x3c u32 | the cameras (0x24 bytes each; 0x30 in CR3's 64-bit geoms - the same fields, padded) |
| +0x40 u32 | the inverse bind matrices, one per skeleton joint: 3x4 row-major (0x30 bytes) on Android / the Vita, 4x4 column-major (0x40) on the iPad |

| Mesh | |
|---|---|
| +0x02 u16 | attributes |
| +0x04 u16 | vertex stride |
| +0x06 u16 | index type: 0x1403 (u16) in all |
| +0x0a u16 | primitive: 5 (triangle strip) in all; degenerate triangles join the strips |
| +0x10 u32 | material |
| +0x14 u32 | vertices |
| +0x18 u32 | indices (all of them drawn; some skinned meshes have more vertices than indices) |
| +0x24, +0x28 | vertex data, index data |
| +0x00 u16 | the matrix palette's size |
| +0x2c | the palette: that many u32 joint indices (a rigid mesh: one) |
| +0x34 | attribute descriptors, 8 bytes each: u8 semantic, u8 normalised, u16 components, u16 GL type, u16 offset |
| +0x38 | bounds: centre (3 floats), half extents (3; not exact), radius (+0x50) |

Semantics (the shaders' attributes): 1 Position (3 floats), 2 Normal, 3 Tangent, 4 Binormal (3
normalised shorts each), 5 / 6 Texcoord0 / 1 (2 shorts, divided by 1024 in the vertex shader),
9 Color (4 normalised bytes), 10 / 11 blend indices / weights (bytes). Skinning: a palette of 56
joints (`MatrixPalette[56*3]`). The weights, normalised bytes, fall short of 1 (two 127s: 254 /
255), and a vertex short of its joints is pulled towards the world's origin by that much: unseen
near it, plain far off (an event's j001 37 units out: his hair drawn out behind him, his collar and
eyes askew) - CR3-U makes each vertex's sum 1 (GeomMesh::resolveJoints). The releases store a vertex's joint differently, and the vertex
shader says how it reads it:

- Android: blend indices as MatrixPalette's rows (slot x 3; `idx = Indices`), on every skinned mesh.
- The iPad: blend indices as palette slots (`idx = Indices * 3.0`); a mesh on one joint a vertex
  has no blend indices but a four-component Position whose w is the slot (`idx = Position.w * 3.0`;
  3 shaders take `Position.w` as the row). Meshes on the built-in figure shader (key 0) store slots.
- The Vita: as the iPad's meshes, but the stored values are rows (slot x 3) - its programs are
  compiled.

Read so, the three releases' 714 skinned meshes all index inside their palettes (107 on the
iPad and the Vita through the position's w: the interface's masks, as the HP / MP bars' that
uncover the gauge, and figures' rigid parts).

A material: u32 hash, +0x04..+0x13 its **shader key** (4 u32: below), +0x14 u8 parameters, +0x15
u8 render states, then 0x14-byte entries: u8 parameter, u8 kind (0 texture, 1..4 that many
floats, 100 a render state), u16, then the texture's name (12 characters: `images\<name>.img`)
and at +0x10 its sampler flags, or the values. The parameter ids index MVGL's shader parameter
table (48-byte entries of a name and its size, in the library): 0x32 ColorSampler, 0x33
DiffuseColor, 0x35 NormalSampler, 0x36 Bumpiness, 0x38 SpecularStrength, 0x39 SpecularPower,
0x3e SurfaceColor, 0x3f FuzzySpecColor, 0x40 SubColor, 0x41 RollOff, 0x42 VelvetStrength, 0x43
LightSampler, 0x44 OverlayColorSampler, 0x47 OverlayStrength, 0x48 CLUTSampler, 0x54 Time, 0x55
ScrollSpeedSet1, 0x58 ScrollSpeedSet2, 0x5e OffsetSet1, 0x61 OffsetSet2, 0x64
DistortionStrength (and, set by the engine, 0x09 / 0x0b DirLamp01Color / Dir, 0x2e AmbientColor,
0x2f SkyDir, 0x31 GroundColor, 0x53 CameraPosition, 0x65 FogParams, 0x69 FogColor, 0x6a
AlphaRef). The UI is figures too: the title screen is `title.geom` (7 textured quads, its own
skeleton) played with `title_in` / `title_wait` / `title_loop` / `title_out`; its colour comes
from LightSampler (an A8 mask beside an opaque colour texture: `hud_logoTtlC` + `hud_logoTtlA`)
and its DiffuseColor from the animations (user channels, below).

A texture entry's sampler flags (`Texture::Build` copies them to the texture; `FinishInitialize`
sets them): bits 0-2 wrap S, 3-5 wrap T (the table at 0x53b6a4 in the Android library: 2
GL_MIRRORED_REPEAT, 3 GL_CLAMP_TO_EDGE, else GL_REPEAT), 6-8 the min filter's function
(`AdjustTextureFilter`: 1, 3, 4 nearest, else linear; with mips, the mipmapped forms), 9-11 the
mag filter (the table at 0x53b688: 1 nearest, else linear). All three releases agree: 1579
repeat / repeat, 1359 mirrored / repeat (the characters: their meshes mirror the left half's
texture coordinates past u = 0), 123 mirrored both, 117 + 97 clamped both, 26 mixed.

A render state's entry holds 4 u32 GL values; the ids are `RenderContext`'s table (its names, in
order from 0x78): `alpha_func` (func, ref as a float - GREATER), `alpha_test_enable`,
`blend_func` (src, dst), `blend_equation`, `blend_enable`, `cull_face`, `cull_face_enable`,
`depth_func`, `depth_mask`, `depth_test_enable`, `polygon_offset`, `polygon_offset_fill_enable`,
`color_mask`, `stencil_func`, `stencil_mask`, `stencil_op`, `stencil_test_enable` (0x88). Those
a material leaves out keep the defaults: no alpha test, no blending, back faces culled, depth test
and writes on. The textures' alpha is not transparency unless the material tests or blends (the
characters' carries other data).

### Lights

A figure can carry lights (`Figure::SetupLights`, `RenderContaindLights`), records of 0x34 bytes
(0x40 in the 64-bit builds: the same fields, padded - read at 0x34, a figure's later lights, the
maps' and events' fog, come out as garbage): +0 u32 the joint whose name hash it is (that joint
places it), +4 u16 type (0 point, 2 ambient, 3 directional, 4 fog), +6 u16 which of its kind
(DirLamp01..03, PointLamp01..07), then floats from +8:

| Type | +0x08 | +0x0c | +0x10.. | +0x1c | +0x20.. |
|---|---|---|---|---|---|
| directional | intensity | | colour (4) | | |
| point | intensity | range (unused) | colour | | |
| ambient | intensity | | sky colour (3) | | ground colour (3; 0 in every map) |
| fog | far | near | colour (3) | alpha | |

The engine's uniforms: DirLamp01Dir = the joint's +Z axis, normalised (the direction towards the
light), DirLamp01Color = intensity x colour; AmbientColor / GroundColor = intensity x the colours,
SkyDir = the joint's position, normalised ((0, 1, 0) at the origin); FogParams = (far / (far -
near), -1 / (far - near), alpha), FogColor. Without a light figure, RenderContext's defaults: the
light (0.531, -0.758, 0.379) at 0.5 grey, ambient 0.5, ground 0.3, sky (0, -1, 0), fog (1.5,
-0.005, 1) white. The maps' lights are figures of their own: `<map>c` lights the characters (118
with ambient + directional + fog, ...), `<map>b` battles, `*_evt_light` events; a vista its map
figure's own (VistaMain::LoadMapData builds `<map>` and `<map>s`: e0009f's dark navy fog, far
2500).

### Cameras

`Figure::SetupCameras` / `GetContaindCamera`: +0 u32 the joint whose name hash it is (the camera
looks down that joint's -Z, its +Y up), +4 f32 the vertical field of view in degrees, +0xc near,
+0x10 far, +0x14 an orthographic half height, +0x18 u32 orthographic. The maps' `<map>c` figures
hold their field camera (`CAM_d0101c`: 13.2 degrees, 12 units away), with the lights, a
`defaultPos` joint (where the player starts) and the walls (`MTR_kabe`); `<map>p` the entrances
(`d0101_p1`, `ragdoll_d0103_p1`, ...) and boxes (`evt021_box00`, `c208_box00_scale080`).

Names are hashed with `MVGL::GenerateNameHash`: CRC-32 (reflected, 0xedb88320) from 0xffffffff,
not inverted at the end.

### Particles (never run)

The header's +0xe (u16) counts particle systems and +0x44 is the offset of their data. Only
`eff267` and `eff280` have one, a single system each and no meshes.

A system is 0xbc bytes, then its curves:

- u32 n, then n × (t, r, g, b): the colour.
- u32 n, then n × (t, alpha).
- u32 n, then n × (t, x size).
- u32 n, then n × (t, y size).
- u16 n, u16 stride (20), then n × 8 bytes: its vertex layout (position 3 floats, colour 4
  bytes, texture coordinates).

Each curve is linear between its keys (Particle::GetValue). Fields of the 0xbc bytes, as
`MVGL::Draw::Particle` reads them:

- +0x08: the emission's spread, 0 over a sphere, 1 about one axis, else none.
- +0x0c / +0x10: the distance from the emitter.
- +0x14..+0x1c: the direction.
- +0x24 / +0x28: the speed and its random part.
- +0x2c: the most alive at once.
- +0x34 / +0x38: the life and its random part.
- +0x40 / +0x44 and +0x48 / +0x4c: the size and its range.
- +0x58..+0x60: an acceleration added to the velocity each update.
- +0x68: the damping the velocity is multiplied by.
- +0x78: the sprite's twist (1 none).

The game never runs any of it. Figure::SetupParticles, Figure::RenderParticle and
Particle::Update are in the library, but nothing calls them: no branch, no pointer and no
relocation names them. MVGL's other effect system, Spark, needs a `.sprk` beside the figure, and
there is none. So `eff267` draws nothing. It is commands 252 .. 255's shot, the breaths of the
dragons and Wyverns, between their launch effect `eff266` and the mark `eff268`. `eff280` is only
named in the battle effect table. The port draws neither.

## Fields

`FldMap::ChangeArea` builds a map from a table in the **executable** (225 records of 0x18 bytes,
from `t0101`; the iPad's executable, the Vita's `eboot.bin` and Android's library each have one -
the iPad's and the Vita's set +0x17 on 8 maps where Android's doesn't):

| Record | |
|---|---|
| +0x00 | the name (`t0101`, `d0101`, ...) |
| +0x08 i32 | the painting: 0 = `f1` (drawn at half scale), else `f2`; for the scroll, bit 1 = 2048 units wide (else 1024), bit 0 = 2048 tall, in units of a 960-wide screen |
| +0x0c u8, +0x0d u8 | a midground, a foreground layer |
| +0x10 i32 | 1: the foreground at twice the scroll, 1152 units to the left (one map) |
| +0x14 u8, +0x17 u8 | a 3D figure `<map>f` (and its animator) |
| +0x15 u8 | the painting is the shared `d5051f2_hdbg` (a sky: 0.4 of the scroll, not clamped) |
| +0x16 u8 | no painting (a 3D map: the camera unscaled, unscrolled) |

The painted layers are `images\<map><f1|f2>_hd<bg|mg|fg>.img`: 2048 x 2048 textures (the painting
in their middle, alpha for the foreground's cut-outs), each drawn as a 4096 x 4096-unit quad
(`CustomFigure`: the background at z -5000, the others -0.5, blending src alpha) with an
orthographic camera in MVGL's virtual pixels (`DisplayInfo`: 1920 wide, the height the screen's
aspect gives; `Camera::GetProjectionMatrix` with the half height = half the larger side) and
the painting's scale (+0x260 / +0x264: 1, or 0.5 for `f1`). The 3D figures use the `<map>c`
camera (near x 0.5, at least 0.1; far x 5, at most 5000; the aspect the virtual screen's),
magnified on screen by `2.13 x width / height` x the painting's scale: the painting's height is
the camera's field of view, the two line up by construction.

Draw order (`FldMain`): the background, the midground (half the scroll), the 3D passes (opaque,
alpha-tested, transparent), the foreground. Scrolling (`CalcNewScreenPos`, `SetScrollStop`,
`CalcScrollStop`): the offset = -(the player's NDC in the map camera) x the screen's size x the
scale, clamped to +-(the painting's size x width / 960 - the screen's size), and applied to both
cameras' screen matrices (`GetScreenMatrix`: offset / the screen's size, in NDC) - the player
stays centred until the painting's edge.

Collision (`PhysicsSceneLoader::LoadScene`, `<map>c.phys` the walls and floors, `<map>p.phys`
the exits' trigger volumes): a header (+4 version 1, +8 bodies, +0xc shapes, +0x10 surface names,
then the bodies', the shape pointers' and the names' offsets), bodies of 0x4c bytes (position,
rotation quaternion, friction / restitution, +0x2c the shape, +0x30 contact response - 0 a
trigger, +0x34 the name), shapes (u32 kind: 0 a box - half extents, surface; 1 a sphere - radius,
surface; 2 a triangle mesh - +4 vertices, +8 triangles, +0x24 / +0x28 / +0x2c the indices', the
vertices' and a surface id per triangle's offsets) and 64-byte surface names: the floors name
their footstep sound (`se02`), the walls `MTR_kabe` (0.5 units tall). The player is Bullet
2.81's `btKinematicCharacterController` (`MVGL::Physics::CharacterController`): a sphere of
radius 0.02, step height 0.01, Bullet's defaults otherwise (gravity 29.4, fall speed 55, 45-degree
slopes), the figure drawn 0.02 below its centre. `FldPlayer::Update` turns the stick (the touch's
drag from where it started, over the virtual screen's size, x 0.01: under 0.0002 idle, under
0.0005 walking, else running) into a direction along the ground from the camera and passes
`direction x speed x dt x 60` (walking 0.09, running 0.48) to `setVelocityForTimeInterval` for
0.05 s each of the game's 20 updates a second, and FldMain steps the world once
(`stepSimulation(dt, 1, 1/60)`: one substep of 1/60 s, the rest of the 0.05 s dropped) - so 0.09
and 0.48 units a second, the world's gravity at a third of real time. The engine takes that 1/60 s
of world time as three 1/180 s steps, one each 1/60 s of real time: the same speeds and gravity,
a position for each of 60 frames a second, and the walls met finely (at the game's one step of
0.024 units the sphere can wedge past a wall's plane in a crease: `cr2tool walk` finds 2 runs of
4032 that end beyond a wall, none in three steps).

Exits: `<map>p.phys`'s bodies, each named for the pop point it leads to - the destination map is
the name's first five characters (`FldMap::GetFieldMapNumFromPopPointName`), the arrival a joint of
that map's `<map>p` figure (`FldMain::ChangeAreaExecute`): d0101's `d0103_p1` leads to d0103's
point `d0103_p1`. They are solid (contact response on: the player stops at them) and the contact
(within Bullet's breaking threshold, 0.02 - the controller rests up to a step short of what stops
it) changes the area (`CollisionEventExec`: a sound, a fade). `cr2tool exits`: all 427 exits of
the 217 maps lead to a map that has the point.

Gimmicks (`FldGimmickDataManager::SetData`): `<map>p`'s joints, each placing one by its
position, rotation and scale, and saying by its name what it is - `ragdoll_*` an exit's marker,
`<map>_p<n>` a pop point, `c<nnn>` / `j<nnn>` an NPC (`FldNPC`: the figure `c<nnn>`, idling on
`c<nnn>fn01`), `obj<nnn>` an object (the figure), `eff<nnn>` an effect (the figure),
`tbox` / `fbox` / `obox` a treasure box (`obj011`, `obox` `obj012`), `sprint` (`obj151`), `hjump`,
`mystic`, `iai`, `aim`, `marts` the characters' field abilities, `spot<nn>` / `POP` / `JUMP`
spots, `evt<nnn>` an event box - and with `_scaleNNN` the figure's scale (NNN / 100), `_check`
a thing to tap, `_box` / `_ball` / `_cap` its collision shape: a solid body (NPCs, objects,
boxes) sized by the joint's scale / 100 (a box's half extents; a ball's radius, x; a capsule's
radius, x, and height, y). The scripts name a gimmick by its name's start (5 characters for NPCs and boxes, 7 for objects,
effects and event boxes) and the two digits after `_box` / `_ball` / `_cap`
(`Cr3UtilGetScriptFunctionName`): `c208_box00_scale080` is `c208_00`, `tbox_check_cap07`
`tbox_07`, `evt021_box00` `evt021_00` (its script function `evt021`). Some places hold a character more than once (t0101's `c003_check_cap00`
/ `_cap01` / `_cap09`): alternative spots the story chooses between. Across the maps: 503 pop
points, 427 exit markers, 310 objects, 244 spots, 178 effects, 123 event boxes, 96 sprint spots,
93 NPCs, 130 boxes.

The player (`FldPlayer::CreatePlayer`): `c<id>` with the motions `c<id>` + `fn01` (idle), `ff01`
(walk), `ff02` (run), `fj01..03` (jumps), `fp01..06` (the character's own).

## Shaders (`shad`) and shading

Every material with a non-zero shader key draws with the GLSL ES 1.00 pair
`shaders\<%08x_%08x_%08x_%08x>_vp.shad` / `_fp.shad` (`Figure::SetupMaterials`: the key's words
printed in order; Android 219 keys, the iPad 204 (233 pairs in its archive), the Vita the same
219 as Android, compiled - its files are GXP programs). Key 0 (668 materials) uses MVGL's
built-in `Figure<attributes>` shaders, in the library: texture x vertex colour x DiffuseColor.

The pairs are generated from the key, so they share one vocabulary: about 60 distinct fragment
lines in 103 distinct fragment programs. `engine/src/gfx/shaders/shading.glsl` is all of them as
one, chosen by bits (`shade_flags.h`), the same formulas in the same order:

- colour = ColorSampler (or 1) x DiffuseColor; LightSampler's alpha replaces the texture's
  first; the vertex colour tints rgb or rgba
- lit (130 keys): colour x (light x obscure + ambient) + specular, with light = N.L (clamped) x
  DirLamp01Color, or a CLUT: CLUTSampler at (N.L / 2 + 1/2, N.H) gives the light (rgb) and the
  specular's power (a); ambient flat (AmbientColor) or a hemisphere: mix(GroundColor,
  AmbientColor, (N.SkyDir + 1) / 2); specular = SpecularStrength (or the vertex alpha, or on the
  iPad the normal map's alpha) x Schlick's d / (SpecularPower (1 - d) + d) of d = N.H x the light;
  velvet: light x SurfaceColor + SubColor x VelvetStrength x (smoothstep(-RollOff, 1, N.L) -
  smoothstep(0, 1, N.L)), and with specular a rim (1 - N.V) x FuzzySpecColor x VelvetStrength
- **obscure**: the vertex colour's red, on the light only (and the specular where the key says) -
  the characters' vertex colours are this, ambient occlusion, not a tint
- the normal map bumps N by (rgb - 1/2) x Bumpiness along the tangent and binormal
- fog: mix by clamp(1 - (FogParams.x + depth x FogParams.y)) x FogParams.z; alpha test: the
  texture's alpha < AlphaRef discarded (the material's alpha_func reference)
- texture coordinates + OffsetSet1 + fract(ScrollSpeedSet1 x Time)

Billboards (25 keys - nearly every effect, a few objects and figures): the vertices turned by
ViewInverse's 3x3 (the camera's axes), or - word 2's 0x8, 5 keys - by camPos (the view's third
row with y 0, normalised; y x it; their cross: upright, turning about y alone), then scaled by
MatrixPalette[0]'s columns' lengths and moved to its translation - its rotation and the vertices'
joints unused. The second texture coordinate set (18 keys: Texcoord1 / 1024, + OffsetSet2 or +
fract(ScrollSpeedSet2 x Time)) is the distortion's (16) - NormalSampler read there, the colour's
coordinates + (its rgb - 1/2).xy x DistortionStrength, the bump from the same texel - or an
overlay's (2; the third reads it at the first set): the colour's rgb mixed toward
OverlayColorSampler's by OverlayStrength x its alpha, before DiffuseColor. d0252b's overlay names
no texture: its sampler, never set, reads unit 0 - the colour's own, over itself. Point lights (2
keys - the 40 sky dungeons' `<map>f` figures, obj101 / obj102): per vertex the direction to
PointLamp01Pos and max(radius - distance, 0) / radius, then max(0, N.L) x PointLamp01Color x that
on the light (the direction as interpolated); RenderContaindLights puts the point light's joint's
place and, for its radius, the joint's x scale (d0303's 50; +0x0c's range, 0.5, unused), colour
intensity x colour. FldMap::Draw, on the maps whose +0x15 is set (the d5051f2 sky's), moves it by
the leader's DummyEP04 + 0.1 up: a light carried. Android's / the Vita's key bits (word 0 first):
word 0 - 0x1 DirLamp01, 0x4 PointLamp01, 0x20 hemisphere, 0x40 flat ambient, 0x80 CLUT, 0x100
specular, 0xe00 its strength from the vertex alpha, 0x4000 obscure, 0x8000 the vertex colour
tints, 0x10000 ColorSampler, 0x20000 NormalSampler, 0x80000 overlay; word 1 - 0x4000 velvet,
0x10000 the tint includes alpha, 0x01000000 alpha test, 0x02000000 ScrollSpeedSet1,
0x04000000 ScrollSpeedSet2, 0x10000000 OffsetSet1, 0x20000000 distortion; word 2 - 0x1 OffsetSet2, 0x4
billboard, 0x10 fog, 0x700 LightSampler, 0x8000 skinned (0x10000 two weights). Combinations:
obscure on the specular = 0x100 and 0x4000, the rim = 0x100 and word 1's 0x4000. `cr2tool
shaders`: on Android the decoder agrees with every key's GLSL, bit for bit. The iPad's older
generator lays words 1 and 2 out differently (alpha test 0x00800000, fog 0x8, ...), so its
shaders are read from their GLSL; the Vita's keys, Android's, are decoded.

Name prefixes: `d` dungeon maps (832), `eff` effects (281), `t` towns (148), `obj` objects (72),
`m` monsters (64), `cam_z` cameras (63), `c` characters (42), `v` vistas (36), `gmk` gimmicks.

## Skeletons (`skel`) and animations (`anim`)

MVGL animates with Sony's **Edge Animation** (the PS3 SDK's library, ported: `edgeAnimProcessBlendTree`,
`_edgeAnimEvaluate`, `edgeAnimLocalJointsToWorldMatrices3x4`, ... are in the Android library by
name). The skeletons are the same bytes in all three releases.

| Skeleton | |
|---|---|
| +0x00 | `ES02` (`20SE` as bytes), u32 size |
| +0x10 u16 | joints |
| +0x12 u16 | user channels |
| +0x14 u16 | hierarchy groups |
| +0x18 | self-relative offset: the base pose, a JointTransform per joint (0x30 bytes: rotation quaternion x y z w, translation x y z + pad, scale x y z + pad) |
| +0x20 | self-relative offset: joint name hashes (u32 each) |
| +0x24 | self-relative offset: the user channels' names (u32 each): parameter id << 4 \| component (0x330..0x333: DiffuseColor r g b a) |
| +0x28 | self-relative offset: the user channels' targets (u32 each): a material's hash |
| +0x40 | the hierarchy: groups of 4 (joint, parent) u16 pairs, parents first (groups padded with repeats); parent & 0x7fff >= 0x4000: a root (the figure's root transform); bit 0x8000 clear: scale compensation |

World matrices (`edgeAnimLocalJointsToWorldMatrices3x4`): world = parent world * local, except
that with compensation the child's rotation and scale use the parent's columns divided by the
parent's own local scale (its translation the parent's whole matrix). Skinning matrices
(`Figure::Pose`) = world * the geom's inverse bind, 3x4 row-major, the translation in the 4th
column: `MatrixPalette`'s rows. `tools/skel.py check`: in every release, 73.7% of the joints the
meshes skin to have base pose * inverse bind = identity - the base pose is the bind pose but where
a figure's is posed otherwise (effects and map pieces mostly: theirs is animated).

### Animations

Every animation in the game uses Edge's fixed formats (no packing specs at +0x4c; the decoder for
packed ones - per-channel bit specs: sign, exponent and mantissa widths per component - is in the
library but unused). From `_edgeAnimEvaluate` and the frame search it calls:

| Header | |
|---|---|
| +0x00 | `EA04` (`40AE` as bytes) |
| +0x04 f, +0x08 f | duration (s), sample rate (20 Hz) |
| +0x0c u16 | header size |
| +0x0e u16, +0x10 u16, +0x12 u16 | joints, frames, frame sets |
| +0x16..+0x24 u16 | channel counts: constant rotation, translation, scale, user; animated rotation, translation, scale, user |
| +0x34, +0x38 | self-relative: the frame sets' DMA array (u16, u16 size, u32 offset) and info (u16 start frame, u16 count) |
| +0x3c..+0x48 | self-relative: the constant rotation / translation / scale / user data |
| +0x60 | each channel's joint (u16), in the counts' order; the first table padded to 8 entries, the others to 4 |

A rotation is 6 bytes, the smallest three of a unit quaternion: three 15-bit values, (v - 16383.5)
/ 23169.768, the dropped one's index in the low 2 bits (rebuilt as sqrt(1 - the others' squares));
a translation or scale 3 little-endian floats.

A frame set covers its local frames 0 .. count: 8 u16 sizes (initial rotation, translation, scale,
user; key rotation, translation, scale, user - padded to 4), the initial values (local frame 0),
one keyframe bit per animated channel per frame (frames 1 .. count, MSB first; rotation channels,
then translation, scale, user), the keys (each channel's in turn), and - aligned to 16 - the next
set's header and initial values (local frame count + 1), so a set reaches the next one alone.
**Motions are relative to the figure's base pose**: their constant channels hold 0 /
identity where a joint stays put (19,238 constant translations are 0 where the base's isn't, none
equals the base; 1,078 constant rotations are identity, one equals the base's), so a motion
evaluated over an identity pose is composed onto the base as `edgeAnimBlendJointsRelative` does:
rotation base * delta (Hamilton product), translation base + delta, scale base * delta. Each
figure has a `<figure>.anim` with no channels (892 of them: the base layer). That is
Figure::Pose's blend tree: a figure has five animator slots - slot 0 the base layer, 1 + 2 and
3 + 4 cross-faded (op 0, linear) - and slot 0 is joined to the rest under a relative branch (ops
1 - 6: edgeAnimBlendPose's table). The characters', NPCs', objects' and boxes' motions are bound
to slots 1 / 2 (FldNPC, FldObject, FldBox, the motion managers). An animation alone in slot 0 -
a map's figure (FldMap::ChangeArea), the vista's map, camera and light, an event's scene - is the
tree's one leaf, **absolute**: evaluated over the base pose, its channels replacing the base's
values (edgeAnimProcessBlendTree; the cameras' animations start at their base poses).

User channels are floats (constant ones at +0x48, animated ones' initial values after the
scales', their keys after the scale keys aligned to 4) and absolute: MVGL's are material
parameters (the skeleton's user channel tables) - the UI's fades and colours (`title`'s
DiffuseColor is 0.01 placeholders in the file, the animations set it).

Between the keys P and Q around frame i + f: t = (i - P + f) / (Q - P); rotations slerp on the
short arc (lerp from a dot of 0.999), the rest lerp; channels without keys in the set run from
the initial value to the next set's. `tools/anim.py check`: in all three releases every frame set
is as its sizes say, every driven rotation is unit length, and all 10742..12340 boundaries are
continuous to 1e-8 (the Vita splits its animations into more frame sets).

## Text (`message\*.dat`)

From `EvtTextData` and `EvtWindowMessage::ReadPage`; `tools/msg.py check`: all 874 files in the
three releases, 75,490 of the 75,504 strings UTF-8 (the 14 others, all in `instructions.dat`,
start inside another string's character: offsets into a shared tail, for entries not shown in that
language).

| | |
|---|---|
| header | u32 records' offset (0x10), u32 strings' offset, u32 records, u32 0 |
| records | 0x20 bytes: 8 u32 offsets into the strings, one per language (`Cr3UtilOptionLanguage`); messages count from 1 |
| strings | UTF-8, NUL-terminated |
| trailer | optional (853 files): a u32 voice id per record, then `voic`, u32 count - the last 8 bytes |

A message's commands: `\f` the page ends, `\n` the next line, `\c` the line is centred, `\#`
digit the line's colour (1 red, 2 (0, 0.8, 1), 3 green, 4 (1, 0.8, 0), 5 magenta, 6 white, 7
black; else white), `\d` and 3 digits the speaker's tag (one of 21: `d001`..`d007`, `d101`..`d103`,
`d301`..`d312`, `d407`, `d509`, `d510`, `d999` - its index picks the name from the window's name
data), `\w` digits a wait, `\@` a wait mark (each page gets one at its end), `\*` the message
ends, `\m` a page flag, `\\` a backslash.

The fonts: Android's APK carries TypeBank Gothic Pro Bold and Regular (`assets/00000`, `00001`:
CFF OpenType), the Vita's `CR2/resources/font` the same Bold plus DynaFont faces for Chinese and
Korean (`DFGB_H9.ttc`, `DFT_C5.ttc`, `DFMINBD.TTF`); the iPad's release has none (iOS's own).
Android draws text in Java (`local.mediav.Text2Bitmap`): white, antialiased, the typeface by
index - 0 `00000` (Bold: Japanese), 1 `00001` (Regular: English), 2 the system's (the other
languages; MenuText::SetTextRender: the option's language 0 / 1, else 2) - into a bitmap the
advance's width (plus twice a stroke) by the font's top-to-bottom extent, the baseline (size - 1)
from its top.

## The interface

The menus and windows are figures too (MVGL::Interface::PartsBase), drawn through the camera of
`hud_camera2D` (InterfaceMain::Initialize: interface 11's contained camera): orthographic, aspect
1.5, size 0.48 - a 0.96 x 0.64 space, 3:2, the camera at z 0.881, depth +-2.56 - fitted to the
screen and centred (on Android's 1.87:1 game area: by its height; measured against the reference
video within a pixel). A figure's animators besides its own are relative to its base pose
(`vstWin_talk` moves the name line +0.512). Joints place things: `call_<figure>` another figure,
`type<size>_<align><slot>` text (Cr3UtilSearchTextPosition: size S 28, M 32, else 64 pixels of a
1000-a-unit layout, drawn 4 smaller - SetTextRender; align L, C, R; the line's left edge half the
size left of the joint, its middle on it - SetTextOffsetData).

The vista's message window (VistaConversationWindowMenu): `vstWin` - `GEO_vstWin`, a black band
from y -0.4 to -0.192 (past the foot of any screen), `GEO_nameLine` (`hud_vstMsgTC` / `A8`, the
name's bar) - stretched across screens wider than 3:2 (SetScale aspect / 1.5); its slots `01` the
speaker's name (`typeS_L01`), `11` / `12` a page's lines (`typeM_L11`, `L12`), `21` / `22`
centred ones; `sendCsr_loop` the page cursor at `call_sendCsr_loop`. A line types a character an
update (MenuText::Step: + speed, EvtWindow::UpdatePage's 1 - 0 while skipping); the name at once.

The cut-ins' message window (ConversationWindowMenu, Cr3Event's window 0): `msgWin` - its
open and close animators `msgWin_in` / `msgWin_out` - stretched as the vista's on screens wider
than 3:2, 0.0403125 lower on narrower ones; slots `01` the name (`typeS_L01`), `02`..`04` a page's
three lines (EvtCutinWindow::GetLinePerPage), `05`..`07` centred ones; `msgCsr_loop` its page
cursor. Behind it (depth -0.064 against -0.048) the portraits (CutInWindowMenu): `cutIn`, one
material whose colour map is the face (`cutInNNN_LMM`, 512 x 256, set by EvtFace.SetTexture /
ChangeTexture) and whose DiffuseColor is its brightness (SetColor: 0.5 for who isn't speaking);
five layouts (EvtFace.Create 0..4: interfaces 24, 23, 22, 99, 100), each its io animation -
`cutInR_io`, `cutInLR_io`, `cutInL_io`, `cutInPty_io`, `cutInSgl_io`, 0.2 s, forward on opening
and back from where it is on closing - with the other side's `GEO_face*_L` / `_R` joints hidden.
On screens wider than 3:2 a one-sided layout moves toward its side by the margin over the 3:2
area's width (PositionCorrection: int((W - s 960) / 2) / (s 960), s = min(W / 960, H / 640); the
fit parameter's side - Cr3UtilGetFitXOffset's 3 x 3 table, 1 / 4 / 7 left, 3 / 6 / 9 right).

The narration (NarrationWindowMenu): `narrationPos`, no meshes - ten rows 0.048 apart, columns
`typeM_C01`..`C10` (x 0), `L11`..`L20` (-0.368), `R21`..`R30` (0.368), the cursor at
`call_sendCsr_loop50` - its text over the screen (the scripts fade it to black first). A page's
line i takes slot i (EvtWindow::ShowMessage; with a name tag, i + 1), always in the centred
column (SetStringData's alignment 1); the messages put blank lines first to set where it sits
(the prologue's: four, then the line at C05, the screen's middle). The windows' depths put them
in front of the fade windows' (GetWindowDepth: their z).

## Battles (`data\battledata.dat`)

BtlData's tables. The header: u32 offsets - +0x00 the characters, +0x04 the party, +0x08 the
monsters, +0x0c the couplings (encounters), +0x10 the areas, +0x14 the commands, ..., +0x30 the
strings - then s16 counts from +0x34 (characters 9, monsters 274, couplings 355 - one past the
records before the areas -, areas 63, commands 462, ...). A record's names are u32 offsets into
the strings, one per language (Android / iPad / Vita alike: +0 Japanese, +4 English).

| Record | Size | Fields |
|---|---|---|
| character | 0x98 | +0x08 its battle figure (`j001` Darwin .. `j007`; 9 `c102` Piupiu) |
| monster | 0x110 | +0x10 its figure - four characters - and colour (`m206a`: `m206`, images `m206a01`); +0x1c HP (GetMonsterHP: + 25 a level); +0x53 size (0 small, 1 medium, 2 large, 3 extra large, 4 human-sized) |
| coupling | 0x1c | +0x00 music; +0x02 the map (BtlMapTable, from 1: `d0151b` .. `d1154v`); +0x04 a flag; +0x08 three monsters (0 none) |

`BattleIN(id)` fights coupling `id` (FldEncount::SetBattle). The stage (BtlMainCtrl::StepInit):
the map's figure, its own lights (BtlMap: RenderContaindLights) with the directional one turned
to come from the camera (BtlMain::SetLights); the monsters' sizes pick a pattern
(BtlWork::SetSizePattern over BtlCouplingTable: S, SS, SSS, M, MS, MSS, MM, L, XL, XLXL, Hsolo,
Hpair; m310 its own, `neron`), the pattern where each stands - a joint of `battledpos`
(BtlDposTable: `dPos_sssL`, `dPos_mC`, ...; its names only hashed, no `.name`) whose place and
turn the figure takes (BtlModel::SetPosRot) - the party a pair on `dPos_pPairA` / `B`. They start
on their first motions (BtlMotionTable's 25: 0 `bb01` the stance, 1 `bb02`, `bg01`, `bd01`, ...,
`bz03`; the party bb02 then bb01). The cameras are `BattleCamera_move`'s and `_nomove`'s by name
(`Cam_Start_<pattern>_00` as it opens, `Cam_Getset_<pattern>_00` while commands are chosen,
`Cam_Win_*`, ...: BtlCameraCtrl::Start; near halved - at least 0.1 - far five times, at most
5000). `cr2tool battle <archive> all`: the 340 couplings with monsters set up, everything they use
in the archive, in all three releases.

The battle's interface (BtlInterface): the party's windows (BattleStatusMenu) `pc01win` /
`pc02win`, their `call_` joints placing `btlHpBar_per` / `btlMpBar_per` (posed at HP x 5 / max
seconds, at least 0.07: its `JNT_percent` moves an alpha-0 mask over the lost part, the depth test
hiding the gradient behind it), `btlFace` (images `btlFaceC00<n>` / `btlFaceA00<n>` alpha, n the
character), the labels (StatusId2: `hud_statusAI` by StatusIdParamPackTable, the joint
`call_status_frNN` naming the sprite - 01 HP, 02 MP) and the numbers (`pc0NhpNum` / `mpNum`'s
`call_dotNum_num1`: DotNumId2, five / four digits of DotNumParamPackTable's set 90 from
`hud_dotnum`, right to left, w / 1000 apart); the break gauge (BreakWindowMenu) `brkWin`,
`brkGauge` (posed at (gauge + 4) / 2 seconds), `brkInfo` (`brkInfoEV_io` / `PL` / `EN`); the
command's name in `btlMsg` (`typeM_C01`, `btlMsg_in`); the damage (DamageInfo00) `btlNum` with
`btlNum_fgrN` for N digits, a digit of set 20 (30, 40, 50, 60 for the other kinds:
damageinfodata01) at each `call_dotNum_idNN`, id01 the ones. An interface figure's own animation
(SetParameterDataBase's, alone in slot 0) is absolute; the animators added beside it relative.

## Sound

MVGL::Sound (over OpenSL ES on Android). 37 music tracks (`Sound/bgm01` .. `bgm37`) and two banks,
the effects (`Sound/sndeff`) and the voices (`Sound/voice`).

- **Where.** The Android OBB holds them after its MDB1 archive (which ends at 391 MB): the
  library's `obbtable_pos` / `obbtable_size` (40 u32 each: MVGL::Utilities::Fios::GetObbPos /
  GetObbSize) - 0 the archive, 1..37 the tracks, 0x26 sndeff, 0x27 voice; each piece at a 4 KiB
  boundary. The iPad keeps the same banks as files (`Sound.ios/sound/sndeff.bin`, `voice.bin` -
  byte for byte the OBB's) and its music as AAC in CAF (`bgmNN.caf`).
- **A track** (Android): an `MCA_` header of 0x400 bytes, then Ogg Vorbis - 44.1 kHz stereo,
  46 s .. 5 min. It loops back to its repeat frame at its end (Cr3UtilSoundPlayBGM:
  Cr3SoundSetRepeatFrame from the library's table, a sample; bgm27..29's lie past their ends:
  once). `cr2tool sound` decodes them all.
- **The iPad's tracks**: AAC-LC in CAF, 48 kHz stereo. The chunks are `desc`, `kuki` (an ESDS
  holding the AudioSpecificConfig), `pakt` (the packets' sizes, 7-bit big-endian runs, after the
  valid frames, the priming of 2112 and the remainder) and `data` (after a u32 edit count).
  - The iPad executable has its own repeat table (its Cr3UtilSoundPlayBGM's, `kBgmLoopIpad`). It
    counts AAC packets of 1024 samples, -1 for bgm27..29.
  - Packet p is p x 1024 less the priming in the decoded stream. Its 2000832 for bgm01 is the
    Vita's 2003653: the Vita's content runs some 2,400..3,000 frames later than the iPad's.
- **The Vita's** (`Sound.psp2/sound/`): SXD files (MVGL::Sound's PSP2 build) - `bgmNN.sxd` the
  music; `sndeff.sxd` the effects, `voice.sxd1` the voices' entries over `voice.sxd2` (their
  streams after a 0x60-byte "SXDS" header). Cr3UtilSoundPlaySE(n) / PlayVoice(n) play wave n
  (Cr3SoundPlay_Package): 391 effects and 3249 voices, Android's first 391 / 3249 in order.
  - An SXD is "SXDF", a 0x60-byte header (+0x20 its name), then chunks {char[4] id, u32 size,
    body}: REQD, NAME, WAVE, DATA.
  - WAVE: +4 its entries' count, then a u32 each from +8, the entry that far from it.
  - An entry: +0 flags (bit 1: streamed - +0x24 an offset into the `.sxd2`); +4 u8 the codec
    (0x42 ATRAC9, 0x21 HE-VAG) and +5 the channels; +8 the rate; +0x14 the frames;
    +0x18 / +0x1c the loop's start and end, both inclusive (Android's repeat frame at 48 kHz);
    +0x20 the stream's size; +0x24 its offset from there; +0x2c the ATRAC9 config as a
    little-endian word (0xFE7409F0: 48 kHz stereo, superframes of 1024 samples).
  - The music's tracks run on past their loop's end.
  - The effects: HE-VAG, 16-byte frames of 28 samples (a stereo wave's channels a frame each in
    turn), mostly 44.1 kHz where Android's are 22.05 kHz. The 33 stereo ones' left channel is
    Android's clip. The voices: ATRAC9 mono, 48 kHz (config 0xFE7005F0).
- **A bank** (CPackage): a table of {u32 offset, u32 size} - its length the first offset (394
  effects, 3250 voices) - over headerless signed 16-bit mono PCM at 22050 Hz
  (COpenSLESAudioPlayer::initialize(2, 1, 22050000 mHz)). Cr3UtilSoundPlaySE(n) /
  Cr3UtilSoundPlayVoice(n) play entry n (none at a volume of 0).
- **The game's numbers.** The scripts' Cr3Sound (PlayBGM(track, frames), StopBGM(frames),
  ReleaseBGM, GetState(0: the music's - 2 playing, -2 none), PlaySE(clip), PlaySE_Repeat(clip:
  looping, its slot), StopSE(slot, frames), StopSEAll, PlayVoice(clip), StopVoiceAll) take clips
  and tracks directly; a fade's frames are the game's updates (CSndNode::_FadeVolumeProc, 20 a
  second). A message's voice is its text's trailer id (EvtWindow::PlayVoice as its page starts).
  The battles' (BtlSound) go through tables (tools/gen_sound_table.py -> audio/soundtable.inc):
  PlaySE(n) a clip from a 300-entry s16 table; a voice by Cr3VoiceTable(figure, kind) - 22 figures'
  first four characters ("j001", "m309") x 1000 kinds -, the kind one of three at random from
  battledata's voice records (+0x2c, +0x46 of them: BtlSound::PlayVoiceFromData). The menus' own:
  0xb4 decide, 0xb5 back.
