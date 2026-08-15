	.file	"AnimPlasma.cpp"
# GNU C++17 (crosstool-NG esp-2021r2-patch5) version 8.4.0 (xtensa-esp32s3-elf)
#	compiled by GNU C version 6.3.0 20170516, GMP version 6.2.1, MPFR version 4.1.0, MPC version 1.2.1, isl version isl-0.19-GMP

# GGC heuristics: --param ggc-min-expand=100 --param ggc-min-heapsize=131072
# options passed:  -I shim -imultilib no-rtti
# -iprefix c:\users\oystein\.platformio\packages\toolchain-xtensa-esp32s3\bin\../lib/gcc/xtensa-esp32s3-elf/8.4.0/
# ../../src/display/ui/default/bganim/AnimPlasma.cpp -mlongcalls
# -auxbase-strip xtensa-asm/AnimPlasma.S -O2 -std=gnu++17 -fno-exceptions
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
	.literal .LC0, _ZN12_GLOBAL__N_17rowTermE
	.literal .LC1, _ZN12_GLOBAL__N_110rotPaletteE
	.literal .LC2, _ZN12_GLOBAL__N_17colTermE
	.align	4
	.type	_ZN12_GLOBAL__N_14bandEPtiiijPKh, @function
_ZN12_GLOBAL__N_14bandEPtiiijPKh:
	entry	sp, 32	#
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:91:     for (int y = y0; y < y0 + rows; y++) {
	add.n	a4, a3, a4	# _94, y0, rows
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:90: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:91:     for (int y = y0; y < y0 + rows; y++) {
	bge	a3, a4, .L1	# y0, _94,
	slli	a14, a3, 1	# tmp112, y0,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:99:             const uint16_t p0 = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	l32r	a3, .LC1	#, tmp110
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:92:         const int rt = rowTerm[y];
	l32r	a6, .LC0	#, tmp109
	addi	a15, a5, -2	# tmp114, w,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:99:             const uint16_t p0 = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	l32i.n	a12, a3, 0	# rotPalette, rotPalette$2_8
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:99:             const uint16_t p0 = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	l32r	a3, .LC2	#, tmp111
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:92:         const int rt = rowTerm[y];
	l32i.n	a6, a6, 0	# rowTerm, rowTerm$0_2
	srli	a15, a15, 1	# tmp115, tmp114,
	addi.n	a15, a15, 1	# _114, tmp115,
	slli	a4, a4, 1	# tmp113, _94,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:99:             const uint16_t p0 = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	l32i.n	a3, a3, 0	# colTerm, colTerm$3_9
	add.n	a14, a6, a14	# ivtmp$58, rowTerm$0_2, tmp112
	add.n	a4, a6, a4	# _100, rowTerm$0_2, tmp113
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:93:         int x = 0;
	movi.n	a7, 0	# x,
	slli	a6, a15, 2	# _113, _114,
	slli	a15, a15, 1	# x, _114,
.L8:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:92:         const int rt = rowTerm[y];
	l16si	a11, a14, 0	# MEM[base: _104, offset: 0B], rt
	mov.n	a10, a3	# ivtmp$51, colTerm$3_9
	add.n	a13, a2, a6	# dst, dst, _113
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:98:         for (; x + 1 < w; x += 2) {
	bgei	a5, 2, .L6	# w,,
	mov.n	a13, a2	# dst, dst
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:93:         int x = 0;
	mov.n	a8, a7	# x, x
	j	.L5		#
.L10:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:98:         for (; x + 1 < w; x += 2) {
	mov.n	a8, a15	# x, x
.L5:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:104:         for (; x < w; x++) {
	mov.n	a2, a13	# dst, dst
	bge	a8, a5, .L4	# x, w,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:105:             *dst++ = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	slli	a8, a8, 1	# tmp117, x,
	add.n	a8, a3, a8	# tmp118, colTerm$3_9, tmp117
	l16si	a8, a8, 0	# *_82, *_82
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:105:             *dst++ = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	addi.n	a2, a13, 2	# dst, dst,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:105:             *dst++ = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	add.n	a8, a8, a11	# tmp121, *_82, rt
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:105:             *dst++ = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	extui	a8, a8, 4, 8	# tmp123, tmp121,,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:105:             *dst++ = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	slli	a8, a8, 1	# tmp124, tmp123,
	add.n	a8, a12, a8	# tmp125, rotPalette$2_8, tmp124
	l16ui	a8, a8, 0	# *_90, _91
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:105:             *dst++ = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	s16i	a8, a13, 0	# *dst_39, _91
	j	.L4		#
.L6:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:100:             const uint16_t p1 = rotPalette[((colTerm[x + 1] + rt) >> 4) & 255];
	l16si	a9, a10, 2	# MEM[base: _118, offset: 2B], MEM[base: _118, offset: 2B]
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:99:             const uint16_t p0 = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	l16si	a8, a10, 0	# MEM[base: _118, offset: 0B], MEM[base: _118, offset: 0B]
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:100:             const uint16_t p1 = rotPalette[((colTerm[x + 1] + rt) >> 4) & 255];
	add.n	a9, a9, a11	# tmp128, MEM[base: _118, offset: 2B], rt
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:100:             const uint16_t p1 = rotPalette[((colTerm[x + 1] + rt) >> 4) & 255];
	extui	a9, a9, 4, 8	# tmp130, tmp128,,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:99:             const uint16_t p0 = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	add.n	a8, a8, a11	# tmp137, MEM[base: _118, offset: 0B], rt
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:100:             const uint16_t p1 = rotPalette[((colTerm[x + 1] + rt) >> 4) & 255];
	slli	a9, a9, 1	# tmp131, tmp130,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:99:             const uint16_t p0 = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	extui	a8, a8, 4, 8	# tmp139, tmp137,,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:100:             const uint16_t p1 = rotPalette[((colTerm[x + 1] + rt) >> 4) & 255];
	add.n	a9, a12, a9	# tmp132, rotPalette$2_8, tmp131
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:99:             const uint16_t p0 = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	slli	a8, a8, 1	# tmp140, tmp139,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:101:             *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
	l16ui	a9, a9, 0	# *_32, *_32
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:99:             const uint16_t p0 = rotPalette[((colTerm[x] + rt) >> 4) & 255];
	add.n	a8, a12, a8	# tmp141, rotPalette$2_8, tmp140
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:101:             *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
	l16ui	a8, a8, 0	# *_20, *_20
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:101:             *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
	slli	a9, a9, 16	# tmp134, *_32,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:101:             *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
	or	a8, a9, a8	# tmp143, tmp134, *_20
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:101:             *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
	s32i.n	a8, a2, 0	# MEM[base: dst_40, offset: 0B], tmp143
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:102:             dst += 2;
	addi.n	a2, a2, 4	# dst, dst,
	addi.n	a10, a10, 4	# ivtmp$51, ivtmp$51,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:98:         for (; x + 1 < w; x += 2) {
	bne	a2, a13, .L6	# dst, dst,
	j	.L10		#
.L4:
	addi.n	a14, a14, 2	# ivtmp$58, ivtmp$58,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:91:     for (int y = y0; y < y0 + rows; y++) {
	bne	a4, a14, .L8	# _100, ivtmp$58,
.L1:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:108: }
	retw.n
	.size	_ZN12_GLOBAL__N_14bandEPtiiijPKh, .-_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.section	.text._ZN12_GLOBAL__N_15frameEjiiPKh,"ax",@progbits
	.literal_position
	.literal .LC3, _ZN12_GLOBAL__N_15lastPE
	.literal .LC4, _ZN12_GLOBAL__N_112lastThemeGenE
	.literal .LC5, 1374389535
	.literal .LC6, _ZN12_GLOBAL__N_17paletteE
	.literal .LC7, _ZN12_GLOBAL__N_16phase1E
	.literal .LC8, _ZN12_GLOBAL__N_16phase2E
	.literal .LC9, _ZN12_GLOBAL__N_16phase3E
	.literal .LC10, _ZN12_GLOBAL__N_15cycleE
	.literal .LC11, _ZN12_GLOBAL__N_17colTermE
	.literal .LC12, _ZN12_GLOBAL__N_17rowTermE
	.literal .LC13, _ZN12_GLOBAL__N_110rotPaletteE
	.align	4
	.type	_ZN12_GLOBAL__N_15frameEjiiPKh, @function
_ZN12_GLOBAL__N_15frameEjiiPKh:
	entry	sp, 48	#
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:47:     if (memcmp(p, lastP, 4) != 0 || themeGen() != lastThemeGen) {
	l32r	a6, .LC3	#, tmp292
	l8ui	a8, a5, 0	# MEM[(void *)p_97(D)], MEM[(void *)p_97(D)]
	l8ui	a7, a6, 0	# MEM[(void *)&lastP], MEM[(void *)&lastP]
	bne	a8, a7, .L18	# MEM[(void *)p_97(D)], MEM[(void *)&lastP],
	l8ui	a8, a5, 1	# MEM[(void *)p_97(D)], MEM[(void *)p_97(D)]
	l8ui	a7, a6, 1	# MEM[(void *)&lastP], MEM[(void *)&lastP]
	bne	a8, a7, .L18	# MEM[(void *)p_97(D)], MEM[(void *)&lastP],
	l8ui	a8, a5, 2	# MEM[(void *)p_97(D)], MEM[(void *)p_97(D)]
	l8ui	a7, a6, 2	# MEM[(void *)&lastP], MEM[(void *)&lastP]
	bne	a8, a7, .L18	# MEM[(void *)p_97(D)], MEM[(void *)&lastP],
	l8ui	a8, a5, 3	# MEM[(void *)p_97(D)], MEM[(void *)p_97(D)]
	l8ui	a7, a6, 3	# MEM[(void *)&lastP], MEM[(void *)&lastP]
	beq	a8, a7, .L33	# MEM[(void *)p_97(D)], MEM[(void *)&lastP],
.L18:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:48:         memcpy(lastP, p, 4);
	l8ui	a8, a5, 0	# MEM[(char * {ref-all})p_97(D)],
	l8ui	a7, a5, 1	# MEM[(char * {ref-all})p_97(D)],
	s8i	a8, a6, 0	# MEM[(char * {ref-all})&lastP], MEM[(char * {ref-all})p_97(D)]
	l8ui	a8, a5, 2	# MEM[(char * {ref-all})p_97(D)],
	s8i	a7, a6, 1	# MEM[(char * {ref-all})&lastP], MEM[(char * {ref-all})p_97(D)]
	l8ui	a7, a5, 3	# MEM[(char * {ref-all})p_97(D)],
	s8i	a8, a6, 2	# MEM[(char * {ref-all})&lastP], MEM[(char * {ref-all})p_97(D)]
	s8i	a7, a6, 3	# MEM[(char * {ref-all})&lastP], MEM[(char * {ref-all})p_97(D)]
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:49:         lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:50:         const uint16_t bright = 64 + static_cast<uint16_t>(p[2]) * 192 / 100; // 25%..100%
	l8ui	a6, a5, 2	# MEM[(const uint8_t *)p_97(D) + 2B], MEM[(const uint8_t *)p_97(D) + 2B]
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:50:         const uint16_t bright = 64 + static_cast<uint16_t>(p[2]) * 192 / 100; // 25%..100%
	l32r	a7, .LC5	#, tmp295
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:50:         const uint16_t bright = 64 + static_cast<uint16_t>(p[2]) * 192 / 100; // 25%..100%
	slli	a11, a6, 1	# tmp160, MEM[(const uint8_t *)p_97(D) + 2B],
	add.n	a11, a11, a6	# tmp161, tmp160, MEM[(const uint8_t *)p_97(D) + 2B]
	slli	a11, a11, 6	# tmp162, tmp161,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:50:         const uint16_t bright = 64 + static_cast<uint16_t>(p[2]) * 192 / 100; // 25%..100%
	muluh	a11, a11, a7	# tmp287, tmp162, tmp295
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:51:         buildThemeWheel(palette, bright);
	l32r	a6, .LC6	#, tmp294
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:49:         lastThemeGen = themeGen();
	l32r	a8, .LC4	#,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:50:         const uint16_t bright = 64 + static_cast<uint16_t>(p[2]) * 192 / 100; // 25%..100%
	srli	a11, a11, 5	# tmp163, tmp287,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:49:         lastThemeGen = themeGen();
	s32i.n	a10, a8, 0	# lastThemeGen,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:51:         buildThemeWheel(palette, bright);
	l32i.n	a10, a6, 0	# palette,
	addi	a11, a11, 64	#, tmp163,
	call8	_ZN6bganim15buildThemeWheelEPtt		#
	j	.L17		#
.L33:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:47:     if (memcmp(p, lastP, 4) != 0 || themeGen() != lastThemeGen) {
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:47:     if (memcmp(p, lastP, 4) != 0 || themeGen() != lastThemeGen) {
	l32r	a8, .LC4	#,
	l32i.n	a7, a8, 0	# lastThemeGen, lastThemeGen
	bne	a10, a7, .L18	#, lastThemeGen,
	l32r	a7, .LC5	#, tmp295
	l32r	a6, .LC6	#, tmp294
.L17:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:55:     const uint32_t speedMul = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 4..48, /16 = 0.25..3
	l8ui	a9, a5, 0	# *p_97(D), *p_97(D)
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:63:     const uint32_t sx = 128 + static_cast<uint32_t>(p[1]) * 384 / 100; // 128..512, /256
	l8ui	a8, a5, 1	# MEM[(const uint8_t *)p_97(D) + 1B], MEM[(const uint8_t *)p_97(D) + 1B]
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:55:     const uint32_t speedMul = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 4..48, /16 = 0.25..3
	slli	a5, a9, 2	# tmp183, *p_97(D),
	add.n	a5, a5, a9	# tmp184, tmp183, *p_97(D)
	slli	a5, a5, 1	# tmp185, tmp184,
	add.n	a5, a5, a9	# tmp186, tmp185, *p_97(D)
	slli	a5, a5, 2	# tmp187, tmp186,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:55:     const uint32_t speedMul = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 4..48, /16 = 0.25..3
	muluh	a5, a5, a7	# tmp289, tmp187, tmp295
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:63:     const uint32_t sx = 128 + static_cast<uint32_t>(p[1]) * 384 / 100; // 128..512, /256
	slli	a9, a8, 1	# tmp230, MEM[(const uint8_t *)p_97(D) + 1B],
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:55:     const uint32_t speedMul = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 4..48, /16 = 0.25..3
	srli	a5, a5, 5	# tmp188, tmp289,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:55:     const uint32_t speedMul = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 4..48, /16 = 0.25..3
	addi.n	a5, a5, 4	# speedMul, tmp188,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:56:     const uint32_t base = tMs * speedMul >> 4;                            // ~= original frame*2 at 50
	mull	a2, a5, a2	# tmp199, speedMul, tMs
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:63:     const uint32_t sx = 128 + static_cast<uint32_t>(p[1]) * 384 / 100; // 128..512, /256
	add.n	a8, a9, a8	# tmp231, tmp230, MEM[(const uint8_t *)p_97(D) + 1B]
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:56:     const uint32_t base = tMs * speedMul >> 4;                            // ~= original frame*2 at 50
	srli	a9, a2, 4	# base, tmp199,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:58:     phase2 = base * 23 >> 9;
	slli	a5, a9, 1	# tmp208, base,
	add.n	a5, a5, a9	# tmp209, tmp208, base
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:63:     const uint32_t sx = 128 + static_cast<uint32_t>(p[1]) * 384 / 100; // 128..512, /256
	slli	a2, a8, 7	# tmp232, tmp231,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:58:     phase2 = base * 23 >> 9;
	slli	a8, a5, 3	# tmp210, tmp209,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:58:     phase2 = base * 23 >> 9;
	l32r	a14, .LC8	#, tmp298
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:63:     const uint32_t sx = 128 + static_cast<uint32_t>(p[1]) * 384 / 100; // 128..512, /256
	muluh	a2, a2, a7	# tmp291, tmp232, tmp295
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:59:     phase3 = base * 26 >> 9;
	slli	a10, a5, 2	# tmp217, tmp209,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:58:     phase2 = base * 23 >> 9;
	sub	a8, a8, a9	# tmp211, tmp210, base
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:60:     cycle = base * 11 >> 9;
	slli	a13, a9, 2	# tmp223, base,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:59:     phase3 = base * 26 >> 9;
	add.n	a10, a10, a9	# tmp218, tmp217, base
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:58:     phase2 = base * 23 >> 9;
	srli	a8, a8, 9	# tmp212, tmp211,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:58:     phase2 = base * 23 >> 9;
	s32i.n	a8, a14, 0	# phase2, tmp212
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:57:     phase1 = base * 30 >> 9; // ratios preserved from the frame-based original
	slli	a11, a9, 4	# tmp202, base,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:59:     phase3 = base * 26 >> 9;
	l32r	a8, .LC9	#,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:60:     cycle = base * 11 >> 9;
	add.n	a13, a13, a9	# tmp224, tmp223, base
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:63:     const uint32_t sx = 128 + static_cast<uint32_t>(p[1]) * 384 / 100; // 128..512, /256
	movi	a7, 0x80	# tmp243,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:63:     const uint32_t sx = 128 + static_cast<uint32_t>(p[1]) * 384 / 100; // 128..512, /256
	srli	a2, a2, 5	# tmp233, tmp291,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:59:     phase3 = base * 26 >> 9;
	slli	a10, a10, 1	# tmp219, tmp218,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:63:     const uint32_t sx = 128 + static_cast<uint32_t>(p[1]) * 384 / 100; // 128..512, /256
	add.n	a2, a2, a7	# sx, tmp233, tmp243
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:57:     phase1 = base * 30 >> 9; // ratios preserved from the frame-based original
	sub	a11, a11, a9	# tmp203, tmp202, base
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:60:     cycle = base * 11 >> 9;
	slli	a13, a13, 1	# tmp225, tmp224,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:59:     phase3 = base * 26 >> 9;
	srli	a10, a10, 9	# tmp220, tmp219,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:59:     phase3 = base * 26 >> 9;
	s32i.n	a10, a8, 0	# phase3, tmp220
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:64:     const uint32_t f5 = (5 * sx) >> 8;
	slli	a7, a2, 2	# tmp245, sx,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:57:     phase1 = base * 30 >> 9; // ratios preserved from the frame-based original
	l32r	a5, .LC7	#, tmp200
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:60:     cycle = base * 11 >> 9;
	add.n	a9, a13, a9	# tmp226, tmp225, base
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:60:     cycle = base * 11 >> 9;
	l32r	a8, .LC10	#,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:57:     phase1 = base * 30 >> 9; // ratios preserved from the frame-based original
	slli	a11, a11, 1	# tmp204, tmp203,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:64:     const uint32_t f5 = (5 * sx) >> 8;
	add.n	a13, a7, a2	# tmp246, tmp245, sx
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:65:     const uint32_t f2 = (2 * sx) >> 8; // sx >= 128 keeps every frequency >= 1
	slli	a12, a2, 1	# tmp247, sx,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:57:     phase1 = base * 30 >> 9; // ratios preserved from the frame-based original
	srli	a11, a11, 9	# tmp205, tmp204,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:60:     cycle = base * 11 >> 9;
	srli	a9, a9, 9	# tmp227, tmp226,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:57:     phase1 = base * 30 >> 9; // ratios preserved from the frame-based original
	s32i.n	a11, a5, 0	# phase1, tmp205
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:60:     cycle = base * 11 >> 9;
	s32i.n	a9, a8, 0	# cycle, tmp227
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:64:     const uint32_t f5 = (5 * sx) >> 8;
	srli	a13, a13, 8	# f5, tmp246,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:67:     const uint32_t f3 = (3 * sx) >> 8;
	add.n	a2, a12, a2	# tmp251, tmp247, sx
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:65:     const uint32_t f2 = (2 * sx) >> 8; // sx >= 128 keeps every frequency >= 1
	srli	a12, a12, 8	# f2, tmp247,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:71:     const int16_t *sl = sinLut();
	s32i.n	a12, sp, 4	#,
	s32i.n	a13, sp, 0	#,
	s32i.n	a14, sp, 8	#,
	call8	_ZN6bganim6sinLutEv		#
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:66:     const uint32_t f4 = (4 * sx) >> 8;
	srli	a7, a7, 8	# f4, tmp245,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:67:     const uint32_t f3 = (3 * sx) >> 8;
	srli	a2, a2, 8	# f3, tmp251,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:72:     for (int x = 0; x < w; x++) {
	l32i.n	a12, sp, 4	#,
	l32i.n	a13, sp, 0	#,
	l32i.n	a14, sp, 8	#,
	bgei	a3, 1, .L19	# w,,
.L22:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:75:     for (int y = 0; y < h; y++) {
	bgei	a4, 1, .L34	# h,,
	j	.L20		#
.L19:
	slli	a11, a3, 1	# tmp305, w,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:73:         colTerm[x] = sl[(x * f5 + phase1) & (SIN_N - 1)] + sl[(x * f2 + SIN_N - (phase2 & (SIN_N - 1))) & (SIN_N - 1)];
	l32i.n	a15, a5, 0	# phase1, ivtmp$73
	l32r	a5, .LC11	#, tmp255
	l32i.n	a3, a14, 0	# phase2, phase2
	addi	a11, a11, -2	# tmp306, tmp305,
	srli	a11, a11, 1	# tmp307, tmp306,
	l32i.n	a5, a5, 0	# colTerm, ivtmp$75
	neg	a3, a3	# ivtmp$74, phase2
	addi.n	a11, a11, 1	# tmp304, tmp307,
	loop	a11, .L23_LEND	# tmp304,
.L23:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:73:         colTerm[x] = sl[(x * f5 + phase1) & (SIN_N - 1)] + sl[(x * f2 + SIN_N - (phase2 & (SIN_N - 1))) & (SIN_N - 1)];
	extui	a9, a15, 0, 10	# tmp257, ivtmp$73,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:73:         colTerm[x] = sl[(x * f5 + phase1) & (SIN_N - 1)] + sl[(x * f2 + SIN_N - (phase2 & (SIN_N - 1))) & (SIN_N - 1)];
	extui	a8, a3, 0, 10	# tmp260, ivtmp$74,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:73:         colTerm[x] = sl[(x * f5 + phase1) & (SIN_N - 1)] + sl[(x * f2 + SIN_N - (phase2 & (SIN_N - 1))) & (SIN_N - 1)];
	slli	a9, a9, 1	# tmp258, tmp257,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:73:         colTerm[x] = sl[(x * f5 + phase1) & (SIN_N - 1)] + sl[(x * f2 + SIN_N - (phase2 & (SIN_N - 1))) & (SIN_N - 1)];
	slli	a8, a8, 1	# tmp261, tmp260,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:73:         colTerm[x] = sl[(x * f5 + phase1) & (SIN_N - 1)] + sl[(x * f2 + SIN_N - (phase2 & (SIN_N - 1))) & (SIN_N - 1)];
	add.n	a9, a10, a9	# tmp259, sl, tmp258
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:73:         colTerm[x] = sl[(x * f5 + phase1) & (SIN_N - 1)] + sl[(x * f2 + SIN_N - (phase2 & (SIN_N - 1))) & (SIN_N - 1)];
	add.n	a8, a10, a8	# tmp262, sl, tmp261
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:73:         colTerm[x] = sl[(x * f5 + phase1) & (SIN_N - 1)] + sl[(x * f2 + SIN_N - (phase2 & (SIN_N - 1))) & (SIN_N - 1)];
	l16ui	a9, a9, 0	# *_39,
	l16ui	a8, a8, 0	# *_47,
	add.n	a15, a15, a13	# ivtmp$73, ivtmp$73, f5
	add.n	a8, a9, a8	# tmp265, *_39, *_47
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:73:         colTerm[x] = sl[(x * f5 + phase1) & (SIN_N - 1)] + sl[(x * f2 + SIN_N - (phase2 & (SIN_N - 1))) & (SIN_N - 1)];
	s16i	a8, a5, 0	# MEM[base: _207, offset: 0B], tmp265
	add.n	a3, a3, a12	# ivtmp$74, ivtmp$74, f2
	addi.n	a5, a5, 2	# ivtmp$75, ivtmp$75,
	.L23_LEND:		#
	j	.L22		#
.L34:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:76:         rowTerm[y] = sl[(y * f4 + phase2) & (SIN_N - 1)] + sl[(y * f3 + phase3) & (SIN_N - 1)];
	l32r	a3, .LC9	#,
	slli	a5, a4, 1	# tmp301, h,
	l32i.n	a11, a3, 0	# phase3, ivtmp$69
	l32r	a3, .LC12	#, tmp268
	addi	a5, a5, -2	# tmp302, tmp301,
	srli	a5, a5, 1	# tmp303, tmp302,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:76:         rowTerm[y] = sl[(y * f4 + phase2) & (SIN_N - 1)] + sl[(y * f3 + phase3) & (SIN_N - 1)];
	l32i.n	a9, a14, 0	# phase2, ivtmp$68
	l32i.n	a8, a3, 0	# rowTerm, ivtmp$70
	addi.n	a5, a5, 1	# tmp300, tmp303,
	loop	a5, .L24_LEND	# tmp300,
.L24:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:76:         rowTerm[y] = sl[(y * f4 + phase2) & (SIN_N - 1)] + sl[(y * f3 + phase3) & (SIN_N - 1)];
	extui	a4, a9, 0, 10	# tmp270, ivtmp$68,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:76:         rowTerm[y] = sl[(y * f4 + phase2) & (SIN_N - 1)] + sl[(y * f3 + phase3) & (SIN_N - 1)];
	extui	a3, a11, 0, 10	# tmp273, ivtmp$69,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:76:         rowTerm[y] = sl[(y * f4 + phase2) & (SIN_N - 1)] + sl[(y * f3 + phase3) & (SIN_N - 1)];
	slli	a4, a4, 1	# tmp271, tmp270,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:76:         rowTerm[y] = sl[(y * f4 + phase2) & (SIN_N - 1)] + sl[(y * f3 + phase3) & (SIN_N - 1)];
	slli	a3, a3, 1	# tmp274, tmp273,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:76:         rowTerm[y] = sl[(y * f4 + phase2) & (SIN_N - 1)] + sl[(y * f3 + phase3) & (SIN_N - 1)];
	add.n	a4, a10, a4	# tmp272, sl, tmp271
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:76:         rowTerm[y] = sl[(y * f4 + phase2) & (SIN_N - 1)] + sl[(y * f3 + phase3) & (SIN_N - 1)];
	add.n	a3, a10, a3	# tmp275, sl, tmp274
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:76:         rowTerm[y] = sl[(y * f4 + phase2) & (SIN_N - 1)] + sl[(y * f3 + phase3) & (SIN_N - 1)];
	l16ui	a4, a4, 0	# *_61,
	l16ui	a3, a3, 0	# *_69,
	add.n	a9, a9, a7	# ivtmp$68, ivtmp$68, f4
	add.n	a3, a4, a3	# tmp278, *_61, *_69
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:76:         rowTerm[y] = sl[(y * f4 + phase2) & (SIN_N - 1)] + sl[(y * f3 + phase3) & (SIN_N - 1)];
	s16i	a3, a8, 0	# MEM[base: _220, offset: 0B], tmp278
	add.n	a11, a11, a2	# ivtmp$69, ivtmp$69, f3
	addi.n	a8, a8, 2	# ivtmp$70, ivtmp$70,
	.L24_LEND:		#
.L20:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:84:     const uint32_t rot = cycle & 255;
	l32r	a8, .LC10	#,
	l32r	a2, .LC13	#, tmp282
	l8ui	a5, a8, 0	# cycle, ivtmp$64
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:86:         rotPalette[i] = palette[(i + rot) & 255];
	l32i.n	a6, a6, 0	# palette, palette$29_78
	l32i.n	a4, a2, 0	# rotPalette, ivtmp$65
	movi	a3, 0x100	# tmp299,
.L25:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:86:         rotPalette[i] = palette[(i + rot) & 255];
	extui	a2, a5, 0, 8	# tmp283, ivtmp$64,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:86:         rotPalette[i] = palette[(i + rot) & 255];
	slli	a2, a2, 1	# tmp284, tmp283,
	add.n	a2, a6, a2	# tmp285, palette$29_78, tmp284
	l16ui	a2, a2, 0	# *_83, _87
	addi.n	a5, a5, 1	# ivtmp$64, ivtmp$64,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:86:         rotPalette[i] = palette[(i + rot) & 255];
	s16i	a2, a4, 0	# MEM[base: _229, offset: 0B], _87
	addi.n	a4, a4, 2	# ivtmp$65, ivtmp$65,
	addi.n	a3, a3, -1	# tmp299, tmp299,
	bnez.n	a3, .L25	# tmp299,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:88: }
	retw.n
	.size	_ZN12_GLOBAL__N_15frameEjiiPKh, .-_ZN12_GLOBAL__N_15frameEjiiPKh
	.section	.text._ZN12_GLOBAL__N_14initEii,"ax",@progbits
	.literal_position
	.literal .LC14, _ZN12_GLOBAL__N_17colTermE
	.literal .LC15, _ZN12_GLOBAL__N_17rowTermE
	.literal .LC16, _ZN12_GLOBAL__N_17paletteE
	.literal .LC17, _ZN12_GLOBAL__N_110rotPaletteE
	.align	4
	.type	_ZN12_GLOBAL__N_14initEii, @function
_ZN12_GLOBAL__N_14initEii:
	entry	sp, 32	#
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:28:     if (sinLut() == nullptr) {
	call8	_ZN6bganim6sinLutEv		#
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:28:     if (sinLut() == nullptr) {
	beqz.n	a10, .L36	#,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:31:     if (colTerm == nullptr) {
	l32r	a4, .LC14	#, tmp86
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:31:     if (colTerm == nullptr) {
	l32i.n	a5, a4, 0	# colTerm, colTerm
	bnez.n	a5, .L37	# colTerm,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:32:         colTerm = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
	slli	a10, a2, 1	#, w,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:32:         colTerm = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
	s32i.n	a10, a4, 0	# colTerm,
.L37:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:34:     if (rowTerm == nullptr) {
	l32r	a2, .LC15	#, tmp87
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:34:     if (rowTerm == nullptr) {
	l32i.n	a5, a2, 0	# rowTerm, rowTerm
	bnez.n	a5, .L38	# rowTerm,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:35:         rowTerm = static_cast<int16_t *>(alloc(h * sizeof(int16_t)));
	slli	a10, a3, 1	#, h,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:35:         rowTerm = static_cast<int16_t *>(alloc(h * sizeof(int16_t)));
	s32i.n	a10, a2, 0	# rowTerm,
.L38:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:37:     if (palette == nullptr) {
	l32r	a3, .LC16	#, tmp88
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:37:     if (palette == nullptr) {
	l32i.n	a5, a3, 0	# palette, palette
	bnez.n	a5, .L39	# palette,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:38:         palette = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
	movi	a10, 0x200	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:38:         palette = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
	s32i.n	a10, a3, 0	# palette,
.L39:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:40:     if (rotPalette == nullptr) {
	l32r	a5, .LC17	#, tmp85
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:40:     if (rotPalette == nullptr) {
	l32i.n	a8, a5, 0	# rotPalette, rotPalette
	bnez.n	a8, .L40	# rotPalette,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:41:         rotPalette = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
	movi	a10, 0x200	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:41:         rotPalette = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
	s32i.n	a10, a5, 0	# rotPalette,
.L40:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:43:     return colTerm != nullptr && rowTerm != nullptr && palette != nullptr && rotPalette != nullptr;
	l32i.n	a4, a4, 0	# colTerm, colTerm
	beqz.n	a4, .L36	# colTerm,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:43:     return colTerm != nullptr && rowTerm != nullptr && palette != nullptr && rotPalette != nullptr;
	l32i.n	a2, a2, 0	# rowTerm, rowTerm
	beqz.n	a2, .L36	# rowTerm,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:43:     return colTerm != nullptr && rowTerm != nullptr && palette != nullptr && rotPalette != nullptr;
	l32i.n	a2, a3, 0	# palette, palette
	beqz.n	a2, .L36	# palette,
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:43:     return colTerm != nullptr && rowTerm != nullptr && palette != nullptr && rotPalette != nullptr;
	l32i.n	a3, a5, 0	# rotPalette, rotPalette
	movi.n	a2, 1	# <retval>,
	bnez.n	a3, .L41	# rotPalette,
.L36:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:43:     return colTerm != nullptr && rowTerm != nullptr && palette != nullptr && rotPalette != nullptr;
	movi.n	a2, 0	# <retval>,
.L41:
# ../../src/display/ui/default/bganim/AnimPlasma.cpp:44: }
	retw.n
	.size	_ZN12_GLOBAL__N_14initEii, .-_ZN12_GLOBAL__N_14initEii
	.global	bg_anim_plasma
	.section	.rodata.str1.4,"aMS",@progbits,1
	.align	4
.LC18:
	.string	"plasma"
	.align	4
.LC19:
	.string	"Plasma"
	.align	4
.LC20:
	.string	"speed"
	.align	4
.LC21:
	.string	"Speed"
	.align	4
.LC22:
	.string	"scale"
	.align	4
.LC23:
	.string	"Scale"
	.align	4
.LC24:
	.string	"brightness"
	.align	4
.LC25:
	.string	"Brightness"
	.section	.rodata
	.align	4
	.type	bg_anim_plasma, @object
	.size	bg_anim_plasma, 68
bg_anim_plasma:
# id:
	.word	.LC18
# name:
	.word	.LC19
# params:
# key:
	.word	.LC20
# label:
	.word	.LC21
# def:
	.byte	50
	.zero	3
# key:
	.word	.LC22
# label:
	.word	.LC23
# def:
	.byte	50
	.zero	3
# key:
	.word	.LC24
# label:
	.word	.LC25
# def:
	.byte	70
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
	.local	_ZN12_GLOBAL__N_15cycleE
	.comm	_ZN12_GLOBAL__N_15cycleE,4,4
	.local	_ZN12_GLOBAL__N_16phase3E
	.comm	_ZN12_GLOBAL__N_16phase3E,4,4
	.local	_ZN12_GLOBAL__N_16phase2E
	.comm	_ZN12_GLOBAL__N_16phase2E,4,4
	.local	_ZN12_GLOBAL__N_16phase1E
	.comm	_ZN12_GLOBAL__N_16phase1E,4,4
	.data
	.align	4
	.type	_ZN12_GLOBAL__N_112lastThemeGenE, @object
	.size	_ZN12_GLOBAL__N_112lastThemeGenE, 4
_ZN12_GLOBAL__N_112lastThemeGenE:
	.word	-1
	.align	4
	.type	_ZN12_GLOBAL__N_15lastPE, @object
	.size	_ZN12_GLOBAL__N_15lastPE, 4
_ZN12_GLOBAL__N_15lastPE:
	.byte	-1
	.byte	-1
	.byte	-1
	.byte	-1
	.local	_ZN12_GLOBAL__N_110rotPaletteE
	.comm	_ZN12_GLOBAL__N_110rotPaletteE,4,4
	.local	_ZN12_GLOBAL__N_17paletteE
	.comm	_ZN12_GLOBAL__N_17paletteE,4,4
	.local	_ZN12_GLOBAL__N_17rowTermE
	.comm	_ZN12_GLOBAL__N_17rowTermE,4,4
	.local	_ZN12_GLOBAL__N_17colTermE
	.comm	_ZN12_GLOBAL__N_17colTermE,4,4
	.ident	"GCC: (crosstool-NG esp-2021r2-patch5) 8.4.0"
