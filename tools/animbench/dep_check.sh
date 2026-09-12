#!/usr/bin/env bash
# Does a firmware header change still rebuild the host binaries?
#
# build/ramp_dump is the oracle that proves web/src/config/gradientRamp.js
# computes the same palette as the firmware. It shipped with its project
# headers missing from the Makefile's prerequisites, so a change to rgb565 in
# BgAnimCommon.h left `make check` comparing the JS against a binary built
# before the change, and the check passed while the two implementations
# disagreed (gm-nov3.9). The Makefile now writes its own header list during the
# build. This is the guard that the list is really being used.
#
# Method, the same one the review used: `make -n -W <header> <binary>` asks
# make what it would do if only that header had just changed. An up to date
# binary that prints no recipe is not tracking the header.
#
# Run from tools/animbench. Exits non-zero on the first header that is not
# tracked.
set -u
cd "$(dirname "$0")" || exit 1
unset MAKEFLAGS  # so a parent make's own flags cannot reach these runs

ANIM_DIR=../../src/display/ui/default/bganim

# The headers the palette arithmetic is actually made of. BgAnimCommon.h
# defines rgb565, BgAnim.h the stop limits and the wire parser's declarations,
# BgAnimThemeTable.h the generated built-in gradients.
HEADERS="$ANIM_DIR/BgAnimCommon.h $ANIM_DIR/BgAnim.h $ANIM_DIR/BgAnimThemeTable.h"
BINS="build/ramp_dump build/bench build/interlace_check"

fail=0
for bin in $BINS; do
    # -W proves nothing about a binary that is out of date anyway.
    make "$bin" >/dev/null || exit 1
    for hdr in $HEADERS; do
        if make -n -W "$hdr" "$bin" | grep -q '^g++'; then
            echo "ok:   $bin rebuilds when $(basename "$hdr") changes"
        else
            echo "FAIL: $bin does not rebuild when $(basename "$hdr") changes"
            fail=1
        fi
    done
done

if [ "$fail" -ne 0 ]; then
    echo "dep check FAIL: a host binary is not tracking the firmware headers it compiles"
    exit 1
fi
echo "dep check OK: every host binary rebuilds on a change to the firmware headers it compiles"
