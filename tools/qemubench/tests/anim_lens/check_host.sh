#!/bin/bash
# Exercise the kernel wrappers under ASan/UBSan on the host.
set -euo pipefail
lens_root="$(cd "$(dirname "$0")/../../../.." && pwd)"
lens_out="$lens_root/tools/qemubench/build/tests/anim_lens/host_parity"
mkdir -p "$(dirname "$lens_out")"
python3 - "$lens_root" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
src = (root / 'src/display/ui/default/bganim/AnimLens.cpp').read_text()
test = (root / 'tools/qemubench/tests/anim_lens/main.c').read_text()
start = 'GM_ANIM_IRAM __attribute__((noinline)) uint32_t lensRunPairsAsm'
assert src[src.index(start):src.index('#else\n// Portable twins')] == test[test.index(start):test.index('static uint16_t blend_ref')]
start = 'GM_ANIM_IRAM void featherAsm'
wrapper = src[src.index(start):src.index('#endif // GM_BGANIM_LENS_ASM')]
for name in ('stage8', 'pieConst', 'out'):
    wrapper = wrapper.replace('reinterpret_cast<uintptr_t>(' + name + ')', '((uintptr_t)' + name + ')')
wrapper = wrapper.replace('static_cast<int>((16 - (((uintptr_t)out) & 15)) & 15)', '((int)((16 - (((uintptr_t)out) & 15)) & 15))')
assert test[test.index(start):test.index('static uint32_t run_ref')].rstrip() == wrapper.rstrip()
print('PASS production/QEMU copies: three kernels verbatim; featherAsm differs only in C++ cast syntax')
PY
/usr/bin/g++ -O1 -g -std=gnu++17 -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$lens_root" -I"$lens_root/tools/animbench/shim" \
    "$lens_root/tools/qemubench/tests/anim_lens/host_parity.cc" \
    "$lens_root/src/display/ui/default/bganim/BgAnimCommon.cpp" \
    -o "$lens_out" -lm
ASAN_OPTIONS=verify_asan_link_order=0:detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$lens_out"
