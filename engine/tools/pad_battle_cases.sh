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
want battle && run last-attack \
    "0.1:until=a_last_attack 1:quit" \
    "a finisher - command 13|a last attack (kind 9, motion 18) - camera Cam_t01_j003_00" \
    -- --battle 22 --team-level 20 --cr3-party 1,2,3 --input "auto"

# BtlFinestArtsDirection (kind 12): the gauge at the party's full advantage (+1000), a participant's Finest Arts -
# the stage hidden, its own camera, white at its frame, under the white home and all shown, the white lifted 1 s on
export CR3_BATTLE_SEED=6 # (its draw: a Finest Arts within 30 s)
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
