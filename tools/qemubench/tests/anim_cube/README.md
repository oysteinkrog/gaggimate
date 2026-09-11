Cube kernel verification

main.c contains verbatim copies of cubeFaceSpanAsm, cubePaletteRowAsm, and
cubeFacePixelsRef from src/display/ui/default/bganim/AnimCube.cpp. The C
oracle evaluates the four affine planes independently at each x. Guard
words surround every output. Build and run with:

    cd tools/qemubench
    ./build.sh tests/anim_cube
    ./run.sh build/tests/anim_cube.elf 30

probe/main.c checks the additional PIE forms before the combined kernel:
VMIN.S32, VADDS.S32, VSR.32, and MOVI.32.A. S32 addition saturates at
-INT32_MAX on negative overflow. Production values cannot reach saturation.

Numeric audit against the complete page entry, re-read after implementation:

The 120 s tilt is 0.62 + 0.30*sin(tau*t/120 + 0.4). The 60 s spin is
0.9 + tau*t/60. Speed uses the common exp2 curve. Time is derived from tMs,
with a Q24 product reduced modulo 120,000 ms before float conversion.
The vertices have half-size 88 + 0.64*size in absolute pixels. The six face
orders and rotated normal z values match VERTS and FACES exactly.

Feather is 26 px. Back amplitude is 0.35. AMP is 52 + round(34*glow/100),
CAP is round(1.45*AMP), and faces below amplitude 0.6 are skipped. Each
contribution is truncated before accumulation, preserving Uint8Array
assignment semantics. Q16.16 stores the amplitude-scaled edge planes.
Quantization to a zero x coefficient replaces the page's near-zero normal
test; integer clipping and pixel stepping use the same coefficients.

Background is 56 + round(18*y/(h-1)). Dither is the shared BAYER8 matrix,
lround((value-31.5)*1.5/31.5), indexed by absolute x and y. Glow scales RGB
channels by 184 + round(72*glow/100) before RGB565 quantization through
buildThemeRamp. The final possible index is 54..201, so clamping to 0..255
is redundant. Rendering one cleared row at a time replaces the full page
accumulator and cached background without changing pixels. The page's
ceil(size*1.7321)+26+2 clearing box already encloses every projected face.

Slab at 480x480: field 1920 B, faces 960 B, palette 512 B, bgRow 480 B,
dither 256 B, total 4128 B. Geometry scratch is 180 B in PSRAM. At 240x240
the slab total is 2928 B. Failed init releases every allocation, including
an emergency heap allocation that cannot satisfy vector alignment.

The scalar baseline compiled before writing the kernels had a 16-instruction
face loop and a 12-instruction palette loop. The PIE face body uses 18
instructions per four face pixels. The palette body uses 41 instructions
per eight output pixels. All immediate load-use dependencies are scheduled
away. Those issue counts are only an ideal hot-memory lower bound of 4.5
and 5.125 cycles per pixel, respectively. Per-face setup, row clearing,
cache misses, interrupt preemption, and vector resource stalls need device
timing. No device speedup is claimed.

verification.txt records the clean rebuild command tails and exit statuses.
The remaining device checks are /api/debug/animtest?anim=43 for parity and
production timing against bandRef with useref=1, including 480/240-wide and
interlaced rendering. The assembly flag defaults to 1 and supports 0 as a
fallback. No firmware flash or commit was performed.

Knowledge-base retrieval used qmd query with the requested collection and
limit. The default launcher hit a Windows/Linux native-module mismatch and
the Windows GPU run hit a CUDA error. A Windows CPU query with one rerank
candidate completed successfully and returned lx7-core-pipeline-and-cost-model
at 93%. Before kernel implementation, the local zero-overhead loop, PIE
arithmetic, and PIE hazard/latency leaves were also read.
