#!/usr/bin/env bash
set -euo pipefail

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
    -print0 | xargs -0 -r clang-format -i
