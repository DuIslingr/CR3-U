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

- **A lost battle's Continue** (ContinueMenu::Update `0x8113c03a`): ↑ / ↓ over Give Up and Revive
  (com_002), the one under it in its tapped look, ○ pressing it; its question (YNChoiceButton
  `FUN_812818ca`) ↑ / ↓ / ← / → between Yes and No, ○ that, × No; the told's ○ its OK. `--input
  ctn:hold` keeps `auto` from answering it (tests).

- **The right stick turns the field's camera** (Fld2CameraGom::UpdateControl `0x810c4f9a`, the
  stick's axes x -120 as the camera touch's offset - under 20 nothing, under 60 0.45 rad a second,
  further 1.5 -, the tilt within 20 degrees), as the right half's touch does, with the follow camera.
  (The Vita's L, GomBehind `0x810c4484`, swings it behind the player over 0.3 s: not in yet.)

- **START on the title** (TitleMenu, `0x81256cde`: ○ or START) is its tap, as ○ is.

`CR3_PAD_SCRIPT` also takes `rx=` / `ry=` (the right stick), `until=<text>` (the later steps wait for a log line with it, `_` a
space, their times counted from then), `shot=<file.bmp>` (the frame then, as `--capture` writes one) and `quit`.


### Not yet as the Vita's (to do)

The audit of the Vita's button code (the callers of the masks `FUN_8128cd9c` pressed,
`FUN_8128ce86` held, `FUN_8128cf70` released, `FUN_8128d05a` repeating; the list cursor
`FUN_8128d1ea`) found these still different. Addresses are the Vita build's; readings are from
the decompilation, to be checked on screen as each is done.

- **Field**
  - L: GomBehind `0x810c4484` swings the camera behind the player over 0.3 s (yaw to the
    player's facing the short way, the tilt reset); it needs the Gom camera's swing in
    `Cr3GomCamera`.
  - FieldGoodsMenu `0x81144642` / `0x81145a2c`: R steps the focus round the goods' icons
    (no sound), □ uses the one in focus, ○ the check / talk icon. The port's □ is the
    fast-forward there.
- **Battle**
  - comBtnMenu `0x8112bdc4`: com_002 on each move; com_005 on a disabled button; com_004 on
    back; the focused button's `mat_com_menuBtn_02` in (0.5, 1, 1.5), a disabled one
    (0.17, 0.33, 0.5).
  - comListMenu `0x81137862`: ↑ / ↓ stop at the ends (the port's wrap); L / R the
    categories where there are several; com_005 on a row that can't be used.
  - TargetSelectMenu `0x81253dd0`: past either end the cursor reaches the enemy-info part
    (`mat_bt_parts_03` cyan, ○ PersonalInfo 6); with an all-target command ↑ / ↓ toggle
    all / info; com_004 on back.
  - WildCardMenu `0x81277712`: ← / → move between the cards (no wrap, no sound), ○ picks one
    that can be picked, else com_004. The port takes the keys 1 - 5 only.
  - phaseVictory `0x81057fec`: ○ cuts the victory camera short.
  - BattleResultV2Menu `0x8122d28e`: the first ○ runs the tally to its end, a later ○ closes.
- **Messages**
  - VistaWindowMenu `0x8125f47e`: △ toggles the vista's pause (SetVistaPauseButtonEnable,
    not in the port).
  - TutorialMenu `0x8125c57e`: L / R its pages (direction not yet checked).
  - An event's yes / no (YNChoiceButton `FUN_812818ca`): ↑ / ↓ between them with com_002,
    cyan, ○ that one (Yes com_003, No com_004), × No. The port takes Return / Backspace,
    with no cursor.
- **Saves**: SaveFileMenu `0x812384a2`. ↑ from the first slot reaches the Copy / Delete row,
  ← / → between them, ↓ back; ↓ on the last slot stays. The port reaches Copy / Delete by
  the C / D keys alone; its 12 paged slots are its own.
- **Movies** (`0x812d6060`): △ pauses / resumes (sceAvPlayerPause / Resume), □ stops; ○ does
  nothing. The port skips on ○ twice within 3 s.
- **Lunch** (LunchMain `0x810b2bf0`): ↑ / → the next balloon, ↓ / ← the one before, the
  focused one pulsing 1.1 - 1.5 x, ○ running it. The port takes clicks only.
- **Camp**
  - CampTopMenu `0x81118718`:
    - ← / → over the party's panels (com_002); → past the last hands focus to the buttons.
    - Hold ○ on a panel and ← / → to move that member along (the party's order).
    - CampcomBtnMenu `0x811160a2`: ← from the buttons back to the panels; com_005 on a
      disabled button.
  - CampItemMenu `0x8110dd74` / `0x8110ec86`: the count's reels.
    - ← / → between the tens, the ones and Yes / No; ↑ / ↓ turn the reel in focus (cyan).
    - ○ the button in focus, × No.
  - CampEquipFutterMenu `0x81108660`: □ the next character too.
  - FutterMenu `0x81156662`: L / R the Gene / Skill tab (the skill change menu); □ cycles 6
    sort orders.
  - GeneChangeMenu `0x81159e62`:
    - ← to its change button (`mat_gene_change_01` cyan, the cards dimmed 0.6).
    - From there a 2 x 4 grid of the gene's skills with their help.
    - Wearing another's card asks first (0x241); com_005 on one that can't be worn.
    - GeneSkillListMenu `0x81161726` and GeneStatusMenu `0x81166386` are not in the port.
  - The gene lab:
    - GeneSynthesisListMenu `0x8116d6b6`: △ opens the row's gene status; ← back to the left
      panel. The port's △ is Mix.
    - NormalSynthesisMenu `0x8117cf06`: ↑ / ↓ over the two materials and the Mix button
      (cyan).
    - IntensificationSynthesisMenu `0x81178302`: ← / → between its two buttons (cyan).
    - GeneSynthesisFutter `0x811690b6`: □ the next tab, START its OK.
  - GiftSynthesizeListMenu `0x81189cf2`: △ the gene status. GiftSynthesizeMain `0x8118c40a`:
    ← Yes / → No in focus.
  - OptionMenu `0x81203e54`:
    - ↑ / ↓ over the rows, on to the footer (OptionFooterMenu `0x81201416`).
    - The Sound tab: ← / → ±10, repeating; the SE row plays com_003, Voice j002_110. The
      port picks the row by the keys 1 - 3.
    - The focused item's `mat_opt_btn_01` cyan.
  - PictureBookMenu `0x81211910`:
    - A card cursor over 2 pages x 6 (com_002); ○ its profile.
    - ↓ from the bottom row to the 8 tabs (cyan, the others 0.5).
    - The profile's ↑ / ↓ the rarity.
- **Shops, My Page, VS**
  - SShopMenu `0x8124b4e8`: a cursor over Box, Item, VIP.
    - → / ← between Box and Item; ↓ / ↑ between Item and VIP.
    - In focus scale 1 and (1, 1, 1); the others 0.9 and (0.8, 0.8, 0.8).
    - The port's keys 1 - 3 only: Item and VIP can't be reached by a controller.
  - LuckyBoxMenu `0x811ea496`:
    - ← / → over the 3 buy panels; ↑ / ↓ to the check button (cyan); com_002 on each move.
    - The box at 1.35 while the panels have focus.
  - MyPageNewTopMenu `0x811fb75c`: ↑ / ↓ a cursor over its 4 buttons, round, skipping hidden
    ones; the right stick scrolls the log. The port's keys 1 - 4.
  - WorldSelectMenu `0x8127e8e0`: ○ presses List (cyan). The port's Return enters the dungeon;
    its list is on the L key.
  - VsModeMenu `0x81272806`: a cursor over 6 places.
    - Two large buttons, then a 2 x 2 grid; the focused one in its highlight frame.
    - × back one screen.
    - A controller is stuck on the port's second screen, which takes the keys 1 / 2.
  - VsModeListMenu `0x8126adba`: △ picks the row at once; ← out of the list.
  - TournamentDitailMenu `0x81266bb2`: L / R the tournament before / after.
  - PresentGet `0x8121a866`: ← / → between OK and Cancel.
