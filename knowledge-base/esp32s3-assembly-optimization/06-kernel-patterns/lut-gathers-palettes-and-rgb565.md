---
title: Table lookups, palettes and RGB565 arithmetic on the ESP32-S3
id: 06-kernel-patterns/lut-gathers-palettes-and-rgb565
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, pie, lut, gather, palette, rgb565, dithering, fixed-point]
confidence: medium
---

# Table lookups, palettes and RGB565 arithmetic on the ESP32-S3

Two things dominate a pixel kernel on this chip: how a value gets out of a table,
and how a 16-bit pixel gets built once it is out. This file gives the instruction
budget for the scalar lookup loop, the packing rules for 16-bit pixels, the
split-channel blend that weights all three colour channels in one multiply, and the
parts of the PIE vector unit that do and do not help. No cycle claims are made: the
sequences below are code shape, confirmed by assembling them and by reading what GCC
14.2 emits. Turning a shape into a time needs a measurement; see `05-measurement/`.

## Instruction semantics used below

Scalar semantics come from the Xtensa ISA Reference Manual (RC-2010.1, April
2010)[^1], vector semantics from the ESP32-S3 Technical Reference Manual version
1.8[^2].

| Instruction | Operation | Source |
|---|---|---|
| `ADDX2 ar, as, at` | `ar = (as << 1) + at` | ISA p. 254[^3] |
| `ADDX4 ar, as, at` | `ar = (as << 2) + at` | ISA p. 255[^4] |
| `EXTUI ar, at, shift, mask` | `ar = (at >> shift) & ((1 << mask) - 1)`, shift 0..31, mask 1..16 | ISA p. 344[^5] |
| `SLLI ar, as, 1..31` | shift left, immediate | ISA p. 525[^6] |
| `SRLI ar, at, 0..15` | shift right, immediate, no form for 16 or more | ISA p. 530[^7] |
| `L8UI at, as, 0..255` | zero-extended byte load, byte offset | ISA p. 369[^8] |
| `L16UI at, as, 0..510` | zero-extended halfword load, offset is a multiple of 2 | ISA p. 372[^9] |
| `S16I at, as, 0..510` | halfword store, offset is a multiple of 2 | ISA p. 505[^10] |
| `S32I at, as, 0..1020` | word store, offset is a multiple of 4 | ISA p. 510[^11] |
| `MULL ar, as, at` | low 32 bits of a 32x32 product | ISA p. 450[^12] |
| `EE.VLD.128.IP qu, as, imm` | clears the low 4 address bits, loads 16 bytes, post-increments | TRM 1.8.88, p. 164[^13] |
| `EE.VST.128.IP qv, as, imm` | clears the low 4 address bits, stores 16 bytes, post-increments | TRM 1.8.192, p. 275[^14] |
| `EE.LDXQ.32 qu, qs, as, sel4, sel8` | one indexed 32-bit load, index from a 16-bit lane of `qs` | TRM 1.8.37, p. 113[^15] |
| `EE.VZIP.8 qs0, qs1` | interleaves the bytes of two q registers, writes both | TRM 1.8.212, p. 295[^16] |
| `EE.VUNZIP.8 qs0, qs1` | the inverse, even bytes into `qs0` and odd into `qs1` | TRM 1.8.209, p. 292[^17] |
| `EE.VMUL.U8 qz, qx, qy` | sixteen unsigned 8x8 products, each right-shifted by `SAR`, low 8 bits kept | TRM 1.8.131, p. 207[^18] |
| `EE.VMUL.S16 qz, qx, qy` | eight signed 16x16 products, each arithmetically right-shifted by `SAR`, low 16 bits kept | TRM 1.8.122, p. 198[^19] |

All the above assemble with the shipped toolchain (GNU as, crosstool-NG
`esp-14.2.0_20241119`, binutils 2.43.1) at the operand orders shown [experience].

## PIE has no full gather, only a one-lane indexed load

The PIE extension defines 220 instructions[^20]. Every vector load is either
contiguous with a post-increment (`EE.VLD.128.IP`, `.XP`), a broadcast of one scalar
to all lanes (`EE.VLDBC.8/.16/.32`), or a load into the multiply accumulators.
Nothing loads all lanes from independently computed addresses. The one exception is
`EE.LDXQ.32`, with `EE.STXQ.32` as its counterpart[^15][^21].
It picks one of the eight 16-bit lanes of `qs` as an index, scales it by 4, adds the
address register `as`, clears the low 2 bits, and writes the loaded word into one of
the four 32-bit lanes of `qu`. Four of these fill one q register, so it is a
per-element indexed load, not a gather. Its limits:

- The element size is fixed at 32 bits, so a 16-bit palette must be read as
  pairs of entries or half of each load wasted.
- Indices are scaled by 4 and the address is aligned down, so the table has
  to be an array of 32-bit words.
- Both lane selectors are immediates, so filling a register takes four
  distinct instructions rather than a loop.
- Whether the index is signed or unsigned is not stated [uncertain].

Espressif's own DSP library never uses it: a search of the tree at commit `3c8ac0f`
finds no occurrence[^22], and its kernels are all contiguous streams. Two designs
remain for a table-driven kernel. **Vector index, scalar lookup**
computes the indices with PIE, stores them to a 16-byte-aligned scratch with
`EE.VST.128.IP`, then runs a scalar loop over the scratch; the alignment is required
because the store clears the low 4 address bits without complaint[^14]. **Fully
scalar** keeps the index in a register and never builds a vector, which wins
whenever the index arithmetic is cheap, because it skips the round trip through
memory.

## The scalar gather loop

The shape is: load the index, scale it into the table base, load the entry, pack,
store. GCC 14.2 emits exactly that. For a byte-indexed 16-bit palette written as
`dst[i] = pal[src[i]]`, the loop body is six instructions per pixel[^23]:

```
.L3:
        l8ui    a9, a3, 0       ; index byte
        addi.n  a3, a3, 1       ; advance source
        addx2   a9, a9, a4      ; a4 = palette base; a9 = base + index*2
        l16ui   a9, a9, 0       ; table entry
        s16i    a9, a2, 0       ; result <- consumed by the next instruction
        addi.n  a2, a2, 2       ; advance destination
```

`ADDX2` is the scaling instruction for a 16-bit table and `ADDX4` for a 32-bit one;
the manual names address calculation as their intended use[^3]. The defect in this
body is that `l16ui` writes `a9` and `s16i` reads it immediately. The LX7 interlocks
operand dependencies in hardware, so the code is correct, but the pipeline stalls
instead of retiring[^24]. See `00-foundations/` for the cost model. The fix is to
keep more than one lookup in flight. Interleaving two of them, and walking the
pointers instead of indexing, gives eleven instructions for two pixels[^23]:

```
.L3:
        l8ui    a8, a3, 1       ; second index
        l8ui    a10, a3, 0      ; first index
        addx2   a8, a8, a4
        l16ui   a8, a8, 0       ; second entry
        addx2   a10, a10, a4    ; separates the load from its use
        l16ui   a10, a10, 0     ; first entry
        slli    a8, a8, 16      ; pack: high pixel
        or      a8, a8, a10     ; pack: low pixel
        s32i    a8, a2, 0       ; two pixels in one word
        addi.n  a3, a3, 2
        addi.n  a2, a2, 4
```

The same loop written with an index variable and `dst[i>>1]` costs thirteen
instructions for those two pixels, because GCC keeps a separate counter and rebuilds
the address with `SRAI` and `ADDX4` every iteration[^23]. Walk pointers. Four
lookups per iteration reach twenty-two instructions for four pixels, the same rate
per pixel, but every table load then has two or more instructions before its first
use[^23]. That is the version to write when the loop is load-bound. One toolchain
caveat: GCC 14.2 emitted the zero-overhead `LOOP` for the one- and two-pixel
versions and a counted branch for the four-pixel one[^23].

Keep every table base and constant in a register for the whole loop. A base reloaded
per iteration is an extra load with its own interlock, and the `L8UI` and `L16UI`
offset fields reach only 255 and 510 bytes[^8][^9], so a large table cannot be
covered from one base by immediate offsets anyway.

## Packing pixel pairs

Two RGB565 pixels are one 32-bit word. Building it costs one `SLLI` and one `OR`,
and one `S32I` replaces two `S16I`, halving the store count. Two conditions apply:
the row must start at a 4-byte boundary, and an odd width needs a scalar tail. The
ISA manual's base behaviour is that `S32I` ignores the low two address bits when the
Unaligned Exception Option is absent, silently writing the wrong place[^11]. This
part declares hardware unaligned load and store support and no unaligned
exception[^25], so an unaligned `S32I` should not be truncated here. Whether that
holds in every memory region, cached external memory in particular, is unconfirmed
[uncertain]. Align the rows; it is free. The vector store has no such escape:
`EE.VST.128.IP` clears the low four address bits as documented behaviour[^14], so a
PIE span is aligned with a scalar prefix, never by trusting the pointer.

## Sizing and padding a table

A 256-entry 16-bit palette is 512 bytes, small enough for internal SRAM and to stay
resident in the data cache. A 64 KB texture is not. The data cache is 32 KB or the
whole 64 KB of Internal SRAM 2, depending on configuration[^26], so a randomly
indexed read stream into a table that size misses on nearly every access and runs at
external memory speed. That is a placement rule, not an instruction-count rule: rank
tables by reads per frame and give internal memory to the ones read per pixel. See
`03-memory-hierarchy/` for the miss costs.

One trick buys a register and a load. When two 8-bit tables are always read at the
same index, interleave them into one 16-bit table and split the loaded halfword. GCC
14.2 shows the difference[^27]: the two-table form keeps two base registers live and
issues two `L8UI`, while the one-table form keeps one base and becomes `ADDX2`,
`L16UI`, `SRLI`. `SRLI` is legal only because the shift is 8; there is no `SRLI` for
shifts of 16 or more[^7].

Pad the table if the index is not clamped, because an unclamped gather will read
past the end somewhere in the parameter space. Size the pad from the largest index
the expression can produce, not the largest one you have seen: if a signed offset
bounded by B is added to an index, the table needs B extra entries above the top and
B below the base. Be strict, because the test that should catch an overrun does not
unless the sanitizers are on: a one-entry overrun reads the adjacent byte, returns a
plausible value, and a golden-output comparison passes. See `05-measurement/` for
the rule that a fuzzer without AddressSanitizer is not a fuzzer.

## RGB565 layout and channel extraction

The 16-bit word is red in bits 15..11, green in bits 10..5, blue in bits 4..0, with
channel masks `0xF800`, `0x07E0` and `0x001F`. Extraction is one instruction per
channel with `EXTUI`, which shifts right and masks in one step[^5]:

```
        extui   a5, a2, 11, 5   ; red,   5 bits
        extui   a6, a2, 5,  6   ; green, 6 bits
        extui   a7, a2, 0,  5   ; blue,  5 bits
```

`EXTUI` masks at most 16 bits and is undefined when shift plus mask width exceeds
31[^5]. Neither limit binds for RGB565.

## Two channels in one 32-bit multiply

A 32-bit register can carry the channels far enough apart that one multiply weights
all three without their results colliding. Copy the pixel into both halves of a word
and mask with `0x07E0F81F`. The surviving fields are blue at bits 0..4, red at bits
11..15 and green at bits 21..26, with six free bits above blue and five above each
of red and green. That headroom sets the maximum weight. Green binds: six bits of
value times a weight of 32 is 2016, which occupies bits 21 to 31 and just fits,
while a weight of 33 runs off the end of the word. So the weight is 0 to 32 and the
shift back is 5.

```c
#define M 0x07E0F81Fu
uint32_t F = (fg | (fg << 16)) & M;
uint32_t B = (bg | (bg << 16)) & M;
uint32_t r = ((F * w + B * (32 - w)) >> 5) & M;   /* w in 0..32 */
uint16_t out = (uint16_t)((r >> 16) | r);
```

LVGL 8.4.0 uses the difference form of the same identity and reduces an 8-bit `mix`
to the 0..32 range with `(mix + 4) >> 3`[^28]. Both forms reproduce per-channel
arithmetic exactly: checked over 1.56 billion (foreground, background, weight)
samples for weights 0 to 32 with zero mismatches, and the same check at weight 33
fails, as the headroom argument predicts [experience]. Truncation biases the result
down; adding a half-step to each field before the shift rounds instead. That
constant is 16 at each field's base position, `16 | (16 << 11) | (16 << 21)`, which
is `0x02008010`, checked against per-channel rounding over 2.58 billion samples with
zero mismatches [experience].

Both weighted products are `MULL`[^12], and the pack at the end is the `SLLI`/`OR`
pair from the pixel-pair section:

```
        extui   a8, a2, 0, 16   ; fg
        slli    a9, a8, 16
        or      a8, a8, a9      ; fg | fg<<16
        and     a8, a8, a12     ; a12 = 0x07E0F81F, held in a register
        ...                     ; same three for bg into a10
        mull    a8, a8, a13     ; a13 = w
        mull    a10, a10, a14   ; a14 = 32 - w
        add.n   a8, a8, a10
        add.n   a8, a8, a15     ; a15 = 0x02008010, rounding
        srli    a8, a8, 5
        and     a8, a8, a12
        extui   a9, a8, 16, 16
        or      a2, a8, a9      ; result in the low half
```

Keep the mask, the weight, its complement and the rounding constant in registers.
`MOVI` cannot carry a full 32-bit constant, so a reload becomes a literal-pool load
with its own interlock.

## The same blend across PIE lanes

**Widen to 16-bit lanes, then narrow.** `EE.VZIP.8` interleaves the bytes of two q
registers and writes both[^16]. Zipping packed bytes against a zeroed register gives
zero-extended 16-bit lanes, the first register holding lanes 0 to 7 and the second
lanes 8 to 15; the zero register is consumed, so it must be re-zeroed each time.
`EE.VMUL.S16` weights the lanes with a right shift taken from `SAR`, the fixed-point
scale factor[^19], and `EE.VUNZIP.8` puts the low byte of every lane back
together[^17].

**Stay on bytes.** `EE.VMUL.U8` does sixteen 8x8 products in one instruction, each
shifted by `SAR` and truncated to 8 bits[^18]. That suits an 8-bit-per-channel
source, but not RGB565, whose 5- and 6-bit fields are not on byte boundaries. Either
way the pixels must be split into channel planes first, and that is the real cost: a
kernel reading and writing RGB565 pays a pack and an unpack per pixel to use them.
The scalar split-channel blend needs neither, which is why it competes.

## Alpha blending with 8-bit alpha

With an alpha of 0..255 the exact result per channel is `(fg * a + bg * (255 - a)) /
255`. The division is a reciprocal multiply; LVGL 8.4.0 uses `(x * 0x8081) >>
23`[^29], which is exact for inputs up to 66298 [experience]. The largest value the
numerator can reach is 65025 plus a rounding offset, so it is safe. GCC 14.2 does not
use `MULL` for it: it expands the constant into two shift-and-add pairs and finishes
with `EXTUI a2, a2, 23, 9`, five instructions in all[^30]. The `EXTUI` is forced,
since `SRLI` has no form for a shift of 23[^7].

Rounding is a choice, not a detail. Truncating darkens every blend by up to one
step, and a chain of blends on the same pixel accumulates the bias. Add half the
divisor before the divide; LVGL makes this a configurable offset on its per-channel
path[^28]. If the alpha only takes a few values, drop to the 0..32 weight of the
split-channel form and skip the division.

## Ordered dithering before quantisation

Quantising 8-bit channels to RGB565 discards 3 bits of red and blue and 2 of green,
so a smooth gradient bands. Ordered dithering removes the banding by varying the
rounding point across the image with a fixed tiled matrix, which decorrelates the
quantisation error from the signal[^31]. The matrix is Bayer's, built by a
recurrence from the 2 by 2 case[^31][^32]:

```
I_2N = [ 4*I_N + 1,  4*I_N + 2 ;
         4*I_N + 3,  4*I_N     ]
```

An entry becomes a threshold by scaling it across the output range. The order
matters: the offset goes onto the full-precision value and the quantisation happens
afterwards. Applied after quantisation it does nothing, because the value is already
on the output grid. In fixed point, centre the matrix so it is a signed offset, then
clamp. LVGL 8.4.0 does exactly this for gradients: an 8 by 8 matrix of values 0 to 63,
minus 32 to centre it, added to each 8-bit channel with a clamp, and only then
converted to the display format[^33]. Its shortcut is worth knowing. It adds the
same offset to all three channels, although the ideal offset scales with each
channel's quantisation step, and green's step is half of red's and blue's.

## Byte order

RGB565 in memory is little-endian on this chip, so the low byte sits at the lower
address. Several panel interfaces, 8-bit serial ones in particular, want the
opposite order. LVGL makes this a build option and applies the swap on the way into
and out of its blend, not inside it[^28]. Do the same: swap at the boundary, once
per buffer or span, and keep the kernel in native order. A swap folded into the
pixel loop is one more instruction on every pixel for something a DMA setting or a
one-pass fixup can often do instead. In registers, one pixel is `EXTUI` plus `SLLI`
plus `OR`, and a 16-byte span is `EE.VZIP.8` work once the bytes are split into even
and odd planes with `EE.VUNZIP.8`[^16][^17].

## Footnotes

[^1]: Cadence/Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference Manual*, RC-2010.1 Release, issue date April 2010, document PD-09-0801-10-01. Copy consulted: https://0x04.net/~mwk/doc/xtensa.pdf
[^2]: Espressif Systems, *ESP32-S3 Technical Reference Manual*, Version 1.8, Chapter 1 "Processor Instruction Extensions (PIE)". https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf
[^3]: ISA manual[^1], "ADDX2 Add with Shift by 1", p. 254. Operation: `AR[r] <- (AR[s]30..0||0) + AR[t]`. The description names address calculation as its common use.
[^4]: ISA manual[^1], "ADDX4 Add with Shift by 2", p. 255.
[^5]: ISA manual[^1], "EXTUI Extract Unsigned Immediate", p. 344. `shiftimm` is 0..31, `maskimm` is 1..16, and the result is undefined when `sa + op2 > 31`.
[^6]: ISA manual[^1], "SLLI Shift Left Logical Immediate", p. 525. Shift amount 1..31; the assembler rewrites a shift of 0 into `OR`.
[^7]: ISA manual[^1], "SRLI Shift Right Logical Immediate", p. 530. "There is no SRLI for shifts >= 16. EXTUI replaces these shifts." The assembler performs the substitution.
[^8]: ISA manual[^1], "L8UI Load 8-bit Unsigned", p. 369.
[^9]: ISA manual[^1], "L16UI Load 16-bit Unsigned", p. 372. The machine-code offset is in 16-bit units; the assembler divides the byte offset by two.
[^10]: ISA manual[^1], "S16I Store 16-bit", p. 505.
[^11]: ISA manual[^1], "S32I Store 32-bit", p. 510. Without the Unaligned Exception Option the two low address bits are ignored.
[^12]: ISA manual[^1], "MULL Multiply Low", p. 450, 32-bit Integer Multiply Option. ESP-IDF 5.5.1 `components/xtensa/esp32s3/include/xtensa/config/core-isa.h` sets `XCHAL_HAVE_MUL16`, `XCHAL_HAVE_MUL32` and `XCHAL_HAVE_MUL32_HIGH` to 1, so the multiply options are present on this part.
[^13]: TRM[^2], Section 1.8.88 "EE.VLD.128.IP", p. 164.
[^14]: TRM[^2], Section 1.8.192 "EE.VST.128.IP", p. 275.
[^15]: TRM[^2], Section 1.8.37 "EE.LDXQ.32", p. 113. Note that the operation listing in version 1.8 writes `vaddr3 = as + qs[63:47] * 4`, where the other seven lines follow a 16-bit lane pattern that would give `qs[63:48]`. Read as a typographical error in the manual [uncertain].
[^16]: TRM[^2], Section 1.8.212 "EE.VZIP.8", p. 295.
[^17]: TRM[^2], Section 1.8.209 "EE.VUNZIP.8", p. 292.
[^18]: TRM[^2], Section 1.8.131 "EE.VMUL.U8", p. 207.
[^19]: TRM[^2], Section 1.8.122 "EE.VMUL.S16", p. 198.
[^20]: TRM[^2], Chapter 1, Section 1.8. Count of numbered instruction subsections in the chapter text of version 1.8.
[^21]: TRM[^2], Section 1.8.69 "EE.STXQ.32", p. 145.
[^22]: Espressif, esp-dsp, commit `3c8ac0fdfec83740b783e200862c8d0c056de0ad` (2026-05-12). A case-insensitive search of the whole tree for `ldxq` returns no matches. https://github.com/espressif/esp-dsp
[^23]: Compiled with `xtensa-esp32s3-elf-gcc` (crosstool-NG `esp-14.2.0_20241119`) 14.2.0 at `-O2 -mlongcalls`, reading the emitted `.s`. Instruction counts are of the loop body only and are code shape, not timings.
[^24]: ISA manual[^1], Section 8.4.1 "Processor Performance Terminology and Modeling", Figure 8-54 "Instruction Operand Dependency Interlock", p. 607, and Section 5.1 "General Registers": "Reads from and writes to the AR register file are always interlocked by hardware."
[^25]: ESP-IDF 5.5.1, `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`: `XCHAL_UNALIGNED_LOAD_EXCEPTION` 0, `XCHAL_UNALIGNED_STORE_EXCEPTION` 0, `XCHAL_UNALIGNED_LOAD_HW` 1, `XCHAL_UNALIGNED_STORE_HW` 1.
[^26]: TRM[^2], Chapter 4 "System and Memory", Section 4.3, p. 403: Internal SRAM 2 is 64 KB, of which 32 KB or all 64 KB can be configured as data cache; Internal SRAM 0 is 32 KB, of which 16 KB or all 32 KB can be instruction cache.
[^27]: Same toolchain and flags as[^23], comparing a two-table byte-lookup loop against the same loop reading one interleaved 16-bit table.
[^28]: LVGL 8.4.0, `src/misc/lv_color.h`, `lv_color_mix()`. The 16-bit path masks with `0x7E0F81F`, reduces `mix` with `(mix + 4) >> 3`, and shifts back by 5. `LV_COLOR_16_SWAP` applies the byte swap on entry and exit of that function. `LV_COLOR_MIX_ROUND_OFS` is the rounding offset on the per-channel path. https://github.com/lvgl/lvgl/blob/v8.4.0/src/misc/lv_color.h
[^29]: LVGL 8.4.0, `src/misc/lv_math.h`: `#define LV_UDIV255(x) (((x) * 0x8081U) >> 0x17)`. https://github.com/lvgl/lvgl/blob/v8.4.0/src/misc/lv_math.h
[^30]: Same toolchain and flags as[^23], compiling `(x * 0x8081U) >> 23`.
[^31]: C. A. Bouman, *Digital Image Processing Laboratory: Image Halftoning*, Purdue University, 11 May 2011, Section 4 "Ordered Dithering", equations 6 to 9. Gives the Bayer recurrence, the threshold formula and the decorrelation argument. https://engineering.purdue.edu/~bouman/grad-labs/Image-Halftoning/pdf/lab.pdf
[^32]: B. E. Bayer, "An optimum method for two-level rendition of continuous-tone pictures", *IEEE International Conference on Communications*, vol. 1, 11-13 June 1973, pp. 11-15. Cited as reference [1] of[^31]; the paper itself was not consulted directly [uncertain].
[^33]: LVGL 8.4.0, `src/draw/sw/lv_draw_sw_dither.c`, `dither_ordered_threshold_matrix` (8 by 8, values 0 to 63) and `lv_dither_ordered_hor()`. The comment on the table reads "Shift by 6 to normalize"; the code subtracts 32 and clamps each 8-bit channel before conversion. https://github.com/lvgl/lvgl/blob/v8.4.0/src/draw/sw/lv_draw_sw_dither.c
