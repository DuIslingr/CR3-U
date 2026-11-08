# The Vita's controls, and CR3-U's controller

> Inherited from CR2-U with its names changed; it describes CR2-U's engine and game layer, which CR3-U still carries while CR3's own is written.

The iPad's and Android's releases are touch alone. The Vita's (PCSG00498) adds its buttons in its
own code - there are no names for it in the Android library, so this is read from the Vita build's
decompilation (`re/vita`, `tools/re_setup.sh`):

- **The pad** (0x810efbe6): `sceCtrlReadBufferPositive`, the sticks to -1 .. 1 with a 0.25 dead zone;
  sixteen buttons by the table at 0x812fdcac - □ ×  ○ △, L R, -, -, SELECT START, -, -, up right down
  left (SCE_CTRL 0x8000, 0x4000, 0x2000, 0x1000, 0x100, 0x200, 1, 8, 0x10 .. 0x80) - into
  MVGL::Input's device record (+0x404.. the sticks, +0x42c.. the buttons).
- **InputEquipment** (Setup at 0x810ec...: Android's same channels, mpSquare .. mpR2, mpUp .. mpRight,
  the four axes): each channel keeps +0x20 held, +0x21 pressed this update, +0x22 released, +0x28
  repeating (pressed, then after 0.6 s every 0.08 s: 0x810eda7c). On the Vita the four directions
  are the d-pad *or* the left stick pushed all the way (0x810ed90e).
- **The queries** (Android's ButtonType: 0 up, 1 down, 2 left, 3 right, 4 □, 5 ×, 6 △, 7 ○, 8 L1,
  9 L2, 10 R1, 11 R2, 12 START, 13 SELECT) and four masks over them (0x81044ebc pressed, 0x81044fa6
  held, 0x81045090 released, 0x8104517a repeating: bit 1 << (type + 1) - 0x2 up .. 0x100 ○, 0x200
  L, 0x400 R, 0x800 START, 0x1000 SELECT); 0x810452ba / 0x810452fe step a cursor by up / down,
  playing SE 0xb3.

What the game does with them (the 38 functions that call the masks):

| Button | The Vita |
|---|---|
| ○ | confirm; on the field the nearest touch action or balloon (FldMain::CheckTouchEventExec_Tap), a message's page (Cr3UtilGetTouchState ORs it into the touch), a result's or a special scene's tap |
| × | back, cancel |
| □ held | the messages' fast-forward, the staff roll's (VistaConversationWindowMenu, StaffrollMenu, the menus' texts) |
| d-pad / left stick | the menus' cursors (repeating); on the field the stick walks - FldMain::PlayerMove takes its push as a finger's drag |
| L / R | a scroll list's page (ScrollListMenu::PartsUpdate), the shop's character (ShopCutInPiupiuMenu) |
| START | the title's "Please Touch" (with ○) |
| △, SELECT | nothing |

The field's tab (FieldMenuTabMenu, 0x81098da8) is opened by a touch even on the Vita; once open the
d-pad steps round camp, encounters, save, map (the encounters left out while off), the one lit in
diffuse colour (1, 2, 3), ○ choosing it.

CR3-U maps a controller onto that by position (○ the right button, × the bottom one; the options'
Graphics list has Swap ○ and ×), as the keys the menus take: ○ Return, × Backspace, the directions
the arrows with the same repeat, L / R a page (the shop: its character), START the title, □ held
the fast-forward. △ opens the field's tab - the port's own, as a controller has no touch. Where the
touch releases have no cursor of the keyboard's either (the title's buttons and questions, the
shop's first two menus, the party's), the keys and the controller now move a focus over the buttons
in the game's own focused look. `CR3_PAD_SCRIPT` (tests) drives a virtual controller.

## CR3's field by touch or the mouse

CR3's field is walked by dragging, as the iPad's and Android's releases do (Fld2TaskPlayer::
CalcPlayerMoveDir): press on the field and drag - the finger's offset from where it pressed, under
2% of the screen standing, under 5% walking, further running, turned by the camera -, the
controller (`feConMove01` .. `03`) showing once it moves or after half a second. Let go without
having moved and it is a tap (a message's page, the check object at hand). The keys, the d-pad and
the left stick walk as before.

## CR3's camera and field buttons

The camera turns as the iPad's and Android's releases turn it (Fld2TaskTouch::UpdateTouch,
Fld2CameraGom::UpdateControl): with the follow camera, the screen is split by the Options'
Controls - Two-Handed (the default) walks on the left half and turns the camera on the right,
One-Handed walks on the bottom half and turns on the top -, both at once with two fingers. The
camera's touch works as a pad, not a drag: held 20 to 60 pixels (of the screen's 960 x 640) from
where it pressed it turns 0.45 rad a second, further 1.5; across turns it, up and down tilts it
within 20 degrees. Its pad (feConCamera) shows once it moves or after half a second; let go without
moving, it is a tap. With a mouse, the right button held turns the camera the same way, anywhere;
the left button walks. Q / E and R / V still turn and tilt it. The look-down and rail cameras
(houses, the village) are not turned, as in the game. The choice is kept in `system.txt`.

The field's buttons (CampFieldMenu) start folded away behind the top-right button, as each area
begins in the game: tap it (or Tab, the controller's triangle) to open Camp, My Page, the SpShop,
Save, Battle, the encounters' switch and the way back to town; tap it again to close them. Battle
and the encounters' switch are a dungeon's, once the story has given them (Cr3TabMenu.AddTabMenu),
and not while riding. Folded,
only that button and a town's Short Cut take taps.

With the Vita copy as the game a controller works the buttons as the Vita's own code does
(CampFieldMenu, `FUN_811116b4`): △ opens and shuts them (com_008 / com_009), × shuts them; open, a
cursor starts on Camp and the d-pad moves it in the game's order - the way back to town, the town
map, Battle, the encounters, Save, Camp, My Page, the SpShop; ↑ / ← back, ↓ / → on, passing those
not shown (com_002) -, the button under it lit in DiffuseColor (0.5, 1, 1.5), and ○ presses it.
While it is up the d-pad moves the cursor and the left stick still walks. `engine/tools/pad_vita_test.sh`
(ctest `viewer-pad-vita`, with `CR3_VITA_DATA` the Vita copy's folder) drives the virtual controller
through it headless; `--input camp:padleft` (padup, paddown, padright, padok, padback, padtri)
presses for a capture. In a
dungeon the minimap (MiniMapMenu) shows at the top left: the area's map, its warps and objects, the
party's arrow and the camera's view. The game's names for three buttons are one round from their
pictures (feBtnShop is Camp, feBtnCamp My Page, feBtnMypage the SpShop); CR3-U goes by what they
open in the game (Fld2Main's states 6, 9 and 10).

## CR3's save files

The native title-screen save menu has 12 slots across four pages of three.
Use Left/Right, Page Up/Page Down, the on-screen arrows, or controller L/R to
change pages. Up/Down selects a slot and crosses page boundaries; confirm opens
its prompt. C starts Copy, D or Delete starts Delete, and Back cancels. Copy
can select its source and empty destination on different pages.

Completed saves show CLEARED. Opening one offers Continue, New Game Plus and Back.
Click the labeled option or select it with Up/Down and Enter (controller d-pad
and confirm). New Game Plus then asks for an empty destination on any page.
The final confirmation defaults to No; arrows change the choice and Enter confirms
it. Back cancels. There is no global New Game Plus button or shortcut.

## CR3's Vita buttons in the port

With the Vita copy as the game, where the Vita's own code moves a cursor the iPad's and Android's
releases (touch alone) have none of, the controller does as the Vita does:

- **An event's choice** (SelectTwoChoicesMenu / SelectThreeChoicesMenu, `FUN_8123f2f4` /
  `FUN_8123e184`, the cursor `FUN_8128d1ea`): the first option to begin with, ↑ / ↓ the one before /
  after, round, com_002; the option under it its `comMsgC<n>Panel` shown, its text dark (0.227), the
  others' 0.88; ○ chooses it (com_003). The keys 1-3 still choose outright.

- **L / R** (masks 0x200 / 0x400) over CR3's menus do what the Vita's do there, by the port's own
  keys for the same: the gene lab's tabs (GeneSynthesisFutter `FUN_811690b6`), the shop's Buy /
  Sell (ShopMenuMain::Update), the gifts' and the quests' categories and a quest's neighbours
  (GiftSynthesizeListMenu, comListMenu, QuestDitailMenu), the Options' tabs (OptionMenu
  `FUN_81203e54`: not round, com_003), the Gene Index's pages and profiles (PictureBookMenu
  `FUN_81211910`), the Lucky Box's boxes (LuckyBoxMenu::Update).

`CR3_PAD_SCRIPT` also takes `shot=<file.bmp>` (the frame then, as `--capture` writes one) and `quit`.

