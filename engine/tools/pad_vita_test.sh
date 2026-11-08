#!/bin/sh
# The Vita's buttons, as the port takes them with the Vita copy as the game: cr3view headless, its
# virtual controller (CR3_PAD_SCRIPT) pressing, each case's log lines looked for in order.
#   pad_vita_test.sh <cr3view> [case ...]   (CR3_VITA_DATA: the Vita copy's folder - without it, skipped)
viewer=$1
shift
[ -n "$CR3_VITA_DATA" ] && [ -d "$CR3_VITA_DATA" ] || { echo "CR3_VITA_DATA not set: skipped"; exit 77; }
work=$(mktemp -d "${TMPDIR:-/tmp}/cr3pad.XXXXXX") || exit 1
trap 'if [ "$problems" -eq 0 ]; then rm -rf "$work"; else echo "failed test logs: $work"; fi' EXIT
mkdir -p "$work/home"
problems=0

# run <case> <pad script> <expected lines, | between> -- <cr3view args>
run() {
    name=$1 pad=$2 want=$3
    shift 3
    [ "$1" = "--" ] && shift
    HOME="$work/home" XDG_DATA_HOME="$work/home" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy CR3_PAD_SCRIPT="$pad" \
        timeout 120 "$viewer" --data "$CR3_VITA_DATA" --mute "$@" > "$work/$name.log" 2>&1
    status=$?
    if [ "$status" -eq 0 ] && awk -v want="$want" 'BEGIN { n = split(want, w, "|"); k = 1 }
        k <= n && index($0, w[k]) { ++k }
        END { if (k <= n) { print "  missing: " w[k]; exit 1 } }' "$work/$name.log"; then
        echo "$name: ok"
    else
        echo "$name: FAILED"
        echo "  viewer exit: $status"
        tail -25 "$work/$name.log" | sed 's/^/    /'
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

# Fld2CameraGom::UpdateControl (0x810c4f9a): the right stick as the camera's touch - 1 s full right 1.5 rad,
# 0.4 down 0.45 rad a second, the tilt kept within 20 degrees
want camera && run right-stick "6:rx=1 7:rx=0 8:ry=-0.4 9:ry=0 10:quit" \
    "yaw -1.50, pitch 0.30|yaw -1.50, pitch 0.35" \
    -- --area d0301

# TitleMenu (0x81256cde): ○ or START its tap
want title && run title-start "3:start 5:quit" "title: START|title: tapped" -- --title

want camera && run camera-behind "5:rx=1 6:rx=0 6.5:l 7.5:quit" \
    "camera: the right stick let go|camera: behind the player" -- --area d0301
want saves && run save-footer "3:start 4:down 4.5:up 5:right 5.5:down 6:quit" \
    "title: save focus 1|title: save footer copy|title: save footer delete|title: save focus 1" -- --title
want movies && run movie-pad "3:north 4:east 5:north 6:west 7:quit" \
    "movie: paused|test controller: east|movie: resumed|movie c01: skipped" -- --movie c01 --area t0101
want goods && run field-goods "3:r 3.5:r 4:west 5:quit" \
    "field: goods cursor 1|field: goods cursor 0|field: goods uses 0" \
    -- --area t0101 --input "goods:show0 goods:show1"
want lunch && run lunch-balloons "0.1:until=the_balloons,_at_frame 0.5:up 1:down 1.5:east 4:quit" \
    "lunch: the balloons, at frame|lunch: the cursor on|lunch: the cursor on|lunch: s001_t02_00301_" \
    -- --area t0101 --page 0.01 --input "lunch:start:s001_t02_00301_evt:s001_t02_00301"

case_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
for group in shop camp battle; do
    [ ! -f "$case_dir/pad_${group}_cases.sh" ] || . "$case_dir/pad_${group}_cases.sh"
done

echo "the Vita's buttons: $problems problems"
[ "$problems" -eq 0 ]
