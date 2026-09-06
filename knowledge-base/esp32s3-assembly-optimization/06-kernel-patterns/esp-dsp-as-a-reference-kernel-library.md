---
title: Espressif's esp-dsp as a reference kernel library
id: 06-kernel-patterns/esp-dsp-as-a-reference-kernel-library
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, pie, esp-dsp, kernel-patterns, fixed-point, benchmarks]
confidence: medium
---

# Espressif's esp-dsp as a reference kernel library

Espressif publishes and maintains `esp-dsp`, a DSP library covering ESP32,
ESP32-S3 and ESP32-P4[^1]. It is the largest open-source body of
hand-written ESP32-S3 PIE assembly that Espressif itself ships and
supports, so it is the best available reference for how the vendor's own
engineers write, dispatch and test PIE kernels. This document covers what
the library holds, how it dispatches per chip, its assembly conventions,
its own benchmarks and tests, its licence, and where its patterns stop
being a template. Checked against a local clone at tag `v1.8.2`, commit
`a53a0756833c045311ea1d79a2badf495cdfde4c`[^2].

## What the library contains

Code is grouped by function family under `modules/`[^2]: `dotprod/` (8/16-bit
fixed-point and float dot products, plus an image variant `dspi_*` that
dot-products a filter against a strided 2D window), `fir/` (FIR,
decimating/interpolating FIR, a polyphase resampler), `iir/` (biquad,
direct-form II and its transposed "s" variant), `fft/` (radix-2 and
radix-4 complex FFT, float and 16-bit fixed `sc16`), `conv/` (convolution,
correlation, circular correlation, float only), `matrix/` (multiply, add,
subtract, add-constant, multiply-constant; float plus one 16-bit
fixed-point multiply), `math/` (elementwise add/sub/mul/mulc/addc/sqrt,
float and fixed), `kalman/` (built on `matrix/`), `dct/` (built on
`fft/`), `windows/` (Hann, Blackman-Harris, etc.), `support/` (tone/noise
generation, SNR/SFDR measurement, a spectrum viewer, and `mem/`, a
memcpy/memset pair with an ESP32-S3-specific path), and `common/` (error
codes, platform macros, the top-level `esp_dsp.h`). A family with more
than one input type or layout goes one level deeper (`matrix/mul/`,
`matrix/add/`), each with its own `float/`, `fixed/`, `include/`, `test/`.

## Which modules carry ESP32-S3-specific (PIE) assembly

Every optimized file's suffix names its target: `_ansi` (portable C),
`_ae32` (ESP32's Xtensa config, which has MAC16 and FP but no PIE), `_aes3`
(ESP32-S3, PIE `ee.*` instructions), `_arp4` (ESP32-P4)[^2]. `find modules
-name '*_aes3*.S'` at this tag lists 29 files: the fixed-point dot products
(`dsps_dp_s8_aes3.S` and the eight `dspi_dotprod_*_aes3.S` image
variants, four plain and four `_off_`), the float dot product
(`dsps_dotprod_f32_aes3.S`), FIR (`dsps_fird_s16_aes3.S`,
`dsps_fir_f32_aes3.S`, `dsps_fird_f32_aes3.S`), biquad
(`dsps_biquad_f32_aes3.S`), FFT
(`dsps_fft2r_sc16_aes3.S`, `dsps_bit_rev_lookup_fc32_aes3.S`,
`dsps_fft2r_fc32_aes3_.S`, `dsps_fft4r_fc32_aes3_.S`), matrix multiply
(`dspm_mult_f32_aes3.S`, `dspm_mult_ex_f32_aes3.S`,
`dspm_mult_s16_aes3.S`), elementwise math
(`dsps_add_s16_aes3.S`, `dsps_add_s8_aes3.S`, `dsps_sub_s16_aes3.S`,
`dsps_sub_s8_aes3.S`, `dsps_mul_s16_aes3.S`, `dsps_mul_s8_aes3.S`), and
`modules/support/mem/esp32s3/dsps_memcpy_aes3.S` /
`dsps_memset_aes3.S`[^2]. Two of these do not match `'*_aes3.S'`, because
the FFT pair carries a trailing underscore; there is no `_aes3` file for
`dsps_dotprode_f32` or for the transposed biquad `dsps_biquad_sf32`, which
have `_ae32`, `_ansi` and `_arp4` variants only[^2].

`conv/` (convolution, correlation, circular correlation) has **no**
`_aes3` files at all: its platform header defines only
`dsps_conv_f32_ae32_enabled`, `dsps_ccorr_f32_ae32_enabled`,
`dsps_corr_f32_ae32_enabled`, with no ESP32-S3 macro[^3]. As of `v1.8.2`,
convolution on ESP32-S3 runs the ESP32 FPU code path or the ANSI
fallback[^3].

**No float kernel here is PIE-vectorized.** Every `q` register in the tree
belongs to a fixed-point kernel: `dsps_dp_s8_aes3.S`, the eight image dot
products, `dsps_fird_s16_aes3.S`, `dsps_fft2r_sc16_aes3.S`,
`dspm_mult_s16_aes3.S`, the six elementwise `add`/`sub`/`mul` files, and
the `memcpy`/`memset` pair[^23]. What the float `_aes3` files take from the
extension is its wide floating-point load and store, `EE.LDF.128.IP` and
its `.XP`, `.64` and `EE.STF.*` relatives, which move two or four `f`
registers to or from memory in one instruction against a 16-byte-aligned
address[^24]; the arithmetic that follows is scalar `madd.s` and `add.s` on
one register pair at a time. That is what `dsps_dotprod_f32_aes3.S` does,
after an alignment and length check that falls through to the ESP32 body
when the operands are not eligible[^4], and what the float FIR, the float
FFTs and the two float matrix multiplies do[^23]. One file goes further and
uses no extended instruction at all: `dsps_biquad_f32_aes3.S` is scalar FPU
inside a `loopnez`, and its `_aes3` suffix names the target, not the
instruction set it reaches for[^23]. A reader looking here for a float
vector template will not find one.

## Build-time selection: a compile-time macro, not a runtime dispatch

`esp-dsp` picks an implementation with the C preprocessor at compile time,
never a runtime function pointer. Two header layers do it: a
`*_platform.h` header turns on one `_enabled` macro per implementation,
gated on `CONFIG_IDF_TARGET_ESP32S3`/`CONFIG_IDF_TARGET_ESP32P4` (Kconfig)
and, for the ESP32 path, the core's own `XCHAL_HAVE_FP`/`XCHAL_HAVE_LOOPS`/
`XCHAL_HAVE_MAC16`[^5]; the function's own header then chains
`#if`/`#elif` on those macros and, under `CONFIG_DSP_OPTIMIZED`, `#define`s
a suffix-free name to whichever variant won, falling through to `_ansi`
last[^6]:

```c
#if (dsps_dotprod_f32_aes3_enabled == 1)
#define dsps_dotprod_f32 dsps_dotprod_f32_aes3
#define dsps_dotprode_f32 dsps_dotprode_f32_ae32
#elif (dsps_dotprod_f32_arp4_enabled == 1)
#define dsps_dotprod_f32 dsps_dotprod_f32_arp4
#define dsps_dotprode_f32 dsps_dotprode_f32_arp4
#elif (dotprod_f32_ae32_enabled == 1)
#define dsps_dotprod_f32 dsps_dotprod_f32_ae32
#define dsps_dotprode_f32 dsps_dotprode_f32_ae32
#else
#define dsps_dotprod_f32 dsps_dotprod_f32_ansi
#define dsps_dotprode_f32 dsps_dotprode_f32_ansi
#endif // dsps_dotprod_f32_ae32_enabled
```

(`modules/dotprod/include/dsps_dotprod.h`, lines 136-148[^6]; with
`CONFIG_DSP_OPTIMIZED` off, all four suffix-free names in this header
collapse to `_ansi`.) Application and test code calls the suffix-free name
and gets whatever the build's Kconfig target selected, at zero runtime
dispatch cost. Read the branches, not the macro names: the block above
selects the ESP32 kernel for `dsps_dotprode_f32` even when the ESP32-S3
branch is taken, and the block that precedes it does the same for
`dsps_dotprod_s16`, which has no `_aes3` implementation at all. A macro
called `dsps_dotprod_s16_aes3_enabled` is what turns on the `_aes3` `s8`
dot product, not an `_aes3` `s16` one[^6]. A
second, narrower fallback lives inside some `_aes3` kernels themselves:
`dsps_dotprod_f32_aes3` checks length and 16-byte alignment at entry and
jumps to the ESP32 FPU body when unmet, rather than to `_ansi`[^4];
`dsps_dp_s8_aes3` jumps to the ANSI function via `call8` when its length
is not a multiple of 16[^7].

## Coding conventions in the ESP32-S3 assembly

**Windowed entry, arguments.** Every `_aes3.S` function opens with `entry
a1, <frame-size>` and returns with `retw.n`, the standard windowed-ABI
prologue/epilogue[^8]. Arguments arrive in the callee's `a2`-`a7`, in C
declaration order, and every kernel documents the mapping in a comment at
its label, e.g. `dsps_dotprod_f32_aes3`: `src1-a2, src2-a3, dest-a4,
len-a5`[^4]; `dsps_biquad_f32_aes3` labels `a2`-`a5` as
`input`/`output`/`len`/`coeffs`[^9]; `dsps_fird_s16_aes3` labels `a2`-`a5`
as `fir`/`input`/`output`/`len`, then unpacks the `fir` struct's own
fields (coefficients, delay line, length, decimation) by hand with
`l32i.n`/`l16si`[^10]. A kernel that cannot service the fast path falls
back with an ordinary windowed `call8` into the portable C
implementation, arguments shuffled through with `mov.n`, return value read
from `a10`[^7][^11].

**Alignment and length preconditions.** The docs state the FIR constraint
directly: "For esp32s3 length should be divided by 4 and aligned to
16"[^12] (the `coeffs_len` passed to `dsps_fir_init_f32`); the matching
free function "frees allocated memory in case the length of the filter
(and the delay line) is not divisible by 8 and new delay line and filter
coefficients arrays are created for the purpose of the esp32s3
assembly"[^13]. The init and free path pads and reallocates so the
assembly's fixed vector width is always satisfied, rather than the
assembly handling an arbitrary length itself. Where a kernel checks alignment itself, it
does so before the vector path, never by trusting the pointer:
`dsps_dotprod_f32_aes3` ORs the two source pointers, masks the low 4
bits, and checks `len % 4`, sending any failure to the scalar
fallback[^4]; `dsps_dp_s8_aes3` checks only length (multiple of 16),
branching to ANSI otherwise[^7]. Test buffers match this discipline:
`memalign(16, ...)` or `__attribute__((aligned(16)))` throughout the
dotprod and FFT tests[^14][^15].

**Loads, stores, and unaligned reads.** `ee.vld.128.ip`/`ee.vst.128.ip`
load/store a full 128-bit `q` register and post-increment the pointer,
used for the aligned case[^7][^10]. Where a base pointer is not guaranteed
aligned (a circular delay line read at an arbitrary offset), kernels
switch to the SAR-based unaligned pair: `ee.ld.128.usar.ip` loads a chunk
and latches the pointer's low bits into `SAR_BYTE`, and `ee.src.q.ld.ip`
shift-merges two such loads into one aligned `q` register while loading
the next chunk (`modules/fir/fixed/dsps_fird_s16_aes3.S`, lines
243-250[^10]).

**SAR setup, the accumulator, and readout.** Fixed-point kernels clear the
`accx` accumulator explicitly before the main loop with
`wur.accx_0`/`wur.accx_1` (write user register)[^7]. A loop that uses the
`.qup` forms on already-aligned data also zeroes `SAR_BYTE` with
`wur.sar_byte` first, because those forms carry a shift-merge whose byte
count comes from `SAR_BYTE`; zeroing it makes the merge a pass-through and
the aligned loop correct[^16][^22]. The multiply-accumulate is one fused
instruction, `ee.vmulas.<type>.accx...`, which multiplies two `q`
registers' lanes into `accx`, saturates the sum, and in its `.ld` forms
also loads the next vector; the `.qup` forms add the `SAR_BYTE` shift-merge
of two further `q` registers into the first of them[^22], which is what
lets the same instruction feed an unaligned stream[^7][^10][^17]:

```
loopnez a5, .loop_dsps_dp_s8_aes3
    ee.vmulas.s8.accx.ld.ip q1, a3, 16, q0, q1
    ee.vld.128.ip           q0, a2, 16
```

(`modules/dotprod/fixed/dsps_dp_s8_aes3.S`, lines 51-54[^7]. The file also
carries a stray Russian debug comment on line 41, "always ANSI, remove
before release", which describes nothing the code does; it is in the
published tag.) Readout takes
two forms: `rur.accx_0`/`rur.accx_1` (read user register) pull the raw
accumulator into two general registers unshifted, for callers that scale
afterward[^7][^18]; `ee.srs.accx <dest>, <shift>, <round-flag>` shifts by a
register-held amount and stores the low 32 bits in one instruction, used
by FIR to apply the filter's final shift and rounding together
(`ee.srs.accx a15, a11, 0`, `dsps_fird_s16_aes3.S`, line 169[^10]).

**Loop structure: `LOOP` plus manual unrolling.** Every hot loop uses the
zero-overhead hardware loop (`loopnez`/`loopgtz`, Xtensa's `LOOP`
family[^19]) and is also manually unrolled 2 to 4 times inside that body,
rotating 4 to 6 `q` registers by hand so a load issued this iteration is
consumed later and the MAC never stalls on its own load[^7][^10][^17].
`dspi_dotprod_s16_aes3`'s main loop issues four
`ee.vld.128.ip`/`ee.vmulas.s16.accx.ld.ip.qup` pairs per hardware-loop
pass and cycles six registers, `q0` to `q5`, through the load, multiply
and merge operand slots, so no instruction reads a register the one before
it wrote (`modules/dotprod/fixed/dspi_dotprod_s16_aes3.S`,
lines 156-166[^17]).

**Tails that are not a multiple of the vector width.** FIR handles a
non-vector-width remainder with a compile-time decision tree, not a
scalar cleanup loop: after the main `loopnez` body consumes full 128-bit
chunks, a chain of `beqi`/`bgei`/`beqz`/`bgez` against the exact remainder
(16, 8, 0, descending) picks one of several hand-written tail blocks, each
doing exactly that much partial-vector work, still with `ee.vmulas`, never
a scalar loop (`modules/fir/fixed/dsps_fird_s16_aes3.S`, lines
262-267[^10]; the six-way pattern repeats per decimation factor and
circular-buffer half). The simpler dot-product kernels take the opposite
approach: refuse the vector path outright on a non-multiple length and
fall back to the scalar loop (`dsps_dp_s8_aes3`, ANSI on `len % 16 !=
0`)[^7]. Which approach a kernel uses tracks how often its real callers
pass an inconvenient length: FIR coefficient counts are rarely powers of
two, a raw dot product length often is.

## Documented benchmarks

Benchmark data is checked into the repo as a CSV
(`docs/esp_bm_results.csv`), rendered into the published docs by
`docs/build_bm_table.py`[^20]. Each row gives optimized and ANSI cycle
counts for one input size at `-O2`. Selected ESP32-S3 rows (`v1.8.2`,
"Esp32s3" section[^20]):

| Function | Input | ESP32-S3 optimized | ANSI | Speedup |
|---|---|---:|---:|---:|
| `dsps_dotprod_f32` | N=256 | 432 | 1,311 | 3.0x |
| `dsps_fft2r_fc32` | 256 complex pts | 20,139 | 35,806 | 1.8x |
| `dsps_fft2r_sc16` | 256 complex pts | 3,412 | 44,609 | 13.1x |
| `dspm_mult_f32` | 16x16 * 16x16 | 6,280 | 52,656 | 8.4x |
| `dspi_dotprod_s8/u8` | 64x64 image | 689 | 33,733 | 49.0x |

The matching "Esp32" section (no PIE, MAC16/FP only) shows the same image
dot product's "optimized" path is not faster than ANSI at all on that chip
(37,962 vs 37,962 cycles)[^20]: the `_ae32` path for that function is not
meaningfully different from the scalar loop, and the 49x win only appears
once PIE exists. The float FFT's ESP32-S3 win (1.8x) is far smaller than
the fixed-point FFT's (13.1x) or the image dot product's (49x); `esp-dsp`
does not explain the gap, and a plausible cause (float FFT time going into
scalar FPU and twiddle bookkeeping PIE cannot touch) is this document's
own inference, not a stated claim in the source. `[uncertain]`

## Test structure: unit tests against the ANSI reference

Every kernel with more than one implementation is tested by running the
optimized and `_ansi` functions on identical input and comparing:
bit-exact for fixed-point, a small tolerance or downstream metric for
float. The FFT test computes both transforms, logs at `1e-5` for
diagnostics, but the actual pass/fail check is downstream (peak bin
position, peak magnitude rounded to 0.1 dB)[^15]:

```c
dsps_fft2r_fc32(data, N_check);
dsps_fft2r_fc32_ansi(check_data, N_check);
TEST_ASSERT_EQUAL(check_bin, max_pos);
TEST_ASSERT_EQUAL(6 * 10, round(max * 10));
```

(`modules/fft/test/test_dsps_fft2r_fc32_ae32.c`, lines 57-88[^15].) The dot
product test instead sweeps every length 1 to 1023 through the
suffix-free (dispatched) function and checks against a value computed by
hand from constant inputs, exercising every alignment and remainder
branch without a second reference implementation[^14]. Buffers in both
tests are 16-byte aligned, matching what the optimized kernels
require[^14][^15]. This is a reasonable general model: run reference and
optimized on identical input, compare at a tolerance the arithmetic
actually needs, and pick input sizes that force every tail/alignment
branch at least once.

## Licence

The repo's `LICENSE` is Apache License 2.0[^21], and every file inspected
carries an Apache-2.0 header (long-form notice, or `SPDX-License-Identifier:
Apache-2.0` in newer files)[^2]. Apache-2.0 permits copying, modifying and
redistributing, including into a closed-source or differently licensed
project, provided the notice and any `NOTICE` content are kept and changes
documented; it also grants an explicit patent licence from contributors,
which matters more than usual when the code is a hardware vendor's own
vector-instruction patterns. Copying one idiom or a whole file both fall
within what the licence allows; only attribution and change-notice
obligations travel with the copy.

## Idioms worth copying

| Idiom | File | Lines |
|---|---|---|
| Compile-time suffix-free dispatch via chained `#if`/`#define`, ANSI last | `modules/dotprod/include/dsps_dotprod.h` | 120-155 |
| Alignment + length precondition check, OR-then-mask on two pointers | `modules/dotprod/float/dsps_dotprod_f32_aes3.S` | 42-52 |
| Length-only precondition check, `call8` fallback to ANSI | `modules/dotprod/fixed/dsps_dp_s8_aes3.S` | 34-40, 66-74 |
| Accumulator zero-init, fused load+MAC in the loop body | `modules/dotprod/fixed/dsps_dp_s8_aes3.S` | 43-54 |
| SAR-latched unaligned load pair for a circular buffer at an arbitrary offset | `modules/fir/fixed/dsps_fird_s16_aes3.S` | 243-250 |
| 6-register software-pipelined MAC loop using `.qup` chaining | `modules/dotprod/fixed/dspi_dotprod_s16_aes3.S` | 156-166 |
| Rounding shift-and-store of the accumulator in one instruction | `modules/fir/fixed/dsps_fird_s16_aes3.S` | 169 |
| Six-way remainder decision tree for a non-vector-width tail | `modules/fir/fixed/dsps_fird_s16_aes3.S` | 262-267 |
| Sweep every length through the dispatched function to force every branch | `modules/dotprod/test/test_dotprod_f32.c` | 26-58 |
| Dual-implementation comparison plus a downstream-metric acceptance check | `modules/fft/test/test_dsps_fft2r_fc32_ae32.c` | 57-88 |

## What esp-dsp does not cover

`esp-dsp` is a signal-processing and linear-algebra library; its patterns
stop being a direct template outside that domain. It has **no
gather-based lookups**: every kernel surveyed streams contiguous or
fixed-stride memory, none index a `q` register's lanes from a table by a
data-dependent index, so no palette or LUT-gather analogue exists here.
The one instruction that could serve, `EE.LDXQ.32`, appears nowhere in the
tree at either revision[^22b]; see
[Table lookups, palettes and RGB565 arithmetic](./lut-gathers-palettes-and-rgb565.md)
for what it does and what it costs.
It has **no RGB565 or packed-pixel arithmetic**: fixed-point types are
treated as PCM-style samples or matrix elements, not packed sub-byte
colour channels, so there is no unpack/blend/repack idiom to borrow. It
has **no fixed-point image blending or compositing**: the `dspi_*`
"image" functions are a dot product between an image window and a filter
kernel (convolution-style filtering), not alpha blending or colour-space
conversion. And it has **no convolution PIE kernel on ESP32-S3** as of
`v1.8.2`: `conv/` has no `_aes3` files, so the fastest path there is the
ESP32 FPU code or ANSI[^3].

Its value as a reference is in the surrounding machinery: how a real,
vendor-maintained codebase structures per-chip dispatch, documents its
alignment contract, tests a hand-written kernel against a portable
reference, and reports benchmarks with input size and compiler flag
stated. That transfers to a different arithmetic domain even where the
specific `ee.*` sequences do not.

## Footnotes

[^1]: Espressif Systems, `esp-dsp` `README.md`, "Overview": "ESP-DSP is the official DSP library for all Espressif chips. The library contains optimized functions for ESP32, ESP32-S3 and ESP32P4 chips." github.com/espressif/esp-dsp, tag `v1.8.2`, commit `a53a0756833c045311ea1d79a2badf495cdfde4c`.
[^2]: github.com/espressif/esp-dsp, tag `v1.8.2`, commit `a53a0756833c045311ea1d79a2badf495cdfde4c`, cloned and inspected directly (`modules/` tree, per-file licence headers).
[^3]: `modules/conv/include/dsps_conv_platform.h`, same repo/tag: defines only `dsps_conv_f32_ae32_enabled`, `dsps_ccorr_f32_ae32_enabled`, `dsps_corr_f32_ae32_enabled`; `find modules/conv -name '*_aes3.S'` returns no files at this tag.
[^4]: `modules/dotprod/float/dsps_dotprod_f32_aes3.S`, same repo/tag, lines 36-84 (label, argument comment, alignment/length check, branch to `.dsps_dotprod_f32_ae32_body`, FPU MAC body using `wfr`/`madd.s`/`add.s`).
[^5]: `modules/dotprod/include/dsps_dotprod_platform.h`, same repo/tag: `#if CONFIG_IDF_TARGET_ESP32S3` guards `dsps_dotprod_s16_aes3_enabled`/`dsps_dotprod_f32_aes3_enabled`; ESP32-path macros guarded by `XCHAL_HAVE_FP`, `XCHAL_HAVE_LOOPS`, `XCHAL_HAVE_MAC16` from `<xtensa/config/core-isa.h>`.
[^6]: `modules/dotprod/include/dsps_dotprod.h`, same repo/tag, lines 120-155: the `#if CONFIG_DSP_OPTIMIZED` block chains `_aes3`/`_arp4`/`_ae32`/`_ansi` into the suffix-free names, `s16` and `dp_s8` at lines 122-134 and `f32` and `dotprode_f32` at 136-148; the `#else` at 150-155 collapses all four to `_ansi`. Line 123 defines `dsps_dotprod_s16` to `dsps_dotprod_s16_ae32` inside the `_aes3_enabled` branch, and line 138 defines `dsps_dotprode_f32` to `dsps_dotprode_f32_ae32` in the same way.
[^7]: `modules/dotprod/fixed/dsps_dp_s8_aes3.S`, same repo/tag, 76 lines: minimum-length and multiple-of-16 checks at lines 34-40, `wur.accx_0`/`wur.accx_1` zero-init at 43-45, `ee.vld.128.ip` preloads at 46-47, `loopnez`-wrapped `ee.vmulas.s8.accx.ld.ip` fused MAC-and-load at 51-54, `rur.accx_0` readout at 56, `.dsps_dp_s8_aes3_via_ansi` fallback via `call8 dsps_dp_s8_ansi` at 66-74.
[^8]: Pattern observed across every `_aes3.S` file opened in this repo/tag (`dsps_dotprod_f32_aes3.S`, `dsps_dp_s8_aes3.S`, `dspi_dotprod_s16_aes3.S`, `dsps_fird_s16_aes3.S`, `dsps_biquad_f32_aes3.S`): each begins `entry a1, <N>`, ends `retw.n`. The windowed ABI itself is documented in [Register windows and the windowed ABI](../00-foundations/register-windows-and-windowed-abi.md).
[^9]: `modules/iir/biquad/dsps_biquad_f32_aes3.S`, same repo/tag: the label is at line 36 and the register comment naming `a2` to `a6` runs 37-41; the C the kernel implements is quoted in a comment at lines 23-34.
[^10]: `modules/fir/fixed/dsps_fird_s16_aes3.S`, same repo/tag: register map lines 20-28; `entry` and struct unpack lines 31-37; aligned fill loop lines 105-118; `main_loop_decim_16` at line 123, its MAC loop and the `ee.srs.accx a15, a11, 0` readout at line 169; SAR-latched unaligned load and shift-merge lines 243-250; six-way remainder tree lines 262-267, the pattern repeating per decimation factor and circular-buffer half through the end; full file 1,026 lines.
[^11]: `modules/dotprod/fixed/dspi_dotprod_s16_aes3.S`, same repo/tag, lines 61-73 and 201-212 (`call8 dspi_dotprod_s16_ansi` fallback sites).
[^12]: `modules/fir/include/dsps_fir.h`, same repo/tag, line 86, doc comment on `dsps_fir_init_f32`'s `coeffs_len`: "For esp32s3 length should be divided by 4 and aligned to 16."
[^13]: `modules/fir/include/dsps_fir.h`, same repo/tag, lines 289-290, point 3 of the doc comment on the `fir_s16_t` freeing function (comment block 283-296): "frees allocated memory in case the length of the filter (and the delay line) is not divisible by 8 and new delay line and filter coefficients arrays are created for the purpose of the esp32s3 assembly."
[^14]: `modules/dotprod/test/test_dotprod_f32.c`, same repo/tag, lines 26-58: `memalign(16, ...)` allocation at 29-31, then two sweeps of every length 1 to 1023 through the suffix-free `dsps_dotprod_f32` (41-47 and 52-58), each checked against a value derived from the fixed test inputs and against guard values written either side of the destination.
[^15]: `modules/fft/test/test_dsps_fft2r_fc32_ae32.c`, same repo/tag: `__attribute__((aligned(16)))` buffers lines 28-31; `memalign` at lines 36 (1024-byte alignment), 127 and 130 (16-byte); the two transforms at 57-58, the logging-only `1e-5` comparison loop at 60-66, and the pass/fail assertions at 86 and 88.
[^16]: `wur.sar_byte` appears in `modules/dotprod/fixed/dspi_dotprod_s16_aes3.S`, line 105 (same repo/tag), immediately before the `wur.accx_0`/`wur.accx_1` pair at 106-107, zeroing `SAR_BYTE` for an aligned vector loop whose `.qup` instructions still perform their shift-merge.
[^17]: `modules/dotprod/fixed/dspi_dotprod_s16_aes3.S`, same repo/tag, lines 156-166: `loopnez` at 156, `.LBB219_dspi_dotprod_s16_aes3` body at 158-166, four `ee.vld.128.ip`/`ee.vmulas.s16.accx.ld.ip.qup` pairs per pass cycling `q0` through `q5`.
[^18]: `modules/dotprod/fixed/dspi_dotprod_s16_aes3.S`, same repo/tag, lines 185-198 (`rur.accx_0`/`rur.accx_1` raw readout followed by a manual funnel-shift `src`).
[^19]: Cadence/Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference Manual*, RC-2010.1 Release, issue date April 2010: the Loop Option, Section 4.3.2, p. 54, and the instruction descriptions `LOOP` p. 392, `LOOPGTZ` p. 394 and `LOOPNEZ` p. 396. The ESP32-S3 configures the option: `XCHAL_HAVE_LOOPS` is 1 in `components/xtensa/esp32s3/include/xtensa/config/core-isa.h` (ESP-IDF 5.5.1). Full treatment: [Zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md).

[^22]: Espressif, *ESP32-S3 Technical Reference Manual*, version 1.8, Section 1.8.136 `EE.VMULAS.S16.ACCX.LD.IP.QUP`, p. 212: the instruction multiply-accumulates `qx` and `qy` into `ACCX` with saturation, forces the low 4 bits of `as` to 0 and loads 16 bytes into `qu`, and "obtains 16-byte unaligned data by concatenating and shifting consecutive aligned data stored in the two registers `qs0` and `qs1` and stores it to `qs0`. The shift byte is stored in special register SAR_BYTE."; also Table 1.5-1, p. 45, which lists `SAR_BYTE` as a 4-bit customized special register. <https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf>
[^20]: `docs/esp_bm_results.csv` and `docs/build_bm_table.py`, same repo/tag. Each CSV row is `<name>, <optimized-cycles>, <ansi-cycles>, <opt-flag: 1=O2/2=Os>, <cpu: 1=ESP32/3=ESP32-S3/4=ESP32-P4>`, confirmed against `build_bm_table.py`'s parsing logic and the CSV's `Esp32`/`Esp32s3`/`Esp32p4` section headers. Figures quoted here are the `-O2` rows.
[^21]: `esp-dsp` `LICENSE` file, same repo/tag: Apache License, Version 2.0, January 2004.

[^22b]: Case-insensitive search of the whole tree for `ldxq` at tag `v1.8.2` (`a53a0756833c045311ea1d79a2badf495cdfde4c`) and at `master` sha `3c8ac0fdfec83740b783e200862c8d0c056de0ad`: no matches at either.

[^23]: Counted at tag `v1.8.2` over all 29 files matching `*_aes3*.S`: a case-insensitive count of `ee.` mnemonics per file, and a count of `q0` to `q7` operands per file. Every file with a `q` operand is fixed-point. The float files (`dsps_dotprod_f32_aes3.S`, `dsps_fir_f32_aes3.S`, `dsps_fird_f32_aes3.S`, `dsps_fft2r_fc32_aes3_.S`, `dsps_fft4r_fc32_aes3_.S`, `dsps_bit_rev_lookup_fc32_aes3.S`, `dspm_mult_f32_aes3.S`, `dspm_mult_ex_f32_aes3.S`) use only `EE.LDF.*` and `EE.STF.*` and have zero `q` operands. `dsps_biquad_f32_aes3.S` has zero `ee.` mnemonics.

[^24]: Espressif, *ESP32-S3 Technical Reference Manual*, version 1.8, Sections 1.8.25 to 1.8.28 (`EE.LDF.128.IP`, p. 101, `EE.LDF.128.XP`, p. 102, `EE.LDF.64.IP`, p. 103, `EE.LDF.64.XP`, p. 104) and 1.8.65 to 1.8.68 (`EE.STF.*`, pp. 141 to 144). `EE.LDF.128.IP fu3, fu2, fu1, fu0, as, -128..112` "forces the lower 4 bits of the access address in register `as` to 0, loads 16-byte data from memory, and stores it in order from low bit to high bit to floating-point registers `fu0`, `fu1`, `fu2`, and `fu3`". <https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf>
