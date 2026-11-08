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

echo "the Vita's buttons: $problems problems"
[ "$problems" -eq 0 ]
