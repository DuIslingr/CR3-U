# Sourced by pad_vita_test.sh: run / want / viewer are provided by the runner.
# SShopMenu's spatial cursor reaches Item and VIP, then returns to Box.
want shops && run sp-shop-cursor "3:right 3.5:east 4.2:south 5:down 5.5:east 6.2:south 7:left 7.5:east 9:quit" \
    "shop: the cursor on 1|shop: the goods|shop: the cursor on 2|shop: VIP|shop: the cursor on 0|shop: the boxes" \
    -- --area t0101 --flag 27 --input "camp:field5"

# LuckyBoxMenu (its tutorial seen - flag 143): left/right over buy panels; up/down hands focus to its
# check button - the first box (focus 0) a gene box: its list (GeneBoxListMenu), down scrolls, back shuts it
# (the second press a second after the first: under load the boxes' screen can still swallow one at +0.5 s)
want shops && run sp-box-panels "3:east 4:east 4.5:right 5:right 5.5:left 6:up 6.5:east 7.5:down 8:south 9:down 9.5:south 10:south 11.5:quit" \
    "shop: the boxes|shop: box buy panel 1|shop: box buy panel 2|shop: box buy panel 1|shop: box focus check|shop: box contents (the gene box's list)|shop: the gene box's list - month|shop: the gene box's list at line 1|shop: the gene box's list closed|shop: box focus buy|shop: leaving the boxes|shop: back from the boxes" \
    -- --area t0101 --flag 27 --flag 143 --input "camp:field5"

# Round 30: LuckyBoxMenu's slide (Update 0xacf37c states 1 / 2, 0.3 s): → to the next box, → again mid-slide not taken
# (+0xd8 clear), the focus one on at its end; ← back to the first
want shops && run sp-box-slide "3:east 4:right 4.1:right 5:left 6:quit" \
    "shop: the boxes - 2 (0 in focus)|shop: box 1 in focus|shop: box 0 in focus" \
    -- --area t0101 --flag 27 --flag 143 --input "camp:field5"

# CreateLuckyBoxMenu: the first visit (flag 143 clear) - the TutorialMenu (AnnounceTutorialTxt 0x144), read through
want shops && run sp-box-tutorial "3:east 3.5:east 4:east 4.5:east 5:east 5.5:east 6:east 6.5:east 7:east 7.5:east 8:east 8.5:east 9:east 10:quit" \
    "(0 in focus)|shop: the boxes' tutorial|shop: the boxes' tutorial read" \
    -- --area t0101 --flag 27 --input "camp:field5"

# MyPageNewTopMenu: the hidden Story button skipped; the initial daily present may show.
want mypage && run my-page-cursor "3:down 3.5:down 4:down 4.5:up 5:east 6:south 7:quit" \
    "my page: open|my page: the cursor on 4|my page: the cursor on 2|my page: the cursor on 4" \
    -- --area t0101 --input "camp:mypage"

# The fresh profile's daily present: choose Cancel with Right and confirm, leaving it unclaimed.
want mypage && run my-page-present "3:down 3.5:east 4:right 4.5:east 6:quit" \
    "my page: presents|my page: present button 1|my page: presents closed" \
    -- --area t0101 --input "camp:mypage"

# VsModeMenu: the six-place cursor, not just the two number-key shortcuts.
# (a player past the VS tutorial - vs.nut's VStuto_Clear: 3501 .. 3515, 3521 .. 3523, 3619, 33 - 35 -,
# so VS opens on its menu: evt000 reserves no tutorial vista, MP and My Page are up)
vs_done=$(for f in 3501 3502 3503 3504 3505 3506 3507 3508 3509 3510 3511 3512 3513 3514 3515 3521 3522 3523 3619 33 34 35; do printf ' --flag %d' $f; done)
want vs && run vs-cursor "3:east 3.5:down 4:east 4.5:south 5:down 5.5:right 6:down 6.5:east 7.5:south 9:quit" \
    "vs: open|vs: the cursor presses 0|vs: the cursor on 1|vs: the cursor presses 1|special tournaments|vs: the cursor on 3|vs: the cursor on 5|vs: the cursor presses 5|my page: open" \
    -- --area t0101 --input "vs:open" $vs_done

# TournamentDitailMenu L/R changes the tournament without leaving the detail.
want vs && run vs-detail "3:east 3.5:east 4:east 4.5:r 5:l 5.5:south 6:left 7:quit" \
    "normal tournaments|vs: tournament detail 1|vs: tournament detail 0" \
    -- --area t0101 --input "vs:open" $vs_done

# VsModeMain 8: back from the Sp Shop - DBSystem::Sync(0x84), SyncProperty -, VsModeMenu's comHeAutoSave_fr00 for 2 s
want vs && run vs-autosave "3:down 3.5:east 4.5:south 6:quit" \
    "vs: the cursor presses 1|vs: sp shop|vs: back from the Sp Shop - SyncProperty, the autosave icon for 2 s" \
    -- --area t0101 --input "vs:open" $vs_done

# The VS tutorial (round 10): a fresh game's VS - vs.nut's evt000 reserves s910_t01_00101_evt; its
# gene cards (CreateGeneCardSelector: one put in focus, then taken), the gene's status, VS again:
# s910_t01_00201_evt's camp party window (its back asking 0x1f0 - the party wears its genes -, ○ on
# Yes), its VsBattleIn - tournament 1's battles after the vista
want vs && run vs-tutorial "0:until=gene_selector:_genes 1:right 1.5:east 2:east 2.5:until=gene:_status_4 1:east 1.5:until=opened_by_the_event 1:south 1.5:east 2:until=reserved_tournament 1:quit" \
    "vs: to the vista s910_t01_00101_evt|gene selector: genes 1, 4, 7|gene selector: card 1 in focus (gene 4)|gene selector: card 1 taken (gene 4)|gene: status 4 at level 1|vs: to the vista s910_t01_00201_evt|camp: opened by the event|camp: is this the party? (0x1f0)|camp: the party chosen (0x1f0 Yes)|camp: closed (CR3)|vs: battle reserved - tournament 1|vs: the reserved tournament 1's battles" \
    -- --area t0101 --cr3-party 1 --page 0.05 --input "auto vs:open"
# After the first tournament (3501, 3502): VS_Top's s910_t01_00701 vista (the party joins), VS again,
# VS_Top's 00601 under OverLayMain's mask - PleaseCampPush: only the camp's button not shut
want vs && run vs-tutorial-camp "0:until=event_ended 0.1:until=vs_script:_ended 1:east 1.5:quit" \
    "vs: to the vista s910_t01_00701_evt|event ended|vs: script VS_Top|vs script: ended|vs: button 1 shut" \
    -- --area t0101 --cr3-party 1 --page 0.05 --flag 3501 --flag 3502 --input "vs:open"
# Round 28: the colosseum's commentary through tournament 1 (commentators 1, 2; flag 7001 - battle_2001's start message
# seen): the battle-in lines, category 6 cut on the first get-set while they still speak, the after-damage cuts (0xe: the
# battle decided), the win's 0x7c, PlayContinue's 0x83 cut and the next battle once it is quiet, the get-set's 0x86 / 0x87,
# the perfect's 0x7f, 0x80, 0x82
export CR3_BATTLE_SEED=1
want vs && run vs-commentary "0.1:until=tournament_over 1:quit" \
    "vs commentary: commentators 1, 2|vs commentary: request 1 (1.0 s)|turn 1, the get-set|vs commentary: cut 6|vs commentary: cut 14|battle 2001: won|vs commentary: request 124|vs commentary: cut 131|vs: on to battle 2 (the commentary quiet)|vs commentary: request 134|vs: on to battle 3 (the commentary quiet)|vs commentary: request 135|vs commentary: request 127|vs commentary: request 128|vs commentary: request 130" \
    -- --area t0101 --team-level 60 $vs_done --flag 7001 --input "vs:open wait:3 vs:battle wait:2 vs:normal wait:2 vs:row0 wait:1 vs:ok wait:1 vs:yes auto wait:3 vs:wild0 vs:next wait:3 vs:wild1 vs:next wait:3 vs:wild2 vs:next"
unset CR3_BATTLE_SEED
