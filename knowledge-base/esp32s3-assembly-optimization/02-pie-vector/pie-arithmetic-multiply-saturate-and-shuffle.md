---
title: "PIE compute instructions: multiply, saturate, compare and shuffle"
id: 02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, pie, simd, fixed-point, saturation, shuffle]
confidence: high
---

# PIE compute instructions: multiply, saturate, compare and shuffle

The ESP32-S3 adds a SIMD extension that Espressif calls PIE (Processor Instruction
Extensions). Its instructions are spelled `EE.*` and they work on eight 128-bit `q`
registers plus a set of special registers that most of them read or write without
naming.[^trm-pie][^trm-regs]

This file covers the compute half of the set: multiplies, multiply accumulate, adds,
compares, bitwise ops, shifts, lane moves, interleaves, the activation instructions and
the FFT helpers. Loads and stores are in
[PIE load and store instructions and their alignment rules](./pie-load-store-and-alignment.md);
the `q` registers themselves and the coprocessor context rules are in
[PIE register file, state registers and context save](./pie-register-file-sar-and-context.md).
Everything below is the ESP32-S3 Technical Reference Manual version 1.8 unless a
footnote says otherwise.[^trm-pie]

## The shape of the set

| Fact | Value |
|---|---|
| Vector registers | 8, named `q0` to `q7`, 128 bits each[^trm-regs] |
| Lane widths | 16 x 8-bit, 8 x 16-bit, or 4 x 32-bit, chosen by the instruction[^trm-regs] |
| Scalar shift register | `SAR`, 6 bits, read by every multiply and by the 32-bit lane shifts[^trm-regs][^trm-shift] |
| Byte shift register | `SAR_BYTE`, 4 bits, read by the funnel shifts[^trm-regs] |
| Wide accumulator, flat | `ACCX`, 40 bits, one lane[^trm-regs] |
| Wide accumulator, per lane | `QACC_L` and `QACC_H`, 160 bits each[^trm-regs] |
| Overflow rule | Saturate where the instruction says so, wrap everywhere else[^trm-sat] |

The overflow rule is the opposite of what most SIMD sets do. Only instructions whose
description mentions saturation clamp; every other result keeps the low bits that fit the
destination and drops the rest.[^trm-sat]

## Multiplies

`EE.VMUL.*` has no shift operand. It multiplies each pair of lanes at full product
width, shifts the product right by `SAR`, then writes the low bits of that back to a
lane as wide as the inputs.[^trm-mul] `SAR` is part of the multiply's meaning, not a
separate step. Set it before the loop with `wsr.sar a3`, which is the only writer that
reaches all six bits. `ssr a3` writes `AR[3][4:0]` and clears the top bit of `SAR`, and
`ssai <imm>` takes 0 to 31, so neither can express a shift of 32 or
more.[^dsp-mul][^isa-ssr][^gas-test] Signedness picks the shift kind: `EE.VMUL.S16`
shifts the product arithmetically, `EE.VMUL.U16` logically.[^trm-mul]

| Instruction | Lanes | Product | Written back | Overflow |
|---|---|---|---|---|
| `EE.VMUL.S8 qz, qx, qy` | 16 x s8 | 16 x s16 | low 8 bits of `product >> SAR`, arithmetic | wrap[^trm-mul] |
| `EE.VMUL.U8 qz, qx, qy` | 16 x u8 | 16 x u16 | low 8 bits of `product >> SAR`, logical | wrap[^trm-mul] |
| `EE.VMUL.S16 qz, qx, qy` | 8 x s16 | 8 x s32 | low 16 bits of `product >> SAR`, arithmetic | wrap[^trm-mul] |
| `EE.VMUL.U16 qz, qx, qy` | 8 x u16 | 8 x u32 | low 16 bits of `product >> SAR`, logical | wrap[^trm-mul] |
| `EE.CMUL.S16 qz, qx, qy, sel4` | 2 complex pairs per `sel4` value | s32 | `>> SAR` | wrap[^trm-mul] |

There is no 32-bit lane multiply and no widening multiply that keeps the full product in
a register pair, so the accumulators are the only route to a full product.[^trm-list]
There is also no way to add a rounding constant inside `EE.VMUL.*`, which is why the
rounding idiom below goes through `QACC`. Each form also exists with a fused 16-byte load
or store, spelled `.LD.INCP` or `.ST.INCP`, taking five operands: destination of the
load, address register, result, then the two multiplicands.[^trm-mul] A real loop over
two `int16_t` arrays:[^dsp-mul]

```asm
    wsr.sar a9                    // a9 holds the right shift for the products
    ee.vld.128.ip     q0, a2, 16
loop:
    ee.vld.128.ip     q1, a3, 16
    ee.vmul.s16.ld.incp q0, a2, q4, q0, q1   // q4 = (q0*q1) >> SAR, then q0 <- [a2], a2 += 16
    ee.vst.128.ip     q4, a4, 16
```

## Multiply accumulate

Two accumulators with different shapes. `ACCX` is one 40-bit register and the `.ACCX`
instructions sum every lane product into it, which makes them a horizontal reduce.
`QACC_L` and `QACC_H` together hold one wide accumulator per lane, and the `.QACC`
instructions keep the lanes apart.[^trm-regs]

| Instruction | Reads | Accumulates into | Saturates at |
|---|---|---|---|
| `EE.VMULAS.S16.ACCX qx, qy` | 8 x s16 pairs | one 40-bit `ACCX` | +/- 2^39[^trm-mac] |
| `EE.VMULAS.S8.ACCX qx, qy` | 16 x s8 pairs | one 40-bit `ACCX` | +/- 2^39[^trm-mac] |
| `EE.VMULAS.S16.QACC qx, qy` | 8 x s16 pairs | 8 x 40-bit lanes | +/- 2^39 per lane[^trm-mac] |
| `EE.VMULAS.S8.QACC qx, qy` | 16 x s8 pairs | 16 x 20-bit lanes | +/- 2^19 per lane[^trm-mac] |
| `EE.VSMULAS.S16.QACC qx, qy, sel8` | 8 x s16 against one lane of `qy` | 8 x 40-bit lanes | +/- 2^39 per lane[^trm-mac] |
| `EE.VSMULAS.S8.QACC qx, qy, sel16` | 16 x s8 against one lane of `qy` | 16 x 20-bit lanes | +/- 2^19 per lane[^trm-list] |

`U8` and `U16` variants of every `EE.VMULAS.*` form exist. The scalar-lane forms
`EE.VSMULAS.*` are signed only.[^trm-list]

The accumulator lane width is the detail people get backwards. A 16-bit multiply makes a
32-bit product, and those go into eight 40-bit lanes; an 8-bit multiply makes a 16-bit
product, and those go into sixteen 20-bit lanes. The manual's register overview describes
the same thing by product width rather than input width, which reads as a contradiction
until you spot the switch.[^trm-regs][^trm-mac] Every `EE.VMULAS.*` form also exists with
a fused load: `.LD.IP` with an immediate address step, `.LD.XP` with a register step, and
for the `.QACC` forms `.LDBC.INCP`, which loads one scalar and broadcasts it. A `.QUP`
suffix on top realigns the loaded data using `SAR_BYTE`.[^trm-list]

### Getting the accumulators back out

| Path | What it does |
|---|---|
| `EE.SRCMB.S16.QACC qu, as, sel2` | Shifts all 8 40-bit lanes right by `as[5:0]`, writes them back into `QACC`, and writes each lane saturated to s16 into `qu`[^trm-mac] |
| `EE.SRCMB.S8.QACC qu, as, sel2` | Same for 16 20-bit lanes, shift `as[4:0]`, saturated to s8[^trm-mac] |
| `EE.SRS.ACCX au, as, sel2` | Shifts `ACCX` right by `as[5:0]`, writes it back, and writes the value saturated to s32 into an `a` register[^trm-mac] |
| `RUR.ACCX_0`, `RUR.ACCX_1` | Read the raw 40 bits of `ACCX` as two `a` register halves[^gas-test][^dsp-dp8] |
| `RUR.QACC_L_0` to `_L_4`, `RUR.QACC_H_0` to `_H_4` | Read each 160-bit accumulator through five `a` register windows[^gas-test] |
| `EE.ST.QACC_[H/L].*`, `EE.LD.QACC_[H/L].*` | Move the accumulators to and from memory[^trm-list] |
| `WUR.ACCX_0`, `WUR.ACCX_1` | Write `ACCX`, which is how esp-dsp zeroes it[^dsp-dp8] |

Both shift-and-read instructions modify the accumulator they read, so save it first if it
has to survive. Zeroing is cheaper: `EE.ZERO.Q qa`, `EE.ZERO.QACC` and
`EE.ZERO.ACCX`.[^trm-mac] `EE.MOV.[S/U][8/16].QACC qs` seeds the per-lane accumulator from a `q` register by sign-
or zero-extending each lane into the matching accumulator lane, which is the only way to
preload it without touching memory.[^trm-mac]

## Adds, subtracts, compares and min or max

All signed. There is no unsigned add, subtract, compare, min or max.[^trm-list]

| Instruction | Lanes | Rule |
|---|---|---|
| `EE.VADDS.S[8/16/32] qa, qx, qy` | 16, 8 or 4 | saturating add[^trm-cmp] |
| `EE.VSUBS.S[8/16/32] qa, qx, qy` | 16, 8 or 4 | saturating subtract[^trm-list] |
| `EE.VMAX.S[8/16/32] qa, qx, qy` | 16, 8 or 4 | per-lane larger value[^trm-cmp] |
| `EE.VMIN.S[8/16/32] qa, qx, qy` | 16, 8 or 4 | per-lane smaller value[^trm-list] |
| `EE.VCMP.EQ.S[8/16/32] qa, qx, qy` | 16, 8 or 4 | all ones if equal, else zero[^trm-cmp] |
| `EE.VCMP.LT.S[8/16/32] qa, qx, qy` | 16, 8 or 4 | all ones if `qx < qy`[^trm-list] |
| `EE.VCMP.GT.S[8/16/32] qa, qx, qy` | 16, 8 or 4 | all ones if `qx > qy`[^trm-list] |

Each of these also exists with a fused `.LD.INCP` or `.ST.INCP` 16-byte
access.[^trm-list] The compares produce a lane mask of all ones or all zeros, the usual
shape for a masked select. There is no select instruction and no and-not, so a select
costs four: `EE.NOTQ` to invert the mask, two `EE.ANDQ`, one `EE.ORQ`.

## Bitwise and clear

| Instruction | Result |
|---|---|
| `EE.ANDQ qa, qx, qy` | `qa = qx & qy`[^trm-cmp] |
| `EE.ORQ qa, qx, qy` | `qa = qx \| qy`[^trm-cmp] |
| `EE.XORQ qa, qx, qy` | `qa = qx ^ qy`[^trm-list] |
| `EE.NOTQ qa, qx` | `qa = ~qx`[^trm-cmp] |
| `EE.ZERO.Q qa` | `qa = 0`[^trm-mac] |

## Shifts

This is the sharpest hole in the set. Lane shifts exist for 32-bit lanes only.

| Instruction | Shift amount | Lanes |
|---|---|---|
| `EE.VSL.32 qa, qs` | `SAR[5:0]`, zero fill | 4 x 32-bit, left[^trm-shift] |
| `EE.VSR.32 qa, qs` | `SAR[5:0]`, sign fill | 4 x 32-bit, arithmetic right[^trm-shift] |

There is no 8-bit or 16-bit lane shift at all.[^trm-list] The substitute is a multiply,
and it is exact:

- Left shift 16-bit lanes by `k`: `SAR = 0`, multiply by a vector of `2^k`. This works
  for `k` of 0 to 14 with `EE.VMUL.S16`, because `2^15` does not fit a signed 16-bit
  lane. For `k = 15` use `EE.VMUL.U16` against `0x8000`.
- Arithmetic right shift 16-bit lanes by `k`: `SAR = k`, multiply by a vector of 1 with
  `EE.VMUL.S16`.
- Logical right shift 16-bit lanes by `k`: the same with `EE.VMUL.U16`.

The 8-bit cases work the same way with `EE.VMUL.S8` and `EE.VMUL.U8`.

The other shifts move whole bytes across a 32-byte pair of registers. They exist to
extract unaligned 16-byte data, not to do arithmetic.

| Instruction | Amount | Notes |
|---|---|---|
| `EE.SRC.Q qa, qs0, qs1` | `SAR_BYTE * 8` bits, right | Reads the pair, writes one register[^trm-shift] |
| `EE.SRCI.2Q qs1, qs0, imm` | `(imm + 1) * 8` bits, right, zero fill | Writes both registers in place[^trm-shift] |
| `EE.SLCI.2Q qs1, qs0, imm` | `(imm + 1) * 8` bits, left, zero fill | Writes both registers in place[^trm-shift] |
| `EE.SRCXXP.2Q qs1, qs0, as, ad` | `(as[3:0] + 1) * 8` bits, right | Also does `as += ad`[^trm-shift] |
| `EE.SLCXXP.2Q qs1, qs0, as, ad` | `(as[3:0] + 1) * 8` bits, left | Also does `as += ad`[^trm-shift] |

The `+ 1` matters. The immediate and register forms cannot shift by zero bytes, and
their range is 1 to 16 bytes, not 0 to 15. Only `EE.SRC.Q` can shift by zero, because it
uses `SAR_BYTE` directly.[^trm-shift]

## Lane moves and inserts

| Instruction | Effect |
|---|---|
| `EE.MOVI.32.A qs, au, sel4` | Copy one of four 32-bit lanes of `qs` into an `a` register[^trm-lane] |
| `EE.MOVI.32.Q qu, as, sel4` | Copy an `a` register into one of four 32-bit lanes of `qu`[^trm-lane] |
| `MV.QR qu, qs` | Copy a whole `q` register. No `EE.` prefix, and it is the manual's last numbered entry[^trm-mvqr] |

Granularity is 32 bits in both directions, so reaching one byte or one 16-bit lane needs
a scalar shift or mask around the move. Building a constant vector from an immediate
takes four `EE.MOVI.32.Q`, so load constants from a 16-byte aligned table instead.

## Interleave, deinterleave and the widen idiom

`EE.VZIP.[8/16/32] qs0, qs1` and `EE.VUNZIP.[8/16/32] qs0, qs1` both read and write both
registers. They have no separate destination, so both inputs are destroyed.[^trm-lane]

`EE.VZIP.8` interleaves the pair byte by byte: `qs0` receives `qs0[0], qs1[0], qs0[1],
qs1[1]` through byte 7 of each, and `qs1` the same pattern from bytes 8 to 15.
`EE.VUNZIP.8` is the inverse, so `qs0` receives the eight even bytes of `qs0` then the
eight even bytes of `qs1`, and `qs1` the odd bytes in that order.[^trm-lane] That gives
the widening pair every 8-bit kernel needs, since there is no widening multiply:

```asm
    ee.zero.q  q1              // q1 = 0
    ee.vzip.8  q0, q1          // q0 = low 8 bytes as u16 lanes, q1 = high 8 bytes as u16 lanes
    ...                        // 16-bit lane arithmetic on q0 and q1
    ee.vunzip.8 q0, q1         // q0 = low byte of every lane, back in the original order
```

The zip against a zeroed register is a zero-extend. There is no sign-extending widen.
For signed bytes, either fix the sign after the zip, or accumulate through
`EE.VMULAS.S8.QACC` and read the 20-bit lanes back with `EE.SRCMB.S8.QACC`. The 16-bit
and 32-bit forms follow the same pattern at their lane width.[^trm-lane]

## The two activation instructions

Both come from the neural network side of the design, and both are a conditional
multiply rather than a clamp. `EE.VRELU.S[8/16] qs, ax, ay` replaces each lane that is not greater than zero with
`(lane * ax[15:0]) >> ay[5:0]` and leaves positive lanes alone, in place on
`qs`.[^trm-act] `EE.VPRELU.S[8/16] qz, qx, qy, ay` does the same with a per-lane
coefficient from `qy` and writes to `qz`.[^trm-act]

A plain clamp at zero is `EE.VMAX.S16` against a zeroed register, which is one
instruction and needs no `a` register setup.

## FFT helpers

Five families exist only for fixed-point FFTs. `EE.FFT.R2BF.S16` does a radix-2
butterfly, `EE.FFT.CMUL.S16.[LD.XP/ST.XP]` the complex butterfly multiply, and
`EE.FFT.AMS.S16.*` a whole add, multiply and shift step in four addressing flavours, one
of which carries unaligned state across calls in the `UA_STATE` register.
`EE.FFT.VST.R32.DECP` swaps the two 16-bit halves of each 32-bit word on the way out,
optionally shifting each half right by 1, and steps `as` back 16.[^trm-fftvst]
`EE.BITREV qa, as` is not a plain bit reversal: for eight consecutive values of a counter
in `as` it writes the larger of the value and its bit reversal, at a width set by
`FFT_BIT_WIDTH`, then adds 8 to `as`.[^trm-act] `EE.CMUL.S16` sits with the arithmetic
instructions but is the same complex multiply in general form. Its immediate does two
things at once: `sel4` of 0 or 2 works on the low 64 bits and 1 or 3 on the high 64 bits,
two complex pairs either way, and 0 or 1 gives `ac - bd` for the real part while 2 or 3
gives `ac + bd`, which is the conjugate.[^trm-mul] All of them assume the interleaved
complex layout, so outside an FFT they rarely fit.

## What is not there

| Missing | What you do instead |
|---|---|
| Multi-lane gather | There is none, but there is a one-lane indexed load. `EE.LDXQ.32 qu, qs, as, sel4, sel8` loads one 32-bit word using one 16-bit lane of `qs` scaled by 4 as the index, into one lane of `qu`. Filling all four lanes is 4 instructions plus the index setup[^trm-act] |
| Multi-lane scatter | `EE.STXQ.32` is the matching one-lane indexed store[^trm-list] |
| Per-lane variable shift | Every shift takes `SAR`, `SAR_BYTE` or an immediate, one value for the whole register[^trm-list] |
| 8-bit or 16-bit lane shift | Multiply by a power of two, with `SAR` for the right shift |
| 32-bit lane multiply | Nothing. Split into 16-bit pieces or leave the loop scalar |
| Widening multiply into a register pair | The `QACC` accumulators, read back with `EE.SRCMB.*` |
| Vector divide, reciprocal or square root | Nothing. Multiply by a reciprocal table entry |
| Float lanes | Nothing. `EE.LDF.*` and `EE.STF.*` move `FR` registers 64 or 128 bits at a time, but no PIE arithmetic is floating point[^trm-regs] |
| Horizontal reduce | `EE.VMULAS.*.ACCX` against a vector of ones, then `EE.SRS.ACCX` or `RUR.ACCX_0` |
| Unsigned compare, min or max | Bias the values into the signed range first |
| Select, blend, and-not | Four instructions: `EE.NOTQ`, two `EE.ANDQ`, `EE.ORQ` |
| Lane broadcast from another lane | `EE.VLDBC.[8/16/32]` broadcasts from memory, not from a register[^trm-list] |
| Population count, leading zero count | Nothing at lane width |

## Composition idioms

### Q15 multiply with rounding

`EE.VMUL.S16` truncates, because the shift happens inside the instruction and there is
nowhere to add the half. The accumulator can be seeded with the rounding constant before
the multiply. For `q15 * q15 -> q15` the constant is `2^14` and the shift is 15:

```asm
    // q3 holds 0x4000 in all eight 16-bit lanes, built once outside the loop
    movi    a4, 15
loop:
    ee.mov.s16.qacc  q3          // every 40-bit lane = 16384
    ee.vmulas.s16.qacc q0, q1    // lane += a * b, exact in 40 bits
    ee.srcmb.s16.qacc q2, a4, 0  // q2 = sat16((16384 + a*b) >> 15)
```

Three instructions per eight lanes instead of one, and the result saturates instead of
wrapping. The cheap version is `SAR = 15` and one `EE.VMUL.S16`, which truncates toward
negative infinity and wraps on the one input pair that overflows, `-1.0 * -1.0`.

### Multiply as a shift on 16-bit lanes

Lane math for a left shift by 5 and an arithmetic right shift by 3:

```asm
    // q6 holds 0x0020 (2^5) in all lanes, q7 holds 0x0001 in all lanes
    ssai    0
    ee.vmul.s16 q2, q0, q6       // (x * 32) >> 0, low 16 bits: x << 5, wrapping
    ssai    3
    ee.vmul.s16 q3, q0, q7       // (x * 1) >> 3, arithmetic
```

Switching between the two costs a `SAR` write, so group operations that share a shift
amount instead of alternating.

### RGB565 channel extraction

A 16-bit pixel holds red in bits 15 to 11, green in bits 10 to 5, blue in bits 4 to 0.
Unsigned multiplies do the shifts and `EE.ANDQ` does the masks. Red needs no mask,
because a logical right shift by 11 of a zero-extended 16-bit value leaves only the 5
bits.

```asm
    // q5 = 0x003F in all lanes, q6 = 0x001F in all lanes, q7 = 0x0001 in all lanes
    ssai    11
    ee.vmul.u16 q1, q0, q7       // q1 = p >> 11        red, 5 bits, no mask needed
    ssai    5
    ee.vmul.u16 q2, q0, q7       // q2 = p >> 5
    ee.andq     q2, q2, q5       // green, 6 bits
    ee.andq     q3, q0, q6       // blue, 5 bits
```

Repacking runs the other way, with `SAR = 0`, multiplies by `0x0800` and `0x0020`, then
two `EE.ORQ`. Three of the eight `q` registers hold constants before any pixel arrives,
which is the real cost of this idiom.

### 8-bit blend through 16-bit lanes

Widen, work at 16 bits, narrow. With a blend factor `f` in 0 to 255 and `g = 255 - f`,
both held in 16-bit lanes:

```asm
    ee.zero.q   q7
    ee.vzip.8   q0, q7           // q0, q7 = the a bytes widened to u16 lanes
    ee.zero.q   q6
    ee.vzip.8   q1, q6           // q1, q6 = the b bytes widened to u16 lanes
    ssai        8
    ee.vmul.u16 q0, q0, q4       // (a * f) >> 8,  max 254
    ee.vmul.u16 q1, q1, q5       // (b * g) >> 8,  max 254
    ee.vadds.s16 q0, q0, q1      // sum, max 254 because f + g = 255
    // repeat for the high halves in q7 and q6, then:
    ee.vunzip.8 q0, q7           // q0 = the blended bytes, original order
```

Two facts make this exact: the products fit 16 bits because `255 * 255` is 65025, and
the sum after both shifts cannot exceed 254 because `f + g = 255`, so the signed
saturating add never clamps. Recheck both if the factor range changes. The result carries
up to 2 counts of truncation error, one per shift.

### Saturating accumulation versus an exact sum

`EE.VADDS.S32` gives a running per-lane sum that clamps at the 32-bit limits instead of
wrapping, which is usually what a fixed-point pipeline wants. For an exact sum with more
headroom, `EE.VMULAS.S16.ACCX` against a vector of ones adds all eight lanes into the
40-bit `ACCX` in one instruction, and `EE.SRS.ACCX` reads it back shifted and saturated
to 32 bits. A dot product loop uses the fused form so the accumulate and the next load
are one instruction:[^dsp-dp8]

```asm
    movi.n  a14, 0
    wur.accx_0 a14
    wur.accx_1 a14
    ...
    ee.vmulas.s8.accx.ld.ip q1, a3, 16, q0, q1
    ...
    rur.accx_0 a14
```

## Documentation defects found while writing this

- The pseudo-code for `EE.MOV.S16.QACC` uses `qs[79]` and `qs[95]` as the sign bits for
  the two highest accumulator lanes, where it should use `qs[111]` and `qs[127]`. The
  prose above it is correct, and Espressif's QEMU model sign-extends each lane on its
  own.[^trm-mac][^qemu-mov]
- `EE.SRCMB.*.QACC` and `EE.SRS.ACCX` take a third operand that the manual writes as a
  literal `0` and never describes. Its instruction-word diagram shows no field for it at
  all: the bit it actually uses is drawn as a constant `0` inside the fixed field the
  diagram writes as `010` for `EE.SRCMB.*` and `001` for `EE.SRS.ACCX`. Four sources say
  four partial things. The toolchain's own configuration names the operand `sel2` and
  accepts 0 or 1, encoding a different word for each. esp-dsp passes 1, once, with no
  comment. Espressif's QEMU calls it `sel2` too, hands it to the `EE.SRCMB.*` helper,
  which never reads it, and for `EE.SRS.ACCX` does not decode it at all. The manual says
  nothing. What the bit does on silicon is therefore `[uncertain]`, and only a device
  test can settle it.[^gas-test][^dsp-dp8][^qemu-srcmb][^trm-mac]
- The register overview says `EE.VSR.32` and `EE.VSL.32` use "the lower 5 bits of SAR",
  while both instruction entries call the register 6 bits and read `SAR[5:0]`, and the
  QEMU model passes the register unmasked. Take the entries as the
  authority.[^trm-regs][^trm-shift][^qemu-vsx] What a 32-bit lane does when shifted by 32
  or more is stated nowhere, so keep the amount inside 0 to 31, where the two readings
  agree.
- `EE.CMUL.S16`'s prose puts the real part in the upper 16 bits of each 32-bit pair, but
  its own operation section treats the low 16 bits as the real part, and QEMU does the
  same. Follow the operation section: real in the low half, imaginary in the high half,
  which is also the order a `re, im` array lands in on a little-endian
  machine.[^trm-mul][^qemu-cmul] The two agreeing sources are not independent, since the
  QEMU model was plainly written from the operation section, so a device test would still
  be worth having.
- In esp-dsp's `dsps_add_s16_aes3`, the vector path writes `SAR` then runs
  `EE.VADDS.S16`, which does not read `SAR`, while the scalar fallback in the same file
  applies the shift. The two paths disagree about the `shift` argument. Read from the
  source, not tested.[^dsp-add]

[^trm-pie]: Espressif Systems, 2026. *ESP32-S3 Technical Reference Manual*, version 1.8, chapter 1, "Processor Instruction Extensions (PIE)". https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf
[^trm-regs]: Same manual, section 1.5.1 "Registers" (table 1.5-1) and section 1.5.1.2 "Special Registers".
[^trm-sat]: Same manual, section 1.5.4, "Data Overflow and Saturation Handling".
[^trm-list]: Same manual, section 1.6, "Extended Instruction List": table 1.6-1 and tables 1.6-2 through 1.6-17.
[^trm-mul]: Same manual, sections 1.8.122 `EE.VMUL.S16` (page 198), 1.8.123 `EE.VMUL.S16.LD.INCP`, 1.8.125 `EE.VMUL.S8`, 1.8.128 `EE.VMUL.U16`, 1.8.131 `EE.VMUL.U8`, 1.8.4 `EE.CMUL.S16` (page 80). `EE.CMUL.S16`'s description sentence reads "The real and imaginary parts of complex numbers are stored in the upper 16 bits and lower 16 bits of the 32 bits respectively", while its operation section computes `qz[15:0] = (qx[15:0]*qy[15:0] - qx[31:16]*qy[31:16]) >> SAR[5:0]`, which puts the real part in the low half.
[^trm-mac]: Same manual, sections 1.8.134 `EE.VMULAS.S16.ACCX` (page 210), 1.8.146 `EE.VMULAS.S8.ACCX`, 1.8.139 `EE.VMULAS.S16.QACC` (page 215), 1.8.151 `EE.VMULAS.S8.QACC` (page 227), 1.8.187 `EE.VSMULAS.S16.QACC` (page 269), 1.8.189 `EE.VSMULAS.S8.QACC`, 1.8.54 and 1.8.55 `EE.SRCMB.S16/S8.QACC` (pages 130 and 131), 1.8.58 `EE.SRS.ACCX` (page 134), 1.8.38 to 1.8.41 `EE.MOV.[S/U][8/16].QACC` (from page 114), 1.8.215 to 1.8.217 `EE.ZERO.ACCX`, `EE.ZERO.Q`, `EE.ZERO.QACC` (from page 298). Both `EE.VMULAS.*.ACCX` forms saturate the 41-bit sum to +/- 2^39. Neither `EE.SRCMB.*` nor `EE.SRS.ACCX` shows a field for its third operand in its instruction-word diagram.
[^trm-cmp]: Same manual, sections 1.8.70 `EE.VADDS.S16`, 1.8.79 `EE.VCMP.EQ.S16`, 1.8.104 `EE.VMAX.S16`. Bitwise ops: 1.8.1 `EE.ANDQ`, 1.8.44 `EE.NOTQ`, 1.8.45 `EE.ORQ`.
[^trm-shift]: Same manual, sections 1.8.186 `EE.VSL.32` (page 268), 1.8.191 `EE.VSR.32` (page 274), 1.8.49 `EE.SRC.Q` (page 125), 1.8.53 `EE.SRCI.2Q`, 1.8.47 `EE.SLCI.2Q`, 1.8.57 `EE.SRCXXP.2Q`, 1.8.48 `EE.SLCXXP.2Q`. `EE.VSL.32` and `EE.VSR.32` both read "the value in the 6-bit special register SAR" and both operate as a shift by `SAR[5:0]`.
[^trm-lane]: Same manual, sections 1.8.42 `EE.MOVI.32.A`, 1.8.43 `EE.MOVI.32.Q`, 1.8.207 `EE.VUNZIP.16`, 1.8.209 `EE.VUNZIP.8`, 1.8.210 `EE.VZIP.16`, 1.8.212 `EE.VZIP.8`.
[^trm-act]: Same manual, sections 1.8.182 `EE.VPRELU.S16` (page 264), 1.8.184 `EE.VRELU.S16` (page 266), 1.8.2 `EE.BITREV` (page 77), 1.8.37 `EE.LDXQ.32` (page 113). `EE.BITREV` writes `max(tmpN, SwitchW(tmpN))` for `tmp0 = as[15:0]` through `tmp7 = as[15:0] + 7`, zero-extends each to 16 bits, and ends with `as = as + 8`.

[^trm-mvqr]: Same manual, section 1.8.220 `MV.QR`, page 303, the last numbered entry of Chapter 1.

[^trm-fftvst]: Same manual, section 1.8.15 `EE.FFT.VST.R32.DECP`, page 91. It stores `{qv[31:16], qv[15:0], qv[63:48], qv[47:32], ...}`, each half shifted right by the immediate `sar2` of 0 or 1, then does `as = as - 16`.

[^isa-ssr]: Cadence Tensilica, 2010. *Xtensa Instruction Set Architecture (ISA) Reference Manual*, issue 4/2010 (RC-2010.1). `SSR`, page 539: only `AR[s]4..0` is written to SAR and the most significant bit of SAR is cleared, unlike `WSR.SAR`, which writes `AR[t]5..0`. `SSAI`, page 533: syntax `SSAI 0..31`.
[^gas-test]: [measured] Assembled with `xtensa-esp32s3-elf-as`, GNU assembler 2.43.1 (crosstool-NG esp-14.2.0_20241119), 2026-09-06. `ee.srcmb.s16.qacc q2, a4, 0` encodes as `dd7244` and `..., 1` as `dd7644`; `ee.srs.accx a5, a4, 0` encodes as `7e1544` and `..., 1` as `7e5544`; a third operand of 2 or more is rejected as an invalid value. Confirms the mnemonics and operand ranges quoted here: `EE.SLCI.2Q` immediates 0 to 15, `EE.SRS.ACCX` and `EE.SRCMB.*` third operand 0 or 1 with different encodings, `EE.MOVI.32.A` selector 0 to 3, `EE.VSMULAS.S16.QACC` selector 0 to 7, `EE.CMUL.S16` selector 0 to 3, `SSAI` immediate 0 to 31, and the `RUR`/`WUR` names `ACCX_0` to `ACCX_1`, `QACC_L_0` to `QACC_L_4`, `QACC_H_0` to `QACC_H_4`. Assembling proves the syntax the toolchain accepts, not the semantics.
[^dsp-mul]: espressif/esp-dsp, `modules/math/mul/fixed/dsps_mul_s16_aes3.S`, commit `3c8ac0fdfec83740b783e200862c8d0c056de0ad`. https://github.com/espressif/esp-dsp
[^dsp-add]: espressif/esp-dsp, same commit, `modules/math/add/fixed/dsps_add_s16_aes3.S`.
[^dsp-dp8]: espressif/esp-dsp, same commit, `modules/dotprod/fixed/dsps_dp_s8_aes3.S`; `modules/matrix/mul/fixed/dspm_mult_s16_aes3.S` is the `EE.SRCMB` and `EE.VMULAS.*.LDBC` example.
[^qemu-mov]: espressif/qemu, `target/xtensa/translate_tie_esp32s3.c`, `HELPER(mov_qacc_s3)`, branch `esp-develop`, commit `febae182e132e4055529be423a818225ebddaa3a`. https://github.com/espressif/qemu

[^qemu-vsx]: Same file and commit, `translate_vsx32_s3`, which hands `HELPER(vsx32_s3)` the value of `cpu_SR[SAR]` with no mask applied.

[^qemu-cmul]: Same file and commit, `HELPER(cmul_s3)`. For `op_type` 0 it writes `Q[qz].s16[0] = (Q[qx].s16[0]*Q[qy].s16[0] - Q[qx].s16[1]*Q[qy].s16[1]) >> sar`, so lane 0, the low 16 bits, holds the real part.
[^qemu-srcmb]: espressif/qemu, same file and commit. `translate_srcmb_qacc_s3` passes `arg[2].imm` to `HELPER(srcmb_qacc_s3)` as a parameter named `sel`, which the helper body never reads; the opcode table comments the syntax as `qu, as, sel2`. `translate_srs_accx_s3` does not read `arg[2]` at all. The toolchain names the same operand `sel2`: the ESP32-S3 dynconfig carries `fld_semantic.SRCMB_QACC_sel2` and `fld_ee_srs_accx_sel2`.
