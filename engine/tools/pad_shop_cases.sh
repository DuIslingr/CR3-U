# Sourced by pad_vita_test.sh: run / want / viewer are provided by the runner.
# SShopMenu's spatial cursor reaches Item and VIP, then returns to Box.
want shops && run sp-shop-cursor "3:right 3.5:east 4.2:south 5:down 5.5:east 6.2:south 7:left 7.5:east 9:quit" \
    "shop: the cursor on 1|shop: the goods|shop: the cursor on 2|shop: VIP|shop: the cursor on 0|shop: the boxes" \
    -- --area t0101 --flag 27 --input "camp:field5"

# LuckyBoxMenu: left/right over buy panels; up/down hands focus to its contents button.
want shops && run sp-box-panels "3:east 3.5:east 4:right 4.5:right 5:left 5.5:up 6:east 6.5:right 7:south 7.5:down 8:south 8.5:south 10:quit" \
    "shop: the boxes|shop: box buy panel 1|shop: box buy panel 2|shop: box buy panel 1|shop: box focus check|shop: box contents|shop: box contents page 2|shop: box focus buy" \
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
want vs && run vs-cursor "3:east 3.5:down 4:east 4.5:south 5:down 5.5:right 6:down 6.5:east 7.5:south 9:quit" \
    "vs: open|vs: the cursor presses 0|vs: the cursor on 1|vs: the cursor presses 1|special tournaments|vs: the cursor on 3|vs: the cursor on 5|vs: the cursor presses 5|my page: open" \
    -- --area t0101 --input "vs:open"

# TournamentDitailMenu L/R changes the tournament without leaving the detail.
want vs && run vs-detail "3:east 3.5:east 4:east 4.5:r 5:l 5.5:south 6:left 7:quit" \
    "normal tournaments|vs: tournament detail 1|vs: tournament detail 0" \
    -- --area t0101 --input "vs:open"
