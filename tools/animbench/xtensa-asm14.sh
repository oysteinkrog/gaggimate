#!/bin/bash
# Like xtensa-asm.sh, but takes the compiler and every flag from the
# firmware build's own compile_commands.json (env $GM_ASM14_ENV, default
# "display"), instead of a hand-maintained flag list.
#
# Why: a hand-maintained list drifts. The old flag list here never defined
# ESP_PLATFORM, so GM_ANIM_IRAM (src/display/ui/default/bganim/*.cpp,
# guarded by "#if defined(ESP_PLATFORM)") expanded to nothing, and every
# kernel this tool showed was missing the IRAM placement the firmware
# actually builds with, among any other ESP_PLATFORM-only codepath. Reading
# the real command from compile_commands.json means whatever the named env
# was last built with is what this compiles with; a stale or missing
# compile_commands.json fails loudly instead of silently falling back to a
# guess (xtensa_cmd_from_compiledb.py does the extraction).
#
#   ./xtensa-asm14.sh AnimCaustics      # one animation
#   ./xtensa-asm14.sh all               # the fleet + BgAnimCommon
#   GM_ASM14_ENV=display-loadtest ./xtensa-asm14.sh AnimOrbits
#
# Output: xtensa-asm14/<name>.S (annotated assembly), xtensa-asm14/<name>.o
# (proof that every inline-asm block actually assembles, not only emits
# text), and the xtensa_report.py summary on stdout. The resolved compile
# command is printed first, so a mismatch against the firmware ELF starts
# from a command to diff, not a guess. This toolchain runs from a WSL path
# directly, no cmd.exe detour needed.
set -e
cd "$(dirname "$0")"

ENV_NAME="${GM_ASM14_ENV:-display}"
COMPILEDB="$(pwd)/../../.pio/build/${ENV_NAME}/compile_commands.json"
BGANIM_DIR="$(pwd)/../../src/display/ui/default/bganim"
OUT_DIR="$(pwd)/xtensa-asm14"
mkdir -p "$OUT_DIR"

check_compiledb() {
    local src="$1"
    if [ ! -f "$COMPILEDB" ]; then
        echo "error: $COMPILEDB not found." >&2
        echo "Build the '$ENV_NAME' env normally first (e.g. \`pio run -e $ENV_NAME\`) so PlatformIO emits it; this script never builds for you." >&2
        return 1
    fi
    if [ "$COMPILEDB" -ot "$src" ]; then
        echo "error: $COMPILEDB is older than $src." >&2
        echo "Rebuild the '$ENV_NAME' env so the compile database reflects this source, then re-run." >&2
        return 1
    fi
}

build_one() {
    local base="$1"
    local src="${BGANIM_DIR}/${base}.cpp"
    [ -f "$src" ] || { echo "no such source: $src"; return 1; }
    check_compiledb "$src" || return 1

    local suffix="display/ui/default/bganim/${base}.cpp"
    local asm_cmd obj_cmd
    asm_cmd=$(python3 xtensa_cmd_from_compiledb.py "$COMPILEDB" "$suffix" asm "${OUT_DIR}/${base}.S") || return 1
    obj_cmd=$(python3 xtensa_cmd_from_compiledb.py "$COMPILEDB" "$suffix" obj "${OUT_DIR}/${base}.o") || return 1

    echo "# ${base} (env: ${ENV_NAME})"
    echo "$asm_cmd"
    eval "$asm_cmd"
    eval "$obj_cmd"
    python3 xtensa_report.py "${OUT_DIR}/${base}.S"
}

if [ "$1" = "all" ]; then
    for f in "${BGANIM_DIR}"/Anim*.cpp "${BGANIM_DIR}/BgAnimCommon.cpp"; do
        build_one "$(basename "$f" .cpp)"
    done
else
    build_one "${1%.cpp}"
fi
