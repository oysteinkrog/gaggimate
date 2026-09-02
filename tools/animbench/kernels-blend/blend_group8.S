/* blendGroup8General -- Xtensa ESP32-S3 PIE (EE.*) implementation of the
 * "no lane is a==255" branch of the blendRow group-8 dispatch (see
 * blend_model.h/.cpp for the C++ model this must match, and blend_ref.h for
 * the scalar ground truth it must reproduce bit-for-bit).
 *
 * This is a STANDALONE assembler-syntax probe, not the integration form.
 * Its purpose: (1) prove every mnemonic and operand-register combination
 * below assembles cleanly against the REAL device assembler
 * (xtensa-esp32s3-elf-as, crosstool-NG esp-14.2.0, confirmed present at
 * /mnt/c/Users/Oystein/.platformio/packages/toolchain-xtensa-esp-elf/bin/),
 * and (2) give tools/animbench/xtensa_report.py something to count
 * instructions on. It uses "ret.n" rather than a real windowed-ABI
 * entry/retw pair because it is never linked or executed -- the pasteable
 * integration form (blend_pie_kernel.cpp) embeds the identical instruction
 * body as an `asm volatile` block inside a normal C++ function, the same
 * way scale565Oct does in SleepAnimation.cpp (~line 234), and the compiler
 * generates the real ABI prologue/epilogue there. See prove_asm_match.py in
 * this directory: it extracts the mnemonic+register sequence from both
 * this file and blend_pie_kernel.cpp's asm block and diffs them, so the two
 * cannot silently drift out of lockstep.
 *
 * Register/argument convention (matches what a windowed-ABI call8 would
 * pass in a2..a7, six pointer args -- not meant to imply this file is
 * itself entered via call8, only that the register choice previews the
 * real callee's incoming argument registers):
 *   a2 = rd   -- read pointer into dst[] (8 x uint16_t, RGB565), 16B aligned
 *   a3 = wr   -- write pointer into dst[], same address as a2 at entry,
 *                passed as a second copy because EE.VLD.128.IP and
 *                EE.VST.128.IP both auto-increment their own pointer
 *                register and we need dst read before the group's other
 *                pointers are consumed and written back after -- exactly
 *                scale565Oct's rd/wr split, same reason.
 *   a4 = col  -- colStage8, 8 x uint16_t RGB565 gathered from the 3-byte-
 *                stride overlay snapshot, 16B aligned
 *   a5 = av   -- aStage8, 8 x uint16_t, alpha (0..254) widened per lane
 *   a6 = iv   -- invStage8, 8 x uint16_t, (256-alpha) widened per lane
 *   a7 = ct   -- constTable, 6 x (8 x uint16_t) = 96B, 16B aligned, laid
 *                out in exactly the load order below (see blend_pie_kernel.cpp
 *                for the table contents: ones, maskR, constR2048, maskG,
 *                constG32, maskB)
 *
 * Derivation (see blend_model.h's block comment for the full algebra):
 * blend565's per-channel formula, re-derived on each channel's raw 0..31
 * (R,B) / 0..63 (G) magnitude instead of its bit-positioned value, is
 * bit-exact to blend565 for any a in [0,254] (NEVER a==255 -- see the
 * worked counterexample in blend_ref.h). Raw magnitudes keep every
 * intermediate product (Xraw*a <= 7905, sum <= 7936 for R/B, <= 16128 for
 * G) comfortably under the 16-bit lane width EE.VMUL.U16 truncates to and
 * far under the 32767 signed-saturation threshold EE.VADDS.S16 (the only
 * available vector add) would ever clamp at, so saturation never actually
 * triggers here -- it is a non-issue, not a risk this design leans on.
 *
 * EE.VMUL.U16 only ever shifts RIGHT (product >> SAR, low 16 bits kept), so
 * both "extract raw magnitude from a bit-positioned channel" (>>11 for R,
 * >>5 for G, no shift for B) and "reposition raw magnitude back into its
 * bit field" (<<11 for R via *2048, <<5 for G via *32, no-op for B) go
 * through the same "multiply by a constant, shift by SAR" primitive --
 * extraction multiplies by the all-ones vector at SAR=11/5, repositioning
 * multiplies by 2048/32 at SAR=0. There is no left-shift instruction, so
 * repositioning must be a real multiply by a real constant, not a shift.
 *
 * Register budget forces per-channel-serial processing, not the batched
 * (all channels' extractions grouped by SAR value) order the header
 * comment in blend_model.h works out on paper: q0 (fg), q1 (bg), q2 (a),
 * q3 (inv), q7 (ones) are pinned for the whole 8-pixel group (5 registers,
 * needed by every channel), leaving only q4/q5/q6 free -- not enough to
 * also hold two full channels' raw fg+raw bg simultaneously across a
 * SAR-batched schedule. q6 doubles as the running OR-accumulator across
 * the three channels so no channel's finished value needs a spill to
 * memory. This costs more SAR MODE CHANGES (ssai) than the batched-by-SAR
 * order would (9 here vs. 5 there) but stays inside the 8 q-register file
 * with zero spills -- see the report for why this tradeoff was chosen.
 */
.text
.align 4
.global blendGroup8General
blendGroup8General:
    ee.vld.128.ip q7, a7, 16       // q7 = ones (0x0001 x8) -- pinned all function
    ee.vld.128.ip q0, a4, 16       // q0 = fg (colour)      -- pinned all function
    ee.vld.128.ip q1, a2, 16       // q1 = bg (dst read)    -- pinned all function
    ee.vld.128.ip q2, a5, 16       // q2 = a                -- pinned all function
    ee.vld.128.ip q3, a6, 16       // q3 = inv              -- pinned all function

    // ---- R channel (bit position 11, mask 0xF800) ----
    ee.vld.128.ip q4, a7, 16       // q4 = maskR
    ee.andq  q5, q0, q4            // q5 = fgR bitpos
    ee.andq  q4, q1, q4            // q4 = bgR bitpos
    ssai 11
    ee.vmul.u16 q5, q5, q7         // q5 = fgR raw (0..31)
    ee.vmul.u16 q4, q4, q7         // q4 = bgR raw (0..31)
    ssai 0
    ee.vmul.u16 q5, q5, q2         // q5 = fgR_raw * a      (<=7905)
    ee.vmul.u16 q4, q4, q3         // q4 = bgR_raw * inv    (<=7936)
    ee.vadds.s16 q5, q5, q4        // q5 = sum              (<=15841, no saturation)
    ssai 8
    ee.vmul.u16 q5, q5, q7         // q5 = outR raw (0..31) = sum >> 8
    ee.vld.128.ip q4, a7, 16       // q4 = constR2048 (repositioning constant)
    ssai 0
    ee.vmul.u16 q6, q5, q4         // q6 = outR bitpos = outR_raw << 11 (accumulator init)

    // ---- G channel (bit position 5, mask 0x07E0) ----
    ee.vld.128.ip q4, a7, 16       // q4 = maskG
    ee.andq  q5, q0, q4            // q5 = fgG bitpos
    ee.andq  q4, q1, q4            // q4 = bgG bitpos
    ssai 5
    ee.vmul.u16 q5, q5, q7         // q5 = fgG raw (0..63)
    ee.vmul.u16 q4, q4, q7         // q4 = bgG raw (0..63)
    ssai 0
    ee.vmul.u16 q5, q5, q2         // q5 = fgG_raw * a      (<=16002)
    ee.vmul.u16 q4, q4, q3         // q4 = bgG_raw * inv    (<=16128)
    ee.vadds.s16 q5, q5, q4        // q5 = sum              (<=16128, no saturation)
    ssai 8
    ee.vmul.u16 q5, q5, q7         // q5 = outG raw (0..63) = sum >> 8
    ee.vld.128.ip q4, a7, 16       // q4 = constG32 (repositioning constant)
    ssai 0
    ee.vmul.u16 q5, q5, q4         // q5 = outG bitpos = outG_raw << 5
    ee.orq q6, q6, q5              // accumulator |= outG bitpos

    // ---- B channel (bit position 0, mask 0x001F; no shift needed either way) ----
    ee.vld.128.ip q4, a7, 16       // q4 = maskB
    ee.andq  q5, q0, q4            // q5 = fgB raw (== bitpos, P=0)
    ee.andq  q4, q1, q4            // q4 = bgB raw
    // SAR is already 0 here (from the G-channel repositioning multiply
    // above) -- no ssai needed before these two SAR=0 multiplies.
    ee.vmul.u16 q5, q5, q2         // q5 = fgB_raw * a      (<=7905)
    ee.vmul.u16 q4, q4, q3         // q4 = bgB_raw * inv    (<=7936)
    ee.vadds.s16 q5, q5, q4        // q5 = sum              (<=15841, no saturation)
    ssai 8
    ee.vmul.u16 q5, q5, q7         // q5 = outB raw == outB bitpos (P=0, no repositioning)
    ee.orq q6, q6, q5              // accumulator |= outB bitpos

    ee.vst.128.ip q6, a3, 16       // dst[0..7] = accumulator (R|G|B combined)
    ret.n
