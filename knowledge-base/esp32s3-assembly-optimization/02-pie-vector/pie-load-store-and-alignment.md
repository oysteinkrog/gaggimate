---
title: PIE load and store instructions and their alignment rules
id: 02-pie-vector/pie-load-store-and-alignment
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, pie, simd, alignment, load-store]
confidence: high
---

# PIE load and store instructions and their alignment rules

The ESP32-S3's Processor Instruction Extensions (PIE) move data between memory
and the eight 128-bit `q` registers. Every one of those instructions forces the
low address bits to zero before the access. Nothing traps. A misaligned pointer
gives you the wrong 16 bytes, and on a store it overwrites the wrong 16 bytes.
This leaf lists the forms, their operands, their immediate ranges, and the rules
that keep them correct. For what a `q` register is and how it is saved across a
context switch, see
[PIE register file, state registers and context save](./pie-register-file-sar-and-context.md).

## The addressing model

Every PIE memory instruction takes an address register `as`. There are two
post-increment flavours, and the suffix names them:

- `.IP` adds an immediate to `as` after the access. The immediate is encoded as
  a small signed field, left-shifted by the access width, so the range and the
  granularity are both fixed by the instruction.[^trm-vld128ip]
- `.XP` adds the value of a second address register `ad` to `as` after the
  access.[^trm-vld128xp]
- `.INCP` adds a fixed step. It is 16 for the 128-bit fused forms and 2 for the
  16-bit broadcast fused form.[^trm-vadds-ld-incp][^trm-vmulas-ldbc]

Two properties matter more than they look:

1. **The access address is masked, `as` is not.** The pseudocode is
   `qu = load128({as[31:4], 4{0}})` then `as = as + imm`.[^trm-vld128ip] The
   increment applies to the original register value, low bits included, so a
   stream that starts three bytes off stays three bytes off forever.
2. **The increment is free.** It is part of the same instruction, which is why
   PIE loops are written around a walking pointer rather than an index.

## The alignment rule, in the manual's own words

The TRM is explicit, and it does not say "undefined":

> all access addresses in the extended instruction set are forced to be aligned,
> i.e., the lowest bits will be replaced by 0. For example, if a read operation
> is initiated for 128-bit data at 0x3fc8_0024, the lowest 4-bit of this access
> address will be forced to be set to 0. Eventually, the 128-bit data stored at
> 0x3fc8_0020 will be read.[^trm-align]

The same rule is restated for reads and for writes in the instruction
overview,[^trm-read][^trm-write] and again in every individual entry in section
1.8, each naming its own bit count.

| Access width | Bits forced to zero | Effective granularity |
|---|---|---|
| 128-bit | low 4 | 16 bytes |
| 64-bit | low 3 | 8 bytes |
| 32-bit | low 2 | 4 bytes |
| 16-bit | low 1 | 2 bytes |
| 8-bit | none | 1 byte |

Only the 8-bit broadcast load takes the address as given.[^trm-vldbc8]

This is documented behaviour, not an observation. Section 1.5.3 states the
rule, every entry in Section 1.8 repeats it with its own bit count, and
Espressif's own QEMU fork implements it the same way: `ee.vld.128.*` translates
to `tcg_gen_andi_i32(addr, arg[1].in, 0xfffffff0)` before the memory op, and
`ee.ld.128.usar.*` additionally captures `arg[1].in & 0xf` into
`SAR_BYTE`.[^trm-align][^qemu-tie] To demonstrate it on hardware, put a known
16-byte pattern in an aligned buffer, run one `ee.vld.128.ip` from `base + k`
for `k = 0..15`, and compare: all sixteen results come back identical.

### Why this is dangerous rather than merely surprising

The instruction bus behaves the opposite way. The CPU may access data over the
data bus at single-byte, double-byte, 4-byte and 16-byte alignment, but over the
instruction bus only in a 4-byte aligned manner, and a non-aligned access there
raises a CPU exception.[^trm-addrmap] A scalar bug against instruction memory
announces itself. A PIE alignment bug does not. A load silently returns the
aligned block containing the pointer, so the kernel keeps running and the output
looks like an arithmetic bug. A store silently overwrites up to 16 bytes at the
aligned address below the pointer, so the damage lands on a neighbour and the
symptom shows up in unrelated data.

The rules that follow:

- Declare buffers with `aligned(16)`, or allocate them with
  `heap_caps_aligned_alloc`.[^trm-align]
- Never form a `q`-register pointer by adding a byte offset that is not a
  multiple of 16. If the offset is not a compile-time constant, use the
  unaligned path below.
- If a span's start is not guaranteed aligned, either run a scalar prologue to
  the next 16-byte boundary plus a scalar epilogue, or take the `SAR_BYTE`
  route.
- Espressif's own DSP library takes the first route by refusing the vector path
  entirely: the S3 fixed-point add checks `bany a2, 0xF` and `bany a3, 0xF`
  against both input pointers and branches to a scalar loop if either is not
  16-byte aligned.[^espdsp-add]

## The 128-bit and 64-bit forms

```
ee.vld.128.ip / .xp    qu, as, -2048..2032 | ad      # 16-byte steps
ee.vst.128.ip / .xp    qv, as, -2048..2032 | ad
ee.vld.l.64.ip / .xp   qu, as, -1024..1016 | ad      # 8-byte steps, qu[63:0]
ee.vld.h.64.ip / .xp   qu, as, -1024..1016 | ad      # qu[127:64]
ee.vst.l.64.ip / .xp   qv, as, -1024..1016 | ad
ee.vst.h.64.ip / .xp   qv, as, -1024..1016 | ad
ld.qr / st.qr          qu, as, -128..112             # no write-back to as
mv.qr                  qu, qs                        # register to register
```

The 128-bit immediate is an 8-bit signed field left-shifted by 4 and the 64-bit
one is left-shifted by 3, which fixes the range and the granularity
together.[^trm-vld128ip][^trm-vst128ip][^trm-vld64] The assembler enforces both:
`ee.vld.128.ip q0, a2, 8` and `ee.vld.128.ip q0, a2, 2048` are rejected as an
invalid operand value, while 16, `-2048` and 2032 assemble [measured].[^gas]
The half forms are the only way to touch half a vector, useful for an 8-byte
tail and for building a 128-bit value from two 8-byte sources.

Note the missing `ee.` prefix on `ld.qr`, `st.qr` and `mv.qr`. They are the
last three numbered entries of the chapter, 1.8.218 to 1.8.220, and the
assembler accepts `-128..112` in steps of 16, which matches the entry's own
syntax line.[^trm-ldqr][^gas] Two things about `LD.QR` are worth knowing. Its
pseudocode is written `qu = load128(as + imm)` with no masking, unlike every
other load entry, but Section 1.5.3's rule covers the whole extended
instruction set and QEMU does mask; QEMU masks `as` with `0xfffffff0` first and
adds the displacement afterwards, which comes to the same address because the
displacement is always a multiple of 16.[^trm-align][^trm-ldqr][^qemu-tie] And
the `ST.QR` entry's syntax line reads `LD.QR`, a typographical slip in the
manual.[^trm-ldqr]

## The broadcast loads

A broadcast load reads one small element and replicates it across the whole
register. Each width has a bare form with no write-back plus both
post-increment forms.

```
ee.vldbc.8  qu, as / .ip qu, as, 0..127 / .xp qu, as, ad   # 16 lanes, no mask
ee.vldbc.16 qu, as / .ip qu, as, 0..254 / .xp qu, as, ad   # 8 lanes, low 1 bit forced
ee.vldbc.32 qu, as / .ip qu, as, -512..508 / .xp qu, as, ad # 4 lanes, low 2 bits forced
ee.vldhbc.16.incp qu, qu1, as                              # +16, low 4 bits forced
```

The 8-bit and 16-bit immediates are unsigned 7-bit fields, so they cannot walk
backwards; the 32-bit immediate is signed 8-bit.[^trm-vldbc] That last one is a
documentation defect of the same shape as `EE.ST.ACCX.IP` below: the syntax line
for `EE.VLDBC.32.IP` reads `-256..252`, while its own description says the
8-bit signed field is left-shifted by 2, which is `-512..508`, and that is what
the assembler accepts.[^trm-vldbc][^gas] The half-broadcast form reads 16 bytes
and spreads eight 16-bit elements across two registers with each element
doubled.[^trm-vldhbc]

## The unaligned path: SAR_BYTE and the funnel shift

When the address genuinely is not known to be aligned, the extension gives you a
byte funnel shift driven by a dedicated 4-bit special register, `SAR_BYTE`.[^trm-regs]
This is separate from the scalar `SAR`.

The pieces:

```
ee.ld.128.usar.ip  qu, as, -2048..2032   # load masked, SAR_BYTE = as[3:0], as += imm
ee.ld.128.usar.xp  qu, as, ad
ee.src.q           qa, qs0, qs1          # qa = {qs1,qs0} >> (SAR_BYTE*8)
ee.src.q.qup       qa, qs0, qs1          # same, then qs0 = qs1
ee.src.q.ld.ip     qu, as, imm, qs0, qs1 # shift into qs0, and load qu, as += imm
ee.src.q.ld.xp     qu, as, ad, qs0, qs1
ee.srcq.128.st.incp qs0, qs1, as         # store the shifted result, as += 16
```

`ee.ld.128.usar.*` does two things at once. It loads the aligned 16 bytes that
contain the pointer, and it saves the pointer's low four bits into `SAR_BYTE`
before the increment.[^trm-usar] `ee.src.q` then right-shifts the 32-byte
concatenation `{qs1, qs0}` by `SAR_BYTE * 8` bits and keeps the low 128
bits.[^trm-srcq] The TRM calls this an arithmetic right shift; because only the
low half of the concatenation is kept, it behaves as a byte-granular funnel
shift over the two registers.

The streaming pattern, step by step:

1. Point `as` at the unaligned source.
2. `ee.ld.128.usar.ip q2, as, 16` latches the byte offset and gives the first
   aligned block. `as` moves on 16 bytes, still unaligned.
3. `ee.vld.128.ip q3, as, 16` gives the adjacent block. The two straddle the
   data you want.
4. `ee.src.q q4, q2, q3` produces the 16 bytes at the original address.
5. Roll the window: the block ahead becomes the block behind. `ee.src.q.qup`
   shifts and rolls in one instruction; `ee.src.q.ld.ip` shifts, rolls into
   `qs0`, and loads the next block in one instruction.

The TRM gives the minimal three-line case.[^trm-align] A production loop looks
like the unaligned branch of Espressif's `memcpy` for the S3, which keeps a
three-register rotation and moves 48 bytes per iteration:[^espdsp-memcpy]

```
ee.ld.128.usar.ip   q2,  a3,  16            # preload, latch SAR_BYTE
ee.ld.128.usar.ip   q3,  a3,  16            # preload
loopnez a5, ._main_loop_unaligned
    ee.src.q.ld.ip    q4,  a3,  16, q2, q3  # shift into q2, load q4
    ee.vst.128.ip     q2,  a2,  16          # store to the aligned destination
    ee.src.q.ld.ip    q2,  a3,  16, q3, q4  # shift into q3, load q2
    ee.vst.128.ip     q3,  a2,  16
    ee.src.q.ld.ip    q3,  a3,  16, q4, q2  # shift into q4, load q3
    ee.vst.128.ip     q4,  a2,  16
._main_loop_unaligned:
```

Two things to copy from it. The destination is always aligned and only the
source runs through the funnel, so the routine spends a scalar prologue getting
the destination onto a 16-byte boundary rather than funnelling both sides. And
the rotation is unrolled by three so each `ee.src.q.ld.ip` names a distinct
register pair, which removes the register moves an unrolled-by-one version
would need.

`ee.srcq.128.st.incp` is the store-side counterpart: it shifts and writes the
result straight to memory with a `+16` step, without materialising it in a named
register.[^trm-srcqst]

## Fused load-and-op and store-and-op forms

Many arithmetic instructions carry a 128-bit memory access in the same
instruction word. The access is masked and post-incremented exactly like the
standalone form. These are the suffixes and which operations carry them:

| Suffix | Access | Step | Instructions that have it |
|---|---|---|---|
| `.LD.INCP` | 128-bit load into `qu` | +16 | `EE.VADDS.S[8/16/32]`, `EE.VSUBS.S[8/16/32]`, `EE.VMUL.[U/S][8/16]`, `EE.CMUL.S16`, `EE.VMAX.S[8/16/32]`, `EE.VMIN.S[8/16/32]`, `EE.VSMULAS.S[8/16].QACC`, `EE.FFT.AMS.S16` |
| `.ST.INCP` | 128-bit store from `qv` | +16 | `EE.VADDS.S[8/16/32]`, `EE.VSUBS.S[8/16/32]`, `EE.VMUL.[U/S][8/16]`, `EE.CMUL.S16`, `EE.VMAX.S[8/16/32]`, `EE.VMIN.S[8/16/32]`, `EE.FFT.R2BF.S16`, `EE.FFT.AMS.S16`, `EE.SRCQ.128` |
| `.LD.IP` | 128-bit load into `qu` | signed immediate, 16-byte steps | `EE.VMULAS.[U/S][8/16].ACCX`, `EE.VMULAS.[U/S][8/16].QACC`, `EE.SRC.Q` |
| `.LD.XP` | 128-bit load into `qu` | register `ad` | the same three |
| `.LDBC.INCP` | 16-bit broadcast load into `qu` | +2 | `EE.VMULAS.[U/S][8/16].QACC` |
| `.QUP` | no extra access | n/a | appended to the `EE.VMULAS.*.LD.IP/LD.XP/LDBC.INCP` forms and to `EE.SRC.Q`; adds the unaligned funnel shift on `qs0, qs1` |
[^trm-list]

The FFT family carries its own fused forms outside this table:
`EE.FFT.CMUL.S16.LD.XP` and `.ST.XP` take a register step, and
`EE.FFT.AMS.S16` exists in four addressing flavours including a `.LD.R32.DECP`
that steps backwards.[^trm-list]

The fused immediate is narrower than the standalone one. `EE.VMULAS.*.LD.IP`
encodes a 6-bit signed field left-shifted by 4, so its range is `-512..496`,
not `-2048..2032`.[^trm-vmulas-ldip] The assembler agrees: 496 and `-512`
assemble, 512 does not [measured].[^gas]

In the same DSP library the fused add makes a saturating 8-sample add loop three
instructions long: `ee.vld.128.ip`, `ee.vadds.s16.ld.incp`, `ee.vst.128.ip`,
with the second one both adding and fetching the next input.[^espdsp-add]

## Accumulator and state load and store

There is no `EE.STQA`. The accumulator move is asymmetric: the load side widens
memory into `QACC`, and the store side only spills the raw register.

```
ee.ldqa.[u8/s8/u16/s16].128.ip / .xp  as, -2048..2032 | ad  # widen into QACC
ee.ld / ee.st .qacc_[h/l].l.128.ip    as, -2048..2032       # raw, 16 bytes
ee.ld / ee.st .qacc_[h/l].h.32.ip     as, -512..508         # raw, 4 bytes
ee.ld / ee.st .accx.ip                as, -1024..1016       # 8 bytes, 40-bit ACCX
ee.ld / ee.st .ua_state.ip            as, -2048..2032       # 16 bytes
```

`EE.LDQA.U8` slices 16 bytes into sixteen lanes and zero-extends each to 20 bits;
`U16` slices into eight lanes of 40 bits; the `S8` and `S16` forms sign-extend
instead.[^trm-ldqa] `QACC_H` and `QACC_L` are 160 bits each and `ACCX` is 40
bits, which is why the raw spill of a `QACC` half needs both a 128-bit and a
32-bit instruction.[^trm-regs]

The TRM has a documentation defect here. The syntax line for `EE.ST.ACCX.IP`
gives `-512..508`, but the description right below it says the 8-bit
sign-extended constant is left-shifted by 3, which is `-1024..1016`, and that
is what the assembler accepts.[^trm-staccx][^gas] Trust the description and the
assembler. The matching load, `EE.LD.ACCX.IP`, has the correct range on its
syntax line.[^trm-ldaccx]

## Indexed load and store: a one-lane gather, not a vector one

Two instructions compute the address from a `q` register instead of taking it
from `as` alone:

```
ee.ldxq.32  qu, qs, as, 0..3, 0..7
ee.stxq.32  qv, qs, as, 0..3, 0..7
```

These are the extension's only indexed accesses, and they are one lane wide.
`EE.LDXQ.32` picks the 16-bit lane of `qs` named by the second immediate,
multiplies it by 4, adds it to `as`, forces the low 2 bits of the result to
zero, and writes the loaded word into the 32-bit lane of `qu` named by the first
immediate.[^trm-ldxq] `EE.STXQ.32` mirrors it. There is no multi-lane gather:
both selectors are encoded in the instruction, so filling all four 32-bit lanes
of `qu` from four table entries costs four instructions plus the index setup,
not one.

## The `f` register forms

The extension can also move 64 or 128 bits between memory and the scalar
floating-point register file, two or four `f` registers at a time:

```
ee.ldf.128.ip / .xp  fu3, fu2, fu1, fu0, as, -128..112 | ad   # 16-byte steps
ee.ldf.64.ip  / .xp  fu1, fu0, as, -1024..1016 | ad           # 8-byte steps
ee.stf.128.ip / .xp  fv3, fv2, fv1, fv0, as, -128..112 | ad
ee.stf.64.ip  / .xp  fv1, fv0, as, -1024..1016 | ad
```

The 128-bit forms carry only a 4-bit signed immediate, so their reach is
`-128..112`, far shorter than the `q`-register forms, and the lowest `f`
register in the list takes the lowest 32 bits.[^trm-ldf128] These touch the FPU
register file, so the lazy coprocessor context rules apply as for any float
code; see
[coprocessors, CPENABLE and lazy context](../00-foundations/coprocessors-cpenable-and-lazy-context.md).

## Where the operand lives

- **PSRAM.** External memory is mapped into the address space and the CPU reaches
  it through the cache.[^trm-addrmap] A PIE load is a data-bus load, so it takes
  the same cache path and the same miss cost as a scalar load of the same
  address. That the PIE unit has no separate port or bypass is `[uncertain]`:
  the TRM states the rule for CPU access to external memory but does not restate
  it for the extension. See
  [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)
  for the miss costs.
- **Internal DRAM.** The data bus accepts single-byte, double-byte, 4-byte and
  16-byte aligned access, so both scalar byte access and PIE 128-bit access are
  fine.[^trm-addrmap]
- **IRAM.** Instruction memory can only be read or written via 4-byte aligned
  words, and an unaligned access over the instruction bus raises a CPU
  exception.[^trm-addrmap][^idf-memtypes] A scalar `l8ui` against an IRAM address
  is a fault, not a slow path. Whether a 128-bit PIE access to an instruction-bus
  address is serviced correctly is `[uncertain]`: the address would satisfy the
  4-byte rule, but the TRM does not say the instruction bus carries a 16-byte
  data access. Keep PIE-read tables in DRAM, where the data bus is documented to
  carry the 16-byte form. See
  [the ESP32-S3 memory map and address spaces](../00-foundations/esp32s3-memory-map-and-address-spaces.md).

## Pipeline note

Table 1.7-2 of the TRM gives the use and def pipeline stage of every operand.
For every 128-bit load the loaded register `qu` is defined at stage 2 while the
updated `as` is defined at stage 1, and the plain arithmetic instructions
use their `q` operands at stage 1.[^trm-pipeline] That one-stage gap is the
load-use distance a PIE loop has to hide, which is why the loop bodies above
load one iteration ahead. Every 128-bit load defines its `qu` at stage 2,
with no exceptions. The use side does have exceptions: the four FFT store
forms (`EE.CMUL.S16.ST.INCP`, `EE.FFT.CMUL.S16.ST.XP`,
`EE.FFT.AMS.S16.ST.INCP`, `EE.FFT.VST.R32.DECP`) read their `qv` at stage 2,
and `EE.VRELU.S8` and `EE.VRELU.S16` define `qs` at stage 2, the same as a
load.[^trm-pipeline] The per-instruction tables and the interlock arithmetic
are in [the PIE hazards and latency leaf](pie-hazards-latencies-and-issue-rules.md);
the scalar pipeline model is in
[the LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md).

## Every load and store mnemonic

Widths are the memory access, not the register. "Forced" is the number of low
address bits replaced by zero. TRM references are to version 1.8 section 1.8
unless noted.

| Mnemonic | Width | Forced | Increment | Source |
|---|---|---|---|---|
| `ld.qr qu, as, imm` | 128 | 4 | none (displacement `-128..112`, step 16) | 1.8.218; the entry's own pseudocode shows no mask[^trm-ldqr][^qemu-tie] |
| `st.qr qv, as, imm` | 128 | 4 | none (displacement `-128..112`, step 16) | 1.8.219; as above[^trm-ldqr][^qemu-tie] |
| `mv.qr qu, qs` | none | n/a | none | 1.8.220[^trm-ldqr] |
| `ee.vld.128.ip qu, as, imm` | 128 | 4 | imm `-2048..2032`, step 16 | 1.8.88[^trm-vld128ip] |
| `ee.vld.128.xp qu, as, ad` | 128 | 4 | `ad` | 1.8.89[^trm-vld128xp] |
| `ee.vst.128.ip qv, as, imm` | 128 | 4 | imm `-2048..2032`, step 16 | 1.8.192[^trm-vst128ip] |
| `ee.vst.128.xp qv, as, ad` | 128 | 4 | `ad` | 1.8.193[^trm-vst128ip] |
| `ee.vld.l.64.ip qu, as, imm` | 64 | 3 | imm `-1024..1016`, step 8 | 1.8.92[^trm-vld64] |
| `ee.vld.h.64.ip qu, as, imm` | 64 | 3 | imm `-1024..1016`, step 8 | 1.8.90[^trm-vld64] |
| `ee.vld.l.64.xp qu, as, ad` | 64 | 3 | `ad` | 1.8.93[^trm-vld64] |
| `ee.vld.h.64.xp qu, as, ad` | 64 | 3 | `ad` | 1.8.91[^trm-vld64] |
| `ee.vst.l.64.ip qv, as, imm` | 64 | 3 | imm `-1024..1016`, step 8 | 1.8.196[^trm-vst64] |
| `ee.vst.h.64.ip qv, as, imm` | 64 | 3 | imm `-1024..1016`, step 8 | 1.8.194[^trm-vst64] |
| `ee.vst.l.64.xp qv, as, ad` | 64 | 3 | `ad` | 1.8.197[^trm-vst64] |
| `ee.vst.h.64.xp qv, as, ad` | 64 | 3 | `ad` | 1.8.195[^trm-vst64] |
| `ee.vldbc.8 qu, as` | 8 | 0 | none | 1.8.100[^trm-vldbc8] |
| `ee.vldbc.8.ip qu, as, imm` | 8 | 0 | imm `0..127`, step 1 | 1.8.101[^trm-vldbc] |
| `ee.vldbc.8.xp qu, as, ad` | 8 | 0 | `ad` | 1.8.102[^trm-vldbc] |
| `ee.vldbc.16 qu, as` | 16 | 1 | none | 1.8.94[^trm-vldbc] |
| `ee.vldbc.16.ip qu, as, imm` | 16 | 1 | imm `0..254`, step 2 | 1.8.95[^trm-vldbc] |
| `ee.vldbc.16.xp qu, as, ad` | 16 | 1 | `ad` | 1.8.96[^trm-vldbc] |
| `ee.vldbc.32 qu, as` | 32 | 2 | none | 1.8.97[^trm-vldbc] |
| `ee.vldbc.32.ip qu, as, imm` | 32 | 2 | imm `-512..508`, step 4, per the description and the assembler; the syntax line says `-256..252` | 1.8.98[^trm-vldbc][^gas] |
| `ee.vldbc.32.xp qu, as, ad` | 32 | 2 | `ad` | 1.8.99[^trm-vldbc] |
| `ee.vldhbc.16.incp qu, qu1, as` | 128 | 4 | +16 | 1.8.103[^trm-vldhbc] |
| `ee.ld.128.usar.ip qu, as, imm` | 128 | 4 | imm `-2048..2032`, step 16 | 1.8.17[^trm-usar] |
| `ee.ld.128.usar.xp qu, as, ad` | 128 | 4 | `ad` | 1.8.18[^trm-usar] |
| `ee.src.q.ld.ip qu, as, imm, qs0, qs1` | 128 | 4 | imm `-2048..2032`, step 16 | 1.8.50[^trm-srcqld] |
| `ee.src.q.ld.xp qu, as, ad, qs0, qs1` | 128 | 4 | `ad` | 1.8.51[^trm-srcqld] |
| `ee.srcq.128.st.incp qs0, qs1, as` | 128 | 4 | +16 | 1.8.56[^trm-srcqst] |
| `ee.ldf.128.ip fu3..fu0, as, imm` | 128 | 4 | imm `-128..112`, step 16 | 1.8.25[^trm-ldf128] |
| `ee.ldf.128.xp fu3..fu0, as, ad` | 128 | 4 | `ad` | 1.8.26[^trm-ldf128] |
| `ee.ldf.64.ip fu1, fu0, as, imm` | 64 | 3 | imm `-1024..1016`, step 8 | 1.8.27[^trm-ldf64] |
| `ee.ldf.64.xp fu1, fu0, as, ad` | 64 | 3 | `ad` | 1.8.28[^trm-ldf64] |
| `ee.stf.128.ip fv3..fv0, as, imm` | 128 | 4 | imm `-128..112`, step 16 | 1.8.65[^trm-stf] |
| `ee.stf.128.xp fv3..fv0, as, ad` | 128 | 4 | `ad` | 1.8.66[^trm-stf] |
| `ee.stf.64.ip fv1, fv0, as, imm` | 64 | 3 | imm `-1024..1016`, step 8 | 1.8.67[^trm-stf] |
| `ee.stf.64.xp fv1, fv0, as, ad` | 64 | 3 | `ad` | 1.8.68[^trm-stf] |
| `ee.ldqa.[u8/s8/u16/s16].128.ip as, imm` | 128 | 4 | imm `-2048..2032`, step 16 | 1.8.29 to 1.8.36[^trm-ldqa] |
| `ee.ldqa.[u8/s8/u16/s16].128.xp as, ad` | 128 | 4 | `ad` | 1.8.29 to 1.8.36[^trm-ldqa] |
| `ee.ld.qacc_[h/l].l.128.ip as, imm` | 128 | 4 | imm `-2048..2032`, step 16 | 1.8.21, 1.8.23[^trm-qacc] |
| `ee.ld.qacc_[h/l].h.32.ip as, imm` | 32 | 2 | imm `-512..508`, step 4 | 1.8.20, 1.8.22[^trm-qacc] |
| `ee.st.qacc_[h/l].l.128.ip as, imm` | 128 | 4 | imm `-2048..2032`, step 16 | 1.8.61, 1.8.63[^trm-qacc] |
| `ee.st.qacc_[h/l].h.32.ip as, imm` | 32 | 2 | imm `-512..508`, step 4 | 1.8.60, 1.8.62[^trm-qacc] |
| `ee.ld.accx.ip as, imm` | 64 | 3 | imm `-1024..1016`, step 8 | 1.8.19[^trm-ldaccx] |
| `ee.st.accx.ip as, imm` | 64 | 3 | imm `-1024..1016`, step 8 | 1.8.59 pseudocode and the assembler; the syntax line disagrees[^trm-staccx][^gas] |
| `ee.ld.ua_state.ip as, imm` | 128 | 4 | imm `-2048..2032`, step 16 | 1.8.24[^trm-uastate] |
| `ee.st.ua_state.ip as, imm` | 128 | 4 | imm `-2048..2032`, step 16 | 1.8.64[^trm-uastate] |
| `ee.ldxq.32 qu, qs, as, sel4, sel8` | 32 | 2 | address computed from `qs` lane | 1.8.37[^trm-ldxq] |
| `ee.stxq.32 qv, qs, as, sel4, sel8` | 32 | 2 | address computed from `qs` lane | 1.8.69[^trm-ldxq] |
| `ee.<op>.ld.incp qu, as, ...` | 128 | 4 | +16 | 1.8.71 and the other fused forms[^trm-vadds-ld-incp] |
| `ee.<op>.st.incp qv, as, ...` | 128 | 4 | +16 | 1.8.72 and the other fused forms[^trm-vadds-st-incp] |
| `ee.vmulas.*.ld.ip qu, as, imm, ...` | 128 | 4 | imm `-512..496`, step 16 | 1.8.140 and siblings[^trm-vmulas-ldip] |
| `ee.vmulas.*.ldbc.incp qu, as, ...` | 16 | 1 | +2 | 1.8.144 and siblings[^trm-vmulas-ldbc] |

## Footnotes

[^trm-align]: Espressif, 2026. *ESP32-S3 Technical Reference Manual*, version 1.8, section 1.5.3 "Data Format and Alignment", pages 48 to 49. https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf
[^trm-read]: Same manual, section 1.6.1 "Read Instructions" and Table 1.6-2, pages 53 to 54.
[^trm-write]: Same manual, section 1.6.2 "Write Instructions" and Table 1.6-3, pages 54 to 55.
[^trm-list]: Same manual, Table 1.6-1 "Extended Instruction List", pages 51 to 53, and the category tables 1.6-2 to 1.6-17, pages 54 to 65. Section 1.8 numbers 220 entries: 1.8.1 to 1.8.217 are the `EE.*` instructions, 1.8.218 to 1.8.220 are `LD.QR`, `ST.QR` and `MV.QR`.
[^trm-ldqr]: Same manual, sections 1.8.218 `LD.QR` (page 301), 1.8.219 `ST.QR` (page 302) and 1.8.220 `MV.QR` (page 303). `LD.QR`'s operation reads `qu = load128(as + imm)`, with no address masking shown, and its syntax line reads `LD.QR qu, as, imm, -128..112`. The `ST.QR` entry's syntax line reads `LD.QR`, not `ST.QR`.
[^trm-regs]: Same manual, Table 1.5-1 "Register List of ESP32-S3 Extended Instruction Set", page 45. Eight 128-bit `QR`, one 4-bit `SAR_BYTE`, one 40-bit `ACCX`, two 160-bit `QACC`, one 128-bit `UA_STATE`.
[^trm-vld128ip]: Same manual, section 1.8.88 `EE.VLD.128.IP`, page 164.
[^trm-vld128xp]: Same manual, section 1.8.89 `EE.VLD.128.XP`, page 165.
[^trm-vst128ip]: Same manual, sections 1.8.192 and 1.8.193 `EE.VST.128.IP` and `.XP`, pages 275 to 276.
[^trm-vld64]: Same manual, sections 1.8.90 to 1.8.93, pages 166 to 169.
[^trm-vst64]: Same manual, sections 1.8.194 to 1.8.197, pages 277 to 280.
[^trm-vldbc8]: Same manual, section 1.8.100 `EE.VLDBC.8`, page 176. It is the only load with no forced bits.
[^trm-vldbc]: Same manual, sections 1.8.94 to 1.8.102, pages 170 to 178. `EE.VLDBC.32.IP` (1.8.98, page 174) carries the range defect: syntax line `-256..252`, description "8-bit sign-extended constant in the instruction code segment left-shifted by 2".
[^trm-vldhbc]: Same manual, section 1.8.103 `EE.VLDHBC.16.INCP`, page 179.
[^trm-usar]: Same manual, sections 1.8.17 and 1.8.18 `EE.LD.128.USAR.IP` and `.XP`, pages 93 to 94.
[^trm-srcq]: Same manual, sections 1.8.49 `EE.SRC.Q` and 1.8.52 `EE.SRC.Q.QUP`, pages 125 and 128.
[^trm-srcqld]: Same manual, sections 1.8.50 and 1.8.51 `EE.SRC.Q.LD.IP` and `.LD.XP`, pages 126 to 127.
[^trm-srcqst]: Same manual, section 1.8.56 `EE.SRCQ.128.ST.INCP`, page 132.
[^trm-ldf128]: Same manual, sections 1.8.25 and 1.8.26 `EE.LDF.128.IP` and `.XP`, pages 101 to 102.
[^trm-ldf64]: Same manual, sections 1.8.27 and 1.8.28 `EE.LDF.64.IP` and `.XP`, pages 103 to 104.
[^trm-stf]: Same manual, sections 1.8.65 to 1.8.68 `EE.STF.*`, pages 141 to 144.
[^trm-ldqa]: Same manual, sections 1.8.29 to 1.8.36 `EE.LDQA.*`, pages 105 to 112, and Table 1.6-2, page 54.
[^trm-qacc]: Same manual, sections 1.8.20 to 1.8.23 and 1.8.60 to 1.8.63, pages 96 to 99 and 136 to 139.
[^trm-ldaccx]: Same manual, section 1.8.19 `EE.LD.ACCX.IP`, page 95.
[^trm-staccx]: Same manual, section 1.8.59 `EE.ST.ACCX.IP`, page 135. The syntax line reads `-512..508`; the description says the 8-bit sign-extended constant is left-shifted by 3, and the operation line writes only `as += imm8`.
[^trm-uastate]: Same manual, sections 1.8.24 and 1.8.64, pages 100 and 140.
[^trm-ldxq]: Same manual, sections 1.8.37 `EE.LDXQ.32` and 1.8.69 `EE.STXQ.32`, pages 113 and 145.
[^trm-vadds-ld-incp]: Same manual, section 1.8.71 `EE.VADDS.S16.LD.INCP`, page 147.
[^trm-vadds-st-incp]: Same manual, section 1.8.72 `EE.VADDS.S16.ST.INCP`, page 148.
[^trm-vmulas-ldip]: Same manual, section 1.8.140 `EE.VMULAS.S16.QACC.LD.IP`, page 216. The immediate is a 6-bit signed field left-shifted by 4.
[^trm-vmulas-ldbc]: Same manual, section 1.8.144 `EE.VMULAS.S16.QACC.LDBC.INCP`, page 220.
[^trm-pipeline]: Same manual, Table 1.7-2 "Extended Instruction Pipeline Stages", section 1.7.1, pages 66 to 74.
[^trm-addrmap]: Same manual, chapter 4 "System and Memory", section 4.3.1 "Address Mapping", page 402.
[^qemu-tie]: Espressif QEMU fork, branch `esp-develop`, commit `febae182e132e4055529be423a818225ebddaa3a`, file `target/xtensa/translate_tie_esp32s3.c`. `translate_vld_128_s3` masks with `0xfffffff0`; `translate_ld_128_usar_s3` additionally sets `SAR_BYTE` from `arg[1].in & 0xf`; `translate_ld_qr_s3` and `translate_st_qr_s3` mask `arg[1].in` with `0xfffffff0` and then add `arg[2].imm`. https://github.com/espressif/qemu
[^espdsp-add]: Espressif esp-dsp, tag v1.8.2, commit `7a0f3edf86dd530f7a7f42a4b54eb04a0cd57384`, file `modules/math/add/fixed/dsps_add_s16_aes3.S`. The vector path is guarded by `movi a15, 0xF` and `bany` on both input pointers. https://github.com/espressif/esp-dsp
[^espdsp-memcpy]: Same repository and commit, file `modules/support/mem/esp32s3/dsps_memcpy_aes3.S`, label `._main_loop_unaligned`.
[^gas]: [measured] GNU assembler (crosstool-NG esp-14.2.0_20241119) 2.43.1, `xtensa-esp32s3-elf-as`, run 2026-09-06. Immediate ranges and granularity were established by assembling candidate values and recording acceptance or the "operand N has invalid value" diagnostic. Confirmed at both ends and one step past each end: `ld.qr` and `st.qr` `-128..112` step 16, `ee.vld.128.ip` `-2048..2032` step 16, `ee.vld.l.64.ip` and `ee.ldf.64.ip` `-1024..1016` step 8, `ee.ldf.128.ip` and `ee.stf.128.ip` `-128..112` step 16, `ee.ldqa.u8.128.ip` and `ee.ld.ua_state.ip` `-2048..2032` step 16, `ee.ld.qacc_h.h.32.ip` `-512..508` step 4, `ee.src.q.ld.ip` `-2048..2032` step 16, `ee.vmulas.s16.qacc.ld.ip` `-512..496` step 16, `ee.st.accx.ip` `-1024..1016` step 8, `ee.vldbc.8.ip` `0..127`, `ee.vldbc.16.ip` `0..254` step 2, `ee.vldbc.32.ip` `-512..508` step 4, and `ee.ldxq.32` selectors `0..3` and `0..7`.
[^idf-memtypes]: Espressif, 2026. *ESP-IDF Programming Guide*, ESP32-S3, "Memory Types". Instruction memory "can only be read or written via 4-byte aligned words"; data memory "can be accessed via individual byte operations". https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/memory-types.html
