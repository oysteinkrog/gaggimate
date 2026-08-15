	.file	"AnimStarfield.cpp"
# GNU C++17 (crosstool-NG esp-2021r2-patch5) version 8.4.0 (xtensa-esp32s3-elf)
#	compiled by GNU C version 6.3.0 20170516, GMP version 6.2.1, MPFR version 4.1.0, MPC version 1.2.1, isl version isl-0.19-GMP

# GGC heuristics: --param ggc-min-expand=100 --param ggc-min-heapsize=131072
# options passed:  -I shim -imultilib no-rtti
# -iprefix c:\users\oystein\.platformio\packages\toolchain-xtensa-esp32s3\bin\../lib/gcc/xtensa-esp32s3-elf/8.4.0/
# ../../src/display/ui/default/bganim/AnimStarfield.cpp -mlongcalls
# -auxbase-strip xtensa-asm/AnimStarfield.S -O2 -std=gnu++17
# -fno-exceptions -fno-rtti -ffunction-sections -fverbose-asm
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
	.section	.text._ZN12_GLOBAL__N_118rebuildThemeAssetsEv,"ax",@progbits
	.literal_position
	.literal .LC0, _ZN12_GLOBAL__N_17starColE
	.literal .LC1, _ZN12_GLOBAL__N_15starsE
	.literal .LC2, 0x42960000
	.literal .LC3, _ZN12_GLOBAL__N_16vigLUTE
	.literal .LC4, _ZN12_GLOBAL__N_18vigColorE
	.literal .LC5, _ZN12_GLOBAL__N_18shootColE
	.align	4
	.type	_ZN12_GLOBAL__N_118rebuildThemeAssetsEv, @function
_ZN12_GLOBAL__N_118rebuildThemeAssetsEv:
	entry	sp, 48	#
	l32r	a6, .LC0	#, tmp134
	l32r	a5, .LC1	#, tmp137
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:121:     for (int i = 0; i < MAX_STARS; i++) {
	movi.n	a2, 0	# i,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:122:         themeRGB(180 + static_cast<int>(stars[i].hue * 75.0f), &starCol[i * 3]);
	movi	a4, 0xb4	# tmp97,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:121:     for (int i = 0; i < MAX_STARS; i++) {
	movi	a3, 0x190	# tmp99,
.L2:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:122:         themeRGB(180 + static_cast<int>(stars[i].hue * 75.0f), &starCol[i * 3]);
	l32i.n	a8, a5, 0	# stars, stars
	slli	a9, a2, 5	# tmp91, i,
	add.n	a8, a8, a9	# tmp92, stars, tmp91
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:122:         themeRGB(180 + static_cast<int>(stars[i].hue * 75.0f), &starCol[i * 3]);
	l32r	a9, .LC2	#,
	lsi	f0, a8, 24	# _4->hue, _4->hue
	wfr	f1, a9	#,
	mul.s	f0, f0, f1	# tmp93, _4->hue,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:122:         themeRGB(180 + static_cast<int>(stars[i].hue * 75.0f), &starCol[i * 3]);
	slli	a8, a2, 1	# tmp85, i,
	l32i.n	a11, a6, 0	# starCol, starCol
	add.n	a8, a8, a2	# tmp86, tmp85, i
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:122:         themeRGB(180 + static_cast<int>(stars[i].hue * 75.0f), &starCol[i * 3]);
	trunc.s	a10, f0, 0	# tmp96, tmp93
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:121:     for (int i = 0; i < MAX_STARS; i++) {
	addi.n	a2, a2, 1	# i, i,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:122:         themeRGB(180 + static_cast<int>(stars[i].hue * 75.0f), &starCol[i * 3]);
	add.n	a11, a11, a8	#, starCol, tmp86
	add.n	a10, a10, a4	#, tmp96, tmp97
	call8	_ZN6bganim8themeRGBEiPh		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:121:     for (int i = 0; i < MAX_STARS; i++) {
	bne	a2, a3, .L2	# i, tmp99,
	l32r	a5, .LC3	#, tmp136
	l32r	a4, .LC4	#, tmp138
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:124:     for (int idx = 0; idx < 128; idx++) {
	movi.n	a6, 0	# idx,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a3, -0x800	# tmp117,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a2, 0x7e0	# tmp122,
.L3:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:126:         themeRGB((vigLUT[idx] * 36) >> 8, c);
	l32i.n	a8, a5, 0	# vigLUT, vigLUT
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:126:         themeRGB((vigLUT[idx] * 36) >> 8, c);
	mov.n	a11, sp	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:126:         themeRGB((vigLUT[idx] * 36) >> 8, c);
	add.n	a8, a8, a6	# tmp102, vigLUT, idx
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:126:         themeRGB((vigLUT[idx] * 36) >> 8, c);
	l8ui	a8, a8, 0	# *_15, *_15
	slli	a10, a8, 3	# tmp107, *_15,
	add.n	a10, a10, a8	# tmp108, tmp107, *_15
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:126:         themeRGB((vigLUT[idx] * 36) >> 8, c);
	srli	a10, a10, 6	#, tmp108,
	call8	_ZN6bganim8themeRGBEiPh		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:127:         vigColor[idx] = rgb565(c[0], c[1], c[2]);
	l8ui	a8, sp, 0	# c, c
	l8ui	a9, sp, 1	# c, c
	l8ui	a11, sp, 2	# c, c
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a8, a8, 8	# tmp116, c,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a9, a9, 3	# tmp121, c,
	and	a9, a9, a2	# tmp124, tmp121, tmp122
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:127:         vigColor[idx] = rgb565(c[0], c[1], c[2]);
	l32i.n	a10, a4, 0	# vigColor, vigColor
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a8, a8, a3	# tmp119, tmp116, tmp117
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:127:         vigColor[idx] = rgb565(c[0], c[1], c[2]);
	slli	a12, a6, 1	# tmp113, idx,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a8, a8, a9	# tmp127, tmp119, tmp124
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	srai	a11, a11, 3	# tmp129, c,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:127:         vigColor[idx] = rgb565(c[0], c[1], c[2]);
	add.n	a9, a10, a12	# tmp114, vigColor, tmp113
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a8, a8, a11	# tmp132, tmp127, tmp129
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:127:         vigColor[idx] = rgb565(c[0], c[1], c[2]);
	s16i	a8, a9, 0	# *_28, tmp132
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:124:     for (int idx = 0; idx < 128; idx++) {
	addi.n	a6, a6, 1	# idx, idx,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:124:     for (int idx = 0; idx < 128; idx++) {
	bnei	a6, 128, .L3	# idx,,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:129:     themeRGB(255, shootCol);
	l32r	a11, .LC5	#,
	movi	a10, 0xff	#,
	call8	_ZN6bganim8themeRGBEiPh		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:130: }
	retw.n
	.size	_ZN12_GLOBAL__N_118rebuildThemeAssetsEv, .-_ZN12_GLOBAL__N_118rebuildThemeAssetsEv
	.global	__divsf3
	.section	.text._ZN12_GLOBAL__N_15frameEjiiPKh,"ax",@progbits
	.literal_position
	.literal .LC6, 0x00000000
	.literal .LC7, 0x40000000
	.literal .LC8, 0x3f800000
	.literal .LC9, 0x3d072b02
	.literal .LC10, _ZN12_GLOBAL__N_112lastThemeGenE
	.literal .LC11, _ZN12_GLOBAL__N_18g_nStarsE
	.literal .LC12, 1374389535
	.literal .LC13, 0x3a83126f
	.literal .LC14, 0x3c23d70a
	.literal .LC15, 0x3faaaaab
	.literal .LC16, 0x3eaaaaab
	.literal .LC17, 0x3d610e10
	.literal .LC18, 0x40400000
	.literal .LC19, _ZN12_GLOBAL__N_111lastDriftMsE
	.literal .LC20, _ZN12_GLOBAL__N_18bandHeadE
	.literal .LC21, _ZN12_GLOBAL__N_17lastTMsE
	.literal .LC22, _ZN12_GLOBAL__N_15starsE
	.literal .LC23, _ZN12_GLOBAL__N_16driftQE
	.literal .LC24, _ZN12_GLOBAL__N_15drawsE
	.literal .LC25, _ZN12_GLOBAL__N_15starYE
	.literal .LC26, _ZN12_GLOBAL__N_18bandNextE
	.literal .LC27, _ZN12_GLOBAL__N_17starColE
	.literal .LC28, 0x47800000
	.literal .LC29, 0x37800000
	.literal .LC30, 0x3d4ccccd
	.literal .LC31, 0x3fc90fda
	.literal .LC32, 0x4222f983
	.literal .LC33, 0x3e99999a
	.literal .LC34, 0x3f333333
	.literal .LC35, 0x437f0000
	.literal .LC36, _ZN12_GLOBAL__N_15shootE
	.literal .LC37, _ZN12_GLOBAL__N_111nextShootMsE
	.literal .LC38, _ZN12_GLOBAL__N_13rngE
	.literal .LC39, 0x33800000
	.literal .LC40, 0x40490fdb
	.literal .LC41, 0x3e4ccccd
	.literal .LC42, 0x3ef1463b
	.literal .LC43, 0x430c0000
	.literal .LC44, 0x43820000
	.literal .LC45, 0x3f19999a
	.literal .LC46, 0x3f000000
	.literal .LC47, -28500
	.literal .LC48, 30000
	.align	4
	.type	_ZN12_GLOBAL__N_15frameEjiiPKh, @function
_ZN12_GLOBAL__N_15frameEjiiPKh:
	entry	sp, 80	#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:132: void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
	s32i.n	a2, sp, 16	# %sfp, tMs
	s32i.n	a3, sp, 24	# %sfp, w
	s32i.n	a5, sp, 20	# %sfp, p
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:134:     if (themeGen() != lastThemeGen) {
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:134:     if (themeGen() != lastThemeGen) {
	l32r	a2, .LC10	#, tmp264
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:134:     if (themeGen() != lastThemeGen) {
	l32i.n	a3, a2, 0	# lastThemeGen, lastThemeGen
	beq	a10, a3, .L8	#, lastThemeGen,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:135:         rebuildThemeAssets();
	call8	_ZN12_GLOBAL__N_118rebuildThemeAssetsEv		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:136:         lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:136:         lastThemeGen = themeGen();
	s32i.n	a10, a2, 0	# lastThemeGen,
.L8:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:140:     const float twinkleAmt = p[2] * (1.0f / 100.0f); // reciprocal multiply: dividend isn't a compile-time
	l32i.n	a3, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:151:     const float driftPxPerSec = 3.0f * speedMul(p[0]);
	l32i.n	a5, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:138:     g_nStars = 40 + (p[1] * (MAX_STARS - 40)) / 100;
	l8ui	a4, a3, 1	# MEM[(const uint8_t *)p_146(D) + 1B], MEM[(const uint8_t *)p_146(D) + 1B]
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:140:     const float twinkleAmt = p[2] * (1.0f / 100.0f); // reciprocal multiply: dividend isn't a compile-time
	l8ui	a2, a3, 2	# MEM[(const uint8_t *)p_146(D) + 2B], MEM[(const uint8_t *)p_146(D) + 2B]
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:138:     g_nStars = 40 + (p[1] * (MAX_STARS - 40)) / 100;
	slli	a3, a4, 1	# tmp270, MEM[(const uint8_t *)p_146(D) + 1B],
	add.n	a4, a3, a4	# tmp271, tmp270, MEM[(const uint8_t *)p_146(D) + 1B]
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:140:     const float twinkleAmt = p[2] * (1.0f / 100.0f); // reciprocal multiply: dividend isn't a compile-time
	float.s	f0, a2, 0	# tmp289, MEM[(const uint8_t *)p_146(D) + 2B]
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:140:     const float twinkleAmt = p[2] * (1.0f / 100.0f); // reciprocal multiply: dividend isn't a compile-time
	l32r	a3, .LC14	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:151:     const float driftPxPerSec = 3.0f * speedMul(p[0]);
	l8ui	a2, a5, 0	# *p_146(D), *p_146(D)
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:140:     const float twinkleAmt = p[2] * (1.0f / 100.0f); // reciprocal multiply: dividend isn't a compile-time
	wfr	f1, a3	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	addi	a2, a2, -50	# tmp295, *p_146(D),
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:138:     g_nStars = 40 + (p[1] * (MAX_STARS - 40)) / 100;
	slli	a3, a4, 4	# tmp272, tmp271,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:140:     const float twinkleAmt = p[2] * (1.0f / 100.0f); // reciprocal multiply: dividend isn't a compile-time
	mul.s	f0, f0, f1	# twinkleAmt, tmp289,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:138:     g_nStars = 40 + (p[1] * (MAX_STARS - 40)) / 100;
	sub	a3, a3, a4	# tmp273, tmp272, tmp271
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	float.s	f1, a2, 0	# tmp296, tmp295
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:138:     g_nStars = 40 + (p[1] * (MAX_STARS - 40)) / 100;
	l32r	a2, .LC12	#, tmp277
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:138:     g_nStars = 40 + (p[1] * (MAX_STARS - 40)) / 100;
	slli	a3, a3, 3	# tmp274, tmp273,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:138:     g_nStars = 40 + (p[1] * (MAX_STARS - 40)) / 100;
	muluh	a3, a3, a2	# tmp492, tmp274, tmp277
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:150:     const float twinkleDepth = twinkleAmt * (twinkleAmt * (4.0f / 3.0f) + (1.0f / 3.0f));
	l32r	a2, .LC16	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:138:     g_nStars = 40 + (p[1] * (MAX_STARS - 40)) / 100;
	srli	a3, a3, 5	# tmp275, tmp492,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:150:     const float twinkleDepth = twinkleAmt * (twinkleAmt * (4.0f / 3.0f) + (1.0f / 3.0f));
	wfr	f2, a2	# tmp291,
	l32r	a2, .LC15	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:139:     const float t = tMs * 0.001f;
	l32i.n	a4, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:150:     const float twinkleDepth = twinkleAmt * (twinkleAmt * (4.0f / 3.0f) + (1.0f / 3.0f));
	wfr	f3, a2	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	l32r	a2, .LC17	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:150:     const float twinkleDepth = twinkleAmt * (twinkleAmt * (4.0f / 3.0f) + (1.0f / 3.0f));
	madd.s	f2, f0, f3	# tmp291, twinkleAmt,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	wfr	f3, a2	#,
	mul.s	f1, f1, f3	#, tmp296,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:138:     g_nStars = 40 + (p[1] * (MAX_STARS - 40)) / 100;
	l32r	a2, .LC11	#, tmp493
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:138:     g_nStars = 40 + (p[1] * (MAX_STARS - 40)) / 100;
	addi	a3, a3, 40	# tmp285, tmp275,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:138:     g_nStars = 40 + (p[1] * (MAX_STARS - 40)) / 100;
	s32i.n	a3, a2, 0	# g_nStars, tmp285
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:139:     const float t = tMs * 0.001f;
	l32r	a3, .LC13	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	rfr	a10, f1	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:139:     const float t = tMs * 0.001f;
	ufloat.s	f1, a4, 0	# tmp286,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:139:     const float t = tMs * 0.001f;
	wfr	f3, a3	#,
	mul.s	f1, f1, f3	#, tmp286,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:150:     const float twinkleDepth = twinkleAmt * (twinkleAmt * (4.0f / 3.0f) + (1.0f / 3.0f));
	mul.s	f2, f2, f0	#, tmp291, twinkleAmt
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:139:     const float t = tMs * 0.001f;
	ssi	f1, sp, 8	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:150:     const float twinkleDepth = twinkleAmt * (twinkleAmt * (4.0f / 3.0f) + (1.0f / 3.0f));
	ssi	f2, sp, 12	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	call8	exp2f		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:159:     const float rawDt = (tMs - lastDriftMs) * 0.001f;
	l32r	a4, .LC19	#, tmp499
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:151:     const float driftPxPerSec = 3.0f * speedMul(p[0]);
	l32r	a5, .LC18	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:160:     const float driftDt = lastDriftMs == 0xFFFFFFFF ? 0.0f : (rawDt < 2.0f ? rawDt : 2.0f);
	l32r	a6, .LC6	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:159:     const float rawDt = (tMs - lastDriftMs) * 0.001f;
	l32i.n	a3, a4, 0	# lastDriftMs, lastDriftMs$21_17
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:151:     const float driftPxPerSec = 3.0f * speedMul(p[0]);
	wfr	f0, a10	#,
	wfr	f1, a5	#,
	mul.s	f7, f0, f1	# driftPxPerSec,,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:160:     const float driftDt = lastDriftMs == 0xFFFFFFFF ? 0.0f : (rawDt < 2.0f ? rawDt : 2.0f);
	wfr	f6, a6	# rawDt,
	beqi	a3, -1, .L9	# lastDriftMs$21_17,,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:159:     const float rawDt = (tMs - lastDriftMs) * 0.001f;
	l32i.n	a8, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:160:     const float driftDt = lastDriftMs == 0xFFFFFFFF ? 0.0f : (rawDt < 2.0f ? rawDt : 2.0f);
	l32r	a10, .LC7	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:159:     const float rawDt = (tMs - lastDriftMs) * 0.001f;
	sub	a3, a8, a3	# tmp301,, lastDriftMs$21_17
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:159:     const float rawDt = (tMs - lastDriftMs) * 0.001f;
	ufloat.s	f6, a3, 0	# tmp302, tmp301
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:159:     const float rawDt = (tMs - lastDriftMs) * 0.001f;
	l32r	a3, .LC13	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:160:     const float driftDt = lastDriftMs == 0xFFFFFFFF ? 0.0f : (rawDt < 2.0f ? rawDt : 2.0f);
	wfr	f0, a10	# tmp304,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:159:     const float rawDt = (tMs - lastDriftMs) * 0.001f;
	wfr	f1, a3	#,
	mul.s	f6, f6, f1	# rawDt, tmp302,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:160:     const float driftDt = lastDriftMs == 0xFFFFFFFF ? 0.0f : (rawDt < 2.0f ? rawDt : 2.0f);
	olt.s	b0, f6, f0	#, rawDt, tmp304
	movf.s	f6, f0, b0	# rawDt, tmp304,
.L9:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:161:     lastDriftMs = tMs;
	l32i.n	a6, sp, 16	# %sfp,
	s32i.n	a6, a4, 0	# lastDriftMs,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:169:     const float *cosTab = cosTableF();
	ssi	f6, sp, 28	#,
	ssi	f7, sp, 32	#,
	call8	_ZN6bganim9cosTableFEv		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:173:         bandHead[b] = -1;
	l32r	a3, .LC20	#, tmp306
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:162:     const int32_t wQ = w << 16;
	l32i.n	a8, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:173:         bandHead[b] = -1;
	l32i.n	a3, a3, 0	# bandHead,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:173:         bandHead[b] = -1;
	lsi	f6, sp, 28	#,
	lsi	f7, sp, 32	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:173:         bandHead[b] = -1;
	s32i.n	a3, sp, 4	# %sfp,
	mov.n	a4, a3	# ivtmp$153,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:162:     const int32_t wQ = w << 16;
	slli	a5, a8, 16	# wQ,,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:173:         bandHead[b] = -1;
	movi.n	a6, -1	# tmp307,
	movi.n	a3, 0x1e	# tmp540,
	loop	a3, .L10_LEND	# tmp540,
.L10:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:173:         bandHead[b] = -1;
	s16i	a6, a4, 0	# MEM[base: _360, offset: 0B], tmp307
	addi.n	a4, a4, 2	# ivtmp$153, ivtmp$153,
	.L10_LEND:		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:175:     for (int i = 0; i < g_nStars; i++) {
	l32i.n	a14, a2, 0	# g_nStars, g_nStars$27_270
	bgei	a14, 1, .L11	# g_nStars$27_270,,
.L27:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:207:     const float dt = lastTMs != 0 && tMs > lastTMs ? (tMs - lastTMs) * 0.001f : 0.033f;
	l32r	a3, .LC21	#, tmp495
	l32i.n	a2, a3, 0	# lastTMs, lastTMs$60_94
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:207:     const float dt = lastTMs != 0 && tMs > lastTMs ? (tMs - lastTMs) * 0.001f : 0.033f;
	beqz.n	a2, .L47	# lastTMs$60_94,
	l32i.n	a4, sp, 16	# %sfp,
	bltu	a2, a4, .L12	# lastTMs$60_94,,
.L47:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:207:     const float dt = lastTMs != 0 && tMs > lastTMs ? (tMs - lastTMs) * 0.001f : 0.033f;
	l32r	a5, .LC9	#,
	wfr	f0, a5	# iftmp$59_135,
	j	.L15		#
.L11:
	l32r	a2, .LC22	#, tmp318
	l32r	a8, .LC6	#,
	l32i.n	a9, a2, 0	# stars, stars
	l32r	a2, .LC23	#, tmp320
	l32i.n	a6, sp, 24	# %sfp,
	l32i.n	a13, a2, 0	# driftQ, ivtmp$138
	l32r	a2, .LC24	#, tmp321
	l32r	a4, .LC27	#, tmp324
	wfr	f4, a8	# tmp496,
	l32r	a8, .LC8	#,
	l32i.n	a11, a2, 0	# draws, ivtmp$139
	l32r	a2, .LC25	#, tmp322
	float.s	f14, a6, 0	# _412,
	l32i.n	a12, a4, 0	# starCol, ivtmp$143
	l32r	a6, .LC29	#,
	l32r	a4, .LC28	#,
	wfr	f3, a8	# tmp498,
	l32r	a8, .LC33	#,
	l32i.n	a3, a2, 0	# starY, ivtmp$140
	l32r	a2, .LC26	#, tmp323
	wfr	f13, a4	# tmp506,
	wfr	f12, a6	# tmp507,
	l32r	a4, .LC31	#,
	l32r	a6, .LC32	#,
	wfr	f9, a8	# tmp503,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	l32r	a8, .LC35	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:175:     for (int i = 0; i < g_nStars; i++) {
	movi.n	a15, 0	# i,
	l32i.n	a2, a2, 0	# bandNext, ivtmp$141
	wfr	f11, a4	# tmp501,
	wfr	f10, a6	# tmp502,
	addi.n	a9, a9, 8	# ivtmp$136, stars,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	movi	a6, 0xff	# iftmp$15_205,
	mov.n	a4, a15	# iftmp$15_205, i
	wfr	f15, a8	# tmp535,
	s32i.n	a10, sp, 44	# %sfp, cosTab
	loop	a14, .L28_LEND	# g_nStars$27_270,
.L28:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:177:         const float v = driftPxPerSec * s.driftSpeed;
	lsi	f0, a9, 20	# MEM[base: _379, offset: 20B], MEM[base: _379, offset: 20B]
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:178:         int32_t pos = driftQ[i] + static_cast<int32_t>(v * driftDt * 65536.0f);
	l32i.n	a8, a13, 0	# MEM[base: _378, offset: 0B], MEM[base: _378, offset: 0B]
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:177:         const float v = driftPxPerSec * s.driftSpeed;
	mul.s	f0, f7, f0	# v, driftPxPerSec, MEM[base: _379, offset: 20B]
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:178:         int32_t pos = driftQ[i] + static_cast<int32_t>(v * driftDt * 65536.0f);
	mul.s	f0, f0, f6	# tmp327, v, rawDt
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:178:         int32_t pos = driftQ[i] + static_cast<int32_t>(v * driftDt * 65536.0f);
	mul.s	f0, f0, f13	# tmp328, tmp327, tmp506
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:178:         int32_t pos = driftQ[i] + static_cast<int32_t>(v * driftDt * 65536.0f);
	trunc.s	a7, f0, 0	# tmp330, tmp328
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:178:         int32_t pos = driftQ[i] + static_cast<int32_t>(v * driftDt * 65536.0f);
	add.n	a7, a7, a8	# pos, tmp330, MEM[base: _378, offset: 0B]
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:179:         if (pos >= wQ) {
	blt	a7, a5, .L16	# pos, wQ,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:180:             pos -= wQ;
	sub	a7, a7, a5	# pos, pos, wQ
	j	.L17		#
.L16:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:182:             pos += wQ;
	add.n	a8, a7, a5	# tmp508, pos, wQ
	movltz	a7, a8, a7	# pos, tmp508, pos
.L17:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:185:         const float x = pos * (1.0f / 65536.0f);
	float.s	f1, a7, 0	# tmp332, pos
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:190:         const float edgeRaw = (x < (w - x) ? x : (w - x)) * 0.05f; // *0.05f == /20.0f
	l32r	a8, .LC30	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:185:         const float x = pos * (1.0f / 65536.0f);
	mul.s	f1, f1, f12	# x, tmp332, tmp507
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:184:         driftQ[i] = pos;
	s32i.n	a7, a13, 0	# MEM[base: _378, offset: 0B], pos
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:190:         const float edgeRaw = (x < (w - x) ? x : (w - x)) * 0.05f; // *0.05f == /20.0f
	wfr	f0, a8	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:190:         const float edgeRaw = (x < (w - x) ? x : (w - x)) * 0.05f; // *0.05f == /20.0f
	sub.s	f2, f14, f1	# _38, _412, x
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:190:         const float edgeRaw = (x < (w - x) ? x : (w - x)) * 0.05f; // *0.05f == /20.0f
	olt.s	b0, f1, f2	#, x, _38
	bf	b0, .L67	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:190:         const float edgeRaw = (x < (w - x) ? x : (w - x)) * 0.05f; // *0.05f == /20.0f
	mul.s	f2, f1, f0	# iftmp$34_132, x,
	j	.L20		#
.L67:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:190:         const float edgeRaw = (x < (w - x) ? x : (w - x)) * 0.05f; // *0.05f == /20.0f
	mul.s	f2, f2, f0	# iftmp$34_132, _38,
.L20:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:192:         const float twinkleRad = t * s.rate + s.phase - 1.5707963f;
	lsi	f5, a9, 4	# MEM[base: _379, offset: 4B],
	lsi	f8, sp, 8	# %sfp,
	lsi	f0, a9, 0	# MEM[base: _379, offset: 0B],
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:193:         const float twinkle = 0.7f + 0.3f * cosTab[static_cast<int>(twinkleRad * RAD_TO_TAB) & 255];
	l32i.n	a10, sp, 44	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:192:         const float twinkleRad = t * s.rate + s.phase - 1.5707963f;
	madd.s	f0, f5, f8	#,,
	trunc.s	a8, f1, 0	# tmp359, x
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:194:         float bF = s.baseBrightness * (1.0f - twinkleDepth * (1.0f - twinkle)) * edgeFade;
	lsi	f5, sp, 12	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:194:         float bF = s.baseBrightness * (1.0f - twinkleDepth * (1.0f - twinkle)) * edgeFade;
	lsi	f8, a9, 8	# MEM[base: _379, offset: 8B],
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:192:         const float twinkleRad = t * s.rate + s.phase - 1.5707963f;
	ssi	f0, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:192:         const float twinkleRad = t * s.rate + s.phase - 1.5707963f;
	sub.s	f0, f0, f11	# twinkleRad,, tmp501
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:191:         const float edgeFade = edgeRaw < 1.0f ? edgeRaw : 1.0f;
	olt.s	b0, f2, f3	#, iftmp$34_132, tmp498
	movf.s	f2, f3, b0	# iftmp$34_132, tmp498,
	sext	a8, a8, 15	# _438, tmp359
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:193:         const float twinkle = 0.7f + 0.3f * cosTab[static_cast<int>(twinkleRad * RAD_TO_TAB) & 255];
	mul.s	f0, f0, f10	# tmp342, twinkleRad, tmp502
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:193:         const float twinkle = 0.7f + 0.3f * cosTab[static_cast<int>(twinkleRad * RAD_TO_TAB) & 255];
	trunc.s	a7, f0, 0	# tmp344, tmp342
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:193:         const float twinkle = 0.7f + 0.3f * cosTab[static_cast<int>(twinkleRad * RAD_TO_TAB) & 255];
	extui	a7, a7, 0, 8	# tmp345, tmp344,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:193:         const float twinkle = 0.7f + 0.3f * cosTab[static_cast<int>(twinkleRad * RAD_TO_TAB) & 255];
	slli	a7, a7, 2	# tmp346, tmp345,
	add.n	a7, a10, a7	# tmp347,, tmp346
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:193:         const float twinkle = 0.7f + 0.3f * cosTab[static_cast<int>(twinkleRad * RAD_TO_TAB) & 255];
	l32r	a10, .LC34	#,
	lsi	f0, a7, 0	# *_48, *_48
	wfr	f1, a10	#,
	madd.s	f1, f0, f9	#, *_48, tmp503
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:194:         float bF = s.baseBrightness * (1.0f - twinkleDepth * (1.0f - twinkle)) * edgeFade;
	sub.s	f0, f3, f1	# tmp352, tmp498, twinkle
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:194:         float bF = s.baseBrightness * (1.0f - twinkleDepth * (1.0f - twinkle)) * edgeFade;
	mov.s	f1, f3	#, tmp498
	msub.s	f1, f0, f5	#, tmp352,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:194:         float bF = s.baseBrightness * (1.0f - twinkleDepth * (1.0f - twinkle)) * edgeFade;
	mul.s	f0, f1, f8	# tmp356, tmp354,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:194:         float bF = s.baseBrightness * (1.0f - twinkleDepth * (1.0f - twinkle)) * edgeFade;
	mul.s	f2, f0, f2	# bF, tmp356, iftmp$34_132
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:195:         bF = bF < 0.0f ? 0.0f : (bF > 1.0f ? 1.0f : bF);
	olt.s	b0, f2, f4	#, bF, tmp496
	bt	b0, .L39	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:195:         bF = bF < 0.0f ? 0.0f : (bF > 1.0f ? 1.0f : bF);
	olt.s	b0, f3, f2	#, tmp498, bF
	bt	b0, .L23	#,
	j	.L22		#
.L39:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:195:         bF = bF < 0.0f ? 0.0f : (bF > 1.0f ? 1.0f : bF);
	mov.s	f2, f4	# bF, tmp496
.L22:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:196:         draws[i].x = static_cast<int16_t>(x);
	s16i	a8, a11, 0	# MEM[base: _375, offset: 0B], _438
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:197:         draws[i].r = clamp8f(starCol[i * 3 + 0] * bF);
	l8ui	a8, a12, 0	# MEM[base: _369, offset: 0B], MEM[base: _369, offset: 0B]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.n	a7, a4	# iftmp$15_207, iftmp$15_205
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:197:         draws[i].r = clamp8f(starCol[i * 3 + 0] * bF);
	float.s	f0, a8, 0	# tmp363, MEM[base: _369, offset: 0B]
	mul.s	f0, f0, f2	# _304, tmp363, bF
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f0, f4	#, _304, tmp496
	bt	b0, .L24	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	l32r	a8, .LC35	#,
	mov.n	a7, a6	# iftmp$15_207, iftmp$15_205
	wfr	f1, a8	#,
	olt.s	b0, f1, f0	#,, _304
	bf	b0, .L35	#,
	j	.L24		#
.L68:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:195:         bF = bF < 0.0f ? 0.0f : (bF > 1.0f ? 1.0f : bF);
	mov.s	f2, f3	# bF, tmp498
.L35:
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	utrunc.s	a7, f0, 0	# tmp367, _304
	extui	a7, a7, 0, 8	# iftmp$15_207, tmp367
.L24:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:197:         draws[i].r = clamp8f(starCol[i * 3 + 0] * bF);
	s8i	a7, a11, 2	# MEM[base: _374, offset: 2B], iftmp$15_207
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:198:         draws[i].g = clamp8f(starCol[i * 3 + 1] * bF);
	l8ui	a8, a12, 1	# MEM[base: _367, offset: 1B], MEM[base: _367, offset: 1B]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.n	a7, a4	# iftmp$15_205, iftmp$15_205
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:198:         draws[i].g = clamp8f(starCol[i * 3 + 1] * bF);
	float.s	f0, a8, 0	# tmp369, MEM[base: _367, offset: 1B]
	mul.s	f0, f0, f2	# _68, tmp369, bF
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f0, f4	#, _68, tmp496
	bt	b0, .L25	#,
	l32r	a10, .LC35	#,
	wfr	f1, a10	# tmp505,
.L37:
	olt.s	b0, f1, f0	#, tmp505, _68
	mov.n	a7, a6	# iftmp$15_205, iftmp$15_205
	bt	b0, .L25	#,
	utrunc.s	a7, f0, 0	# tmp373, _68
	extui	a7, a7, 0, 8	# iftmp$15_205, tmp373
.L25:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:198:         draws[i].g = clamp8f(starCol[i * 3 + 1] * bF);
	s8i	a7, a11, 3	# MEM[base: _357, offset: 3B], iftmp$15_205
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:199:         draws[i].b = clamp8f(starCol[i * 3 + 2] * bF);
	l8ui	a8, a12, 2	# MEM[base: _69, offset: 2B], MEM[base: _69, offset: 2B]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.n	a7, a4	# iftmp$15_203, iftmp$15_205
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:199:         draws[i].b = clamp8f(starCol[i * 3 + 2] * bF);
	float.s	f0, a8, 0	# tmp375, MEM[base: _69, offset: 2B]
	mul.s	f2, f0, f2	# _79, tmp375, bF
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f2, f4	#, _79, tmp496
	bt	b0, .L26	#,
	l32r	a8, .LC35	#,
	mov.n	a7, a6	# iftmp$15_203, iftmp$15_205
	wfr	f0, a8	#,
	olt.s	b0, f0, f2	#,, _79
	bt	b0, .L26	#,
	utrunc.s	a7, f2, 0	# tmp379, _79
	extui	a7, a7, 0, 8	# iftmp$15_203, tmp379
.L26:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:199:         draws[i].b = clamp8f(starCol[i * 3 + 2] * bF);
	s8i	a7, a11, 4	# MEM[base: _357, offset: 4B], iftmp$15_203
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:200:         draws[i].sizeClass = s.sizeClass;
	l8ui	a7, a9, 12	# MEM[base: _379, offset: 12B], _80
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:202:         bandNext[i] = bandHead[bandIdx];
	l32i.n	a10, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:200:         draws[i].sizeClass = s.sizeClass;
	s8i	a7, a11, 5	# MEM[base: _357, offset: 5B], _80
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:201:         const int bandIdx = starY[i] >> 4;
	l16si	a7, a3, 0	# MEM[base: _372, offset: 0B], MEM[base: _372, offset: 0B]
	addi.n	a13, a13, 4	# ivtmp$138, ivtmp$138,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:201:         const int bandIdx = starY[i] >> 4;
	srai	a7, a7, 4	# bandIdx, MEM[base: _372, offset: 0B],
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:202:         bandNext[i] = bandHead[bandIdx];
	slli	a7, a7, 1	# tmp383, bandIdx,
	add.n	a7, a10, a7	# _89,, tmp383
	l16ui	a8, a7, 0	# *_89,
	addi	a9, a9, 32	# ivtmp$136, ivtmp$136,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:202:         bandNext[i] = bandHead[bandIdx];
	s16i	a8, a2, 0	# MEM[base: _371, offset: 0B], *_89
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:203:         bandHead[bandIdx] = static_cast<int16_t>(i);
	s16i	a15, a7, 0	# *_89, i
	addi.n	a11, a11, 6	# ivtmp$139, ivtmp$139,
	addi.n	a3, a3, 2	# ivtmp$140, ivtmp$140,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:175:     for (int i = 0; i < g_nStars; i++) {
	addi.n	a15, a15, 1	# i, i,
	addi.n	a2, a2, 2	# ivtmp$141, ivtmp$141,
	addi.n	a12, a12, 3	# ivtmp$143, ivtmp$143,
	.L28_LEND:		#
	j	.L27		#
.L12:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:207:     const float dt = lastTMs != 0 && tMs > lastTMs ? (tMs - lastTMs) * 0.001f : 0.033f;
	l32i.n	a4, sp, 16	# %sfp,
	sub	a2, a4, a2	# tmp385,, lastTMs$60_94
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:207:     const float dt = lastTMs != 0 && tMs > lastTMs ? (tMs - lastTMs) * 0.001f : 0.033f;
	ufloat.s	f0, a2, 0	# tmp386, tmp385
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:207:     const float dt = lastTMs != 0 && tMs > lastTMs ? (tMs - lastTMs) * 0.001f : 0.033f;
	l32r	a2, .LC13	#,
	wfr	f1, a2	#,
	mul.s	f0, f0, f1	# iftmp$59_135, tmp386,
.L15:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:208:     lastTMs = tMs;
	l32i.n	a4, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:210:     if (!shoot.active && shootFreq > 0 && tMs > nextShootMs) {
	l32r	a2, .LC36	#, tmp497
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:208:     lastTMs = tMs;
	s32i.n	a4, a3, 0	# lastTMs,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:209:     const uint8_t shootFreq = p[3];
	l32i.n	a6, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:210:     if (!shoot.active && shootFreq > 0 && tMs > nextShootMs) {
	l8ui	a4, a2, 0	# shoot.active, _97
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:210:     if (!shoot.active && shootFreq > 0 && tMs > nextShootMs) {
	movi.n	a5, 1	# tmp391,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:209:     const uint8_t shootFreq = p[3];
	l8ui	a3, a6, 3	# MEM[(const uint8_t *)p_146(D) + 3B], shootFreq
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:210:     if (!shoot.active && shootFreq > 0 && tMs > nextShootMs) {
	beq	a4, a5, .L29	# _97, tmp391,
	beqz.n	a3, .L29	# shootFreq,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:210:     if (!shoot.active && shootFreq > 0 && tMs > nextShootMs) {
	l32r	a5, .LC37	#, tmp399
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:210:     if (!shoot.active && shootFreq > 0 && tMs > nextShootMs) {
	l32i.n	a8, sp, 16	# %sfp,
	l32i.n	a4, a5, 0	# nextShootMs, nextShootMs
	bgeu	a4, a8, .L7	# nextShootMs,,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	l32r	a6, .LC38	#, tmp401
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	l32r	a10, .LC39	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	l32i.n	a7, a6, 0	# rng, _254
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	wfr	f5, a10	# tmp408,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a4, a7, 13	# tmp402, _254,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a7, a4, a7	# _256, tmp402, _254
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a4, a7, 17, 15	# tmp403, _256,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a4, a4, a7	# _258, tmp403, _256
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a7, a4, 5	# tmp404, _258,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a7, a7, a4	# _260, tmp404, _258
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a4, a7, 13	# tmp413, _260,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a8, a4, a7	# _246, tmp413, _260
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a4, a8, 17, 15	# tmp414, _246,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a4, a4, a8	# _248, tmp414, _246
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a8, a4, 5	# tmp415, _248,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a8, a8, a4	# _250, tmp415, _248
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a4, a8, 13	# tmp424, _250,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a9, a4, a8	# _237, tmp424, _250
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a4, a9, 17, 15	# tmp425, _237,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a4, a4, a9	# _239, tmp425, _237
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a9, a4, 5	# tmp426, _239,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a4, a9, a4	# _241, tmp426, _239
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a9, a4, 13	# tmp435, _241,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a10, a9, a4	# _228, tmp435, _241
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a9, a10, 17, 15	# tmp436, _228,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a9, a9, a10	# _230, tmp436, _228
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a10, a9, 5	# tmp437, _230,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a9, a10, a9	# _232, tmp437, _230
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a7, a7, 8	# tmp405, _260,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f3, a7, 0	# tmp406, tmp405
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a4, a4, 8	# tmp428, _241,
	srli	a7, a9, 8	# tmp439, _232,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f2, a4, 0	# tmp429, tmp428
	float.s	f4, a7, 0	# tmp440, tmp439
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:214:         shoot.x = nextRandf(rng) * w * 0.6f;
	l32i.n	a4, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f3, f3, f5	# tmp407, tmp406, tmp408
	mul.s	f2, f2, f5	# tmp430, tmp429, tmp408
	mul.s	f4, f4, f5	# tmp441, tmp440, tmp408
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:214:         shoot.x = nextRandf(rng) * w * 0.6f;
	float.s	f1, a4, 0	# _102,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:211:         const float angle = 3.14159265f * 0.15f + nextRandf(rng) * 3.14159265f * 0.2f;
	l32r	a4, .LC40	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:214:         shoot.x = nextRandf(rng) * w * 0.6f;
	mul.s	f2, f2, f1	# tmp432, tmp430, _102
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:211:         const float angle = 3.14159265f * 0.15f + nextRandf(rng) * 3.14159265f * 0.2f;
	wfr	f6, a4	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:211:         const float angle = 3.14159265f * 0.15f + nextRandf(rng) * 3.14159265f * 0.2f;
	l32r	a4, .LC42	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:211:         const float angle = 3.14159265f * 0.15f + nextRandf(rng) * 3.14159265f * 0.2f;
	mul.s	f3, f3, f6	# tmp409, tmp407,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:215:         shoot.y = nextRandf(rng) * w * 0.3f;
	mul.s	f4, f4, f1	# tmp443, tmp441, _102
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:211:         const float angle = 3.14159265f * 0.15f + nextRandf(rng) * 3.14159265f * 0.2f;
	wfr	f1, a4	#,
	l32r	a4, .LC41	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a8, a8, 8	# tmp416, _250,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:211:         const float angle = 3.14159265f * 0.15f + nextRandf(rng) * 3.14159265f * 0.2f;
	wfr	f7, a4	#,
	madd.s	f1, f3, f7	#, tmp409,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:214:         shoot.x = nextRandf(rng) * w * 0.6f;
	l32r	a4, .LC45	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f6, a8, 0	# tmp417, tmp416
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:214:         shoot.x = nextRandf(rng) * w * 0.6f;
	wfr	f7, a4	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:215:         shoot.y = nextRandf(rng) * w * 0.3f;
	l32r	a4, .LC33	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:211:         const float angle = 3.14159265f * 0.15f + nextRandf(rng) * 3.14159265f * 0.2f;
	mov.s	f3, f1	# angle,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:214:         shoot.x = nextRandf(rng) * w * 0.6f;
	mul.s	f1, f2, f7	# tmp433, tmp432,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:215:         shoot.y = nextRandf(rng) * w * 0.3f;
	wfr	f7, a4	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:212:         const float speed = 260.0f + nextRandf(rng) * 140.0f;
	l32r	a4, .LC44	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f6, f6, f5	# tmp418, tmp417, tmp408
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:215:         shoot.y = nextRandf(rng) * w * 0.3f;
	mul.s	f2, f4, f7	# tmp444, tmp443,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:212:         const float speed = 260.0f + nextRandf(rng) * 140.0f;
	wfr	f4, a4	#,
	l32r	a4, .LC43	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:214:         shoot.x = nextRandf(rng) * w * 0.6f;
	ssi	f1, a2, 4	# shoot.x, tmp433
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:212:         const float speed = 260.0f + nextRandf(rng) * 140.0f;
	wfr	f7, a4	#,
	madd.s	f4, f6, f7	#, tmp418,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:213:         shoot.active = true;
	movi.n	a4, 1	# tmp396,
	s8i	a4, a2, 0	# shoot.active, tmp396
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:215:         shoot.y = nextRandf(rng) * w * 0.3f;
	ssi	f2, a2, 8	# shoot.y, tmp444
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:216:         shoot.vx = cosf(angle) * speed;
	rfr	a10, f3	#, angle
	ssi	f0, sp, 36	#,
	s32i.n	a9, sp, 32	#,
	ssi	f5, sp, 40	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:212:         const float speed = 260.0f + nextRandf(rng) * 140.0f;
	ssi	f4, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:216:         shoot.vx = cosf(angle) * speed;
	ssi	f3, sp, 28	#,
	call8	cosf		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:216:         shoot.vx = cosf(angle) * speed;
	lsi	f4, sp, 0	# %sfp,
	wfr	f2, a10	#,
	mul.s	f1, f2, f4	# tmp447,,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:217:         shoot.vy = sinf(angle) * speed;
	lsi	f3, sp, 28	#,
	rfr	a10, f3	#, angle
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:216:         shoot.vx = cosf(angle) * speed;
	ssi	f1, a2, 12	# shoot.vx, tmp447
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:217:         shoot.vy = sinf(angle) * speed;
	call8	sinf		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	l32i.n	a9, sp, 32	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	lsi	f5, sp, 40	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a7, a9, 13	# tmp450, _232,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a7, a7, a9	# _219, tmp450, _232
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a4, a7, 17, 15	# tmp451, _219,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a7, a4, a7	# _221, tmp451, _219
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a4, a7, 5	# tmp452, _221,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a4, a4, a7	# _223, tmp452, _221
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a7, a4, 8	# tmp453, _223,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f1, a7, 0	# tmp454, tmp453
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:219:         shoot.maxLife = 0.5f + nextRandf(rng) * 0.3f;
	l32r	a8, .LC46	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f1, f1, f5	# tmp455, tmp454, tmp408
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:219:         shoot.maxLife = 0.5f + nextRandf(rng) * 0.3f;
	wfr	f2, a8	# pretmp_434,
	l32r	a8, .LC33	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:217:         shoot.vy = sinf(angle) * speed;
	lsi	f7, sp, 0	# %sfp,
	wfr	f6, a10	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:219:         shoot.maxLife = 0.5f + nextRandf(rng) * 0.3f;
	wfr	f8, a8	#,
	madd.s	f2, f1, f8	# pretmp_434, tmp455,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:217:         shoot.vy = sinf(angle) * speed;
	mul.s	f3, f6, f7	# tmp449,,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a7, a4, 13	# tmp466, _223,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a7, a7, a4	# _210, tmp466, _223
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a4, a7, 17, 15	# tmp467, _210,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a4, a4, a7	# _212, tmp467, _210
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:220:         shoot.invMaxLife = 1.0f / shoot.maxLife; // one divide per shoot trigger (rare), not per band()
	l32r	a10, .LC8	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:217:         shoot.vy = sinf(angle) * speed;
	ssi	f3, a2, 16	# shoot.vy, tmp449
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:219:         shoot.maxLife = 0.5f + nextRandf(rng) * 0.3f;
	ssi	f2, a2, 24	# shoot.maxLife, pretmp_434
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:220:         shoot.invMaxLife = 1.0f / shoot.maxLife; // one divide per shoot trigger (rare), not per band()
	rfr	a11, f2	#, pretmp_434
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a7, a4, 5	# tmp468, _212,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:220:         shoot.invMaxLife = 1.0f / shoot.maxLife; // one divide per shoot trigger (rare), not per band()
	ssi	f2, sp, 28	#,
	ssi	f5, sp, 40	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a7, a7, a4	# _214, tmp468, _212
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:220:         shoot.invMaxLife = 1.0f / shoot.maxLife; // one divide per shoot trigger (rare), not per band()
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a4, a7, 8	# tmp471, _214,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:221:         const uint32_t interval = 1500 > 30000 - shootFreq * 280 ? 1500 : 30000 - shootFreq * 280;
	movi	a8, -0x118	# tmp463,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f1, a4, 0	# tmp472, tmp471
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:221:         const uint32_t interval = 1500 > 30000 - shootFreq * 280 ? 1500 : 30000 - shootFreq * 280;
	mull	a3, a3, a8	# tmp462, shootFreq, tmp463
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:221:         const uint32_t interval = 1500 > 30000 - shootFreq * 280 ? 1500 : 30000 - shootFreq * 280;
	l32r	a4, .LC47	#, tmp464
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	lsi	f5, sp, 40	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:221:         const uint32_t interval = 1500 > 30000 - shootFreq * 280 ? 1500 : 30000 - shootFreq * 280;
	max	a3, a3, a4	# tmp462, tmp462, tmp464
	l32r	a4, .LC48	#, tmp465
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f1, f1, f5	# tmp473, tmp472, tmp408
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:221:         const uint32_t interval = 1500 > 30000 - shootFreq * 280 ? 1500 : 30000 - shootFreq * 280;
	add.n	a3, a3, a4	# _117, tmp462, tmp465
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:222:         nextShootMs = tMs + interval + static_cast<uint32_t>(nextRandf(rng) * interval * 0.5f);
	float.s	f3, a3, 0	# tmp475, _117
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:222:         nextShootMs = tMs + interval + static_cast<uint32_t>(nextRandf(rng) * interval * 0.5f);
	l32i.n	a4, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:222:         nextShootMs = tMs + interval + static_cast<uint32_t>(nextRandf(rng) * interval * 0.5f);
	mul.s	f1, f1, f3	# tmp476, tmp473, tmp475
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:222:         nextShootMs = tMs + interval + static_cast<uint32_t>(nextRandf(rng) * interval * 0.5f);
	add.n	a3, a3, a4	# tmp480, _117,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:222:         nextShootMs = tMs + interval + static_cast<uint32_t>(nextRandf(rng) * interval * 0.5f);
	l32r	a4, .LC46	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	s32i.n	a7, a6, 0	# rng, _214
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:222:         nextShootMs = tMs + interval + static_cast<uint32_t>(nextRandf(rng) * interval * 0.5f);
	wfr	f4, a4	#,
	mul.s	f1, f1, f4	# tmp477, tmp476,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:222:         nextShootMs = tMs + interval + static_cast<uint32_t>(nextRandf(rng) * interval * 0.5f);
	l32r	a6, .LC6	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:220:         shoot.invMaxLife = 1.0f / shoot.maxLife; // one divide per shoot trigger (rare), not per band()
	s32i.n	a10, a2, 28	# shoot.invMaxLife,
	lsi	f0, sp, 36	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:222:         nextShootMs = tMs + interval + static_cast<uint32_t>(nextRandf(rng) * interval * 0.5f);
	utrunc.s	a4, f1, 0	# tmp479, tmp477
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:222:         nextShootMs = tMs + interval + static_cast<uint32_t>(nextRandf(rng) * interval * 0.5f);
	wfr	f3, a6	# pretmp_432,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:222:         nextShootMs = tMs + interval + static_cast<uint32_t>(nextRandf(rng) * interval * 0.5f);
	add.n	a3, a4, a3	# tmp481, tmp479, tmp480
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:222:         nextShootMs = tMs + interval + static_cast<uint32_t>(nextRandf(rng) * interval * 0.5f);
	s32i.n	a3, a5, 0	# nextShootMs, tmp481
	lsi	f2, sp, 28	#,
	j	.L32		#
.L29:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:224:     if (shoot.active) {
	beqz.n	a4, .L7	# _97,
	lsi	f3, a2, 20	# shoot.life, pretmp_432
	lsi	f2, a2, 24	# shoot.maxLife, pretmp_434
.L32:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:225:         shoot.life += dt;
	add.s	f0, f0, f3	# _126, iftmp$59_135, pretmp_432
	ssi	f0, a2, 20	# shoot.life, _126
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:226:         if (shoot.life >= shoot.maxLife) {
	ole.s	b0, f2, f0	#, pretmp_434, _126
	bf	b0, .L7	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:227:             shoot.active = false;
	movi.n	a3, 0	# tmp486,
	s8i	a3, a2, 0	# shoot.active, tmp486
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:230: }
	j	.L7		#
.L23:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:196:         draws[i].x = static_cast<int16_t>(x);
	s16i	a8, a11, 0	# MEM[base: _376, offset: 0B], _438
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:197:         draws[i].r = clamp8f(starCol[i * 3 + 0] * bF);
	l8ui	a7, a12, 0	# MEM[base: _368, offset: 0B], MEM[base: _368, offset: 0B]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.s	f1, f15	# tmp505, tmp535
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:197:         draws[i].r = clamp8f(starCol[i * 3 + 0] * bF);
	float.s	f0, a7, 0	# _304, MEM[base: _368, offset: 0B]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f15, f0	#, tmp535, _304
	bf	b0, .L68	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:197:         draws[i].r = clamp8f(starCol[i * 3 + 0] * bF);
	movi.n	a8, -1	#,
	s8i	a8, a11, 2	# MEM[base: _22, offset: 2B],
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:198:         draws[i].g = clamp8f(starCol[i * 3 + 1] * bF);
	l8ui	a7, a12, 1	# MEM[base: _160, offset: 1B], MEM[base: _160, offset: 1B]
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:195:         bF = bF < 0.0f ? 0.0f : (bF > 1.0f ? 1.0f : bF);
	mov.s	f2, f3	# bF, tmp498
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:198:         draws[i].g = clamp8f(starCol[i * 3 + 1] * bF);
	float.s	f0, a7, 0	# _68, MEM[base: _160, offset: 1B]
	j	.L37		#
.L7:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:230: }
	retw.n
	.size	_ZN12_GLOBAL__N_15frameEjiiPKh, .-_ZN12_GLOBAL__N_15frameEjiiPKh
	.section	.text._ZN12_GLOBAL__N_14initEii,"ax",@progbits
	.literal_position
	.literal .LC49, _ZN12_GLOBAL__N_15starsE
	.literal .LC50, 12800
	.literal .LC51, 2400
	.literal .LC52, _ZN12_GLOBAL__N_15drawsE
	.literal .LC53, _ZN12_GLOBAL__N_15starYE
	.literal .LC54, _ZN12_GLOBAL__N_18bandHeadE
	.literal .LC55, _ZN12_GLOBAL__N_18bandNextE
	.literal .LC56, _ZN12_GLOBAL__N_16driftQE
	.literal .LC57, _ZN12_GLOBAL__N_13dx2E
	.literal .LC58, _ZN12_GLOBAL__N_13dy2E
	.literal .LC59, _ZN12_GLOBAL__N_16vigLUTE
	.literal .LC60, _ZN12_GLOBAL__N_17starColE
	.literal .LC61, _ZN12_GLOBAL__N_18vigColorE
	.literal .LC62, 0x3f000000
	.literal .LC63, 0x42fe0000
	.literal .LC64, 0x3f800000
	.literal .LC65, 0x00000000
	.literal .LC66, 0x437f0000
	.literal .LC67, _ZN12_GLOBAL__N_13rngE
	.literal .LC68, 0x33800000
	.literal .LC69, 0x3f333333
	.literal .LC70, 0x3f6b851f
	.literal .LC71, 0x40c90fdb
	.literal .LC72, 0x3f8ccccd
	.literal .LC73, 0x3e99999a
	.literal .LC74, 0x3e800000
	.literal .LC75, 0x3ee66666
	.literal .LC76, 0x47800000
	.literal .LC77, _ZN12_GLOBAL__N_112lastThemeGenE
	.align	4
	.type	_ZN12_GLOBAL__N_14initEii, @function
_ZN12_GLOBAL__N_14initEii:
	entry	sp, 64	#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:62:     if (stars == nullptr) {
	l32r	a4, .LC49	#, tmp204
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:115:     return true;
	movi.n	a5, 1	# <retval>,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:62:     if (stars == nullptr) {
	l32i.n	a6, a4, 0	# stars, stars
	bnez.n	a6, .L70	# stars,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:63:         stars = static_cast<Star *>(alloc(MAX_STARS * sizeof(Star)));
	l32r	a10, .LC50	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:63:         stars = static_cast<Star *>(alloc(MAX_STARS * sizeof(Star)));
	s32i.n	a10, a4, 0	# stars,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:64:         draws = static_cast<StarDraw *>(alloc(MAX_STARS * sizeof(StarDraw)));
	l32r	a10, .LC51	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:64:         draws = static_cast<StarDraw *>(alloc(MAX_STARS * sizeof(StarDraw)));
	l32r	a5, .LC52	#, tmp209
	s32i.n	a10, a5, 0	# draws,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:65:         starY = static_cast<int16_t *>(alloc(MAX_STARS * sizeof(int16_t)));
	movi	a10, 0x320	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:65:         starY = static_cast<int16_t *>(alloc(MAX_STARS * sizeof(int16_t)));
	l32r	a6, .LC53	#, tmp210
	s32i.n	a10, a6, 0	# starY,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:66:         bandHead = static_cast<int16_t *>(alloc(NUM_BANDS * sizeof(int16_t)));
	movi.n	a10, 0x3c	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:66:         bandHead = static_cast<int16_t *>(alloc(NUM_BANDS * sizeof(int16_t)));
	l32r	a7, .LC54	#, tmp211
	s32i.n	a10, a7, 0	# bandHead,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:67:         bandNext = static_cast<int16_t *>(alloc(MAX_STARS * sizeof(int16_t)));
	movi	a10, 0x320	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:67:         bandNext = static_cast<int16_t *>(alloc(MAX_STARS * sizeof(int16_t)));
	l32r	a8, .LC55	#,
	s32i.n	a10, a8, 0	# bandNext,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:68:         driftQ = static_cast<int32_t *>(alloc(MAX_STARS * sizeof(int32_t)));
	movi	a10, 0x640	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:68:         driftQ = static_cast<int32_t *>(alloc(MAX_STARS * sizeof(int32_t)));
	l32r	a8, .LC56	#,
	s32i.n	a10, a8, 0	# driftQ,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:69:         dx2 = static_cast<int32_t *>(alloc(w * sizeof(int32_t)));
	slli	a10, a2, 2	#, w,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:69:         dx2 = static_cast<int32_t *>(alloc(w * sizeof(int32_t)));
	l32r	a8, .LC57	#,
	s32i.n	a10, a8, 0	# dx2,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:70:         dy2 = static_cast<int32_t *>(alloc(h * sizeof(int32_t)));
	slli	a10, a3, 2	#, h,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:70:         dy2 = static_cast<int32_t *>(alloc(h * sizeof(int32_t)));
	l32r	a8, .LC58	#,
	s32i.n	a10, a8, 0	# dy2,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:71:         vigLUT = static_cast<uint8_t *>(alloc(128));
	movi	a10, 0x80	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:71:         vigLUT = static_cast<uint8_t *>(alloc(128));
	l32r	a8, .LC59	#,
	s32i.n	a10, a8, 0	# vigLUT,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:72:         starCol = static_cast<uint8_t *>(alloc(MAX_STARS * 3));
	movi	a10, 0x4b0	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:72:         starCol = static_cast<uint8_t *>(alloc(MAX_STARS * 3));
	l32r	a8, .LC60	#,
	s32i.n	a10, a8, 0	# starCol,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:73:         vigColor = static_cast<uint16_t *>(alloc(128 * sizeof(uint16_t)));
	movi	a10, 0x100	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:73:         vigColor = static_cast<uint16_t *>(alloc(128 * sizeof(uint16_t)));
	l32r	a8, .LC61	#, tmp220
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:74:         if (stars == nullptr || draws == nullptr || starY == nullptr || bandHead == nullptr || bandNext == nullptr ||
	l32i.n	a4, a4, 0	# stars, stars$74_24
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:73:         vigColor = static_cast<uint16_t *>(alloc(128 * sizeof(uint16_t)));
	s32i.n	a10, a8, 0	# vigColor, _23
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:74:         if (stars == nullptr || draws == nullptr || starY == nullptr || bandHead == nullptr || bandNext == nullptr ||
	beqz.n	a4, .L93	# stars$74_24,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:74:         if (stars == nullptr || draws == nullptr || starY == nullptr || bandHead == nullptr || bandNext == nullptr ||
	l32i.n	a5, a5, 0	# draws, draws
	beqz.n	a5, .L70	# draws,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:74:         if (stars == nullptr || draws == nullptr || starY == nullptr || bandHead == nullptr || bandNext == nullptr ||
	l32i.n	a5, a6, 0	# starY, starY$76_26
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:74:         if (stars == nullptr || draws == nullptr || starY == nullptr || bandHead == nullptr || bandNext == nullptr ||
	beqz.n	a5, .L70	# starY$76_26,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:74:         if (stars == nullptr || draws == nullptr || starY == nullptr || bandHead == nullptr || bandNext == nullptr ||
	l32i.n	a6, a7, 0	# bandHead, bandHead
	beqz.n	a6, .L93	# bandHead,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:74:         if (stars == nullptr || draws == nullptr || starY == nullptr || bandHead == nullptr || bandNext == nullptr ||
	l32r	a8, .LC55	#,
	l32i.n	a6, a8, 0	# bandNext, bandNext
	beqz.n	a6, .L93	# bandNext,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:75:             driftQ == nullptr || dx2 == nullptr || dy2 == nullptr || vigLUT == nullptr || starCol == nullptr ||
	l32r	a6, .LC56	#,
	l32i.n	a6, a6, 0	# driftQ,
	s32i.n	a6, sp, 12	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:74:         if (stars == nullptr || draws == nullptr || starY == nullptr || bandHead == nullptr || bandNext == nullptr ||
	beqz.n	a6, .L93	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:75:             driftQ == nullptr || dx2 == nullptr || dy2 == nullptr || vigLUT == nullptr || starCol == nullptr ||
	l32r	a6, .LC57	#,
	l32i.n	a8, a6, 0	# dx2, dx2$80_30
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:75:             driftQ == nullptr || dx2 == nullptr || dy2 == nullptr || vigLUT == nullptr || starCol == nullptr ||
	beqz.n	a8, .L93	# dx2$80_30,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:75:             driftQ == nullptr || dx2 == nullptr || dy2 == nullptr || vigLUT == nullptr || starCol == nullptr ||
	l32r	a6, .LC58	#,
	l32i.n	a11, a6, 0	# dy2, dy2$81_31
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:75:             driftQ == nullptr || dx2 == nullptr || dy2 == nullptr || vigLUT == nullptr || starCol == nullptr ||
	beqz.n	a11, .L93	# dy2$81_31,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:75:             driftQ == nullptr || dx2 == nullptr || dy2 == nullptr || vigLUT == nullptr || starCol == nullptr ||
	l32r	a6, .LC59	#,
	l32i.n	a7, a6, 0	# vigLUT, vigLUT$82_32
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:75:             driftQ == nullptr || dx2 == nullptr || dy2 == nullptr || vigLUT == nullptr || starCol == nullptr ||
	beqz.n	a7, .L93	# vigLUT$82_32,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:75:             driftQ == nullptr || dx2 == nullptr || dy2 == nullptr || vigLUT == nullptr || starCol == nullptr ||
	l32r	a6, .LC60	#,
	movi.n	a12, 1	# tmp237,
	l32i.n	a9, a6, 0	# starCol, starCol
	movi.n	a13, 0	# tmp238,
	moveqz	a13, a12, a9	# tmp238, tmp237, starCol
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:75:             driftQ == nullptr || dx2 == nullptr || dy2 == nullptr || vigLUT == nullptr || starCol == nullptr ||
	extui	a9, a13, 0, 8	# tmp239, tmp236
	bnez.n	a9, .L93	# tmp239,
	moveqz	a9, a12, a10	# tmp244, tmp237, _23
	bnez.n	a9, .L93	# tmp244,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:79:         const float cx = w * 0.5f, cy = h * 0.5f;
	float.s	f0, a2, 0	#, w
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:79:         const float cx = w * 0.5f, cy = h * 0.5f;
	float.s	f1, a3, 0	#, h
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:79:         const float cx = w * 0.5f, cy = h * 0.5f;
	l32r	a6, .LC62	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:79:         const float cx = w * 0.5f, cy = h * 0.5f;
	ssi	f1, sp, 8	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:79:         const float cx = w * 0.5f, cy = h * 0.5f;
	wfr	f3, a6	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:79:         const float cx = w * 0.5f, cy = h * 0.5f;
	ssi	f0, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:79:         const float cx = w * 0.5f, cy = h * 0.5f;
	mul.s	f2, f0, f3	# cx,,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:79:         const float cx = w * 0.5f, cy = h * 0.5f;
	mul.s	f1, f1, f3	# cy,,
	mov.n	a10, a8	# ivtmp$174, dx2$80_30
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:80:         for (int x = 0; x < w; x++) {
	bge	a2, a12, .L75	# w,,
.L74:
	mov.n	a9, a11	# ivtmp$171, dy2$81_31
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:84:         for (int y = 0; y < h; y++) {
	movi.n	a8, 0	# y,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:84:         for (int y = 0; y < h; y++) {
	bgei	a3, 1, .L77	# h,,
.L76:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:88:         const float maxR2 = cx * cx + cy * cy;
	mul.s	f1, f1, f1	# tmp247, cy, cy
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:89:         for (int i = 0; i < 128; i++) {
	movi.n	a6, 0	# i,
	l32r	a3, .LC65	#, tmp344
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:91:             const float rn = sqrtf(i / 127.0f * maxR2) / sqrtf(maxR2);
	l32r	a2, .LC63	#, tmp370
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:88:         const float maxR2 = cx * cx + cy * cy;
	madd.s	f1, f2, f2	# tmp247, cx, cx
	ssi	f1, sp, 0	# %sfp, tmp247
	j	.L73		#
.L75:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:81:             const float d = x - cx;
	float.s	f0, a9, 0	# tmp248, x
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:81:             const float d = x - cx;
	addi.n	a9, a9, 1	# x, x,
	sub.s	f0, f0, f2	# d, tmp248, cx
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:82:             dx2[x] = static_cast<int32_t>(d * d);
	mul.s	f0, f0, f0	# tmp249, d, d
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:82:             dx2[x] = static_cast<int32_t>(d * d);
	trunc.s	a8, f0, 0	# tmp250, tmp249
	s32i.n	a8, a10, 0	# MEM[base: _410, offset: 0B], tmp250
	addi.n	a10, a10, 4	# ivtmp$174, ivtmp$174,
	addi.n	a2, a2, -1	# w, w,
	bnez.n	a2, .L75	# w,
	j	.L74		#
.L77:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:85:             const float d = y - cy;
	float.s	f0, a8, 0	# tmp251, y
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:85:             const float d = y - cy;
	addi.n	a8, a8, 1	# y, y,
	sub.s	f0, f0, f1	# d, tmp251, cy
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:86:             dy2[y] = static_cast<int32_t>(d * d);
	mul.s	f0, f0, f0	# tmp252, d, d
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:86:             dy2[y] = static_cast<int32_t>(d * d);
	trunc.s	a2, f0, 0	# tmp253, tmp252
	s32i.n	a2, a9, 0	# MEM[base: _414, offset: 0B], tmp253
	addi.n	a9, a9, 4	# ivtmp$171, ivtmp$171,
	addi.n	a3, a3, -1	# h, h,
	bnez.n	a3, .L77	# h,
	j	.L76		#
.L73:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:91:             const float rn = sqrtf(i / 127.0f * maxR2) / sqrtf(maxR2);
	float.s	f0, a6, 0	#, i
	mov.n	a11, a2	#, tmp370
	rfr	a10, f0	#,
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:91:             const float rn = sqrtf(i / 127.0f * maxR2) / sqrtf(maxR2);
	lsi	f2, sp, 0	# %sfp,
	wfr	f1, a10	#,
	mul.s	f1, f1, f2	#,,
	rfr	a10, f1	#,
	call8	sqrtf		#
	wfr	f0, a10	# _63,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:91:             const float rn = sqrtf(i / 127.0f * maxR2) / sqrtf(maxR2);
	l32i.n	a10, sp, 0	# %sfp,
	ssi	f0, sp, 16	#,
	call8	sqrtf		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:91:             const float rn = sqrtf(i / 127.0f * maxR2) / sqrtf(maxR2);
	lsi	f0, sp, 16	#,
	mov.n	a11, a10	#,
	rfr	a10, f0	#, _63
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:92:             vigLUT[i] = static_cast<uint8_t>(fmaxf(0.0f, 1.0f - rn) * 255.0f);
	l32r	a8, .LC64	#,
	wfr	f15, a10	#,
	wfr	f3, a8	#,
	sub.s	f15, f3, f15	#,,
	mov.n	a10, a3	#, tmp344
	rfr	a11, f15	#,
	call8	fmaxf		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:92:             vigLUT[i] = static_cast<uint8_t>(fmaxf(0.0f, 1.0f - rn) * 255.0f);
	l32r	a8, .LC66	#,
	wfr	f1, a10	#,
	wfr	f2, a8	#,
	mul.s	f0, f1, f2	# tmp262,,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:92:             vigLUT[i] = static_cast<uint8_t>(fmaxf(0.0f, 1.0f - rn) * 255.0f);
	add.n	a10, a7, a6	# tmp261, vigLUT$82_32, i
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:89:         for (int i = 0; i < 128; i++) {
	addi.n	a6, a6, 1	# i, i,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:92:             vigLUT[i] = static_cast<uint8_t>(fmaxf(0.0f, 1.0f - rn) * 255.0f);
	utrunc.s	a9, f0, 0	# tmp264, tmp262
	s8i	a9, a10, 0	# MEM[base: _418, offset: 0B], tmp264
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:89:         for (int i = 0; i < 128; i++) {
	bnei	a6, 128, .L73	# i,,
	l32r	a6, .LC69	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:102:                                       : layer == 1 ? (0.45f + nextRandf(rng) * 0.25f)
	l32r	a2, .LC74	#,
	l32r	a3, .LC68	#,
	wfr	f6, a6	# tmp347,
	l32r	a6, .LC73	#,
	wfr	f12, a2	# tmp367,
	wfr	f4, a3	# tmp346,
	l32r	a2, .LC71	#,
	l32r	a3, .LC72	#,
	l32r	a10, .LC67	#, tmp352
	wfr	f5, a6	# tmp350,
	l32r	a6, .LC75	#,
	wfr	f10, a2	# tmp348,
	wfr	f9, a3	# tmp349,
	l32r	a2, .LC76	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:96:             const uint8_t layer = roll < 0.7f ? 0 : (roll < 0.92f ? 1 : 2);
	l32r	a3, .LC70	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:102:                                       : layer == 1 ? (0.45f + nextRandf(rng) * 0.25f)
	wfr	f13, a6	# tmp368,
	l32i.n	a8, a10, 0	# rng, rng_lsm$154
	l32i.n	a6, sp, 12	# %sfp, ivtmp$162
	wfr	f8, a2	# tmp351,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:96:             const uint8_t layer = roll < 0.7f ? 0 : (roll < 0.92f ? 1 : 2);
	wfr	f11, a3	# tmp364,
	movi.n	a11, 0	# iftmp$91_130,
	movi.n	a13, 1	# tmp365,
	movi.n	a12, 2	# tmp366,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:101:             stars[i].baseBrightness = layer == 0 ? (0.25f + nextRandf(rng) * 0.25f)
	mov.s	f7, f12	# tmp369, tmp367
	movi	a9, 0x190	# tmp371,
	loop	a9, .L82_LEND	# tmp371,
.L82:
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a2, a8, 13	# tmp266, rng_lsm$154,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a2, a2, a8	# _77, tmp266, rng_lsm$154
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a8, a2, 17, 15	# tmp267, _77,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a8, a8, a2	# _79, tmp267, _77
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a2, a8, 5	# tmp268, _79,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a8, a2, a8	# _81, tmp268, _79
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a2, a8, 13	# tmp274, _81,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a3, a2, a8	# _86, tmp274, _81
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a2, a3, 17, 15	# tmp275, _86,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a2, a2, a3	# _88, tmp275, _86
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a3, a2, 5	# tmp276, _88,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a2, a3, a2	# _90, tmp276, _88
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a3, a2, 13	# tmp281, _90,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a3, a3, a2	# _100, tmp281, _90
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a14, a3, 17, 15	# tmp282, _100,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a3, a14, a3	# _102, tmp282, _100
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a14, a3, 5	# tmp283, _102,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a14, a14, a3	# _104, tmp283, _102
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a3, a14, 13	# tmp288, _104,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a3, a3, a14	# _110, tmp288, _104
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a7, a3, 17, 15	# tmp289, _110,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a3, a7, a3	# _112, tmp289, _110
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a7, a3, 5	# tmp290, _112,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a7, a7, a3	# _114, tmp290, _112
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a3, a7, 13	# tmp297, _114,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a3, a3, a7	# _120, tmp297, _114
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a15, a3, 17, 15	# tmp298, _120,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a3, a15, a3	# _122, tmp298, _120
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a2, a2, 8	# tmp277, _90,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a15, a3, 5	# tmp299, _122,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f0, a2, 0	# tmp278, tmp277
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a15, a15, a3	# _124, tmp299, _122
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a7, a7, 8	# tmp291, _114,
	srli	a14, a14, 8	# tmp284, _104,
	srli	a8, a8, 8	# tmp269, _81,
	srli	a3, a15, 8	# tmp300, _124,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f2, a7, 0	# tmp292, tmp291
	float.s	f1, a14, 0	# tmp285, tmp284
	slli	a7, a15, 13	# tmp307, _124,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f0, f0, f4	# tmp279, tmp278, tmp346
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f3, a8, 0	# tmp270, tmp269
	xor	a7, a7, a15	# _467, tmp307, _124
	float.s	f14, a3, 0	# tmp301, tmp300
	extui	a15, a7, 17, 15	# tmp308, _467,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:97:             stars[i].x = nextRandf(rng) * w;
	lsi	f15, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f1, f1, f4	# tmp286, tmp285, tmp346
	mul.s	f3, f3, f4	# _84, tmp270, tmp346
	mul.s	f14, f14, f4	# tmp302, tmp301, tmp346
	xor	a7, a15, a7	# _469, tmp308, _467
	mul.s	f2, f2, f4	# tmp293, tmp292, tmp346
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:97:             stars[i].x = nextRandf(rng) * w;
	mul.s	f0, f0, f15	# _98, tmp279,
	slli	a15, a7, 5	# tmp309, _469,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:98:             stars[i].y = nextRandf(rng) * h;
	lsi	f15, sp, 8	# %sfp,
	xor	a15, a15, a7	# _471, tmp309, _469
	srli	a2, a15, 8	# tmp310, _471,
	mul.s	f1, f1, f15	# _108, tmp286,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:96:             const uint8_t layer = roll < 0.7f ? 0 : (roll < 0.92f ? 1 : 2);
	olt.s	b0, f3, f6	#, _84, tmp347
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:100:             stars[i].rate = 0.3f + nextRandf(rng) * 1.1f;
	mov.s	f15, f5	# tmp304, tmp350
	madd.s	f15, f14, f9	# tmp304, tmp302, tmp349
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:99:             stars[i].phase = nextRandf(rng) * 6.2831853f;
	mul.s	f2, f2, f10	# tmp295, tmp293, tmp348
	float.s	f14, a2, 0	# tmp311, tmp310
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:96:             const uint8_t layer = roll < 0.7f ? 0 : (roll < 0.92f ? 1 : 2);
	mov.n	a2, a11	# iftmp$91_130, iftmp$91_130
	bt	b0, .L78	#,
	olt.s	b0, f3, f11	#, _84, tmp364
	mov.n	a2, a12	# iftmp$91_130, tmp366
	movt	a2, a13, b0	# iftmp$91_130, tmp365,
.L78:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:97:             stars[i].x = nextRandf(rng) * w;
	ssi	f0, a4, 0	# MEM[base: _431, offset: 0B], _98
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:98:             stars[i].y = nextRandf(rng) * h;
	ssi	f1, a4, 4	# MEM[base: _431, offset: 4B], _108
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:99:             stars[i].phase = nextRandf(rng) * 6.2831853f;
	ssi	f2, a4, 8	# MEM[base: _431, offset: 8B], tmp295
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:100:             stars[i].rate = 0.3f + nextRandf(rng) * 1.1f;
	ssi	f15, a4, 12	# MEM[base: _431, offset: 12B], tmp304
	mul.s	f14, f14, f4	# _475, tmp311, tmp346
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:101:             stars[i].baseBrightness = layer == 0 ? (0.25f + nextRandf(rng) * 0.25f)
	bnez.n	a2, .L79	# iftmp$91_130,
	mov.s	f2, f7	#, tmp369
	madd.s	f2, f14, f7	#, _475, tmp369
	mov.s	f14, f2	# iftmp$101_141,
	j	.L80		#
.L79:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:102:                                       : layer == 1 ? (0.45f + nextRandf(rng) * 0.25f)
	bnei	a2, 1, .L81	# iftmp$91_130,,
	mov.s	f3, f13	#, tmp368
	madd.s	f3, f14, f12	#, _475, tmp367
	mov.s	f14, f3	# iftmp$101_141,
	j	.L80		#
.L81:
	mov.s	f15, f6	#, tmp347
	madd.s	f15, f14, f5	#, _475, tmp350
	mov.s	f14, f15	# iftmp$101_141,
.L80:
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a7, a15, 13	# tmp319, _471,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a15, a7, a15	# _167, tmp319, _471
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a7, a15, 17, 15	# tmp320, _167,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a15, a7, a15	# _169, tmp320, _167
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a7, a15, 5	# tmp321, _169,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a7, a7, a15	# _171, tmp321, _169
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a3, a7, 13	# tmp326, _171,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a3, a3, a7	# _176, tmp326, _171
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a8, a3, 17, 15	# tmp327, _176,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a3, a8, a3	# _178, tmp327, _176
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a8, a3, 5	# tmp328, _178,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a8, a8, a3	# rng_lsm$154, tmp328, _178
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a3, a8, 8	# tmp329, rng_lsm$154,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f3, a3, 0	# tmp330, tmp329
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a7, a7, 8	# tmp322, _171,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:106:             stars[i].driftSpeed = 0.5f + nextRandf(rng);
	l32r	a3, .LC62	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f2, a7, 0	# tmp323, tmp322
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:106:             stars[i].driftSpeed = 0.5f + nextRandf(rng);
	wfr	f15, a3	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f2, f2, f4	# tmp324, tmp323, tmp346
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:106:             stars[i].driftSpeed = 0.5f + nextRandf(rng);
	madd.s	f15, f3, f4	#, tmp330, tmp346
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:110:             driftQ[i] = static_cast<int32_t>(stars[i].x * 65536.0f);
	mul.s	f0, f0, f8	# tmp335, _98, tmp351
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:107:             starY[i] = static_cast<int16_t>(stars[i].y);
	trunc.s	a3, f1, 0	# tmp334, _108
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:104:             stars[i].sizeClass = layer;
	s8i	a2, a4, 20	# MEM[base: _431, offset: 20B], iftmp$91_130
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:101:             stars[i].baseBrightness = layer == 0 ? (0.25f + nextRandf(rng) * 0.25f)
	ssi	f14, a4, 16	# MEM[base: _431, offset: 16B], iftmp$101_141
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:105:             stars[i].hue = nextRandf(rng);
	ssi	f2, a4, 24	# MEM[base: _431, offset: 24B], tmp324
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:106:             stars[i].driftSpeed = 0.5f + nextRandf(rng);
	ssi	f15, a4, 28	# MEM[base: _431, offset: 28B], tmp331
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:110:             driftQ[i] = static_cast<int32_t>(stars[i].x * 65536.0f);
	trunc.s	a2, f0, 0	# tmp337, tmp335
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:107:             starY[i] = static_cast<int16_t>(stars[i].y);
	s16i	a3, a5, 0	# MEM[base: _423, offset: 0B], tmp334
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:110:             driftQ[i] = static_cast<int32_t>(stars[i].x * 65536.0f);
	s32i.n	a2, a6, 0	# MEM[base: _422, offset: 0B], tmp337
	addi	a4, a4, 32	# ivtmp$160, ivtmp$160,
	addi.n	a5, a5, 2	# ivtmp$161, ivtmp$161,
	addi.n	a6, a6, 4	# ivtmp$162, ivtmp$162,
	.L82_LEND:		#
	s32i.n	a8, a10, 0	# rng, rng_lsm$154
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:112:         rebuildThemeAssets();
	call8	_ZN12_GLOBAL__N_118rebuildThemeAssetsEv		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:113:         lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:113:         lastThemeGen = themeGen();
	l32r	a2, .LC77	#, tmp339
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:115:     return true;
	movi.n	a5, 1	# <retval>,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:113:         lastThemeGen = themeGen();
	s32i.n	a10, a2, 0	# lastThemeGen,
	j	.L70		#
.L93:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:77:             return false;
	movi.n	a5, 0	# <retval>,
.L70:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:116: }
	mov.n	a2, a5	#, <retval>
	retw.n
	.size	_ZN12_GLOBAL__N_14initEii, .-_ZN12_GLOBAL__N_14initEii
	.section	.text._ZN12_GLOBAL__N_14bandEPtiiijPKh,"ax",@progbits
	.literal_position
	.literal .LC78, _ZN12_GLOBAL__N_13dy2E
	.literal .LC79, _ZN12_GLOBAL__N_13dx2E
	.literal .LC80, _ZN12_GLOBAL__N_18vigColorE
	.literal .LC81, _ZN12_GLOBAL__N_15shootE
	.literal .LC82, _ZN12_GLOBAL__N_18bandHeadE
	.literal .LC83, _ZN12_GLOBAL__N_15drawsE
	.literal .LC84, _ZN12_GLOBAL__N_15starYE
	.literal .LC85, _ZN12_GLOBAL__N_18bandNextE
	.literal .LC86, -858993459
	.literal .LC88, 0x3ca3d70a
	.literal .LC89, 0x3e99999a
	.literal .LC90, 0x3f800000
	.literal .LC91, 0x3d924925
	.literal .LC92, _ZN12_GLOBAL__N_18shootColE
	.literal .LC93, 0x00000000
	.literal .LC94, 0x437f0000
	.align	4
	.type	_ZN12_GLOBAL__N_14bandEPtiiijPKh, @function
_ZN12_GLOBAL__N_14bandEPtiiijPKh:
	entry	sp, 80	#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:244: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
	s32i.n	a4, sp, 32	# %sfp, rows
	s32i.n	a2, sp, 16	# %sfp, dst
	s32i.n	a3, sp, 0	# %sfp, y0
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:246:     for (int ry = 0; ry < rows; ry++) {
	bgei	a4, 1, .L105	# rows,,
	add.n	a15, a4, a3	# tmp713, rows, y0
.L109:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:260:     int bandLo = (y0 >> 4) - 1, bandHi = ((y0 + rows - 1) >> 4) + 1;
	l32i.n	a3, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:260:     int bandLo = (y0 >> 4) - 1, bandHi = ((y0 + rows - 1) >> 4) + 1;
	addi.n	a2, a15, -1	# tmp333, tmp713,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:260:     int bandLo = (y0 >> 4) - 1, bandHi = ((y0 + rows - 1) >> 4) + 1;
	srai	a4, a3, 4	# tmp331,,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:260:     int bandLo = (y0 >> 4) - 1, bandHi = ((y0 + rows - 1) >> 4) + 1;
	srai	a2, a2, 4	# tmp334, tmp333,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:260:     int bandLo = (y0 >> 4) - 1, bandHi = ((y0 + rows - 1) >> 4) + 1;
	addi.n	a4, a4, -1	# bandLo, tmp331,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:260:     int bandLo = (y0 >> 4) - 1, bandHi = ((y0 + rows - 1) >> 4) + 1;
	addi.n	a2, a2, 1	# bandHi, tmp334,
	movi.n	a9, 0	# tmp335,
	movi.n	a6, 0x1d	# tmp336,
	max	a4, a4, a9	# bandLo, bandLo, tmp335
	min	a2, a2, a6	# bandHi, bandHi, tmp336
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:267:     for (int b = bandLo; b <= bandHi; b++) {
	bge	a2, a4, .L155	# bandHi, bandLo,
	j	.L106		#
.L105:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:251:             const int32_t r2 = dyv + dx2[x];
	l32r	a3, .LC79	#, tmp338
	l32i.n	a9, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:248:         const int32_t dyv = dy2[y];
	l32r	a2, .LC78	#, tmp337
	l32i.n	a6, sp, 32	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:251:             const int32_t r2 = dyv + dx2[x];
	l32i.n	a14, a3, 0	# dx2, dx2$4_8
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:256:             row[x] = vigColor[idx];
	l32r	a3, .LC80	#, tmp339
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:248:         const int32_t dyv = dy2[y];
	l32i.n	a12, a2, 0	# dy2, dy2$0_1
	add.n	a15, a6, a9	# tmp713,,
	slli	a10, a9, 2	# tmp340,,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:256:             row[x] = vigColor[idx];
	l32i.n	a9, a3, 0	# vigColor, vigColor$6_13
	l32i.n	a3, sp, 16	# %sfp,
	slli	a13, a5, 1	# _388, w,
	slli	a2, a15, 2	# tmp342, tmp713,
	add.n	a10, a12, a10	# ivtmp$190, dy2$0_1, tmp340
	add.n	a6, a3, a13	# ivtmp$192,, _388
	add.n	a12, a12, a2	# _348, dy2$0_1, tmp342
	neg	a11, a13	# tmp346, tmp345
	movi	a8, 0x7f	# tmp780,
.L110:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:248:         const int32_t dyv = dy2[y];
	l32i.n	a7, a10, 0	# MEM[base: _375, offset: 0B], dyv
	mov.n	a4, a14	# ivtmp$186, dx2$4_8
	add.n	a3, a11, a6	# ivtmp$187, tmp346, ivtmp$192
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:250:         for (int x = 0; x < w; x++) {
	bgei	a5, 1, .L112	# w,,
.L111:
	addi.n	a10, a10, 4	# ivtmp$190, ivtmp$190,
	add.n	a6, a6, a13	# ivtmp$192, ivtmp$192, _388
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:246:     for (int ry = 0; ry < rows; ry++) {
	bne	a12, a10, .L110	# _348, ivtmp$190,
	j	.L109		#
.L112:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:251:             const int32_t r2 = dyv + dx2[x];
	l32i.n	a2, a4, 0	# MEM[base: _403, offset: 0B], MEM[base: _403, offset: 0B]
	addi.n	a4, a4, 4	# ivtmp$186, ivtmp$186,
	add.n	a2, a7, a2	# r2, dyv, MEM[base: _403, offset: 0B]
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:252:             int idx = r2 >> 10; // 480x480: max r2 ~115200 -> 112
	srai	a2, a2, 10	# idx, r2,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:256:             row[x] = vigColor[idx];
	min	a2, a2, a8	# idx, idx, tmp780
	slli	a2, a2, 1	# tmp351, idx,
	add.n	a2, a9, a2	# tmp352, vigColor$6_13, tmp351
	l16ui	a2, a2, 0	# *_16, _18
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:256:             row[x] = vigColor[idx];
	s16i	a2, a3, 0	# MEM[base: _402, offset: 0B], _18
	addi.n	a3, a3, 2	# ivtmp$187, ivtmp$187,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:250:         for (int x = 0; x < w; x++) {
	bne	a6, a3, .L112	# ivtmp$192, ivtmp$187,
	j	.L111		#
.L106:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:283:     if (shoot.active) {
	l32r	a2, .LC81	#, tmp353
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:283:     if (shoot.active) {
	l8ui	a3, a2, 0	# shoot.active, shoot.active
	bnez.n	a3, .L113	# shoot.active,
	j	.L104		#
.L155:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:269:             const StarDraw &d = draws[i];
	l32r	a8, .LC83	#, tmp356
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:268:         for (int16_t i = bandHead[b]; i >= 0; i = bandNext[i]) {
	l32r	a6, .LC82	#, tmp355
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:269:             const StarDraw &d = draws[i];
	l32i.n	a8, a8, 0	# draws,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:268:         for (int16_t i = bandHead[b]; i >= 0; i = bandNext[i]) {
	l32i.n	a6, a6, 0	# bandHead, bandHead$9_23
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:269:             const StarDraw &d = draws[i];
	s32i.n	a8, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:270:             const int y = starY[i] - y0;
	l32r	a8, .LC84	#, tmp357
	slli	a4, a4, 1	# tmp359, bandLo,
	l32i.n	a8, a8, 0	# starY,
	addi.n	a7, a6, 2	# tmp360, bandHead$9_23,
	s32i.n	a8, sp, 8	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:268:         for (int16_t i = bandHead[b]; i >= 0; i = bandNext[i]) {
	l32r	a8, .LC85	#, tmp358
	slli	a2, a2, 1	# tmp361, bandHi,
	l32i.n	a3, a8, 0	# bandNext, bandNext$13_83
	add.n	a4, a6, a4	#, bandHead$9_23, tmp359
	add.n	a2, a7, a2	#, tmp360, tmp361
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	l32i.n	a10, sp, 32	# %sfp, rows
	s32i.n	a4, sp, 20	# %sfp,
	s32i.n	a2, sp, 28	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	movi.n	a4, 1	# tmp771,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	s32i.n	a3, sp, 12	# %sfp, bandNext$13_83
.L116:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:268:         for (int16_t i = bandHead[b]; i >= 0; i = bandNext[i]) {
	l32i.n	a6, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	l32r	a9, .LC86	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:268:         for (int16_t i = bandHead[b]; i >= 0; i = bandNext[i]) {
	l16si	a2, a6, 0	# MEM[base: _417, offset: 0B], i
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	s32i.n	a9, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:268:         for (int16_t i = bandHead[b]; i >= 0; i = bandNext[i]) {
	bgez	a2, .L115	# i,
.L130:
	l32i.n	a11, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:267:     for (int b = bandLo; b <= bandHi; b++) {
	l32i.n	a13, sp, 28	# %sfp,
	addi.n	a11, a11, 2	#,,
	s32i.n	a11, sp, 20	# %sfp,
	bne	a11, a13, .L116	#,,
	j	.L106		#
.L115:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:269:             const StarDraw &d = draws[i];
	slli	a7, a2, 1	# tmp364, i,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:270:             const int y = starY[i] - y0;
	l32i.n	a6, sp, 8	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:269:             const StarDraw &d = draws[i];
	add.n	a2, a7, a2	# tmp365, tmp364, i
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:269:             const StarDraw &d = draws[i];
	l32i.n	a15, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:269:             const StarDraw &d = draws[i];
	slli	a2, a2, 1	# tmp366, tmp365,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:270:             const int y = starY[i] - y0;
	add.n	a3, a6, a7	# tmp367,, tmp364
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:269:             const StarDraw &d = draws[i];
	add.n	a2, a15, a2	# d,, tmp366
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:270:             const int y = starY[i] - y0;
	l32i.n	a9, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:270:             const int y = starY[i] - y0;
	l16si	a3, a3, 0	# *_32, *_32
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:271:             const int x = d.x;
	l16si	a8, a2, 0	# d_160->x, _35
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:270:             const int y = starY[i] - y0;
	sub	a3, a3, a9	# y, *_32,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	extui	a6, a8, 31, 1	# tmp372, _35,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	mov.n	a9, a4	# tmp373, tmp771
	bge	a8, a5, .L117	# _35, w,
	movi.n	a9, 0	# tmp373,
.L117:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	or	a6, a6, a9	# tmp376, tmp372, tmp373
	extui	a6, a6, 0, 8	# _193, tmp376
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bnez.n	a6, .L118	# _193,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bltz	a3, .L118	# y,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bge	a3, a10, .L118	# y, rows,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	mull	a9, a3, a5	# tmp384, y, w
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:272:             plotMax(dst, rows, w, x, y, d.r, d.g, d.b);
	l8ui	a14, a2, 2	# d_160->r, d_160->r
	l8ui	a12, a2, 3	# d_160->g, d_160->g
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	l32i.n	a11, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a15, -0x800	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	add.n	a9, a9, a8	# tmp385, tmp384, _35
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a14, a14, 8	# tmp388, d_160->r,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a12, a12, 3	# tmp393, d_160->g,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a14, a14, a15	# tmp391, tmp388,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	slli	a9, a9, 1	# tmp386, tmp385,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:272:             plotMax(dst, rows, w, x, y, d.r, d.g, d.b);
	l8ui	a13, a2, 4	# d_160->b, d_160->b
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a15, 0x7e0	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	add.n	a9, a11, a9	# px,, tmp386
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a11, a12, a15	# tmp396, tmp393,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a11, a14, a11	# tmp399, tmp391, tmp396
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	srai	a13, a13, 3	# tmp401, d_160->b,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:239:     if (c > px) {
	l16ui	a12, a9, 0	# *px_209, *px_209
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a13, a11, a13	# _223, tmp399, tmp401
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:239:     if (c > px) {
	bgeu	a12, a13, .L118	# *px_209, _223,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:240:         px = c;
	s16i	a13, a9, 0	# *px_209, _223
.L118:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:273:             if (d.sizeClass >= 1) {
	l8ui	a9, a2, 5	# d_160->sizeClass, _42
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:273:             if (d.sizeClass >= 1) {
	beqz.n	a9, .L121	# _42,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	addi.n	a12, a8, 1	# _43, _35,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bltz	a12, .L122	# _43,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bge	a12, a5, .L122	# _43, w,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bltz	a3, .L122	# y,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bge	a3, a10, .L122	# y, rows,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	mull	a13, a3, a5	#, y, w
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	l8ui	a14, a2, 2	# d_160->r, d_160->r
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	l8ui	a11, a2, 3	# d_160->g, d_160->g
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	s32i.n	a13, sp, 36	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	l32i.n	a13, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	slli	a14, a14, 1	# tmp426, d_160->r,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	slli	a11, a11, 1	# tmp444, d_160->g,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	muluh	a14, a14, a13	# tmp693, tmp426,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	muluh	a11, a11, a13	# tmp695, tmp444,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	l32i.n	a13, sp, 36	# %sfp, tmp420
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	l8ui	a15, a2, 4	# d_160->b, d_160->b
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	add.n	a12, a13, a12	# tmp421, tmp420, _43
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	l32i.n	a13, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	slli	a15, a15, 1	# tmp465, d_160->b,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:274:                 plotMax(dst, rows, w, x + 1, y, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	muluh	a15, a15, a13	# tmp697, tmp465,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	l32i.n	a13, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	slli	a12, a12, 1	# tmp422, tmp421,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	add.n	a12, a13, a12	# px,, tmp422
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a14, a14, 6	# tmp437, tmp693,
	movi	a13, -0x800	#,
	and	a14, a14, a13	# tmp440, tmp437,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a11, a11, 1	# tmp455, tmp695,
	movi	a13, 0x7e0	#,
	and	a11, a11, a13	# tmp458, tmp455,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a11, a14, a11	# tmp461, tmp440, tmp458
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	srli	a15, a15, 5	# tmp476, tmp697,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:239:     if (c > px) {
	l16ui	a13, a12, 0	# *px_246, *px_246
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a15, a11, a15	# _260, tmp461, tmp476
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:239:     if (c > px) {
	bgeu	a13, a15, .L122	# *px_246, _260,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:240:         px = c;
	s16i	a15, a12, 0	# *px_246, _260
	l8ui	a9, a2, 5	# d_160->sizeClass, _42
.L122:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:276:             if (d.sizeClass >= 2) {
	bltui	a9, 2, .L121	# _42,,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:277:                 plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	l8ui	a14, a2, 2	# d_160->r, _59
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:277:                 plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	l8ui	a13, a2, 3	# d_160->g, _63
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:277:                 plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	l8ui	a12, a2, 4	# d_160->b, _67
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bnez.n	a6, .L126	# _193,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:277:                 plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	addi.n	a6, a3, 1	# _57, y,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bltz	a6, .L126	# _57,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bge	a6, a10, .L126	# _57, rows,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:277:                 plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	l32r	a15, .LC86	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:277:                 plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	slli	a9, a13, 1	# tmp508, _63,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:277:                 plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	muluh	a9, a9, a15	# tmp701, tmp508,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:277:                 plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	slli	a11, a14, 1	# tmp492, _59,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a9, a9, 1	#, tmp701,
	s32i.n	a9, sp, 40	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:277:                 plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	l32r	a9, .LC86	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:277:                 plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	muluh	a11, a11, a15	# tmp699, tmp492,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:277:                 plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	slli	a15, a12, 1	#, _67,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:277:                 plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	muluh	a9, a15, a9	#,,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	mull	a6, a6, a5	# tmp488, _57, w
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:277:                 plotMax(dst, rows, w, x, y + 1, d.r * 2 / 5, d.g * 2 / 5, d.b * 2 / 5);
	s32i.n	a9, sp, 36	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a11, a11, 6	# tmp503, tmp699,
	movi	a9, -0x800	#,
	and	a11, a11, a9	#, tmp503,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	l32i.n	a15, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	add.n	a6, a6, a8	# tmp489, tmp488, _35
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	s32i.n	a11, sp, 44	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	slli	a6, a6, 1	# tmp490, tmp489,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	l32i.n	a11, sp, 40	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	add.n	a6, a15, a6	# px,, tmp490
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a15, 0x7e0	#,
	and	a9, a11, a15	# tmp522,,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	l32i.n	a11, sp, 44	# %sfp,
	or	a9, a11, a9	# tmp525,, tmp522
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	l32i.n	a11, sp, 36	# %sfp,
	srli	a15, a11, 5	# tmp538,,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:239:     if (c > px) {
	l16ui	a11, a6, 0	# *px_295, *px_295
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a15, a9, a15	# _309, tmp525, tmp538
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:239:     if (c > px) {
	bgeu	a11, a15, .L126	# *px_295, _309,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:240:         px = c;
	s16i	a15, a6, 0	# *px_295, _309
	l8ui	a14, a2, 2	# d_160->r, _59
	l8ui	a13, a2, 3	# d_160->g, _63
	l8ui	a12, a2, 4	# d_160->b, _67
.L126:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:278:                 plotMax(dst, rows, w, x - 1, y, d.r * 3 / 10, d.g * 3 / 10, d.b * 3 / 10);
	addi.n	a8, a8, -1	# _70, _35,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bltz	a8, .L121	# _70,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bge	a8, a5, .L121	# _70, w,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bltz	a3, .L121	# y,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bge	a3, a10, .L121	# y, rows,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:278:                 plotMax(dst, rows, w, x - 1, y, d.r * 3 / 10, d.g * 3 / 10, d.b * 3 / 10);
	l32r	a15, .LC86	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:278:                 plotMax(dst, rows, w, x - 1, y, d.r * 3 / 10, d.g * 3 / 10, d.b * 3 / 10);
	slli	a2, a14, 1	# tmp561, _59,
	add.n	a14, a2, a14	# tmp562, tmp561, _59
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:278:                 plotMax(dst, rows, w, x - 1, y, d.r * 3 / 10, d.g * 3 / 10, d.b * 3 / 10);
	slli	a2, a13, 1	# tmp578, _63,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	mull	a3, a3, a5	# tmp557, y, w
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:278:                 plotMax(dst, rows, w, x - 1, y, d.r * 3 / 10, d.g * 3 / 10, d.b * 3 / 10);
	add.n	a13, a2, a13	# tmp579, tmp578, _63
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:278:                 plotMax(dst, rows, w, x - 1, y, d.r * 3 / 10, d.g * 3 / 10, d.b * 3 / 10);
	slli	a6, a12, 1	# tmp598, _67,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:278:                 plotMax(dst, rows, w, x - 1, y, d.r * 3 / 10, d.g * 3 / 10, d.b * 3 / 10);
	muluh	a14, a14, a15	# tmp705, tmp562,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:278:                 plotMax(dst, rows, w, x - 1, y, d.r * 3 / 10, d.g * 3 / 10, d.b * 3 / 10);
	add.n	a12, a6, a12	# tmp599, tmp598, _67
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:278:                 plotMax(dst, rows, w, x - 1, y, d.r * 3 / 10, d.g * 3 / 10, d.b * 3 / 10);
	muluh	a13, a13, a15	# tmp707, tmp579,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	add.n	a8, a3, a8	# tmp558, tmp557, _70
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a2, -0x800	#,
	slli	a14, a14, 5	# tmp573, tmp705,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:278:                 plotMax(dst, rows, w, x - 1, y, d.r * 3 / 10, d.g * 3 / 10, d.b * 3 / 10);
	muluh	a12, a12, a15	# tmp709, tmp599,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a3, 0x7e0	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	l32i.n	a6, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a14, a14, a2	# tmp576, tmp573,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	slli	a8, a8, 1	# tmp559, tmp558,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a13, a13, a3	# tmp593, tmp707,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	add.n	a8, a6, a8	# px,, tmp559
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a14, a14, a13	# tmp596, tmp576, tmp593
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	srli	a12, a12, 6	# tmp610, tmp709,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a12, a14, a12	# tmp613, tmp596, tmp610
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:239:     if (c > px) {
	l16ui	a2, a8, 0	# *px_320, *px_320
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	extui	a12, a12, 0, 16	# _334, tmp613
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:239:     if (c > px) {
	bgeu	a2, a12, .L121	# *px_320, _334,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:240:         px = c;
	s16i	a12, a8, 0	# *px_320, _334
.L121:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:268:         for (int16_t i = bandHead[b]; i >= 0; i = bandNext[i]) {
	l32i.n	a9, sp, 12	# %sfp,
	add.n	a7, a9, a7	# tmp615,, tmp364
	l16si	a2, a7, 0	# *_84, i
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:268:         for (int16_t i = bandHead[b]; i >= 0; i = bandNext[i]) {
	bgez	a2, .L115	# i,
	j	.L130		#
.L113:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:289:         const float progress = shoot.life * shoot.invMaxLife;
	lsi	f1, a2, 20	# shoot.life, _86
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:289:         const float progress = shoot.life * shoot.invMaxLife;
	lsi	f0, a2, 28	# shoot.invMaxLife, shoot.invMaxLife
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:290:         const float hx = shoot.x + shoot.vx * shoot.life;
	lsi	f4, a2, 12	# shoot.vx, _89
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:289:         const float progress = shoot.life * shoot.invMaxLife;
	mul.s	f0, f1, f0	# progress, _86, shoot.invMaxLife
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:291:         const float hy = shoot.y + shoot.vy * shoot.life;
	lsi	f10, a2, 16	# shoot.vy, _92
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:290:         const float hx = shoot.x + shoot.vx * shoot.life;
	lsi	f5, a2, 4	# shoot.x, shoot.x
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:291:         const float hy = shoot.y + shoot.vy * shoot.life;
	lsi	f13, a2, 8	# shoot.y, shoot.y
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:296:             const float fade = (1.0f - f) * (1.0f - progress * 0.3f);
	l32r	a13, .LC90	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:294:             const float px = hx - shoot.vx * 0.02f * k;
	l32r	a11, .LC88	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:296:             const float fade = (1.0f - f) * (1.0f - progress * 0.3f);
	l32r	a2, .LC89	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:294:             const float px = hx - shoot.vx * 0.02f * k;
	wfr	f2, a11	# tmp624,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:290:         const float hx = shoot.x + shoot.vx * shoot.life;
	madd.s	f5, f1, f4	# hx, _86, _89
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:291:         const float hy = shoot.y + shoot.vy * shoot.life;
	madd.s	f13, f1, f10	# hy, _86, _92
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:296:             const float fade = (1.0f - f) * (1.0f - progress * 0.3f);
	wfr	f8, a13	# _103, tmp710
	wfr	f1, a2	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:294:             const float px = hx - shoot.vx * 0.02f * k;
	mul.s	f4, f4, f2	# _96, _89, tmp624
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:295:             const float py = hy - shoot.vy * 0.02f * k;
	mul.s	f10, f10, f2	# _99, _92, tmp624
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:296:             const float fade = (1.0f - f) * (1.0f - progress * 0.3f);
	msub.s	f8, f0, f1	# _103, progress,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	l32r	a3, .LC94	#,
	l32r	a9, .LC93	#,
	l32r	a6, .LC91	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:292:         for (int k = 0; k < 14; k++) {
	movi.n	a7, 0	# k,
	l32r	a10, .LC92	#, tmp714
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:292:         for (int k = 0; k < 14; k++) {
	l32i.n	a15, sp, 32	# %sfp, rows
	wfr	f3, a9	# tmp712,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:296:             const float fade = (1.0f - f) * (1.0f - progress * 0.3f);
	wfr	f6, a13	# tmp710,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	wfr	f9, a3	# tmp758,
	wfr	f7, a6	# tmp711,
	mov.n	a9, a7	# _447, k
	movi	a12, 0xff	# _447,
	wfr	f12, a3	# tmp760, tmp758
	wfr	f11, a3	# tmp761, tmp758
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	movi	a14, 0x7e0	# tmp763,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:292:         for (int k = 0; k < 14; k++) {
	movi.n	a8, 0xe	# tmp781,
	loop	a8, .L137_LEND	# tmp781,
.L137:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:293:             const float f = k * INV14;
	float.s	f2, a7, 0	# _94, k
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:296:             const float fade = (1.0f - f) * (1.0f - progress * 0.3f);
	mov.s	f1, f6	# tmp631, tmp710
	msub.s	f1, f2, f7	# tmp631, _94, tmp711
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:297:             plotMax(dst, rows, w, static_cast<int>(px), static_cast<int>(py) - y0, clamp8f(shootCol[0] * fade),
	l8ui	a2, a10, 0	# shootCol, shootCol
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:294:             const float px = hx - shoot.vx * 0.02f * k;
	mov.s	f14, f5	# px, hx
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:297:             plotMax(dst, rows, w, static_cast<int>(px), static_cast<int>(py) - y0, clamp8f(shootCol[0] * fade),
	float.s	f0, a2, 0	# tmp637, shootCol
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:296:             const float fade = (1.0f - f) * (1.0f - progress * 0.3f);
	mul.s	f1, f1, f8	# fade, tmp631, _103
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:294:             const float px = hx - shoot.vx * 0.02f * k;
	msub.s	f14, f2, f4	# px, _94, _96
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.n	a6, a9	# _447, _447
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:297:             plotMax(dst, rows, w, static_cast<int>(px), static_cast<int>(py) - y0, clamp8f(shootCol[0] * fade),
	mul.s	f0, f0, f1	# _110, tmp637, fade
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:297:             plotMax(dst, rows, w, static_cast<int>(px), static_cast<int>(py) - y0, clamp8f(shootCol[0] * fade),
	trunc.s	a3, f14, 0	# _104, px
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f0, f3	#, _110, tmp712
	bt	b0, .L131	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f9, f0	#, tmp758, _110
	mov.n	a6, a12	# _447, _447
	bt	b0, .L131	#,
	utrunc.s	a6, f0, 0	# tmp641, _110
	extui	a6, a6, 0, 8	# _447, tmp641
.L131:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:298:                     clamp8f(shootCol[1] * fade), clamp8f(shootCol[2] * fade));
	l8ui	a2, a10, 1	# shootCol, shootCol
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.n	a4, a9	# _449, _447
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:298:                     clamp8f(shootCol[1] * fade), clamp8f(shootCol[2] * fade));
	float.s	f0, a2, 0	# tmp644, shootCol
	mul.s	f0, f0, f1	# _116, tmp644, fade
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f0, f3	#, _116, tmp712
	bt	b0, .L132	#,
	olt.s	b0, f12, f0	#, tmp760, _116
	mov.n	a4, a12	# _449, _447
	bt	b0, .L132	#,
	utrunc.s	a4, f0, 0	# tmp648, _116
	extui	a4, a4, 0, 8	# _449, tmp648
.L132:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:298:                     clamp8f(shootCol[1] * fade), clamp8f(shootCol[2] * fade));
	l8ui	a2, a10, 2	# shootCol, shootCol
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.n	a11, a9	# _451, _447
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:298:                     clamp8f(shootCol[1] * fade), clamp8f(shootCol[2] * fade));
	float.s	f0, a2, 0	# tmp651, shootCol
	mul.s	f1, f0, f1	# _121, tmp651, fade
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f1, f3	#, _121, tmp712
	bt	b0, .L133	#,
	olt.s	b0, f11, f1	#, tmp761, _121
	mov.n	a11, a12	# _451, _447
	bt	b0, .L133	#,
	utrunc.s	a11, f1, 0	# tmp655, _121
	extui	a11, a11, 0, 8	# _451, tmp655
.L133:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bltz	a3, .L134	# _104,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bge	a3, a5, .L134	# _104, w,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:295:             const float py = hy - shoot.vy * 0.02f * k;
	mov.s	f0, f13	# py, hy
	msub.s	f0, f2, f10	# py, _94, _99
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:297:             plotMax(dst, rows, w, static_cast<int>(px), static_cast<int>(py) - y0, clamp8f(shootCol[0] * fade),
	l32i.n	a13, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:297:             plotMax(dst, rows, w, static_cast<int>(px), static_cast<int>(py) - y0, clamp8f(shootCol[0] * fade),
	trunc.s	a2, f0, 0	# tmp664, py
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:297:             plotMax(dst, rows, w, static_cast<int>(px), static_cast<int>(py) - y0, clamp8f(shootCol[0] * fade),
	sub	a2, a2, a13	# _106, tmp664,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bltz	a2, .L134	# _106,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:233:     if (x < 0 || x >= w || y < 0 || y >= rows) {
	bge	a2, a15, .L134	# _106, rows,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	mull	a2, a2, a5	# tmp672, _106, w
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	l32i.n	a13, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	add.n	a2, a2, a3	# tmp673, tmp672, _104
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a6, a6, 8	# tmp675, _447,
	movi	a3, -0x800	#,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	slli	a2, a2, 1	# tmp674, tmp673,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	slli	a4, a4, 3	# tmp679, _449,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a6, a6, a3	# tmp678, tmp675,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:237:     uint16_t &px = dst[static_cast<size_t>(y) * w + x];
	add.n	a2, a13, a2	# px,, tmp674
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	and	a4, a4, a14	# tmp682, tmp679, tmp763
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a4, a6, a4	# tmp685, tmp678, tmp682
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	srai	a11, a11, 3	# tmp686, _451,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:239:     if (c > px) {
	l16ui	a3, a2, 0	# *px_357, *px_357
# ../../src/display/ui/default/bganim/BgAnimCommon.h:24:     return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	or	a11, a4, a11	# _371, tmp685, tmp686
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:239:     if (c > px) {
	bgeu	a3, a11, .L134	# *px_357, _371,
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:240:         px = c;
	s16i	a11, a2, 0	# *px_357, _371
.L134:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:292:         for (int k = 0; k < 14; k++) {
	addi.n	a7, a7, 1	# k, k,
	.L137_LEND:		#
.L104:
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:301: }
	retw.n
	.size	_ZN12_GLOBAL__N_14bandEPtiiijPKh, .-_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.section	.text.startup._GLOBAL__sub_I_bg_anim_starfield,"ax",@progbits
	.literal_position
	.literal .LC95, _ZN12_GLOBAL__N_15shootE
	.align	4
	.type	_GLOBAL__sub_I_bg_anim_starfield, @function
_GLOBAL__sub_I_bg_anim_starfield:
	entry	sp, 32	#
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:49: struct Shoot {
	l32r	a8, .LC95	#, tmp42
	movi.n	a9, 0	# tmp43,
	s8i	a9, a8, 0	# MEM[(bool *)&shoot], tmp43
# ../../src/display/ui/default/bganim/AnimStarfield.cpp:313: };
	retw.n
	.size	_GLOBAL__sub_I_bg_anim_starfield, .-_GLOBAL__sub_I_bg_anim_starfield
	.section	.ctors,"aw",@progbits
	.align	4
	.word	_GLOBAL__sub_I_bg_anim_starfield
	.global	bg_anim_starfield
	.section	.rodata.str1.4,"aMS",@progbits,1
	.align	4
.LC96:
	.string	"starfield"
	.align	4
.LC97:
	.string	"Starfield"
	.align	4
.LC98:
	.string	"speed"
	.align	4
.LC99:
	.string	"Drift speed"
	.align	4
.LC100:
	.string	"density"
	.align	4
.LC101:
	.string	"Stars"
	.align	4
.LC102:
	.string	"twinkle"
	.align	4
.LC103:
	.string	"Twinkle"
	.align	4
.LC104:
	.string	"shooting"
	.align	4
.LC105:
	.string	"Shooting stars"
	.section	.rodata
	.align	4
	.type	bg_anim_starfield, @object
	.size	bg_anim_starfield, 68
bg_anim_starfield:
# id:
	.word	.LC96
# name:
	.word	.LC97
# params:
# key:
	.word	.LC98
# label:
	.word	.LC99
# def:
	.byte	50
	.zero	3
# key:
	.word	.LC100
# label:
	.word	.LC101
# def:
	.byte	45
	.zero	3
# key:
	.word	.LC102
# label:
	.word	.LC103
# def:
	.byte	50
	.zero	3
# key:
	.word	.LC104
# label:
	.word	.LC105
# def:
	.byte	30
	.zero	3
# init:
	.word	_ZN12_GLOBAL__N_14initEii
# frame:
	.word	_ZN12_GLOBAL__N_15frameEjiiPKh
# band:
	.word	_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.data
	.align	4
	.type	_ZN12_GLOBAL__N_111lastDriftMsE, @object
	.size	_ZN12_GLOBAL__N_111lastDriftMsE, 4
_ZN12_GLOBAL__N_111lastDriftMsE:
	.word	-1
	.local	_ZN12_GLOBAL__N_17lastTMsE
	.comm	_ZN12_GLOBAL__N_17lastTMsE,4,4
	.align	4
	.type	_ZN12_GLOBAL__N_13rngE, @object
	.size	_ZN12_GLOBAL__N_13rngE, 4
_ZN12_GLOBAL__N_13rngE:
	.word	12648430
	.align	4
	.type	_ZN12_GLOBAL__N_111nextShootMsE, @object
	.size	_ZN12_GLOBAL__N_111nextShootMsE, 4
_ZN12_GLOBAL__N_111nextShootMsE:
	.word	6000
	.local	_ZN12_GLOBAL__N_15shootE
	.comm	_ZN12_GLOBAL__N_15shootE,32,4
	.local	_ZN12_GLOBAL__N_18g_nStarsE
	.comm	_ZN12_GLOBAL__N_18g_nStarsE,4,4
	.align	4
	.type	_ZN12_GLOBAL__N_112lastThemeGenE, @object
	.size	_ZN12_GLOBAL__N_112lastThemeGenE, 4
_ZN12_GLOBAL__N_112lastThemeGenE:
	.word	-1
	.align	4
	.type	_ZN12_GLOBAL__N_18shootColE, @object
	.size	_ZN12_GLOBAL__N_18shootColE, 3
_ZN12_GLOBAL__N_18shootColE:
	.byte	-36
	.byte	-31
	.byte	-1
	.local	_ZN12_GLOBAL__N_18vigColorE
	.comm	_ZN12_GLOBAL__N_18vigColorE,4,4
	.local	_ZN12_GLOBAL__N_17starColE
	.comm	_ZN12_GLOBAL__N_17starColE,4,4
	.local	_ZN12_GLOBAL__N_16vigLUTE
	.comm	_ZN12_GLOBAL__N_16vigLUTE,4,4
	.local	_ZN12_GLOBAL__N_13dy2E
	.comm	_ZN12_GLOBAL__N_13dy2E,4,4
	.local	_ZN12_GLOBAL__N_13dx2E
	.comm	_ZN12_GLOBAL__N_13dx2E,4,4
	.local	_ZN12_GLOBAL__N_16driftQE
	.comm	_ZN12_GLOBAL__N_16driftQE,4,4
	.local	_ZN12_GLOBAL__N_18bandNextE
	.comm	_ZN12_GLOBAL__N_18bandNextE,4,4
	.local	_ZN12_GLOBAL__N_18bandHeadE
	.comm	_ZN12_GLOBAL__N_18bandHeadE,4,4
	.local	_ZN12_GLOBAL__N_15starYE
	.comm	_ZN12_GLOBAL__N_15starYE,4,4
	.local	_ZN12_GLOBAL__N_15drawsE
	.comm	_ZN12_GLOBAL__N_15drawsE,4,4
	.local	_ZN12_GLOBAL__N_15starsE
	.comm	_ZN12_GLOBAL__N_15starsE,4,4
	.ident	"GCC: (crosstool-NG esp-2021r2-patch5) 8.4.0"
