---
title: Writing GCC extended inline assembly for the Xtensa LX7
id: 04-toolchain-and-codegen/gcc-extended-inline-asm-on-xtensa
schema_version: 1
doc_type: how-to
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, gcc, inline-asm, constraints, clobbers, codegen]
confidence: high
---

# Writing GCC extended inline assembly for the Xtensa LX7

Every claim below about what the compiler accepts or emits was reproduced with
`xtensa-esp-elf-gcc` 14.2.0 (crosstool-NG `esp-14.2.0_20241119`), the toolchain
ESP-IDF 5.x ships for the ESP32-S3, built `-mlongcalls -O2` on 2026-09-06.[^tc]
Another GCC version can allocate differently.

## The form

The template string, then colon-separated outputs, inputs and clobbers, and
with the `goto` qualifier a fifth field of labels.[^gccasm] Use `__asm__`, not
`asm`, so the code survives `-std=c11`. Operands are numbered across both lists
in order, outputs first from `%0`. Renumbering after an edit is the most common
way to break a working block, so name them, as in
`: [dst] "+r"(dst) : [n] "r"(count)`, then write `%[dst]` and `%[n]`.
Separate instructions inside one string literal with `\n\t`. GCC does not
parse the template, but it counts statements for its inlining size
estimate.[^gccasm]

## The Xtensa constraint letters

The authority is the backend's `constraints.md`, not the manual. The GCC 14.2
manual page for Xtensa lists only `a`, `b`, `A`, `I`, `J`, `K` and `L`, and
omits nine letters the backend defines with public documentation strings: `f`,
`M`, `N`, `O`, `P`, `Y`, `R`, `T` and `U`.[^machconstr] Register
constraints:[^constraints]

| Letter | Meaning | Available when |
|---|---|---|
| `a` | AR registers `a0`-`a15` except `a1` (sp) | always (windowed ABI) |
| `b` | boolean register | Boolean Option configured |
| `f` | float registers `f0`-`f15` | hard-float coprocessor configured |
| `A` | low 32 bits of the MAC16 accumulator | MAC16 Option configured |
| `q` | the stack pointer `a1`, marked internal | windowed ABI |
| `c`, `d`, `B`, `C`, `D`, `W` | internal, for the machine description | option-dependent |

Integer constant constraints, with the instruction each targets:[^constraints]

| Letter | Range or rule | For |
|---|---|---|
| `I` | signed 12-bit | `MOVI` |
| `J` | signed 8-bit | `ADDI` |
| `K` | a signed-comparison branch immediate, or zero | `BEQI`, `BLTI` |
| `L` | an unsigned-comparison branch immediate | `BGEUI`, `BLTUI` |
| `M` | `-32` to `95` | `MOVI.N` |
| `N` | unsigned 8-bit shifted left 8 | `ADDMI` |
| `O` | `-1`, or `1` to `15` | `ADDI.N` |
| `P` | a valid `EXTUI` mask | `EXTUI` |
| `Y` | a constant usable in a relaxed `MOVI` | with `-mauto-litpools` |

Memory constraints: `R` is memory reachable with a 4-bit unsigned register
offset, `T` is memory in a literal pool (`L32R`-addressable), `U` is memory
that is not in a literal pool.[^constraints] The generic `i` takes any
assembler-time constant, and Xtensa immediates print bare, with no `#` or `$`:
`"i"(5)` at `%2` in `srli %0, %1, %2` produced `srli a2, a2, 5`.[^tc]

### `r` versus `a`

`r` maps to `GENERAL_REGS`, which the Xtensa backend defines as `AR_REGS`: all
sixteen address registers including `a1`. `a` maps to `GR_REGS`, the same set
minus `a1`.[^xtensah] So `a` is the honest constraint. In practice they behave
the same, because `a0` and `a1` are both in `FIXED_REGISTERS` and the allocator
never gives them to an asm operand.[^xtensah] Every test here used `r` and got
registers from `a2`-`a15`.[^tc] Prefer `a` in new code; do not rewrite working
`r` blocks. The trap runs the other way: on Xtensa `q` is the stack pointer,
not a PIE vector register, and `"=q"(y)` on an `int` fails with `inconsistent
operand constraints in an 'asm'`.[^tc]

## Output modifiers

`=` is a write-only output. `+` is read and written, and takes one operand slot
rather than two. A kernel with a walking pointer needs `+r`: the block reads
the pointer, advances it, and leaves it somewhere new. `"r"(p)` claims the
value is unchanged afterwards, which is a lie the optimiser acts on. `"+r"(p)`
is true, and it lets GCC drop the pointer arithmetic it would otherwise emit
around the block.

`&` is an early clobber: the operand is written before all inputs are read, so
it must not share a register with an input, and any temporary written before
the last input read needs it. Without it GCC may overlap them. The block
`"movi %0, 0 \n\t add %0, %0, %1 \n\t add %0, %0, %2"`, declared
`: "=r"(o) : "r"(x), "r"(y)`, compiled to
`movi a2, 0; add a2, a2, a2; add a2, a2, a3`. GCC put `o` and `x` both in
`a2`, so `x` was destroyed and the function returned `y` alone. Changing one
character to `"=&r"(o)` moved `o` to `a8` and fixed it.[^tc]

## Clobbers

`"memory"` says the block reads or writes memory that is not an operand. The
manual calls it a read/write memory barrier for the compiler: values are
flushed before the block and reloaded after it.[^gccasm] Without it GCC keeps
its cached view across your stores; see worked example 3. Named register
clobbers list scratch registers the block writes and does not declare as
operands. The nameable registers on this backend are `a0`, `sp`, `a2`-`a15`,
`fp`, `argp`, `b0`, `f0`-`f15` and `acc`, plus the alias `a1`.[^xtensah]
Anything else is rejected: `"q0"` and `"sar"` both fail with `unknown register
name ... in 'asm'`, while `"acc"` is accepted.[^tc]

The danger is on both sides. **Writing a register you did not declare**: GCC
may hold a live value there. In one test GCC placed the output `out` in `a8`,
and the template `"mov %0, %1 \n\t movi a8, 0"` zeroed `a8` after producing the
output, so the caller got 0 instead of the result.[^tc] The compiler cannot
warn, because it does not read the template. **Naming a register that is also
an operand**: force an operand into a register with a local register variable
and clobber the same register, and GCC diagnoses it (`'asm' specifier for
variable 'r' conflicts with 'asm' clobber list`).[^tc] The implicit case is the
one that ships. Each clobbered register is also one the allocator loses.

### What the block owns silently

Some Xtensa state has no register class, therefore no constraint and no clobber
name. A block that touches it owns it, and a comment at the top of the block is
the only way to say so. Both worked examples below do that.

- **PIE `q` registers.** GCC 14's Xtensa register classes are `NO_REGS`,
  `BR_REGS`, `FP_REGS`, `ACC_REG`, `SP_REG`, `ISC_REGS`, `RL_REGS`,
  `GR_REGS`, `AR_REGS`, `ALL_REGS`. There is no vector class, and no `q`
  register appears in `REGISTER_NAMES`.[^xtensah] The assembler knows the
  instructions; the compiler does not know the registers exist. That is
  also why no clobber is needed for them: the allocator has no class that
  contains `q0` to `q7`, so it can never put a C value there. [Coprocessors,
  CPENABLE and lazy context](../00-foundations/coprocessors-cpenable-and-lazy-context.md)
  covers what the block does owe, which is the coprocessor state.
- **`SAR` and `SAR_BYTE`.** Not nameable either.[^tc] The block that sets
  the shift amount and the block that consumes it must be one block.
- **`LBEG`, `LEND`, `LCOUNT`.** A block using `LOOP` owns them, and
  hardware loops do not nest.
- **Special registers generally.** ESP-IDF's compare-and-set writes
  `SCOMPARE1` from inline asm for this reason: it cannot be declared, so
  the write and the `S32C1I` that consumes it live in one block.[^idf]

## `asm volatile`

`volatile` stops GCC discarding a block whose outputs are unused, and stops it
hoisting the block out of a loop whose inputs do not change. Blocks with no
output operands, and all `asm goto` blocks, are implicitly volatile. It is not
a memory barrier and it does not pin the block between neighbouring statements:
the manual's own example shows GCC moving an addition back across a `volatile`
block, with an artificial data dependency as the fix.[^gccasm] The matching
failure mode is a mis-declared output on a non-volatile block:

```c
void fill_bad(uint32_t *dst, uint32_t v){
    uint32_t tmp;
    __asm__("s32i %1, %2, 0" : "=r"(tmp) : "r"(v), "r"(dst));
}
```

compiled to `entry a1, 32; retw.n`. The only declared output is dead, so the
whole block was deleted.[^tc] The store was real, but GCC had no way to know.
`volatile` plus `"memory"` keeps it.

## Labels

Named labels break when GCC duplicates a block, which it may do while
optimising; the manual points at `%=` for this.[^gccasm] `%=` expands to a
number unique to each instance, so `"%=:"` defines a local numeric label and
`"%=f"` branches forward to it. Plain local numeric labels (`1:`, `1f`, `1b`)
work too, but prefer `%=`.

### `LOOP` needs a reachable label, and a long body costs a prologue

`LOOP`, `LOOPNEZ` and `LOOPGTZ` take a count register and a label marking the
end of the body. Both must be inside the same asm block, because GCC is free to
place its own code between blocks. Writing one by hand is also the only way to
get a hardware loop around hand-written code at all: any `__asm__` statement in
a C loop body makes GCC refuse the loop, which
[Zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md) covers.

The body has a size limit: on this assembler 256 bytes assembles as a plain
`loopnez`, and at 258 bytes the assembler relaxes it to

```
beqz       a5, <end>
loopnez    a5, <short end>
rsr.lend   a5
wsr.lbeg   a5
l32r       a5, <literal>
nop
wsr.lend   a5
isync
rsr.lcount a5
addi       a5, a5, 1
```

One instruction becomes ten, plus a literal-pool entry.[^tc] The sequence writes
`LBEG` and `LEND` by hand, so it is still a hardware loop; it is not a
compare-and-branch fallback. Read the last two lines before assuming the worst:
the assembler uses the count register as its own scratch and then puts the count
back, reading `LCOUNT` and adding the one that `LOOPNEZ` took off. Reproduced
inside a real C block with the count declared `"r"(n)` and read again after the
block: GCC allocated `n` to `a8`, the relaxed sequence used `a8` as scratch and
restored it, and the value stored afterwards was the original count.[^tc] So a
plain `"r"` count is safe here, and `"+r"` is not needed for this reason. It is
still needed whenever the body itself advances the count.

Three real costs remain, so keep a hardware-loop body under 256 bytes anyway:
eight extra instructions in the loop prologue, a literal that has to be
reachable, and no relaxation at all if the assembler is not allowed to transform.
`--no-transform` turns the same source into `Error: operand 2 of 'loop' has out
of range value`.[^tc] Separately, the assembler always aligns `LOOP`, by widening
density instructions or inserting `NOP`s, and `-mno-target-align` does not
disable that.[^xtopts] Your block is therefore not exactly the size you wrote,
and a cycle claim about a `LOOP` body needs a measurement.

## `asm goto`

The `goto` form branches to C labels, written `%l[label]`. This compiles on
Xtensa and produced the expected `l32i` and `bne`:[^tc]

```c
__asm__ goto("l32i a8, %0, 0 \n\t bne a8, %1, %l[miss]"
             :: "r"(p), "r"(v) : "a8", "memory" : miss);
```

Three rules from the manual:[^gccasm] `asm goto` is always implicitly volatile;
GCC assumes execution falls through to the next statement, so add
`__builtin_unreachable()` after the block if it cannot; and an output set on
only some paths must use `+`, not `=`. Reference labels by name, never by
number, because numbered references count past every input and output and a `+`
output counts as two.

## Shape: a `noinline` wrapper taking pointers and ints

The robust shape for a kernel is a small `__attribute__((noinline))` function
whose parameters are plain pointers and integers, with the block as its whole
body. Register allocation is then stable, because the block sees its arguments
in the argument registers rather than in whatever the inlining context left
live, so a spill is a property of the block, not of one call site. The block
reads like a leaf function, it compiles out of tree against a portable C twin
with the same signature so bit-exactness is a diff rather than an argument, and
`noinline` stops GCC duplicating it, removing a class of label problems.

## Checking what GCC did

`-S -fverbose-asm` prints the block bracketed by `#APP` and `#NO_APP`, each
operand annotated with the C name it came from:

```
#APP
	srli a2, a2, 5	# o, tmp46,
#NO_APP
```

Read the register numbers, not the names. The file header also records the
options in force, including the core description
(`-mdynconfig=xtensa_esp32s3.so`).[^tc] For what actually shipped, run
`objdump -d`: the manual is explicit that `-mlongcalls` relaxation is
invisible in the compiler's assembly output.[^xtopts] Two failure modes to
look for.

**Spills, or a refusal.** The windowed ABI gives the allocator `a2`-`a15`. A
block with four early-clobbered outputs, seven inputs and
`"a8", "a9", "a10", "a11"` in the clobber list was rejected with `'asm'
operand has impossible constraints or there are not enough registers`;
removing the four named clobbers compiled it, with no `sp` reference inside
the block.[^tc] Below that
threshold you get silent `s32i`/`l32i` pairs around the block instead of an
error. Count live operands, and prefer packing values behind one pointer
operand over adding operands.

**A dead output.** If the disassembly shows `entry` and `retw.n` with nothing
between, the block was deleted; see `fill_bad` above.

## `-mlongcalls` and `call8` inside a block

`-mlongcalls` is implemented in the assembler, not the compiler: it makes the
assembler turn a direct `CALL` into `L32R` plus `CALLX` when the target may be
out of range.[^xtopts] GCC passes `--longcalls` to the assembler when the
option is given, and nothing when it is not.[^tc] So a `call8 foo` inside an
inline asm block is relaxed exactly like a compiler-generated call, provided
the translation unit is built with `-mlongcalls`; if it is not, load the
address yourself and use `callx8` through a register. Either way a call
invalidates the scratch-register picture, because `call8` rotates the register
window, and you must not call from inside a hardware loop.

## Worked example 1: a scalar pack-and-store loop with `LOOP`

Two 16-bit values become one 32-bit store per iteration. The pointers walk, so
they are `+r`; the temporaries are written before the last input read, so they
are `=&r`; the loop label is `%=`. The count is `"r"` only because the body is
short.

```c
__attribute__((noinline))
void pack_pairs(uint32_t *dst, const uint16_t *src, unsigned pairs)
{
    if (!pairs) return;              /* LOOPGTZ needs a positive count */
    uint32_t lo, hi;
    /* Owns the LOOP registers (LBEG/LEND/LCOUNT); they cannot be clobbered
       by name, so nothing may nest inside this block. */
    __asm__ volatile(
        "   loopgtz %[n], %=f            \n\t"
        "   l16ui   %[lo], %[s], 0       \n\t"
        "   l16ui   %[hi], %[s], 2       \n\t"
        "   slli    %[hi], %[hi], 16     \n\t"
        "   or      %[lo], %[lo], %[hi]  \n\t"
        "   s32i    %[lo], %[d], 0       \n\t"
        "   addi    %[s], %[s], 4        \n\t"
        "   addi    %[d], %[d], 4        \n\t"
        "%=:                             \n\t"
        : [d] "+r"(dst), [s] "+r"(src), [lo] "=&r"(lo), [hi] "=&r"(hi)
        : [n] "r"(pairs)
        : "memory");
}
```

GCC allocated `dst`, `src` and `pairs` to `a2`, `a3`, `a4`, the temporaries to
`a8`, `a9`, chose density forms for `s32i.n` and both `addi.n`, and produced an
18-byte body, far under the relaxation threshold.[^tc]

## Worked example 2: an unaligned PIE streaming load

`EE.LD.128.USAR.IP qu, as, imm` forces the low four bits of the address in `as`
to zero, loads the sixteen bytes there into `qu`, saves the four bits it just
cleared into `SAR_BYTE`, and then advances `as` by the sign-extended immediate
shifted left by four.[^trmusar] `EE.SRC.Q.LD.IP qu, as, imm, qs0, qs1` does two
things at once: it shifts the 32-byte concatenation of `qs1` and `qs0` right by
`SAR_BYTE` bytes and writes the result back into `qs0`, which is the unaligned
sixteen bytes, and it loads the next aligned sixteen bytes into `qu` and advances
`as` the same way.[^trmsrcq] So the shifted data comes out of `qs0`, not `qu`,
which is why the store below names the register that was just consumed. Neither
`SAR_BYTE` nor the `q` registers can be declared, so the block owns them. The
rotation over three `q` registers, and moving 48 bytes per iteration, follow the
shape esp-dsp uses in its ESP32-S3 `memcpy`.[^espdsp]

```c
__attribute__((noinline))
void stream_copy_u(uint8_t *dst, const uint8_t *src, unsigned triples)
{
    /* Owns q0-q2, SAR_BYTE and the LOOP registers. None is nameable in a
       clobber list. dst must be 16-byte aligned; src need not be. */
    __asm__ volatile(
        "   ee.ld.128.usar.ip q0, %[s], 16          \n\t"
        "   ee.ld.128.usar.ip q1, %[s], 16          \n\t"
        "   loopnez %[n], %=f                       \n\t"
        "   ee.src.q.ld.ip    q2, %[s], 16, q0, q1  \n\t"
        "   ee.vst.128.ip     q0, %[d], 16          \n\t"
        "   ee.src.q.ld.ip    q0, %[s], 16, q1, q2  \n\t"
        "   ee.vst.128.ip     q1, %[d], 16          \n\t"
        "   ee.src.q.ld.ip    q1, %[s], 16, q2, q0  \n\t"
        "   ee.vst.128.ip     q2, %[d], 16          \n\t"
        "%=:                                        \n\t"
        : [d] "+r"(dst), [s] "+r"(src), [n] "+r"(triples)
        :
        : "memory");
}
```

All three pointers and the count are `+r`: the auto-increment loads and stores
advance the pointers, and `LOOPNEZ` consumes the count. It assembled to exactly
the eight instructions above.[^tc] Two cautions. The destination really must be
16-byte aligned, because every 128-bit access in this instruction set forces the
low four address bits to zero rather than faulting, so a misaligned `dst` writes
to the wrong place in silence.[^trmalign] And the block leaves the PIE
coprocessor state dirty, which interacts with lazy coprocessor context switching;
see [Coprocessors, CPENABLE and lazy
context](../00-foundations/coprocessors-cpenable-and-lazy-context.md).

## Worked example 3: the wrong one

```c
int bad(uint32_t *buf, uint32_t v){
    buf[0] = 1;
    __asm__ volatile("s32i %1, %0, 0" :: "r"(buf), "r"(v));
    return buf[0];
}
```

The block stores `v` through `buf`. GCC knows the pointer, because it is an
input, but not that the pointed-to memory changed: there is no `"memory"`
clobber and `volatile` does not supply one. The whole compiled output:

```
entry  a1, 32
mov.n  a8, a2
movi.n a2, 1
s32i.n a2, a8, 0
s32i.n a3, a8, 0
retw.n
```

Both stores happen, in the right order. The reload does not: `a2` still holds 1
from the C-level store, so the function returns 1 whatever `v` was. Adding
`: "memory"` produced the same stores followed by `l32i.n a2, a2, 0`.[^tc] This
survives review because the assembly is correct and both stores are visible in
the disassembly. It shows up as a stale value read back one statement later,
and it moves when the optimisation level changes.

---

[^tc]: Reproduced with `xtensa-esp-elf-gcc (crosstool-NG esp-14.2.0_20241119) 14.2.0`, target `xtensa-esp32s3-elf`, invoked as `xtensa-esp32s3-elf-gcc -mlongcalls -O2 -c` and inspected with `xtensa-esp32s3-elf-objdump -d`, on 2026-09-06. `[measured]` for the byte thresholds and register allocations quoted; another GCC version or optimisation level may allocate differently.
[^gccasm]: GNU Compiler Collection 14.2.0 manual, section 6.48.2 "Extended Asm - Assembler Instructions with C Expression Operands", subsections "Qualifiers", "Assembler Template", "Clobbers and Scratch Registers", "Volatile" and "GotoLabels". https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Extended-Asm.html
[^machconstr]: GNU Compiler Collection 14.2.0 manual, "Constraints for Particular Machines", Xtensa subsection. https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Machine-Constraints.html
[^constraints]: GCC source, `gcc/config/xtensa/constraints.md`, `releases/gcc-14` branch, retrieved 2026-09-06. https://github.com/gcc-mirror/gcc/blob/releases/gcc-14/gcc/config/xtensa/constraints.md
[^xtensah]: GCC source, `gcc/config/xtensa/xtensa.h`, `releases/gcc-14` branch, retrieved 2026-09-06: `FIXED_REGISTERS`, `REGISTER_NAMES`, `ADDITIONAL_REGISTER_NAMES`, `enum reg_class`, `REG_CLASS_NAMES`, `REG_CLASS_CONTENTS` and `#define GENERAL_REGS AR_REGS`. https://github.com/gcc-mirror/gcc/blob/releases/gcc-14/gcc/config/xtensa/xtensa.h
[^xtopts]: GNU Compiler Collection 14.2.0 manual, "Xtensa Options", entries `-mlongcalls` and `-mtarget-align`. https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Xtensa-Options.html
[^espdsp]: esp-dsp, `modules/support/mem/esp32s3/dsps_memcpy_aes3.S`, repository tree sha `3c8ac0fdfec83740b783e200862c8d0c056de0ad` (master, retrieved 2026-09-06), the `_main_loop_unaligned` block: a `loopnez` moving 48 bytes an iteration through `ee.src.q.ld.ip` and `ee.vst.128.ip` over `q2`, `q3` and `q4`, with the file's own header note that the destination must always be aligned. https://github.com/espressif/esp-dsp
[^trmusar]: Espressif Systems, *ESP32-S3 Technical Reference Manual* v1.8, section 1.8.17 `EE.LD.128.USAR.IP`, page 93. Operation: `qu[127:0] = load128({as[31:4],4{0}})`, `SAR_BYTE = as[3:0]`, `as = as + {20{imm16[7]},imm16[7:0],4{0}}`.
[^trmsrcq]: Same manual, section 1.8.50 `EE.SRC.Q.LD.IP`, page 126. Operation: `qs0[127:0] = {qs1[127:0], qs0[127:0]} >> {SAR_BYTE[3:0] << 3}`, `qu[127:0] = load128({as[31:4],4{0}})`, `as = as + {20{imm16[7]},imm16[7:0],4{0}}`. The `SAR_BYTE` special register is described in section 1.4 under "SAR_BYTE", which points at section 1.5.3 for the alignment rule.
[^trmalign]: Same manual, section 1.5.3 "Data Format and Alignment", page 48: "all access addresses in the extended instruction set are forced to be aligned, i.e., the lowest bits will be replaced by 0", four bits for 128-bit data, three for 64-bit, two for 32-bit, one for 16-bit; "Otherwise, the data read will not be what you expected."
[^idf]: ESP-IDF v5.5.1, `components/xtensa/include/xt_utils.h`, `xt_utils_compare_and_set`: a shipped inline asm block that writes the `SCOMPARE1` special register, and uses the older matching-operand form `"0"(old_value)` where `"+r"` would serve today.
