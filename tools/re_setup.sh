#!/bin/bash
# Builds CR3-U's local reverse-engineering groundwork in re/ (never committed) from your own copies:
#   tools/re_setup.sh <ChaosRings3_FINAL_ENG.app> <unpacked Android base.apk> <Vita PCSG00500 folder> [part...]
# parts: android ios vita (default all; ios and vita need android's features.jsonl).
# Needs llvm-nm / llvm-objdump and Ghidra (GHIDRA=/opt/ghidra by default). The Android build (1.1.3,
# arm64 libcrx.so) exports its functions by name; the iPhone/iPad build (1.2.2, arm64) and the
# Vita's (PCSG00500, Thumb-2) are stripped and get Android's names by matching. Produces:
#   re/android/arm64/  libcrx.so: symbols.txt, decomp.c, functions.tsv, features.jsonl, ghidra/
#   re/android/dex/    classes.dex (the Java shell)
#   re/ios/            ChaosRings3_FINAL_ENG (arm64) symbols, stubs, names_from_android.tsv, decomp.c
#   re/vita/           eboot.elf + imports.tsv (tools/vita_self.py), names_from_android.tsv, decomp.c
# The Vita's eboot.bin must be a decrypted SELF (the PCSG00500 install is; the Prequel Trilogy's
# CR3.self is still NPDRM-encrypted). Vita import names come from re/vita/nids_vita3k.tsv when present.
set -e
[ $# -ge 3 ] || { sed -n 2,3p "$0"; exit 1; }
R="$(cd "$(dirname "$0")/.." && pwd)"; T="$R/tools"; G="${GHIDRA:-/opt/ghidra}/support/analyzeHeadless"
export GHIDRA_HEADLESS_MAXMEM="${GHIDRA_HEADLESS_MAXMEM:-12G}"
APP="$1"; APK="$2"; V="$3"; shift 3
PARTS="${*:-android ios vita}"
has() { [[ " $PARTS " == *" $1 "* ]]; }
A="$R/re/android/arm64"
# ios and vita may run alongside android (three invocations): they wait for its features to match
wait_android() { until [ -f "$A/.done" ]; do sleep 30; done; }

if has android; then
    mkdir -p "$A/ghidra" "$R/re/android/dex/ghidra"; rm -f "$A/.done"
    echo "ghidra: android arm64"
    so="$APK/lib/arm64-v8a/libcrx.so"
    llvm-nm -D -n --defined-only "$so" > "$A/symbols.txt"
    "$G" "$A/ghidra" cr3_arm64 -import "$so" -overwrite -scriptPath "$T/ghidra" \
        -postScript ExportDecomp.java "$A" -postScript ExportFeatures.java "$A/features.jsonl" > "$A/ghidra.log" 2>&1
    echo "ghidra: android dex"
    for dex in "$APK"/classes*.dex; do
        n="$(basename "$dex" .dex)"
        "$G" "$R/re/android/dex/ghidra" "$n" -import "$dex" -overwrite -scriptPath "$T/ghidra" \
            -postScript ExportDecomp.java "$R/re/android/dex/$n" > "$R/re/android/dex/$n.log" 2>&1
    done
    touch "$A/.done"
fi

if has ios; then
    mkdir -p "$R/re/ios/ghidra"
    echo "ghidra: iOS"
    B="$R/re/ios/ChaosRings3_FINAL_ENG.arm64"
    cp "$APP/ChaosRings3_FINAL_ENG" "$B"
    llvm-nm -n "$B" > "$R/re/ios/symbols.txt" || true
    llvm-objdump --macho --indirect-symbols "$B" | awk '/^Indirect symbols for/ {s=$4} /^0x/ && s ~ /stub/ {print $1, $3}' > "$R/re/ios/stubs.txt"
    "$G" "$R/re/ios/ghidra" cr3ios -import "$B" -overwrite -scriptPath "$T/ghidra" \
        -postScript ExportFeatures.java "$R/re/ios/features.jsonl" > "$R/re/ios/ghidra.log" 2>&1
    wait_android
    python3 -I "$T/ghidra/match_functions.py" "$A/features.jsonl" "$R/re/ios/features.jsonl" \
        "$R/re/ios/names_from_android.tsv" > "$R/re/ios/match.log"
    "$G" "$R/re/ios/ghidra" cr3ios -process "$(basename "$B")" -noanalysis -scriptPath "$T/ghidra" \
        -postScript ApplyNames.java "$R/re/ios/names_from_android.tsv" "Android" -postScript ExportDecomp.java "$R/re/ios" > "$R/re/ios/apply.log" 2>&1
fi

if has vita; then
    mkdir -p "$R/re/vita/ghidra"
    echo "ghidra: Vita"
    NIDS=(); [ -f "$R/re/vita/nids_vita3k.tsv" ] && NIDS=("$R/re/vita/nids_vita3k.tsv")
    python3 -I "$T/vita_self.py" "$V/eboot.bin" "$R/re/vita/eboot.elf" "$R/re/vita/imports.tsv" "${NIDS[@]}" > "$R/re/vita/self.log"
    "$G" "$R/re/vita/ghidra" cr3vita -import "$R/re/vita/eboot.elf" -overwrite -scriptPath "$T/ghidra" \
        -preScript VitaSetup.java "$R/re/vita/imports.tsv" > "$R/re/vita/ghidra.log" 2>&1
    "$G" "$R/re/vita/ghidra" cr3vita -process eboot.elf -scriptPath "$T/ghidra" -preScript VitaFindFunctions.java \
        -postScript ExportFeatures.java "$R/re/vita/features.jsonl" > "$R/re/vita/ghidra2.log" 2>&1
    wait_android
    python3 -I "$T/ghidra/match_functions.py" "$A/features.jsonl" "$R/re/vita/features.jsonl" \
        "$R/re/vita/names_from_android.tsv" --cross > "$R/re/vita/match.log"
    "$G" "$R/re/vita/ghidra" cr3vita -process eboot.elf -noanalysis -scriptPath "$T/ghidra" \
        -postScript ApplyNames.java "$R/re/vita/names_from_android.tsv" "Android" -postScript ExportDecomp.java "$R/re/vita" > "$R/re/vita/apply.log" 2>&1
fi
grep -h "^matched" "$R/re/ios/match.log" "$R/re/vita/match.log" 2>/dev/null || true
echo "done: $PARTS"
