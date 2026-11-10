# CR3 touch, keyboard and Vita controller controls

The iPad and Android releases use touch. The Vita releases (PCSG00500 and the
PCSG00457 trilogy) add native button handling. The controller mappings here are
based on the saved Vita decompilation in `re/vita`; the follow-up table gives
screen-specific function addresses and the port's behavior.

The Vita pad reader (`FUN_8135689e`, table `0x8160cb70`) feeds InputEquipment.
Its channels distinguish pressed, held, released and repeating input. The game's
masks (`FUN_8128cd9c`, `FUN_8128ce86`, `FUN_8128cf70`, `FUN_8128d05a`) use Up
`0x2`, Down `0x4`, Left `0x8`, Right `0x10`, Square `0x20`, Cross `0x40`,
Triangle `0x80`, Circle `0x100`, L `0x200`, R `0x400`, START `0x800` and
SELECT `0x1000`. Menu directions also accept the left stick at full push;
repeat starts after 0.6 seconds and continues every 0.08 seconds.

CR3-U maps controller buttons by position: Circle is the right face button and
Cross the bottom button. Options → Graphics → Swap ○ and × reverses confirm
and cancel. Circle normally confirms or advances a message, Cross cancels,
Square holds message fast-forward, Triangle opens the field bar, the left stick
walks and the right stick turns the follow camera. Each screen can give these
buttons its own action, as listed below. START also starts the Vita title.

`CR3_PAD_SCRIPT` drives an SDL virtual controller for headless tests. Controller
focus uses the game's existing parts, cyan tint and sounds where available.

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

With any CR3 release (iOS, Android or Vita), a controller works the buttons as the Vita's own code does
(CampFieldMenu, `FUN_811116b4`): △ opens and shuts them (com_008 / com_009), × shuts them; open, a
cursor starts on Camp and the d-pad moves it in the game's order - the way back to town, the town
map, Battle, the encounters, Save, Camp, My Page, the SpShop; ↑ / ← back, ↓ / → on, passing those
not shown (com_002) -, the button under it lit in DiffuseColor (0.5, 1, 1.5), and ○ presses it.
While it is up the d-pad moves the cursor and the left stick still walks. `engine/tools/pad_vita_test.sh`
(ctest `viewer-pad-vita`, with `CR3_VITA_DATA` or `CR3_DATA` set to a CR3 release) drives the virtual controller
through it headless; `--input camp:padleft` (padup, paddown, padright, padok, padback, padtri)
presses for a capture. In a
dungeon the minimap (MiniMapMenu) shows at the top left: the area's map, its warps and objects, the
party's arrow and the camera's view. The game's names for three buttons are one round from their
pictures (feBtnShop is Camp, feBtnCamp My Page, feBtnMypage the SpShop); CR3-U goes by what they
open in the game (Fld2Main's states 6, 9 and 10).

## CR3's save files

The native title-screen save menu has 12 slots across four pages of three.
Use Left/Right, Page Up/Page Down, the on-screen arrows, or controller L/R to
change pages. Up/Down selects a slot (the Vita cursor uses the footer at the upper boundary); confirm opens
its prompt. C starts Copy, D or Delete starts Delete, and Back cancels. Copy
can select its source and empty destination on different pages.

Completed saves show CLEARED. Opening one offers Continue, New Game Plus and Back.
Click the labeled option or select it with Up/Down and Enter (controller d-pad
and confirm). New Game Plus then asks for an empty destination on any page.
The final confirmation defaults to No; arrows change the choice and Enter confirms
it. Back cancels. There is no global New Game Plus button or shortcut.

## CR3's Vita buttons in the port

Across all CR3 releases (iOS, Android and Vita), where the Vita's own code moves a cursor the iPad's and Android's
executables (touch alone) have none of, the controller does as the Vita does:

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
  (L swings it behind the player over 0.3 s, restoring the tilt; fixed and rail cameras keep their own views.)

- **START on the title** (TitleMenu, `0x81256cde`: ○ or START) is its tap, as ○ is.

`CR3_PAD_SCRIPT` also takes `rx=` / `ry=` (the right stick), `until=<text>` (the later steps wait for a log line with it, `_` a
space, their times counted from then), `shot=<file.bmp>` (the frame then, as `--capture` writes one), `hold=<button>`, `release=<button>` and `quit`.


### Controller follow-up

The remaining input gaps from the November 8 audit are implemented and enabled across all CR3
releases (iOS, Android and Vita), while touch/mouse controls remain available alongside them. Confirm/cancel follow
Options → Graphics → Swap ○ and ×. Directions below also accept the left stick at full push.

| Screen | Vita controls now handled by the port | Reference |
|---|---|---|
| Field camera | L swings behind the player's facing over 0.3 seconds by the short route and restores tilt. Manual camera input interrupts it. | GomBehind `0x810c4484` |
| Field goods | R cycles visible goods without a sound; □ uses the focused goods. ○ keeps check/talk. | FieldGoodsMenu `0x81144642`, `0x81145a2c` |
| Battle commands | Directions move focus with com_002; disabled commands play com_005; Back plays com_004. Focus uses cyan, disabled buttons use the darker material. | comBtnMenu `0x8112bdc4` |
| Battle lists | Up/Down stop at the ends. L/R select Items/Gifts; unusable entries play com_005. | comListMenu `0x81137862` |
| Battle targets | Flexible-target commands expose the cyan target-side switch beyond the target list. With all-target commands, Up/Down choose the whole side or its switch. Back plays com_004. | TargetSelectMenu `0x81253dd0`, phaseTargetSelect `0x8105ad76`, OpenTargetSelect `0x81050f20` |
| VS wildcards | Left/Right select cards without wrapping or cursor sound; ○ chooses an available card, otherwise com_004. | WildCardMenu `0x81277712` |
| Battle victory/result | ○ shortens the victory camera. During the result tally, the first ○ completes the tally; a subsequent ○ closes it. | phaseVictory `0x81057fec`, BattleResultV2Menu `0x8122d28e` |
| Vista messages | With the Vista window open, the first △ reveals controls for three seconds; another △ pauses, and △ resumes. SetVistaPauseButtonEnable controls the on-screen button visibility, not controller permission. Scene and sound pause together. | VistaWindowMenu `0x8125f47e` |
| Tutorials | L previous, R next, within page boundaries. The manual traverses tutorial entries and returns to its list at the ends. | TutorialMenu `0x8125c57e`, ChangeChapterSentence `0x8125b324` |
| Event Yes/No | Up/Down select with com_002 and cyan focus; ○ answers (Yes com_003, No com_004), × answers No. | YNChoiceButton `0x812818ca` |
| Save files | Up from the page's first slot reaches Copy/Delete; Left/Right choose between them; Down returns to files. Down at the page's last slot stops. L/R change pages. Questions have directional Yes/No focus. Copy/Delete still require their existing confirmations. | SaveFileMenu `0x812384a2` |
| Movies | △ pauses/resumes picture and sound; □ stops; ○ has no controller action. Touch/mouse keep their Skip control. | `0x812d6060` |
| Lunch | Up/Right next visible balloon, Down/Left previous; ○ runs its conversation. The focused balloon pulses between 1.1 and 1.5 scale. | LunchMain `0x810b2bf0` |
| Camp top | Left/Right move over party panels and back to buttons; hold ○ with Left/Right to reorder the party. Disabled buttons play com_005. | CampTopMenu `0x81118718`, CampcomBtnMenu `0x811160a2` |
| Item quantity | Left/Right focus tens, ones, Yes and No; Up/Down turn the focused reel; ○ chooses the focused button; × cancels. Focused reels use cyan. | CampItemMenu `0x8110dd74`, `0x8110ec86` |
| Equipment | □ selects the next character. | CampEquipFutterMenu `0x81108660` |
| Gene change | L/R select Gene/Skill; □ cycles six native sorts. Left reaches the lock button; ○ toggles lock. Directions focus the skill grid/help; wearing another member's gene asks first. | FutterMenu `0x81156662`, GeneChangeMenu `0x81159e62` |
| Gene lab | △ inspects a row's gene; Left reaches the materials/Mix panel; Up/Down move within it. Enhance buttons use Left/Right. □ cycles footer tabs and START confirms. | GeneSynthesisListMenu `0x8116d6b6`, NormalSynthesisMenu `0x8117cf06`, IntensificationSynthesisMenu `0x81178302`, GeneSynthesisFutter `0x811690b6` |
| Gifts | △ inspects the resulting gene. Left selects Yes, Right selects No. | GiftSynthesizeListMenu `0x81189cf2`, GiftSynthesizeMain `0x8118c40a` |
| Options | Up/Down move through rows and the footer. Sound Left/Right changes volume by 10, with SE/voice samples; focused controls use cyan. | OptionMenu `0x81203e54`, OptionFooterMenu `0x81201416` |
| Gene Index | Directions move over the 2×3 cards, then the eight tabs; ○ opens a profile; profile Up/Down selects rarity. | PictureBookMenu `0x81211910` |
| Premium shop | Spatial focus over Box, Item and VIP; focused scale/color 1/white, others 0.9/0.8. | SShopMenu `0x8124b4e8` |
| Lucky Box | Left/Right choose buy panels; Up/Down switch to the contents button. L/R change boxes while buying. Focus is cyan; the box enlarges while buying. | LuckyBoxMenu `0x811ea496` |
| My Page | Up/Down wrap over visible buttons; right stick scrolls the log. Presents use Left/Right between OK and Cancel. | MyPageNewTopMenu `0x811fb75c`, PresentGet `0x8121a866` |
| World select | ○ opens List with cyan focus. | WorldSelectMenu `0x8127e8e0` |
| VS menus | Directions navigate six positions and their native highlight frames; × returns one screen. In tournament lists, △ selects and Left leaves. Detail L/R selects adjacent tournaments. | VsModeMenu `0x81272806`, VsModeListMenu `0x8126adba`, TournamentDitailMenu `0x81266bb2` |

Two audit interpretations were corrected against the callers. The battle target
button changes target side; it is not an enemy-information screen. The Gene Change
left button toggles the gene's lock; it does not equip it. Tutorial L/R return codes
are interpreted by TutorialMain, giving the usual previous/next directions.

The six gene sorts follow the native comparators: UID ascending, element ascending,
level descending, level ascending, rarity descending, rarity ascending. Non-UID ties
use master ID ascending (`GeneSortStatusListUp 0x8115cfe8`, comparators
`0x814281e8` through `0x8142c278`).

The port reuses its existing gene status/card UI for inspection and its skill grid/help
for Skill focus; the Vita's separate GeneStatus/GeneSkillList window layouts are not
reconstructed here. Lucky Box contents use the port's paged system window. These
controls are usable from the pad; those layouts are not pixel-identical to the Vita.

### Verification

`engine/tools/pad_vita_test.sh` drives an SDL virtual gamepad against any CR3
release (Vita, Android or iOS), headless, with isolated profiles. Its sourced camp/shop/battle files cover
party reorder, reels, gene locks/sorts/tabs, index focus, options, equipment, shop
and VS navigation, battle commands/categories/result, plus the existing field,
choice, Continue, camera and title checks. Additional cases cover save footer,
movie pause/stop, goods and lunch. Set `CR3_VITA_DATA` (or `CR3_DATA`) to a CR3 release
and run CTest's `viewer-pad-vita`, or pass case groups to the script. Failed runs
retain their logs and any nonzero viewer exit is a failure.

`gom-camera` checks recenter timing, shortest-angle movement, manual interruption
and fixed-camera behavior. `battle-target-side` checks actual damage/healing on the
selected side using synthetic tables, including default and fixed-side commands.
`event-reset` checks tutorial boundaries and cleared pause state. `viewer-vista-pause` checks reveal-before-pause, frozen scene time, resume after timeout/window close, and the native visibility setter. Physical
controller/device validation is not claimed; captures inspect the desktop renderer.

Validated November 8, 2026 on Linux, RelWithDebInfo: all 30 virtual-controller
scenarios passed against the local Vita copy. After the final pause and held-confirm
cleanup, all 24 component/regression CTests passed, including graphics recovery and
the new Vista pause test. The normal `engine/build/cr3view` is rebuilt and up to date.
No game data or captures are included in these changes.
