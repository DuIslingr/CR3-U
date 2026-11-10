# CR3 opening and runtime audit

Targeted audit, 2026-11-09, in the spirit of CRO-U's, CR1:U's, and CR2-U's opening and transition
audits: CR3-U's opening flow (`TitleMain` -> `t0108` -> `s000_t01_00101_evt` -> `battle_0001` ->
`s000_t01_00103_evt`), event timeline, fades and cross-fades, audio, message windows, cut-ins,
ending staffroll, field runtime, battle rules and victory choreography, and offline server database
(`DBSystem`, VIP, Battle Mode / VS) checked call by call against the shipped Android 1.1.3 ARM64
library (`libcrx.so`, SHA-256 `43950b32c2c064cf25c6324a75d2b89c0cfc243996dcbb20961ef855bd99bcf3`),
cross-referenced against PS Vita 1.00 (`eboot.elf`) and iOS 1.2.2 (`ChaosRings3_FINAL_ENG.arm64`).

Like CR2, Chaos Rings III runs compiled Squirrel scripts over C++ tasks and UI menus rather than
CR1 / Omega's bytecode VM, but its frame scheduler differs from CR2's 20-Hz loop:
`FrameControlTask::OnUpdate` (`0x009301ec`, Vita `0x810b474e`) ticks at **60 Hz** (`dt >= 1/60 s`)
while `GameSystem + 0x140` sets the logical script/animation rate to **30 Hz** (`1/30 s`). On each
pair of 60-Hz ticks, `Framework::Task::ChildUpdate(fVar5)` runs **twice**, alternating `fVar5 = 0.0`
on odd 60-Hz ticks and `fVar5 = 1/30` on even 60-Hz ticks. Consequently:

- `dt`-scaled subsystems (`WaitProcessor::ProcessWait` via `*(GameSystem + 0x2d0)`, `EvtScene::Update`,
  `CrxSound::ProcBGM`, `PartsBase::Step` on UI `.anim` clips, `\w` wait timers, and `tapWait` guards)
  advance at **30 logical frames per second** (`kTick = 1/30 s` in CR3-U).
- Un-scaled per-`Update` counters run **60 times per second** (`2` calls per `1/30 s` tick), notably
  `ProvisionalMenuText::Step` (`+0x450 += 1.0` per call = **60 chars/s**) and
  `FadeWindowMenu::InterfaceCloseAnimeSet` (`+0x24 = 4` calls = **2 logical 30-Hz ticks**).

## What the opening calls

`TitleMain::OnUpdate` (`0x0096dfa4`): on New Game, `CrxGameData::SetDefault` (`0x00843cb0`) zeroes
flags and clears party/item tables, then `TitleMain` explicitly sets flag `1` (`SetScenarioFlag(1, 1)`)
before reserving the initial area entry (`t0108`, `evt000`).

`t0108` (`evt000` -> `s000_t01_00101_set`): initialises new-game flags, sets up the prologue vista
`t01:s000_t01_00101_evt` (`EvtScene.Init`, `LoadSharedTextData`, `SetAnimator` / `LoadMotion` /
`SetMotion`, `VistaEvent.SetMove`, `VistaEvent.CameraMove`, `VistaEvent.LightMove`, `Cr3Fade.FadeIn`
/ `FadeOut`, narration and conversation messages with `\c`, `\w`, and `\*` codes, `Cr3Sound.PlayBGM`
/ `PlaySE` / `StopSE`), and hands off into the scripted prologue battle (`Coupling` `1`,
`battle_0001.nut`: `Battle_Boot_Base`, `Battle_Start_Base`, `Battle_Victory_Base`) with guest hero
character `6` against monsters `1051` and `1052` on `d1251b`.

After `battle_0001`, `Fld2Main::BranchAfterEvent` (`0x0087e774`) routes through the post-battle
vistas (`s000_t01_00102_evt`, `s000_t01_00103_evt` with `LoadSecondMap` / `ChangeMap`) into the
opening house and coastal town (`t0101..t0108`), `MapNameMenu` area banners, quest-scoped field
gimmicks (`Fld2TaskGimmick`), and the first regular field encounters (`PostEffectGlassFragment`
shatter -> `BtlSysStateBattleIn` -> `BtlSysStateStart` -> `BtlSysStateSelect` -> `BtlSysStateAction`
-> `BtlSysStateVictory` / `BattleResultV2Menu`).

## The original's behaviour, and what changed

### 1. Event timeline, fades, and audio

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| New Game initial flag (`CrxGameData::SetDefault` `0x00843cb0`, `TitleMain::OnUpdate` `0x0096dfa4`) | `SetDefault` zeroes all scenario flags, then `TitleMain` sets flag `1` (`true`) when starting New Game before entering `t0108` | `updateCr3Title` reset `GameState` without setting flag `1` | `game.setFlag(1, true)` set on New Game in `updateCr3Title` |
| Fade pass order (`FadeWindowMenu` pass `0x13..0x15` + `FadeOnRender` `0x0087f56c` vs `BackFadeWindowMenu` pass `0xd..0xf` `0x009689a0`) | `BackFade` draws behind 3D/cut-ins (`0xd..0xf`), while `Cr3Fade.FadeOut` / `FadeIn` (`FadeWindowMenu`) draws at `0x13..0x15` **over** Vista 3D, cut-ins, conversation windows, and camp/shop menus, and **under** Narration (`0x17`), System/Announce (`0x18`), and choice/tutorial overlays | `drawEventUi` drew `ev->fade()` behind portraits and conversation windows, leaving cut-ins and dialogue boxes bright over black fades | `backFade()` drawn behind cut-ins/windows; `fade()` drawn over cut-ins, conversation windows, and camp/shop menus while keeping Narration, System, and choices above it |
| Fade timing & `FadeIn` hold (`GameMain::OnUpdate` `0x0084264c`, `FadeWindowMenu::InterfaceOpenAnimeSet` `0x0087f3ac`, `InterfaceCloseAnimeSet` `0x0087f4a8`, `Update` `0x0087f4fc`) | `InterfaceMain::Update` ticks fades **before** `EvtSystem::Update` resumes scripts; `frames <= 0` snaps immediately (`0.499999f`); `FadeIn` sets `+0x24 = 4` (4 calls at 60 Hz = **2 logical 30-Hz ticks**) before stepping alpha down | Fades stepped on the same tick the script called them, had no 2-tick `FadeIn` hold, and clamped `frames` to `>= 1` | Fades tick before `vm->resume()`, `frames <= 0` snaps immediately, and `FadeIn` (`to < from`) holds `2` 30-Hz ticks (`EventFade::hold = 2`) |
| `VistaEvent.SetMove(model, from, to)` (`EvtModel::SetMove` `0x0085e608`, `EvtScene::Update` `0x0085f114`) | Sets `sceneTime = from / 30`, `moveEnd = max(from, to) / 30`, and `moving = to > from`; `SetMove(m, 300, 0)` parks at frame `300` without moving or snapping to `0`; CR3 has no CR2 `d11_06_0102_evt` special case | `to <= from` left `moveEnd = 0` while advancing `sceneTime`, snapping static `SetMove(m, N, 0)` calls back to frame `0`; carried over CR2's `d11_06` check | `moving = to > from`, `sceneTime = from * kFrame`, `moveEnd = max(from, to) * kFrame`; CR2 leftover check removed |
| `VistaEvent.CameraMove(from, to)` & `LightMove(from, to)` (`VistaCamera::SetFrame` `0x0085f218`, `VistaLight::SetFrame` `0x008488fc`) | End time is `to * (1/30)` (resting on `to`), unlike CR2's `(to + 1) / 20` | Used `(to + 1) * kFrame`, overshooting every camera and light move by 1 frame | End time is `to * kFrame` |
| CR3 BGM / SE / Voice fade durations (`utils::SoundPlayBGM` `0x0092855c`, `SoundStopBGM` `0x00928738`, `CrxSound::ProcBGM` `0x00925f98`) | Script fade counts are in **30-Hz frames** (`seconds = frames / 30.0`), stepped by `ProcBGM` using `1/30 s` updates | `AudioPlayer` divided by CR2's `kUpdates = 20.0f` and scaled BGM fades by `* 2 / 3`, leaving SE and voice fades 50% too slow | `AudioPlayer` uses `kCr3Updates = 30.0f` across CR3 BGM, SE, and voice fades directly |
| `EvtScene.DestroySprite` & `ChangeColor` (`Cr3BindEvtScene` `0x0084e61c`, `Cr3EvtSceneDestroySprite` `0x0084607c`, `EvtModel::ChangeColor` `0x0085e724`) | Shipped CR3 `.nut` scripts call `EvtScene.DestroySprite(id)` (`26` call sites, `0` to `DeleteSprite`) to remove event sprites and `EvtScene.ChangeColor(model, r, g, b)` to tint scene models | Bound `DeleteSprite` only (leaving full-screen event sprites stuck on screen) and left `ChangeColor` unbound | Bound `EvtScene.DestroySprite` alongside `DeleteSprite` and implemented `EvtScene.ChangeColor` (`EventModel::tint` applied in `poseEventFigure`) |
| `Cr3System.CrossFade(frames)` (`Cr3BindSystem::CrossFade` `0x0092fb20`, `PostEffectCrossFade` `0x0096cf98`, `0x0096d93c`) | Captures the pre-transition 3D frame before `WaitFrame(1)` + `VistaEvent.ChangeMap()` (e.g. `s000_t01_00103_evt`) and blends its alpha linearly down over `frames / 30` seconds | Logged as an unimplemented viewer stub and switched vista maps with a hard cut | `EventRuntime::CrossFade` + `grabCrossFade` capture the pre-transition 3D frame into `crossFadeTexture` and blend it out over `frames / 30 s` above 3D and below UI |

### 2. Message windows, cut-ins, choices, map banner, and staffroll

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| Character typing rate & `\w` / `\@` stop boundaries (`ProvisionalMenuText::Step` `0x0088a360`) | Advances `+0x450` by `1.0` on every 60-Hz `ChildUpdate` call (**60 chars/s** = 2 chars per 30-Hz tick), halting immediately at the exact index of any `\w<n>` or `\@` stop | Typed 2 chars per 30-Hz tick (`revealed_ += 2`), which matched 60 chars/s on plain text but stepped from `at - 1` to `at + 1` across odd-index stops, revealing 1 character past `\w` / `\@` | Steps 1 character twice per 30-Hz tick, halting cleanly on every stop boundary while keeping 60 chars/s |
| Conversation window `\c` slot bounds (`MessageWindowMenu::SetStringData` `0x00ad7548`, Vita `0x811ed0ba`) | `comMsgMenu` (`conversationWindow`) has 4 text slots (`01` tag, `02..04` body lines) and ignores `align`; `narrationPos` has 6 slots (`01..03` left, `04..06` centred) | `centred` computed `slot = 1 + 3 + i = 4 + i` unconditionally, indexing past `comMsgMenu`'s 4 slots and rendering **blank body text** whenever `\c` appeared in a cut-in/conversation line | Uses centred slots `1 + linesPerPage + i` only when `slots.size()` provides them; otherwise centres within the regular body slot |
| Per-line `\c` and `\l` alignment (`EvtWindowMessage::ReadPage` `0x008632a0`) | Alignment is tracked per line (`\c` sets centred (`2`), `\l` resets to left (`0`), newline defaults left) | `ParsedPage` stored a single page-wide `bool centred` and ignored `\l` | `TextLine` tracks per-line `centred`, `\l` resets to left, and the glyph-run cache key includes alignment and base colour |
| Window open/close duration & typing hold (`MessageWindowMenu::InterfaceOpenAnimeSet` `0x00ad6c40`, `Update` `0x00ad7140`) | `comMsgMenu` and `sysWin` play a `0.166667 s` (`5` 30-Hz ticks) `_io` slide/fade; `EvtWindow::UpdatePage` waits until `IsOpen()` finishes before typing | `drawWindow` clipped open/close to `0.5 s` (playing static frames past `0.167 s`), and `EventRuntime` typed from tick `0` while `CutIn` / `System` was still opening | Clipped to `0.1667 s` (`5 * kTick`); `EventRuntime::windowOpening()` holds typing on `CutIn` and `System` for those 5 opening ticks |
| Tap during typing & `tapWait` guard (`NarrationMenu::TouchGestureTap` `0x00b4531c`, `MessageWindowMenu::Update` `0x00ad7140`) | Tap while typing reveals up to the next stop without skipping an active `\w<n>` timer; once awaiting tap, taps within `0.10 s` (`3` 30-Hz ticks) are ignored (`tapWait`) | `EventRuntime::touch()` called `advance(false)` with no `tapWait` guard | `touch()` calls `advance(true)`: reveals up to `stops[stop_].at`, never skips `\w<n>`, and enforces the `0.10 s` (`3 * kTick`) `tapWait` guard |
| Cut-in portraits & centre cut-in (`CutInWindowMenu` `0x0096a0c0`, `CutInCenterMenu` `0x0096a1c4`) | `cutIn*_io` clips run `0.20 s` (`6` ticks), portrait alpha fades in over `8/30 s` and out over `4/30 s`, and `CutInCenterMenu` fades its frame out on close | Cut-in alpha snapped with the slide and `drawCr3CentreCutIn` kept full frame opacity during close | `0.20 s` clip with `8/30 s` fade-in and `4/30 s` fade-out alpha; `drawCr3CentreCutIn` fades `c.menu` on close |
| Yes/No choice close (`YNChoiceButton` / `SelectTwoChoicesMenu::TouchSimpleRelease` `0x00b7a52c`) | `HideYesNoButton` plays the `0.10 s` (`3` ticks) reverse `combtn_io` close animation | Disappeared in 0 frames on `HideYesNoButton` | `yesNoClosing_` plays the `0.10 s` (`3 * kTick`) reverse close animation in `drawYesNo` |
| `MapNameMenu` floor digits (`MapNameMenu::Setup` `0x00b1eb24`) | Draws floor-level digits only when `level > 0`; towns and level-0 areas omit the level number, and `mapNameLevel` resets when the banner finishes | Could show stale/zero floor digits if `mapNameLevel_` lingered | `drawMapName` omits digits when `mapNameLevel() == 0` and `EventRuntime` resets `mapNameLevel_ = 0` when the banner ends |
| `InterfaceWindow.*StaffrollMenu` (`StaffrollMenu` `0x00b3e0e0..0x00b3ed78`, `Cr3BindInterfaceWindow` `0x008f2e9c..0x008f3364`) | Scrolls pairs of `message/Staff.dat` rows (`+0` left, `+800` right; `"graphiclogotypea"` / `"b"` -> `ttlendrogo_fr00`) upward at 60 Hz (`speed` / `fastSpeed`), emitting 1 row in `Initialize` before `SetDisplaySectionStaffrollMenu` sets `baseRow = start - 2` (1-based first row `start - 1`) for `endSteps = end - start + 0x1b` steps alongside `ttlendmenu` cut-in slides | All 9 `StaffrollMenu` natives were unimplemented stubs | Implemented all 9 `StaffrollMenu` natives in `EventRuntime` and two-column text, `ttlendrogo_fr00` logos, and `ttlendmenu` cut-ins in `drawStaffroll` |

### 3. Field runtime, cameras, gimmicks, boxes, and transitions

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| Analog stick walk/run & fixed speeds (`Fld2TaskPlayer::UpdateMovePad` `0x0087b878`, Vita `0x810d5d48`) | Deadzone `0.05`: any stick deflection `> 0.05` runs at fixed `4.8 m/s` (walk `1.2 m/s` is used only on tiny deflection or scripted walk), never scaling velocity by stick magnitude | Scaled speed continuously by stick tilt with a high run threshold, making full/partial tilt walk when the original ran | Stick threshold `0.05` with fixed `1.2` / `4.8 m/s` speeds (`cr3field.hpp` / `cr3field.cpp`) |
| Follow camera (`Fld2CameraGom::Update` `0x00867ca0`, `UpdateControl` `0x00869194`, Vita `0x810c4f9a`) | Tracks player position without auto-rotating yaw behind movement; yaw changes only on touch drag / right stick (`1.5 rad/s` yaw, `1.125 rad/s` pitch within `+-20 deg`) or `L` behind-player reset | Auto-rotated camera yaw behind character movement | Free follow camera matching `Fld2CameraGom`; right stick and `L` reset preserved (`gom-camera` & `pad_vita_test.sh`) |
| Dungeon camera selection (`Fld2Main::Setup` `0x0087d89c`, `Fld2TaskMap::GetDungeonNo` `0x0086b870`, `Fld2CameraLookDown::SetupCamera` `0x0086a81c`) | Maps all 24 `Field_` prefixes (`t01..t05` = `0..4`, `d01..d18` = `5..22`, `e00` = `23`); dungeons with `GetFieldCameraPositionLD` entries (`d15..d18`, `e00`) use `Fld2CameraLookDown` | `cr3DungeonNum` only recognised `d01..d07`, leaving later dungeons (`d1501`, `e0001`, etc.) on the wrong camera | `cr3DungeonNum` / `cr3DungeonPrefix` cover all 24 `Field_` entries (`0..23`), engaging `Fld2CameraLookDown` on every configured dungeon |
| Quest-scoped gimmicks, `"ragdoll_chk"`, & live reload (`Fld2TaskGimmick::GetFieldParamByName` `0x0086bf84`, `SetGimmick` `0x0086c210`) | Matches quest-specific `Field` table rows while the active quest matches before falling back to quest `0`, loads `"ragdoll_chk"` gimmicks, honours per-row `checkRange`, and reloads when `Cr3GimmickData.Setup` is called | Ignored quest-scoped `Field` rows, skipped `"ragdoll_chk"`, used default check radius, and left `Cr3GimmickData.Setup` as a no-op | `setupAreaGimmicks` resolves quest-scoped rows, loads `"ragdoll_chk"`, applies custom `checkRange`, and re-runs on `game.gimmickSetupRequested` |
| Check-target priority (`Fld2BalloonManager::CheckBalloon` `0x0087aee8`, `Fld2NPC::Check` `0x0087bad8`) | Selects the **closest** eligible check target within range (`bestDist`) | Took the first matching check object in iteration order | Selects minimum squared distance (`bestCheckDist`) |
| Treasure-box tier roll (`Fld2TBoxList::Drawing` `0x0087b378`) | Iterates `GBOX_PROBS` tiers in order and **breaks** as soon as a tier fails its roll (`else break;`) | Continued testing higher tiers after a lower tier failed | Breaks on first failed tier roll (`else break;`) |
| Post-event branching (`Fld2Main::BranchAfterEvent` `0x0087e774`) | Checks `popPoint` / `changeDungeon` **before** vista/battle/title transitions when an area event finishes | Checked battle/vista before `popPoint` / `changeDungeon` | Checks `popPoint` and `changeDungeon` first in `afterAreaScript` |
| Field turn interpolation & skip speed (`Fld2Main::Update` `0x009ed044`, `stepAreaTurns`) | Advances actor turns on `1/30 s` steps and accelerates field event updates by `5x` while skip/fast-forward is held | Stepped field turns at `1/20 s` and did not `5x`-accelerate `updateArea` on `skipHeld()` | `stepAreaTurns` uses `1/30 s` and `updateArea` scales `dt * 5` while `skipHeld()` |
| Encounter BGM fade (`Fld2TaskEncount` -> `BtlMain` `0x009ed198`) | Stops field BGM over `30` frames (`1.0 s`) when an encounter triggers | Left field BGM uncut until battle BGM replaced it | `startEncounter` calls `audio.stopBgm(30)` |

### 4. Battle rules, turn order, glass shatter, damage UI, escape, and victory

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| `IsBootCharacter` solo coupling (`BtlSystem::createParty` `0x007d903c`, `calculateBasicDamage` `0x007e18a0`) | When `MbCouplingInfo + 0x44` bit `3` (`0x8`) is set, spawns solo `Char` `7` instead of `s_BootParam` and multiplies basic damage by `10` | Read `Coupling` after party creation and ignored `couplingFlags & 8` in both party setup and damage | `Cr3Battle::setup` reads `Coupling` first and uses `{7}` when `(couplingFlags & 8) != 0`; `Cr3BattleRules` sets `bootCharacter` and multiplies basic damage by `10` |
| Glass shatter presets (`PostEffectGlassFragment` `0x00950600`, `Cr3BindSystem::SetGlassEffectPattern` `0x00861f04`) | Supports presets `0` (`RadialCenter`, 230 pts), `1` (`Drop` peel, 200 pts, default), and `2` (`RadialSwirl`, 400 pts), set per encounter via `Cr3System.SetGlassEffectPattern` and reset to `1` after build | Single fixed 240-point Voronoi shatter; `SetGlassEffectPattern` unbound | Bound `Cr3System.SetGlassEffectPattern`, implemented presets `0`, `1`, and `2` in `Cr3Glass::build`, and resets `game.glassPattern = 1` after each shatter |
| First-strike / surprise skills (`BtlSystem` start-mode roll `0x007d94f4`) | Sums start-initiative passive skills across **player** units only | Iterated all `fighters` including enemies | Filters to `if (!f.player) continue;` in `rollCr3StartMode` |
| Battle-in skip & `GetSet` camera end (`BtlSysStateBattleIn` / `BtlSysStateStart` `0x009d4be8`, `0x009d4c24`) | Tapping during glass shatter skips straight into command selection, and `GetSet` ends as soon as `camT >= cr3Cam.end` | Ignored tap during glass shatter and padded `GetSet` by `+ 0.5f` s | Tap sets `cr3BattleInSkipped`, and `GetSet` ends at `camT >= cr3Cam.end` |
| Turn-order buckets & speed cost (`BtlCommandManager` + `BtlCommandList::Sort` `0x007e54cc`) | Splits actions into bucket `0` (normal), bucket `2` (`priority == -8`), and bucket `5` (`priority == -9`, unsorted); shuffles then stable-sorts buckets `0` and `2` by `priority` descending (player `Guard` uses command `51`'s `priority = 2`), then `speedOf(f) - weight + speedCost` descending (ascending under `reverseSpeed` / status kind `11`) | Sorted all commands together by raw `speedOf(f) - weight` only, ignoring `priority`, `speedCost`, and `reverseSpeed` | Reads `Command` columns `25` (`priority`) and `26` (`speedCost`), sorts buckets `0` / `2` / `5` with `Guard` `+2` and `reverseSpeed` inversion |
| Break-Advantage passive skills (`BtlSkill` `0x00983a44`) | Passive skills read columns `17..20` (`+0x44`) when the unit's side holds Break Advantage, else `12..15` (`+0x30`) | `skillConstant` / `skillConstants` always read columns `12..15` | Reads `base = advantage(f.player) ? 17 : 12` |
| Hit accuracy, once-per-target `crit`/`down`, & per-hit `chain` (`calculateHit` `0x007e0f3c`, `calculateCriticalHit` `0x007e0c28`, `BtlDamageTarget::Calculate` `0x00983a44`) | Accuracy includes monster `+13` taken-hit, attacker skill kind `17`, target skill kind `18`, target status kind `49`, and attacker kind `122` sure-hit; `crit` (with monster `+6`, skill `19`, status/skill `139`) and `down` are rolled **once** per `(attacker, target)` before the hit loop while accuracy, spread, and `++t.chain` (`c.chain == 3`, `+0.1%` per landed hit, reset on side change) run **per hit** | Missing monster `+13`/`+6` & skill `17`/`18`/`19`/`139` modifiers; re-rolled `crit`/`down` on every hit of multi-hit attacks and lacked `t.chain` escalation | `rollCritAndDown` runs once before the hit loop; `hits()`, spread, and `++t.chain` run per hit; `chain` resets on side change |
| Damage popup colours (`BtlDamageUI::getDamageColor` `0x009a0968`, `drawDamage` `0x009a0c44`, Vita `0x813f2e6c`) | Indexes `btnum_fr00.anim` at frame `(color * 10 + digit) / 30` with `color`: `0` normal, `1` elemental weak (`rate > 1`), `2` resist (`rate < 1`), `3` HP heal, `4` MP heal | Always drew colour `0` (white) except heals | `Cr3Hit::colors` records `0..4` per hit and `Cr3Popup` renders `(color * 10 + digit) / 30` |
| Escape guarantee & command backout (`BtlSystem::Escape` `0x007d94f4`, `BtlSysStateSelect`) | Escape succeeds automatically with Break Advantage or passive skill kind `86` (`0x56`); cancelling from fighter `N` returns to fighter `N - 1` (refunding any queued item) or to `GetSet` / `Escape` without incrementing `cr3Turn` | Rolled 50% escape even with Break Advantage / skill `86`; cancel could not step back across party members or refund items cleanly | `chooseCr3Escape` guarantees escape on `advantage(true)` or skill `86`; `Backspace` / cancel steps back to the previous chooser, refunds `cr3TurnItems`, and uses `beginCr3Turn(false)` |
| Victory pose & result BGM fade (`BtlUnitList::PlayVictory` `0x009d6ed0`, `BattleResultV2Menu` `0x00a65528`) | Living party fighters play motion slot `2` (`bt_win`) on victory, and battle BGM fades out over `30` frames when entering the result screen | Stayed in idle motion and cut BGM abruptly | Plays `cr3MotionOf(2)` on living party units on victory and calls `audio.stopBgm(30)` in `enterCr3Result` |
| `GetMpCost` & `GetHpCost` (`BtlUnit::GetMpCost` `0x007dc840`, `GetHpCost` `0x007dcafc`) | Supports `mpKind` / `hpKind` `0` (flat), `1` (`%` current, min `1`), `2` (`%` max, min `1`), and `3` (`mpKind == 3` applies two-step `double` truncation `max(int(cost * consumption), 1)` then `max(int(step * less), 1)` with skills `74` & `136`), plus integer `mpCostPercent` (`129`) and `mpCostScale` | Treated all MP/HP costs as flat integers with single-pass float scaling | Reads `hpKind`, `hpCost`, `mpKind`, and `mpCost` from `Command` (`+0xac..+0xb8`) and implements modes `0..3` with two-step `double` truncation |
| Extended `BtlCalculator` formulas (`0x007df13c..0x007e3b38`) | `action == 14` scales hit count by `speed / targetSpeed`; heals `5` (power + skill `78`, `bootCharacter` `10x`, spread), `6` (constant + `chain == 19` Chocolat bonus skill `77`), `7` (`%` max HP), `15` (living party count), `16`/`17` (constant/`%` MP heal, colour `4`), `18` (`%` HP+MP double popup); offensive `8` (`%` current HP less skill `89`), `12` (status count + 1), `13` (HP consumption), `20` (MP damage, colour `4`), `22` (MP release), `23` (magic by speed), `24` (reduce HP to constant without KO) | Handled basic damage (`1..4`), partial heals (`5..7`), fixed/gravity (`9`), and gene drive (`19`) only; MP heals/damage and `action == 14` were missing | Implemented all audited `BtlCalculator` formulas, `action == 14`, `Cr3Hit::mpHits` colour-`4` MP popups, and `damage == 18` dual HP+MP popups |
| Revive / status target rules & `OpenTargetSelect` (`BtlCommand::CheckTarget` `0x007dd71c`, `BtlInterfaceCtrl::OpenTargetSelect` `0x009a8f10`) | Enforces `Command` `requiredEffects` (`+0x80`) and `forbiddenEffects` (`+0x90`), allows targeting KO'd allies when `revives()` or `requiredEffects` includes `1`, `continue`s past empty-target actions without aborting remaining actors' turns, shares identical target filtering across touch and pad, and shows MP in `btTgtPanel` on MP heals | Ignored `requiredEffects` / `forbiddenEffects`, diverged between touch and pad target filtering, aborted remaining turn actions on an empty target set, and always showed HP in `btTgtPanel` | Enforces `requiredEffects` / `forbiddenEffects` and revive KO targeting, `continue`s past empty-target actions until `outcome() != 0`, unifies touch/pad `OpenTargetSelect`, and switches `btTgtPanel` to MP on `mpHeal()` |
| Solo finisher victory camera (`BtlSysStateVictory::Enter` `0x009d6e4c`, `AnimDirection` `(modelId, 2)`) | Selects the last living party attacker, hides all non-finisher party/enemy units and clears lingering popups during the victory close-up, and plays that model's `Cam_b03_*` clip from `model.mbe` for its exact clip duration | Kept non-finisher units and lingering popups visible and did not load `(modelId, 2)` `Cam_b03_*` clips | `Cr3BattleTables::victoryCamera(modelId)` loads `(modelId, 2)` from `model.mbe`, hides non-victor units and clears popups during `cr3VictoryIn`, and runs for the `Cam_b03_*` clip duration |

### 5. Offline server database (`DBSystem`), VIP, and Battle Mode (`VS`)

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| Immediate VIP login bonus (`DBSystem::SM_BuyVip` `0x007fec54`, `SM_BuyVipTrial` `0x007feec4`) | Grants the day's VIP login gift-box bonus immediately upon purchasing VIP or starting the VIP trial | Granted VIP login bonus only on subsequent daily login checks | `buyVIP` and `buyVIPTrial` call `sendVipLoginBonus(s)` immediately |
| VS tournament `prize_gold` & `GOLD_MAX` (`DBSystem::SM_EndVSTournament` `0x007fd5d4`, `GOLD_MAX = 9999999`) | Awards `vstournaments.prize_gold` on tournament completion and clamps held gold to `9,999,999` across shops, gene sales, quest prizes, VS tournaments, and battle results | Omitted `prize_gold` in `endVSTournament` and left gold unclamped on several award paths | `endVSTournament` awards `t.prizeGold`, and `addGold` / `enterCr3Result` clamp to `GOLD_MAX` (`9,999,999`) |
| VS season score thresholds (`DBSystem::CheckVSSeasonScore` `0x007ff670`) | Initializes `seasonCount` from `vsseasons.scores[0]` (`100`) and steps the per-level threshold to `scores[1]` (`250`) once `seasonLevel >= count` (`10`) | Initialized `seasonCount` from `count` (`10`) instead of `scores[0]` (`100`) and never stepped to `scores[1]` | Persists `Cr3Vs::seasonLevel`, initializes `seasonCount` from `scores[0]`, and advances to `scores[1]` once `seasonLevel >= count` |

### 6. Round 3 parity audit (events, battle rules & HUD, camp, gene lab, field & minimap, offline DB)

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| Announcement windows `8..10`, `TutorialMenu` `20`, & `NarrationMenu` (`FieldAnnouncementMenu::SetParamNumber` `0x00a65664`, `SetContentTutorialMenuNum` `0x00b526cc`, `OnFadeNarretionMenu` `0x00aebb04`) | Windows `2..4`, `6`, and `8..10` default lines to centred while honouring `\l`; `8..10` render `comannmenu` at Y-offsets `0`, `-2.1`, `-4.2` independent of `TutorialMenu`; `NarrationMenu` (`commsgvmenu` without frame) supports `OnFadeNarretionMenu` above `Cr3Fade`; `MapNameMenu` tilts `feNameList`/`feNameNum` by `30.37 deg` (`0x00ad5bd0`); `StaffrollMenu` shifts `-0.85` on 16:9 | Collided `TutorialMenu = 8` with `EvtWindow.Open(8)` (`AnnounceTop`), left-aligned un-tagged lines in announcement/narration windows, left `SetContentTutorialMenuNum` & `OnFadeNarretionMenu` unbound, and drew `MapNameMenu` digits untitled | Separated `AnnounceTop = 8 .. AnnounceBot = 10` from `TutorialMenu = 20`, added `defaultCentredWindow`, bound `SetContentTutorialMenuNum` & `OnFadeNarretionMenu`, and matched `NarrationMenu`, `FieldAnnouncementMenu`, `vistaFastButton`, `MapNameMenu` `30.37 deg` tilt, and `StaffrollMenu` 16:9 shift |
| Textured `EventSprite`, `EvtModel` attachments, & sub-viewport (`EvtSprite::Build` `0x00f01134`, `Cr3EvtSceneLoadAttachmentMotion` `0x00844914`) | `EvtScene.CreateSpriteWithTexture` / `SetSpritePosition` / `SetSpriteSize` / `SetSpriteScale` / `SetSpriteColorAnimation` draw `1920x1920`-cover `.img` sprites; `EvtScene` `ChangeVariation`, `ChangeArmor`, `SetWeaponVisible`, `LoadWeapon`, and `CreateAttachment`..`SetAttachmentMotion` attach props to bone sockets; `Cr3System.SetViewportEnable/Position/Size` scopes the 3D scene viewport | Left textured sprite, attachment, variation/temporary-armor, and sub-viewport natives as no-op stubs | Implemented textured `EventSprite` rendering (with bottom-up `.img` UV orientation verified on `s000_t01_00201_evt`), `EventModel` bone-socket attachments & variations, and `GameState` 3D sub-viewport |
| Unison single execution & `bucket0` gate, element shift & `GetSameElementCorrection` (`0x007dc7f4`, `0x009b25a4`, `0x009b0648`, `0x0097b9b0`) | Initializes player `element` from equipped Gene (`initializeGene` `0x009b25a4`); restricts Unison to `bucket0` and executes **one** combined action scaled by `coopScale_[n]`; skill `103` adds `+c%` on same element and `-c%` on mismatch; non-item elemental commands shift caster & Unison partner elements after damage unless Fix Element (`102`) holds | Left player `element = 0`, allowed priority `-8` commands into Unison, scaled Unison per partner loop, and omitted skill `103`, `102`, and post-action element shifting | Initializes `f.element = gi.i(12)`, gates Unison to `bucket0` as a single scaled action, applies `GetSameElementCorrection` (`103`), and runs post-damage `ChangeElementToCalculation` respecting `102` |
| Status stacking, group priority, & specific status immunity (`CanCancel` `0x009b874c`, `GetAttackMultipliers` `0x009b1574` / `0x009b164c`, `HasInvalidStatusEffectToCalculation` `0x009b6bd4` / `0x009afc28`) | Parses full 34-column `StatusEffect` (`group`, `priority`, `maxAccumulation`, `CanCancel` bit `0x10`, plus skill `84` duration boost); odd kinds `37/39/41/43` and `45/46` sum `stacks * constant / 100` added to even-kind multipliers; status/skill kind `68` (`blocked == id`) and `resist > 256` block even `flags & 8` guaranteed ailments | Read 23-column `StatusEffect` without group priority or stack counts, omitted odd stat multipliers `37..43` and passive ATK `36`, and did not check `blocked == id` before `flags & 8` | Parses 34-column `StatusEffect`, implements group cancellation/override and `FUN_009b164c` stack deltas across stats & `endTurn` regen/slip, and enforces per-ID `68` / `resist > 256` immunity before `flags & 8` |
| CR3 skill IDs, Formula `12` `extraType 18`, mid-turn MP abort, Break `extraType 13` / skill `70`, & `BtlTotalDamageUI` (`0x009af9cc`, `0x007e1c8c`, `0x009a4744`, `0x00867d88`) | Uses CR3 skill/status IDs (`35` Gravity resist, `81`/`82` hit/evade, `44` crit, `83` status land, `85` party drop boost, `104`/`105` hit counts); Formula `12` `extraType 18` adds `+20` power per matching target status; aborts mid-turn (`-2`) if drained below `mpCost`; supports `extraType 13` Break force-set & skill `70` Break Reset; renders `BtlTotalDamageUI` (`bttopanel`), `BattleInColor`, Gene level-up `RecoverBonusMP`, and Ace Coupling level-up | Used several CR2 skill IDs (`89`, `17..19`, `49`), missed `extraType 18`/`13`, skill `70`/`85`/`104`/`105`, mid-turn MP abort, `BtlTotalDamageUI`, Gene `RecoverBonusMP`, and Ace Coupling level-up | Aligned all CR3 skill/status IDs and formulas, added mid-turn `HaveEnoughMp` / `47` / `67` checks, rendered `BtlTotalDamageUI` (`bttoposition_io` -> `bttopanel`, `bttohp01/02_per`, `bttonum`, `bthitnum`), and wired `BattleInColor`, `RecoverBonusMP`, and Ace Coupling progression |
| Camp Equipment/Items, Gene Lab lock/worn filter, Field encounters & Minimap, and `DBSystem` presents/daily items (`0x00a9388c`, `0x00a1da3c`, `0x00a2c2f0`, `0x00886a38`, `0x008677d8`, `0x00ad838c`, `0x00ad8654`, `0x0081fb78`, `0x0082e48c`) | Blocks locked/worn genes as Combine bases (`0x00a9388c`); draws `CampEquip` Step 0 `comlistcategorypanel` (`"Equipment"`), Step 1 `Weapons`/`Armor`/`Accessory` tabs, and full-width 16:9 `comftrequipmenu`; unwraps key gifts `600065`/`600067`/`600068`, party-wide MP items, and Command/Skill cards; averages equipped Gene levels (`CalcAveLevel`), rolls Ace encounters (`!d01`), filters minimap boxes/gimmicks/`d09` layers; fixes Oz-Coin presents (`COIN_MAX`), partial-stack daily items (`-> 99`), `mergeGene` flag `34`, and `CheckNewGene` timing | Allowed locked/worn Combine bases; omitted `CampEquip` category headers and left a 16:9 footer gap; lacked key-gift unwrapping & card slot learning; used character levels for encounters; showed unopened/hidden minimap icons; inserted bogus item `-2` on Oz-Coin presents and blocked partial-stack daily items | Enforces Gene Lab Combine base rules, `CampEquip` headers & 16:9 footer stretch, camp gift/MP/card item flows, `CalcAveLevel` & Ace encounters, minimap filtering, Oz-Coin/`COIN_MAX` presents, partial-stack `getDailyItem`, `mergeGene` flag `34`, and `Invoke(1)` `cr3GeneBook` sync |

### 7. Round 4 parity audit (screen quake, field-map quake no-ops, unbound natives)

Round 4 re-ran the full `4127`-event replay with `CR3_STUBS=1` and looked past the `EvtScene` /
`EvtWindow` / `Cr3Sound` / `VistaEvent` namespaces that Rounds 1-3 cleared. Some natives the replay
reports as stubs (`Cr3FieldMapCamera.SetScriptCamera*`, `Cr3System.StartQuake` in fields and
vistas) are bound by the viewer (`bindAreaNatives`, `bindQuakeNatives`), which `cr3tool` doesn't
load; the rest are listed under *Not settled*. The quake was checked against walkthrough Part 3
(`Q-i3K6JUxqY`, 720p): the boss death at `14:24..14:28` is `BattleDeathDirection`'s
`StartQuake(10, 0.2, 1.0, 143)`, and frame-to-frame phase correlation of its static left band gives
alternating `+11.6` / `-8.2` px deltas at the end (`1.7` px/frame camera drift), an amplitude of
`4.95` px of `720`.

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| `Cr3System.StartQuake` magnitude (`QuakeControlTask::Render` `0x009440f8`, `RenderLayerScreen::Offset` `0x00f168a4`, `MVGL::Draw::Camera::GetScreenMatrix` `0x00b8f184`) | Writes `width * (from + (to - from) * time / |seconds|)` to the 3D layers' (`5..9`, `0`) `RenderLayerScreen` y offset (`+0x88`, the camera's `+0x24c`); the screen matrix (`clip = Screen * Proj * View`) moves clip space by that **/ `DisplayInfo` height**, i.e. `offset / 2` device pixels; measured on the playthrough as `10 / 727` NDC | `width * k * 2 / 640` NDC, **2.25x** the playthrough's shake | `width * k / 720` NDC (`kQuakeDisplayHeight`, the playthrough's display) |
| Quake time, end, and held quakes (`utils::StartQuake` `0x0092d33c`, `QuakeControlTask::Start` `0x00944188`, `OnUpdate` `0x00944084`, `Stop` `0x009441ac`) | `seconds = frames * GameSystem+0x148` (`1/30`), negative held at `to` until `StopQuake` (time clamped to `|seconds|`, no end); one root task stepped by every `ChildUpdate`, whatever the scene | `max(frames, 1) / 30` (a negative count ended after one frame); stepped separately by the field (`dt * 5` while skip was held) and `stepEvent`, not at all in battle | `Viewer::startQuake` / `stepQuake` in `Viewer::advance` (the root task's place): signed seconds, `|seconds|` clamp, real-time `dt` |
| Shake parity (`QuakeControlTask::Render` sets `+0xd0`; `OnUpdate` flips `+0xcc` only when it's set) | The way flips at the first 60-Hz update **after a drawn frame**; the first frame after `Start` goes down (`-width * k`) | Flipped inside `applyQuake`, so two 3D passes in one frame (field + vista sub-viewport) cancelled out, and the flip rate followed the display's | `applyQuake` only marks the frame drawn; `stepQuake` flips at 60-Hz boundaries after a drawn frame |
| Battle quakes (`BattleDeathDirection` in `battle_0004`, `0008`, `0013`, ... - `162` `StartQuake(10, 0.2, 1.0, 143)` sites) | Same root `QuakeControl` task, battle layers `5..9` | Battle scripts' `Cr3System.StartQuake` / `StopQuake` were stubs and the battle projection never shook | Battle `extraNatives` bind `bindQuakeNatives`; `drawCr3Battle` applies `applyQuake` (`quake: 10 wide, 0.2 -> 1 over 143 frames` at battle 4's boss death) |
| `Cr3FieldMapCamera.QuakeStart` / `QuakeStop` (`Cr3BindFieldMapCamera` `0x00851d20`, `FldUtilFieldMapCameraQuakeStart` `0x00872074`) | CR2's 2D field-map quake left as **no-ops** in CR3 (`return param_1`); the scripts' `CameraQuake` / `vCameraQuake` wrappers are never called | `FieldScript` steps a 2D map quake (`frames * 0.05`, CR2's 20 Hz) | Unchanged (unreachable from CR3's scripts); recorded here |

### 8. Round 5 parity audit (battle script staging: boss deaths, event cameras)

A census of the 157 battle scripts' native calls against CR3-U's bindings found the boss-death
staging (`BattleDeathDirection`, from `Battle_Victory` in 22 battles), the event camera after a
last attack (`ChangeCameraAfterLastAttack`, 38 battles), and the scripted events' `StopAuto`
(`BattleEventStartSet`, 62 battles) all landing on stubs. The six natives were traced from
`Cr3BindBattle` (`0x00854b98`). Battle 4's boss death (`m212`) now plays as walkthrough Part 3's
does at `14:24..14:30`: the boss's own camera (`Cam_d02_m235_00`) with the light pillars
(effect `186`) and the quake, the party hidden, the field empty once the boss is hidden, then
the finisher's solo victory pose.

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| `BattleUnit.PlayAnimation(id, type)` (`BtlScript::BattleUnit::PlayAnimation` `0x009a7b5c`, `BtlUnitList::PlayAnimation` `0x009d802c`, `BtlUnit::updateAnimationEnd` `0x009e10c4`, `SetDie` `0x009da470`) | `AnimCommon`'s type from its start, no blend; at its end types `1, 3, 4, 5, 7..10, 14..17` (mask `0x3c7aa`, or `4`) go back to the stance, the rest hold; type `6` (death) marks the unit dying first | Stub | `setCr3Motion(cr3MotionOf(type))`, type `0` looped, the mask's types back to the stance, the rest held; type `6` keeps the unit in sight (its knockout fade undone) |
| `BattleUnit.PlayEffect(id, effect, point, scale)` (`0x009a7d24` -> `BtlUnit::PlayHitEffect` `0x009de81c` -> `BtlVisualEffectSystem::PlayHit` `0x009e1d50`) | `Effect`'s figure at the model's effect point (`0..7`, its locator plus offset), placed once in world space (not following), scaled uniformly, its own sound and end | Stub | `spawnCr3Effect(effect, cr3EffectPoint(unit, point), scale)` |
| `BattleUnit.Hide(id)` (`0x009a7c30` -> `BtlUnit::DisableDraw` `0x009e02b8`; `EnableDraw` `0x009e0238`) | Undrawn at once, no fade, no script-level Show; the victory's finisher drawn again | Stub | `Cr3BattleUnitView::scriptHidden`; cleared for the victory's finisher |
| `BattleCamera.Play(name)` / `SetPosition(x, y, z)` (`0x009a65f0` -> `BtlCameraSystem::Play` `0x009772dc`; `0x009a665c` -> `SetPosition` `0x009792d0`) | The clip's own figure (`CRDBpak1/<name>.geom/.anim`; a `Cam_bt` name the battle's), from the origin, not looped, held at its end until another `Play`; `SetPosition` moves its origin in world space at once (after `Play`, which resets it) | Stubs; the victory then put its "all" camera over the boss's | Own-figure clip from the origin for its duration, `cr3Cam.actor` = the position; `cr3ScriptCamera` keeps the victory's default camera off a script's |
| `BattleArgument.GetActionType` (`0x009a4d38` -> `BtlDamageChunk::GetActionType` `0x0097e450`) | The damage chunk's (else the last direction's) command's `MbCommandInfo +0x18 -> +0x04` - its **CommandDirection** row's kind (`MbGetCommandInfo` `0x00912020`: `+0x60a20 + id * 0x20`; `CreateDirection` `0x00912438` fills `+0x60a38`) -, `-1` without; `5..8` / `22` read `0x20..0x24` when the chunk's `+0x20` is set | Stub, so `ChangeCameraAfterLastAttack` never cut to `Cam_event_start` after kinds `9, 10, 20, 30, 39` | Round 5 read `CommandFomula +0x04` (0 for nearly every command - wrong); round 7 reads the CommandDirection kind, so the cut follows finishing moves |
| `Battle.StopAuto` (`0x009a82f4` -> `BtlInterfaceSystem::StopAuto` `0x009a4028`) | Clears the player's auto battle (`+0xd4`) and closes the command buttons | Stub | Bound as a no-op: CR3-U has no in-game auto battle |

### 9. Round 6 parity audit (the scene's feedback blurs)

`Cr3System.SetMotionBlur(motion, zoom)` (`310` calls turning it on in the events) and
`SetSpecialBlur` (`63`) were unbound. They set `GameSystem +0x68` / `+0x69` / `+0x6a`, which
`GameMain::PostEffectBlur` reads once a frame. In `GameMain::Render`, the 3D layers (`2..8`) go
into the offscreen scene target, then `RenderEffect`, bloom, bloom mask, **`PostEffectBlur`** and
`CrossFade` work on that target; layer `0xc` puts it on the screen and the UI layers `0xd..0x19` draw
over it. So the blurs touch the 3D scene only. `PostEffectRenderer::Build` gives its full-screen
passes: `0` opaque copy, `1` additive (`SRC_ALPHA, ONE`), `3` shader type `8` with blending off,
`5` alpha-blended (`SRC_ALPHA, ONE_MINUS_SRC_ALPHA`) at `0.5`.

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| Motion blur (`+0x68`; `utils::SetMotionBlur` `0x0092b3c4`, `GameMain::PostEffectBlur` `0x0096ca54`) | Ping-pong buffers `4` / `5` (read `[n & 1]`, written `[(n & 1) ^ 1]`): the scene copied to the next (pass `0`), then the last one blended over it at `0.5` (pass `5`, scale `1.0`) - this frame and the one before, averaged | Stub; no blur | `Viewer::applySceneBlur` after `dev->endScene()` and before the cross-fade and the UI, with the same buffers, order and pass |
| Zoom blur (`+0x69`) | The last buffer blended over at `0.5` scaled `1.2` about the centre (pass `5`), *then* the result copied on: a trail that accumulates outward | Stub | Same order: `blurQuad(..., 1.2, 0, 0.5)`, then `copyFrame` |
| Special blur (`+0x6a`; `SetSpecialBlur` `0x0092b3f4`) | The last buffer added over (pass `1`) scaled `1.1`, turned `0.01`, at alpha `min(n / 50 + 0.5, 0.8)`; the result copied on (pass `3`); the scene put on the screen zoomed `min(1 + n / 1000, 1.02)` (layer `0xc`'s `RenderLayerScreen` scale); all flags off: `n = 0`, zoom `1` | Stub | Same; pass `3`'s type-`8` shader drawn as a plain copy (not decompiled); the zoom drawn from the frame copy |
| Frame copies (`PostEffectBuffer` targets, `Cr3OffScreen +0x10`) | GPU render targets the scene renders into | No way to keep a frame on the GPU (the cross-fade reads one back through the CPU) | `RenderDevice::copyFrame`: GL `glCopyTexSubImage2D` (bottom row first: `Texture::bottomUp`); Vulkan ends dynamic rendering, `vkCmdCopyImage`s the frame into a texture whose view reads alpha `1`, resumes, and with MSAA (transient samples, not stored) draws the copy back as the base |

Checked headless on both renderers and with Vulkan MSAA `4x`: `vs:q058_t01_00212_evt` (motion
and zoom, Daisuke's "Aaaaangh...", 41.0..46.0 s, 298 frames) shows the outward trail on the 3D
scene with the cut-in portrait and the window crisp; `vs:s221_t01_00702_evt` (special, 72 frames)
shows the additive, slightly turning feedback. A display faster than 60 Hz runs the feedback per
drawn frame, as `GameMain::Render` does, so its trails are shorter in time there.

### 10. Round 7 parity audit (the rest of the battle scripts' natives)

Done in a separate worktree by a delegated agent (REA bundle
`/tmp/cr3-opening-audit/libcrx-audit-round6-battle-natives.json`), reviewed and re-checked here
(`HasProhibitedTarget` and `MbGetCommandInfo` / `CreateDirection` re-decompiled). Battle 7 (the
tutorial with the grab: command `1749` holds both heroes with status effect `32`) could not be won
at team level 60: its follow-up `1755` and its release `1844` were dropped by targeting, so the
party stayed locked. Stub lines in headless battles went from `2..31` to none.

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| Required target effects (`BtlStatusEffectList::HasProhibitedTarget` `0x009b88cc`, `MbCommandInfo::DisableNonTarget` `0x009124d0`) | **Any one** of the command's required effects is enough; any forbidden one blocks; a unit under a kind-`72` effect is targetable only by a command that removes it or (`DisableNonTarget`) requires it | All required effects needed; kind `72` passed only for removers | One-of; `DisableNonTarget` honoured (`1844` needs `32` or `43`; `1755` requires `32`) |
| `BattleUnit.GetAttachId` / `Attach` (`0x009a8080`, `0x009a7ca0` -> `BtlUnitList` `0x009da9a8` / `0x009d9e70`) | `BtlUnit +0x238` the held: Hold directions `0x11` / `0x1e` on targets with effect `32` (`UpdateHold`), or the script; `poseChild` puts the held on the holder's effect point | Stubs (null) - battle 7 never matched | Holder -> held map from Hold directions and `Attach`; the held drawn on the effect point (its rotation not followed) |
| `BattleDirection.InsertRelease` / `InsertWaitDie` (`0x009a4d98` / `0x009a4dc0` -> `Insert` `0x009915a4`) | A direction `0x27` / `0x28` after the current one; `PlayRelease` `0x009bf930`: detach, hit effect `0x34`, animation `8` or the knockout | Stubs | Inserted actions, the release at their end (the original's at its timing event 3); `0x13` commands (`1844`) release too |
| `BattleDirection.GetNextCommandId` (`0x009a4de8` -> `0x009922ac`) | The next direction's command, `-1` none | Stub | The next non-guard action's command |
| `BattleStatus.GetMode` / `HasEffect` / `AddMp` (`0x009a6434` / `0x009a64b0` / `0x009a6260`) | `VGetMode` (a monster's `+0x1fc`, a character's `0`); a status-effect or skill kind; MP added | Stubs | `Cr3Fighter::mode` (`changeMode`); effect or skill kind; MP clamped and shown |
| `BattleArgument.IsCombo` / `WasBreak` / `WasHitStatusEffect` / `GetElement` (`0x009a4ccc` / `0x009a4d0c` / `0x009a4cf8` / `0x009a4ce4`) | Chunk `+0x9e` / `+0xa8` (target-list events 2, 3 - `notifyCombo`, `BtlBreakPoint::Add`'s break), any effect landed, chunk `+0x24` | Stubs | `Cr3Action::combo` / `broke` from the rules (`addBreak` returns the break); effects given; the command's element |
| `Battle.AddFieldObject` / `SetFieldAnimatorEndTime` / `ResetFieldAnimatorLooping` (`0x009a82c8` / `0x009a8198` / `0x009a81bc`) | A figure on a field joint, looped; the field animator's end time; looping off | Stubs | Drawn on the joint; the field's time stepped and held (battle 69's joints are the final field `e0054b`'s - not seen drawn under `--battle 69`'s `d1251b`) |
| `BattleModel.Create` / `PlayAnimation` / `SetPosition` / `SetRotation` / `PlayEffect` (`0x009a6960` .. `0x009a6c6c`) | The script's own models; an `Effect` at a point | Stubs | Drawn (battle 15's guest `c104`); the effect upright; the x/y/z order unverified (every call passes 0) |
| `BattleUnit.SetHomePosition` / `HideMesh` (`0x009a7fec` / `0x009a7c68`) | Home moved and `ReturnHome`; `SetVisible` of `Model +0x10`'s mesh | Stubs | Home moved at once; `HideMesh` a no-op (`m222_sub`, the only name, is in no figure of the release) |
| `Battle.OpenUI` / `CloseUI`, `BattleCommand.SetStart`, `Battle.IsWildCard` (`0x009a8290` / `0x009a82ac` / `0x009a6044` / `0x009a8414`) | The status and break-gauge UI; an opening command through `phaseGetSetStart` into `phaseCalculate`; the VS tournament row's `+0x6c` | Stubs | HUD panels hidden / shown; the command opens the first turn (sorted with the monsters' - where the original puts it UNKNOWN); `IsWildCard` "this tournament deals wildcards" (`+0x6c`'s meaning UNKNOWN) |
| Addition-unit natives, `BattleUnit.DeleteEffect` (`0x009a7af8` .. `0x009a7fb4`) | Effects on addition units (battle 54's cards) | Stubs | Documented no-ops: CR3-U has no addition units |

### 11. Round 8 parity audit (the events' remaining natives)

The Round 4 replay's stub list counted calls made by each script's top-level code, which
`cr3tool events` runs in a bare VM without the game's natives. `Cr3System.GetAreaNum` was one of
these: it is bound (`bindGameState`) and no event reached it unbound. `CR3_EVENT_STUBS=1 cr3tool
events <release> all` now lists each event's own unbound natives with their counts
(`stubs <script>:<event> <native> <n>`). The census was compared with the viewer's own bindings
(`bindAreaNatives` for an area's events, the quake alone for a vista's). Six natives and two sets
were traced from their `Cr3Bind*` binders by a delegated REA pass
(`/tmp/cr3-opening-audit/libcrx-audit-round8-evidence.json`, 33 records). Seven of those functions
were re-decompiled here, plus `addLastAttack`, `CampCharaPanel::SetSelectStatus` / `SetPanelLock`,
and `VistaMain` / `Fld2Main::GetModeType`
(`/tmp/cr3-opening-audit/libcrx-audit-round8-verify.json`). The script side was read with
`tools/nut.py dis`: `PartyEquip(chr)` (`SetEquip`, `ChangePlayerEquip`, `SetLastAttackLevel(chr, 2)`),
`SetCampWindowParty` / `WaitCamp` / `LockPanel`, and `UnEquip*`'s `while (CheckDisposeMenu())`.

Walkthrough playlist (gocalibergaming, about 89 parts): Daisuke and Leary join (`s100_t01_00501_evt`,
`PartyEquip`) at the end of Part 2, `22:50`. Part 3 then shows the new party's first field (`d01`)
and battle (`11:20..17:00`, the Break Gauge tutorial).

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| `Cr3System.SetLastAttackLevel(chr, lv)` / `SetFinestArtsLevel` (`utils::` `0x00930040` / `0x00930060`) | The character's `CrxCharacterData +0x14` / `+0x18` (`crx_game_data +0x15ba0`, `0x2c` per character), saved. `CrxCharacter::GetLastAttackCommandId` `0x00931b20` filters the character's `LastAttack` rows (`command_para.mbe`, 18 rows) on required level (`+8`) no higher than its own and gene value (`+0xc`) no higher than the worn gene's `MbGeneInfo +8`, then picks one weighted by `+0x10` (20 / 30 / 40). `PartyEquip`'s `(chr, 2)` opens all three tiers | Stubs (5 events) | `GameState::cr3LastAttackLevel` / `cr3FinestArtsLevel`, saved (`cr3lastattack` / `cr3finestarts` lines). The finishers that read them are not in CR3-U (*Not settled*) |
| `Cr3System.CheckDisposeMenu` (`0x0092e5bc`) and `Unequip*` (`0x0092e3ac` ..) | `GetCurrentMode - 0x16 < 3`: true while the disposal menu (`DisposeControlTask`, modes `0x16..0x18`) is open. `Unequip*` opens that menu when the bag was already full (`IsWeaponFull` `0x009537fc`: more than 94 held besides the worn) | Stub (8 events; the scripts' wait ended on `null`) | `false`: CR3-U's bags have no such limit, so an unequip never needs the menu |
| `Cr3GimmickData.ChangePlayerEquip` (`Fld2ChangePlayerEquip` `0x00870038` -> `Fld2TaskPlayer::ChangePlayer` `0x00877d70` / `0x0087612c`) | Field mode only (mode type 1; a vista's `VistaMain` is `0xe`): the leader (party slot 0) rebuilt in its armour's body (`BuildCharacter`'s `<name>b<NN>`) where it stands, idle loop again | Stub (3 events) | `Viewer::cr3RebuildLeader` in an area's events (the `loadArea` leader choice, now `cr3LeaderBody`). A no-op in a vista |
| `Cr3FieldMapCharacter.SetVisible` (`Fld2SetPlayerVisible` `0x0086d4e0`) / `SetPos_p000` / `SetRot_p000` (`0x00842fac` / `0x00842fcc`) | `SetVisible`: `Fld2TaskPlayer` flag `0x400`, field mode only, set again by `ChangeArea`. `SetPos_p000` / `SetRot_p000`: `Func0`, their arguments dropped; only an `EvtScene` made if there is none | Stubs (e00 / d02) | `areaPlayerShown` (reset by `loadArea`), area events only. `SetPos_p000` / `SetRot_p000` are no-ops |
| `Cr3FieldMapCamera.SetMode` / `Scroll` / `ScrollTo` (`0x0086f068` / `0x00870c64` / `0x00870cec`) | Act only on `Fld2CameraOrtho`: the map table's (`0x00f3e568`) type 2, `e0013` and `e0015` (`CreateArea2D`). `Scroll(x, y)` jumps to `(xmax x, -ymax y)`; `ScrollTo` tweens over `frames / 30` s in mode 1. Elsewhere they do nothing | Stubs (e00 / d02, CR2-style calls such as `SetPos(0, -1200)`) | No-ops: CR3-U draws `e0013` / `e0015` with the 3D camera (*Not settled*) |
| `InterfaceWindow.SetCampWindowParty` / `CheckAliveCampWindowParty` (`InterfaceMain` `0x008f00dc` / `0x008f0620`) | `CampTopMenu` (`0x33`) with `CampcomBtnMenu` over the event in its event mode (`+0xf5`). The scripts wait while it exists (`Func0`: their `true` dropped) | Stubs: the party window never opened and the wait ended at once (13 events, area and vista - `q074_d05_00104_evt`) | `openCr3Camp` with `cr3CampEvent`, drawn and stepped over a vista too. `CheckAlive` is true while the camp is open; closing it lets the event go on (`camp-event-lock`) |
| `InterfaceWindow.LockCampTopCharacterPanel(slot)` / `UnLock` (`0x008f06ac` / `0x008f0764`; `CampCharaPanel::SetPanelLock` `0xa34244`) | `InterfaceMain +0x288 + slot` (party slot `0..4`, not saved). The panel and its parts are drawn at `SetSelectStatus(3)`'s diffuse `(0.5, 0.5, 0.5)` (`DAT_00f63890` / `0x00f638a0`: status 0 `1,1,1`, 1 `0.714,0.714,0.851`, 3 grey). It can't be taken up (`CheckTopArea`) and is skipped as a swap target (`TouchSimpleMove`) | Stubs (15 / 14 events) | `cr3PanelLock`: the panel greyed, the Vita's held-○ move skips it, and a locked panel won't move |
| `Cr3System.SetBloom(flg, offset, scale, limit)` (`0x0092b43c`; `GameMain::PostEffectBloom` `0x0096c618`, `PostEffects::RenderBloom` `0x00b90908`) | Off by default (`GameSystem +0x10 = 0`; defaults 160 / 60 / 90). On, it adds a bloom to the 3D scene before the blur and cross-fade: a 512 copy, a 32x32 luminance read a frame late (`exp(Σ ln(rgb)/1023/1024) + scale`), a bright pass and five 128x128 blur passes (step `offset * 0.01`, `limit`), then added ONE/ONE | Stub (8+ events: `s100_t01_00501`, `s902_d02_01103`, ...) | Still unbound (*Not settled*): passes `5` / `6` are built-in MVGL shaders not yet recovered |

### 12. Round 9 parity audit (an event's gene windows; the VS tutorial)

Round 8's census left two groups of natives. Both were traced from `Cr3BindInterfaceWindow` /
`Cr3BindSystem` by a delegated REA pass (`/tmp/cr3-opening-audit/libcrx-audit-round9-evidence.json`,
18 records). The scripts were read with `tools/nut.py dis`.

- **The gene windows around a reward.** A story event (`s481_d16_00104_evt`, gene 378 at level 100)
  or a quest's report (`quest_report`: quest 58's gene 230 at level 40, and others) first calls
  `CreateDisposalEquipMenu_Gene`, waiting while `GetDisposalEquipMenuResult() == 0`. It then calls
  `CardGet_Msg`, which gives the gene through `Cr3System.GeneGet` and shows the gene-get window
  (both already bound). Last comes `CreateGeneStatusSCModalMenu(gene, level, false)`, waiting
  while `AliveGeneStatusMenu(true)`.
- **This was a bug in CR3-U:** `GeneGet` returns −1 and drops the gene when 50 are held. The
  original's disposal step exists to make room first, so before this round a full box silently
  lost the reward.
- **The reference:** walkthrough Part 3 (`Q-i3K6JUxqY`) shows the same status window at `17:20`,
  after the first gift fusion ("Nobunaga", level 001, over Fuse Gifts).

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| `InterfaceWindow.CreateDisposalEquipMenu_Gene` / `GetDisposalEquipMenuResult` (`InterfaceMain::CreateDisposalMain(0x14)` `0x8f2688`, `DisposalEquipMain::Update` `0xa60188`; `Func0` / `Func0<int>`) | `DisposalEquipMain` (`0xc5`); the result is `InterfaceMain +0x290`: 0 running, 8 done, no cancel. The box not full (`CrxGameData::IsGeneFull` `0x954f6c`: 50 slots) is done at once. A full box shows told `0x218` (`{categoryname}` `0x303`), then a held gene picked (`CreateBaseGeneSelectMenu`, header sentence `0x2f6`, no back), asked `0x236` (`{genename}`; No returns to the list), `DBSystem::RemoveGene`, told `0x237`, then done | Stubs (3 events and `quest_report`). The wait ended on `null` and the reward's `GeneGet` failed on a full box | `openScriptGeneDisposal`: done at once with room (`GeneGet`'s own rule, so the discard always makes room). Full, the same steps over the event (the presents' discard pieces: `drawCr3CampGenes`, `drawCr3Told`, `drawCr3Asked`) |
| `InterfaceWindow.CreateGeneStatusSCModalMenu(gene, level, skills)` / `AliveGeneStatusMenu` (`0x8dbee4` / `0x8dbaf4`; `Func3<int, int, bool>` / `Func0<bool>`) | `GeneStatusMenu` (`0xa7`, `Initialize` `0xa9b5ec`). It shows `CrxGeneManager::GetGene(gene, level)` on `mixStsPositon`: `geneCngStsMenu` at `call_geneCngStsMenu_fit0`, the portrait `mixGeneIcon_id00` (`mat_card_001` = `card_<series>`) at `call_geneCngCharaIcon_fr00`, then name, level (MAX at the cap), HP / MP, four stats and the eight skills (unlearned dim). No element, lock or card beside it. Any tap or back closes it (0.2 s fade). With `skills`, a tap on a skill slot shows its help in the header instead | Stubs: never shown | `openScriptGeneStatus` with `drawGeneBase`'s new `GeneStatusMenu` layout, over an area's or a vista's event; any tap, ○ or back closes it. Matches Part 3's `17:20` frame (panel and portrait in place). The `skills` help taps aren't in |
| VS tutorial natives: `CreateGeneCardSelector` (menu `0xe1`, `Func6<int>`; three `CardModel`s on `VsWildNull`'s `VsWildTouch_002..004`, `bt_602`; press, then press again to pick; result: the index, −1 till then; no cancel), `CreateOverLay` / `DeleteOverLay` (`OverLayMain` `0xdf`: a modal container, an RGBA `(0, 0, 0, 0.7)` mask on layer `0x12`, later windows inside it; immediate), `GetSelectTournamentID` (`InterfaceMain +0x704`, −1 until a tournament is picked), `Cr3System.VsBattleIn(id, p2, p3)` (`FldUtilReserveVsBattleIN` `0x872630`, `Func3<int>`: a battle reserved from the VS tournament row, black battle-entry colour, `SetGlassEffect`) | Run by the VS tutorial: `VSMain::OnUpdate` `0x96e2dc` loads `vs.nut` and runs `evt000` before the VS menu exists. That chains `s910_t01_00101_set` (flags `3501` ..: the starter-gene choice, `CreateGeneCardSelector(1, 17, 4, 18, 7, 19)` -> `Sync2('tuto_get_gene1..3')`) and `00201` (`VsBattleIn(1, -1, 1)`); `VsModeMain`'s states call `VS_Top`, `VS_Top2`, `VS_Normal_Top`, `VS_BattleIn` for the rest. `DBSystem::SM_SkipVSTuto` `0x814ad0` is the skip | Stubs; CR3-U's VS mode (`openVs`) doesn't run `vs.nut` or its hooks, so none of it is reached | Unchanged: the tutorial flow is *Not settled* (binding these alone would change nothing) |

### 13. Round 10 parity audit (finishers; bloom)

Two delegated REA passes traced the finishers (`/tmp/cr3-opening-audit/libcrx-audit-round10-lastattack.json`,
51 records) and the bloom (`/tmp/cr3-opening-audit/libcrx-audit-round10-bloom.json`, 20 records; five
records from a dropped REA connection are lost, their findings re-checked with `llvm-objdump`).

**Finishers.** `BtlCommandManager::calculateCommands` `0x007db93c` calls `addLastAttack` `0x007dbfc8`
before each next command. `command_para.mbe` has the data: 55 commands with `+0xbd` bit 1 (the
characters' attacks 1..9, physical skills 201..235, and a few others). The `LastAttack` rows (commands
11..18 at level 0 with any gene, 21..25 at level 1 / gene stage 1, 31..35 at level 2 / stage 2; weights
20 / 30 / 40) and `FinestArts` rows (101 .. 127) are ordinary commands. Their CommandDirection kinds (9
LastAttack, 10 MultiLastAttack, 11 SingleFinestArts, 12 FinestArts, 20, 30, 39) play them; the only
announcement is the command name.

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| When (`addLastAttack` `0x007dbfc8`, `BtlDamageChunk::HaveChanceReset` `0x0097e56c`, `calculateCommand` `0x007dc424`, `BtlCommandList::VInsertTop` `0x007e4fd8`) | Before each next command, from the last action not itself inserted (`+0x188`) and the combo's participants (`+0xb8`: an actor added after a comboing action, alone after any other). It fires when all of these hold: at least two participants; the last command's `+0xbd` bit 1 and its combo; the next command not inserted and breaking the chain (the other side, its actor under status effect `0x1f`, or none of the same units hit); the first target standing. The finisher goes in before the next command, marked inserted (`+0x1a`), so it leads into none | None | `Cr3BattleRules::turn`: the same, the finisher entry marked `2`, `Cr3Action::finisher` set; battle 22 at team level 20 (party 1, 2, 3) played two, command 13 |
| Which (`BtlBreakGaugeUI::HaveAdvantageOver` `0x0099d41c`, `CrxCharacter::GetLastAttackCommandId` `0x00931b20` / `GetFinestArtsCommandId` `0x00931d7c`, `BtlStatus::GetLastAttackCommandId` `0x009b722c`, `MbMonsterInfo` `0x00913b98` / `0x00913ba8`) | With the side's gauge at its full advantage (`+-1000`), a random participant with Finest Arts uses them (`+0x24`). Otherwise the last attacker's Last Attack: the character's `Char` row ids whose level is at most `CrxCharacter +0x1c` and gene requirement at most the worn gene's stage (`gene_para` `+8`; -1 no gene), picked by weight; Finest Arts take the highest allowed level, ties at random. Status effect `0x3f` means none, `0x40` command 18. A monster's are its mode's `MonsterCommand` `+0x18` / `+0x1c` (Finest Arts -1 in all 1132 rows). Levels start at 0 (`CrxGameData::Init` `0x0094f764`) | None (round 8 kept the levels only) | `lastAttackCommand` / `finestArtsCommand`, the levels from `GameState` by the `Char` row's save slot; `battle-target-tests` checks the order, the mark, the full-advantage fallback and a lone action's none (and fails when the expected command is changed) |

**Bloom.** `SetBloom`'s argument order is offset, limit, scale. Round 8's table had the last two the
other way round; the debug HUD's names fix it ("Bloom Limit" `+0x30`, "Bloom Offset" `+0x34`,
"Bloom Scale" `+0x38`). The shaders are MVGL's built-in GLSL in `.rodata` (`DefaultVP` / `DefaultFP`
`0xba00a4` / `0xba00d4`).

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| `Cr3System.SetBloom(on, offset, limit, scale)` / `GameMain::PostEffectBloom` `0x0096c618` / `PostEffects::RenderBloom` `0x00b90908` | Into RGB565 targets (clamped, linear, never cleared, blending off): the scene copied to 512x512, then that to 32x32 (read back a frame late: `key = exp(sum ln(r + g + b) / 1023 / 1024)`). Then `filterBrightFP` from the 512 into 128x128 (`c = colour x 2`, kept where `c.r + c.g + c.b > key + limit`), and five `filterBlurFP` passes (taps `0.0545 / 0.2442 / 0.4026` at `-2 .. 2 x blurOffset`, `x scale`; V, H, V, H, V, offset `x 0.01` UV). The last one is added onto the scene (ONE, ONE), before the blurs, under the UI. Off by default (160 / 60 / 90) | Stub: no bloom | `RenderDevice` filters (`beginFilters`, `filterPass` copy / bright / blur, `endFilters`, `readTexture`): GL FBOs on RGB565 with the original shaders, Vulkan offscreen RGBA8 images (`filter.vert` / `filter.frag`); `Viewer::applySceneBloom` runs the same passes and adds RT2 back. `s902_d02_01103_evt` (offset, limit and scale 0.8) glows on Vulkan, Vulkan with MSAA 4x (the frame drawn back) and GL alike, and the validation layer (with synchronisation checks) reports nothing |

**The VS tutorial.** A third delegated pass (`/tmp/cr3-opening-audit/libcrx-audit-round10-vs.json`,
31 records; some lost to a shared REA session, marked there) traced `VSMain` / `VsModeMain` and
`vs.nut`. Walkthrough Part 7 ("Battle Mode: Normal Battle", `roh96Zm3saM`, local `part7.mp4`) plays it:
the gene cards at `0:55`, the party window at `2:00`, `00601`'s overlay messages at `5:05`, the camp's
Gene Swap at `5:25`, then Battle and the lifeline items.

| | The original | CR3-U before | CR3-U now |
|---|---|---|---|
| VS entry (`VSMain::Init` `0x96e25c` / `OnUpdate` `0x96e2dc`) | `vs.nut`'s `evt000` first, before the menu. Its `_set` functions reserve the tutorial's vistas in turn (`s910_t01_00101_evt` in `t0102f`, `00201` in `t0151bt`; `SetVistaFuncName`): the game goes to the vista and back to `VSMain` anew after it (next mode `0x19`). Otherwise the menu comes up (request `0x66`, `DAT_0149ef7c` -1) | The menu at once; `vs.nut` never run | `Viewer::vsEnter`: `evt000` on its own runtime (`vsEvents`), then `vsVistaReserved` (the field kept, the vista on its map's area's script) or `openVsMenu`; a VS vista's end goes back through `vsEnter` |
| The menu's hooks (`VsModeMain::Update` `0xb6b600`, state `0x24`) | `VS_Top` on the top (entered, back from the camp, from the gene lab), `VS_Top2` on Battle, `VS_Normal_Top` / `VS_Special_Top` on the lists, `VS_Normal_SelectedFullItem` (four lifeline items), `VS_MPRecovery_End`. While one runs the menu keeps drawing but takes no taps. Its windows draw over `OverLayMain`'s mask (`0xdf`: black `0.7`, layer `0x12`; `CreateOverLay` / `DeleteOverLay` at once). A reserved vista closes the menu | None | `runVsScript` at the same points; `activeEvents()` gives the VS script's windows the screen and taps; `drawEventUi` draws the menu, the mask, then the windows |
| Buttons (`VsModeMenu::SetButtonDiffuse` `0xb5f4e8`) | Flags 3601 .. 3612 grey (`0.5`) and shut the top's and Battle's buttons (`PleaseCampPush`: only the camp; `PleaseNormalBattleStart`: only Battle and Normal). The MP / My Page row (part `+0x60`) appears only with flag 33 (`VS_TutoAllClear`): Part 7 has no such row | All up | `vsButtonLocked`; the row hidden and shut until flag 33 |
| The gene cards (`OpenGeneCardSelector` `0x8f8778`, menu `0xe1`) | `CreateGeneCardSelector(1, 17, 4, 18, 7, 19)`: three `CardModel`s (4.5, in focus 5.4) on `VsWildNull`'s `VsWildTouch_002 .. 004`. `AnnounceBattle 0x32` ("Select a card.") goes in the battle announcement bar, the focused card's shared text (17 .. 19: "Michael (Ice)") in its place. A press focuses, a second takes (`com_003`, `bt_603`). The result is the index, then `Sync2('tuto_get_gene1..3')` gives gene 1 / 4 / 7 and its status window shows | Stubs: `null` never -1, so no branch, no starter gene | `geneSelector` with `drawGeneSelector` (`btHelpMenu` for the text); Part 7's `1:00` frame and CR3-U's match (cards, focus, text) |
| `Cr3System.VsBattleIn(id, p2, p3)` (`FldUtilReserveVsBattleIN` `0x872630`) and `Invoke(16 / 17)` (`0x92f8e8`) | Reserves the tournament's battles after the vista (`DAT_0149ef7c` its index; `p2` / `p3` the colosseum's commentators). `00201`'s `(1, -1, -1)` then `Invoke(17)` (BeginVSTournament) until 200: tournament 1's couplings 2001 .. 2003. Every tournament also goes through `VS_BattleIn`'s vista (`99999`, or `00801` for the tutorial's second), whose `VsBattleIn` reserves its battles. Afterwards `VSMain` anew (results, then `evt000`) | Stub; tournaments started at once from the menu | The vista's end runs the reserved tournament (`vsNextBattle`); the menu's start runs `VS_BattleIn` and its vista first; the prizes' end returns through `vsEnter`; `Invoke(17)` begins `GameState::vsSelected` |
| The VS camp's close (`CampTopMenu::SetParamNumber` 2 `0xa389cc`) | Flag 42 off: every member wearing a gene sets 42 and 3505 (the tutorial's `00702` next); one without gets alert `0x307` and the camp stays | Closed as always | The same (`campTopTold`) |

## Original-code evidence

Static inspection via REA/Ghidra against the shipped ARM64 Android shared library (`+0x100000`
Ghidra image base), cross-checked against local PS Vita (`re/vita/decomp.c`) and iOS binaries.
Deterministic REA evidence bundles are stored outside the repository at
`/tmp/cr3-opening-audit/libcrx-audit-evidence.json` (`38` Round 1 records, `0` unknowns),
`/tmp/cr3-opening-audit/libcrx-audit-round2-evidence.json` (`46` Round 2 records, `0` unknowns), and
`/tmp/cr3-opening-audit/libcrx-audit-round3-evidence.json` (`5` batched Round 3 records covering `26` procedures, `0` unknowns), and
`/tmp/cr3-opening-audit/libcrx-audit-round4-evidence.json` (`7` Round 4 records, `0` unknowns).

| Target | SHA-256 |
|---|---|
| CR3 Android 1.1.3 `libcrx.so` | `43950b32c2c064cf25c6324a75d2b89c0cfc243996dcbb20961ef855bd99bcf3` |
| CR3 PS Vita 1.00 `eboot.elf` | `e897c8a14426849604bd0d13f6ace9d9f0b06214d25a70fa95d64f2c39efe89a` |
| CR3 iOS 1.2.2 `ChaosRings3_FINAL_ENG.arm64` | `ec1ebefccf9dcc30f1f2b96aae62d01d7694c73860c14c57a0bd3560632f0ac5` |

| Function | Android VA | Observation | REA Evidence ID |
|---|---|---|---|
| `FrameControlTask::OnUpdate` | `0x009301ec` | 60-Hz outer loop (`dt >= 0.01666667`, Vita `0x810b474e`) calling `ChildUpdate` twice per `1/30 s` logical frame (`0.0f` then `1/30f`). | `ev_595b226a3fe1b4f705847c7cd083b6ac3f6ee0e0903ceeb389db81227503b450` |
| `GameMain::OnUpdate` | `0x0084264c` | Runs `InterfaceMain::Update` before `EvtSystem::Update`. | `ev_8640332df79af00afdfdc60ffda1fbf051f82816e42b5095092f376ff59f3711` |
| `CrxGameData::SetDefault` / `TitleMain::OnUpdate` | `0x00843cb0` / `0x0096dfa4` | `SetDefault` clears flags; `TitleMain` sets scenario flag `1` on New Game. | `ev_595b226a3fe1b4f705847c7cd083b6ac3f6ee0e0903ceeb389db81227503b450` |
| `FadeWindowMenu` (`Open` / `Close` / `Update` / `FadeOnRender`) | `0x0087f3ac` .. `0x0087f56c` | Pass `0x13..0x15` over Vista/cut-ins/menus, immediate snap when `frames <= 0`, `+0x24 = 4` (2-tick) hold on `FadeIn`. | `ev_0ab6acf01e740128d2c5428433efa0a6552f6741ab2de1f45c51887ee9970383`, `ev_e9d58f3bf53c61d7e96b1aa2a04de3a4cc5169d0a35932ebda69f7a0318d072b` |
| `BackFadeWindowMenu` (`Open` / `Close` / `Update`) | `0x009689a0` .. `0x00968af8` | Pass `0xd..0xf` behind portraits and UI windows. | `ev_0ab6acf01e740128d2c5428433efa0a6552f6741ab2de1f45c51887ee9970383` |
| `EvtModel::SetMove` / `EvtScene::Update` / `VistaMain::Init` | `0x0085e608` / `0x0085f114` / `0x0085f8c8` | `from / 30`, `max(from, to) / 30`, `moving = to > from`, initial camera/light paused (`this[8] = 1`); no CR2 `d11_06` special case. | `ev_595b226a3fe1b4f705847c7cd083b6ac3f6ee0e0903ceeb389db81227503b450`, `ev_ff928d712380f39e3f524a3d289cb8d3a1cfe37d2f7ca21febee7d5145ed219f` |
| `VistaCamera::SetFrame` / `VistaLight::SetFrame` / `Cr3BindVistaEvent` | `0x0085f218` / `0x008488fc` / `0x0085010c` | End time `to * (1/30)` resting on frame `to`; `DirectionalLightColorAnimStart` 5-arg signature. | `ev_595b226a3fe1b4f705847c7cd083b6ac3f6ee0e0903ceeb389db81227503b450`, `ev_71405b07494d901fb5892931abfea6f0500b43944777c683421a8e7a18f4bb7a` |
| `Cr3BindEvtScene` / `Cr3EvtSceneDestroySprite` / `EvtModel::ChangeColor` | `0x0084e61c` / `0x0084607c` / `0x0085e724` | `EvtScene.DestroySprite(id)` (`26` script calls) and `EvtScene.ChangeColor(model, r, g, b)`. | `ev_9c38eea2ad3de66b97dcad58331bdbc72b26573c0ce579b6ed7d821e4a97d02b`, `ev_71405b07494d901fb5892931abfea6f0500b43944777c683421a8e7a18f4bb7a` |
| `Cr3BindSystem::CrossFade` / `PostEffectCrossFade` | `0x0092fb20` / `0x0096cf98` / `0x0096d93c` | Captures pre-transition 3D frame and blends alpha down over `frames / 30 s`. | `ev_5e3770b8c8ef0c8cfdcdfa8a9c287e559e851f9b9f93daf5515fe24208a46c8f` |
| `utils::SoundPlayBGM` / `SoundStopBGM` | `0x0092855c` / `0x00928738` | Fade counts in 30-Hz frames (`1/30 s` per step). | `ev_595b226a3fe1b4f705847c7cd083b6ac3f6ee0e0903ceeb389db81227503b450` |
| `ProvisionalMenuText::Step` | `0x0088a360` | `+0x450 += 1.0` per 60-Hz call (60 chars/s), halting on `\w` and `\@` stops. | `ev_0ab6acf01e740128d2c5428433efa0a6552f6741ab2de1f45c51887ee9970383` |
| `EvtWindowMessage::ReadPage` / `EvtWindowPage::NextBlock` | `0x008632a0` / `0x00860c38` | Per-line `\c` (centred) and `\l` (left), `\w<NNN>`, `\@`, `\*`, and 3-digit `\d<NNN>` speaker tag stripped from body text. | `ev_f4fcce59cbf2fa7177b6c66a8846e10652d1453076ad3151ca5c42dcbc0c8539`, `ev_71405b07494d901fb5892931abfea6f0500b43944777c683421a8e7a18f4bb7a` |
| `MessageWindowMenu` (`Open` / `Update` / `SetStringData`) | `0x00ad6c40` .. `0x00ad7548` | `0.1667 s` (`5`-tick) `_io` clip, `0.10 s` `tapWait`, 4-slot `comMsgMenu` (`align` ignored, Vita `0x811ed0ba`). | `ev_e9d58f3bf53c61d7e96b1aa2a04de3a4cc5169d0a35932ebda69f7a0318d072b` |
| `NarrationMenu::TouchGestureTap` / `SelectTwoChoicesMenu::TouchSimpleRelease` | `0x00b4531c` / `0x00b7a52c` | Tap reveals current segment without skipping `\w`; Yes/No plays `0.10 s` reverse close. | `ev_e9d58f3bf53c61d7e96b1aa2a04de3a4cc5169d0a35932ebda69f7a0318d072b` |
| `CutInWindowMenu` / `CutInCenterMenu` / `MapNameMenu::Setup` | `0x0096a0c0` / `0x0096a1c4` / `0x00b1eb24` | `0.20 s` cut-in slide with `8/30 s` in / `4/30 s` out alpha, centre frame close fade, level-0 floor omission. | `ev_0571af08a0fc8b66313a9e365000533a47287771553811152d956f41c29c8d2e` |
| `StaffrollMenu` / `Cr3BindInterfaceWindow` | `0x00b3e0e0` .. `0x00b3ed78` / `0x008f2e9c` .. `0x008f3364` | 60-Hz two-column `Staff.dat` scroll (`+0` / `+800`), `endSteps = end - start + 0x1b`, initial 1-based row `start - 1`, `ttlendrogo_fr00` logos, `ttlendmenu` cut-ins. | `ev_b52047192d29a9b87000f78c015adf80a4069189d2a2662c83ee856646d9252b`, `ev_d5282673f684fc2ee822e59f295e1259efa1e987cb532cc0376542e604eedeee` |
| `Fld2TaskPlayer::UpdateMovePad` | `0x0087b878` | `0.05` pad run threshold, fixed `1.2` / `4.8 m/s` speeds. | `ev_0571af08a0fc8b66313a9e365000533a47287771553811152d956f41c29c8d2e` |
| `Fld2CameraGom::Update` / `UpdateControl` | `0x00867ca0` / `0x00869194` | Free follow camera without movement auto-yaw; `1.5 rad/s` yaw, `1.125 rad/s` pitch (`+-20 deg`). | `ev_9cb204cd7845780ade6a82078972d1f08f002365b4edff093c8a27eee63584ff` |
| `Fld2TaskMap::GetDungeonNo` / `Fld2CameraLookDown::SetupCamera` / `Fld2Main::Setup` | `0x0086b870` / `0x0086a81c` / `0x0087d89c` | 24-entry `Field_` prefix table (`t01..t05`, `d01..d18`, `e00`); LookDown camera selection. | `ev_069779bc20401db695c8e95940fc3b77e89368a26401ac783c7f3e7ea1a8d115`, `ev_8187f818c7b6208eec99fb8298cf7b2c9cc9c8d27f5568207cc0bbdf60b6ed30`, `ev_9d5f27fd137819c88c7e16897a78336490dc6f8cc98b82cfb2b9a28a5b39e9e6` |
| `Fld2TaskGimmick::GetFieldParamByName` / `Fld2TBoxList::Drawing` | `0x0086bf84` / `0x0087b378` | Quest-scoped `Field` row lookup, `"ragdoll_chk"`, custom `checkRange`; `GBOX_PROBS` break on failed tier. | `ev_271152dcbdc54cf3f5a09d4feb5bc601ccc8d40fb78ec812bf7aa571ad93c622`, `ev_9cb204cd7845780ade6a82078972d1f08f002365b4edff093c8a27eee63584ff` |
| `Fld2BalloonManager::CheckBalloon` / `Fld2NPC::Check` | `0x0087aee8` / `0x0087bad8` | Closest check-target selection by squared distance. | `ev_0571af08a0fc8b66313a9e365000533a47287771553811152d956f41c29c8d2e` |
| `Fld2Main::BranchAfterEvent` / `Fld2Main::Update` / `Fld2TaskEncount` / `CrxGetEncounterUp` | `0x0087e774` / `0x009ed044` / `0x00866838` / `0x00958124` | `popPoint` / `changeDungeon` priority after events, `5x` skip speed, `30`-frame encounter BGM fade, passive skill `124` encounter-up. | `ev_595b226a3fe1b4f705847c7cd083b6ac3f6ee0e0903ceeb389db81227503b450`, `ev_271152dcbdc54cf3f5a09d4feb5bc601ccc8d40fb78ec812bf7aa571ad93c622`, `ev_daa1d059705d936ed2831abb47dcd66335c3daefd76e13f01beb8347b230b319` |
| `PostEffectGlassFragment` / `Cr3BindSystem::SetGlassEffectPattern` | `0x00950600` / `0x00861f04` | Shatter presets `0` (radial, 230), `1` (drop, 200), `2` (swirl, 400). | `ev_595b226a3fe1b4f705847c7cd083b6ac3f6ee0e0903ceeb389db81227503b450` |
| `BtlSystem::createParty` / `Escape` | `0x007d903c` / `0x007d94f4` | `Coupling + 0x44` bit `3` solo `Char` `7`; player-only start skills; guaranteed escape on Break Advantage or skill `86`. | `ev_8640332df79af00afdfdc60ffda1fbf051f82816e42b5095092f376ff59f3711` |
| `BtlCommandList::Sort` | `0x007e54cc` | Priority buckets `0` / `2` (`-8`) / `5` (`-9`), `Guard` `+2`, `speed - weight + speedCost`, `reverseSpeed`. | `ev_8640332df79af00afdfdc60ffda1fbf051f82816e42b5095092f376ff59f3711` |
| `BtlUnit::GetMpCost` / `GetHpCost` / `BtlCommand::CheckTarget` / `OpenTargetSelect` | `0x007dc840` / `0x007dcafc` / `0x007dd71c` / `0x009a8f10` | `mpKind` / `hpKind` `0..3` (with `mpKind == 3` two-step `double` truncation), `requiredEffects` / `forbiddenEffects`, revive KO targeting, MP target bar. | `ev_1661f5d700f51055eb05e2326e9330811fbcc1e5f4901e94796b80c1b85e8d84`, `ev_65263118b4e5fb8068423409da87ee3b9984a7136ceded3eca1dbf8d90c660c6` |
| `calculateCriticalHit` / `calculateHit` / `calculateBasicDamage` / `BtlDamageTarget::Calculate` | `0x007e0c28` / `0x007e0f3c` / `0x007e18a0` / `0x00983a44` | Once-per-target `crit`/`down`, monster `+6` crit & `+13` hit, skill kinds `17`/`18`/`19`/`139`, Break-Advantage passives `+0x44`, `bootCharacter` `10x`, per-hit `chain`. | `ev_8640332df79af00afdfdc60ffda1fbf051f82816e42b5095092f376ff59f3711` |
| `BtlCalculator` (`action == 14`, heals `5..7`/`15..18`, offensive `8`/`12`/`13`/`20`/`22..24`) | `0x007df13c` .. `0x007e3b38` | Speed hit-count scaling, HP/MP/Chocolat/friend-count heals, `%`/ailment/HP-consumption/MP/speed/constant-HP formulas. | `ev_1e762e197c539f2e69d1017f473c379681bb9cc717de67bff5e3d2243d2c9464`, `ev_329cec0f284a0e1e9ef47a290e891c752fa3192fd10d42b2fcd99813de7c74a3` |
| `BtlDamageUI::getDamageColor` / `drawDamage` | `0x009a0968` / `0x009a0c44` | `btnum_fr00` digit frame `(color * 10 + digit) / 30` for colours `0..4` (Vita `0x813f2e6c`). | `ev_8640332df79af00afdfdc60ffda1fbf051f82816e42b5095092f376ff59f3711` |
| `BtlSysStateStart` / `BtlSysStateVictory::Enter` / `BtlUnitList::PlayVictory` / `BattleResultV2Menu` | `0x009d4be8` / `0x009d6e4c` / `0x009d6ed0` / `0x00a65528` | Tap skip on shatter, unpadded `GetSet` camera end, solo finisher `(modelId, 2)` `Cam_b03_*` camera & unit hiding, `bt_win` motion `2`, `30`-frame result BGM fade. | `ev_e90cf4022e084b6ad866c2ed351f8fca3832076ea23dd351f593ab83e177693a`, `ev_59aed96041158ab92a845b43a8e3145291e00f49f90cf84a1e8de3b6f0b2c783` |
| `DBSystem::SM_BuyVip` / `SM_BuyVipTrial` / `SM_EndVSTournament` / `CheckVSSeasonScore` | `0x007fec54` / `0x007feec4` / `0x007fd5d4` / `0x007ff670` | Immediate VIP login gift bonus, VS tournament `prize_gold` & `GOLD_MAX` (`9,999,999`), VS season `scores[0] -> scores[1]` progression. | `ev_bea48800a55bd5d3d499d5cbdcc4b9e23ce46a4dc2f6688f7ebd58a3213ca7b3`, `ev_749ae993eb84fd1a94fcf167f3fa2264e94b1cbb773ed80fad33dc08975c9fac`, `ev_11fdade0b11b2065c54f08850bc30cdecf505a60da5716918efabc6e0456c9e5` |
| `DBSystem::GetDailyItem` / `CompleteMergeGene` | `0x0081fb78` / `0x0082e48c` | Partial-stack daily item top-up to `99` before setting daily flag; `mergeGene` sets story flag `34`. | `ev_8a0c98c56a4bbb5bb99402da0aca5f7497f45ddd843384566213793af69ba8e8` |
| `FieldAnnouncementMenu::SetParamNumber` / `SetContentTutorialMenuNum` / `OnFadeNarretionMenu` | `0x00a65664` / `0x00b526cc` / `0x00aebb04` | Announcement windows `8..10` (`0`, `-2.1`, `-4.2` Y-offsets), tutorial page swap without closing `AnnounceTop`, narration Z-order above fade. | `ev_8a0c98c56a4bbb5bb99402da0aca5f7497f45ddd843384566213793af69ba8e8` |
| `EvtSprite::Build` / `Cr3EvtSceneLoadAttachmentMotion` | `0x00f01134` / `0x00844914` | `1920x1920`-cover `.img` sprite geometry & RGBA interpolation; prop attachment bone-socket & motion binding. | `ev_8a0c98c56a4bbb5bb99402da0aca5f7497f45ddd843384566213793af69ba8e8` |
| `BtlStatus::initializeGene` / `BtlStatusEffect::CanCancel` / `GetAttackMultipliers` | `0x009b25a4` / `0x009b874c` / `0x009b1574` / `0x009b164c` | Player element initialized from equipped Gene; status group priority & mutual cancellation (`0x10`); odd status kinds sum `stacks * constant / 100`. | `ev_b1b9b93fcd24c23911414afecd763524eb882458b7e4f39a2c8ef68b1aa99e07` |
| `GetPercentDamageResistance` / `HasStatusEffectInvalid` / `HasInvalidStatusEffectToCalculation` | `0x009af9cc` / `0x009afc28` / `0x009b6bd4` | CR3 Gravity resistance skill `35`; per-ID status immunity (`68`) evaluated before `flags & 8` sure-status. | `ev_b1b9b93fcd24c23911414afecd763524eb882458b7e4f39a2c8ef68b1aa99e07` |
| `calculateProportionalToStatusEffectNumber` / `GetSameElementCorrection` / `ChangeElementToCalculation` / `BtlTotalDamageUI` | `0x007e1c8c` / `0x009b0648` / `0x0097b9b0` / `0x009a4744` | Formula `12` `extraType 18` (`+20` power/status), skill `103` (`+-c%`), post-damage element shift respecting `102`, and `bttopanel` combo HUD. | `ev_78b15d7fc8549df31f9abd9de8293720e0728f5f2dc3273b35ea38ece41c669c` |
| `GeneCardMenu::SetCardLockView` / `CampEquip2Menu::Update` / `CampItemMain::Update` / `CrxUtilAddMP` | `0x00a9388c` / `0x00a1da3c` / `0x00a2c2f0` / `0x00886a38` | Combine lock/worn base block, `CampEquip` Step 0/1 category headers, key-gift unwrapping (`600065`/`600067`/`600068`), party MP items (`0xa24` mask), and Command/Skill card learning. | `ev_78b15d7fc8549df31f9abd9de8293720e0728f5f2dc3273b35ea38ece41c669c`, `ev_7fe959299602a3235299fcf5388c40ca93b8e1bc1a6064292f3e8f385eaebdc3` |
| `Fld2TaskEncount::CalcAveLevel` / `AceCouplingLevelUp` / `MiniMapMenu::ChangeMapTexture` & `SetGimickPointDataView` | `0x008677d8` / `0x00867d88` / `0x00ad838c` / `0x00ad8654` | Equipped-Gene average encounter level, Ace Coupling post-victory level-up, `d0904`/`d0908`/`d0912` negative-layer minimap guard, and box/gimmick icon filtering. | `ev_7fe959299602a3235299fcf5388c40ca93b8e1bc1a6064292f3e8f385eaebdc3` |
| `utils::StartQuake` / `StopQuake` / `QuakeControlTask` (`Start` / `OnUpdate` / `Render` / `Stop`) | `0x0092d33c` / `0x0092d3e0` / `0x00944188` / `0x00944084` / `0x009440f8` / `0x009441ac` | `frames * 1/30` seconds, negative held; time clamped to `|seconds|`; way flipped after a drawn frame; layer `+0x88` offset `width * lerp(from, to)`; defaults `0.005`, `1.0`. | `ev_68de576b287f7abeaee7c129177cb3f108cdd48b454c9e4cdc91c7a675e22417` |
| `RenderLayer::RenderLayer` / `RenderSystem::Render` / `RenderLayerScreen::Offset` / `RenderSystem::GetLayer` / `OnQuake` | `0x00f168fc` / `0x00f13804` / `0x00f168a4` / `0x00f14358` / `0x0095b150` | 0x90-byte layers, `RenderLayerScreen` at `+0x7c` added to the camera's `+0x240..+0x250` (x/y scale, x/y offset, roll) for the pass; debug `OnQuake` `Start(10, 1, 0, 3 s)`. | `ev_2695d5bf159c1049d41c11805ba043addf1066433c5ba93b6f77cfbffd476ae9`, `ev_ccfd601f3698f29c18af53b3cbf337bc92cffa5460e7ec7ecae83a601273f31a` |
| `Cr3BindFieldMapCamera` / `FldUtilFieldMapCameraQuakeStart` / `Fld2SetFieldScriptCamera` / `Fld2TaskCamera::ChangeScriptCamera` | `0x00851d20` / `0x00872074` / `0x0086f518` / `0x009ebc18` | Field-map quake natives are no-ops; script camera `frames * 1/30`, toy camera dropped, character movement paused and field UI hidden while it's on. | `ev_e1bad8b85ee3e09974a5bfa76428cd01b06e84c7df4436fd2f9b0f658053bf2b` |
| `utils::SetMotionBlur` / `SetSpecialBlur` / `GameMain::PostEffectBlur` | `0x0092b3c4` / `0x0092b3f4` / `0x0096ca54` | `GameSystem +0x68` / `+0x69` / `+0x6a` flags drive a ping-pong feedback blur (buffers `4|5`, pass `5` at scale `1.0` / `1.2`; special: pass `1` scale `1.1`, roll `0.01`, alpha `min(n/50 + 0.5, 0.8)`, layer `0xc` zoom `min(1 + n/1000, 1.02)`). | `ev_9f3ab6a0c15ee3a6fbb9bdb3898c9a04b116dfbc3ce71fea6fd763dcb2daa406` |
| `MVGL::Draw::Camera::GetScreenMatrix` / `SetGraphicLevel` / `SM_SetupGraphics` | `0x00b8f184` / `0x0096ac18` / `0x00969360` | Screen translation = camera `+0x248 / DisplayInfo width`, `+0x24c / DisplayInfo height` (device pixels; the scene buffer `GameSystem +0x70 x +0x74` is the display or `80%` of it). | static `re/android/arm64/decomp.c` read (`848708..848794`, `381426..381520`, `380299..380440`) |
| `Cr3BindBattle` / `BattleUnit::PlayAnimation` / `PlayEffect` / `Hide` / `BattleCamera::Play` / `SetPosition` / `Battle::StopAuto` | `0x00854b98` / `0x009a7b5c` / `0x009a7d24` / `0x009a7c30` / `0x009a65f0` / `0x009a665c` / `0x009a82f4` | Sqrat `Func2<int,int>`, `Func5<int,int,int,float,..>`, `Func1<int>`, `Func1<const char*>`, `Func3<float,float,float>`, `Func0`; the behaviour in round 5's table (`BtlUnitList::SetDie` `0x009da470`, `BtlUnit::changeRequest` `0x009dc958`, `loadRequestAnimation` `0x009dcb88`, `changeAnimation` `0x009dcc14`, `updateAnimationEnd` `0x009e10c4`, `PlayHitEffect` `0x009de81c`, `BtlVisualEffect::Initialize` `0x009e23fc`, `DisableDraw` `0x009e02b8`, `BtlCameraSystem::Play` `0x009772dc`, `SetPosition` `0x009792d0`, `BtlInterfaceSystem::StopAuto` `0x009a4028`). | `/tmp/cr3-opening-audit/libcrx-audit-round5-battle-natives.json` (`11` records: `ev_dfc6da68...`, `ev_32c6cb2c...`, `ev_6620ad0d...`, `ev_098762407...`, `ev_2b71f4cb...`, `ev_17f38a89...`, `ev_3256c819...`) |
| `BattleArgument::GetActionType` / `BtlDamageChunk::GetActionType` | `0x009a4d38` / `0x0097e450` | `MbGetCommandInfo(+0x1c) +0x18 -> +0x04`; the chunk's `+0x20` remap of `5..8`, `22`. | static `re/android/arm64/decomp.c` read |
| `PostEffectRenderer::Build` / `Render` / `SetScale` / `SetAlpha` / `SetBlur` / `GameMain::Render` | `0x00ef856c` / `0x00ef8d80` / `0x00ef8c38` / `0x00ef8d38` / `0x00ef8b50` / `0x0096b46c` | Passes `0..5` (`{blend on, src, dst, FUNC_ADD 0x8006}`, shader types `0, 0, 6, 8, 5, 0`; pass `5` alpha `0.5`); `SetScale` scales the figure's size about its centre, `SetAlpha` `+0x1fc`; the render order (scene target, then effect / bloom / mask / blur / cross-fade, then layer `0xc` and the UI). | `/tmp/cr3-opening-audit/libcrx-audit-round6-blur-evidence.json` (`17` records: `ev_047314557070de1248aaa37067f7bc2624ccd3584fb933a20619f35f8eb02c7d`, `ev_1d5f91241ccd0e44d392dbced259241f1e1d9fca85ceb8be8bfeafa5f7b6f6a8`, `ev_2b261c051cb71f2fd58b14ebe569fc272507fbaba2e4bb35ec72e7a2f2e570fd`) |
| `BtlDamageTarget::Hold` / `BtlDirection::holdTarget` / `BtlHoldDirection::VUpdate` | `0x00988474` / `0x0098cbdc` / `0x0099317c` | Attach only via Hold directions and the script. | `/tmp/cr3-opening-audit/libcrx-audit-round6-battle-natives.json` (`ev_c5f34083f2b6...`, `ev_30b6e36b05cc...`, `ev_4764cf4b466b...`, `ev_c9f97aabc698...`, `ev_68c98be14d78...`, `ev_5c834f645829...`) |
| `BtlStatusList::PlayRelease` (callers) / `BattleCommand::HasStart` (callers) / VS tournament globals | `0x009bf930` / `0x009a60a4` / `0x0149ef7c` | WaitDie, HoldLastAttack and Release directions release; `isOpening`, `phaseStart`, `phaseGetSetStart`; Begin / EndVSTournament. | same bundle (`ev_2af59b43dcae...`, `ev_c20e870d0174...`, `ev_8e34f055a6af...`, `ev_9694dbd05261...`, `ev_715488982049...`) |
| `utils::SetLastAttackLevel` / `CrxCharacter::GetLastAttackCommandId` / `utils::SetBloom` / `utils::CheckDisposeMenu` / `Fld2SetPlayerVisible` / `Cr3EvtSceneSetFieldMapPos` / `InterfaceMain::LockCampTopCharacterPanel` | `0x00930040` / `0x00931b20` / `0x0092b43c` / `0x0092e5bc` / `0x0086d4e0` / `0x00842fac` / `0x008f06ac` | Round 8, re-decompiled here: `(&DAT_014bde64)[chr * 0xb] = level`; level- and gene-filtered weighted pick; `GameSystem +0x10 / +0x34 / +0x30 / +0x38`; `mode - 0x16 < 3`; flag `0x400` in mode 1; `Func0` making an `EvtScene` only; `+0x288 + slot` and message `(0x33, 7)`. | `ev_ebe1f653524a7f37c8d46d2b9b12ba9ad79090ccc7ed631447091801f48043cb` (`/tmp/cr3-opening-audit/libcrx-audit-round8-verify.json`) |
| `BtlCommandManager::addLastAttack` / `BtlCharacterStatus::VGetLastAttackCommandId` / `CampCharaPanel::SetSelectStatus` / `SetPanelLock` / `VistaMain::GetModeType` / `Fld2Main::GetModeType` | `0x007dbfc8` / `0x009b29a8` / `0xa33f70` / `0xa34244` / `0x008609e4` / `0x0087e9a4` | After a command with `MbCommandInfo +0xbd` bit 1 whose chunk combo'd (`+0x9e`) and `HaveChanceReset`, with the first target alive: a random ally's Finest Arts under the player's Break advantage, else the actor's Last Attack (none at `-1`). Panel status colours `(t1[s], t1[s], t2[s])`; vista mode `0xe`, field `1`. | `ev_077566f1d3f24a0508b2face97dbe1441d7cf92f9e1da0ec1be75f8240c4825f`, `ev_6d13aef8f38258ec6ce9998ea80ad69f9aa3ffbce9b8a47f9d822c89bb773de8`, `ev_7c99574ed2728276cde5d5779dac041105d074ad9c1201479d6b0382fa989148` |
| Round 8's delegated trace: `Cr3BindSystem` `0x0084bcfc`, `Cr3BindGimmickData` `0x00850e24`, `Cr3BindInterfaceWindow` `0x00853664`, `Cr3BindFieldMapCharacter` `0x00852714`, `GameMain::SetBloomEnable` `0x0096d99c`, `PostEffects::Build` `0x00b8f91c`, `GameSystem` ctor `0x00938fdc`, `IsWeaponFull` `0x009537fc`, `Fld2TaskPlayer::ChangePlayer` `0x00877d70` / `0x0087612c`, `CampTopMenu::Initialize` `0x00a37cc0` | (as listed) | The bindings' `Sqrat` wrappers (from the binders' disassembly and GOT relocations), the bloom defaults and buffers, the full-bag disposal path, the field leader's rebuild, the camp's event mode and the lock's use. Vita: `utils::SetBloom` `0x812abeba`, `RenderBloom` `0x813324da`, `DisposeControlTask` `0x812b335c`, `Fld2ChangePlayerEquip` `0x810df6e0`, `Fld2SetPlayerVisible` `0x810ddb16`, `CampTopMenu::Initialize` `0x81117f48` | `/tmp/cr3-opening-audit/libcrx-audit-round8-evidence.json` (33 records) |
| Round 9's delegated trace: `CRXInterfaceEvent_CreateGeneStatusSCModalMenu` `0xa9f11c` -> `InterfaceMain::CreateGeneStatusSCModalMenu` `0x8dbee4`, `AliveGeneStatusMenu` `0x8dbaf4`, `GeneStatusMenu::Initialize` `0xa9b5ec` / `SetGeneParamerter` `0xa9d4fc`; `CreateDisposalMain` `0x8f2688`, `DisposalEquipMain::Update` `0xa60188`, `CrxGameData::IsGeneFull` `0x954f6c`; `OpenGeneCardSelector` `0x8f8778` (result `0x8f8838`, alive `0x8f88c0`); `CreateOverLay` `0x8f9ee8` / `DeleteOverLay` `0x8f9f18`; `VsModeMain::Update` `0xb6b600`; `FldUtilReserveVsBattleIN` `0x872630`; `VSMain::Init` / `OnUpdate` `0x96e25c` / `0x96e2dc`; `DBSystem::SM_SkipVSTuto` `0x814ad0` | (as listed) | Wrappers from `Sqrat::Object::BindFunc` `0x857e70`'s GOT slots (no `sq_setparamscheck`: extra script arguments are dropped). The gene windows' menus, parts, results and closing; the VS tutorial's start (`vs.nut` `evt000` before the menu) and its hooks. `VsBattleIn` reads the tournament row before it looks up the new id (the previous index; the tutorial's own source index UNKNOWN). The `%c%03d_%s%03d_%s_%03d%02d` event name is the debug launcher's (`OnEventStart` `0x95b57c`) | `/tmp/cr3-opening-audit/libcrx-audit-round9-evidence.json` (18 records) |
| `BtlStatusEffectList::HasProhibitedTarget` / `MbGetCommandInfo` / `MbCommandInfo::CreateDirection` | `0x009b88cc` / `0x00912020` / `0x00912438` | Required effects one-of, forbidden any, kind `72` vs `HaveRemovesStatusEffect` / `DisableNonTarget`; command info `+0x60a20 + id * 0x20`, its `+0x18` the CommandDirection row (`+0x60a38`). | `ev_e3fc4585634207165f3ef2fd47ccab38f947c9d7aa1bfb46a53e104fb40301cc`, `ev_014221d6622f743c0fdcc8eea1857c706905a04bc829dc429c80e6cfca1600a7` |

## Not settled

- **Battle staging still open (Round 5)**: `BattleUnit.PlayAnimation` type `6`'s `notify(4)` at the
  white-fade frame (`BtlUnit::updateWhiteFade` `0x009e1028`) and a model's own camera on a motion
  (`BtlUnit::PlayCamera` `0x009dd234`) are not modelled. Round 7's approximations: the held unit's
  rotation, the release's timing, `SetStart`'s place in the turn, `IsWildCard`, and the addition
  units (above).
- **Unbound natives (Round 10)**: none in the census; the VS tutorial's are bound by the viewer (its
  `vsEvents` and the vistas).
- **The VS tutorial (Round 10)**: these parts aren't in or aren't traced.
  - Not in: `VsBattleIn`'s commentators (`p2` / `p3`: `BtlBootParam +0x38 / +0x3c`, colosseum voices
    in `BtlSystem::Init`); `VSMain`'s results state (`0xb`, `CreateVsAchieveProductionMenu`) before
    `evt000` (CR3-U's own results and prizes come first, then `vsEnter`).
  - Not traced: the overlay's exact quad (`CustomFigure` 14 x 14 at depth 5); whether `CreateInterface`
    routes the script's message windows into the overlay (CR3-U draws them over it); who sends
    `SM_SkipVSTuto` (request `0x97`).
  - Never called: `s910_t01_00602 .. 00605` and `00704` (dead code in every script).
  - `VS_Special_Top` only prints.
  - `FldUtilReserveVsBattleIN` reads the tournament row at the old index before looking the new
    id up (the tutorial's: the `.bss` 0, tournament 1 anyway).
  - The pad case stops when tournament 1's battles start. Its battles, `00701`'s join and
    `00601`'s locks run as VS's own (the battles' wildcard and score screens are CR3-U's).
- **Gene windows (Round 9)**: the `skills` help taps on `GeneStatusMenu`'s skill slots aren't in.
  The disposal list's header title isn't traced (CR3-U shows Gene Swap's). Fullness follows
  `GeneGet`'s count (all held), while `IsGeneFull` counts the 50 box slots. They differ only when the
  stock holds genes, and `GeneGet`'s own rule then refuses earlier than the original's.
- **Finishers (Round 10)**: three parts aren't modelled. The Finest Arts directions' `BtlField::Hide`
  (the stage hidden) and their commentator voice (`PlayClimax`) aren't in. `CalculateForceBreakPoint(true)`'s
  `BtlCalculator +0xa5` (a Finest Arts' break points) is UNKNOWN. The finisher check runs only before a
  next command in the same turn (the original's also carries `+0x188` to the turn-end list).
  `CR3-U`'s `wouldHit` treats a random target's side as all of it (`CollectDamageTarget`'s random pick
  isn't traced). Status effects `0x1f`, `0x3f` and `0x40` are tested by id; their names are UNKNOWN.
- **Bloom (Round 10)**: Vulkan's targets are RGBA8, not the original's RGB565 (no 5/6/5 quantising),
  and Vulkan doesn't read the 32x32 luminance back (its term taken as 1: it can only be 0.995 .. 1.001).
  `PostEffectBloomMask` (`GS +0x55`, off by default, its writer unknown) and `PostEffectBloom2` / `3`
  are separate effects, not traced.
- **The 2D areas' camera (Round 8)**: `e0013` / `e0015` use `Fld2CameraOrtho` in the original
  (`Scroll` / `ScrollTo` / `SetMode`). CR3-U draws them with the 3D camera, and the three natives are
  no-ops.
- **Camp event mode (Round 8)**: in `CampTopMenu`'s event mode button 2 becomes 7, and a Yes/No
  (`0x1f0`) or an alert (`0x307`) can appear. What each button does there is UNKNOWN; CR3-U keeps
  the camp's normal buttons. The Vita focus tint `(0.5, 1, 1.5)` is CR3-U's own; the original's
  status-1 colour is `(0.714, 0.714, 0.851)`. A visible armour change by `ChangePlayerEquip` wasn't
  exercised headless (`s001_t02_00101`'s armour 83 is j001's default body, `b02`); the rebuild reuses
  `loadArea`'s figure loading.
- **Quake reference height**: the original's shake is `offset / 2` *device* pixels, so it shrinks on
  denser displays; CR3-U fixes the divisor at the playthrough's `720` so the look matches the
  reference at any window size.

- Sub-frame interpolation between odd (`0.0f`) and even (`1/30f`) 60-Hz `ChildUpdate` passes is
  collapsed into CR3-U's `EventRuntime::update` (`1/30 s`) plus `eventLead()` render interpolation,
  matching all observable frame counts on 30-Hz boundaries without running duplicate zero-`dt`
  script updates.

## Checks

- **CTest suite (`25/25` passed)**:
  `CR3_DATA=/home/duislingr/Downloads/Archive/CR3_android_1.1.3 ctest --test-dir engine/build --output-on-failure -E viewer-pad-vita`
  passes all 25 suites (Round 4's `viewer-quake`: first frame down by `width * from / 720`, flip
  only after a drawn frame, `from .. to` ramp, end at `frames / 30`, negative frames held at `to`,
  `StopQuake`; and `battle-target-side`, `vita-import`, `input-cancel`, `lifecycle-clock`,
  `gl-context-recovery`, `viewer-input-cancel`, `viewer-lifecycle-clock`, `audio-player`,
  `viewer-scene-audio`, `viewer-session-ui`, `viewer-field-restore`, `viewer-new-session`,
  `viewer-save-slots`, `viewer-vista-pause`, `gom-camera`, `atomic-file`, `hd-texture`, `card-art`,
  `mobile-input`, `save-integrity`, `event-reset`, `atrac9-bits`, `new-game-plus`, `tab-buttons`),
  including Round 1, Round 2, and Round 3 assertions in `event-reset-tests` (0-frame fade snap,
  2-tick `FadeIn` hold, post-fade script resume, `SetMove(m, 300, 0)`, `CameraMove(5, 15)`, per-line
  `\c`/`\l` and default-centred windows `2..4`/`6`/`8..10`, `AnnounceTop = 8` vs `TutorialMenu = 20`,
  `SetContentTutorialMenuNum`, `OnFadeNarretionMenu`, `CutIn` 5-tick open hold, odd-index `\w` stop
  boundary & non-skip on tap, `0.10 s` `tapWait`, `0.10 s` Yes/No close, 24-entry `cr3DungeonNum` /
  `cr3DungeonPrefix`, `EvtScene` `DestroySprite` / `ChangeColor` / `CreateSpriteWithTexture` /
  `SetSpriteScale` / `SetSpriteColorAnimation` / `SetSpriteAlphaAnimation` / `ChangeVariation` /
  `ChangeArmor` / `SetWeaponVisible` / `CreateAttachment` / `DeleteAttachment`, `Cr3System`
  `CrossFade` / `SetDungeonLevel` / `BattleInColor` / `SetViewportEnable`, `CheckNewGene` +
  `Invoke(1)` sync, `dungeonLevel` & `cr3AceLevels` save/load roundtrip, `StaffrollMenu`, immediate
  VIP login bonus, VS `prize_gold` + `GOLD_MAX`, VS season `scores[0] -> scores[1]`, Oz-Coin
  `receivePresent` + `COIN_MAX`, and partial-stack `getDailyItem`) and `battle-target-tests`
  (`BtlCommandList::Sort` buckets `0`/`2`/`5`, `Guard` `+2`, `speedCost`, `reverseSpeed`, popup
  colours `1..4`, multi-hit `chain` escalation, `bootCharacter` `10x`, `mpCost` / `hpCost` modes
  `0..3`, `action == 14`, heals `5..7` / `15..18`, offensive `8` (`35` Gravity resist) / `12`
  (`extraType 18`) / `13` / `20` / `22..24`, revive KO targeting, `requiredEffects` /
  `forbiddenEffects`, single-action `bucket0` Unison, `GetSameElementCorrection` (`103`) +
  post-damage element shift + Fix Element (`102`), 34-column `StatusEffect` stacking/priority/
  `68` immunity vs `flags & 8`, CR3 evasion (`82`) & crit (`44`), mid-turn `HaveEnoughMp` abort,
  passive regen (`45`), `extraType 13` Break force-set, and passive `70` Break Reset).
- **Controller & UI integration suite (`35/35` cases, `0 problems`)**:
  `CR3_DATA=/home/duislingr/Downloads/Archive/CR3_android_1.1.3 sh engine/tools/pad_vita_test.sh engine/build/cr3view`
  passes all 35 headless controller cases. Round 10 added `vs-tutorial` (a fresh game's VS:
  `evt000`'s gene-card vista, the middle card focused and taken, gene 4's status, `00201`'s party
  window, `VsBattleIn`'s tournament 1 started) and `vs-tutorial-camp` (after 3501 / 3502:
  `00701`'s vista, `VS_Top`'s `00601`, Battle shut). `vs-cursor` / `vs-detail` now play a player
  past the tutorial (`VStuto_Clear`'s flags). The VS and camp groups pass on the Japanese Vita and
  iOS releases (14 / 14 each). Round 9 added `gene-event-status` (`s481_d16_00104_evt`
  with room in the box: the disposal done at once, gene 378's status at level 100, ○ closing it, the
  event ended) and `gene-event-disposal` (50 held: told `0x218`, the cursor, asked `0x236`, one
  discarded - 49 held -, told `0x237`, then the reward and its status). The camp group, both
  included, passes on the Japanese Vita and iOS releases too (10/10 each). Round 8 added `camp-event-lock`: `q074_d05_00104_evt`'s camp
  over its vista - slot 0 locked and greyed, held ○ skipping it, the swap with the next, the locked
  panel refused, × closing it and the vista going on to its battle - also on the Japanese Vita and
  iOS releases) across field bar, choices, options/gift tabs, continue
  menu, `Fld2CameraGom` right-stick tilt & `L` reset, title/save footer, movies, field goods, lunch
  balloons, Spaceport shop/boxes/My Page/VS, camp panels/sound/equipment/genes/reels/index, and
  battle cursor/item categories/list boundaries/victory/result confirm.
- **Round 8 event census**: `CR3_EVENT_STUBS=1 engine/build/cr3tool events <Android 1.1.3> all` -
  `4127/4127` ended; `SetLastAttackLevel`, `SetFinestArtsLevel` and `CheckDisposeMenu` gone
  from it (the viewer's natives, which `cr3tool` doesn't load, checked in `cr3view` with `CR3_STUBS=1`:
  `s100_t01_00501_evt` - ended at `192.1 s` -, `e00:s111_d02_00202`, `s484_t01_00101_evt` and
  `s902_d02_01103_evt` leave only `Cr3System.SetBloom`). `event-reset-tests` adds the levels' roundtrip
  through `GameState::save` / `load` and `CheckDisposeMenu`'s `false`.
- **Full-archive & cross-release event replay (`4127/4127` events passed)**:
  `CR3_STUBS=1 engine/build/cr3tool events /home/duislingr/Downloads/Archive/CR3_android_1.1.3 all`
  completes all `4127` events cleanly with zero `EvtScene.*`, `EvtWindow.*`, `Cr3Sound.*`, or
  `VistaEvent.*` stubs; `t01:s000_t01_00101_evt`, `t01:s000_t01_00103_evt`, `t01:s000_t01_00201_evt`,
  and `vs:s491_t01_00207_evt` replay cleanly across Android `1.1.3`, iOS `1.2.2`, and PS Vita `1.00`.
- **Targeted headless `cr3view` & reference-playthrough visual checks**:
  - `CampEquip` Step 0 (`comlistcategorypanel` `"Equipment"`) & Step 1 (`Weapons`/`Armor`/`Accessory`
    tabs) + stretched 16:9 `comftrequipmenu` footer verified against reference playthrough Part 2.
  - `s000_t01_00101_evt` (`NarrationMenu`), `s000_t01_00201_evt` (`Developed by / MEDIA.VISION`
    `CreateSpriteWithTexture("cutin021_c01")`), and `d1201` `MapNameMenu` (`30.37 deg` tilt) verified
    against reference playthrough Part 1.
  - `BtlTotalDamageUI` (`--battle 10 --cr3-party 1,2 --team-level 30 --page 0.01 --input "auto"`):
    renders `bttoposition_io` -> `bttopanel`, foe diamond portrait `bttochara_id00`, gold `Hits`
    counter, `Total` damage digits, and dual green/trail HP bars cleanly under the Break Gauge.
  - Prologue & multi-party solo-finisher victory camera (`--battle 1` / `--battle 2612`), Vista
    `CrossFade` handoff, ending `StaffrollMenu`, `d1501` LookDown camera, and multi-party item
    backout & refund all pass cleanly.
