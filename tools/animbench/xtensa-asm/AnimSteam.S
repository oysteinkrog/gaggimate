	.file	"AnimSteam.cpp"
# GNU C++17 (crosstool-NG esp-2021r2-patch5) version 8.4.0 (xtensa-esp32s3-elf)
#	compiled by GNU C version 6.3.0 20170516, GMP version 6.2.1, MPFR version 4.1.0, MPC version 1.2.1, isl version isl-0.19-GMP

# GGC heuristics: --param ggc-min-expand=100 --param ggc-min-heapsize=131072
# options passed:  -I shim -imultilib no-rtti
# -iprefix c:\users\oystein\.platformio\packages\toolchain-xtensa-esp32s3\bin\../lib/gcc/xtensa-esp32s3-elf/8.4.0/
# ../../src/display/ui/default/bganim/AnimSteam.cpp -mlongcalls
# -auxbase-strip xtensa-asm/AnimSteam.S -O2 -std=gnu++17 -fno-exceptions
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
	.global	__divsf3
	.section	.text._ZN12_GLOBAL__N_19rebuildBgEv,"ax",@progbits
	.literal_position
	.literal .LC0, _ZN12_GLOBAL__N_13g_hE
	.literal .LC1, 0x3f000000
	.literal .LC2, 0x40c00000
	.literal .LC3, 0x41100000
	.literal .LC4, _ZN12_GLOBAL__N_15bgLUTE
	.align	4
	.type	_ZN12_GLOBAL__N_19rebuildBgEv, @function
_ZN12_GLOBAL__N_19rebuildBgEv:
	entry	sp, 48	#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:55:     for (int y = 0; y < g_h; y++) {
	l32r	a2, .LC0	#, tmp112
	l32i.n	a9, a2, 0	# g_h, g_h$19_1
	blti	a9, 1, .L1	# g_h$19_1,,
	l32r	a5, .LC4	#, tmp111
# ../../src/display/ui/default/bganim/AnimSteam.cpp:55:     for (int y = 0; y < g_h; y++) {
	movi.n	a6, 0	# y,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a4, -0x800	# tmp91,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a3, 0x7e0	# tmp96,
.L3:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:56:         const float n = fabsf(y - g_h * 0.5f) / (g_h * 0.5f);
	l32r	a8, .LC1	#,
	float.s	f0, a9, 0	# tmp75, g_h$19_1
	wfr	f2, a8	#,
	mul.s	f0, f0, f2	# _4, tmp75,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:56:         const float n = fabsf(y - g_h * 0.5f) / (g_h * 0.5f);
	float.s	f1, a6, 0	# tmp77, y
	sub.s	f1, f1, f0	# tmp78, tmp77, _4
# ../../src/display/ui/default/bganim/AnimSteam.cpp:56:         const float n = fabsf(y - g_h * 0.5f) / (g_h * 0.5f);
	rfr	a11, f0	#, _4
	abs.s	f1, f1	#, tmp78
	rfr	a10, f1	#,
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:58:         themeRGB(static_cast<int>(9.0f - n * 6.0f), c);
	l32r	a8, .LC3	#,
	wfr	f1, a10	#,
	wfr	f0, a8	# tmp81,
	l32r	a8, .LC2	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:58:         themeRGB(static_cast<int>(9.0f - n * 6.0f), c);
	mov.n	a11, sp	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:58:         themeRGB(static_cast<int>(9.0f - n * 6.0f), c);
	wfr	f2, a8	#,
	msub.s	f0, f1, f2	# tmp81,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:58:         themeRGB(static_cast<int>(9.0f - n * 6.0f), c);
	trunc.s	a10, f0, 0	#, tmp81
	call8	_ZN6bganim8themeRGBEiPh		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:59:         bgLUT[y] = rgb565(c[0], c[1], c[2]);
	l8ui	a8, sp, 0	# c, c
	l8ui	a9, sp, 1	# c, c
	l8ui	a11, sp, 2	# c, c
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a8, a8, 8	# tmp90, c,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a9, a9, 3	# tmp95, c,
	and	a9, a9, a3	# tmp98, tmp95, tmp96
# ../../src/display/ui/default/bganim/AnimSteam.cpp:59:         bgLUT[y] = rgb565(c[0], c[1], c[2]);
	l32i.n	a10, a5, 0	# bgLUT, bgLUT
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a8, a8, a4	# tmp93, tmp90, tmp91
# ../../src/display/ui/default/bganim/AnimSteam.cpp:59:         bgLUT[y] = rgb565(c[0], c[1], c[2]);
	slli	a12, a6, 1	# tmp87, y,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a8, a8, a9	# tmp101, tmp93, tmp98
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	srai	a9, a11, 3	# tmp103, c,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a8, a8, a9	# tmp106, tmp101, tmp103
# ../../src/display/ui/default/bganim/AnimSteam.cpp:59:         bgLUT[y] = rgb565(c[0], c[1], c[2]);
	add.n	a10, a10, a12	# tmp88, bgLUT, tmp87
# ../../src/display/ui/default/bganim/AnimSteam.cpp:55:     for (int y = 0; y < g_h; y++) {
	l32i.n	a9, a2, 0	# g_h, g_h$19_1
# ../../src/display/ui/default/bganim/AnimSteam.cpp:59:         bgLUT[y] = rgb565(c[0], c[1], c[2]);
	s16i	a8, a10, 0	# *_19, tmp106
# ../../src/display/ui/default/bganim/AnimSteam.cpp:55:     for (int y = 0; y < g_h; y++) {
	addi.n	a6, a6, 1	# y, y,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:55:     for (int y = 0; y < g_h; y++) {
	blt	a6, a9, .L3	# y, g_h$19_1,
.L1:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:61: }
	retw.n
	.size	_ZN12_GLOBAL__N_19rebuildBgEv, .-_ZN12_GLOBAL__N_19rebuildBgEv
	.section	.text._ZN12_GLOBAL__N_15frameEjiiPKh,"ax",@progbits
	.literal_position
	.literal .LC5, _ZN12_GLOBAL__N_112lastThemeGenE
	.literal .LC6, 1374389535
	.literal .LC7, _ZN12_GLOBAL__N_110builtCountE
	.literal .LC8, 0x3f000000
	.literal .LC9, 0x3f666666
	.literal .LC10, 0x00000000
	.literal .LC11, 0x3f333333
	.literal .LC12, _ZN12_GLOBAL__N_13rngE
	.literal .LC13, _ZN12_GLOBAL__N_15wispsE
	.literal .LC14, 0x33800000
	.literal .LC15, 0x40000000
	.literal .LC16, 0xbf800000
	.literal .LC17, 0x40c90fdb
	.literal .LC18, 0x399d4952
	.literal .LC19, 0x39ebedfa
	.literal .LC20, 0x3a378034
	.literal .LC21, 0x3aaa64c3
	.literal .LC22, _ZN12_GLOBAL__N_15blobsE
	.literal .LC23, 0x44e10000
	.literal .LC24, 0x45834000
	.literal .LC25, _ZN12_GLOBAL__N_19wispCountE
	.literal .LC26, 0x3d610e10
	.literal .LC27, 0x3d0b4396
	.literal .LC28, 0x42c80000
	.literal .LC29, 0x3fd9999a
	.literal .LC30, 0x3f4ccccd
	.literal .LC31, 0x3f1eb852
	.literal .LC32, _ZN12_GLOBAL__N_18g_activeE
	.literal .LC33, _ZN12_GLOBAL__N_15drawsE
	.literal .LC34, 0x3f800000
	.literal .LC35, 0x3fc90fda
	.literal .LC36, 0x4222f983
	.literal .LC37, 0x3e99999a
	.literal .LC38, 0x3f59999a
	.literal .LC39, 0x41d00000
	.literal .LC40, 0x41200000
	.literal .LC41, 0x41b00000
	.literal .LC42, 0x40a00000
	.literal .LC43, 0x3eb33333
	.literal .LC44, 0x3ee147ae
	.literal .LC45, 0x40800000
	.literal .LC46, 0x3ecccccd
	.literal .LC47, 0x437f0000
	.literal .LC48, 0x4a7c0000
	.literal .LC49, 0x425c0000
	.literal .LC50, 0xc1f00000
	.literal .LC51, 178956971
	.literal .LC52, 536870911
	.align	4
	.type	_ZN12_GLOBAL__N_15frameEjiiPKh, @function
_ZN12_GLOBAL__N_15frameEjiiPKh:
	entry	sp, 112	#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:107: void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
	s32i.n	a2, sp, 40	# %sfp, tMs
# ../../src/display/ui/default/bganim/AnimSteam.cpp:108:     if (themeGen() != lastThemeGen) {
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:108:     if (themeGen() != lastThemeGen) {
	l32r	a2, .LC5	#, tmp293
# ../../src/display/ui/default/bganim/AnimSteam.cpp:108:     if (themeGen() != lastThemeGen) {
	l32i.n	a6, a2, 0	# lastThemeGen, lastThemeGen
	beq	a10, a6, .L7	#, lastThemeGen,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:109:         rebuildBg();
	call8	_ZN12_GLOBAL__N_19rebuildBgEv		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:110:         lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:110:         lastThemeGen = themeGen();
	s32i.n	a10, a2, 0	# lastThemeGen,
.L7:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:112:     const int count = 2 + (p[1] * 3) / 100;
	l8ui	a6, a5, 1	# MEM[(const uint8_t *)p_104(D) + 1B], MEM[(const uint8_t *)p_104(D) + 1B]
# ../../src/display/ui/default/bganim/AnimSteam.cpp:113:     if (count != builtCount) {
	l32r	a15, .LC7	#, tmp610
# ../../src/display/ui/default/bganim/AnimSteam.cpp:112:     const int count = 2 + (p[1] * 3) / 100;
	slli	a2, a6, 1	# tmp300, MEM[(const uint8_t *)p_104(D) + 1B],
	add.n	a2, a2, a6	# tmp301, tmp300, MEM[(const uint8_t *)p_104(D) + 1B]
# ../../src/display/ui/default/bganim/AnimSteam.cpp:112:     const int count = 2 + (p[1] * 3) / 100;
	l32r	a6, .LC6	#, tmp303
	muluh	a2, a2, a6	# tmp581, tmp301, tmp303
# ../../src/display/ui/default/bganim/AnimSteam.cpp:113:     if (count != builtCount) {
	l32i.n	a6, a15, 0	# builtCount, builtCount
# ../../src/display/ui/default/bganim/AnimSteam.cpp:112:     const int count = 2 + (p[1] * 3) / 100;
	srli	a2, a2, 5	# _7, tmp581,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:112:     const int count = 2 + (p[1] * 3) / 100;
	addi.n	a12, a2, 2	# _560, _7,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:113:     if (count != builtCount) {
	bne	a6, a12, .L36	# builtCount, _560,
	l32r	a2, .LC8	#,
	float.s	f0, a4, 0	#, h
	s32i.n	a2, sp, 24	# %sfp, tmp19
	l32r	a3, .LC13	#,
	l32r	a2, .LC10	#,
	l32r	a6, .LC22	#,
	ssi	f0, sp, 20	# %sfp,
	s32i.n	a2, sp, 32	# %sfp, tmp20
	s32i.n	a3, sp, 44	# %sfp,
	s32i.n	a6, sp, 16	# %sfp,
	j	.L8		#
.L36:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:64:     const float cx = w * 0.5f, cy = h * 0.5f;
	l32r	a6, .LC8	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:64:     const float cx = w * 0.5f, cy = h * 0.5f;
	float.s	f7, a3, 0	# tmp312, w
# ../../src/display/ui/default/bganim/AnimSteam.cpp:64:     const float cx = w * 0.5f, cy = h * 0.5f;
	wfr	f1, a6	#,
	mul.s	f7, f7, f1	# cx, tmp312,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:64:     const float cx = w * 0.5f, cy = h * 0.5f;
	float.s	f0, a4, 0	#, h
# ../../src/display/ui/default/bganim/AnimSteam.cpp:64:     const float cx = w * 0.5f, cy = h * 0.5f;
	s32i.n	a6, sp, 24	# %sfp, tmp19
# ../../src/display/ui/default/bganim/AnimSteam.cpp:64:     const float cx = w * 0.5f, cy = h * 0.5f;
	ssi	f0, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:64:     const float cx = w * 0.5f, cy = h * 0.5f;
	mul.s	f1, f0, f1	# cy,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:65:     const float rDisp = (w < h ? w : h) * 0.5f;
	mov.s	f0, f7	# iftmp$24_185, cx
	blt	a3, a4, .L9	# w, h,
	mov.s	f0, f1	# iftmp$24_185, cy
.L9:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:66:     const float y0 = h * 0.90f;
	l32r	a3, .LC9	#,
	lsi	f2, sp, 20	# %sfp,
	wfr	f3, a3	#,
	mul.s	f6, f2, f3	# y0,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:68:     const float halfSpan = sqrtf(fmaxf(0.0f, rDisp * rDisp - dy * dy)) * 0.7f;
	mul.s	f0, f0, f0	# tmp316, iftmp$24_185, iftmp$24_185
# ../../src/display/ui/default/bganim/AnimSteam.cpp:68:     const float halfSpan = sqrtf(fmaxf(0.0f, rDisp * rDisp - dy * dy)) * 0.7f;
	l32r	a3, .LC10	#,
	s32i	a12, sp, 76	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:67:     const float dy = y0 - cy;
	sub.s	f1, f6, f1	# dy, y0, cy
# ../../src/display/ui/default/bganim/AnimSteam.cpp:68:     const float halfSpan = sqrtf(fmaxf(0.0f, rDisp * rDisp - dy * dy)) * 0.7f;
	mov.n	a10, a3	#,
	s32i	a15, sp, 72	#,
	ssi	f6, sp, 68	#,
	msub.s	f0, f1, f1	# tmp316, dy, dy
	ssi	f7, sp, 64	#,
	s32i.n	a3, sp, 32	# %sfp, tmp21
	rfr	a11, f0	#, tmp316
	call8	fmaxf		#
	call8	sqrtf		#
	slli	a9, a2, 1	# tmp323, _7,
	add.n	a9, a9, a2	# tmp324, tmp323, _7
	slli	a9, a9, 3	# tmp325, tmp324,
	l32r	a2, .LC51	#, tmp632
	addi	a9, a9, 24	# tmp629, tmp325,
	srli	a9, a9, 3	# tmp630, tmp629,
	mull	a9, a9, a2	# tmp631, tmp630, tmp632
# ../../src/display/ui/default/bganim/AnimSteam.cpp:68:     const float halfSpan = sqrtf(fmaxf(0.0f, rDisp * rDisp - dy * dy)) * 0.7f;
	l32r	a2, .LC11	#,
	l32r	a6, .LC14	#,
	wfr	f1, a2	#,
	l32r	a2, .LC52	#, tmp634
	l32r	a8, .LC15	#,
	l32r	a3, .LC13	#,
	and	a9, a9, a2	# tmp633, tmp631, tmp634
	wfr	f0, a10	#,
	l32r	a2, .LC16	#,
	wfr	f2, a6	# tmp604,
	l32r	a6, .LC17	#,
	l32r	a14, .LC12	#, tmp591
	wfr	f14, a8	# tmp605,
	mul.s	f8, f0, f1	# halfSpan,,
	l32r	a8, .LC18	#,
	s32i.n	a3, sp, 44	# %sfp,
	wfr	f13, a2	# tmp606,
	wfr	f5, a6	# tmp607,
	l32r	a2, .LC19	#,
	l32r	a6, .LC20	#,
	wfr	f12, a8	# tmp608,
	l32i.n	a3, a14, 0	# rng, rng_lsm$36
	l32i.n	a8, sp, 44	# %sfp, ivtmp$57
	lsi	f7, sp, 64	#,
	lsi	f6, sp, 68	#,
	l32i	a12, sp, 76	#,
	l32i	a15, sp, 72	#,
	wfr	f11, a2	# tmp609,
	wfr	f10, a6	# tmp598,
	addi.n	a9, a9, 1	# tmp627, tmp633,
	loop	a9, .L10_LEND	# tmp627,
.L10:
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a10, a3, 13	# tmp326, rng_lsm$36,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a3, a10, a3	# _195, tmp326, rng_lsm$36
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a10, a3, 17, 15	# tmp327, _195,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a3, a10, a3	# _197, tmp327, _195
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a6, a3, 5	# tmp328, _197,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a10, a6, a3	# _199, tmp328, _197
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a2, a10, 13	# tmp329, _199,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a2, a2, a10	# _208, tmp329, _199
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a4, a2, 17, 15	# tmp330, _208,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a2, a4, a2	# _210, tmp330, _208
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a4, a2, 5	# tmp331, _210,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a4, a4, a2	# _212, tmp331, _210
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a2, a4, 13	# tmp332, _212,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a3, a2, a4	# _218, tmp332, _212
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a2, a3, 17, 15	# tmp333, _218,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a2, a2, a3	# _220, tmp333, _218
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a3, a2, 5	# tmp334, _220,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a2, a3, a2	# _222, tmp334, _220
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a3, a2, 13	# tmp335, _222,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a3, a3, a2	# _228, tmp335, _222
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a7, a3, 17, 15	# tmp336, _228,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a3, a7, a3	# _230, tmp336, _228
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a6, a3, 5	# tmp337, _230,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a7, a6, a3	# _232, tmp337, _230
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a6, a10, 8	# tmp341, _199,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f3, a6, 0	# tmp342, tmp341
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a3, a7, 13	# tmp338, _232,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f3, f3, f2	# tmp343, tmp342, tmp604
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a11, a3, a7	# _239, tmp338, _232
# ../../src/display/ui/default/bganim/AnimSteam.cpp:70:         wisps[i] = {cx + (nextRandf(rng) * 2.0f - 1.0f) * halfSpan,
	mov.s	f9, f13	#, tmp606
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a3, a11, 17, 15	# tmp339, _239,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:70:         wisps[i] = {cx + (nextRandf(rng) * 2.0f - 1.0f) * halfSpan,
	madd.s	f9, f3, f14	#, tmp343, tmp605
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a3, a3, a11	# _241, tmp339, _239
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a6, a3, 5	# tmp340, _241,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a3, a6, a3	# rng_lsm$36, tmp340, _241
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a6, a7, 8	# tmp361, _232,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:70:         wisps[i] = {cx + (nextRandf(rng) * 2.0f - 1.0f) * halfSpan,
	mov.s	f3, f9	# tmp345,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f1, a6, 0	# tmp362, tmp361
# ../../src/display/ui/default/bganim/AnimSteam.cpp:70:         wisps[i] = {cx + (nextRandf(rng) * 2.0f - 1.0f) * halfSpan,
	mov.s	f9, f7	#, cx
	madd.s	f9, f8, f3	#, halfSpan, tmp345
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f1, f1, f2	# tmp363, tmp362, tmp604
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a2, a2, 8	# tmp355, _222,
	srli	a4, a4, 8	# tmp349, _212,
	srli	a7, a3, 8	# tmp368, rng_lsm$36,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f0, a2, 0	# tmp356, tmp355
# ../../src/display/ui/default/bganim/AnimSteam.cpp:70:         wisps[i] = {cx + (nextRandf(rng) * 2.0f - 1.0f) * halfSpan,
	mov.s	f3, f9	# tmp348,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f15, a4, 0	# tmp350, tmp349
	float.s	f4, a7, 0	# tmp369, tmp368
# ../../src/display/ui/default/bganim/AnimSteam.cpp:74:                     0.00045f + nextRandf(rng) * 0.0003f,
	mov.s	f9, f11	#, tmp609
	madd.s	f9, f1, f12	#, tmp363, tmp608
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f15, f15, f2	# tmp351, tmp350, tmp604
	mul.s	f0, f0, f2	# tmp357, tmp356, tmp604
	mul.s	f4, f4, f2	# tmp370, tmp369, tmp604
# ../../src/display/ui/default/bganim/AnimSteam.cpp:75:                     0.0013f + nextRandf(rng) * 0.0007f};
	l32r	a2, .LC21	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:74:                     0.00045f + nextRandf(rng) * 0.0003f,
	mov.s	f1, f9	# tmp365,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:75:                     0.0013f + nextRandf(rng) * 0.0007f};
	wfr	f9, a2	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:72:                     nextRandf(rng) * 6.2831853f,
	mul.s	f15, f15, f5	# tmp353, tmp351, tmp607
# ../../src/display/ui/default/bganim/AnimSteam.cpp:73:                     nextRandf(rng) * 6.2831853f,
	mul.s	f0, f0, f5	# tmp359, tmp357, tmp607
# ../../src/display/ui/default/bganim/AnimSteam.cpp:75:                     0.0013f + nextRandf(rng) * 0.0007f};
	madd.s	f9, f4, f10	#, tmp370, tmp598
# ../../src/display/ui/default/bganim/AnimSteam.cpp:70:         wisps[i] = {cx + (nextRandf(rng) * 2.0f - 1.0f) * halfSpan,
	ssi	f3, a8, 0	# MEM[base: _554, offset: 0B], tmp348
	ssi	f6, a8, 4	# MEM[base: _554, offset: 4B], y0
	ssi	f15, a8, 8	# MEM[base: _554, offset: 8B], tmp353
	ssi	f0, a8, 12	# MEM[base: _554, offset: 12B], tmp359
	ssi	f1, a8, 16	# MEM[base: _554, offset: 16B], tmp365
	ssi	f9, a8, 20	# MEM[base: _554, offset: 20B], tmp372
	addi	a8, a8, 24	# ivtmp$57, ivtmp$57,
	.L10_LEND:		#
	l32r	a6, .LC22	#,
	l32r	a9, .LC24	#,
	l32r	a8, .LC23	#,
	movi	a11, 0xb0	# tmp376,
	l32i.n	a7, sp, 40	# %sfp, tMs
	wfr	f4, a9	# tmp601,
	s32i.n	a6, sp, 16	# %sfp,
	wfr	f6, a8	# tmp600,
	add.n	a10, a11, a6	# ivtmp$53, tmp376,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:77:     for (int i = 0; i < count; i++) {
	movi.n	a9, 0	# i,
	movi	a13, -0xb0	# tmp378,
	s32i.n	a5, sp, 28	# %sfp, p
.L12:
	extui	a6, a9, 0, 8	# _558, i
	add.n	a4, a10, a13	# ivtmp$48, ivtmp$53, tmp378
	movi.n	a5, 0xb	# tmp626,
	loop	a5, .L11_LEND	# tmp626,
.L11:
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a2, a3, 13	# tmp379, rng_lsm$36,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a2, a2, a3	# _256, tmp379, rng_lsm$36
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a3, a2, 17, 15	# tmp380, _256,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a3, a3, a2	# _258, tmp380, _256
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a2, a3, 5	# tmp381, _258,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a3, a2, a3	# _260, tmp381, _258
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a2, a3, 13	# tmp388, _260,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a8, a2, a3	# _268, tmp388, _260
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a2, a8, 17, 15	# tmp389, _268,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a2, a2, a8	# _270, tmp389, _268
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a8, a2, 5	# tmp390, _270,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a2, a8, a2	# _272, tmp390, _270
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a3, a3, 8	# tmp382, _260,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f1, a3, 0	# tmp383, tmp382
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a3, a2, 13	# tmp391, _272,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a3, a3, a2	# _280, tmp391, _272
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f1, f1, f2	# tmp384, tmp383, tmp604
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a8, a3, 17, 15	# tmp392, _280,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a3, a8, a3	# _282, tmp392, _280
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a2, a2, 8	# tmp394, _272,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f0, a2, 0	# tmp395, tmp394
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a8, a3, 5	# tmp393, _282,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a3, a8, a3	# rng_lsm$36, tmp393, _282
# ../../src/display/ui/default/bganim/AnimSteam.cpp:80:             const float lifetime = 4200.0f + nextRandf(rng) * 1800.0f;
	mov.s	f3, f4	# lifetime, tmp601
	madd.s	f3, f1, f6	# lifetime, tmp384, tmp600
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a2, a3, 8	# tmp401, rng_lsm$36,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f0, f0, f2	# tmp396, tmp395, tmp604
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f1, a2, 0	# tmp402, tmp401
# ../../src/display/ui/default/bganim/AnimSteam.cpp:81:             blobs[idx] = {static_cast<uint8_t>(i), tMs - static_cast<uint32_t>(nextRandf(rng) * lifetime), lifetime,
	s8i	a6, a4, 0	# MEM[base: _575, offset: 0B], _558
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f1, f1, f2	# tmp403, tmp402, tmp604
# ../../src/display/ui/default/bganim/AnimSteam.cpp:81:             blobs[idx] = {static_cast<uint8_t>(i), tMs - static_cast<uint32_t>(nextRandf(rng) * lifetime), lifetime,
	mul.s	f0, f0, f3	# tmp398, tmp396, lifetime
# ../../src/display/ui/default/bganim/AnimSteam.cpp:81:             blobs[idx] = {static_cast<uint8_t>(i), tMs - static_cast<uint32_t>(nextRandf(rng) * lifetime), lifetime,
	ssi	f3, a4, 8	# MEM[base: _575, offset: 8B], lifetime
# ../../src/display/ui/default/bganim/AnimSteam.cpp:82:                           nextRandf(rng) * 6.2831853f};
	mul.s	f1, f1, f5	# tmp405, tmp403, tmp607
# ../../src/display/ui/default/bganim/AnimSteam.cpp:81:             blobs[idx] = {static_cast<uint8_t>(i), tMs - static_cast<uint32_t>(nextRandf(rng) * lifetime), lifetime,
	utrunc.s	a2, f0, 0	# tmp399, tmp398
# ../../src/display/ui/default/bganim/AnimSteam.cpp:81:             blobs[idx] = {static_cast<uint8_t>(i), tMs - static_cast<uint32_t>(nextRandf(rng) * lifetime), lifetime,
	sub	a2, a7, a2	# tmp400, tMs, tmp399
# ../../src/display/ui/default/bganim/AnimSteam.cpp:81:             blobs[idx] = {static_cast<uint8_t>(i), tMs - static_cast<uint32_t>(nextRandf(rng) * lifetime), lifetime,
	s32i.n	a2, a4, 4	# MEM[base: _575, offset: 4B], tmp400
	ssi	f1, a4, 12	# MEM[base: _575, offset: 12B], tmp405
	addi	a4, a4, 16	# ivtmp$48, ivtmp$48,
	.L11_LEND:		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:77:     for (int i = 0; i < count; i++) {
	addi.n	a9, a9, 1	# i, i,
	add.n	a10, a10, a11	# ivtmp$53, ivtmp$53, tmp407
# ../../src/display/ui/default/bganim/AnimSteam.cpp:77:     for (int i = 0; i < count; i++) {
	bne	a9, a12, .L12	# i, _560,
	l32i.n	a5, sp, 28	# %sfp, p
	s32i.n	a3, a14, 0	# rng, rng_lsm$36
# ../../src/display/ui/default/bganim/AnimSteam.cpp:85:     builtCount = count;
	s32i.n	a12, a15, 0	# builtCount, _560
.L8:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:117:     const float riseSpeed = 0.034f * speedMul(p[0]);
	l8ui	a2, a5, 0	# *p_104(D), *p_104(D)
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	addi	a2, a2, -50	# tmp412, *p_104(D),
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	float.s	f0, a2, 0	# tmp413, tmp412
	l32r	a2, .LC26	#,
	wfr	f1, a2	#,
	mul.s	f0, f0, f1	#, tmp413,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:116:     wispCount = count;
	l32r	a2, .LC25	#, tmp410
	s32i.n	a12, a2, 0	# wispCount, _560
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	rfr	a10, f0	#,
	s32i	a12, sp, 76	#,
	call8	exp2f		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:117:     const float riseSpeed = 0.034f * speedMul(p[0]);
	l32r	a3, .LC27	#,
	wfr	f0, a10	#,
	wfr	f1, a3	#,
	mul.s	f0, f0, f1	#,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:118:     const float swirl = 0.5f + (p[2] / 100.0f) * 1.7f;
	l8ui	a2, a5, 2	# MEM[(const uint8_t *)p_104(D) + 2B], MEM[(const uint8_t *)p_104(D) + 2B]
# ../../src/display/ui/default/bganim/AnimSteam.cpp:118:     const float swirl = 0.5f + (p[2] / 100.0f) * 1.7f;
	l32r	a11, .LC28	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:117:     const float riseSpeed = 0.034f * speedMul(p[0]);
	ssi	f0, sp, 48	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:118:     const float swirl = 0.5f + (p[2] / 100.0f) * 1.7f;
	float.s	f0, a2, 0	#, MEM[(const uint8_t *)p_104(D) + 2B]
	l32i.n	a2, sp, 40	# %sfp,
	rfr	a10, f0	#,
	ufloat.s	f1, a2, 0	#,
	ssi	f1, sp, 36	# %sfp,
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:118:     const float swirl = 0.5f + (p[2] / 100.0f) * 1.7f;
	l32r	a3, .LC29	#,
	lsi	f0, sp, 24	# %sfp,
	wfr	f1, a10	#,
	wfr	f2, a3	#,
	madd.s	f0, f1, f2	#,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:119:     const float density = 0.5f + (p[3] / 100.0f) * 0.8f;
	l8ui	a2, a5, 3	# MEM[(const uint8_t *)p_104(D) + 3B], MEM[(const uint8_t *)p_104(D) + 3B]
# ../../src/display/ui/default/bganim/AnimSteam.cpp:119:     const float density = 0.5f + (p[3] / 100.0f) * 0.8f;
	l32r	a11, .LC28	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:118:     const float swirl = 0.5f + (p[2] / 100.0f) * 1.7f;
	ssi	f0, sp, 52	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:119:     const float density = 0.5f + (p[3] / 100.0f) * 0.8f;
	float.s	f0, a2, 0	#, MEM[(const uint8_t *)p_104(D) + 3B]
	rfr	a10, f0	#,
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:119:     const float density = 0.5f + (p[3] / 100.0f) * 0.8f;
	l32r	a3, .LC30	#,
	lsi	f1, sp, 24	# %sfp,
	wfr	f3, a3	#,
	wfr	f2, a10	#,
	madd.s	f1, f2, f3	#,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:120:     const float maxHeight = h * 0.62f;
	l32r	a3, .LC31	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:122:     g_active = wispCount * BLOBS_PER_WISP;
	l32i	a12, sp, 76	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:120:     const float maxHeight = h * 0.62f;
	lsi	f0, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:119:     const float density = 0.5f + (p[3] / 100.0f) * 0.8f;
	ssi	f1, sp, 56	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:120:     const float maxHeight = h * 0.62f;
	wfr	f1, a3	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:122:     g_active = wispCount * BLOBS_PER_WISP;
	slli	a2, a12, 2	# tmp431, _560,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:120:     const float maxHeight = h * 0.62f;
	mul.s	f0, f0, f1	#,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:122:     g_active = wispCount * BLOBS_PER_WISP;
	add.n	a2, a2, a12	# tmp432, tmp431, _560
	slli	a2, a2, 1	# tmp433, tmp432,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:122:     g_active = wispCount * BLOBS_PER_WISP;
	l32r	a5, .LC32	#, tmp582
# ../../src/display/ui/default/bganim/AnimSteam.cpp:122:     g_active = wispCount * BLOBS_PER_WISP;
	add.n	a12, a2, a12	# tmp434, tmp433, _560
# ../../src/display/ui/default/bganim/AnimSteam.cpp:123:     for (int i = 0; i < g_active; i++) {
	movi.n	a3, 0	# i,
	l32r	a6, .LC33	#, ivtmp$40
	l32i.n	a2, sp, 16	# %sfp, ivtmp$39
# ../../src/display/ui/default/bganim/AnimSteam.cpp:120:     const float maxHeight = h * 0.62f;
	ssi	f0, sp, 60	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:122:     g_active = wispCount * BLOBS_PER_WISP;
	s32i.n	a12, a5, 0	# g_active, tmp434
# ../../src/display/ui/default/bganim/AnimSteam.cpp:157:         d.visible = d.a8 > 0 && y > -30.0f;
	mov.n	a4, a3	# iftmp$18_94, i
.L23:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:126:         float age = static_cast<float>(tMs - b.birth);
	l32i.n	a8, a2, 4	# MEM[base: _596, offset: 4], MEM[base: _596, offset: 4]
	l32i.n	a9, sp, 40	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:127:         if (age > b.lifetime) {
	lsi	f1, a2, 8	# MEM[base: _596, offset: 8], _27
# ../../src/display/ui/default/bganim/AnimSteam.cpp:126:         float age = static_cast<float>(tMs - b.birth);
	sub	a8, a9, a8	# tmp435,, MEM[base: _596, offset: 4]
# ../../src/display/ui/default/bganim/AnimSteam.cpp:126:         float age = static_cast<float>(tMs - b.birth);
	ufloat.s	f0, a8, 0	# age, tmp435
# ../../src/display/ui/default/bganim/AnimSteam.cpp:127:         if (age > b.lifetime) {
	olt.s	b0, f1, f0	#, _27, age
	bf	b0, .L13	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:128:             b.birth = tMs - static_cast<uint32_t>(fmodf(age, b.lifetime));
	rfr	a10, f0	#, age
	rfr	a11, f1	#, _27
	ssi	f1, sp, 64	#,
	call8	fmodf		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:128:             b.birth = tMs - static_cast<uint32_t>(fmodf(age, b.lifetime));
	wfr	f0, a10	#,
	utrunc.s	a10, f0, 0	# _29,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:128:             b.birth = tMs - static_cast<uint32_t>(fmodf(age, b.lifetime));
	l32i.n	a9, sp, 40	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:129:             age = static_cast<float>(tMs - b.birth);
	ufloat.s	f0, a10, 0	# age, _29
# ../../src/display/ui/default/bganim/AnimSteam.cpp:128:             b.birth = tMs - static_cast<uint32_t>(fmodf(age, b.lifetime));
	sub	a8, a9, a10	# tmp437,, _29
# ../../src/display/ui/default/bganim/AnimSteam.cpp:128:             b.birth = tMs - static_cast<uint32_t>(fmodf(age, b.lifetime));
	lsi	f1, sp, 64	#,
	s32i.n	a8, a2, 4	# MEM[base: _596, offset: 4], tmp437
.L13:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:133:         const float rise = riseSpeed * age;
	lsi	f2, sp, 48	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:131:         const float L = age / b.lifetime;
	rfr	a11, f1	#, _27
# ../../src/display/ui/default/bganim/AnimSteam.cpp:133:         const float rise = riseSpeed * age;
	mul.s	f2, f0, f2	#, age,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:131:         const float L = age / b.lifetime;
	rfr	a10, f0	#, age
# ../../src/display/ui/default/bganim/AnimSteam.cpp:133:         const float rise = riseSpeed * age;
	ssi	f2, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:131:         const float L = age / b.lifetime;
	call8	__divsf3		#
	wfr	f2, a10	# tmp438,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:134:         const float heightFrac = fminf(1.0f, rise / maxHeight);
	l32i.n	a11, sp, 60	# %sfp,
	l32i.n	a10, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:132:         const Wisp &wp = wisps[b.wisp];
	l8ui	a7, a2, 0	# MEM[base: _596, offset: 0], _34
# ../../src/display/ui/default/bganim/AnimSteam.cpp:134:         const float heightFrac = fminf(1.0f, rise / maxHeight);
	ssi	f2, sp, 76	#,
	call8	__divsf3		#
	mov.n	a11, a10	#,
	l32r	a10, .LC34	#,
	call8	fminf		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:135:         const float y = wp.y0 - rise;
	slli	a11, a7, 1	# tmp443, _34,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	l32r	a8, .LC35	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:135:         const float y = wp.y0 - rise;
	l32i.n	a9, sp, 44	# %sfp,
	add.n	a11, a11, a7	# tmp444, tmp443, _34
	slli	a11, a11, 3	# tmp445, tmp444,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	lsi	f0, a2, 12	# MEM[base: _596, offset: 12], MEM[base: _596, offset: 12]
# ../../src/display/ui/default/bganim/AnimSteam.cpp:135:         const float y = wp.y0 - rise;
	add.n	a7, a9, a11	# tmp446,, tmp445
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	wfr	f3, a8	#,
	sub.s	f0, f0, f3	# _166, MEM[base: _596, offset: 12],
# ../../src/display/ui/default/bganim/AnimSteam.cpp:135:         const float y = wp.y0 - rise;
	lsi	f1, a7, 4	# MEM[(const struct Wisp &)&wisps][_34].y0, MEM[(const struct Wisp &)&wisps][_34].y0
	lsi	f3, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:134:         const float heightFrac = fminf(1.0f, rise / maxHeight);
	s32i.n	a10, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:135:         const float y = wp.y0 - rise;
	sub.s	f1, f1, f3	#, MEM[(const struct Wisp &)&wisps][_34].y0,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	ssi	f0, sp, 64	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:135:         const float y = wp.y0 - rise;
	ssi	f1, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	call8	_ZN6bganim9cosTableFEv		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	l32r	a8, .LC36	#,
	lsi	f0, sp, 64	#,
	wfr	f4, a8	#,
	mul.s	f0, f0, f4	# tmp451, _166,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:138:         const float x = wp.x0 + swayAmp * fastSinRad(wp.swayFreq1 * tMs + wp.swayPhase1 + b.seed) +
	lsi	f1, a7, 8	# MEM[(const struct Wisp &)&wisps][_34].swayPhase1, MEM[(const struct Wisp &)&wisps][_34].swayPhase1
	lsi	f4, a7, 16	# MEM[(const struct Wisp &)&wisps][_34].swayFreq1, MEM[(const struct Wisp &)&wisps][_34].swayFreq1
	lsi	f5, sp, 36	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	trunc.s	a8, f0, 0	# tmp453, tmp451
# ../../src/display/ui/default/bganim/AnimSteam.cpp:138:         const float x = wp.x0 + swayAmp * fastSinRad(wp.swayFreq1 * tMs + wp.swayPhase1 + b.seed) +
	madd.s	f1, f4, f5	# tmp487, MEM[(const struct Wisp &)&wisps][_34].swayFreq1,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	extui	a8, a8, 0, 8	# tmp454, tmp453,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	slli	a8, a8, 2	# tmp455, tmp454,
	add.n	a10, a10, a8	# tmp456,, tmp455
# ../../src/display/ui/default/bganim/AnimSteam.cpp:137:         const float swayAmp = (5.0f + 22.0f * heightFrac) * swirl;
	l32r	a8, .LC42	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:136:         const float R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed));
	l32r	a9, .LC40	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:137:         const float swayAmp = (5.0f + 22.0f * heightFrac) * swirl;
	wfr	f5, a8	# tmp464,
	l32r	a8, .LC41	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:136:         const float R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed));
	lsi	f6, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:137:         const float swayAmp = (5.0f + 22.0f * heightFrac) * swirl;
	wfr	f4, a8	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:136:         const float R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed));
	l32r	a8, .LC38	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:138:         const float x = wp.x0 + swayAmp * fastSinRad(wp.swayFreq1 * tMs + wp.swayPhase1 + b.seed) +
	lsi	f3, a2, 12	# MEM[base: _596, offset: 12], MEM[base: _596, offset: 12]
# ../../src/display/ui/default/bganim/AnimSteam.cpp:136:         const float R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed));
	wfr	f0, a9	# tmp461,
	l32r	a9, .LC39	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:137:         const float swayAmp = (5.0f + 22.0f * heightFrac) * swirl;
	madd.s	f5, f6, f4	# tmp464,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:136:         const float R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed));
	wfr	f4, a8	#,
	l32r	a8, .LC37	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:136:         const float R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed));
	wfr	f7, a9	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:138:         const float x = wp.x0 + swayAmp * fastSinRad(wp.swayFreq1 * tMs + wp.swayPhase1 + b.seed) +
	add.s	f1, f1, f3	# tmp490, tmp487, MEM[base: _596, offset: 12]
# ../../src/display/ui/default/bganim/AnimSteam.cpp:136:         const float R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed));
	lsi	f3, a10, 0	# *_173, *_173
# ../../src/display/ui/default/bganim/AnimSteam.cpp:136:         const float R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed));
	madd.s	f0, f6, f7	# tmp461,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:136:         const float R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed));
	wfr	f6, a8	#,
	madd.s	f4, f3, f6	#, *_173,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	l32r	a8, .LC35	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:137:         const float swayAmp = (5.0f + 22.0f * heightFrac) * swirl;
	lsi	f6, sp, 52	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:136:         const float R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed));
	mov.s	f3, f4	# tmp457,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	wfr	f4, a8	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:136:         const float R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed));
	mul.s	f3, f3, f0	# R, tmp457, tmp461
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	sub.s	f1, f1, f4	# _157, tmp490,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:137:         const float swayAmp = (5.0f + 22.0f * heightFrac) * swirl;
	mul.s	f5, f5, f6	#, tmp464,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:138:         const float x = wp.x0 + swayAmp * fastSinRad(wp.swayFreq1 * tMs + wp.swayPhase1 + b.seed) +
	lsi	f4, a7, 0	# MEM[(const struct Wisp &)&wisps][_34].x0, _44
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	ssi	f3, sp, 72	#,
	ssi	f1, sp, 64	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:137:         const float swayAmp = (5.0f + 22.0f * heightFrac) * swirl;
	ssi	f5, sp, 28	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	ssi	f4, sp, 68	#,
	call8	_ZN6bganim9cosTableFEv		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	l32r	a8, .LC36	#,
	lsi	f1, sp, 64	#,
	wfr	f7, a8	#,
	mul.s	f1, f1, f7	# tmp493, _157,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:139:                         swayAmp * 0.35f * fastSinRad(wp.swayFreq2 * tMs + wp.swayPhase2 + b.seed * 1.7f);
	lsi	f6, a7, 20	# MEM[(const struct Wisp &)&wisps][_34].swayFreq2, MEM[(const struct Wisp &)&wisps][_34].swayFreq2
	lsi	f0, a7, 12	# MEM[(const struct Wisp &)&wisps][_34].swayPhase2, MEM[(const struct Wisp &)&wisps][_34].swayPhase2
	lsi	f7, sp, 36	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	trunc.s	a8, f1, 0	# tmp495, tmp493
# ../../src/display/ui/default/bganim/AnimSteam.cpp:139:                         swayAmp * 0.35f * fastSinRad(wp.swayFreq2 * tMs + wp.swayPhase2 + b.seed * 1.7f);
	madd.s	f0, f7, f6	# tmp514,, MEM[(const struct Wisp &)&wisps][_34].swayFreq2
# ../../src/display/ui/default/bganim/AnimSteam.cpp:139:                         swayAmp * 0.35f * fastSinRad(wp.swayFreq2 * tMs + wp.swayPhase2 + b.seed * 1.7f);
	l32r	a9, .LC29	#,
	lsi	f5, a2, 12	# MEM[base: _596, offset: 12], MEM[base: _596, offset: 12]
	wfr	f8, a9	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	extui	a8, a8, 0, 8	# tmp496, tmp495,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:139:                         swayAmp * 0.35f * fastSinRad(wp.swayFreq2 * tMs + wp.swayPhase2 + b.seed * 1.7f);
	madd.s	f0, f5, f8	# tmp517, MEM[base: _596, offset: 12],
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	slli	a8, a8, 2	# tmp497, tmp496,
	add.n	a10, a10, a8	# tmp498,, tmp497
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	l32r	a8, .LC35	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:138:         const float x = wp.x0 + swayAmp * fastSinRad(wp.swayFreq1 * tMs + wp.swayPhase1 + b.seed) +
	lsi	f1, a10, 0	# *_164, *_164
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	wfr	f5, a8	#,
	sub.s	f0, f0, f5	# _148, tmp517,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:138:         const float x = wp.x0 + swayAmp * fastSinRad(wp.swayFreq1 * tMs + wp.swayPhase1 + b.seed) +
	lsi	f4, sp, 68	#,
	lsi	f5, sp, 28	# %sfp,
	madd.s	f4, f5, f1	# _53,, *_164
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	ssi	f0, sp, 64	#,
	ssi	f4, sp, 68	#,
	call8	_ZN6bganim9cosTableFEv		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	l32r	a8, .LC44	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	lsi	f2, sp, 76	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	wfr	f7, a8	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	l32r	a8, .LC34	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	lsi	f6, sp, 56	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	wfr	f5, a8	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	l32r	a8, .LC36	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	mul.s	f1, f6, f7	# tmp530,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	sub.s	f7, f5, f2	# tmp535,, tmp438
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	wfr	f5, a8	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	l32r	a8, .LC34	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	lsi	f0, sp, 64	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	wfr	f6, a8	# tmp538,
	l32r	a8, .LC37	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	mul.s	f0, f0, f5	# tmp523, _148,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	wfr	f8, a8	#,
	lsi	f5, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	l32r	a8, .LC45	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	msub.s	f6, f5, f8	# tmp538,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	wfr	f5, a8	#,
	mul.s	f1, f1, f5	# tmp532, tmp530,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:139:                         swayAmp * 0.35f * fastSinRad(wp.swayFreq2 * tMs + wp.swayPhase2 + b.seed * 1.7f);
	l32r	a8, .LC43	#,
	lsi	f8, sp, 28	# %sfp,
	wfr	f9, a8	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	trunc.s	a8, f0, 0	# tmp525, tmp523
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	mul.s	f0, f1, f2	# tmp534, tmp532, tmp438
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	extui	a8, a8, 0, 8	# tmp526, tmp525,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	slli	a8, a8, 2	# tmp527, tmp526,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:139:                         swayAmp * 0.35f * fastSinRad(wp.swayFreq2 * tMs + wp.swayPhase2 + b.seed * 1.7f);
	mul.s	f5, f8, f9	# tmp521,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	mul.s	f0, f0, f7	# tmp537, tmp534, tmp535
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	add.n	a10, a10, a8	# tmp528,, tmp527
# ../../src/display/ui/default/bganim/AnimSteam.cpp:139:                         swayAmp * 0.35f * fastSinRad(wp.swayFreq2 * tMs + wp.swayPhase2 + b.seed * 1.7f);
	lsi	f1, a10, 0	# *_155, *_155
	lsi	f4, sp, 68	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:140:         float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
	mul.s	f0, f0, f6	# alpha, tmp537, tmp538
# ../../src/display/ui/default/bganim/AnimSteam.cpp:139:                         swayAmp * 0.35f * fastSinRad(wp.swayFreq2 * tMs + wp.swayPhase2 + b.seed * 1.7f);
	madd.s	f4, f5, f1	# _53, tmp521, *_155
# ../../src/display/ui/default/bganim/AnimSteam.cpp:141:         if (alpha > 0.4f) {
	l32r	a8, .LC46	#,
	movi	a11, 0x66	# _612,
	wfr	f2, a8	#,
	olt.s	b0, f2, f0	#,, alpha
# ../../src/display/ui/default/bganim/AnimSteam.cpp:139:                         swayAmp * 0.35f * fastSinRad(wp.swayFreq2 * tMs + wp.swayPhase2 + b.seed * 1.7f);
	mov.s	f1, f4	# x, _53
# ../../src/display/ui/default/bganim/AnimSteam.cpp:141:         if (alpha > 0.4f) {
	lsi	f3, sp, 72	#,
	bt	b0, .L15	#,
	l32r	a8, .LC47	#,
	wfr	f2, a8	#,
	mul.s	f0, f0, f2	# tmp542, alpha,
	utrunc.s	a8, f0, 0	# tmp545, tmp542
	extui	a11, a8, 0, 8	# _612, tmp545
.L15:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:147:         d.xi = static_cast<int>(x >= 0.0f ? x + 0.5f : x - 0.5f);
	lsi	f0, sp, 32	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:144:         d.a8 = static_cast<uint8_t>(alpha * 255.0f);
	s8i	a11, a6, 16	# MEM[base: _586, offset: 16], _612
# ../../src/display/ui/default/bganim/AnimSteam.cpp:147:         d.xi = static_cast<int>(x >= 0.0f ? x + 0.5f : x - 0.5f);
	ole.s	b0, f0, f1	#,, x
	bf	b0, .L37	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:147:         d.xi = static_cast<int>(x >= 0.0f ? x + 0.5f : x - 0.5f);
	lsi	f2, sp, 24	# %sfp,
	add.s	f1, f1, f2	# tmp547, x,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:147:         d.xi = static_cast<int>(x >= 0.0f ? x + 0.5f : x - 0.5f);
	trunc.s	a8, f1, 0	# iftmp$16_92, tmp547
	j	.L18		#
.L37:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:147:         d.xi = static_cast<int>(x >= 0.0f ? x + 0.5f : x - 0.5f);
	lsi	f4, sp, 24	# %sfp,
	sub.s	f1, f1, f4	# tmp549, x,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:147:         d.xi = static_cast<int>(x >= 0.0f ? x + 0.5f : x - 0.5f);
	trunc.s	a8, f1, 0	# iftmp$16_92, tmp549
.L18:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:148:         d.yi = static_cast<int>(y >= 0.0f ? y + 0.5f : y - 0.5f);
	lsi	f5, sp, 32	# %sfp,
	lsi	f6, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:147:         d.xi = static_cast<int>(x >= 0.0f ? x + 0.5f : x - 0.5f);
	s32i.n	a8, a6, 0	# MEM[base: _586, offset: 0], iftmp$16_92
# ../../src/display/ui/default/bganim/AnimSteam.cpp:148:         d.yi = static_cast<int>(y >= 0.0f ? y + 0.5f : y - 0.5f);
	ole.s	b0, f5, f6	#,,
	bf	b0, .L38	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:148:         d.yi = static_cast<int>(y >= 0.0f ? y + 0.5f : y - 0.5f);
	lsi	f7, sp, 24	# %sfp,
	add.s	f0, f6, f7	# tmp552,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:148:         d.yi = static_cast<int>(y >= 0.0f ? y + 0.5f : y - 0.5f);
	trunc.s	a8, f0, 0	# iftmp$17_93, tmp552
	j	.L21		#
.L38:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:148:         d.yi = static_cast<int>(y >= 0.0f ? y + 0.5f : y - 0.5f);
	lsi	f8, sp, 16	# %sfp,
	lsi	f9, sp, 24	# %sfp,
	sub.s	f0, f8, f9	# tmp554,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:148:         d.yi = static_cast<int>(y >= 0.0f ? y + 0.5f : y - 0.5f);
	trunc.s	a8, f0, 0	# iftmp$17_93, tmp554
.L21:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:149:         d.Ri = static_cast<int>(R + 0.5f);
	lsi	f1, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:148:         d.yi = static_cast<int>(y >= 0.0f ? y + 0.5f : y - 0.5f);
	s32i.n	a8, a6, 4	# MEM[base: _586, offset: 4], iftmp$17_93
# ../../src/display/ui/default/bganim/AnimSteam.cpp:149:         d.Ri = static_cast<int>(R + 0.5f);
	add.s	f0, f3, f1	# tmp556, R,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:150:         d.scaleQ = static_cast<int32_t>(63.0f * 65536.0f / (R * R) + 0.5f);
	mul.s	f3, f3, f3	#, R, R
	l32r	a10, .LC48	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:149:         d.Ri = static_cast<int>(R + 0.5f);
	trunc.s	a8, f0, 0	# tmp558, tmp556
# ../../src/display/ui/default/bganim/AnimSteam.cpp:150:         d.scaleQ = static_cast<int32_t>(63.0f * 65536.0f / (R * R) + 0.5f);
	rfr	a11, f3	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:149:         d.Ri = static_cast<int>(R + 0.5f);
	s32i.n	a8, a6, 8	# MEM[base: _586, offset: 8], tmp558
# ../../src/display/ui/default/bganim/AnimSteam.cpp:150:         d.scaleQ = static_cast<int32_t>(63.0f * 65536.0f / (R * R) + 0.5f);
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:153:         themeRGB(200 + static_cast<int>(heightFrac * 55.0f), c);
	l32r	a8, .LC49	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:150:         d.scaleQ = static_cast<int32_t>(63.0f * 65536.0f / (R * R) + 0.5f);
	lsi	f3, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:153:         themeRGB(200 + static_cast<int>(heightFrac * 55.0f), c);
	lsi	f4, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:150:         d.scaleQ = static_cast<int32_t>(63.0f * 65536.0f / (R * R) + 0.5f);
	wfr	f2, a10	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:153:         themeRGB(200 + static_cast<int>(heightFrac * 55.0f), c);
	wfr	f5, a8	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:150:         d.scaleQ = static_cast<int32_t>(63.0f * 65536.0f / (R * R) + 0.5f);
	add.s	f1, f2, f3	# tmp561,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:153:         themeRGB(200 + static_cast<int>(heightFrac * 55.0f), c);
	mul.s	f0, f4, f5	# tmp564,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:153:         themeRGB(200 + static_cast<int>(heightFrac * 55.0f), c);
	movi	a9, 0xc8	#,
	mov.n	a11, sp	#,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:150:         d.scaleQ = static_cast<int32_t>(63.0f * 65536.0f / (R * R) + 0.5f);
	trunc.s	a8, f1, 0	# tmp563, tmp561
# ../../src/display/ui/default/bganim/AnimSteam.cpp:153:         themeRGB(200 + static_cast<int>(heightFrac * 55.0f), c);
	trunc.s	a10, f0, 0	# tmp566, tmp564
# ../../src/display/ui/default/bganim/AnimSteam.cpp:150:         d.scaleQ = static_cast<int32_t>(63.0f * 65536.0f / (R * R) + 0.5f);
	s32i.n	a8, a6, 12	# MEM[base: _586, offset: 12], tmp563
# ../../src/display/ui/default/bganim/AnimSteam.cpp:153:         themeRGB(200 + static_cast<int>(heightFrac * 55.0f), c);
	add.n	a10, a10, a9	#, tmp566,
	call8	_ZN6bganim8themeRGBEiPh		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:154:         d.r = c[0];
	l8ui	a8, sp, 0	# c,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:157:         d.visible = d.a8 > 0 && y > -30.0f;
	l8ui	a10, a6, 16	# MEM[base: _586, offset: 16], MEM[base: _586, offset: 16]
# ../../src/display/ui/default/bganim/AnimSteam.cpp:154:         d.r = c[0];
	s8i	a8, a6, 17	# MEM[base: _586, offset: 17], c
# ../../src/display/ui/default/bganim/AnimSteam.cpp:155:         d.g = c[1];
	l8ui	a8, sp, 1	# c,
	s8i	a8, a6, 18	# MEM[base: _586, offset: 18], c
# ../../src/display/ui/default/bganim/AnimSteam.cpp:156:         d.b = c[2];
	l8ui	a8, sp, 2	# c,
	s8i	a8, a6, 19	# MEM[base: _586, offset: 19], c
# ../../src/display/ui/default/bganim/AnimSteam.cpp:157:         d.visible = d.a8 > 0 && y > -30.0f;
	mov.n	a8, a4	# iftmp$18_94, iftmp$18_94
	beqz.n	a10, .L22	# MEM[base: _586, offset: 16],
# ../../src/display/ui/default/bganim/AnimSteam.cpp:157:         d.visible = d.a8 > 0 && y > -30.0f;
	l32r	a8, .LC50	#,
	lsi	f1, sp, 16	# %sfp,
	wfr	f0, a8	#,
	olt.s	b0, f0, f1	#,,
	movi.n	a8, 1	# tmp576,
	movf	a8, a4, b0	# tmp575, iftmp$18_94,
	extui	a8, a8, 0, 8	# iftmp$18_94, tmp575
.L22:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:123:     for (int i = 0; i < g_active; i++) {
	l32i.n	a10, a5, 0	# g_active, g_active
# ../../src/display/ui/default/bganim/AnimSteam.cpp:157:         d.visible = d.a8 > 0 && y > -30.0f;
	s8i	a8, a6, 20	# MEM[base: _586, offset: 20], iftmp$18_94
# ../../src/display/ui/default/bganim/AnimSteam.cpp:123:     for (int i = 0; i < g_active; i++) {
	addi.n	a3, a3, 1	# i, i,
	addi	a2, a2, 16	# ivtmp$39, ivtmp$39,
	addi	a6, a6, 24	# ivtmp$40, ivtmp$40,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:123:     for (int i = 0; i < g_active; i++) {
	blt	a3, a10, .L23	# i, g_active,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:159: }
	retw.n
	.size	_ZN12_GLOBAL__N_15frameEjiiPKh, .-_ZN12_GLOBAL__N_15frameEjiiPKh
	.section	.text._ZN12_GLOBAL__N_14initEii,"ax",@progbits
	.literal_position
	.literal .LC53, _ZN12_GLOBAL__N_18alphaLUTE
	.literal .LC54, _ZN12_GLOBAL__N_15bgLUTE
	.literal .LC55, _ZN12_GLOBAL__N_13g_hE
	.literal .LC56, 0x427c0000
	.literal .LC57, 0x3f800000
	.literal .LC58, 0x00000000
	.literal .LC59, 0x3fcccccd
	.literal .LC60, 0x437f0000
	.literal .LC61, _ZN12_GLOBAL__N_112lastThemeGenE
	.align	4
	.type	_ZN12_GLOBAL__N_14initEii, @function
_ZN12_GLOBAL__N_14initEii:
	entry	sp, 32	#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:89:     if (alphaLUT == nullptr) {
	l32r	a4, .LC53	#, tmp66
# ../../src/display/ui/default/bganim/AnimSteam.cpp:104:     return true;
	movi.n	a2, 1	# <retval>,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:89:     if (alphaLUT == nullptr) {
	l32i.n	a5, a4, 0	# alphaLUT, alphaLUT
	bnez.n	a5, .L40	# alphaLUT,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:90:         alphaLUT = static_cast<uint8_t *>(alloc(64));
	movi.n	a10, 0x40	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:90:         alphaLUT = static_cast<uint8_t *>(alloc(64));
	s32i.n	a10, a4, 0	# alphaLUT,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:91:         bgLUT = static_cast<uint16_t *>(alloc(h * sizeof(uint16_t)));
	ssl	a2	#
	sll	a10, a3	#, h
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:92:         if (alphaLUT == nullptr || bgLUT == nullptr) {
	l32i.n	a4, a4, 0	# alphaLUT, alphaLUT$27_6
# ../../src/display/ui/default/bganim/AnimSteam.cpp:91:         bgLUT = static_cast<uint16_t *>(alloc(h * sizeof(uint16_t)));
	l32r	a6, .LC54	#, tmp70
# ../../src/display/ui/default/bganim/AnimSteam.cpp:92:         if (alphaLUT == nullptr || bgLUT == nullptr) {
	moveqz	a5, a2, a4	# tmp73, <retval>, alphaLUT$27_6
# ../../src/display/ui/default/bganim/AnimSteam.cpp:91:         bgLUT = static_cast<uint16_t *>(alloc(h * sizeof(uint16_t)));
	s32i.n	a10, a6, 0	# bgLUT, _5
# ../../src/display/ui/default/bganim/AnimSteam.cpp:92:         if (alphaLUT == nullptr || bgLUT == nullptr) {
	extui	a5, a5, 0, 8	# tmp76, tmp73
	bnez.n	a5, .L43	# tmp76,
	movnez	a2, a5, a10	# tmp81, tmp76, _5
	bnez.n	a2, .L43	# tmp81,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:95:         g_h = h;
	l32r	a8, .LC55	#, tmp82
	l32r	a7, .LC58	#, tmp97
	l32r	a6, .LC59	#, tmp98
# ../../src/display/ui/default/bganim/AnimSteam.cpp:97:             const float norm = sqrtf(i / 63.0f);
	l32r	a5, .LC56	#, tmp99
# ../../src/display/ui/default/bganim/AnimSteam.cpp:95:         g_h = h;
	s32i.n	a3, a8, 0	# g_h, h
.L41:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:97:             const float norm = sqrtf(i / 63.0f);
	float.s	f0, a2, 0	#, i
	mov.n	a11, a5	#, tmp99
	rfr	a10, f0	#,
	call8	__divsf3		#
	call8	sqrtf		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:98:             const float a = powf(fmaxf(0.0f, 1.0f - norm), 1.6f);
	l32r	a3, .LC57	#,
	wfr	f2, a10	#,
	wfr	f1, a3	#,
	sub.s	f2, f1, f2	#,,
	mov.n	a10, a7	#, tmp97
	rfr	a11, f2	#,
	call8	fmaxf		#
	mov.n	a11, a6	#, tmp98
	call8	powf		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:99:             alphaLUT[i] = static_cast<uint8_t>(a * 255.0f);
	l32r	a3, .LC60	#,
	wfr	f1, a10	#,
	wfr	f2, a3	#,
	mul.s	f0, f1, f2	# tmp90,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:99:             alphaLUT[i] = static_cast<uint8_t>(a * 255.0f);
	add.n	a9, a4, a2	# tmp89, alphaLUT$27_6, i
# ../../src/display/ui/default/bganim/AnimSteam.cpp:96:         for (int i = 0; i < 64; i++) {
	addi.n	a2, a2, 1	# i, i,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:99:             alphaLUT[i] = static_cast<uint8_t>(a * 255.0f);
	utrunc.s	a8, f0, 0	# tmp92, tmp90
	s8i	a8, a9, 0	# MEM[base: _44, offset: 0B], tmp92
# ../../src/display/ui/default/bganim/AnimSteam.cpp:96:         for (int i = 0; i < 64; i++) {
	bnei	a2, 64, .L41	# i,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:101:         rebuildBg();
	call8	_ZN12_GLOBAL__N_19rebuildBgEv		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:102:         lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:102:         lastThemeGen = themeGen();
	l32r	a3, .LC61	#, tmp93
	movi.n	a2, 1	# <retval>,
	s32i.n	a10, a3, 0	# lastThemeGen,
	j	.L40		#
.L43:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:93:             return false;
	movi.n	a2, 0	# <retval>,
.L40:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:105: }
	retw.n
	.size	_ZN12_GLOBAL__N_14initEii, .-_ZN12_GLOBAL__N_14initEii
	.section	.text._ZN12_GLOBAL__N_14bandEPtiiijPKh,"ax",@progbits
	.literal_position
	.literal .LC62, _ZN12_GLOBAL__N_18g_activeE
	.literal .LC63, _ZN12_GLOBAL__N_15bgLUTE
	.literal .LC65, _ZN12_GLOBAL__N_18alphaLUTE
	.literal .LC66, _ZN12_GLOBAL__N_15drawsE
	.align	4
	.type	_ZN12_GLOBAL__N_14bandEPtiiijPKh, @function
_ZN12_GLOBAL__N_14bandEPtiiijPKh:
	entry	sp, 96	#
# ../../src/display/ui/default/bganim/AnimSteam.cpp:161: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
	s32i.n	a2, sp, 52	# %sfp, dst
	s32i.n	a3, sp, 36	# %sfp, y0
	s32i.n	a5, sp, 44	# %sfp, w
# ../../src/display/ui/default/bganim/AnimSteam.cpp:162:     for (int r = 0; r < rows; r++) {
	bgei	a4, 1, .L46	# rows,,
.L53:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:179:     for (int i = 0; i < g_active; i++) {
	l32r	a2, .LC62	#, tmp176
	l32i.n	a3, a2, 0	# g_active, g_active$5_125
	bgei	a3, 1, .L47	# g_active$5_125,,
	j	.L45		#
.L46:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:171:         const int pairs = w >> 1;
	l32i.n	a2, sp, 44	# %sfp,
	l32i.n	a9, sp, 36	# %sfp,
	srai	a10, a2, 1	# pairs,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:176:             row[w - 1] = c;
	l32i.n	a3, sp, 44	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:163:         const uint16_t c = bgLUT[y0 + r];
	l32r	a2, .LC63	#, tmp177
# ../../src/display/ui/default/bganim/AnimSteam.cpp:176:             row[w - 1] = c;
	slli	a12, a3, 1	# tmp178,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:163:         const uint16_t c = bgLUT[y0 + r];
	l32i.n	a5, a2, 0	# bgLUT, bgLUT$0_1
	slli	a6, a9, 1	# tmp180,,
	add.n	a2, a4, a9	# tmp182, rows,
	slli	a3, a10, 2	# tmp181, pairs,
	l32i.n	a11, sp, 44	# %sfp,
	l32i.n	a9, sp, 52	# %sfp,
	mov.n	a8, a3	# tmp186, tmp181
# ../../src/display/ui/default/bganim/AnimSteam.cpp:176:             row[w - 1] = c;
	addi	a12, a12, -2	# _17, tmp178,
	slli	a2, a2, 1	# tmp183, tmp182,
	extui	a13, a11, 0, 1	# _167,,
	add.n	a3, a9, a3	# ivtmp$87,, tmp181
	add.n	a6, a5, a6	# ivtmp$84, bgLUT$0_1, tmp180
	addi.n	a11, a12, 2	# _198, _17,
	add.n	a9, a5, a2	# _209, bgLUT$0_1, tmp183
	neg	a8, a8	# tmp187, tmp186
.L54:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:163:         const uint16_t c = bgLUT[y0 + r];
	l16ui	a7, a6, 0	# MEM[base: _203, offset: 0B], c
	add.n	a2, a3, a8	# ivtmp$81, ivtmp$87, tmp187
# ../../src/display/ui/default/bganim/AnimSteam.cpp:169:         const uint32_t c2 = (static_cast<uint32_t>(c) << 16) | c;
	slli	a5, a7, 16	# tmp188, c,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:169:         const uint32_t c2 = (static_cast<uint32_t>(c) << 16) | c;
	or	a5, a5, a7	# c2, tmp188, c
# ../../src/display/ui/default/bganim/AnimSteam.cpp:172:         for (int x = 0; x < pairs; x++) {
	bgei	a10, 1, .L52	# pairs,,
.L51:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:175:         if (w & 1) {
	bnez.n	a13, .L49	# _167,
	j	.L50		#
.L52:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:173:             row32[x] = c2;
	s32i.n	a5, a2, 0	# MEM[base: _185, offset: 0B], c2
	addi.n	a2, a2, 4	# ivtmp$81, ivtmp$81,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:172:         for (int x = 0; x < pairs; x++) {
	bne	a2, a3, .L52	# ivtmp$81, ivtmp$87,
	j	.L51		#
.L49:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:176:             row[w - 1] = c;
	add.n	a2, a12, a3	# tmp189, _17, ivtmp$87
	add.n	a2, a2, a8	# tmp190, tmp189, tmp187
	s16i	a7, a2, 0	# MEM[base: _214, offset: 0B], c
.L50:
	addi.n	a6, a6, 2	# ivtmp$84, ivtmp$84,
	add.n	a3, a3, a11	# ivtmp$87, ivtmp$87, _198
# ../../src/display/ui/default/bganim/AnimSteam.cpp:162:     for (int r = 0; r < rows; r++) {
	bne	a6, a9, .L54	# ivtmp$84, _209,
	j	.L53		#
.L47:
	slli	a2, a3, 1	# tmp193, g_active$5_125,
	add.n	a2, a2, a3	# tmp194, tmp193, g_active$5_125
	l32r	a15, .LC66	#, ivtmp$76
	slli	a2, a2, 3	# tmp195, tmp194,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:201:                 const uint8_t a = (static_cast<uint16_t>(alphaLUT[idx]) * d.a8) >> 8;
	l32r	a3, .LC65	#, tmp191
	add.n	a2, a2, a15	#, tmp195, ivtmp$76
# ../../src/display/ui/default/bganim/AnimSteam.cpp:181:         if (!d.visible || d.yi + d.Ri < y0 || d.yi - d.Ri >= y0 + rows) {
	l32i.n	a11, sp, 36	# %sfp,
	s32i.n	a2, sp, 40	# %sfp,
	l32i.n	a2, sp, 44	# %sfp,
	add.n	a4, a4, a11	#, rows,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:201:                 const uint8_t a = (static_cast<uint16_t>(alphaLUT[idx]) * d.a8) >> 8;
	l32i.n	a7, a3, 0	# alphaLUT, alphaLUT$7_73
	slli	a2, a2, 1	#,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:185:         const int yy1 = d.yi + d.Ri < y0 + rows - 1 ? d.yi + d.Ri : y0 + rows - 1;
	addi.n	a3, a4, -1	#,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:181:         if (!d.visible || d.yi + d.Ri < y0 || d.yi - d.Ri >= y0 + rows) {
	s32i.n	a4, sp, 48	# %sfp,
	s32i.n	a2, sp, 28	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:185:         const int yy1 = d.yi + d.Ri < y0 + rows - 1 ? d.yi + d.Ri : y0 + rows - 1;
	s32i.n	a3, sp, 56	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:198:                 if (idx >= 64) {
	movi.n	a6, 0x3f	# tmp259,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:87:     return static_cast<uint16_t>((dr << 11) | (dg << 5) | db);
	mov.n	a10, a7	# alphaLUT$7_73, alphaLUT$7_73
.L57:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:181:         if (!d.visible || d.yi + d.Ri < y0 || d.yi - d.Ri >= y0 + rows) {
	l8ui	a2, a15, 20	# MEM[base: _164, offset: 20], MEM[base: _164, offset: 20]
	bnez.n	a2, .L55	# MEM[base: _164, offset: 20],
.L58:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:179:     for (int i = 0; i < g_active; i++) {
	l32i.n	a4, sp, 40	# %sfp,
	addi	a15, a15, 24	# ivtmp$76, ivtmp$76,
	bne	a15, a4, .L57	# ivtmp$76,,
	j	.L45		#
.L55:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:181:         if (!d.visible || d.yi + d.Ri < y0 || d.yi - d.Ri >= y0 + rows) {
	l32i.n	a2, a15, 4	# MEM[base: _164, offset: 4], _45
# ../../src/display/ui/default/bganim/AnimSteam.cpp:181:         if (!d.visible || d.yi + d.Ri < y0 || d.yi - d.Ri >= y0 + rows) {
	l32i.n	a4, a15, 8	# MEM[base: _164, offset: 8], _46
# ../../src/display/ui/default/bganim/AnimSteam.cpp:181:         if (!d.visible || d.yi + d.Ri < y0 || d.yi - d.Ri >= y0 + rows) {
	l32i.n	a9, sp, 36	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:181:         if (!d.visible || d.yi + d.Ri < y0 || d.yi - d.Ri >= y0 + rows) {
	add.n	a3, a2, a4	# _47, _45, _46
# ../../src/display/ui/default/bganim/AnimSteam.cpp:181:         if (!d.visible || d.yi + d.Ri < y0 || d.yi - d.Ri >= y0 + rows) {
	blt	a3, a9, .L58	# _47,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:181:         if (!d.visible || d.yi + d.Ri < y0 || d.yi - d.Ri >= y0 + rows) {
	l32i.n	a11, sp, 48	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:181:         if (!d.visible || d.yi + d.Ri < y0 || d.yi - d.Ri >= y0 + rows) {
	sub	a8, a2, a4	# _48, _45, _46
# ../../src/display/ui/default/bganim/AnimSteam.cpp:181:         if (!d.visible || d.yi + d.Ri < y0 || d.yi - d.Ri >= y0 + rows) {
	bge	a8, a11, .L58	# _48,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:186:         const int xx0 = d.xi - d.Ri > 0 ? d.xi - d.Ri : 0;
	l32i.n	a5, a15, 0	# MEM[base: _164, offset: 0],
# ../../src/display/ui/default/bganim/AnimSteam.cpp:187:         const int xx1 = d.xi + d.Ri < w - 1 ? d.xi + d.Ri : w - 1;
	l32i.n	a11, sp, 44	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:186:         const int xx0 = d.xi - d.Ri > 0 ? d.xi - d.Ri : 0;
	sub	a9, a5, a4	#,, _46
# ../../src/display/ui/default/bganim/AnimSteam.cpp:186:         const int xx0 = d.xi - d.Ri > 0 ? d.xi - d.Ri : 0;
	s32i.n	a5, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:187:         const int xx1 = d.xi + d.Ri < w - 1 ? d.xi + d.Ri : w - 1;
	addi.n	a5, a11, -1	# tmp199,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:184:         const int yy0 = d.yi - d.Ri > y0 ? d.yi - d.Ri : y0;
	l32i.n	a11, sp, 36	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:186:         const int xx0 = d.xi - d.Ri > 0 ? d.xi - d.Ri : 0;
	s32i.n	a9, sp, 12	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:187:         const int xx1 = d.xi + d.Ri < w - 1 ? d.xi + d.Ri : w - 1;
	l32i.n	a9, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:184:         const int yy0 = d.yi - d.Ri > y0 ? d.yi - d.Ri : y0;
	max	a8, a11, a8	# yy0,, _48
# ../../src/display/ui/default/bganim/AnimSteam.cpp:186:         const int xx0 = d.xi - d.Ri > 0 ? d.xi - d.Ri : 0;
	l32i.n	a11, sp, 12	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:187:         const int xx1 = d.xi + d.Ri < w - 1 ? d.xi + d.Ri : w - 1;
	add.n	a4, a4, a9	# tmp200, _46,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:186:         const int xx0 = d.xi - d.Ri > 0 ? d.xi - d.Ri : 0;
	movi.n	a7, 0	# tmp198,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:185:         const int yy1 = d.yi + d.Ri < y0 + rows - 1 ? d.yi + d.Ri : y0 + rows - 1;
	l32i.n	a9, sp, 56	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:186:         const int xx0 = d.xi - d.Ri > 0 ? d.xi - d.Ri : 0;
	max	a11, a11, a7	#,, tmp198
# ../../src/display/ui/default/bganim/AnimSteam.cpp:187:         const int xx1 = d.xi + d.Ri < w - 1 ? d.xi + d.Ri : w - 1;
	min	a4, a5, a4	#, tmp199, tmp200
# ../../src/display/ui/default/bganim/AnimSteam.cpp:185:         const int yy1 = d.yi + d.Ri < y0 + rows - 1 ? d.yi + d.Ri : y0 + rows - 1;
	min	a3, a9, a3	# yy1,, _47
# ../../src/display/ui/default/bganim/AnimSteam.cpp:186:         const int xx0 = d.xi - d.Ri > 0 ? d.xi - d.Ri : 0;
	s32i.n	a11, sp, 12	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:187:         const int xx1 = d.xi + d.Ri < w - 1 ? d.xi + d.Ri : w - 1;
	s32i.n	a4, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:188:         for (int yy = yy0; yy <= yy1; yy++) {
	blt	a3, a8, .L58	# yy1, yy0,
	l32i.n	a4, sp, 36	# %sfp,
	l32i.n	a5, sp, 44	# %sfp,
	sub	a7, a8, a4	# tmp201, yy0,
	mull	a7, a7, a5	# tmp202, tmp201,
	movi.n	a9, 1	#,
	sub	a5, a9, a2	# tmp206,, _45
	add.n	a7, a7, a11	# tmp203, tmp202,
	l32i.n	a11, sp, 16	# %sfp,
	sub	a8, a8, a2	#, yy0, _45
	add.n	a5, a5, a3	#, tmp206, yy1
	l32i.n	a2, sp, 52	# %sfp,
	l32i.n	a3, sp, 20	# %sfp,
	ssl	a9	#
	sll	a7, a7	# tmp204, tmp203
	sub	a4, a9, a11	# tmp251,,
	add.n	a2, a2, a7	#,, tmp204
	add.n	a4, a4, a3	#, tmp251,
	s32i.n	a8, sp, 4	# %sfp,
	s32i.n	a2, sp, 8	# %sfp,
	s32i.n	a5, sp, 24	# %sfp,
	s32i.n	a4, sp, 32	# %sfp,
.L60:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:190:             const int dy2 = dy * dy;
	l32i.n	a4, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:192:             for (int xx = xx0; xx <= xx1; xx++) {
	l32i.n	a5, sp, 20	# %sfp,
	l32i.n	a9, sp, 12	# %sfp,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:190:             const int dy2 = dy * dy;
	mull	a12, a4, a4	# dy2,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:192:             for (int xx = xx0; xx <= xx1; xx++) {
	bge	a5, a9, .L59	#,,
.L62:
	l32i.n	a11, sp, 4	# %sfp,
	l32i.n	a2, sp, 8	# %sfp,
	l32i.n	a3, sp, 28	# %sfp,
	addi.n	a11, a11, 1	#,,
	add.n	a2, a2, a3	#,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:188:         for (int yy = yy0; yy <= yy1; yy++) {
	l32i.n	a4, sp, 24	# %sfp,
	s32i.n	a11, sp, 4	# %sfp,
	s32i.n	a2, sp, 8	# %sfp,
	bne	a4, a11, .L60	#,,
	j	.L58		#
.L59:
	l32i.n	a9, sp, 16	# %sfp,
	l32i.n	a5, sp, 12	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:87:     return static_cast<uint16_t>((dr << 11) | (dg << 5) | db);
	l32i.n	a11, sp, 32	# %sfp,
	sub	a7, a5, a9	# ivtmp$67,,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:197:                 const int32_t idx = ((dx * dx + dy2) * d.scaleQ) >> 16;
	l32i.n	a13, a15, 12	# MEM[base: _164, offset: 12], _70
	l32i.n	a9, sp, 8	# %sfp, ivtmp$68
# ../../src/display/ui/default/bganim/BgAnimCommon.h:87:     return static_cast<uint16_t>((dr << 11) | (dg << 5) | db);
	sub	a8, a11, a7	# tmp265,, ivtmp$67
	s32i.n	a12, sp, 0	# %sfp, dy2
	loop	a8, .L63_LEND	# tmp265,
.L63:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:197:                 const int32_t idx = ((dx * dx + dy2) * d.scaleQ) >> 16;
	mull	a2, a7, a7	# tmp209, ivtmp$67, ivtmp$67
# ../../src/display/ui/default/bganim/AnimSteam.cpp:197:                 const int32_t idx = ((dx * dx + dy2) * d.scaleQ) >> 16;
	l32i.n	a3, sp, 0	# %sfp,
	addi.n	a7, a7, 1	# ivtmp$67, ivtmp$67,
	add.n	a2, a2, a3	# tmp210, tmp209,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:197:                 const int32_t idx = ((dx * dx + dy2) * d.scaleQ) >> 16;
	mull	a2, a2, a13	# tmp211, tmp210, _70
# ../../src/display/ui/default/bganim/AnimSteam.cpp:197:                 const int32_t idx = ((dx * dx + dy2) * d.scaleQ) >> 16;
	srai	a2, a2, 16	# idx, tmp211,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:201:                 const uint8_t a = (static_cast<uint16_t>(alphaLUT[idx]) * d.a8) >> 8;
	add.n	a3, a10, a2	# tmp213, alphaLUT$7_73, idx
# ../../src/display/ui/default/bganim/AnimSteam.cpp:198:                 if (idx >= 64) {
	blt	a6, a2, .L61	# tmp259, idx,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:201:                 const uint8_t a = (static_cast<uint16_t>(alphaLUT[idx]) * d.a8) >> 8;
	l8ui	a2, a3, 0	# *_75, *_75
	l8ui	a3, a15, 16	# MEM[base: _164, offset: 16], MEM[base: _164, offset: 16]
	mul16u	a2, a2, a3	# tmp218, *_75, MEM[base: _164, offset: 16]
# ../../src/display/ui/default/bganim/AnimSteam.cpp:201:                 const uint8_t a = (static_cast<uint16_t>(alphaLUT[idx]) * d.a8) >> 8;
	srai	a2, a2, 8	# _81, tmp218,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:202:                 if (a == 0) {
	beqz.n	a2, .L61	# _81,
# ../../src/display/ui/default/bganim/AnimSteam.cpp:205:                 row[xx] = addScaled565(row[xx], d.r, d.g, d.b, a);
	l8ui	a12, a15, 17	# MEM[base: _164, offset: 17], MEM[base: _164, offset: 17]
	l8ui	a11, a15, 18	# MEM[base: _164, offset: 18], MEM[base: _164, offset: 18]
# ../../src/display/ui/default/bganim/AnimSteam.cpp:205:                 row[xx] = addScaled565(row[xx], d.r, d.g, d.b, a);
	l16ui	a5, a9, 0	# MEM[base: _153, offset: 0B], _87
# ../../src/display/ui/default/bganim/AnimSteam.cpp:205:                 row[xx] = addScaled565(row[xx], d.r, d.g, d.b, a);
	l8ui	a14, a15, 19	# MEM[base: _164, offset: 19], MEM[base: _164, offset: 19]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:78:     int dr = ((dst >> 11) & 0x1F) + ((r * a) >> 11);
	mull	a12, a12, a2	# tmp220, MEM[base: _164, offset: 17], _81
# ../../src/display/ui/default/bganim/BgAnimCommon.h:79:     int dg = ((dst >> 5) & 0x3F) + ((g * a) >> 10);
	mull	a11, a11, a2	# tmp225, MEM[base: _164, offset: 18], _81
# ../../src/display/ui/default/bganim/BgAnimCommon.h:78:     int dr = ((dst >> 11) & 0x1F) + ((r * a) >> 11);
	srli	a3, a5, 11	# tmp223, _87,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:79:     int dg = ((dst >> 5) & 0x3F) + ((g * a) >> 10);
	srai	a11, a11, 10	# tmp226, tmp225,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:79:     int dg = ((dst >> 5) & 0x3F) + ((g * a) >> 10);
	extui	a4, a5, 5, 6	# tmp228, _87,,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:80:     int db = (dst & 0x1F) + ((b * a) >> 11);
	mull	a2, a14, a2	# tmp230, MEM[base: _164, offset: 19], _81
# ../../src/display/ui/default/bganim/BgAnimCommon.h:78:     int dr = ((dst >> 11) & 0x1F) + ((r * a) >> 11);
	srai	a12, a12, 11	# tmp221, tmp220,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:79:     int dg = ((dst >> 5) & 0x3F) + ((g * a) >> 10);
	add.n	a4, a11, a4	# dg, tmp226, tmp228
# ../../src/display/ui/default/bganim/BgAnimCommon.h:78:     int dr = ((dst >> 11) & 0x1F) + ((r * a) >> 11);
	add.n	a3, a12, a3	# dr, tmp221, tmp223
# ../../src/display/ui/default/bganim/BgAnimCommon.h:87:     return static_cast<uint16_t>((dr << 11) | (dg << 5) | db);
	movi.n	a11, 0x1f	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:80:     int db = (dst & 0x1F) + ((b * a) >> 11);
	extui	a14, a5, 0, 5	# tmp233, _87,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:80:     int db = (dst & 0x1F) + ((b * a) >> 11);
	srai	a2, a2, 11	# tmp231, tmp230,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:87:     return static_cast<uint16_t>((dr << 11) | (dg << 5) | db);
	min	a5, a3, a11	# dr, dr,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:87:     return static_cast<uint16_t>((dr << 11) | (dg << 5) | db);
	min	a4, a4, a6	# dg, dg, tmp259
# ../../src/display/ui/default/bganim/BgAnimCommon.h:80:     int db = (dst & 0x1F) + ((b * a) >> 11);
	add.n	a2, a2, a14	# db, tmp231, tmp233
# ../../src/display/ui/default/bganim/BgAnimCommon.h:87:     return static_cast<uint16_t>((dr << 11) | (dg << 5) | db);
	slli	a5, a5, 11	# tmp236, dr,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:87:     return static_cast<uint16_t>((dr << 11) | (dg << 5) | db);
	slli	a4, a4, 5	# tmp239, dg,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:87:     return static_cast<uint16_t>((dr << 11) | (dg << 5) | db);
	or	a5, a5, a4	# tmp242, tmp236, tmp239
# ../../src/display/ui/default/bganim/BgAnimCommon.h:87:     return static_cast<uint16_t>((dr << 11) | (dg << 5) | db);
	min	a2, a2, a11	# db, db,
	or	a5, a5, a2	# tmp247, tmp242, db
# ../../src/display/ui/default/bganim/AnimSteam.cpp:205:                 row[xx] = addScaled565(row[xx], d.r, d.g, d.b, a);
	s16i	a5, a9, 0	# MEM[base: _153, offset: 0B], tmp247
.L61:
	addi.n	a9, a9, 2	# ivtmp$68, ivtmp$68,
	.L63_LEND:		#
	j	.L62		#
.L45:
# ../../src/display/ui/default/bganim/AnimSteam.cpp:209: }
	retw.n
	.size	_ZN12_GLOBAL__N_14bandEPtiiijPKh, .-_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.global	bg_anim_steam
	.section	.rodata.str1.4,"aMS",@progbits,1
	.align	4
.LC67:
	.string	"steam"
	.align	4
.LC68:
	.string	"Steam"
	.align	4
.LC69:
	.string	"speed"
	.align	4
.LC70:
	.string	"Rise speed"
	.align	4
.LC71:
	.string	"count"
	.align	4
.LC72:
	.string	"Wisps"
	.align	4
.LC73:
	.string	"swirl"
	.align	4
.LC74:
	.string	"Swirl"
	.align	4
.LC75:
	.string	"density"
	.align	4
.LC76:
	.string	"Density"
	.section	.rodata
	.align	4
	.type	bg_anim_steam, @object
	.size	bg_anim_steam, 68
bg_anim_steam:
# id:
	.word	.LC67
# name:
	.word	.LC68
# params:
# key:
	.word	.LC69
# label:
	.word	.LC70
# def:
	.byte	50
	.zero	3
# key:
	.word	.LC71
# label:
	.word	.LC72
# def:
	.byte	55
	.zero	3
# key:
	.word	.LC73
# label:
	.word	.LC74
# def:
	.byte	45
	.zero	3
# key:
	.word	.LC75
# label:
	.word	.LC76
# def:
	.byte	50
	.zero	3
# init:
	.word	_ZN12_GLOBAL__N_14initEii
# frame:
	.word	_ZN12_GLOBAL__N_15frameEjiiPKh
# band:
	.word	_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.data
	.align	4
	.type	_ZN12_GLOBAL__N_13g_hE, @object
	.size	_ZN12_GLOBAL__N_13g_hE, 4
_ZN12_GLOBAL__N_13g_hE:
	.word	480
	.align	4
	.type	_ZN12_GLOBAL__N_112lastThemeGenE, @object
	.size	_ZN12_GLOBAL__N_112lastThemeGenE, 4
_ZN12_GLOBAL__N_112lastThemeGenE:
	.word	-1
	.local	_ZN12_GLOBAL__N_18g_activeE
	.comm	_ZN12_GLOBAL__N_18g_activeE,4,4
	.align	4
	.type	_ZN12_GLOBAL__N_13rngE, @object
	.size	_ZN12_GLOBAL__N_13rngE, 4
_ZN12_GLOBAL__N_13rngE:
	.word	305441741
	.align	4
	.type	_ZN12_GLOBAL__N_110builtCountE, @object
	.size	_ZN12_GLOBAL__N_110builtCountE, 4
_ZN12_GLOBAL__N_110builtCountE:
	.word	-1
	.local	_ZN12_GLOBAL__N_19wispCountE
	.comm	_ZN12_GLOBAL__N_19wispCountE,4,4
	.local	_ZN12_GLOBAL__N_15bgLUTE
	.comm	_ZN12_GLOBAL__N_15bgLUTE,4,4
	.local	_ZN12_GLOBAL__N_18alphaLUTE
	.comm	_ZN12_GLOBAL__N_18alphaLUTE,4,4
	.local	_ZN12_GLOBAL__N_15drawsE
	.comm	_ZN12_GLOBAL__N_15drawsE,1320,4
	.local	_ZN12_GLOBAL__N_15blobsE
	.comm	_ZN12_GLOBAL__N_15blobsE,880,4
	.local	_ZN12_GLOBAL__N_15wispsE
	.comm	_ZN12_GLOBAL__N_15wispsE,120,4
	.ident	"GCC: (crosstool-NG esp-2021r2-patch5) 8.4.0"
