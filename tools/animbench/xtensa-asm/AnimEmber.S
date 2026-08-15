	.file	"AnimEmber.cpp"
# GNU C++17 (crosstool-NG esp-2021r2-patch5) version 8.4.0 (xtensa-esp32s3-elf)
#	compiled by GNU C version 6.3.0 20170516, GMP version 6.2.1, MPFR version 4.1.0, MPC version 1.2.1, isl version isl-0.19-GMP

# GGC heuristics: --param ggc-min-expand=100 --param ggc-min-heapsize=131072
# options passed:  -I shim -imultilib no-rtti
# -iprefix c:\users\oystein\.platformio\packages\toolchain-xtensa-esp32s3\bin\../lib/gcc/xtensa-esp32s3-elf/8.4.0/
# ../../src/display/ui/default/bganim/AnimEmber.cpp -mlongcalls
# -auxbase-strip xtensa-asm/AnimEmber.S -O2 -std=gnu++17 -fno-exceptions
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
	.literal .LC0, _ZN12_GLOBAL__N_112g_flickerAmpE
	.literal .LC1, _ZN12_GLOBAL__N_14g_cyE
	.literal .LC2, _ZN12_GLOBAL__N_19g_breatheE
	.literal .LC3, _ZN12_GLOBAL__N_14g_cxE
	.literal .LC4, _ZN6bganim6BAYER8E
	.literal .LC5, 1717986919
	.literal .LC6, _ZN12_GLOBAL__N_15noiseE
	.literal .LC7, _ZN12_GLOBAL__N_14g_syE
	.literal .LC8, _ZN12_GLOBAL__N_19radiusLUTE
	.literal .LC9, _ZN12_GLOBAL__N_110flickerLUTE
	.literal .LC10, _ZN12_GLOBAL__N_14g_sxE
	.literal .LC11, _ZN12_GLOBAL__N_110paletteExtE
	.align	4
	.type	_ZN12_GLOBAL__N_14bandEPtiiijPKh, @function
_ZN12_GLOBAL__N_14bandEPtiiijPKh:
	entry	sp, 96	#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:157: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
	s32i.n	a5, sp, 28	# %sfp, w
# ../../src/display/ui/default/bganim/AnimEmber.cpp:158:     const bool doFlicker = g_flickerAmp != 0;
	l32r	a5, .LC0	#, tmp158
# ../../src/display/ui/default/bganim/AnimEmber.cpp:157: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
# ../../src/display/ui/default/bganim/AnimEmber.cpp:158:     const bool doFlicker = g_flickerAmp != 0;
	l32i.n	a5, a5, 0	# g_flickerAmp,
	s32i.n	a5, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:159:     for (int r = 0; r < rows; r++) {
	blti	a4, 1, .L1	# rows,,
	l32r	a6, .LC1	#, tmp159
	l32r	a5, .LC3	#, tmp162
	l32i.n	a6, a6, 0	# g_cy,
	l32i.n	a5, a5, 0	# g_cx, pretmp_237
	s32i.n	a6, sp, 24	# %sfp,
	l32i.n	a10, sp, 24	# %sfp,
	slli	a13, a5, 1	# tmp165, pretmp_237,
	sub	a9, a3, a10	# ivtmp$74, y0,
	l32i.n	a3, sp, 28	# %sfp,
	add.n	a4, a9, a4	#, ivtmp$74, rows
	slli	a3, a3, 1	#,,
	s32i.n	a3, sp, 20	# %sfp,
	l32r	a3, .LC2	#, tmp160
	neg	a13, a13	# tmp166, tmp165
	mull	a5, a5, a5	#, pretmp_237, pretmp_237
	s32i.n	a4, sp, 32	# %sfp,
	l32i.n	a4, sp, 20	# %sfp,
	addi.n	a6, a13, 1	#, tmp166,
	l8ui	a15, a3, 0	# g_breathe, _236
	s32i.n	a6, sp, 40	# %sfp,
	s32i.n	a5, sp, 36	# %sfp,
	sub	a13, a2, a13	# ivtmp$76, dst, tmp166
	add.n	a14, a6, a4	# _201,,
.L7:
	l32i.n	a6, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:164:         const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
	l32r	a10, .LC4	#,
	add.n	a7, a6, a9	# _177,, ivtmp$74
# ../../src/display/ui/default/bganim/AnimEmber.cpp:164:         const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
	extui	a5, a7, 0, 3	# tmp169, _177,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:164:         const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
	slli	a5, a5, 3	# tmp170, tmp169,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:162:         const int dy2 = dy * dy;
	mull	a3, a9, a9	# dy2, ivtmp$74, ivtmp$74
# ../../src/display/ui/default/bganim/AnimEmber.cpp:164:         const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
	add.n	a5, a10, a5	# ivtmp$70,, tmp170
	mov.n	a8, sp	# ivtmp$71,
	movi.n	a6, 8	# tmp241,
	loop	a6, .L3_LEND	# tmp241,
.L3:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:170:             perCol[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 5 - g_breathe);
	l8ui	a4, a5, 0	# MEM[base: _194, offset: 0B], MEM[base: _194, offset: 0B]
# ../../src/display/ui/default/bganim/AnimEmber.cpp:170:             perCol[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 5 - g_breathe);
	l32r	a10, .LC5	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:170:             perCol[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 5 - g_breathe);
	addi	a4, a4, -31	# tmp173, MEM[base: _194, offset: 0B],
# ../../src/display/ui/default/bganim/AnimEmber.cpp:170:             perCol[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 5 - g_breathe);
	mulsh	a2, a4, a10	# tmp229, tmp173,
	srai	a4, a4, 31	# tmp179, tmp173,
	srai	a2, a2, 1	# tmp178, tmp229,
	sub	a2, a2, a4	# tmp174, tmp178, tmp179
# ../../src/display/ui/default/bganim/AnimEmber.cpp:170:             perCol[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 5 - g_breathe);
	sub	a2, a2, a15	# tmp181, tmp174, _236
# ../../src/display/ui/default/bganim/AnimEmber.cpp:170:             perCol[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 5 - g_breathe);
	s8i	a2, a8, 0	# MEM[base: _193, offset: 0B], tmp181
	addi.n	a5, a5, 1	# ivtmp$70, ivtmp$70,
	addi.n	a8, a8, 1	# ivtmp$71, ivtmp$71,
	.L3_LEND:		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:173:         int r2 = dx * dx + dy2;
	l32i.n	a2, sp, 36	# %sfp,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:176:         if (doFlicker) {
	l32i.n	a4, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:173:         int r2 = dx * dx + dy2;
	add.n	a3, a3, a2	# r2, dy2,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:176:         if (doFlicker) {
	beqz.n	a4, .L4	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:177:             const uint8_t *noiseRow = noise + ((y + g_sy) & 255) * 256;
	l32r	a6, .LC7	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:177:             const uint8_t *noiseRow = noise + ((y + g_sy) & 255) * 256;
	l32r	a10, .LC6	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:177:             const uint8_t *noiseRow = noise + ((y + g_sy) & 255) * 256;
	l32i.n	a2, a6, 0	# g_sy, g_sy
# ../../src/display/ui/default/bganim/AnimEmber.cpp:178:             for (int x = 0; x < w; x++) {
	l32i.n	a4, sp, 28	# %sfp,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:177:             const uint8_t *noiseRow = noise + ((y + g_sy) & 255) * 256;
	add.n	a2, a7, a2	# tmp184, _177, g_sy
# ../../src/display/ui/default/bganim/AnimEmber.cpp:177:             const uint8_t *noiseRow = noise + ((y + g_sy) & 255) * 256;
	extui	a2, a2, 0, 8	# tmp186, tmp184,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:177:             const uint8_t *noiseRow = noise + ((y + g_sy) & 255) * 256;
	l32i.n	a6, a10, 0	# noise, noise$7_23
# ../../src/display/ui/default/bganim/AnimEmber.cpp:177:             const uint8_t *noiseRow = noise + ((y + g_sy) & 255) * 256;
	slli	a2, a2, 8	# _28, tmp186,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:178:             for (int x = 0; x < w; x++) {
	bgei	a4, 1, .L5	#,,
.L8:
	l32i.n	a6, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:159:     for (int r = 0; r < rows; r++) {
	l32i.n	a10, sp, 32	# %sfp,
	addi.n	a9, a9, 1	# ivtmp$74, ivtmp$74,
	add.n	a13, a13, a6	# ivtmp$76, ivtmp$76,
	bne	a10, a9, .L7	#, ivtmp$74,
	j	.L1		#
.L5:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	l32r	a4, .LC8	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	l32r	a10, .LC9	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	l32i.n	a12, a4, 0	# radiusLUT, radiusLUT$9_29
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	l32r	a4, .LC10	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	l32i.n	a11, a10, 0	# flickerLUT, flickerLUT$11_38
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	l32i.n	a10, a4, 0	# g_sx, g_sx$12_39
# ../../src/display/ui/default/bganim/AnimEmber.cpp:181:                 row[x] = paletteExt[PAD + rn];
	l32r	a4, .LC11	#,
	s32i.n	a9, sp, 48	# %sfp, ivtmp$74
	l32i.n	a4, a4, 0	# paletteExt,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:178:             for (int x = 0; x < w; x++) {
	movi.n	a5, 0	# x,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:181:                 row[x] = paletteExt[PAD + rn];
	s32i.n	a4, sp, 44	# %sfp,
	l32i.n	a9, sp, 44	# %sfp, paletteExt$13_49
# ../../src/display/ui/default/bganim/AnimEmber.cpp:174:         int ddx = 2 * dx + 1; // r2 delta for this step; += 2 per pixel thereafter
	l32i.n	a4, sp, 40	# %sfp, ddx
	add.n	a8, a6, a2	# tmp230, noise$7_23, _28
.L9:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	add.n	a2, a10, a5	# tmp191, g_sx$12_39, x
	extui	a2, a2, 0, 8	# tmp192, tmp191,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	add.n	a2, a8, a2	# tmp194, tmp230, tmp192
	l8ui	a2, a2, 0	# *_43, *_43
# ../../src/display/ui/default/bganim/AnimEmber.cpp:179:                 const int ridx = r2 >> RSHIFT;
	srai	a7, a3, 9	# ridx, r2,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	extui	a6, a5, 0, 3	# tmp202, x,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	add.n	a2, a11, a2	# tmp196, flickerLUT$11_38, *_43
	l8ui	a2, a2, 0	# *_46,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	add.n	a7, a12, a7	# tmp200, radiusLUT$9_29, ridx
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	add.n	a6, sp, a6	# tmp203,, tmp202
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	l8ui	a7, a7, 0	# *_31, *_31
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	l8ui	a6, a6, 0	# perCol,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	sext	a2, a2, 7	# *_46, *_46
# ../../src/display/ui/default/bganim/AnimEmber.cpp:181:                 row[x] = paletteExt[PAD + rn];
	add.n	a2, a2, a7	# tmp206, *_46, *_31
# ../../src/display/ui/default/bganim/AnimEmber.cpp:180:                 const int rn = radiusLUT[ridx] + perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]];
	sext	a6, a6, 7	# tmp204, tmp205
# ../../src/display/ui/default/bganim/AnimEmber.cpp:181:                 row[x] = paletteExt[PAD + rn];
	add.n	a6, a2, a6	# tmp207, tmp206, tmp204
	addi	a6, a6, 64	# tmp208, tmp207,
	slli	a6, a6, 1	# tmp209, tmp208,
	add.n	a6, a9, a6	# tmp210, paletteExt$13_49, tmp209
# ../../src/display/ui/default/bganim/AnimEmber.cpp:181:                 row[x] = paletteExt[PAD + rn];
	addi.n	a2, a4, -1	# tmp211, ddx,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:181:                 row[x] = paletteExt[PAD + rn];
	l16ui	a6, a6, 0	# *_53, _57
# ../../src/display/ui/default/bganim/AnimEmber.cpp:181:                 row[x] = paletteExt[PAD + rn];
	add.n	a2, a13, a2	# tmp212, ivtmp$76, tmp211
# ../../src/display/ui/default/bganim/AnimEmber.cpp:182:                 r2 += ddx;
	add.n	a3, a3, a4	# r2, r2, ddx
# ../../src/display/ui/default/bganim/AnimEmber.cpp:181:                 row[x] = paletteExt[PAD + rn];
	s16i	a6, a2, 0	# MEM[base: _220, offset: 0B], _57
# ../../src/display/ui/default/bganim/AnimEmber.cpp:183:                 ddx += 2;
	addi.n	a4, a4, 2	# ddx, ddx,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:178:             for (int x = 0; x < w; x++) {
	addi.n	a5, a5, 1	# x, x,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:178:             for (int x = 0; x < w; x++) {
	bne	a4, a14, .L9	# ddx, _201,
	l32i.n	a9, sp, 48	# %sfp, ivtmp$74
	j	.L8		#
.L4:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:186:             for (int x = 0; x < w; x++) {
	l32i.n	a6, sp, 28	# %sfp,
	blti	a6, 1, .L8	#,,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:188:                 const int rn = radiusLUT[ridx] + perCol[x & 7];
	l32r	a10, .LC8	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:189:                 row[x] = paletteExt[PAD + rn];
	l32r	a2, .LC11	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:188:                 const int rn = radiusLUT[ridx] + perCol[x & 7];
	l32i.n	a7, a10, 0	# radiusLUT, radiusLUT$15_58
# ../../src/display/ui/default/bganim/AnimEmber.cpp:189:                 row[x] = paletteExt[PAD + rn];
	l32i.n	a6, a2, 0	# paletteExt, paletteExt$17_66
# ../../src/display/ui/default/bganim/AnimEmber.cpp:186:             for (int x = 0; x < w; x++) {
	l32i.n	a5, sp, 16	# %sfp, x
# ../../src/display/ui/default/bganim/AnimEmber.cpp:174:         int ddx = 2 * dx + 1; // r2 delta for this step; += 2 per pixel thereafter
	l32i.n	a4, sp, 40	# %sfp, ddx
.L10:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:188:                 const int rn = radiusLUT[ridx] + perCol[x & 7];
	extui	a8, a5, 0, 3	# tmp218, x,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:187:                 const int ridx = r2 >> RSHIFT;
	srai	a2, a3, 9	# ridx, r2,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:188:                 const int rn = radiusLUT[ridx] + perCol[x & 7];
	add.n	a8, sp, a8	# tmp219,, tmp218
# ../../src/display/ui/default/bganim/AnimEmber.cpp:188:                 const int rn = radiusLUT[ridx] + perCol[x & 7];
	add.n	a2, a7, a2	# tmp216, radiusLUT$15_58, ridx
# ../../src/display/ui/default/bganim/AnimEmber.cpp:188:                 const int rn = radiusLUT[ridx] + perCol[x & 7];
	l8ui	a8, a8, 0	# perCol,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:188:                 const int rn = radiusLUT[ridx] + perCol[x & 7];
	l8ui	a2, a2, 0	# *_60, *_60
# ../../src/display/ui/default/bganim/AnimEmber.cpp:188:                 const int rn = radiusLUT[ridx] + perCol[x & 7];
	sext	a8, a8, 7	# tmp220, tmp221
# ../../src/display/ui/default/bganim/AnimEmber.cpp:189:                 row[x] = paletteExt[PAD + rn];
	add.n	a2, a2, a8	# tmp222, *_60, tmp220
	addi	a2, a2, 64	# tmp223, tmp222,
	slli	a2, a2, 1	# tmp224, tmp223,
	add.n	a2, a6, a2	# tmp225, paletteExt$17_66, tmp224
# ../../src/display/ui/default/bganim/AnimEmber.cpp:189:                 row[x] = paletteExt[PAD + rn];
	addi.n	a8, a4, -1	# tmp226, ddx,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:189:                 row[x] = paletteExt[PAD + rn];
	l16ui	a10, a2, 0	# *_70, _74
# ../../src/display/ui/default/bganim/AnimEmber.cpp:189:                 row[x] = paletteExt[PAD + rn];
	add.n	a2, a13, a8	# tmp227, ivtmp$76, tmp226
# ../../src/display/ui/default/bganim/AnimEmber.cpp:190:                 r2 += ddx;
	add.n	a3, a3, a4	# r2, r2, ddx
# ../../src/display/ui/default/bganim/AnimEmber.cpp:189:                 row[x] = paletteExt[PAD + rn];
	s16i	a10, a2, 0	# MEM[base: _207, offset: 0B], _74
# ../../src/display/ui/default/bganim/AnimEmber.cpp:191:                 ddx += 2;
	addi.n	a4, a4, 2	# ddx, ddx,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:186:             for (int x = 0; x < w; x++) {
	addi.n	a5, a5, 1	# x, x,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:186:             for (int x = 0; x < w; x++) {
	bne	a4, a14, .L10	# ddx, _201,
	j	.L8		#
.L1:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:195: }
	retw.n
	.size	_ZN12_GLOBAL__N_14bandEPtiiijPKh, .-_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.global	__divsf3
	.section	.text._ZN12_GLOBAL__N_15frameEjiiPKh,"ax",@progbits
	.literal_position
	.literal .LC12, _ZN12_GLOBAL__N_112lastThemeGenE
	.literal .LC13, _ZN12_GLOBAL__N_17paletteE
	.literal .LC14, _ZN12_GLOBAL__N_110paletteExtE
	.literal .LC15, _ZN12_GLOBAL__N_18lastGlowE
	.literal .LC16, 0x3c656042
	.literal .LC17, 0x3f0ccccd
	.literal .LC18, _ZN12_GLOBAL__N_16g_maxRE
	.literal .LC19, 0x437f0000
	.literal .LC20, _ZN12_GLOBAL__N_19radiusLUTE
	.literal .LC21, 0x00000000
	.literal .LC22, 131072
	.literal .LC23, 0x3d610e10
	.literal .LC24, 0x42c80000
	.literal .LC25, 380085
	.literal .LC26, 0x3b000000
	.literal .LC27, 242653
	.literal .LC28, 1162060969
	.literal .LC29, _ZN12_GLOBAL__N_19g_breatheE
	.literal .LC30, 704092
	.literal .LC31, -1423993136
	.literal .LC32, 0x3e19999a
	.literal .LC33, 0x3e99999a
	.literal .LC34, 0x3d4ccccd
	.literal .LC35, 0x428c0000
	.literal .LC36, -858993459
	.literal .LC38, _ZN12_GLOBAL__N_112g_flickerAmpE
	.literal .LC39, _ZN12_GLOBAL__N_116lastFlickerParamE
	.literal .LC40, _ZN12_GLOBAL__N_110flickerLUTE
	.literal .LC41, _ZN12_GLOBAL__N_14g_sxE
	.literal .LC42, _ZN12_GLOBAL__N_14g_syE
	.align	4
	.type	_ZN12_GLOBAL__N_15frameEjiiPKh, @function
_ZN12_GLOBAL__N_15frameEjiiPKh:
	entry	sp, 64	#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:129: void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
	s32i.n	a2, sp, 8	# %sfp, tMs
	s32i.n	a5, sp, 4	# %sfp, p
# ../../src/display/ui/default/bganim/AnimEmber.cpp:130:     if (themeGen() != lastThemeGen) {
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:130:     if (themeGen() != lastThemeGen) {
	l32r	a6, .LC12	#, tmp268
# ../../src/display/ui/default/bganim/AnimEmber.cpp:130:     if (themeGen() != lastThemeGen) {
	l32i.n	a3, a6, 0	# lastThemeGen, lastThemeGen
	beq	a10, a3, .L14	#, lastThemeGen,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:131:         buildThemeRamp(palette, 256, /*reversed=*/true); // brightest stop at the core
	l32r	a3, .LC13	#, tmp154
	movi.n	a12, 1	#,
	l32i.n	a10, a3, 0	# palette,
	movi	a11, 0x100	#,
	call8	_ZN6bganim14buildThemeRampEPttb		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:96:         paletteExt[i] = lo;
	l32r	a4, .LC14	#, tmp156
# ../../src/display/ui/default/bganim/AnimEmber.cpp:93:     const uint16_t lo = palette[0];
	l32i.n	a3, a3, 0	# palette, palette$25_70
# ../../src/display/ui/default/bganim/AnimEmber.cpp:96:         paletteExt[i] = lo;
	l32i.n	a4, a4, 0	# paletteExt, ivtmp$106
# ../../src/display/ui/default/bganim/AnimEmber.cpp:93:     const uint16_t lo = palette[0];
	l16ui	a9, a3, 0	# *palette$25_70, lo
# ../../src/display/ui/default/bganim/AnimEmber.cpp:94:     const uint16_t hi = palette[255];
	l16ui	a8, a3, 510	# MEM[(uint16_t *)palette$25_70 + 510B], hi
	movi	a3, 0x280	# tmp157,
	add.n	a3, a4, a3	# ivtmp$107, ivtmp$106, tmp157
	movi.n	a7, 0x40	# tmp281,
	loop	a7, .L15_LEND	# tmp281,
.L15:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:96:         paletteExt[i] = lo;
	s16i	a9, a4, 0	# MEM[base: _117, offset: 0B], lo
# ../../src/display/ui/default/bganim/AnimEmber.cpp:97:         paletteExt[PAD + 256 + i] = hi;
	s16i	a8, a3, 0	# MEM[base: _185, offset: 0B], hi
	addi.n	a4, a4, 2	# ivtmp$106, ivtmp$106,
	addi.n	a3, a3, 2	# ivtmp$107, ivtmp$107,
	.L15_LEND:		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:133:         lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:133:         lastThemeGen = themeGen();
	s32i.n	a10, a6, 0	# lastThemeGen,
.L14:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:135:     if (p[1] != lastGlow) {
	l32i.n	a2, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:135:     if (p[1] != lastGlow) {
	l32r	a8, .LC15	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:135:     if (p[1] != lastGlow) {
	l8ui	a3, a2, 1	# MEM[(const uint8_t *)p_56(D) + 1B], _5
# ../../src/display/ui/default/bganim/AnimEmber.cpp:135:     if (p[1] != lastGlow) {
	l8ui	a4, a8, 0	# lastGlow, lastGlow
	beq	a4, a3, .L16	# lastGlow, _5,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:76:     const float glowGain = 0.55f + 0.014f * glow;
	l32r	a2, .LC17	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:76:     const float glowGain = 0.55f + 0.014f * glow;
	float.s	f0, a3, 0	# tmp162, _5
# ../../src/display/ui/default/bganim/AnimEmber.cpp:76:     const float glowGain = 0.55f + 0.014f * glow;
	wfr	f1, a2	#,
	l32r	a2, .LC16	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:77:     const float scale = 255.0f / (g_maxR * glowGain);
	l32r	a3, .LC18	#, tmp166
# ../../src/display/ui/default/bganim/AnimEmber.cpp:76:     const float glowGain = 0.55f + 0.014f * glow;
	wfr	f2, a2	#,
	madd.s	f1, f0, f2	#, tmp162,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:77:     const float scale = 255.0f / (g_maxR * glowGain);
	l32r	a10, .LC19	#,
	movi.n	a2, 0	# ivtmp$99,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	movi	a7, 0xff	# iftmp$37_133,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:76:     const float glowGain = 0.55f + 0.014f * glow;
	mov.s	f0, f1	# glowGain,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:77:     const float scale = 255.0f / (g_maxR * glowGain);
	lsi	f1, a3, 0	# g_maxR, g_maxR
# ../../src/display/ui/default/bganim/AnimEmber.cpp:77:     const float scale = 255.0f / (g_maxR * glowGain);
	mul.s	f1, f0, f1	#, glowGain, g_maxR
	rfr	a11, f1	#,
	call8	__divsf3		#
	l32r	a3, .LC20	#, tmp170
	l32r	a4, .LC22	#, tmp266
	l32i.n	a6, a3, 0	# radiusLUT, pretmp_171
	s32i.n	a10, sp, 0	# %sfp,
	mov.n	a5, a6	# ivtmp$100, pretmp_171
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.n	a3, a2	# iftmp$37_133, ivtmp$99
.L18:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:79:         const float r = sqrtf(static_cast<float>(i << RSHIFT));
	float.s	f0, a2, 0	#, ivtmp$99
	addmi	a2, a2, 0x200	# ivtmp$99, ivtmp$99,
	rfr	a10, f0	#,
	call8	sqrtf		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:80:         radiusLUT[i] = clamp8f(r * scale);
	lsi	f1, sp, 0	# %sfp,
	wfr	f2, a10	#,
	mul.s	f0, f1, f2	# _129,,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	l32r	a8, .LC21	#,
	mov.n	a10, a3	# iftmp$37_133, iftmp$37_133
	wfr	f4, a8	#,
	olt.s	b0, f0, f4	#, _129,
	bt	b0, .L17	#,
	l32r	a8, .LC19	#,
	utrunc.s	a11, f0, 0	# tmp175, _129
	wfr	f1, a8	#,
	olt.s	b0, f1, f0	#,, _129
	mov.n	a10, a7	# iftmp$37_133, iftmp$37_133
	bt	b0, .L17	#,
	extui	a10, a11, 0, 8	# iftmp$37_133, tmp175
.L17:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:80:         radiusLUT[i] = clamp8f(r * scale);
	s8i	a10, a5, 0	# MEM[base: _175, offset: 0B], iftmp$37_133
	addi.n	a5, a5, 1	# ivtmp$100, ivtmp$100,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:78:     for (int i = 0; i < 256; i++) {
	bne	a2, a4, .L18	# ivtmp$99, tmp266,
	addmi	a4, a6, 0x100	# ivtmp$92, pretmp_171,
	movi.n	a3, 0x40	# tmp280,
	loop	a3, .L19_LEND	# tmp280,
.L19:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:86:         radiusLUT[i] = radiusLUT[255];
	l8ui	a7, a6, 255	# MEM[(uint8_t *)pretmp_171 + 255B], _140
	s8i	a7, a4, 0	# MEM[base: _181, offset: 0B], _140
	addi.n	a4, a4, 1	# ivtmp$92, ivtmp$92,
	.L19_LEND:		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:137:         lastGlow = p[1];
	l32i.n	a2, sp, 4	# %sfp,
	l32r	a8, .LC15	#,
	l8ui	a3, a2, 1	# MEM[(const uint8_t *)p_56(D) + 1B],
	s8i	a3, a8, 0	# lastGlow, MEM[(const uint8_t *)p_56(D) + 1B]
.L16:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:139:     const float spd = speedMul(p[0]);
	l32i.n	a2, sp, 4	# %sfp,
	l8ui	a3, a2, 0	# *p_56(D), *p_56(D)
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	l32r	a2, .LC23	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	addi	a3, a3, -50	# tmp181, *p_56(D),
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	float.s	f0, a3, 0	# tmp182, tmp181
	wfr	f1, a2	#,
	mul.s	f0, f0, f1	#, tmp182,
	rfr	a10, f0	#,
	call8	exp2f		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:142:     const uint32_t vt = static_cast<uint32_t>(tMs * spd);
	l32i.n	a3, sp, 8	# %sfp,
	wfr	f1, a10	#,
	ufloat.s	f0, a3, 0	# tmp185,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:143:     const float pulseGain = p[3] / 100.0f;
	l32i.n	a8, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:142:     const uint32_t vt = static_cast<uint32_t>(tMs * spd);
	mul.s	f0, f0, f1	# tmp186, tmp185,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:143:     const float pulseGain = p[3] / 100.0f;
	l8ui	a2, a8, 3	# MEM[(const uint8_t *)p_56(D) + 3B], MEM[(const uint8_t *)p_56(D) + 3B]
# ../../src/display/ui/default/bganim/AnimEmber.cpp:143:     const float pulseGain = p[3] / 100.0f;
	l32r	a11, .LC24	#,
	float.s	f2, a2, 0	#, MEM[(const uint8_t *)p_56(D) + 3B]
# ../../src/display/ui/default/bganim/AnimEmber.cpp:142:     const uint32_t vt = static_cast<uint32_t>(tMs * spd);
	utrunc.s	a8, f0, 0	# vt, tmp186
# ../../src/display/ui/default/bganim/AnimEmber.cpp:143:     const float pulseGain = p[3] / 100.0f;
	rfr	a10, f2	#,
	s32i.n	a8, sp, 24	#,
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:19: inline int16_t sin1024(uint32_t idx) { return sinLut()[idx & (SIN_N - 1)]; }
	s32i.n	a10, sp, 20	#, tmp22
	call8	_ZN6bganim6sinLutEv		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:144:     const float s1 = sin1024((vt * STEP1) >> 22) * (1.0f / SIN_AMP);
	l32i.n	a8, sp, 24	#,
	l32r	a2, .LC25	#, tmp191
	mull	a2, a8, a2	# tmp190, vt, tmp191
# ../../src/display/ui/default/bganim/AnimEmber.cpp:144:     const float s1 = sin1024((vt * STEP1) >> 22) * (1.0f / SIN_AMP);
	extui	a2, a2, 22, 10	# tmp192, tmp190,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:19: inline int16_t sin1024(uint32_t idx) { return sinLut()[idx & (SIN_N - 1)]; }
	slli	a2, a2, 1	# tmp193, tmp192,
	add.n	a2, a10, a2	# tmp194,, tmp193
# ../../src/display/ui/default/bganim/AnimEmber.cpp:144:     const float s1 = sin1024((vt * STEP1) >> 22) * (1.0f / SIN_AMP);
	l16si	a2, a2, 0	# *_93, *_93
	float.s	f2, a2, 0	# tmp195, *_93
# ../../src/display/ui/default/bganim/AnimEmber.cpp:144:     const float s1 = sin1024((vt * STEP1) >> 22) * (1.0f / SIN_AMP);
	l32r	a2, .LC26	#,
	wfr	f4, a2	#,
	mul.s	f2, f2, f4	# s1, tmp195,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:19: inline int16_t sin1024(uint32_t idx) { return sinLut()[idx & (SIN_N - 1)]; }
	ssi	f2, sp, 16	#,
	call8	_ZN6bganim6sinLutEv		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:145:     const float s2 = sin1024(((vt * STEP2) + PHOFF2) >> 22) * (1.0f / SIN_AMP);
	l32i.n	a8, sp, 24	#,
	l32r	a2, .LC27	#, tmp200
# ../../src/display/ui/default/bganim/AnimEmber.cpp:145:     const float s2 = sin1024(((vt * STEP2) + PHOFF2) >> 22) * (1.0f / SIN_AMP);
	l32r	a3, .LC28	#, tmp202
# ../../src/display/ui/default/bganim/AnimEmber.cpp:145:     const float s2 = sin1024(((vt * STEP2) + PHOFF2) >> 22) * (1.0f / SIN_AMP);
	mull	a2, a8, a2	# tmp199, vt, tmp200
# ../../src/display/ui/default/bganim/AnimEmber.cpp:145:     const float s2 = sin1024(((vt * STEP2) + PHOFF2) >> 22) * (1.0f / SIN_AMP);
	add.n	a2, a2, a3	# tmp201, tmp199, tmp202
# ../../src/display/ui/default/bganim/AnimEmber.cpp:145:     const float s2 = sin1024(((vt * STEP2) + PHOFF2) >> 22) * (1.0f / SIN_AMP);
	extui	a2, a2, 22, 10	# tmp203, tmp201,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:19: inline int16_t sin1024(uint32_t idx) { return sinLut()[idx & (SIN_N - 1)]; }
	slli	a2, a2, 1	# tmp204, tmp203,
	add.n	a2, a10, a2	# tmp205,, tmp204
# ../../src/display/ui/default/bganim/AnimEmber.cpp:145:     const float s2 = sin1024(((vt * STEP2) + PHOFF2) >> 22) * (1.0f / SIN_AMP);
	l16si	a2, a2, 0	# *_89, *_89
	float.s	f0, a2, 0	# tmp206, *_89
# ../../src/display/ui/default/bganim/AnimEmber.cpp:145:     const float s2 = sin1024(((vt * STEP2) + PHOFF2) >> 22) * (1.0f / SIN_AMP);
	l32r	a2, .LC26	#,
	wfr	f1, a2	#,
	mul.s	f0, f0, f1	# s2, tmp206,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:19: inline int16_t sin1024(uint32_t idx) { return sinLut()[idx & (SIN_N - 1)]; }
	ssi	f0, sp, 12	#,
	call8	_ZN6bganim6sinLutEv		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:146:     const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
	l32i.n	a8, sp, 24	#,
	l32r	a2, .LC30	#, tmp212
# ../../src/display/ui/default/bganim/AnimEmber.cpp:146:     const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
	l32r	a3, .LC31	#, tmp214
# ../../src/display/ui/default/bganim/AnimEmber.cpp:146:     const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
	mull	a2, a8, a2	# tmp211, vt, tmp212
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	lsi	f0, sp, 12	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:146:     const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
	add.n	a2, a2, a3	# tmp213, tmp211, tmp214
# ../../src/display/ui/default/bganim/AnimEmber.cpp:146:     const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
	extui	a2, a2, 22, 10	# tmp215, tmp213,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:19: inline int16_t sin1024(uint32_t idx) { return sinLut()[idx & (SIN_N - 1)]; }
	slli	a2, a2, 1	# tmp216, tmp215,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	l32r	a3, .LC32	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:19: inline int16_t sin1024(uint32_t idx) { return sinLut()[idx & (SIN_N - 1)]; }
	add.n	a2, a10, a2	# tmp217,, tmp216
# ../../src/display/ui/default/bganim/AnimEmber.cpp:146:     const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
	l16si	a2, a2, 0	# *_85, *_85
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	wfr	f1, a3	#,
	mul.s	f0, f0, f1	# tmp223, s2,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:146:     const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
	float.s	f1, a2, 0	# tmp218, *_85
# ../../src/display/ui/default/bganim/AnimEmber.cpp:146:     const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
	l32r	a2, .LC26	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	lsi	f2, sp, 16	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:146:     const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
	wfr	f4, a2	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	l32r	a2, .LC33	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:146:     const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
	mul.s	f1, f1, f4	# s3, tmp218,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	wfr	f4, a2	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:148:     g_flickerAmp = (10 * p[2]) / 100;
	l32i.n	a2, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	madd.s	f0, f2, f4	# tmp225, s1,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:148:     g_flickerAmp = (10 * p[2]) / 100;
	l8ui	a3, a2, 2	# MEM[(const uint8_t *)p_56(D) + 2B], _35
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	l32r	a2, .LC34	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	lsi	f3, sp, 20	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	wfr	f2, a2	#,
	madd.s	f0, f1, f2	# tmp227, s3,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	l32r	a2, .LC35	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:148:     g_flickerAmp = (10 * p[2]) / 100;
	l32r	a7, .LC36	#, tmp234
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	wfr	f1, a2	#,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	mul.s	f0, f0, f3	# tmp229, tmp227, tmp189
# ../../src/display/ui/default/bganim/AnimEmber.cpp:148:     g_flickerAmp = (10 * p[2]) / 100;
	muluh	a7, a3, a7	# tmp264, _35, tmp234
	l32r	a2, .LC38	#, tmp241
# ../../src/display/ui/default/bganim/AnimEmber.cpp:149:     if (p[2] != lastFlickerParam) {
	l32r	a9, .LC39	#, tmp267
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	mul.s	f0, f0, f1	# tmp230, tmp229,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:148:     g_flickerAmp = (10 * p[2]) / 100;
	srli	a7, a7, 3	# _37, tmp264,
	s32i.n	a7, a2, 0	# g_flickerAmp, _37
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	l32r	a6, .LC29	#, tmp210
	trunc.s	a2, f0, 0	# tmp232, tmp230
# ../../src/display/ui/default/bganim/AnimEmber.cpp:149:     if (p[2] != lastFlickerParam) {
	l8ui	a4, a9, 0	# lastFlickerParam, lastFlickerParam
# ../../src/display/ui/default/bganim/AnimEmber.cpp:147:     g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
	s32i.n	a2, a6, 0	# g_breathe, tmp232
# ../../src/display/ui/default/bganim/AnimEmber.cpp:149:     if (p[2] != lastFlickerParam) {
	beq	a4, a3, .L20	# lastFlickerParam, _35,
	l32r	a3, .LC40	#, tmp248
	slli	a2, a7, 7	# tmp246, _37,
	l32i.n	a4, a3, 0	# flickerLUT, ivtmp$85
	neg	a2, a2	# ivtmp$84, tmp246
	movi	a3, 0x100	# tmp279,
	loop	a3, .L21_LEND	# tmp279,
.L21:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:103:         flickerLUT[i] = static_cast<int8_t>(((i - 128) * flickerAmp) >> 7);
	srai	a6, a2, 7	# tmp249, ivtmp$84,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:103:         flickerLUT[i] = static_cast<int8_t>(((i - 128) * flickerAmp) >> 7);
	s8i	a6, a4, 0	# MEM[base: _118, offset: 0B], tmp249
	add.n	a2, a2, a7	# ivtmp$84, ivtmp$84, _37
	addi.n	a4, a4, 1	# ivtmp$85, ivtmp$85,
	.L21_LEND:		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:151:         lastFlickerParam = p[2];
	l32i.n	a3, sp, 4	# %sfp,
	l8ui	a2, a3, 2	# MEM[(const uint8_t *)p_56(D) + 2B],
	s8i	a2, a9, 0	# lastFlickerParam, MEM[(const uint8_t *)p_56(D) + 2B]
.L20:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:153:     g_sx = static_cast<int>((vt * 6u) >> 10) & 255;
	slli	a2, a8, 1	# tmp254, vt,
	add.n	a2, a2, a8	# tmp255, tmp254, vt
# ../../src/display/ui/default/bganim/AnimEmber.cpp:153:     g_sx = static_cast<int>((vt * 6u) >> 10) & 255;
	l32r	a3, .LC41	#, tmp252
# ../../src/display/ui/default/bganim/AnimEmber.cpp:153:     g_sx = static_cast<int>((vt * 6u) >> 10) & 255;
	extui	a2, a2, 9, 8	# tmp258, tmp255,,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:153:     g_sx = static_cast<int>((vt * 6u) >> 10) & 255;
	s32i.n	a2, a3, 0	# g_sx, tmp258
# ../../src/display/ui/default/bganim/AnimEmber.cpp:154:     g_sy = static_cast<int>((vt * 4u) >> 10) & 255;
	l32r	a2, .LC42	#, tmp259
# ../../src/display/ui/default/bganim/AnimEmber.cpp:154:     g_sy = static_cast<int>((vt * 4u) >> 10) & 255;
	extui	a8, a8, 8, 8	# tmp262, vt,,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:154:     g_sy = static_cast<int>((vt * 4u) >> 10) & 255;
	s32i.n	a8, a2, 0	# g_sy, tmp262
# ../../src/display/ui/default/bganim/AnimEmber.cpp:155: }
	retw.n
	.size	_ZN12_GLOBAL__N_15frameEjiiPKh, .-_ZN12_GLOBAL__N_15frameEjiiPKh
	.section	.text._ZN12_GLOBAL__N_14initEii,"ax",@progbits
	.literal_position
	.literal .LC43, _ZN12_GLOBAL__N_110paletteExtE
	.literal .LC44, _ZN12_GLOBAL__N_19radiusLUTE
	.literal .LC45, _ZN12_GLOBAL__N_110flickerLUTE
	.literal .LC46, _ZN12_GLOBAL__N_15noiseE
	.literal .LC47, _ZN12_GLOBAL__N_17paletteE
	.literal .LC48, _ZN12_GLOBAL__N_14g_cxE
	.literal .LC49, 715827883
	.literal .LC50, _ZN12_GLOBAL__N_14g_cyE
	.literal .LC51, _ZN12_GLOBAL__N_16g_maxRE
	.literal .LC52, _ZN12_GLOBAL__N_112lastThemeGenE
	.literal .LC53, _ZN12_GLOBAL__N_18lastGlowE
	.literal .LC54, _ZN12_GLOBAL__N_116lastFlickerParamE
	.align	4
	.type	_ZN12_GLOBAL__N_14initEii, @function
_ZN12_GLOBAL__N_14initEii:
	entry	sp, 32	#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:108:     if (paletteExt == nullptr) {
	l32r	a4, .LC43	#, tmp68
# ../../src/display/ui/default/bganim/AnimEmber.cpp:108:     if (paletteExt == nullptr) {
	l32i.n	a7, a4, 0	# paletteExt, paletteExt
	bnez.n	a7, .L29	# paletteExt,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:109:         paletteExt = static_cast<uint16_t *>(alloc(PAL_EXT_N * sizeof(uint16_t)));
	movi	a10, 0x300	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:109:         paletteExt = static_cast<uint16_t *>(alloc(PAL_EXT_N * sizeof(uint16_t)));
	s32i.n	a10, a4, 0	# paletteExt,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:110:         radiusLUT = static_cast<uint8_t *>(alloc(RLUT_N));
	movi	a10, 0x140	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:110:         radiusLUT = static_cast<uint8_t *>(alloc(RLUT_N));
	l32r	a5, .LC44	#, tmp71
	s32i.n	a10, a5, 0	# radiusLUT,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:111:         flickerLUT = static_cast<int8_t *>(alloc(256));
	movi	a10, 0x100	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:111:         flickerLUT = static_cast<int8_t *>(alloc(256));
	l32r	a6, .LC45	#, tmp72
	s32i.n	a10, a6, 0	# flickerLUT,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:112:         noise = noiseTex256();
	call8	_ZN6bganim11noiseTex256Ev		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:113:         if (paletteExt == nullptr || radiusLUT == nullptr || flickerLUT == nullptr || noise == nullptr) {
	l32i.n	a8, a4, 0	# paletteExt, paletteExt$41_6
# ../../src/display/ui/default/bganim/AnimEmber.cpp:112:         noise = noiseTex256();
	l32r	a4, .LC46	#, tmp73
	s32i.n	a10, a4, 0	# noise, _5
# ../../src/display/ui/default/bganim/AnimEmber.cpp:113:         if (paletteExt == nullptr || radiusLUT == nullptr || flickerLUT == nullptr || noise == nullptr) {
	beqz.n	a8, .L33	# paletteExt$41_6,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:113:         if (paletteExt == nullptr || radiusLUT == nullptr || flickerLUT == nullptr || noise == nullptr) {
	l32i.n	a4, a5, 0	# radiusLUT, radiusLUT
	beqz.n	a4, .L33	# radiusLUT,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:113:         if (paletteExt == nullptr || radiusLUT == nullptr || flickerLUT == nullptr || noise == nullptr) {
	l32i.n	a4, a6, 0	# flickerLUT, flickerLUT
	movi.n	a5, 1	# tmp81,
	moveqz	a7, a5, a4	# tmp80, tmp81, flickerLUT
# ../../src/display/ui/default/bganim/AnimEmber.cpp:113:         if (paletteExt == nullptr || radiusLUT == nullptr || flickerLUT == nullptr || noise == nullptr) {
	extui	a7, a7, 0, 8	# tmp83, tmp80
	bnez.n	a7, .L33	# tmp83,
	moveqz	a7, a5, a10	# tmp88, tmp81, _5
	bnez.n	a7, .L33	# tmp88,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:116:         palette = paletteExt + PAD;
	movi	a4, 0x80	# tmp90,
	add.n	a8, a8, a4	# tmp91, paletteExt$41_6, tmp90
# ../../src/display/ui/default/bganim/AnimEmber.cpp:116:         palette = paletteExt + PAD;
	l32r	a4, .LC47	#, tmp89
	s32i.n	a8, a4, 0	# palette, tmp91
.L29:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:119:     g_cy = h / 2 + (20 * h) / 480;
	l32r	a9, .LC49	#, tmp102
# ../../src/display/ui/default/bganim/AnimEmber.cpp:119:     g_cy = h / 2 + (20 * h) / 480;
	extui	a8, a3, 31, 1	# tmp97, h,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:119:     g_cy = h / 2 + (20 * h) / 480;
	mulsh	a9, a3, a9	# tmp120, h, tmp102
# ../../src/display/ui/default/bganim/AnimEmber.cpp:119:     g_cy = h / 2 + (20 * h) / 480;
	add.n	a8, a8, a3	# tmp98, tmp97, h
# ../../src/display/ui/default/bganim/AnimEmber.cpp:119:     g_cy = h / 2 + (20 * h) / 480;
	srai	a4, a3, 31	# tmp105, h,
	srai	a9, a9, 2	# tmp104, tmp120,
	sub	a9, a9, a4	# tmp100, tmp104, tmp105
# ../../src/display/ui/default/bganim/AnimEmber.cpp:119:     g_cy = h / 2 + (20 * h) / 480;
	srai	a8, a8, 1	# tmp99, tmp98,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:119:     g_cy = h / 2 + (20 * h) / 480;
	add.n	a8, a8, a9	# _13, tmp99, tmp100
# ../../src/display/ui/default/bganim/AnimEmber.cpp:121:     const float dy = static_cast<float>(g_cy > h - g_cy ? g_cy : h - g_cy);
	sub	a3, a3, a8	# tmp108, h, _13
# ../../src/display/ui/default/bganim/AnimEmber.cpp:121:     const float dy = static_cast<float>(g_cy > h - g_cy ? g_cy : h - g_cy);
	max	a3, a3, a8	# tmp107, tmp108, _13
# ../../src/display/ui/default/bganim/AnimEmber.cpp:121:     const float dy = static_cast<float>(g_cy > h - g_cy ? g_cy : h - g_cy);
	float.s	f0, a3, 0	# dy, tmp107
# ../../src/display/ui/default/bganim/AnimEmber.cpp:118:     g_cx = w / 2;
	extui	a3, a2, 31, 1	# tmp92, w,
	add.n	a2, a3, a2	# tmp93, tmp92, w
# ../../src/display/ui/default/bganim/AnimEmber.cpp:122:     g_maxR = sqrtf(dx * dx + dy * dy);
	mul.s	f0, f0, f0	# tmp109, dy, dy
# ../../src/display/ui/default/bganim/AnimEmber.cpp:118:     g_cx = w / 2;
	srai	a2, a2, 1	# tmp94, tmp93,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:120:     const float dx = static_cast<float>(g_cx);
	float.s	f1, a2, 0	# dx, tmp94
# ../../src/display/ui/default/bganim/AnimEmber.cpp:118:     g_cx = w / 2;
	l32r	a3, .LC48	#, tmp95
# ../../src/display/ui/default/bganim/AnimEmber.cpp:122:     g_maxR = sqrtf(dx * dx + dy * dy);
	madd.s	f0, f1, f1	# tmp109, dx, dx
# ../../src/display/ui/default/bganim/AnimEmber.cpp:118:     g_cx = w / 2;
	s32i.n	a2, a3, 0	# g_cx, tmp94
# ../../src/display/ui/default/bganim/AnimEmber.cpp:119:     g_cy = h / 2 + (20 * h) / 480;
	l32r	a2, .LC50	#, tmp106
# ../../src/display/ui/default/bganim/AnimEmber.cpp:122:     g_maxR = sqrtf(dx * dx + dy * dy);
	rfr	a10, f0	#, tmp109
# ../../src/display/ui/default/bganim/AnimEmber.cpp:119:     g_cy = h / 2 + (20 * h) / 480;
	s32i.n	a8, a2, 0	# g_cy, _13
# ../../src/display/ui/default/bganim/AnimEmber.cpp:122:     g_maxR = sqrtf(dx * dx + dy * dy);
	call8	sqrtf		#
# ../../src/display/ui/default/bganim/AnimEmber.cpp:122:     g_maxR = sqrtf(dx * dx + dy * dy);
	l32r	a3, .LC51	#, tmp111
# ../../src/display/ui/default/bganim/AnimEmber.cpp:123:     lastThemeGen = 0xFFFFFFFF;
	movi.n	a2, -1	# tmp113,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:122:     g_maxR = sqrtf(dx * dx + dy * dy);
	s32i.n	a10, a3, 0	# g_maxR,
# ../../src/display/ui/default/bganim/AnimEmber.cpp:123:     lastThemeGen = 0xFFFFFFFF;
	l32r	a3, .LC52	#, tmp112
	s32i.n	a2, a3, 0	# lastThemeGen, tmp113
# ../../src/display/ui/default/bganim/AnimEmber.cpp:124:     lastGlow = 255;
	l32r	a3, .LC53	#, tmp114
	s8i	a2, a3, 0	# lastGlow, tmp113
# ../../src/display/ui/default/bganim/AnimEmber.cpp:125:     lastFlickerParam = 255;
	l32r	a3, .LC54	#, tmp116
	s8i	a2, a3, 0	# lastFlickerParam, tmp113
# ../../src/display/ui/default/bganim/AnimEmber.cpp:126:     return true;
	movi.n	a2, 1	# <retval>,
	j	.L30		#
.L33:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:114:             return false;
	movi.n	a2, 0	# <retval>,
.L30:
# ../../src/display/ui/default/bganim/AnimEmber.cpp:127: }
	retw.n
	.size	_ZN12_GLOBAL__N_14initEii, .-_ZN12_GLOBAL__N_14initEii
	.global	bg_anim_ember
	.section	.rodata.str1.4,"aMS",@progbits,1
	.align	4
.LC55:
	.string	"ember"
	.align	4
.LC56:
	.string	"Ember"
	.align	4
.LC57:
	.string	"speed"
	.align	4
.LC58:
	.string	"Speed"
	.align	4
.LC59:
	.string	"glow"
	.align	4
.LC60:
	.string	"Glow size"
	.align	4
.LC61:
	.string	"flicker"
	.align	4
.LC62:
	.string	"Flicker"
	.align	4
.LC63:
	.string	"pulse"
	.align	4
.LC64:
	.string	"Pulse"
	.section	.rodata
	.align	4
	.type	bg_anim_ember, @object
	.size	bg_anim_ember, 68
bg_anim_ember:
# id:
	.word	.LC55
# name:
	.word	.LC56
# params:
# key:
	.word	.LC57
# label:
	.word	.LC58
# def:
	.byte	50
	.zero	3
# key:
	.word	.LC59
# label:
	.word	.LC60
# def:
	.byte	45
	.zero	3
# key:
	.word	.LC61
# label:
	.word	.LC62
# def:
	.byte	20
	.zero	3
# key:
	.word	.LC63
# label:
	.word	.LC64
# def:
	.byte	50
	.zero	3
# init:
	.word	_ZN12_GLOBAL__N_14initEii
# frame:
	.word	_ZN12_GLOBAL__N_15frameEjiiPKh
# band:
	.word	_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.local	_ZN12_GLOBAL__N_14g_syE
	.comm	_ZN12_GLOBAL__N_14g_syE,4,4
	.local	_ZN12_GLOBAL__N_14g_sxE
	.comm	_ZN12_GLOBAL__N_14g_sxE,4,4
	.local	_ZN12_GLOBAL__N_112g_flickerAmpE
	.comm	_ZN12_GLOBAL__N_112g_flickerAmpE,4,4
	.local	_ZN12_GLOBAL__N_19g_breatheE
	.comm	_ZN12_GLOBAL__N_19g_breatheE,4,4
	.data
	.align	4
	.type	_ZN12_GLOBAL__N_16g_maxRE, @object
	.size	_ZN12_GLOBAL__N_16g_maxRE, 4
_ZN12_GLOBAL__N_16g_maxRE:
	.word	1135663514
	.align	4
	.type	_ZN12_GLOBAL__N_14g_cyE, @object
	.size	_ZN12_GLOBAL__N_14g_cyE, 4
_ZN12_GLOBAL__N_14g_cyE:
	.word	260
	.align	4
	.type	_ZN12_GLOBAL__N_14g_cxE, @object
	.size	_ZN12_GLOBAL__N_14g_cxE, 4
_ZN12_GLOBAL__N_14g_cxE:
	.word	240
	.type	_ZN12_GLOBAL__N_116lastFlickerParamE, @object
	.size	_ZN12_GLOBAL__N_116lastFlickerParamE, 1
_ZN12_GLOBAL__N_116lastFlickerParamE:
	.byte	-1
	.type	_ZN12_GLOBAL__N_18lastGlowE, @object
	.size	_ZN12_GLOBAL__N_18lastGlowE, 1
_ZN12_GLOBAL__N_18lastGlowE:
	.byte	-1
	.align	4
	.type	_ZN12_GLOBAL__N_112lastThemeGenE, @object
	.size	_ZN12_GLOBAL__N_112lastThemeGenE, 4
_ZN12_GLOBAL__N_112lastThemeGenE:
	.word	-1
	.local	_ZN12_GLOBAL__N_15noiseE
	.comm	_ZN12_GLOBAL__N_15noiseE,4,4
	.local	_ZN12_GLOBAL__N_110flickerLUTE
	.comm	_ZN12_GLOBAL__N_110flickerLUTE,4,4
	.local	_ZN12_GLOBAL__N_19radiusLUTE
	.comm	_ZN12_GLOBAL__N_19radiusLUTE,4,4
	.local	_ZN12_GLOBAL__N_17paletteE
	.comm	_ZN12_GLOBAL__N_17paletteE,4,4
	.local	_ZN12_GLOBAL__N_110paletteExtE
	.comm	_ZN12_GLOBAL__N_110paletteExtE,4,4
	.ident	"GCC: (crosstool-NG esp-2021r2-patch5) 8.4.0"
