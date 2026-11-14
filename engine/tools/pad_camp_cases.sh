#!/bin/sh
# Sourced by pad_vita_test.sh: run/want and the virtual-controller test fixture.
want camp && run camp-panels "3:left 3.5:left 4:hold=east 4.5:right 5:release=east 5.5:right 6:quit" \
    "camp: open (CR3)|camp: party panel 1|camp: party panel 0|camp: party order moved 0 to 1|camp: party panel -1" \
    -- --area t0101 --flag 26 --cr3-party 1,2 --input "camp:open"

want camp && run camp-sound "3:down 3.5:left 4:down 4.5:left 5:down 5.5:east 7:quit" \
    "options: sound row 1|options: sound row 2|options: sound row 2 footer" \
    -- --area t0101 --flag 26 --input "camp:open camp:button6"

want camp && run camp-equipment "3:west 3.5:west 5:quit" \
    "camp: equipment (1)|camp: equipment character 2|camp: equipment character 1" \
    -- --area t0101 --flag 26 --cr3-party 1,2 --input "camp:open camp:button1"

want camp && run camp-genes "4:r 4.5:l 5:west 5.5:west 6:quit" \
    "camp: 1's gene|camp: gene tab Skill|camp: gene tab Gene|camp: gene sort 1|camp: gene sort 2" \
    -- --area t0101 --flag 26 --flag 41 --flag 21 --cr3-party 1,2 --cr3-gene 1:1 --cr3-gene 2:2 \
    --input "camp:open camp:button2 camp:row0"

# FutterMenu's Skills (round 27, the Vita's R): GeneSkillListMenu in the cards' place - the gene's eight, then those its levels
# still to come teach (@ the level: dimmed, "Learn at Level {level}"); ↓ its rows
want camp && run camp-gene-skills "4:r 4.5:down 5:down 6:quit" \
    "camp: gene tab Skill|camp: gene skills (9): 0:241 0:201 0:311 1:41 0:259 1:8 0:247@20 0:318@40 1:121@60|camp: gene skill row 1|camp: gene skill row 2" \
    -- --area t0101 --flag 26 --flag 41 --flag 21 --cr3-party 1,2 --cr3-gene 1:3:3704 --input "camp:open camp:button2 camp:row0"

want camp && run camp-reels "3:up 3.5:right 4:down 4.5:right 5:right 5.5:east 7:quit" \
    "camp: item 2 chosen|camp: item count 11|camp: item count focus 1|camp: item count 10|camp: item count focus 2|camp: item count focus 3|camp: item count cancelled" \
    -- --area t0101 --flag 26 --cr3-item 2:30 --input "camp:open camp:button0 camp:row0"

want camp && run camp-gene-lock "4:left 4.5:east 5:east 5.5:right 6:east 6.5:south 8:quit" \
    "camp: 1's gene|camp: gene 1 locked|camp: gene 1 unlocked|gene: status 1" \
    -- --area t0101 --flag 26 --flag 41 --flag 21 --cr3-party 1,2 --cr3-gene 1:1 \
    --input "camp:open camp:button2 camp:row0"

want camp && run camp-index "3:right 3.5:down 4:down 4.5:down 5:left 5.5:up 6:quit" \
    "my page: gene index|gene index: cursor 1|gene index: cursor 3|gene index: cursor 5|gene index: cursor 5 tabs" \
    -- --area t0101 --flag 27 --input "camp:mypage camp:id204"

# InterfaceWindow.SetCampWindowParty over a quest's vista (q074_d05_00104_evt: LockPanel(0), then the
# camp's top in its event mode, the script waiting on CheckAliveCampWindowParty): ○ held moves a panel
# past slot 0's locked one (TouchSimpleMove), the locked one isn't taken up (CheckTopArea), × asks
# 0x1f0 (CampTopMenu::SetParamNumber 2: every member wearing a gene), ○ on Yes closes the camp after
# 0.233 s and the vista goes on to its battle
event_genes="--cr3-gene 1:1 --cr3-gene 2:2 --cr3-gene 3:3 --cr3-gene 4:4 --cr3-gene 5:5 --cr3-gene 6:6"
want camp && run camp-event-lock "0:until=opened_by_the_event 0.5:left 1:left 1.5:left 2:left 2.5:hold=east 3:left 3.5:right 4:release=east 4.5:left 5:left 5.5:hold=east 6:right 6.5:release=east 7:south 7.5:east 12:quit" \
    "camp: party slot 0's panel locked|camp: opened by the event|camp: party panel 1|camp: party panel 1|camp: party order moved 1 to 2|camp: party panel 0 is locked|camp: is this the party? (0x1f0)|camp: the party chosen (0x1f0 Yes)|camp: closed (CR3)|event ended at frame" \
    -- --event d05:q074_d05_00104_evt --page 0.3 $event_genes

# The event mode's question (CampTopMenu::Update 0xa38130 state 7): No (com_004) back to the top; × its
# hard return - state 6, the buttons not polled (○ on Equipment does nothing) -, × again asking again,
# Yes closing. A member without a gene: the alert 0x307 instead, the camp staying
want camp && run camp-event-ask "0:until=opened_by_the_event 0.5:south 1:down 1.5:east 2:south 2.5:south 3:down 3.5:east 4:south 4.5:east 10:quit" \
    "camp: is this the party? (0x1f0)|camp: the party not chosen (0x1f0 No)|camp: is this the party? (0x1f0)|camp: the question put away|camp: is this the party? (0x1f0)|camp: the party chosen (0x1f0 Yes)|camp: closed (CR3)|event ended at frame" \
    -- --event d05:q074_d05_00104_evt --page 0.3 $event_genes
want camp && run camp-event-nogene "0:until=opened_by_the_event 0.5:south 1:east 1.5:south 3:quit" \
    "camp: opened by the event|camp: a member without a gene - 0x307|camp: a member without a gene - 0x307" \
    -- --event d05:q074_d05_00104_evt --page 0.3

# An event's gene windows (round 9; s481_d16_00104_evt's reward, gene 378 at level 100): with room,
# CreateDisposalEquipMenu_Gene is done at once and CreateGeneStatusSCModalMenu's status (GeneStatusMenu
# 0xa7) waits for a press; with 50 held, DisposalEquipMain's 0x218 told, a gene picked, 0x236 asked,
# RemoveGene, 0x237 told - then the reward and its status
want camp && run gene-event-status "0:until=at_level_100 1:east 15:quit" \
    "gene disposal: room in the box|gene: status 378 at level 100|gene: status closed|event ended at frame" \
    -- --event d16:s481_d16_00104_evt --page 0.05
full_box=$(k=1; while [ $k -le 50 ]; do printf ' --cr3-gene 0:%d' $((k % 20 + 1)); k=$((k + 1)); done)
want camp && run gene-event-disposal "0:until=a_gene_to_discard 1:east 2:right 2.5:east 3.5:east 4.5:east 5.5:until=at_level_100 1:east 15:quit" \
    "gene disposal: the box full (50)|- 49 held|gene disposal: done|gene: status 378 at level 100|gene: status closed|event ended at frame" \
    -- --event d16:s481_d16_00104_evt --page 0.05 $full_box

# OptionMain's Credits / Copyright Notice (round 26: the Other tab's rows, StaffNameListMenu - Staff2's 854 lines,
# Copyright's 668; ↓ a line, × back to the options)
want camp && run options-credits "0:until=camp:_options 0.5:r 1:r 1.5:down 2:down 2.5:down 3:east 4:down 4.5:down 5.5:south 6.5:down 7:east 8:quit" \
    "options: tab 2|options: other row 3|options: credits (854 lines)|staff list: line 2|staff list: line 3|options: credits closed|options: other row 4|options: copyright notice (668 lines)" \
    -- --area d0301 --flag 26 --input "camp:open camp:button6"

# The Quest Clear screen (QuestDitailMenu 0x7e - d02's quest_complete for quest 50): OK, its 1/6 s close, the script on
quest_pad="$(k=1; while [ $k -le 24 ]; do printf "%d:east " $k; k=$((k + 1)); done)26:quit" # (the area's tutorials tapped through, then OK)
want camp && run quest-clear "$quest_pad" \
    "area d0202: quest_complete|quest clear: OK|quest clear: closed" \
    -- --area d0202 --player j001 --work 24:50 --input "wait:1 call:quest_complete"

# OptionFooterMenu (round 27: the field's Options - mpMe panels Title Screen, Download's empty frame, Manual, App Intro,
# Achievements, then the back): ↓ to the footer on the back, ← over the panels - Download passed over -, ○ Title Screen asks
# 0x1a3, ← Yes, ○: the title again (GameMain::Reboot)
want camp && run options-footer "3:down 3.5:down 4:down 4.5:left 5:left 5.5:left 6:left 6.5:east 7.5:left 8:east 10:quit" \
    "options: sound row 2 footer|options: footer focus 4|options: footer focus 3|options: footer focus 2|options: footer focus 0|options: footer panel 0 (0x193)|options: to the title screen (0x1a3 Yes)|title: CR3's" \
    -- --area t0101 --flag 26 --input "camp:open camp:button6"
