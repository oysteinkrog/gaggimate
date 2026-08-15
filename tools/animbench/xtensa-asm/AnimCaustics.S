	.file	"AnimCaustics.cpp"
# GNU C++17 (crosstool-NG esp-2021r2-patch5) version 8.4.0 (xtensa-esp32s3-elf)
#	compiled by GNU C version 6.3.0 20170516, GMP version 6.2.1, MPFR version 4.1.0, MPC version 1.2.1, isl version isl-0.19-GMP

# GGC heuristics: --param ggc-min-expand=100 --param ggc-min-heapsize=131072
# options passed:  -I shim -imultilib no-rtti
# -iprefix c:\users\oystein\.platformio\packages\toolchain-xtensa-esp32s3\bin\../lib/gcc/xtensa-esp32s3-elf/8.4.0/
# ../../src/display/ui/default/bganim/AnimCaustics.cpp -mlongcalls
# -auxbase-strip xtensa-asm/AnimCaustics.S -O2 -std=gnu++17 -fno-exceptions
# -fno-rtti -ffunction-sections -fverbose-asm
# options enabled:  -faggressive-loop-optimizations -falign-functions
# -falign-jumps -falign-labels -falign-loops -fauto-inc-dec
# -fbranch-count-reg -fcaller-saves -fchkp-check-incomplete-type
# -fchkp-check-read -fchkp-check-write -fchkp-instrument-calls
# -fchkp-narrow-bounds -fchkp-optimize -fchkp-store-bounds
# -fchkp-use-static-bounds -fchkp-use-static-const-bounds
# -fchkp-use-wrappers -fcode-hoisting -fcombine-stack-adjustments -fcommon
# -fcompare-elim -fcprop-registers -fcrossjumping -fcse-follow-jumps
# -fdefer-pop -fdelete-null-pointer-checks -fdevirtualize
# -fdevirtualize-speculatively -fearly-inlining
# -feliminate-unused-debug-types -fexpensive-optimizations
# -fforward-propagate -ffp-int-builtin-inexact -ffunction-cse
# -ffunction-sections -fgcse -fgcse-lm -fgnu-runtime -fgnu-unique
# -fguess-branch-probability -fhoist-adjacent-loads -fident -fif-conversion
# -fif-conversion2 -findirect-inlining -finline -finline-atomics
# -finline-functions-called-once -finline-small-functions -fipa-bit-cp
# -fipa-cp -fipa-icf -fipa-icf-functions -fipa-icf-variables -fipa-profile
# -fipa-pure-const -fipa-ra -fipa-reference -fipa-sra -fipa-vrp
# -fira-hoist-pressure -fira-share-save-slots -fira-share-spill-slots
# -fisolate-erroneous-paths-dereference -fivopts -fkeep-static-consts
# -fleading-underscore -flifetime-dse -flra-remat -flto-odr-type-merging
# -fmath-errno -fmerge-constants -fmerge-debug-strings
# -fmove-loop-invariants -fomit-frame-pointer -foptimize-sibling-calls
# -foptimize-strlen -fpartial-inlining -fpeephole -fpeephole2 -fplt
# -fprefetch-loop-arrays -freg-struct-return -freorder-functions
# -frerun-cse-after-loop -fsched-critical-path-heuristic
# -fsched-dep-count-heuristic -fsched-group-heuristic -fsched-interblock
# -fsched-last-insn-heuristic -fsched-rank-heuristic -fsched-spec
# -fsched-spec-insn-heuristic -fsched-stalled-insns-dep -fschedule-fusion
# -fschedule-insns -fschedule-insns2 -fsemantic-interposition -fshow-column
# -fshrink-wrap -fshrink-wrap-separate -fsigned-zeros
# -fsplit-ivs-in-unroller -fsplit-wide-types -fssa-backprop -fssa-phiopt
# -fstdarg-opt -fstore-merging -fstrict-aliasing
# -fstrict-volatile-bitfields -fsync-libcalls -fthread-jumps
# -ftoplevel-reorder -ftrapping-math -ftree-bit-ccp -ftree-builtin-call-dce
# -ftree-ccp -ftree-ch -ftree-coalesce-vars -ftree-copy-prop -ftree-cselim
# -ftree-dce -ftree-dominator-opts -ftree-dse -ftree-forwprop -ftree-fre
# -ftree-loop-if-convert -ftree-loop-im -ftree-loop-ivcanon
# -ftree-loop-optimize -ftree-parallelize-loops= -ftree-phiprop -ftree-pre
# -ftree-pta -ftree-reassoc -ftree-scev-cprop -ftree-sink -ftree-slsr
# -ftree-sra -ftree-switch-conversion -ftree-tail-merge -ftree-ter
# -ftree-vrp -funit-at-a-time -fverbose-asm -fzero-initialized-in-bss
# -mserialize-volatile

	.text
	.section	.text._ZN12_GLOBAL__N_14bandEPtiiijPKh,"ax",@progbits
	.literal_position
	.literal .LC0, _ZN12_GLOBAL__N_110g_rowFreqQE
	.literal .LC1, _ZN12_GLOBAL__N_18g_phaseQE
	.literal .LC2, _ZN12_GLOBAL__N_17ditherIE
	.literal .LC3, _ZN12_GLOBAL__N_17g_stepQE
	.literal .LC4, _ZN12_GLOBAL__N_14palRE
	.literal .LC5, _ZN12_GLOBAL__N_14palGE
	.literal .LC6, _ZN12_GLOBAL__N_14palBE
	.literal .LC7, _ZN12_GLOBAL__N_18shapeLUTE
	.literal .LC8, _ZN12_GLOBAL__N_110g_rowFreqQE+12
	.align	4
	.type	_ZN12_GLOBAL__N_14bandEPtiiijPKh, @function
_ZN12_GLOBAL__N_14bandEPtiiijPKh:
	entry	sp, 160	#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:163: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
	s32i	a5, sp, 88	# %sfp, w
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:170:     const int16_t *lut = sinLut();
	call8	_ZN6bganim6sinLutEv		#
	s32i.n	a10, sp, 32	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:163: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:172:     for (int yy = 0; yy < rows; yy++) {
	blti	a4, 1, .L1	# rows,,
	l32i	a5, sp, 88	# %sfp, w
	s32i	a3, sp, 72	# %sfp, y0
	addi	a5, a5, -4	# tmp326, w,
	srli	a5, a5, 2	#, tmp326,
	l32i	a3, sp, 88	# %sfp,
	l32i	a6, sp, 72	# %sfp,
	s32i	a5, sp, 92	# %sfp,
	addi.n	a5, a5, 1	# tmp564,,
	slli	a8, a5, 2	#, tmp564,
	slli	a3, a3, 1	#,,
	add.n	a4, a4, a6	#, rows,
	slli	a5, a5, 3	#, tmp564,
	s32i	a3, sp, 104	# %sfp,
	s32i	a2, sp, 76	# %sfp, dst
	s32i	a4, sp, 100	# %sfp,
	s32i	a8, sp, 108	# %sfp,
	s32i	a5, sp, 112	# %sfp,
.L7:
	l32r	a3, .LC0	#, ivtmp$89
	l32r	a5, .LC1	#, ivtmp$90
	l32i	a7, sp, 72	# %sfp, ivtmp$97
	mov.n	a4, sp	# ivtmp$91,
.L3:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:176:             phaseQ[k] = static_cast<uint32_t>(y) * g_rowFreqQ[k] + g_phaseQ[k];
	l32i.n	a2, a3, 0	# MEM[base: _538, offset: 0B], MEM[base: _538, offset: 0B]
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:176:             phaseQ[k] = static_cast<uint32_t>(y) * g_rowFreqQ[k] + g_phaseQ[k];
	l32i.n	a6, a5, 0	# MEM[base: _537, offset: 0B], MEM[base: _537, offset: 0B]
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:176:             phaseQ[k] = static_cast<uint32_t>(y) * g_rowFreqQ[k] + g_phaseQ[k];
	mull	a2, a7, a2	# tmp328, ivtmp$97, MEM[base: _538, offset: 0B]
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:175:         for (int k = 0; k < K; k++) {
	l32r	a9, .LC8	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:176:             phaseQ[k] = static_cast<uint32_t>(y) * g_rowFreqQ[k] + g_phaseQ[k];
	add.n	a2, a2, a6	# tmp330, tmp328, MEM[base: _537, offset: 0B]
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:176:             phaseQ[k] = static_cast<uint32_t>(y) * g_rowFreqQ[k] + g_phaseQ[k];
	s32i.n	a2, a4, 0	# MEM[base: _536, offset: 0B], tmp330
	addi.n	a3, a3, 4	# ivtmp$89, ivtmp$89,
	addi.n	a5, a5, 4	# ivtmp$90, ivtmp$90,
	addi.n	a4, a4, 4	# ivtmp$91, ivtmp$91,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:175:         for (int k = 0; k < K; k++) {
	bne	a9, a3, .L3	#, ivtmp$89,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:181:         const int dyBase = (y & 3) << 2;
	l32i	a12, sp, 72	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	l32i.n	a10, sp, 0	# phaseQ,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	l32i.n	a7, sp, 4	# phaseQ, _20
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:186:                           lut[(phaseQ[2] >> PHASE_SHIFT) & (SIN_N - 1)];
	l32i.n	a11, sp, 8	# phaseQ,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:181:         const int dyBase = (y & 3) << 2;
	slli	a4, a12, 2	# tmp332,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:181:         const int dyBase = (y & 3) << 2;
	movi.n	a14, 0xc	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	l32i.n	a13, sp, 32	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:181:         const int dyBase = (y & 3) << 2;
	and	a4, a4, a14	#, tmp332,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	extui	a3, a10, 22, 10	# tmp348,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	extui	a2, a7, 22, 10	# tmp353, _20,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:186:                           lut[(phaseQ[2] >> PHASE_SHIFT) & (SIN_N - 1)];
	extui	a6, a11, 22, 10	# tmp359,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:181:         const int dyBase = (y & 3) << 2;
	s32i	a4, sp, 68	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	slli	a3, a3, 1	# tmp349, tmp348,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	slli	a2, a2, 1	# tmp354, tmp353,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	add.n	a3, a13, a3	# tmp350,, tmp349
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	add.n	a2, a13, a2	# tmp355,, tmp354
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	s32i	a10, sp, 80	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:186:                           lut[(phaseQ[2] >> PHASE_SHIFT) & (SIN_N - 1)];
	slli	a6, a6, 1	# tmp360, tmp359,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	l32i	a10, sp, 68	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	l32r	a12, .LC2	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:186:                           lut[(phaseQ[2] >> PHASE_SHIFT) & (SIN_N - 1)];
	add.n	a6, a13, a6	# tmp361,, tmp360
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	l16si	a9, a3, 0	# *_17, *_17
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	movi.n	a8, 2	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	movi.n	a3, 1	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:186:                           lut[(phaseQ[2] >> PHASE_SHIFT) & (SIN_N - 1)];
	s32i	a11, sp, 84	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	l16si	a2, a2, 0	# *_23, *_23
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	movi.n	a11, 3	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	or	a5, a4, a3	# tmp338,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:192:         while (x + GRID <= w) {
	l32i	a13, sp, 88	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	or	a4, a4, a8	# tmp342,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	or	a3, a10, a11	# tmp346,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:186:                           lut[(phaseQ[2] >> PHASE_SHIFT) & (SIN_N - 1)];
	l16si	a8, a6, 0	# *_30, *_30
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	add.n	a5, a12, a5	# tmp339,, tmp338
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	add.n	a6, a12, a10	# tmp335,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	add.n	a4, a12, a4	# tmp343,, tmp342
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	add.n	a3, a12, a3	# tmp347,, tmp346
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	add.n	a2, a9, a2	# tmp358, *_17, *_23
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	l8ui	a6, a6, 0	# ditherI, d0
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	l8ui	a5, a5, 0	# ditherI, d1
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	l8ui	a4, a4, 0	# ditherI, d2
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:182:         const int8_t d0 = ditherI[dyBase], d1 = ditherI[dyBase | 1], d2 = ditherI[dyBase | 2], d3 = ditherI[dyBase | 3];
	l8ui	a3, a3, 0	# ditherI, d3
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:185:         int32_t sumCur = lut[(phaseQ[0] >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(phaseQ[1] >> PHASE_SHIFT) & (SIN_N - 1)] +
	add.n	a2, a2, a8	# sumCur, tmp358, *_30
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:192:         while (x + GRID <= w) {
	bgei	a13, 4, .L4	#,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:184:         int x = 0;
	movi.n	a5, 0	# x,
.L9:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:227:         for (int i = 0; x < w; i++, x++) {
	l32i	a14, sp, 88	# %sfp,
	blt	a5, a14, .L5	# x,,
.L10:
	l32i	a2, sp, 72	# %sfp,
	l32i	a3, sp, 76	# %sfp,
	l32i	a4, sp, 104	# %sfp,
	addi.n	a2, a2, 1	#,,
	add.n	a3, a3, a4	#,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:172:     for (int yy = 0; yy < rows; yy++) {
	l32i	a5, sp, 100	# %sfp,
	s32i	a2, sp, 72	# %sfp,
	s32i	a3, sp, 76	# %sfp,
	bne	a5, a2, .L7	#,,
	j	.L1		#
.L4:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:194:             const uint32_t np1 = phaseQ[1] + (g_stepQ[1] << 2);
	l32r	a9, .LC3	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	l32r	a13, .LC4	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:194:             const uint32_t np1 = phaseQ[1] + (g_stepQ[1] << 2);
	l32i.n	a8, a9, 4	# g_stepQ, g_stepQ
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:193:             const uint32_t np0 = phaseQ[0] + (g_stepQ[0] << 2);
	mov.n	a10, a9	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:194:             const uint32_t np1 = phaseQ[1] + (g_stepQ[1] << 2);
	slli	a8, a8, 2	#, g_stepQ,
	s32i.n	a8, sp, 40	# %sfp,
	l32i.n	a11, sp, 40	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:195:             const uint32_t np2 = phaseQ[2] + (g_stepQ[2] << 2);
	l32i.n	a8, a10, 8	# g_stepQ, g_stepQ
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:193:             const uint32_t np0 = phaseQ[0] + (g_stepQ[0] << 2);
	l32i.n	a9, a9, 0	# g_stepQ, g_stepQ
	add.n	a11, a7, a11	#, _20,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	l32r	a14, .LC5	#,
	sext	a4, a4, 7	#, d2
	sext	a3, a3, 7	#, d3
	s32i	a11, sp, 96	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:193:             const uint32_t np0 = phaseQ[0] + (g_stepQ[0] << 2);
	slli	a9, a9, 2	#, g_stepQ,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:195:             const uint32_t np2 = phaseQ[2] + (g_stepQ[2] << 2);
	slli	a8, a8, 2	#, g_stepQ,
	sext	a6, a6, 7	#, d0
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	l32i.n	a12, a13, 0	# palR, palR$3_62
	s32i.n	a4, sp, 60	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	l32r	a13, .LC6	#,
	l32i	a4, sp, 76	# %sfp,
	s32i	a3, sp, 64	# %sfp,
	l32i	a3, sp, 112	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	l32i.n	a11, a14, 0	# palG, palG$4_70
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:193:             const uint32_t np0 = phaseQ[0] + (g_stepQ[0] << 2);
	s32i.n	a9, sp, 36	# %sfp,
	l32r	a14, .LC7	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	l32i	a9, sp, 80	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:195:             const uint32_t np2 = phaseQ[2] + (g_stepQ[2] << 2);
	s32i.n	a8, sp, 44	# %sfp,
	s32i.n	a6, sp, 52	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	l32i	a8, sp, 84	# %sfp,
	l32i	a6, sp, 96	# %sfp,
	sext	a5, a5, 7	#, d1
	add.n	a3, a3, a4	#,,
	l32i.n	a10, a13, 0	# palB, palB$5_74
	s32i.n	a5, sp, 56	# %sfp,
	s32i.n	a8, sp, 24	# %sfp,
	s32i.n	a9, sp, 20	# %sfp,
	s32i.n	a14, sp, 16	# %sfp,
	s32i.n	a3, sp, 48	# %sfp,
	mov.n	a5, a4	# ivtmp$82,
	s32i.n	a6, sp, 28	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a9, -0x800	# tmp400,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a8, 0x7e0	# tmp406,
	mov.n	a7, a14	# tmp562,
.L8:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:193:             const uint32_t np0 = phaseQ[0] + (g_stepQ[0] << 2);
	l32i.n	a13, sp, 20	# %sfp,
	l32i.n	a14, sp, 36	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:195:             const uint32_t np2 = phaseQ[2] + (g_stepQ[2] << 2);
	l32i.n	a6, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:193:             const uint32_t np0 = phaseQ[0] + (g_stepQ[0] << 2);
	add.n	a13, a13, a14	#,,
	s32i.n	a13, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:195:             const uint32_t np2 = phaseQ[2] + (g_stepQ[2] << 2);
	l32i.n	a13, sp, 44	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	l32i.n	a3, sp, 28	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:195:             const uint32_t np2 = phaseQ[2] + (g_stepQ[2] << 2);
	add.n	a6, a6, a13	#,,
	s32i.n	a6, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	l32i.n	a14, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:197:                                      lut[(np2 >> PHASE_SHIFT) & (SIN_N - 1)];
	l32i.n	a13, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	extui	a4, a3, 22, 10	# tmp380,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	extui	a6, a14, 22, 10	# tmp375,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	l32i.n	a14, sp, 32	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:197:                                      lut[(np2 >> PHASE_SHIFT) & (SIN_N - 1)];
	extui	a3, a13, 22, 10	# tmp386,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	slli	a6, a6, 1	# tmp376, tmp375,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:197:                                      lut[(np2 >> PHASE_SHIFT) & (SIN_N - 1)];
	l32i.n	a13, sp, 32	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	slli	a4, a4, 1	# tmp381, tmp380,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	add.n	a6, a14, a6	# tmp377,, tmp376
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	add.n	a4, a14, a4	# tmp382,, tmp381
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:197:                                      lut[(np2 >> PHASE_SHIFT) & (SIN_N - 1)];
	slli	a3, a3, 1	# tmp387, tmp386,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	l16si	a14, a6, 0	# *_45, *_45
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	l16si	a4, a4, 0	# *_50, *_50
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:201:             int32_t m = val >> 31;
	srai	a6, a2, 31	# m, sumCur,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:197:                                      lut[(np2 >> PHASE_SHIFT) & (SIN_N - 1)];
	add.n	a3, a13, a3	# tmp388,, tmp387
	l16si	a13, a3, 0	# *_56, *_56
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:202:             uint8_t li = shapeLUT[(val ^ m) - m];
	xor	a3, a6, a2	# tmp393, m, sumCur
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	add.n	a4, a14, a4	# tmp385, *_45, *_50
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:202:             uint8_t li = shapeLUT[(val ^ m) - m];
	sub	a3, a3, a6	# tmp394, tmp393, m
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	add.n	a13, a4, a13	#, tmp385, *_56
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:202:             uint8_t li = shapeLUT[(val ^ m) - m];
	add.n	a3, a7, a3	# tmp395, tmp562, tmp394
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:198:             const int32_t stepInterp = (sumNext - sumCur) >> 2; // GRID==4
	sub	a4, a13, a2	# tmp391,, sumCur
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	l32i.n	a14, sp, 52	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:196:             const int32_t sumNext = lut[(np0 >> PHASE_SHIFT) & (SIN_N - 1)] + lut[(np1 >> PHASE_SHIFT) & (SIN_N - 1)] +
	s32i.n	a13, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	l8ui	a13, a3, 0	# shapeLUT, tmp396
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:198:             const int32_t stepInterp = (sumNext - sumCur) >> 2; // GRID==4
	srai	a6, a4, 2	# stepInterp, tmp391,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	add.n	a13, a13, a14	# _66, tmp396,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:204:             val += stepInterp;
	add.n	a2, a6, a2	# val, stepInterp, sumCur
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	add.n	a3, a11, a13	# tmp403, palG$4_70, _66
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	add.n	a4, a12, a13	# tmp397, palR$3_62, _66
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	l8ui	a14, a3, 0	# *_71, *_71
	l8ui	a4, a4, 0	# *_67, *_67
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:205:             m = val >> 31;
	srai	a15, a2, 31	# m, val,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:206:             li = shapeLUT[(val ^ m) - m];
	xor	a3, a2, a15	# tmp419, val, m
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:206:             li = shapeLUT[(val ^ m) - m];
	sub	a15, a3, a15	# tmp420, tmp419, m
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a4, a4, 8	# tmp399, *_67,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	add.n	a13, a10, a13	# tmp412, palB$5_74, _66
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a14, a14, 3	# tmp405, *_71,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	l8ui	a13, a13, 0	# *_75, *_75
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:206:             li = shapeLUT[(val ^ m) - m];
	add.n	a15, a7, a15	# tmp421, tmp562, tmp420
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a3, a4, a9	# tmp402, tmp399, tmp400
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a14, a14, a8	# tmp408, tmp405, tmp406
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:207:             row[x + 1] = rgb565(palR[li + d1], palG[li + d1], palB[li + d1]);
	l8ui	a4, a15, 0	# shapeLUT, tmp422
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a3, a3, a14	# tmp411, tmp402, tmp408
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:207:             row[x + 1] = rgb565(palR[li + d1], palG[li + d1], palB[li + d1]);
	l32i.n	a14, sp, 56	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	srai	a13, a13, 3	# tmp414, *_75,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:207:             row[x + 1] = rgb565(palR[li + d1], palG[li + d1], palB[li + d1]);
	add.n	a4, a4, a14	# _86, tmp422,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a3, a3, a13	# tmp417, tmp411, tmp414
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:203:             row[x + 0] = rgb565(palR[li + d0], palG[li + d0], palB[li + d0]);
	s16i	a3, a5, 0	# MEM[base: _558, offset: 0B], tmp417
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:208:             val += stepInterp;
	add.n	a2, a6, a2	# val, stepInterp, val
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:207:             row[x + 1] = rgb565(palR[li + d1], palG[li + d1], palB[li + d1]);
	add.n	a13, a12, a4	# tmp423, palR$3_62, _86
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:207:             row[x + 1] = rgb565(palR[li + d1], palG[li + d1], palB[li + d1]);
	add.n	a3, a11, a4	# tmp429, palG$4_70, _86
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:209:             m = val >> 31;
	srai	a15, a2, 31	# m, val,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:207:             row[x + 1] = rgb565(palR[li + d1], palG[li + d1], palB[li + d1]);
	l8ui	a13, a13, 0	# *_87, *_87
	l8ui	a3, a3, 0	# *_90, *_90
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:210:             li = shapeLUT[(val ^ m) - m];
	xor	a14, a2, a15	# tmp445, val, m
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:207:             row[x + 1] = rgb565(palR[li + d1], palG[li + d1], palB[li + d1]);
	add.n	a4, a10, a4	# tmp438, palB$5_74, _86
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:210:             li = shapeLUT[(val ^ m) - m];
	sub	a14, a14, a15	# tmp446, tmp445, m
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a13, a13, 8	# tmp425, *_87,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a3, a3, 3	# tmp431, *_90,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:207:             row[x + 1] = rgb565(palR[li + d1], palG[li + d1], palB[li + d1]);
	l8ui	a4, a4, 0	# *_93, *_93
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a3, a3, a8	# tmp434, tmp431, tmp406
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:210:             li = shapeLUT[(val ^ m) - m];
	add.n	a14, a7, a14	# tmp447, tmp562, tmp446
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a13, a13, a9	# tmp428, tmp425, tmp400
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:211:             row[x + 2] = rgb565(palR[li + d2], palG[li + d2], palB[li + d2]);
	l8ui	a14, a14, 0	# shapeLUT, tmp448
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a13, a13, a3	# tmp437, tmp428, tmp434
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:211:             row[x + 2] = rgb565(palR[li + d2], palG[li + d2], palB[li + d2]);
	l32i.n	a3, sp, 60	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	srai	a4, a4, 3	# tmp440, *_93,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:211:             row[x + 2] = rgb565(palR[li + d2], palG[li + d2], palB[li + d2]);
	add.n	a14, a14, a3	# _104, tmp448,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:212:             val += stepInterp;
	add.n	a2, a6, a2	# val, stepInterp, val
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a13, a13, a4	# tmp443, tmp437, tmp440
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:207:             row[x + 1] = rgb565(palR[li + d1], palG[li + d1], palB[li + d1]);
	s16i	a13, a5, 2	# MEM[base: _558, offset: 2B], tmp443
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:211:             row[x + 2] = rgb565(palR[li + d2], palG[li + d2], palB[li + d2]);
	add.n	a4, a12, a14	# tmp449, palR$3_62, _104
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:213:             m = val >> 31;
	srai	a13, a2, 31	# m, val,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:211:             row[x + 2] = rgb565(palR[li + d2], palG[li + d2], palB[li + d2]);
	add.n	a3, a11, a14	# tmp455, palG$4_70, _104
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:211:             row[x + 2] = rgb565(palR[li + d2], palG[li + d2], palB[li + d2]);
	l8ui	a4, a4, 0	# *_105, *_105
	l8ui	a3, a3, 0	# *_108, *_108
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:214:             li = shapeLUT[(val ^ m) - m];
	xor	a2, a2, a13	# tmp471, val, m
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:214:             li = shapeLUT[(val ^ m) - m];
	sub	a2, a2, a13	# tmp472, tmp471, m
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:211:             row[x + 2] = rgb565(palR[li + d2], palG[li + d2], palB[li + d2]);
	add.n	a14, a10, a14	# tmp464, palB$5_74, _104
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:211:             row[x + 2] = rgb565(palR[li + d2], palG[li + d2], palB[li + d2]);
	l8ui	a14, a14, 0	# *_111, *_111
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a4, a4, 8	# tmp451, *_105,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a3, a3, 3	# tmp457, *_108,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:214:             li = shapeLUT[(val ^ m) - m];
	add.n	a2, a7, a2	# tmp473, tmp562, tmp472
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:215:             row[x + 3] = rgb565(palR[li + d3], palG[li + d3], palB[li + d3]);
	l8ui	a2, a2, 0	# shapeLUT, tmp474
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a3, a3, a8	# tmp460, tmp457, tmp406
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a4, a4, a9	# tmp454, tmp451, tmp400
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:215:             row[x + 3] = rgb565(palR[li + d3], palG[li + d3], palB[li + d3]);
	l32i	a6, sp, 64	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a4, a4, a3	# tmp463, tmp454, tmp460
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	srai	a14, a14, 3	# tmp466, *_111,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:215:             row[x + 3] = rgb565(palR[li + d3], palG[li + d3], palB[li + d3]);
	add.n	a2, a2, a6	# _122, tmp474,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a4, a4, a14	# tmp469, tmp463, tmp466
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:211:             row[x + 2] = rgb565(palR[li + d2], palG[li + d2], palB[li + d2]);
	s16i	a4, a5, 4	# MEM[base: _558, offset: 4B], tmp469
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:215:             row[x + 3] = rgb565(palR[li + d3], palG[li + d3], palB[li + d3]);
	add.n	a3, a11, a2	# tmp481, palG$4_70, _122
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:215:             row[x + 3] = rgb565(palR[li + d3], palG[li + d3], palB[li + d3]);
	add.n	a4, a12, a2	# tmp475, palR$3_62, _122
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:215:             row[x + 3] = rgb565(palR[li + d3], palG[li + d3], palB[li + d3]);
	l8ui	a4, a4, 0	# *_123, *_123
	l8ui	a3, a3, 0	# *_126, *_126
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:215:             row[x + 3] = rgb565(palR[li + d3], palG[li + d3], palB[li + d3]);
	add.n	a2, a10, a2	# tmp490, palB$5_74, _122
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:215:             row[x + 3] = rgb565(palR[li + d3], palG[li + d3], palB[li + d3]);
	l8ui	a2, a2, 0	# *_129, *_129
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a4, a4, 8	# tmp477, *_123,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a3, a3, 3	# tmp483, *_126,
	and	a3, a3, a8	# tmp486, tmp483, tmp406
	l32i.n	a13, sp, 28	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a4, a4, a9	# tmp480, tmp477, tmp400
	l32i.n	a14, sp, 40	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a4, a4, a3	# tmp489, tmp480, tmp486
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	srai	a2, a2, 3	# tmp492, *_129,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a4, a4, a2	# tmp495, tmp489, tmp492
	add.n	a13, a13, a14	#,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:192:         while (x + GRID <= w) {
	l32i.n	a3, sp, 48	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:215:             row[x + 3] = rgb565(palR[li + d3], palG[li + d3], palB[li + d3]);
	s16i	a4, a5, 6	# MEM[base: _558, offset: 6B], tmp495
	s32i.n	a13, sp, 28	# %sfp,
	addi.n	a5, a5, 8	# ivtmp$82, ivtmp$82,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:220:             sumCur = sumNext;
	l32i.n	a2, sp, 16	# %sfp, sumCur
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:192:         while (x + GRID <= w) {
	bne	a3, a5, .L8	#, ivtmp$82,
	l32i	a5, sp, 92	# %sfp,
	l32i.n	a4, sp, 36	# %sfp,
	l32i.n	a6, sp, 44	# %sfp,
	l32i	a8, sp, 80	# %sfp,
	l32i.n	a9, sp, 36	# %sfp,
	l32i	a10, sp, 84	# %sfp,
	mull	a3, a4, a5	# tmp496,,
	mull	a2, a14, a5	# tmp499,,
	mull	a4, a6, a5	# tmp501,,
	l32i	a11, sp, 96	# %sfp,
	add.n	a5, a8, a9	# tmp497,,
	add.n	a6, a10, a6	# tmp502,,
	add.n	a3, a3, a5	# np0, tmp496, tmp497
	add.n	a2, a2, a11	# np1, tmp499,
	add.n	a4, a4, a6	# np2, tmp501, tmp502
	s32i.n	a3, sp, 0	# phaseQ, np0
	s32i.n	a2, sp, 4	# phaseQ, np1
	s32i.n	a4, sp, 8	# phaseQ, np2
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:221:             x += GRID;
	l32i	a5, sp, 108	# %sfp, x
	j	.L9		#
.L5:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:228:             const int32_t sum = lut[((phaseQ[0] + g_stepQ[0] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	l32r	a2, .LC3	#, tmp504
	l32i	a3, sp, 76	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:229:                                  lut[((phaseQ[1] + g_stepQ[1] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	l32i.n	a13, a2, 4	# g_stepQ,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:230:                                  lut[((phaseQ[2] + g_stepQ[2] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)];
	l32i.n	a15, a2, 8	# g_stepQ, _156
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:228:             const int32_t sum = lut[((phaseQ[0] + g_stepQ[0] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	l32i.n	a12, a2, 0	# g_stepQ,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	l32r	a2, .LC4	#, tmp507
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:229:                                  lut[((phaseQ[1] + g_stepQ[1] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	s32i.n	a13, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	l32i.n	a2, a2, 0	# palR,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	l32i	a6, sp, 88	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	s32i.n	a2, sp, 28	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	l32r	a2, .LC5	#, tmp508
	slli	a4, a5, 1	# tmp510, x,
	l32i.n	a13, a2, 0	# palG, palG$23_176
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	l32r	a2, .LC6	#, tmp509
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	s32i.n	a15, sp, 36	# %sfp, _156
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	l32i.n	a14, a2, 0	# palB, palB$24_180
	l32r	a2, .LC7	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:228:             const int32_t sum = lut[((phaseQ[0] + g_stepQ[0] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	l32i.n	a10, sp, 0	# phaseQ, ivtmp$65
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:229:                                  lut[((phaseQ[1] + g_stepQ[1] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	l32i.n	a9, sp, 4	# phaseQ, ivtmp$66
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:230:                                  lut[((phaseQ[2] + g_stepQ[2] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)];
	l32i.n	a8, sp, 8	# phaseQ, ivtmp$67
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	l32i.n	a15, sp, 32	# %sfp, lut
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:228:             const int32_t sum = lut[((phaseQ[0] + g_stepQ[0] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	s32i.n	a12, sp, 20	# %sfp,
	s32i.n	a2, sp, 16	# %sfp,
	add.n	a4, a3, a4	# ivtmp$68,, tmp510
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a11, 0x7e0	# tmp547,
	sub	a7, a6, a5	# tmp570,, x
.L11:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:228:             const int32_t sum = lut[((phaseQ[0] + g_stepQ[0] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	extui	a3, a10, 22, 10	# tmp511, ivtmp$65,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:229:                                  lut[((phaseQ[1] + g_stepQ[1] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	extui	a2, a9, 22, 10	# tmp516, ivtmp$66,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:230:                                  lut[((phaseQ[2] + g_stepQ[2] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)];
	extui	a6, a8, 22, 10	# tmp522, ivtmp$67,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:228:             const int32_t sum = lut[((phaseQ[0] + g_stepQ[0] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	slli	a3, a3, 1	# tmp512, tmp511,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:229:                                  lut[((phaseQ[1] + g_stepQ[1] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	slli	a2, a2, 1	# tmp517, tmp516,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:228:             const int32_t sum = lut[((phaseQ[0] + g_stepQ[0] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	add.n	a3, a15, a3	# tmp513, lut, tmp512
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:229:                                  lut[((phaseQ[1] + g_stepQ[1] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	add.n	a2, a15, a2	# tmp518, lut, tmp517
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:230:                                  lut[((phaseQ[2] + g_stepQ[2] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)];
	slli	a6, a6, 1	# tmp523, tmp522,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:228:             const int32_t sum = lut[((phaseQ[0] + g_stepQ[0] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	l16si	a3, a3, 0	# *_142, *_142
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:229:                                  lut[((phaseQ[1] + g_stepQ[1] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	l16si	a2, a2, 0	# *_151, *_151
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:230:                                  lut[((phaseQ[2] + g_stepQ[2] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)];
	add.n	a6, a15, a6	# tmp524, lut, tmp523
	l16si	a6, a6, 0	# *_161, *_161
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:228:             const int32_t sum = lut[((phaseQ[0] + g_stepQ[0] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	add.n	a2, a3, a2	# tmp521, *_142, *_151
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:228:             const int32_t sum = lut[((phaseQ[0] + g_stepQ[0] * static_cast<uint32_t>(i)) >> PHASE_SHIFT) & (SIN_N - 1)] +
	add.n	a2, a2, a6	# sum, tmp521, *_161
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:231:             const int32_t m = sum >> 31;
	srai	a6, a2, 31	# m, sum,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:232:             const uint8_t li = shapeLUT[(sum ^ m) - m];
	xor	a2, a2, a6	# tmp528, sum, m
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:233:             const int8_t d = ditherI[dyBase | (x & 3)];
	l32i	a12, sp, 68	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:233:             const int8_t d = ditherI[dyBase | (x & 3)];
	extui	a3, a5, 0, 2	# tmp533, x,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:232:             const uint8_t li = shapeLUT[(sum ^ m) - m];
	sub	a2, a2, a6	# tmp529, tmp528, m
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:233:             const int8_t d = ditherI[dyBase | (x & 3)];
	l32r	a6, .LC2	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:233:             const int8_t d = ditherI[dyBase | (x & 3)];
	or	a3, a3, a12	# tmp534, tmp533,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:232:             const uint8_t li = shapeLUT[(sum ^ m) - m];
	l32i.n	a12, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:233:             const int8_t d = ditherI[dyBase | (x & 3)];
	add.n	a3, a6, a3	# tmp535,, tmp534
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:232:             const uint8_t li = shapeLUT[(sum ^ m) - m];
	add.n	a2, a12, a2	# tmp530,, tmp529
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	l8ui	a3, a3, 0	# ditherI,
	l8ui	a6, a2, 0	# shapeLUT, tmp531
	sext	a3, a3, 7	# tmp536, tmp537
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	l32i.n	a2, sp, 28	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	add.n	a6, a6, a3	# _172, tmp531, tmp536
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	add.n	a3, a2, a6	# tmp538,, _172
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	add.n	a2, a13, a6	# tmp544, palG$23_176, _172
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	l8ui	a3, a3, 0	# *_173, *_173
	l8ui	a2, a2, 0	# *_177, *_177
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	add.n	a6, a14, a6	# tmp553, palB$24_180, _172
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	l8ui	a6, a6, 0	# *_181, *_181
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a3, a3, 8	# tmp540, *_173,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a2, a2, 3	# tmp546, *_177,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a12, -0x800	#,
	and	a3, a3, a12	# tmp543, tmp540,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a2, a2, a11	# tmp549, tmp546, tmp547
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a2, a3, a2	# tmp552, tmp543, tmp549
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	srai	a6, a6, 3	# tmp555, *_181,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a2, a2, a6	# tmp558, tmp552, tmp555
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:234:             row[x] = rgb565(palR[li + d], palG[li + d], palB[li + d]);
	s16i	a2, a4, 0	# MEM[base: _565, offset: 0B], tmp558
	l32i.n	a3, sp, 24	# %sfp,
	l32i.n	a2, sp, 20	# %sfp,
	l32i.n	a6, sp, 36	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:227:         for (int i = 0; x < w; i++, x++) {
	addi.n	a5, a5, 1	# x, x,
	add.n	a10, a10, a2	# ivtmp$65, ivtmp$65,
	add.n	a9, a9, a3	# ivtmp$66, ivtmp$66,
	add.n	a8, a8, a6	# ivtmp$67, ivtmp$67,
	addi.n	a4, a4, 2	# ivtmp$68, ivtmp$68,
	addi.n	a7, a7, -1	# tmp570, tmp570,
	bnez.n	a7, .L11	# tmp570,
	j	.L10		#
.L1:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:237: }
	retw.n
	.size	_ZN12_GLOBAL__N_14bandEPtiiijPKh, .-_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.section	.text._ZN12_GLOBAL__N_117buildThemePaletteEv,"ax",@progbits
	.literal_position
	.literal .LC9, _ZN12_GLOBAL__N_14palRE
	.literal .LC10, _ZN12_GLOBAL__N_14palGE
	.literal .LC11, _ZN12_GLOBAL__N_14palBE
	.literal .LC12, _ZN12_GLOBAL__N_17palRExtE
	.literal .LC13, _ZN12_GLOBAL__N_17palGExtE
	.literal .LC14, _ZN12_GLOBAL__N_17palBExtE
	.align	4
	.type	_ZN12_GLOBAL__N_117buildThemePaletteEv, @function
_ZN12_GLOBAL__N_117buildThemePaletteEv:
	entry	sp, 48	#
	l32r	a5, .LC9	#, tmp87
	l32r	a4, .LC10	#, tmp85
	l32r	a3, .LC11	#, tmp86
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:89:     for (int i = 0; i < 256; i++) {
	movi.n	a2, 0	# i,
.L17:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:91:         themeRGB(i, c);
	mov.n	a10, a2	#, i
	mov.n	a11, sp	#,
	call8	_ZN6bganim8themeRGBEiPh		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:92:         palR[i] = c[0];
	l32i.n	a8, a5, 0	# palR, palR$30_1
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:92:         palR[i] = c[0];
	l8ui	a10, sp, 0	# c, _4
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:92:         palR[i] = c[0];
	add.n	a9, a8, a2	# tmp71, palR$30_1, i
	s8i	a10, a9, 0	# *_3, _4
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:93:         palG[i] = c[1];
	l32i.n	a9, a4, 0	# palG, palG$32_5
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:93:         palG[i] = c[1];
	l8ui	a11, sp, 1	# c, _7
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:93:         palG[i] = c[1];
	add.n	a10, a9, a2	# tmp73, palG$32_5, i
	s8i	a11, a10, 0	# *_6, _7
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:94:         palB[i] = c[2];
	l32i.n	a10, a3, 0	# palB, palB$34_8
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:94:         palB[i] = c[2];
	l8ui	a12, sp, 2	# c, _10
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:94:         palB[i] = c[2];
	add.n	a11, a10, a2	# tmp75, palB$34_8, i
	s8i	a12, a11, 0	# *_9, _10
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:89:     for (int i = 0; i < 256; i++) {
	addi.n	a2, a2, 1	# i, i,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:89:     for (int i = 0; i < 256; i++) {
	bnei	a2, 256, .L17	# i,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:97:         palRExt[i] = palR[0];
	l32r	a2, .LC12	#, tmp76
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:100:         palRExt[PAL_PAD + 256 + i] = palR[255];
	movi	a12, 0x108	# tmp79,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:97:         palRExt[i] = palR[0];
	l32i.n	a15, a2, 0	# palRExt, palRExt$37_12
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:98:         palGExt[i] = palG[0];
	l32r	a2, .LC13	#, tmp77
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:100:         palRExt[PAL_PAD + 256 + i] = palR[255];
	movi.n	a11, 8	# tmp88,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:98:         palGExt[i] = palG[0];
	l32i.n	a14, a2, 0	# palGExt, palGExt$40_17
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:99:         palBExt[i] = palB[0];
	l32r	a2, .LC14	#, tmp78
	l32i.n	a13, a2, 0	# palBExt, palBExt$43_21
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:96:     for (int i = 0; i < PAL_PAD; i++) {
	movi.n	a2, 0	# i,
	loop	a11, .L18_LEND	# tmp88,
.L18:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:97:         palRExt[i] = palR[0];
	l8ui	a3, a8, 0	# *palR$30_1, _15
	add.n	a5, a15, a2	# _34, palRExt$37_12, i
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:97:         palRExt[i] = palR[0];
	s8i	a3, a5, 0	# MEM[base: _34, offset: 0B], _15
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:98:         palGExt[i] = palG[0];
	l8ui	a3, a9, 0	# *palG$32_5, _19
	add.n	a4, a14, a2	# _33, palGExt$40_17, i
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:98:         palGExt[i] = palG[0];
	s8i	a3, a4, 0	# MEM[base: _33, offset: 0B], _19
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:99:         palBExt[i] = palB[0];
	l8ui	a6, a10, 0	# *palB$34_8, _23
	add.n	a3, a13, a2	# _51, palBExt$43_21, i
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:99:         palBExt[i] = palB[0];
	s8i	a6, a3, 0	# MEM[base: _51, offset: 0B], _23
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:100:         palRExt[PAL_PAD + 256 + i] = palR[255];
	l8ui	a6, a8, 255	# MEM[(uint8_t *)palR$30_1 + 255B], _27
	add.n	a5, a5, a12	# tmp80, _34, tmp79
	s8i	a6, a5, 0	# MEM[base: _63, offset: 0B], _27
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:101:         palGExt[PAL_PAD + 256 + i] = palG[255];
	l8ui	a5, a9, 255	# MEM[(uint8_t *)palG$32_5 + 255B], _29
	add.n	a4, a4, a12	# tmp82, _33, tmp79
	s8i	a5, a4, 0	# MEM[base: _67, offset: 0B], _29
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:102:         palBExt[PAL_PAD + 256 + i] = palB[255];
	l8ui	a4, a10, 255	# MEM[(uint8_t *)palB$34_8 + 255B], _31
	add.n	a3, a3, a12	# tmp84, _51, tmp79
	s8i	a4, a3, 0	# MEM[base: _71, offset: 0B], _31
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:96:     for (int i = 0; i < PAL_PAD; i++) {
	addi.n	a2, a2, 1	# i, i,
	.L18_LEND:		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:104: }
	retw.n
	.size	_ZN12_GLOBAL__N_117buildThemePaletteEv, .-_ZN12_GLOBAL__N_117buildThemePaletteEv
	.global	__divsf3
	.global	__extendsfdf2
	.global	__muldf3
	.global	__fixunsdfsi
	.section	.text._ZN12_GLOBAL__N_15frameEjiiPKh,"ax",@progbits
	.literal_position
	.literal .LC15, 0x3c656042
	.literal .LC16, 0x3eb33333
	.literal .LC17, _ZN12_GLOBAL__N_112lastThemeGenE
	.literal .LC18, 0x3a83126f
	.literal .LC19, 0x42c80000
	.literal .LC20, 0x3faccccc
	.literal .LC21, 0x3f0ccccd
	.literal .LC22, 0x3d610e10
	.literal .LC23, 0x3f4ccccd
	.literal .LC24, 0x3e0f5c29
	.literal .LC25, 0x3f800000
	.literal .LC26, _ZN12_GLOBAL__N_1L9FREQ_BASEE
	.literal .LC27, 0x4222f983
	.literal .LC28, _ZN12_GLOBAL__N_1L9SPEED_MULE
	.literal .LC29, _ZN12_GLOBAL__N_1L6PHASE0E
	.literal .LC30, 0x40000000
	.literal .LC31, _ZN12_GLOBAL__N_110g_rowFreqQE
	.literal .LC32, 0x3fc90fda
	.literal .LC33, 0x6dc9c883, 0x41c45f30
	.literal .LC34, _ZN12_GLOBAL__N_18g_phaseQE
	.literal .LC35, _ZN12_GLOBAL__N_17g_stepQE
	.literal .LC36, _ZN12_GLOBAL__N_1L10BASE_ANGLEE
	.literal .LC37, _ZN12_GLOBAL__N_1L9ANG_DRIFTE
	.literal .LC38, _ZN12_GLOBAL__N_18shapeLUTE
	.literal .LC39, 0x3a2aaaab
	.literal .LC40, 0x00000000
	.literal .LC41, 0x437f0000
	.literal .LC42, 0x3f000000
	.align	4
	.type	_ZN12_GLOBAL__N_15frameEjiiPKh, @function
_ZN12_GLOBAL__N_15frameEjiiPKh:
	entry	sp, 80	#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:127:     if (themeGen() != lastThemeGen) {
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:127:     if (themeGen() != lastThemeGen) {
	l32r	a3, .LC17	#, tmp129
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:127:     if (themeGen() != lastThemeGen) {
	l32i.n	a4, a3, 0	# lastThemeGen, lastThemeGen
	beq	a10, a4, .L22	#, lastThemeGen,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:128:         buildThemePalette();
	call8	_ZN12_GLOBAL__N_117buildThemePaletteEv		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:129:         lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:129:         lastThemeGen = themeGen();
	s32i.n	a10, a3, 0	# lastThemeGen,
.L22:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:131:     const float t = tMs * 0.001f;
	ufloat.s	f0, a2, 0	# tmp132, tMs
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:131:     const float t = tMs * 0.001f;
	l32r	a2, .LC18	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:132:     const float freqScale = lerpf(0.55f, 1.9f, p[1] / 100.0f);
	l8ui	a10, a5, 1	# MEM[(const uint8_t *)p_57(D) + 1B], MEM[(const uint8_t *)p_57(D) + 1B]
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:131:     const float t = tMs * 0.001f;
	wfr	f2, a2	#,
	mul.s	f0, f0, f2	#, tmp132,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:132:     const float freqScale = lerpf(0.55f, 1.9f, p[1] / 100.0f);
	float.s	f1, a10, 0	#, MEM[(const uint8_t *)p_57(D) + 1B]
	l32r	a11, .LC19	#,
	rfr	a10, f1	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:131:     const float t = tMs * 0.001f;
	ssi	f0, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:132:     const float freqScale = lerpf(0.55f, 1.9f, p[1] / 100.0f);
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:133:     const float speedScale = 0.8f * speedMul(p[0]);
	l8ui	a2, a5, 0	# *p_57(D), *p_57(D)
# ../../src/display/ui/default/bganim/BgAnimCommon.h:50: inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
	wfr	f2, a10	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	addi	a2, a2, -50	# tmp140, *p_57(D),
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	float.s	f0, a2, 0	# tmp141, tmp140
# ../../src/display/ui/default/bganim/BgAnimCommon.h:50: inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
	l32r	a2, .LC21	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:135:     const float invSpan = 1.0f / fmaxf(1e-3f, 1.0f - thresh);
	movi.n	a6, 0	# ivtmp$152,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:50: inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
	wfr	f1, a2	#,
	l32r	a2, .LC20	#,
	wfr	f3, a2	#,
	madd.s	f1, f2, f3	#,,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	l32r	a2, .LC22	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:50: inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
	ssi	f1, sp, 8	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	wfr	f1, a2	#,
	mul.s	f0, f0, f1	#, tmp141,
	rfr	a10, f0	#,
	call8	exp2f		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:133:     const float speedScale = 0.8f * speedMul(p[0]);
	l32r	a3, .LC23	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:134:     const float thresh = 0.14f + 0.55f * (p[2] / 100.0f);
	l8ui	a2, a5, 2	# MEM[(const uint8_t *)p_57(D) + 2B], MEM[(const uint8_t *)p_57(D) + 2B]
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:133:     const float speedScale = 0.8f * speedMul(p[0]);
	wfr	f2, a3	#,
	wfr	f1, a10	#,
	mul.s	f0, f1, f2	# speedScale,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:134:     const float thresh = 0.14f + 0.55f * (p[2] / 100.0f);
	float.s	f1, a2, 0	#, MEM[(const uint8_t *)p_57(D) + 2B]
	l32r	a11, .LC19	#,
	rfr	a10, f1	#,
	ssi	f0, sp, 24	#,
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:135:     const float invSpan = 1.0f / fmaxf(1e-3f, 1.0f - thresh);
	l32r	a2, .LC25	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:134:     const float thresh = 0.14f + 0.55f * (p[2] / 100.0f);
	wfr	f2, a10	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:135:     const float invSpan = 1.0f / fmaxf(1e-3f, 1.0f - thresh);
	wfr	f5, a2	# tmp215,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:134:     const float thresh = 0.14f + 0.55f * (p[2] / 100.0f);
	l32r	a2, .LC24	#,
	lsi	f0, sp, 24	#,
	wfr	f1, a2	#,
	l32r	a2, .LC21	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:135:     const float invSpan = 1.0f / fmaxf(1e-3f, 1.0f - thresh);
	l32r	a10, .LC18	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:134:     const float thresh = 0.14f + 0.55f * (p[2] / 100.0f);
	wfr	f3, a2	#,
	madd.s	f1, f2, f3	#,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:135:     const float invSpan = 1.0f / fmaxf(1e-3f, 1.0f - thresh);
	ssi	f5, sp, 36	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:134:     const float thresh = 0.14f + 0.55f * (p[2] / 100.0f);
	ssi	f1, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:135:     const float invSpan = 1.0f / fmaxf(1e-3f, 1.0f - thresh);
	lsi	f2, sp, 16	# %sfp,
	lsi	f1, sp, 4	# %sfp,
	sub.s	f2, f5, f2	#, tmp215,
	mul.s	f0, f1, f0	#,, speedScale
	rfr	a11, f2	#,
	ssi	f0, sp, 12	# %sfp,
	call8	fmaxf		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:135:     const float invSpan = 1.0f / fmaxf(1e-3f, 1.0f - thresh);
	lsi	f5, sp, 36	#,
	mov.n	a11, a10	#,
	rfr	a10, f5	#, tmp215
	call8	__divsf3		#
	l32r	a3, .LC15	#,
	l32r	a4, .LC16	#,
	l32r	a2, .LC26	#, ivtmp$132
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:145:         g_stepQ[k] = static_cast<uint32_t>(static_cast<double>(cosA * freq) * PHASE_SCALE);
	lsi	f5, sp, 36	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:135:     const float invSpan = 1.0f / fmaxf(1e-3f, 1.0f - thresh);
	wfr	f1, a3	# pretmp_129,
	wfr	f0, a4	# pretmp_127,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:145:         g_stepQ[k] = static_cast<uint32_t>(static_cast<double>(cosA * freq) * PHASE_SCALE);
	s32i.n	a10, sp, 20	# %sfp, invSpan
.L24:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:138:         const float ang = BASE_ANGLE[k] + ANG_DRIFT[k] * t;
	lsi	f2, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	ssi	f5, sp, 36	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:138:         const float ang = BASE_ANGLE[k] + ANG_DRIFT[k] * t;
	madd.s	f0, f2, f1	# pretmp_127,, pretmp_129
	ssi	f0, sp, 0	# %sfp, pretmp_127
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	call8	_ZN6bganim9cosTableFEv		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:143:         g_rowFreqQ[k] = static_cast<uint32_t>(static_cast<double>(sinA * freq) * PHASE_SCALE);
	l32r	a3, .LC31	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	lsi	f1, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:143:         g_rowFreqQ[k] = static_cast<uint32_t>(static_cast<double>(sinA * freq) * PHASE_SCALE);
	add.n	a7, a3, a6	# tmp170,, ivtmp$152
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	l32r	a3, .LC27	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:144:         g_phaseQ[k] = static_cast<uint32_t>(static_cast<double>(phaseRad) * PHASE_SCALE);
	l32r	a4, .LC34	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	wfr	f2, a3	#,
	mul.s	f0, f1, f2	# tmp154,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:145:         g_stepQ[k] = static_cast<uint32_t>(static_cast<double>(cosA * freq) * PHASE_SCALE);
	l32r	a8, .LC35	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:144:         g_phaseQ[k] = static_cast<uint32_t>(static_cast<double>(phaseRad) * PHASE_SCALE);
	add.n	a5, a4, a6	# tmp185,, ivtmp$152
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:145:         g_stepQ[k] = static_cast<uint32_t>(static_cast<double>(cosA * freq) * PHASE_SCALE);
	add.n	a4, a8, a6	# tmp190,, ivtmp$152
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	trunc.s	a3, f0, 0	# tmp156, tmp154
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	extui	a3, a3, 0, 8	# tmp157, tmp156,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	slli	a3, a3, 2	# tmp158, tmp157,
	add.n	a10, a10, a3	# tmp159,, tmp158
	lsi	f3, a10, 0	# *_93, _94
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	ssi	f3, sp, 32	#,
	call8	_ZN6bganim9cosTableFEv		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	l32r	a3, .LC32	#,
	lsi	f1, sp, 0	# %sfp,
	wfr	f2, a3	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:142:         const float phaseRad = PHASE0[k] + t * speedScale * SPEED_MUL[k] * 2.0f;
	l32r	a8, .LC28	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	sub.s	f0, f1, f2	# tmp172,,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:142:         const float phaseRad = PHASE0[k] + t * speedScale * SPEED_MUL[k] * 2.0f;
	add.n	a3, a8, a6	# tmp161,, ivtmp$152
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	l32r	a8, .LC27	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:141:         const float freq = FREQ_BASE[k] * freqScale; // rad/pixel
	lsi	f1, a2, 0	# MEM[base: _114, offset: 0B], MEM[base: _114, offset: 0B]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	wfr	f4, a8	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:141:         const float freq = FREQ_BASE[k] * freqScale; // rad/pixel
	lsi	f2, sp, 8	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	mul.s	f0, f0, f4	# tmp174, tmp172,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:141:         const float freq = FREQ_BASE[k] * freqScale; // rad/pixel
	mul.s	f1, f2, f1	# freq,, MEM[base: _114, offset: 0B]
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:142:         const float phaseRad = PHASE0[k] + t * speedScale * SPEED_MUL[k] * 2.0f;
	lsi	f2, a3, 0	# MEM[base: _112, offset: 0B], MEM[base: _112, offset: 0B]
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:142:         const float phaseRad = PHASE0[k] + t * speedScale * SPEED_MUL[k] * 2.0f;
	l32r	a3, .LC29	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:142:         const float phaseRad = PHASE0[k] + t * speedScale * SPEED_MUL[k] * 2.0f;
	lsi	f4, sp, 12	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:142:         const float phaseRad = PHASE0[k] + t * speedScale * SPEED_MUL[k] * 2.0f;
	add.n	a8, a3, a6	# tmp166,, ivtmp$152
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	trunc.s	a3, f0, 0	# tmp176, tmp174
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:142:         const float phaseRad = PHASE0[k] + t * speedScale * SPEED_MUL[k] * 2.0f;
	mul.s	f2, f4, f2	# tmp164,, MEM[base: _112, offset: 0B]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	extui	a3, a3, 0, 8	# tmp177, tmp176,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	slli	a3, a3, 2	# tmp178, tmp177,
	add.n	a10, a10, a3	# tmp179,, tmp178
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:143:         g_rowFreqQ[k] = static_cast<uint32_t>(static_cast<double>(sinA * freq) * PHASE_SCALE);
	lsi	f6, a10, 0	# *_85, *_85
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:142:         const float phaseRad = PHASE0[k] + t * speedScale * SPEED_MUL[k] * 2.0f;
	l32r	a3, .LC30	#,
	lsi	f0, a8, 0	# MEM[base: _113, offset: 0B], MEM[base: _113, offset: 0B]
	wfr	f8, a3	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:143:         g_rowFreqQ[k] = static_cast<uint32_t>(static_cast<double>(sinA * freq) * PHASE_SCALE);
	mul.s	f6, f1, f6	#, freq, *_85
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:142:         const float phaseRad = PHASE0[k] + t * speedScale * SPEED_MUL[k] * 2.0f;
	madd.s	f0, f2, f8	# phaseRad, tmp164,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:143:         g_rowFreqQ[k] = static_cast<uint32_t>(static_cast<double>(sinA * freq) * PHASE_SCALE);
	ssi	f1, sp, 28	#,
	addi.n	a6, a6, 4	# ivtmp$152, ivtmp$152,
	rfr	a10, f6	#,
	ssi	f0, sp, 24	#,
	call8	__extendsfdf2		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:143:         g_rowFreqQ[k] = static_cast<uint32_t>(static_cast<double>(sinA * freq) * PHASE_SCALE);
	l32r	a12, .LC33	#,
	l32r	a13, .LC33+4	#,
	addi.n	a2, a2, 4	# ivtmp$132, ivtmp$132,
	call8	__muldf3		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:143:         g_rowFreqQ[k] = static_cast<uint32_t>(static_cast<double>(sinA * freq) * PHASE_SCALE);
	call8	__fixunsdfsi		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:144:         g_phaseQ[k] = static_cast<uint32_t>(static_cast<double>(phaseRad) * PHASE_SCALE);
	lsi	f0, sp, 24	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:143:         g_rowFreqQ[k] = static_cast<uint32_t>(static_cast<double>(sinA * freq) * PHASE_SCALE);
	s32i.n	a10, a7, 0	# MEM[base: _111, offset: 0B],
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:144:         g_phaseQ[k] = static_cast<uint32_t>(static_cast<double>(phaseRad) * PHASE_SCALE);
	rfr	a10, f0	#, phaseRad
	call8	__extendsfdf2		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:144:         g_phaseQ[k] = static_cast<uint32_t>(static_cast<double>(phaseRad) * PHASE_SCALE);
	l32r	a12, .LC33	#,
	l32r	a13, .LC33+4	#,
	call8	__muldf3		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:144:         g_phaseQ[k] = static_cast<uint32_t>(static_cast<double>(phaseRad) * PHASE_SCALE);
	call8	__fixunsdfsi		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:145:         g_stepQ[k] = static_cast<uint32_t>(static_cast<double>(cosA * freq) * PHASE_SCALE);
	lsi	f1, sp, 28	#,
	lsi	f3, sp, 32	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:144:         g_phaseQ[k] = static_cast<uint32_t>(static_cast<double>(phaseRad) * PHASE_SCALE);
	s32i.n	a10, a5, 0	# MEM[base: _110, offset: 0B],
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:145:         g_stepQ[k] = static_cast<uint32_t>(static_cast<double>(cosA * freq) * PHASE_SCALE);
	mul.s	f3, f1, f3	#, freq, _94
	rfr	a10, f3	#,
	call8	__extendsfdf2		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:145:         g_stepQ[k] = static_cast<uint32_t>(static_cast<double>(cosA * freq) * PHASE_SCALE);
	l32r	a12, .LC33	#,
	l32r	a13, .LC33+4	#,
	call8	__muldf3		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:145:         g_stepQ[k] = static_cast<uint32_t>(static_cast<double>(cosA * freq) * PHASE_SCALE);
	call8	__fixunsdfsi		#
	s32i.n	a10, a4, 0	# MEM[base: _109, offset: 0B],
	l32r	a4, .LC36	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:137:     for (int k = 0; k < K; k++) {
	lsi	f5, sp, 36	#,
	add.n	a3, a6, a4	# tmp196, ivtmp$152,
	beqi	a6, 12, .L23	# ivtmp$152,,
	lsi	f0, a3, 0	# MEM[base: _104, offset: 0B], pretmp_127
	l32r	a3, .LC37	#, tmp199
	add.n	a3, a6, a3	# tmp198, ivtmp$152, tmp199
	lsi	f1, a3, 0	# MEM[base: _103, offset: 0B], pretmp_129
	j	.L24		#
.L23:
	l32r	a8, .LC39	#,
	l32r	a4, .LC41	#,
	wfr	f3, a8	# tmp223,
	l32r	a2, .LC40	#,
	l32r	a8, .LC42	#,
	lsi	f0, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:155:     for (int i = 0; i < SHAPE_N; i++) {
	movi.n	a3, 0	# i,
	lsi	f4, sp, 20	# %sfp, invSpan
	l32r	a5, .LC38	#, ivtmp$121
	wfr	f7, a4	# tmp228,
	wfr	f2, a2	# tmp214,
	wfr	f6, a8	# tmp229,
	neg.s	f1, f0	# tmp218,
	mov.n	a6, a3	# _140, i
	movi	a7, 0xff	# _140,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:155:     for (int i = 0; i < SHAPE_N; i++) {
	movi	a4, 0x610	# tmp234,
.L26:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:156:         float bright = (i * NORM - thresh) * invSpan;
	float.s	f0, a3, 0	# tmp200, i
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:156:         float bright = (i * NORM - thresh) * invSpan;
	mov.s	f8, f1	#, tmp218
	madd.s	f8, f0, f3	#, tmp200, tmp223
	mov.n	a2, a6	# _140, _140
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:155:     for (int i = 0; i < SHAPE_N; i++) {
	addi.n	a3, a3, 1	# i, i,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:156:         float bright = (i * NORM - thresh) * invSpan;
	mul.s	f0, f8, f4	# bright, tmp202, invSpan
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:157:         bright = bright < 0 ? 0 : (bright > 1 ? 1 : bright);
	olt.s	b0, f0, f2	#, bright, tmp214
	bt	b0, .L25	#,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:157:         bright = bright < 0 ? 0 : (bright > 1 ? 1 : bright);
	olt.s	b0, f5, f0	#, tmp215, bright
	mov.n	a2, a7	# _140, _140
	bt	b0, .L25	#,
	mul.s	f0, f0, f0	# tmp206, bright, bright
	mov.s	f8, f6	#, tmp229
	madd.s	f8, f0, f7	#, tmp206, tmp228
	utrunc.s	a2, f8, 0	# tmp211, tmp207
	extui	a2, a2, 0, 8	# _140, tmp211
.L25:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:159:         shapeLUT[i] = static_cast<uint8_t>(bright * 255.0f + 0.5f);
	s8i	a2, a5, 0	# MEM[base: _120, offset: 0B], _140
	addi.n	a5, a5, 1	# ivtmp$121, ivtmp$121,
	addi.n	a4, a4, -1	# tmp234, tmp234,
	bnez.n	a4, .L26	# tmp234,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:161: }
	retw.n
	.size	_ZN12_GLOBAL__N_15frameEjiiPKh, .-_ZN12_GLOBAL__N_15frameEjiiPKh
	.section	.text._ZN12_GLOBAL__N_14initEii,"ax",@progbits
	.literal_position
	.literal .LC43, _ZN12_GLOBAL__N_17palRExtE
	.literal .LC44, _ZN12_GLOBAL__N_17palGExtE
	.literal .LC45, _ZN12_GLOBAL__N_17palBExtE
	.literal .LC46, _ZN12_GLOBAL__N_14palRE
	.literal .LC47, _ZN12_GLOBAL__N_14palGE
	.literal .LC48, _ZN12_GLOBAL__N_14palBE
	.literal .LC49, _ZN12_GLOBAL__N_112lastThemeGenE
	.literal .LC50, _ZN6bganim6BAYER4E
	.literal .LC51, _ZN12_GLOBAL__N_17ditherIE
	.literal .LC52, 0x40f00000
	.literal .LC53, 0x3f000000
	.align	4
	.type	_ZN12_GLOBAL__N_14initEii, @function
_ZN12_GLOBAL__N_14initEii:
	entry	sp, 32	#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:107:     if (palRExt == nullptr) {
	l32r	a3, .LC43	#, tmp67
	l32r	a4, .LC44	#, tmp89
	l32i.n	a2, a3, 0	# palRExt, palRExt$51_1
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:107:     if (palRExt == nullptr) {
	bnez.n	a2, .L31	# palRExt$51_1,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:108:         palRExt = static_cast<uint8_t *>(alloc(PAL_EXT_N));
	movi	a10, 0x110	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:108:         palRExt = static_cast<uint8_t *>(alloc(PAL_EXT_N));
	s32i.n	a10, a3, 0	# palRExt,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:109:         palGExt = static_cast<uint8_t *>(alloc(PAL_EXT_N));
	movi	a10, 0x110	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:109:         palGExt = static_cast<uint8_t *>(alloc(PAL_EXT_N));
	s32i.n	a10, a4, 0	# palGExt,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:110:         palBExt = static_cast<uint8_t *>(alloc(PAL_EXT_N));
	movi	a10, 0x110	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:112:     if (palRExt == nullptr || palGExt == nullptr || palBExt == nullptr) {
	l32i.n	a2, a3, 0	# palRExt, palRExt$51_1
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:110:         palBExt = static_cast<uint8_t *>(alloc(PAL_EXT_N));
	l32r	a3, .LC45	#, tmp70
	s32i.n	a10, a3, 0	# palBExt,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:112:     if (palRExt == nullptr || palGExt == nullptr || palBExt == nullptr) {
	bnez.n	a2, .L31	# palRExt$51_1,
.L33:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:113:         return false;
	movi.n	a2, 0	# <retval>,
	j	.L32		#
.L31:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:112:     if (palRExt == nullptr || palGExt == nullptr || palBExt == nullptr) {
	l32i.n	a3, a4, 0	# palGExt, palGExt$53_6
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:112:     if (palRExt == nullptr || palGExt == nullptr || palBExt == nullptr) {
	beqz.n	a3, .L33	# palGExt$53_6,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:112:     if (palRExt == nullptr || palGExt == nullptr || palBExt == nullptr) {
	l32r	a4, .LC45	#, tmp73
	l32i.n	a4, a4, 0	# palBExt, palBExt$54_7
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:112:     if (palRExt == nullptr || palGExt == nullptr || palBExt == nullptr) {
	beqz.n	a4, .L33	# palBExt$54_7,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:115:     palR = palRExt + PAL_PAD;
	l32r	a8, .LC46	#, tmp74
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:115:     palR = palRExt + PAL_PAD;
	addi.n	a2, a2, 8	# tmp75, palRExt$51_1,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:115:     palR = palRExt + PAL_PAD;
	s32i.n	a2, a8, 0	# palR, tmp75
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:116:     palG = palGExt + PAL_PAD;
	l32r	a2, .LC47	#, tmp76
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:116:     palG = palGExt + PAL_PAD;
	addi.n	a3, a3, 8	# tmp77, palGExt$53_6,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:116:     palG = palGExt + PAL_PAD;
	s32i.n	a3, a2, 0	# palG, tmp77
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:117:     palB = palBExt + PAL_PAD;
	l32r	a2, .LC48	#, tmp78
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:117:     palB = palBExt + PAL_PAD;
	addi.n	a4, a4, 8	# tmp79, palBExt$54_7,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:117:     palB = palBExt + PAL_PAD;
	s32i.n	a4, a2, 0	# palB, tmp79
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:118:     buildThemePalette();
	call8	_ZN12_GLOBAL__N_117buildThemePaletteEv		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:119:     lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:119:     lastThemeGen = themeGen();
	l32r	a4, .LC49	#, tmp80
	l32r	a2, .LC50	#, ivtmp$161
	l32r	a3, .LC51	#, ivtmp$162
	s32i.n	a10, a4, 0	# lastThemeGen,
	addi	a4, a2, 16	# _55, ivtmp$161,
.L34:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:121:         ditherI[i] = static_cast<int8_t>(lroundf((BAYER4[i] - 7.5f) * 0.5f));
	l8ui	a8, a2, 0	# MEM[base: _52, offset: 0B], MEM[base: _52, offset: 0B]
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:121:         ditherI[i] = static_cast<int8_t>(lroundf((BAYER4[i] - 7.5f) * 0.5f));
	l32r	a5, .LC52	#,
	float.s	f0, a8, 0	# tmp82, MEM[base: _52, offset: 0B]
	wfr	f1, a5	#,
	sub.s	f0, f0, f1	# tmp83, tmp82,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:121:         ditherI[i] = static_cast<int8_t>(lroundf((BAYER4[i] - 7.5f) * 0.5f));
	l32r	a5, .LC53	#,
	addi.n	a2, a2, 1	# ivtmp$161, ivtmp$161,
	wfr	f1, a5	#,
	mul.s	f0, f0, f1	#, tmp83,
	rfr	a10, f0	#,
	call8	lroundf		#
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:121:         ditherI[i] = static_cast<int8_t>(lroundf((BAYER4[i] - 7.5f) * 0.5f));
	s8i	a10, a3, 0	# MEM[base: _32, offset: 0B],
	addi.n	a3, a3, 1	# ivtmp$162, ivtmp$162,
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:120:     for (int i = 0; i < 16; i++) {
	bne	a2, a4, .L34	# ivtmp$161, _55,
	movi.n	a2, 1	# <retval>,
.L32:
# ../../src/display/ui/default/bganim/AnimCaustics.cpp:124: }
	retw.n
	.size	_ZN12_GLOBAL__N_14initEii, .-_ZN12_GLOBAL__N_14initEii
	.global	bg_anim_caustics
	.section	.rodata.str1.4,"aMS",@progbits,1
	.align	4
.LC54:
	.string	"caustics"
	.align	4
.LC55:
	.string	"Caustics"
	.align	4
.LC56:
	.string	"speed"
	.align	4
.LC57:
	.string	"Drift speed"
	.align	4
.LC58:
	.string	"scale"
	.align	4
.LC59:
	.string	"Cell scale"
	.align	4
.LC60:
	.string	"contrast"
	.align	4
.LC61:
	.string	"Contrast"
	.section	.rodata
	.align	4
	.type	bg_anim_caustics, @object
	.size	bg_anim_caustics, 68
bg_anim_caustics:
# id:
	.word	.LC54
# name:
	.word	.LC55
# params:
# key:
	.word	.LC56
# label:
	.word	.LC57
# def:
	.byte	50
	.zero	3
# key:
	.word	.LC58
# label:
	.word	.LC59
# def:
	.byte	45
	.zero	3
# key:
	.word	.LC60
# label:
	.word	.LC61
# def:
	.byte	55
	.zero	3
# key:
	.word	0
# label:
	.word	0
# def:
	.byte	0
	.zero	3
# init:
	.word	_ZN12_GLOBAL__N_14initEii
# frame:
	.word	_ZN12_GLOBAL__N_15frameEjiiPKh
# band:
	.word	_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.local	_ZN12_GLOBAL__N_18shapeLUTE
	.comm	_ZN12_GLOBAL__N_18shapeLUTE,1552,4
	.local	_ZN12_GLOBAL__N_17ditherIE
	.comm	_ZN12_GLOBAL__N_17ditherIE,16,4
	.local	_ZN12_GLOBAL__N_17g_stepQE
	.comm	_ZN12_GLOBAL__N_17g_stepQE,12,4
	.local	_ZN12_GLOBAL__N_18g_phaseQE
	.comm	_ZN12_GLOBAL__N_18g_phaseQE,12,4
	.local	_ZN12_GLOBAL__N_110g_rowFreqQE
	.comm	_ZN12_GLOBAL__N_110g_rowFreqQE,12,4
	.data
	.align	4
	.type	_ZN12_GLOBAL__N_112lastThemeGenE, @object
	.size	_ZN12_GLOBAL__N_112lastThemeGenE, 4
_ZN12_GLOBAL__N_112lastThemeGenE:
	.word	-1
	.local	_ZN12_GLOBAL__N_14palBE
	.comm	_ZN12_GLOBAL__N_14palBE,4,4
	.local	_ZN12_GLOBAL__N_14palGE
	.comm	_ZN12_GLOBAL__N_14palGE,4,4
	.local	_ZN12_GLOBAL__N_14palRE
	.comm	_ZN12_GLOBAL__N_14palRE,4,4
	.local	_ZN12_GLOBAL__N_17palBExtE
	.comm	_ZN12_GLOBAL__N_17palBExtE,4,4
	.local	_ZN12_GLOBAL__N_17palGExtE
	.comm	_ZN12_GLOBAL__N_17palGExtE,4,4
	.local	_ZN12_GLOBAL__N_17palRExtE
	.comm	_ZN12_GLOBAL__N_17palRExtE,4,4
	.section	.rodata
	.align	4
	.type	_ZN12_GLOBAL__N_1L9SPEED_MULE, @object
	.size	_ZN12_GLOBAL__N_1L9SPEED_MULE, 12
_ZN12_GLOBAL__N_1L9SPEED_MULE:
	.word	1065353216
	.word	1062333317
	.word	1067702026
	.align	4
	.type	_ZN12_GLOBAL__N_1L9FREQ_BASEE, @object
	.size	_ZN12_GLOBAL__N_1L9FREQ_BASEE, 12
_ZN12_GLOBAL__N_1L9FREQ_BASEE:
	.word	1027369599
	.word	1031396131
	.word	1024953680
	.align	4
	.type	_ZN12_GLOBAL__N_1L6PHASE0E, @object
	.size	_ZN12_GLOBAL__N_1L6PHASE0E, 12
_ZN12_GLOBAL__N_1L6PHASE0E:
	.word	0
	.word	1074161254
	.word	1083388723
	.align	4
	.type	_ZN12_GLOBAL__N_1L9ANG_DRIFTE, @object
	.size	_ZN12_GLOBAL__N_1L9ANG_DRIFTE, 12
_ZN12_GLOBAL__N_1L9ANG_DRIFTE:
	.word	1013276738
	.word	-1138501878
	.word	1004888130
	.align	4
	.type	_ZN12_GLOBAL__N_1L10BASE_ANGLEE, @object
	.size	_ZN12_GLOBAL__N_1L10BASE_ANGLEE, 12
_ZN12_GLOBAL__N_1L10BASE_ANGLEE:
	.word	1051931443
	.word	1076048691
	.word	1083283866
	.ident	"GCC: (crosstool-NG esp-2021r2-patch5) 8.4.0"
