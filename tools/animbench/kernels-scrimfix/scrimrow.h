// Declarations for scrimRow_ref (the scalar reference, byte-identical to
// scrimRow in SleepAnimation.cpp) and scrimRow_branchlessWord (this task's
// fix). Definitions in scrimrow.cpp -- kept out of this header, and marked
// noinline there, for the same reason SleepAnimation.cpp itself keeps
// scrimRow as a standalone noinline function: the source's comment on
// blendRow (SleepAnimation.cpp:85-89) explains that inlined into a large
// caller, this loop shape ran out of registers on Xtensa's windowed ABI.
// Compiling each variant as its own noinline top-level function is what
// makes the asm.sh dump (and any instruction-count/loop-recovery
// conclusions drawn from it) faithful to what the firmware actually emits.
#pragma once
#include <cstdint>

namespace scrimfix {

// Byte-identical to scrimRow, SleepAnimation.cpp:402-423. Same parameter
// names as the real function (dst, invRow, runs, nRuns, w) -- the call site
// (SleepAnimation.cpp:3024) passes the halo run list under these names.
void scrimRow_ref(uint16_t *__restrict dst, const uint8_t *__restrict invRow, const uint32_t *__restrict runs,
                  int nRuns, int w);

// Intermediate step, kept as its own variant so the "the `continue` alone
// is the blocker" hypothesis can be tested and falsified independently
// rather than asserted: scrimRow_ref with ONLY the `if (inv ==
// SCRIM_INV_NONE) continue;` removed (replaced by an unconditional
// scrimCell_ref call). Still iterates one CELL per trip. See scrimrow.cpp.
void scrimRow_branchlessCell(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                             const uint32_t *__restrict runs, int nRuns, int w);

// scrimRow_ref restructured to iterate one 32-bit WORD (two pixels) per
// loop trip instead of one CELL (two words / four pixels, scrimCell_ref's
// unit), with the same branchless SCRIM_INV_NONE substitution as
// scrimRow_branchlessCell. See scrimrow.cpp for the full derivation and the
// asm evidence this task's own asm.sh produces.
void scrimRow_branchlessWord(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                             const uint32_t *__restrict runs, int nRuns, int w);

} // namespace scrimfix
