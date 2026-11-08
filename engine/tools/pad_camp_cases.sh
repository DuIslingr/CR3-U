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
