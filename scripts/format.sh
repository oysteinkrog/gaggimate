#!/usr/bin/env bash
set -euo pipefail

# Usage: scripts/format.sh [--check]
#   (no args)  rewrite files in place
#   --check    report drift and exit non-zero, changing nothing (what CI runs)
#
# Set CLANG_FORMAT to pin a specific binary. Formatting output is not stable
# across clang-format major versions, so the CI gate pins the same version this
# tree was formatted with (14) rather than taking whatever the runner ships.
# Locally, an unpinned `clang-format` is fine as long as it is 14.x.
CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"

mode=(-i)
if [[ "${1:-}" == "--check" ]]; then
    mode=(--dry-run -Werror)
elif [[ $# -gt 0 ]]; then
    echo "usage: $0 [--check]" >&2
    exit 2
fi

# The two excluded trees are not ours to reformat: src/display/ui holds the
# EEZ Studio generated sources, and src/display/drivers holds vendor panel and
# touch drivers that are updated by re-vendoring. Running clang-format over
# them produces a diff that comes straight back the next time either is
# regenerated.
#
# The exclusions used to be written as './src/display/ui/**/*'. find prints
# paths rooted at the arguments it was given, so a search starting at `src`
# emits `src/display/...` and nothing ever matched a pattern anchored at `./`.
# Both trees were being reformatted: 311 files instead of 207.
find src lib sim \
    \( -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' \) \
    ! -path 'src/display/ui/*' \
    ! -path 'src/display/drivers/*' \
    -print0 | xargs -0 -r "$CLANG_FORMAT" "${mode[@]}"
