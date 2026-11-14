# Sourced by pad_vita_test.sh; run the actual viewer with its SDL virtual controller.
want battle && run battle-command-cursor \
    "0.1:until=fighter_0_chooses 0.5:down 1:down 1.5:up 2:south 3:quit" \
    "fighter 0 chooses|battle: cursor 1|battle: cursor 2|battle: cursor 1" \
    -- --battle 2612 --team-level 30 --input "battle"

# The item list's two native categories include gifts; L and R browse both directions.
want battle && run battle-item-categories \
    "0.1:until=items: 0.5:r 1:l 1.5:south 2:quit" \
    "items:|battle: item category 1|battle: item category 0" \
    -- --battle 2612 --team-level 30 --item 101:3 --input "battle item"

# Unlike button menus, comListMenu stops at both ends instead of wrapping.
want battle && run battle-list-boundaries \
    "0.1:until=items: 0.4:up 0.8:down 1.2:down 1.6:up 2:quit" \
    "test controller: up|test controller: down|battle: cursor 1|test controller: down|test controller: up|battle: cursor 0" \
    -- --battle 2612 --team-level 30 --item 101:3 --item 102:3 --input "battle item"

# The first confirm during the result tally completes it; the second dismisses it.
want battle && run battle-victory-confirm \
    "0.1:until=victory_camera_ready 0.2:east 1:quit" \
    "battle: victory camera ready|battle: victory camera skipped|result -" \
    -- --battle 2612 --team-level 30 --input "win"

want battle && run battle-result-confirm \
    "0.1:until=result_- 0.1:east 0.5:east 1.5:quit" \
    "result -|battle: result tally completed" \
    -- --battle 2612 --team-level 30 --input "win"

# BtlLastAttackDirection (kind 9): battle 22's finisher (command 13, Daisuke) plays his motion's own camera,
# AnimDirection's Cam_t01_j003_00, posed on him at the boss's front, the others hidden
export CR3_BATTLE_SEED=1 # (its draw: unseeded, the boss's monster AI and the assists sometimes left no finisher within 120 s)
want battle && run last-attack \
    "0.1:until=a_last_attack 1:quit" \
    "a finisher - command 13|a last attack (kind 9, motion 18) - camera Cam_t01_j003_00" \
    -- --battle 22 --team-level 20 --cr3-party 1,2,3 --input "auto"
unset CR3_BATTLE_SEED

# BtlFinestArtsDirection (kind 12): the gauge at the party's full advantage (+1000), a participant's Finest Arts -
# the stage hidden, its own camera, white at its frame, under the white home and all shown, the white lifted 1 s on
export CR3_BATTLE_SEED=1 # (its draw: a Finest Arts within 40 s - the boss's MP unlimited, its MP commands in its picks)
want battle && run finest-arts \
    "0.1:until=the_finest_arts'_white_lifted 1:quit" \
    "a finisher - command 1|a finest arts (kind 12|the finest arts' white lifted" \
    -- --battle 22 --team-level 20 --cr3-party 1,2,3 --cr3-gene 1:1 --cr3-gene 2:4 --cr3-gene 3:7 --gauge 4 --input "auto"
unset CR3_BATTLE_SEED

# BtlRangedDirection / BtlShotDirection (kinds 2, 3): Leary's and Elroux's attacks, camera type 1 - PlayRangedCamera ->
# PlayGlobalToTarget, an attack_ALL clip of the battle's camera size at random (not the last one again), at the origin
export CR3_BATTLE_SEED=1
want battle && run ranged-camera \
    "0.1:until=a_ranged_direction_(kind_3 1:quit" \
    "a ranged direction (kind 2, camera type 1) - Cam_bt_attack_ALL_|a ranged direction (kind 3, camera type 1) - Cam_bt_attack_ALL_" \
    -- --battle 22 --team-level 20 --cr3-party 2,4,1 --input "auto"
unset CR3_BATTLE_SEED

# BattleResultV2Menu / GeneLvUpMenu (round 21): battle 22's 832 EXP takes gene 3 (Bismarck, its six own slots full)
# from 19 to 20 and gene 2 from 14 to 17. The first window: 0x135, 0x181 (Augmented Cremation), 0x1cc, the replace
# list (↓ ↓ ↑ - stopping at the ends), ○ First Crack -> 0x1cd, × No back to the list, ○ ○ Yes - forgotten; the
# second: 0x122, 0x181 into a free slot; the cards back
want battle && run gene-level-up \
    "0.1:until=gene_level_up_-_card_0 3.6:east 7:east 8:east 9:down 9.4:down 9.8:up 10.3:east 11.2:south 11.6:east 12:east 16:east 19.5:east 21:quit" \
    "gene level up - card 0 (gene 3, level 19 to 20, 1 learnt)|gene alert 0x135|gene alert 0x181|gene alert 0x1cc|gene 3 - the replace list|gene replace cursor 1|gene replace cursor 2|gene replace cursor 1|gene replace row 1 - asked|gene replace - no|gene replace row 1 - asked|gene 3 forgot 0:201 for 0:247 (slot 1)|gene level up - card 1|gene alert 0x122|gene alert 0x181|gene 2 learnt 1:41|result - the cards back" \
    -- --battle 22 --team-level 20 --cr3-party 1,2,3 --cr3-gene 1:3:3704 --cr3-gene 2:2:1540 --input "auto guard:party result:hold"

# The get-set's tabs (round 22: FUN_8112bdc4 - ↑ from Battle lights Auto Battle, then Mass Attack, ○ its tap): Auto Battle
# chosen, its first-time tutorial (Battle_Tuto_062) paged through with ○, its turns played without the get-set
# (BtlAutomaticManager's picks), × (StopAuto) off - the turn in course played out, the next get-set as ever
want battle && run battle-auto \
    "0.1:until=turn_1,_the_get-set 0.8:up 1.3:east 2.5:east 3.5:east 3.6:until=Auto_Battle_-_turn_2 0.5:south 0.6:until=turn_3,_the_get-set 1:quit" \
    "battle: cursor on the Auto Battle tab|battle: Auto Battle on|Battle_Tuto_062|Auto Battle - turn 1|Auto Battle - turn 2|battle: auto off (the back key)|turn 3, the get-set" \
    -- --battle 2612 --team-level 30 --input "guard:party"

# Mass Attack: ↑ ↑ (Auto Battle, Mass Attack), ↓ back to Auto Battle, ↑, ○, its tutorial (Battle_Tuto_061); every turn
# the party on one foe until ×
want battle && run battle-mass \
    "0.1:until=turn_1,_the_get-set 0.8:up 1.2:up 1.6:down 2:up 2.5:east 3.7:east 4.7:east 4.8:until=Mass_Attack_-_turn_2 0.5:south 0.6:until=turn_3,_the_get-set 1:quit" \
    "battle: cursor on the Auto Battle tab|battle: cursor on the Mass Attack tab|battle: cursor on the Auto Battle tab|battle: cursor on the Mass Attack tab|battle: Mass Attack on|Battle_Tuto_061|Mass Attack - turn 1|Mass Attack - turn 2|battle: auto off (the back key)|turn 3, the get-set" \
    -- --battle 2612 --team-level 30 --input "guard:party"

# Round 23's HUD. BtlAnalyzeDirection (CommandDirection kind 0x16 - Analyze, command 393, gene 19's): BtlMonsterStatus::
# VAnalyze's three lines - the name, AnnounceBattle 32's HP, mon_help's - each the next on ○, the last closed by it; then
# Emperor Mode (315: status effects 3..6) on its caster - btStPanel's status icon the highest of 13..16 first
# (the fresh save's 100 cheer points: this seed's turn ends with an assist - calculateAssist, Enhance 71 - whose
# direction comes before the next get-set, so the quit waits for it)
export CR3_BATTLE_SEED=1 # (its order: Emperor Mode, then the Analyze)
want battle && run battle-analyze \
    "0.1:until=analyze_-_monster 0.5:east 1:east 1.5:east 5:quit" \
    "fighter 0's genes: 393 241|command 315|fighter 1's status icon 16 (mask 0x1e000)|command 393|analyze - monster 913: \"Super Old One\" / \"HP: 55000/55000\" / \"Strong against magic|analyze line 1|analyze line 2|analyze closed|turn 2, the get-set" \
    -- --battle 2612 --team-level 30 --cr3-party 1,2,3 --cr3-gene 1:19 --cr3-gene 2:457 --input "guard:party battle gene0 t0 gene3 guard"
unset CR3_BATTLE_SEED

# BtlTotalDamageUI for the party struck (TakeDamageToDirection: opposite sides, damage not 0): a slot each, in order of
# first hit
export CR3_BATTLE_SEED=1 # (its draw: 913's attack on all on turn 1 - its MP -1 unlimited, 311 and the rest among its picks)
want battle && run battle-party-totals \
    "0.1:until=total_damage_-_fighter_2 0.5:quit" \
    "total damage - fighter 0, slot 0, 1 hits|total damage - fighter 1, slot 1, 1 hits|total damage - fighter 2, slot 2, 1 hits" \
    -- --battle 2612 --team-level 30 --cr3-party 1,2,3 --input "battle guard guard guard"
unset CR3_BATTLE_SEED

# OpenAnnounce's cooperation suffix (OpenAdditionNum: AnnounceBattle 0x1c + the count): two Cremates (241, +0xbc bit 7)
# taken in together under the gauge's advantage - "Cremate II"
export CR3_BATTLE_SEED=1
want battle && run battle-cooperation-name \
    "0.1:until=cooperation_name 0.5:quit" \
    "cooperation name \"Cremate II\"|a cooperation of 2" \
    -- --battle 2612 --team-level 30 --cr3-party 1,2,3 --cr3-gene 1:19 --cr3-gene 3:19 --gauge 4 --input "guard:party battle gene1 t0 guard gene1 t0"
unset CR3_BATTLE_SEED

# Round 24's monster specials. The Mimic (battle 510): its 1646 Scrumptious MP - BtlStealMagicDirection, kind 23: cast
# as magic, the bullet from the target back to it, its MP recovery and be141heal11 as it lands -, then from mode 1 its
# Escape 1741 (BtlEscapeDirection: status effect 51 - kinds 72 + 33 -, faded out): the battle won with no EXP or money
export CR3_BATTLE_SEED=2
want battle && run mimic-steal-escape \
    "0.1:until=result_- 1:quit" \
    "command 1646|a steal back - 0 HP, 3 MP to fighter 3 (effect 161)|fighter 3 escapes - command 1741|battle 510: won|battle 510: result - 0 EXP, 0 money" \
    -- --battle 510 --team-level 30 --cr3-party 1,2,3 --input "battle guard guard guard battle guard guard guard battle guard guard guard battle guard guard guard battle guard guard guard battle guard guard guard battle guard guard guard battle guard guard guard"
unset CR3_BATTLE_SEED

# Reverse Gravity (387, extraType 0xb - the Death Mask 554, battle 152; its MP -1 unlimited): AnnounceBattle 182 after it,
# the order slowest first from turn 2 (fighter 2 first), and at the end of turn 5 - the cast turn and four more - command
# 58 with no source (BtlGravityInversionDirection: line 183). --input foe: the cast forced on turn 1
export CR3_BATTLE_SEED=1
want battle && run reverse-gravity \
    "0.1:until=turn_6,_the_get-set 0.5:quit" \
    "fighter 3 given command 387 on turn 1|action 0 - 1 -> -1|command 387|the reverse gravity on - line 182|turn 2, the get-set|action 0 - 2 -> -1|action 4 - -1 -> -1, command 58|the reverse gravity over - command 58, line 183|turn 6, the get-set" \
    -- --battle 152 --team-level 30 --cr3-party 1,2,3 --input "guard:party foe:1:3:387 battle guard guard guard battle guard guard guard battle guard guard guard battle guard guard guard battle guard guard guard battle guard guard guard"

# Call for Help (1773, Rainforcement - the Ark Raptor 1555, battle 1109): the Raptor in slot 3 felled over two turns, then
# 1773 on it - its name, AnnounceBattle 92, a Raptor 1554 faded in at the slot, acting the turn after (flag 5056: no
# battle tutorials)
want battle && run call-for-help \
    "0.1:until=turn_5,_the_get-set 0.5:quit" \
    "fighter 4 given command 1773 on turn 3|a call for help - command 1773 by fighter 4 on fighter 3|a call for help - line 92 (reinforcements)|reinforcements - fighter 3 is monster 1554 now (m229)|turn 4, the get-set|action 3 - 3 -> 2, command 1001" \
    -- --battle 1109 --team-level 99 --cr3-party 1,2,3 --flag 5056 --input "guard:party battle attack t0 attack t0 attack t0 battle attack t0 attack t0 attack t0 foe:3:4:1773 battle guard guard guard battle guard guard guard battle guard guard guard"
unset CR3_BATTLE_SEED

# Avatar (1730 Inauspicious Rain, BtlAvatarDirection - forced on battle 2612's monster): its motion's event 2 - white-out
# 1 s, ev_018 -, Effect +0x28's movie (c56, where the release has it; skipped here), white-in, camera 5 and the group
# effect with the hits. Gene Switch (1744, BtlGeneChangeDirection): no cast - camera 2, effect 51 on itself, the name
export CR3_BATTLE_SEED=1
want battle && run avatar \
    "0.1:until=turn_2,_the_get-set 0.5:quit" \
    "fighter 3 given command 1730 on turn 1|an avatar - command 1730, motion 9|an avatar's white-out|an avatar's white-in|turn 2, the get-set" \
    -- --battle 2612 --team-level 30 --cr3-party 1,2,3 --flag 5056 --input "guard:party foe:1:3:1730 battle guard guard guard movie:skip"
want battle && run gene-switch \
    "0.1:until=turn_2,_the_get-set 0.5:quit" \
    "fighter 3 given command 1744 on turn 1|action 0 - 3 -> 3, command 1744|a gene switch - command 1744 by fighter 3 (camera 2, effect 51)|turn 2, the get-set" \
    -- --battle 2612 --team-level 30 --cr3-party 1,2,3 --input "guard:party foe:1:3:1744 battle guard guard guard"
unset CR3_BATTLE_SEED
