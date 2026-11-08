#!/bin/sh
# The Vita's buttons, as the port takes them with the Vita copy as the game: cr3view headless, its
# virtual controller (CR3_PAD_SCRIPT) pressing, each case's log lines looked for in order.
#   pad_vita_test.sh <cr3view> [case ...]   (CR3_VITA_DATA: the Vita copy's folder - without it, skipped)
viewer=$1
shift
[ -n "$CR3_VITA_DATA" ] && [ -d "$CR3_VITA_DATA" ] || { echo "CR3_VITA_DATA not set: skipped"; exit 77; }
work=$(mktemp -d "${TMPDIR:-/tmp}/cr3pad.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/home"
problems=0

# run <case> <pad script> <expected lines, | between> -- <cr3view args>
run() {
    name=$1 pad=$2 want=$3
    shift 3
    [ "$1" = "--" ] && shift
    HOME="$work/home" XDG_DATA_HOME="$work/home" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy CR3_PAD_SCRIPT="$pad" \
        timeout 120 "$viewer" --data "$CR3_VITA_DATA" --mute "$@" > "$work/$name.log" 2>&1
    if awk -v want="$want" 'BEGIN { n = split(want, w, "|"); k = 1 }
        k <= n && index($0, w[k]) { ++k }
        END { if (k <= n) { print "  missing: " w[k]; exit 1 } }' "$work/$name.log"; then
        echo "$name: ok"
    else
        echo "$name: FAILED"
        grep -E "test controller|field:|camp|choice|battle|title|shop" "$work/$name.log" | grep -v pressed | tail -20 | sed 's/^/    /'
        problems=$((problems + 1))
    fi
}

cases="$*"
want() { [ -z "$cases" ] || case " $cases " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }

# CampFieldMenu (FUN_811116b4): △ opens on Camp, ← ↑ / ↓ the cursor round the shown buttons, × shuts, ○ presses
want field-bar && run field-bar "3:north 3.5:left 4:up 4.5:up 5:down 5.5:down 6:south 7:north 7.5:east 9:quit" \
    "field: the buttons opened|field: the cursor on 101|field: the cursor on 108|field: the cursor on 105|field: the cursor on 108|field: the cursor on 101|field: the buttons closed|field: the buttons opened|field: the cursor presses 106|camp: open" \
    -- --area t0101 --flag 26 --flag 27

# SelectTwoChoicesMenu (FUN_8123f2f4, the cursor FUN_8128d1ea): ↓ / ↑ round the options from the first, ○ the one under it
want choice && run choice "2:east 3:east 4.5:down 5:down 5.5:down 6.5:east 8:quit" \
    "choice: the cursor on 2|choice: the cursor on 1|choice: the cursor on 2|choice: 2 of 2" \
    -- --area t0101 --input "call:c111_s110_t01_00101"

# L / R over CR3's menus: OptionMenu's tabs (FUN_81203e54: not round), the gifts' categories (0x81189cf2)
want shoulders && run options-tabs "3:r 3.5:r 4:r 4.5:r 5:l 6:quit" \
    "options: tab 1|options: tab 2|options: tab 3|test controller: r|test controller: l|options: tab 2" \
    -- --area t0101 --flag 26 --input "camp:open camp:button6"
want shoulders && run gift-categories "3:r 3.5:r 4:l 6:quit" \
    "camp: gifts, category 1|camp: gifts, category 2|camp: gifts, category 1" \
    -- --area t0101 --flag 26 --input "camp:open camp:button3"

# ContinueMenu (0x8113c03a): ↑ / ↓ over Give Up / Revive, ○ presses (no gem: told), the question's ↓ No, ○
want continue && run continue "1:until=continue? 1:down 1.5:up 2:down 3:east 4:east 5:up 6:east 6.6:down 7.2:east 8:east 8.6:east 10:quit" \
    "continue: the cursor on 1|continue: the cursor on 0|continue: the cursor on 1|continue: 1 to 4|continue: 4 to 1|continue: 1 to 2|continue: 2 to 1|continue: 1 to 2|given up" \
    -- --battle 2612 --team-level 1 --input "auto ctn:hold"

echo "the Vita's buttons: $problems problems"
[ "$problems" -eq 0 ]
