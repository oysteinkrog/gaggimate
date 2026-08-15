	.file	"AnimNebula.cpp"
# GNU C++17 (crosstool-NG esp-2021r2-patch5) version 8.4.0 (xtensa-esp32s3-elf)
#	compiled by GNU C version 6.3.0 20170516, GMP version 6.2.1, MPFR version 4.1.0, MPC version 1.2.1, isl version isl-0.19-GMP

# GGC heuristics: --param ggc-min-expand=100 --param ggc-min-heapsize=131072
# options passed:  -I shim -imultilib no-rtti
# -iprefix c:\users\oystein\.platformio\packages\toolchain-xtensa-esp32s3\bin\../lib/gcc/xtensa-esp32s3-elf/8.4.0/
# ../../src/display/ui/default/bganim/AnimNebula.cpp -mlongcalls
# -auxbase-strip xtensa-asm/AnimNebula.S -O2 -std=gnu++17 -fno-exceptions
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
	.literal .LC0, _ZN12_GLOBAL__N_15g_axFE
	.literal .LC1, _ZN12_GLOBAL__N_15g_ayFE
	.literal .LC2, _ZN12_GLOBAL__N_14g_wAE
	.literal .LC3, _ZN12_GLOBAL__N_14g_wBE
	.literal .LC4, _ZN12_GLOBAL__N_19g_densOffE
	.literal .LC5, _ZN12_GLOBAL__N_15noiseE
	.literal .LC6, _ZN12_GLOBAL__N_15g_ayIE
	.literal .LC7, _ZN12_GLOBAL__N_14g_byE
	.literal .LC8, _ZN12_GLOBAL__N_15g_axIE
	.literal .LC9, _ZN12_GLOBAL__N_14g_bxE
	.literal .LC10, _ZN12_GLOBAL__N_14g_cxE
	.literal .LC11, _ZN12_GLOBAL__N_14g_cyE
	.literal .LC12, _ZN6bganim6BAYER8E
	.literal .LC13, _ZZN12_GLOBAL__N_14bandEPtiiijPKhE8blendedA
	.literal .LC14, _ZN12_GLOBAL__N_17paletteE
	.align	4
	.type	_ZN12_GLOBAL__N_14bandEPtiiijPKh, @function
_ZN12_GLOBAL__N_14bandEPtiiijPKh:
	entry	sp, 128	#
# ../../src/display/ui/default/bganim/AnimNebula.cpp:64: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
	s32i.n	a5, sp, 56	# %sfp, w
# ../../src/display/ui/default/bganim/AnimNebula.cpp:65:     const int axF = g_axF, ayF = g_ayF, wA = g_wA, wB = g_wB, densOff = g_densOff;
	l32r	a5, .LC0	#, tmp193
# ../../src/display/ui/default/bganim/AnimNebula.cpp:64: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
# ../../src/display/ui/default/bganim/AnimNebula.cpp:65:     const int axF = g_axF, ayF = g_ayF, wA = g_wA, wB = g_wB, densOff = g_densOff;
	l32i.n	a8, a5, 0	# g_axF, axF
# ../../src/display/ui/default/bganim/AnimNebula.cpp:65:     const int axF = g_axF, ayF = g_ayF, wA = g_wA, wB = g_wB, densOff = g_densOff;
	l32r	a5, .LC1	#, tmp194
	l32i.n	a6, a5, 0	# g_ayF, ayF
# ../../src/display/ui/default/bganim/AnimNebula.cpp:65:     const int axF = g_axF, ayF = g_ayF, wA = g_wA, wB = g_wB, densOff = g_densOff;
	l32r	a5, .LC2	#, tmp195
	l32i.n	a5, a5, 0	# g_wA,
	s32i.n	a5, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:65:     const int axF = g_axF, ayF = g_ayF, wA = g_wA, wB = g_wB, densOff = g_densOff;
	l32r	a5, .LC3	#, tmp196
	l32i.n	a5, a5, 0	# g_wB,
	s32i.n	a5, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:65:     const int axF = g_axF, ayF = g_ayF, wA = g_wA, wB = g_wB, densOff = g_densOff;
	l32r	a5, .LC4	#, tmp197
	l32i.n	a5, a5, 0	# g_densOff,
	s32i.n	a5, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:67:     for (int r = 0; r < rows; r++) {
	blti	a4, 1, .L1	# rows,,
	l32r	a5, .LC6	#, tmp199
	l32r	a7, .LC11	#, tmp210
	l32i.n	a12, a5, 0	# g_ayI, pretmp_381
	add.n	a10, a3, a4	# tmp212, y0, rows
	l32i.n	a11, a7, 0	# g_cy, g_cy
	add.n	a4, a3, a12	# tmp207, y0, pretmp_381
	slli	a7, a3, 1	# tmp208, y0,
	slli	a9, a3, 2	# tmp209, y0,
	l32r	a3, .LC5	#, tmp198
	l32r	a5, .LC7	#, tmp200
	l32i.n	a3, a3, 0	# noise,
	l32i.n	a13, a5, 0	# g_by, pretmp_383
	s32i.n	a3, sp, 36	# %sfp,
	l32r	a3, .LC8	#, tmp201
	addi.n	a4, a4, 1	#, tmp207,
	l8ui	a3, a3, 0	# g_axI,
	slli	a10, a10, 1	# tmp213, tmp212,
	s32i	a3, sp, 72	# %sfp,
	l32r	a3, .LC9	#, tmp203
	s32i.n	a2, sp, 52	# %sfp, dst
	l8ui	a3, a3, 0	# g_bx,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:70:         const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
	movi.n	a2, -1	# tmp215,
	s32i	a3, sp, 76	# %sfp,
	l32r	a3, .LC10	#, tmp205
	s32i.n	a4, sp, 28	# %sfp,
	l8ui	a3, a3, 0	# g_cx,
	add.n	a4, a7, a13	#, tmp208, pretmp_383
	s32i	a3, sp, 80	# %sfp,
	l32i.n	a3, sp, 56	# %sfp,
	add.n	a11, a9, a11	#, tmp209, g_cy
	slli	a3, a3, 1	#,,
	add.n	a10, a10, a13	#, tmp213, pretmp_383
	xor	a2, a2, a12	#, tmp215, pretmp_381
	l32r	a5, .LC13	#, tmp280
	s32i	a3, sp, 64	# %sfp,
	s32i.n	a4, sp, 32	# %sfp,
	s32i.n	a11, sp, 48	# %sfp,
	s32i.n	a10, sp, 60	# %sfp,
	s32i	a2, sp, 68	# %sfp,
.L7:
	l32i	a2, sp, 68	# %sfp,
	l32i.n	a3, sp, 28	# %sfp,
	mov.n	a9, sp	# ivtmp$65,
	add.n	a4, a2, a3	# tmp216,,
	extui	a4, a4, 0, 3	# tmp217, tmp216,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:70:         const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
	l32r	a2, .LC12	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:70:         const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
	slli	a4, a4, 3	# tmp218, tmp217,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:70:         const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
	add.n	a4, a2, a4	# ivtmp$64,, tmp218
	movi.n	a7, 8	# tmp292,
	loop	a7, .L3_LEND	# tmp292,
.L3:
# ../../src/display/ui/default/bganim/AnimNebula.cpp:73:             dith[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 4);
	l8ui	a3, a4, 0	# MEM[base: _349, offset: 0B], MEM[base: _349, offset: 0B]
	addi.n	a4, a4, 1	# ivtmp$64, ivtmp$64,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:73:             dith[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 4);
	addi	a2, a3, -31	# tmp221, MEM[base: _349, offset: 0B],
# ../../src/display/ui/default/bganim/AnimNebula.cpp:73:             dith[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 4);
	addi	a3, a3, -28	# tmp224, MEM[base: _349, offset: 0B],
	movltz	a2, a3, a2	# tmp223, tmp224, tmp221
	srai	a2, a2, 2	# tmp225, tmp223,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:73:             dith[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 4);
	s8i	a2, a9, 0	# MEM[base: _348, offset: 0B], tmp225
	addi.n	a9, a9, 1	# ivtmp$65, ivtmp$65,
	.L3_LEND:		#
# ../../src/display/ui/default/bganim/AnimNebula.cpp:75:         const uint8_t *rowA0 = noise + ((y + g_ayI) & 255) * 256;
	l32i.n	a3, sp, 28	# %sfp,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:77:         const uint8_t *rowB = noise + ((y * 2 + g_by) & 255) * 256;
	l32i.n	a4, sp, 32	# %sfp,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:75:         const uint8_t *rowA0 = noise + ((y + g_ayI) & 255) * 256;
	addi.n	a13, a3, -1	# tmp226,,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:76:         const uint8_t *rowA1 = noise + ((y + g_ayI + 1) & 255) * 256;
	extui	a12, a3, 0, 8	# tmp229,,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:77:         const uint8_t *rowB = noise + ((y * 2 + g_by) & 255) * 256;
	extui	a3, a4, 0, 8	# tmp231,,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:78:         const uint8_t *rowC = noise + ((y * 4 + g_cy) & 255) * 256;
	l32i.n	a4, sp, 48	# %sfp,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:75:         const uint8_t *rowA0 = noise + ((y + g_ayI) & 255) * 256;
	extui	a13, a13, 0, 8	# tmp227, tmp226,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:78:         const uint8_t *rowC = noise + ((y * 4 + g_cy) & 255) * 256;
	extui	a2, a4, 0, 8	# tmp232,,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:75:         const uint8_t *rowA0 = noise + ((y + g_ayI) & 255) * 256;
	l32i.n	a4, sp, 36	# %sfp,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:75:         const uint8_t *rowA0 = noise + ((y + g_ayI) & 255) * 256;
	slli	a13, a13, 8	# tmp228, tmp227,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:76:         const uint8_t *rowA1 = noise + ((y + g_ayI + 1) & 255) * 256;
	slli	a12, a12, 8	# tmp230, tmp229,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:77:         const uint8_t *rowB = noise + ((y * 2 + g_by) & 255) * 256;
	slli	a3, a3, 8	#, tmp231,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:78:         const uint8_t *rowC = noise + ((y * 4 + g_cy) & 255) * 256;
	slli	a2, a2, 8	#, tmp232,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:75:         const uint8_t *rowA0 = noise + ((y + g_ayI) & 255) * 256;
	add.n	a13, a4, a13	# rowA0,, tmp228
# ../../src/display/ui/default/bganim/AnimNebula.cpp:76:         const uint8_t *rowA1 = noise + ((y + g_ayI + 1) & 255) * 256;
	add.n	a12, a4, a12	# rowA1,, tmp230
# ../../src/display/ui/default/bganim/AnimNebula.cpp:77:         const uint8_t *rowB = noise + ((y * 2 + g_by) & 255) * 256;
	s32i.n	a3, sp, 40	# %sfp,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:78:         const uint8_t *rowC = noise + ((y * 4 + g_cy) & 255) * 256;
	s32i.n	a2, sp, 44	# %sfp,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:80:         for (int i = 0; i < 256; i++) {
	movi.n	a4, 0	# i,
	movi	a7, 0x100	# tmp291,
.L4:
# ../../src/display/ui/default/bganim/AnimNebula.cpp:81:             const int i1 = (i + 1) & 255;
	addi.n	a3, a4, 1	# _35, i,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:81:             const int i1 = (i + 1) & 255;
	extui	a2, a3, 0, 8	# i1$11_36, _35,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:82:             const int da0 = static_cast<int>(rowA0[i1]) - static_cast<int>(rowA0[i]);
	add.n	a11, a13, a4	# tmp233, rowA0, i
# ../../src/display/ui/default/bganim/AnimNebula.cpp:84:             const int da1 = static_cast<int>(rowA1[i1]) - static_cast<int>(rowA1[i]);
	add.n	a10, a12, a4	# tmp239, rowA1, i
# ../../src/display/ui/default/bganim/AnimNebula.cpp:82:             const int da0 = static_cast<int>(rowA0[i1]) - static_cast<int>(rowA0[i]);
	add.n	a9, a13, a2	# tmp234, rowA0, i1$11_36
# ../../src/display/ui/default/bganim/AnimNebula.cpp:84:             const int da1 = static_cast<int>(rowA1[i1]) - static_cast<int>(rowA1[i]);
	add.n	a2, a12, a2	# tmp242, rowA1, i1$11_36
# ../../src/display/ui/default/bganim/AnimNebula.cpp:82:             const int da0 = static_cast<int>(rowA0[i1]) - static_cast<int>(rowA0[i]);
	l8ui	a11, a11, 0	# MEM[base: _364, offset: 0B], _43
# ../../src/display/ui/default/bganim/AnimNebula.cpp:84:             const int da1 = static_cast<int>(rowA1[i1]) - static_cast<int>(rowA1[i]);
	l8ui	a10, a10, 0	# MEM[base: _360, offset: 0B], _51
# ../../src/display/ui/default/bganim/AnimNebula.cpp:82:             const int da0 = static_cast<int>(rowA0[i1]) - static_cast<int>(rowA0[i]);
	l8ui	a9, a9, 0	# *_37, *_37
# ../../src/display/ui/default/bganim/AnimNebula.cpp:84:             const int da1 = static_cast<int>(rowA1[i1]) - static_cast<int>(rowA1[i]);
	l8ui	a2, a2, 0	# *_46, *_46
# ../../src/display/ui/default/bganim/AnimNebula.cpp:82:             const int da0 = static_cast<int>(rowA0[i1]) - static_cast<int>(rowA0[i]);
	sub	a9, a9, a11	# da0, *_37, _43
# ../../src/display/ui/default/bganim/AnimNebula.cpp:84:             const int da1 = static_cast<int>(rowA1[i1]) - static_cast<int>(rowA1[i]);
	sub	a2, a2, a10	# da1, *_46, _51
# ../../src/display/ui/default/bganim/AnimNebula.cpp:83:             const int va = rowA0[i] + ((da0 * axF) >> 8);
	mull	a9, a9, a8	# tmp237, da0, axF
# ../../src/display/ui/default/bganim/AnimNebula.cpp:85:             const int vb = rowA1[i] + ((da1 * axF) >> 8);
	mull	a2, a2, a8	# tmp245, da1, axF
# ../../src/display/ui/default/bganim/AnimNebula.cpp:83:             const int va = rowA0[i] + ((da0 * axF) >> 8);
	srai	a9, a9, 8	# tmp238, tmp237,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:85:             const int vb = rowA1[i] + ((da1 * axF) >> 8);
	srai	a2, a2, 8	# tmp246, tmp245,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:83:             const int va = rowA0[i] + ((da0 * axF) >> 8);
	add.n	a9, a9, a11	# va, tmp238, _43
# ../../src/display/ui/default/bganim/AnimNebula.cpp:85:             const int vb = rowA1[i] + ((da1 * axF) >> 8);
	add.n	a2, a2, a10	# vb, tmp246, _51
# ../../src/display/ui/default/bganim/AnimNebula.cpp:86:             blendedA[i] = static_cast<uint8_t>(va + (((vb - va) * ayF) >> 8));
	sub	a2, a2, a9	# tmp248, vb, va
# ../../src/display/ui/default/bganim/AnimNebula.cpp:86:             blendedA[i] = static_cast<uint8_t>(va + (((vb - va) * ayF) >> 8));
	mull	a2, a2, a6	# tmp249, tmp248, ayF
# ../../src/display/ui/default/bganim/AnimNebula.cpp:86:             blendedA[i] = static_cast<uint8_t>(va + (((vb - va) * ayF) >> 8));
	add.n	a4, a5, a4	# tmp240, tmp280, i
# ../../src/display/ui/default/bganim/AnimNebula.cpp:86:             blendedA[i] = static_cast<uint8_t>(va + (((vb - va) * ayF) >> 8));
	srai	a2, a2, 8	# tmp250, tmp249,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:86:             blendedA[i] = static_cast<uint8_t>(va + (((vb - va) * ayF) >> 8));
	add.n	a2, a2, a9	# tmp253, tmp250, va
# ../../src/display/ui/default/bganim/AnimNebula.cpp:86:             blendedA[i] = static_cast<uint8_t>(va + (((vb - va) * ayF) >> 8));
	s8i	a2, a4, 0	# MEM[base: _356, offset: 0B], tmp253
	mov.n	a4, a3	# i, _35
	addi.n	a7, a7, -1	# tmp291, tmp291,
	bnez.n	a7, .L4	# tmp291,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:92:         for (int x = 0; x < w; x++) {
	l32i.n	a2, sp, 56	# %sfp,
	bgei	a2, 1, .L5	#,,
.L8:
	l32i.n	a3, sp, 32	# %sfp,
	l32i.n	a4, sp, 52	# %sfp,
	l32i	a2, sp, 64	# %sfp,
	addi.n	a3, a3, 2	#,,
	add.n	a4, a4, a2	#,,
	s32i.n	a3, sp, 32	# %sfp,
	l32i.n	a3, sp, 28	# %sfp,
	s32i.n	a4, sp, 52	# %sfp,
	l32i.n	a4, sp, 48	# %sfp,
	addi.n	a3, a3, 1	#,,
	s32i.n	a3, sp, 28	# %sfp,
	addi.n	a4, a4, 4	#,,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:67:     for (int r = 0; r < rows; r++) {
	l32i.n	a2, sp, 60	# %sfp,
	l32i.n	a3, sp, 32	# %sfp,
	s32i.n	a4, sp, 48	# %sfp,
	bne	a2, a3, .L7	#,,
	j	.L1		#
.L5:
	l32i.n	a3, sp, 44	# %sfp,
	l32i.n	a2, sp, 36	# %sfp,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:102:             row[x] = palette[v];
	l32r	a4, .LC14	#,
	add.n	a11, a2, a3	# tmp283,,
	l32i.n	a3, sp, 40	# %sfp,
	l32i.n	a15, a4, 0	# palette, palette$22_81
	l32i.n	a14, sp, 52	# %sfp, ivtmp$49
# ../../src/display/ui/default/bganim/AnimNebula.cpp:91:         int cIdx = g_cx & 255;
	l32i	a9, sp, 80	# %sfp, cIdx
# ../../src/display/ui/default/bganim/AnimNebula.cpp:90:         int bIdx = g_bx & 255;
	l32i	a7, sp, 76	# %sfp, bIdx
# ../../src/display/ui/default/bganim/AnimNebula.cpp:89:         int x0 = g_axI & 255;
	l32i	a4, sp, 72	# %sfp, x0
# ../../src/display/ui/default/bganim/AnimNebula.cpp:102:             row[x] = palette[v];
	l32i.n	a12, sp, 56	# %sfp, w
# ../../src/display/ui/default/bganim/AnimNebula.cpp:92:         for (int x = 0; x < w; x++) {
	movi.n	a13, 0	# x,
	add.n	a10, a2, a3	# tmp281,,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:102:             row[x] = palette[v];
	s32i.n	a8, sp, 40	# %sfp, axF
	s32i.n	a6, sp, 44	# %sfp, ayF
	loop	a12, .L9_LEND	# w,
.L9:
# ../../src/display/ui/default/bganim/AnimNebula.cpp:95:             const int c = rowC[cIdx];
	add.n	a6, a11, a9	# tmp256, tmp283, cIdx
# ../../src/display/ui/default/bganim/AnimNebula.cpp:94:             const int b = rowB[bIdx];
	add.n	a3, a10, a7	# tmp258, tmp281, bIdx
# ../../src/display/ui/default/bganim/AnimNebula.cpp:95:             const int c = rowC[cIdx];
	l8ui	a6, a6, 0	# *_68, c
# ../../src/display/ui/default/bganim/AnimNebula.cpp:94:             const int b = rowB[bIdx];
	l8ui	a3, a3, 0	# *_65, b
# ../../src/display/ui/default/bganim/AnimNebula.cpp:93:             const int a = blendedA[x0];
	add.n	a2, a5, a4	# tmp263, tmp280, x0
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	l32i.n	a8, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:93:             const int a = blendedA[x0];
	l8ui	a2, a2, 0	# blendedA, a
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	sub	a3, a3, a6	# tmp260, b, c
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	mull	a3, a3, a8	# tmp261, tmp260,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	l32i.n	a8, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	sub	a2, a2, a6	# tmp265, a, c
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	mull	a2, a2, a8	# tmp266, tmp265,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	extui	a8, a13, 0, 3	# tmp271, x,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	add.n	a2, a3, a2	# tmp267, tmp261, tmp266
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	add.n	a8, sp, a8	# tmp272,, tmp271
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	l32i.n	a3, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	srai	a2, a2, 6	# tmp268, tmp267,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	l8ui	a8, a8, 0	# dith,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	add.n	a2, a2, a6	# tmp269, tmp268, c
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	add.n	a2, a2, a3	# tmp270, tmp269,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	sext	a8, a8, 7	# tmp273, tmp274
# ../../src/display/ui/default/bganim/AnimNebula.cpp:96:             int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[x & 7];
	add.n	a8, a2, a8	# _100, tmp270, tmp273
	movi	a2, 0xff	#,
	min	a8, a8, a2	# _100, _100,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:102:             row[x] = palette[v];
	movi.n	a3, 0	#,
	max	a8, a8, a3	# v, _100,
	slli	a8, a8, 1	# tmp278, v,
	add.n	a8, a15, a8	# tmp279, palette$22_81, tmp278
	l16ui	a2, a8, 0	# *_84, _87
# ../../src/display/ui/default/bganim/AnimNebula.cpp:103:             x0 = (x0 + 1) & 255;
	addi.n	a4, a4, 1	# _88, x0,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:102:             row[x] = palette[v];
	s16i	a2, a14, 0	# MEM[base: _368, offset: 0B], _87
# ../../src/display/ui/default/bganim/AnimNebula.cpp:104:             bIdx = (bIdx + 2) & 255;
	addi.n	a7, a7, 2	# _89, bIdx,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:105:             cIdx = (cIdx + 4) & 255;
	addi.n	a9, a9, 4	# _90, cIdx,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:103:             x0 = (x0 + 1) & 255;
	extui	a4, a4, 0, 8	# x0, _88,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:104:             bIdx = (bIdx + 2) & 255;
	extui	a7, a7, 0, 8	# bIdx, _89,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:105:             cIdx = (cIdx + 4) & 255;
	extui	a9, a9, 0, 8	# cIdx, _90,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:92:         for (int x = 0; x < w; x++) {
	addi.n	a13, a13, 1	# x, x,
	addi.n	a14, a14, 2	# ivtmp$49, ivtmp$49,
	.L9_LEND:		#
	l32i.n	a8, sp, 40	# %sfp, axF
	l32i.n	a6, sp, 44	# %sfp, ayF
	j	.L8		#
.L1:
# ../../src/display/ui/default/bganim/AnimNebula.cpp:108: }
	retw.n
	.size	_ZN12_GLOBAL__N_14bandEPtiiijPKh, .-_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.global	__divsf3
	.section	.text._ZN12_GLOBAL__N_15frameEjiiPKh,"ax",@progbits
	.literal_position
	.literal .LC15, _ZN12_GLOBAL__N_112lastThemeGenE
	.literal .LC16, _ZN12_GLOBAL__N_17paletteE
	.literal .LC17, 0x3d610e10
	.literal .LC18, 0x3f99999a
	.literal .LC19, 0x42aa0000
	.literal .LC20, _ZN12_GLOBAL__N_13sAxE
	.literal .LC21, 0x424c0000
	.literal .LC22, _ZN12_GLOBAL__N_13sAyE
	.literal .LC23, _ZN12_GLOBAL__N_13sBxE
	.literal .LC24, 0x43110000
	.literal .LC25, 0x42de0000
	.literal .LC26, _ZN12_GLOBAL__N_13sByE
	.literal .LC27, 0x435e0000
	.literal .LC28, _ZN12_GLOBAL__N_13sCxE
	.literal .LC29, _ZN12_GLOBAL__N_13sCyE
	.literal .LC30, 0x43330000
	.literal .LC31, 0x42c80000
	.literal .LC32, _ZN12_GLOBAL__N_14g_wAE
	.literal .LC33, 0x3e19999a
	.literal .LC34, 0x3f19999a
	.literal .LC35, 0x42800000
	.literal .LC36, _ZN12_GLOBAL__N_14g_wBE
	.literal .LC37, 0x3d4ccccd
	.literal .LC38, 0x3e800000
	.literal .LC39, _ZN12_GLOBAL__N_19g_densOffE
	.literal .LC40, 0x3f8ccccd
	.literal .LC41, _ZN12_GLOBAL__N_15g_axIE
	.literal .LC42, _ZN12_GLOBAL__N_15g_axFE
	.literal .LC43, _ZN12_GLOBAL__N_15g_ayIE
	.literal .LC44, _ZN12_GLOBAL__N_15g_ayFE
	.literal .LC45, _ZN12_GLOBAL__N_14g_bxE
	.literal .LC46, _ZN12_GLOBAL__N_14g_byE
	.literal .LC47, _ZN12_GLOBAL__N_14g_cxE
	.literal .LC48, _ZN12_GLOBAL__N_14g_cyE
	.align	4
	.type	_ZN12_GLOBAL__N_15frameEjiiPKh, @function
_ZN12_GLOBAL__N_15frameEjiiPKh:
	entry	sp, 48	#
# ../../src/display/ui/default/bganim/AnimNebula.cpp:37:     if (themeGen() != lastThemeGen) {
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimNebula.cpp:37:     if (themeGen() != lastThemeGen) {
	l32r	a2, .LC15	#, tmp114
# ../../src/display/ui/default/bganim/AnimNebula.cpp:37:     if (themeGen() != lastThemeGen) {
	l32i.n	a3, a2, 0	# lastThemeGen, lastThemeGen
	beq	a10, a3, .L15	#, lastThemeGen,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:38:         buildThemeRamp(palette, 256);
	l32r	a3, .LC16	#, tmp116
	movi.n	a12, 0	#,
	l32i.n	a10, a3, 0	# palette,
	movi	a11, 0x100	#,
	call8	_ZN6bganim14buildThemeRampEPttb		#
# ../../src/display/ui/default/bganim/AnimNebula.cpp:39:         lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimNebula.cpp:39:         lastThemeGen = themeGen();
	s32i.n	a10, a2, 0	# lastThemeGen,
.L15:
# ../../src/display/ui/default/bganim/AnimNebula.cpp:42:     const float g = 1.2f * speedMul(p[0]);
	l8ui	a2, a5, 0	# *p_70(D), *p_70(D)
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	addi	a2, a2, -50	# tmp119, *p_70(D),
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	float.s	f0, a2, 0	# tmp120, tmp119
	l32r	a2, .LC17	#,
	wfr	f1, a2	#,
	mul.s	f0, f0, f1	#, tmp120,
	rfr	a10, f0	#,
	call8	exp2f		#
# ../../src/display/ui/default/bganim/AnimNebula.cpp:42:     const float g = 1.2f * speedMul(p[0]);
	l32r	a2, .LC18	#,
	wfr	f1, a10	#,
	wfr	f2, a2	#,
	mul.s	f0, f1, f2	# g,,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:43:     sAx += static_cast<uint16_t>(85.0f * g);
	l32r	a2, .LC19	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:44:     sAy += static_cast<uint16_t>(51.0f * g);
	l32r	a9, .LC22	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:43:     sAx += static_cast<uint16_t>(85.0f * g);
	wfr	f1, a2	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:44:     sAy += static_cast<uint16_t>(51.0f * g);
	l32r	a2, .LC21	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:43:     sAx += static_cast<uint16_t>(85.0f * g);
	mul.s	f5, f0, f1	# tmp124, g,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:44:     sAy += static_cast<uint16_t>(51.0f * g);
	wfr	f1, a2	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:45:     sBx -= static_cast<uint16_t>(145.0f * g);
	l32r	a2, .LC24	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:44:     sAy += static_cast<uint16_t>(51.0f * g);
	mul.s	f4, f0, f1	# tmp132, g,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:45:     sBx -= static_cast<uint16_t>(145.0f * g);
	wfr	f1, a2	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:46:     sBy += static_cast<uint16_t>(111.0f * g);
	l32r	a2, .LC25	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:45:     sBx -= static_cast<uint16_t>(145.0f * g);
	mul.s	f3, f0, f1	# tmp141, g,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:46:     sBy += static_cast<uint16_t>(111.0f * g);
	wfr	f1, a2	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:47:     sCx += static_cast<uint16_t>(222.0f * g);
	l32r	a2, .LC27	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:44:     sAy += static_cast<uint16_t>(51.0f * g);
	l16ui	a6, a9, 0	# sAy,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:47:     sCx += static_cast<uint16_t>(222.0f * g);
	l32r	a9, .LC28	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:47:     sCx += static_cast<uint16_t>(222.0f * g);
	wfr	f6, a2	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:43:     sAx += static_cast<uint16_t>(85.0f * g);
	l32r	a8, .LC20	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:48:     sCy -= static_cast<uint16_t>(179.0f * g);
	l32r	a2, .LC30	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:47:     sCx += static_cast<uint16_t>(222.0f * g);
	l16ui	a3, a9, 0	# sCx,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:44:     sAy += static_cast<uint16_t>(51.0f * g);
	utrunc.s	a9, f4, 0	# tmp135, tmp132
# ../../src/display/ui/default/bganim/AnimNebula.cpp:46:     sBy += static_cast<uint16_t>(111.0f * g);
	mul.s	f2, f0, f1	# tmp148, g,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:43:     sAx += static_cast<uint16_t>(85.0f * g);
	l16ui	a7, a8, 0	# sAx,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:47:     sCx += static_cast<uint16_t>(222.0f * g);
	mul.s	f1, f0, f6	# tmp156, g,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:43:     sAx += static_cast<uint16_t>(85.0f * g);
	utrunc.s	a11, f5, 0	# tmp127, tmp124
# ../../src/display/ui/default/bganim/AnimNebula.cpp:48:     sCy -= static_cast<uint16_t>(179.0f * g);
	wfr	f6, a2	#,
	mul.s	f0, f0, f6	# tmp165, g,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:46:     sBy += static_cast<uint16_t>(111.0f * g);
	l32r	a8, .LC26	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:44:     sAy += static_cast<uint16_t>(51.0f * g);
	add.n	a9, a9, a6	# tmp138, tmp135, sAy
	extui	a6, a9, 0, 16	# _14, tmp138
# ../../src/display/ui/default/bganim/AnimNebula.cpp:45:     sBx -= static_cast<uint16_t>(145.0f * g);
	l32r	a2, .LC23	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:43:     sAx += static_cast<uint16_t>(85.0f * g);
	l32r	a9, .LC20	#,
	add.n	a11, a11, a7	# tmp130, tmp127, sAx
# ../../src/display/ui/default/bganim/AnimNebula.cpp:46:     sBy += static_cast<uint16_t>(111.0f * g);
	l16ui	a4, a8, 0	# sBy,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:43:     sAx += static_cast<uint16_t>(85.0f * g);
	extui	a7, a11, 0, 16	# _10, tmp130
# ../../src/display/ui/default/bganim/AnimNebula.cpp:48:     sCy -= static_cast<uint16_t>(179.0f * g);
	l32r	a8, .LC29	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:43:     sAx += static_cast<uint16_t>(85.0f * g);
	s16i	a7, a9, 0	# sAx, _10
# ../../src/display/ui/default/bganim/AnimNebula.cpp:45:     sBx -= static_cast<uint16_t>(145.0f * g);
	l16ui	a14, a2, 0	# sBx,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:45:     sBx -= static_cast<uint16_t>(145.0f * g);
	utrunc.s	a15, f3, 0	# tmp144, tmp141
# ../../src/display/ui/default/bganim/AnimNebula.cpp:44:     sAy += static_cast<uint16_t>(51.0f * g);
	l32r	a9, .LC22	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:48:     sCy -= static_cast<uint16_t>(179.0f * g);
	l16ui	a2, a8, 0	# sCy,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:48:     sCy -= static_cast<uint16_t>(179.0f * g);
	utrunc.s	a8, f0, 0	# tmp168, tmp165
# ../../src/display/ui/default/bganim/AnimNebula.cpp:44:     sAy += static_cast<uint16_t>(51.0f * g);
	s16i	a6, a9, 0	# sAy, _14
# ../../src/display/ui/default/bganim/AnimNebula.cpp:45:     sBx -= static_cast<uint16_t>(145.0f * g);
	sub	a14, a14, a15	# tmp146, sBx, tmp144
# ../../src/display/ui/default/bganim/AnimNebula.cpp:46:     sBy += static_cast<uint16_t>(111.0f * g);
	utrunc.s	a13, f2, 0	# tmp151, tmp148
# ../../src/display/ui/default/bganim/AnimNebula.cpp:45:     sBx -= static_cast<uint16_t>(145.0f * g);
	l32r	a9, .LC23	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:48:     sCy -= static_cast<uint16_t>(179.0f * g);
	sub	a8, a2, a8	# tmp170, sCy, tmp168
# ../../src/display/ui/default/bganim/AnimNebula.cpp:45:     sBx -= static_cast<uint16_t>(145.0f * g);
	extui	a2, a14, 0, 16	# _18, tmp146
	s16i	a2, a9, 0	# sBx, _18
# ../../src/display/ui/default/bganim/AnimNebula.cpp:47:     sCx += static_cast<uint16_t>(222.0f * g);
	utrunc.s	a12, f1, 0	# tmp159, tmp156
# ../../src/display/ui/default/bganim/AnimNebula.cpp:46:     sBy += static_cast<uint16_t>(111.0f * g);
	add.n	a13, a13, a4	# tmp154, tmp151, sBy
	l32r	a9, .LC26	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:49:     const float turb = p[2] / 100.0f;
	l8ui	a10, a5, 2	# MEM[(const uint8_t *)p_70(D) + 2B], MEM[(const uint8_t *)p_70(D) + 2B]
# ../../src/display/ui/default/bganim/AnimNebula.cpp:46:     sBy += static_cast<uint16_t>(111.0f * g);
	extui	a4, a13, 0, 16	# _22, tmp154
# ../../src/display/ui/default/bganim/AnimNebula.cpp:47:     sCx += static_cast<uint16_t>(222.0f * g);
	add.n	a12, a12, a3	# tmp162, tmp159, sCx
# ../../src/display/ui/default/bganim/AnimNebula.cpp:46:     sBy += static_cast<uint16_t>(111.0f * g);
	s16i	a4, a9, 0	# sBy, _22
# ../../src/display/ui/default/bganim/AnimNebula.cpp:47:     sCx += static_cast<uint16_t>(222.0f * g);
	l32r	a9, .LC28	#,
	extui	a3, a12, 0, 16	# _26, tmp162
# ../../src/display/ui/default/bganim/AnimNebula.cpp:49:     const float turb = p[2] / 100.0f;
	float.s	f0, a10, 0	#, MEM[(const uint8_t *)p_70(D) + 2B]
	l32r	a11, .LC31	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:47:     sCx += static_cast<uint16_t>(222.0f * g);
	s16i	a3, a9, 0	# sCx, _26
# ../../src/display/ui/default/bganim/AnimNebula.cpp:48:     sCy -= static_cast<uint16_t>(179.0f * g);
	l32r	a9, .LC29	#,
	extui	a8, a8, 0, 16	# _30, tmp170
# ../../src/display/ui/default/bganim/AnimNebula.cpp:49:     const float turb = p[2] / 100.0f;
	rfr	a10, f0	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:48:     sCy -= static_cast<uint16_t>(179.0f * g);
	s16i	a8, a9, 0	# sCy, _30
# ../../src/display/ui/default/bganim/AnimNebula.cpp:49:     const float turb = p[2] / 100.0f;
	s32i.n	a8, sp, 0	#,
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimNebula.cpp:50:     g_wA = static_cast<int>((0.60f - 0.15f * turb) * 64.0f);
	l32r	a9, .LC34	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:49:     const float turb = p[2] / 100.0f;
	wfr	f0, a10	# tmp174,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:50:     g_wA = static_cast<int>((0.60f - 0.15f * turb) * 64.0f);
	wfr	f1, a9	# tmp176,
	l32r	a9, .LC33	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:53:     g_densOff = static_cast<int>((p[1] - 50) * 1.1f);
	l8ui	a5, a5, 1	# MEM[(const uint8_t *)p_70(D) + 1B], MEM[(const uint8_t *)p_70(D) + 1B]
# ../../src/display/ui/default/bganim/AnimNebula.cpp:50:     g_wA = static_cast<int>((0.60f - 0.15f * turb) * 64.0f);
	wfr	f2, a9	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:51:     g_wB = static_cast<int>((0.25f + 0.05f * turb) * 64.0f);
	l32r	a9, .LC38	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:50:     g_wA = static_cast<int>((0.60f - 0.15f * turb) * 64.0f);
	msub.s	f1, f0, f2	# tmp176, tmp174,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:51:     g_wB = static_cast<int>((0.25f + 0.05f * turb) * 64.0f);
	wfr	f2, a9	#,
	l32r	a9, .LC37	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:53:     g_densOff = static_cast<int>((p[1] - 50) * 1.1f);
	addi	a5, a5, -50	# tmp191, MEM[(const uint8_t *)p_70(D) + 1B],
# ../../src/display/ui/default/bganim/AnimNebula.cpp:51:     g_wB = static_cast<int>((0.25f + 0.05f * turb) * 64.0f);
	wfr	f3, a9	#,
	madd.s	f2, f0, f3	#, tmp174,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:50:     g_wA = static_cast<int>((0.60f - 0.15f * turb) * 64.0f);
	l32r	a9, .LC35	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:58:     g_bx = sBx >> 8;
	srai	a14, a2, 8	# tmp207, _18,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:53:     g_densOff = static_cast<int>((p[1] - 50) * 1.1f);
	l32r	a2, .LC39	#, tmp189
# ../../src/display/ui/default/bganim/AnimNebula.cpp:51:     g_wB = static_cast<int>((0.25f + 0.05f * turb) * 64.0f);
	mov.s	f0, f2	# tmp183,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:50:     g_wA = static_cast<int>((0.60f - 0.15f * turb) * 64.0f);
	wfr	f2, a9	# tmp180,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:51:     g_wB = static_cast<int>((0.25f + 0.05f * turb) * 64.0f);
	mul.s	f0, f0, f2	# tmp186, tmp183, tmp180
# ../../src/display/ui/default/bganim/AnimNebula.cpp:50:     g_wA = static_cast<int>((0.60f - 0.15f * turb) * 64.0f);
	mul.s	f1, f1, f2	# tmp179, tmp176, tmp180
# ../../src/display/ui/default/bganim/AnimNebula.cpp:61:     g_cy = sCy >> 8;
	l32i.n	a8, sp, 0	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:50:     g_wA = static_cast<int>((0.60f - 0.15f * turb) * 64.0f);
	l32r	a11, .LC32	#, tmp175
# ../../src/display/ui/default/bganim/AnimNebula.cpp:51:     g_wB = static_cast<int>((0.25f + 0.05f * turb) * 64.0f);
	trunc.s	a9, f0, 0	# tmp188, tmp186
# ../../src/display/ui/default/bganim/AnimNebula.cpp:53:     g_densOff = static_cast<int>((p[1] - 50) * 1.1f);
	float.s	f0, a5, 0	# tmp192, tmp191
	l32r	a5, .LC40	#,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:50:     g_wA = static_cast<int>((0.60f - 0.15f * turb) * 64.0f);
	trunc.s	a10, f1, 0	# tmp181, tmp179
# ../../src/display/ui/default/bganim/AnimNebula.cpp:53:     g_densOff = static_cast<int>((p[1] - 50) * 1.1f);
	wfr	f1, a5	#,
	mul.s	f0, f0, f1	# tmp193, tmp192,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:54:     g_axI = sAx >> 8;
	srai	a15, a7, 8	# tmp197, _10,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:50:     g_wA = static_cast<int>((0.60f - 0.15f * turb) * 64.0f);
	s32i.n	a10, a11, 0	# g_wA, tmp181
# ../../src/display/ui/default/bganim/AnimNebula.cpp:51:     g_wB = static_cast<int>((0.25f + 0.05f * turb) * 64.0f);
	l32r	a10, .LC36	#, tmp182
# ../../src/display/ui/default/bganim/AnimNebula.cpp:53:     g_densOff = static_cast<int>((p[1] - 50) * 1.1f);
	trunc.s	a5, f0, 0	# tmp195, tmp193
# ../../src/display/ui/default/bganim/AnimNebula.cpp:55:     g_axF = sAx & 0xFF;
	extui	a11, a7, 0, 8	# tmp200, _10
# ../../src/display/ui/default/bganim/AnimNebula.cpp:53:     g_densOff = static_cast<int>((p[1] - 50) * 1.1f);
	s32i.n	a5, a2, 0	# g_densOff, tmp195
# ../../src/display/ui/default/bganim/AnimNebula.cpp:54:     g_axI = sAx >> 8;
	l32r	a2, .LC41	#, tmp196
# ../../src/display/ui/default/bganim/AnimNebula.cpp:51:     g_wB = static_cast<int>((0.25f + 0.05f * turb) * 64.0f);
	s32i.n	a9, a10, 0	# g_wB, tmp188
# ../../src/display/ui/default/bganim/AnimNebula.cpp:54:     g_axI = sAx >> 8;
	s32i.n	a15, a2, 0	# g_axI, tmp197
# ../../src/display/ui/default/bganim/AnimNebula.cpp:55:     g_axF = sAx & 0xFF;
	l32r	a2, .LC42	#, tmp198
# ../../src/display/ui/default/bganim/AnimNebula.cpp:56:     g_ayI = sAy >> 8;
	srai	a10, a6, 8	# tmp202, _14,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:55:     g_axF = sAx & 0xFF;
	s32i.n	a11, a2, 0	# g_axF, tmp200
# ../../src/display/ui/default/bganim/AnimNebula.cpp:56:     g_ayI = sAy >> 8;
	l32r	a2, .LC43	#, tmp201
# ../../src/display/ui/default/bganim/AnimNebula.cpp:57:     g_ayF = sAy & 0xFF;
	extui	a9, a6, 0, 8	# tmp205, _14
# ../../src/display/ui/default/bganim/AnimNebula.cpp:56:     g_ayI = sAy >> 8;
	s32i.n	a10, a2, 0	# g_ayI, tmp202
# ../../src/display/ui/default/bganim/AnimNebula.cpp:57:     g_ayF = sAy & 0xFF;
	l32r	a2, .LC44	#, tmp203
# ../../src/display/ui/default/bganim/AnimNebula.cpp:60:     g_cx = sCx >> 8;
	srai	a12, a3, 8	# tmp211, _26,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:57:     g_ayF = sAy & 0xFF;
	s32i.n	a9, a2, 0	# g_ayF, tmp205
# ../../src/display/ui/default/bganim/AnimNebula.cpp:58:     g_bx = sBx >> 8;
	l32r	a3, .LC45	#, tmp206
# ../../src/display/ui/default/bganim/AnimNebula.cpp:59:     g_by = sBy >> 8;
	l32r	a2, .LC46	#, tmp208
# ../../src/display/ui/default/bganim/AnimNebula.cpp:59:     g_by = sBy >> 8;
	srai	a13, a4, 8	# tmp209, _22,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:58:     g_bx = sBx >> 8;
	s32i.n	a14, a3, 0	# g_bx, tmp207
# ../../src/display/ui/default/bganim/AnimNebula.cpp:59:     g_by = sBy >> 8;
	s32i.n	a13, a2, 0	# g_by, tmp209
# ../../src/display/ui/default/bganim/AnimNebula.cpp:61:     g_cy = sCy >> 8;
	l32r	a3, .LC48	#, tmp212
# ../../src/display/ui/default/bganim/AnimNebula.cpp:60:     g_cx = sCx >> 8;
	l32r	a2, .LC47	#, tmp210
# ../../src/display/ui/default/bganim/AnimNebula.cpp:61:     g_cy = sCy >> 8;
	srai	a8, a8, 8	# tmp213, _30,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:60:     g_cx = sCx >> 8;
	s32i.n	a12, a2, 0	# g_cx, tmp211
# ../../src/display/ui/default/bganim/AnimNebula.cpp:61:     g_cy = sCy >> 8;
	s32i.n	a8, a3, 0	# g_cy, tmp213
# ../../src/display/ui/default/bganim/AnimNebula.cpp:62: }
	retw.n
	.size	_ZN12_GLOBAL__N_15frameEjiiPKh, .-_ZN12_GLOBAL__N_15frameEjiiPKh
	.section	.text._ZN12_GLOBAL__N_14initEii,"ax",@progbits
	.literal_position
	.literal .LC49, _ZN12_GLOBAL__N_17paletteE
	.literal .LC50, _ZN12_GLOBAL__N_112lastThemeGenE
	.literal .LC51, _ZN12_GLOBAL__N_15noiseE
	.align	4
	.type	_ZN12_GLOBAL__N_14initEii, @function
_ZN12_GLOBAL__N_14initEii:
	entry	sp, 32	#
# ../../src/display/ui/default/bganim/AnimNebula.cpp:25:     if (palette == nullptr) {
	l32r	a2, .LC49	#, tmp52
# ../../src/display/ui/default/bganim/AnimNebula.cpp:25:     if (palette == nullptr) {
	l32i.n	a3, a2, 0	# palette, palette
	beqz.n	a3, .L17	# palette,
.L19:
# ../../src/display/ui/default/bganim/AnimNebula.cpp:32:     lastThemeGen = 0xFFFFFFFF;
	l32r	a2, .LC50	#, tmp54
	movi.n	a3, -1	# tmp55,
	s32i.n	a3, a2, 0	# lastThemeGen, tmp55
# ../../src/display/ui/default/bganim/AnimNebula.cpp:33:     return true;
	movi.n	a2, 1	# <retval>,
	j	.L18		#
.L17:
# ../../src/display/ui/default/bganim/AnimNebula.cpp:26:         palette = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
	movi	a10, 0x200	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimNebula.cpp:26:         palette = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
	s32i.n	a10, a2, 0	# palette,
# ../../src/display/ui/default/bganim/AnimNebula.cpp:27:         noise = noiseTex256();
	call8	_ZN6bganim11noiseTex256Ev		#
# ../../src/display/ui/default/bganim/AnimNebula.cpp:28:         if (palette == nullptr || noise == nullptr) {
	l32i.n	a2, a2, 0	# palette, palette
	movi.n	a8, 1	# tmp62,
	moveqz	a3, a8, a2	# tmp61, tmp62, palette
# ../../src/display/ui/default/bganim/AnimNebula.cpp:27:         noise = noiseTex256();
	l32r	a2, .LC51	#, tmp57
# ../../src/display/ui/default/bganim/AnimNebula.cpp:28:         if (palette == nullptr || noise == nullptr) {
	extui	a3, a3, 0, 8	# tmp64, tmp61
# ../../src/display/ui/default/bganim/AnimNebula.cpp:27:         noise = noiseTex256();
	s32i.n	a10, a2, 0	# noise, _3
# ../../src/display/ui/default/bganim/AnimNebula.cpp:28:         if (palette == nullptr || noise == nullptr) {
	bnez.n	a3, .L21	# tmp64,
	moveqz	a3, a8, a10	# tmp69, tmp62, _3
	beqz.n	a3, .L19	# tmp69,
.L21:
# ../../src/display/ui/default/bganim/AnimNebula.cpp:29:             return false;
	movi.n	a2, 0	# <retval>,
.L18:
# ../../src/display/ui/default/bganim/AnimNebula.cpp:34: }
	retw.n
	.size	_ZN12_GLOBAL__N_14initEii, .-_ZN12_GLOBAL__N_14initEii
	.global	bg_anim_nebula
	.section	.rodata.str1.4,"aMS",@progbits,1
	.align	4
.LC52:
	.string	"nebula"
	.align	4
.LC53:
	.string	"Nebula"
	.align	4
.LC54:
	.string	"speed"
	.align	4
.LC55:
	.string	"Drift speed"
	.align	4
.LC56:
	.string	"density"
	.align	4
.LC57:
	.string	"Density"
	.align	4
.LC58:
	.string	"turbulence"
	.align	4
.LC59:
	.string	"Turbulence"
	.section	.rodata
	.align	4
	.type	bg_anim_nebula, @object
	.size	bg_anim_nebula, 68
bg_anim_nebula:
# id:
	.word	.LC52
# name:
	.word	.LC53
# params:
# key:
	.word	.LC54
# label:
	.word	.LC55
# def:
	.byte	50
	.zero	3
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
	.byte	40
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
	.local	_ZZN12_GLOBAL__N_14bandEPtiiijPKhE8blendedA
	.comm	_ZZN12_GLOBAL__N_14bandEPtiiijPKhE8blendedA,256,4
	.local	_ZN12_GLOBAL__N_14g_cyE
	.comm	_ZN12_GLOBAL__N_14g_cyE,4,4
	.local	_ZN12_GLOBAL__N_14g_cxE
	.comm	_ZN12_GLOBAL__N_14g_cxE,4,4
	.local	_ZN12_GLOBAL__N_14g_byE
	.comm	_ZN12_GLOBAL__N_14g_byE,4,4
	.local	_ZN12_GLOBAL__N_14g_bxE
	.comm	_ZN12_GLOBAL__N_14g_bxE,4,4
	.local	_ZN12_GLOBAL__N_15g_ayFE
	.comm	_ZN12_GLOBAL__N_15g_ayFE,4,4
	.local	_ZN12_GLOBAL__N_15g_ayIE
	.comm	_ZN12_GLOBAL__N_15g_ayIE,4,4
	.local	_ZN12_GLOBAL__N_15g_axFE
	.comm	_ZN12_GLOBAL__N_15g_axFE,4,4
	.local	_ZN12_GLOBAL__N_15g_axIE
	.comm	_ZN12_GLOBAL__N_15g_axIE,4,4
	.local	_ZN12_GLOBAL__N_19g_densOffE
	.comm	_ZN12_GLOBAL__N_19g_densOffE,4,4
	.data
	.align	4
	.type	_ZN12_GLOBAL__N_14g_wBE, @object
	.size	_ZN12_GLOBAL__N_14g_wBE, 4
_ZN12_GLOBAL__N_14g_wBE:
	.word	20
	.align	4
	.type	_ZN12_GLOBAL__N_14g_wAE, @object
	.size	_ZN12_GLOBAL__N_14g_wAE, 4
_ZN12_GLOBAL__N_14g_wAE:
	.word	32
	.local	_ZN12_GLOBAL__N_13sCyE
	.comm	_ZN12_GLOBAL__N_13sCyE,2,2
	.local	_ZN12_GLOBAL__N_13sCxE
	.comm	_ZN12_GLOBAL__N_13sCxE,2,2
	.local	_ZN12_GLOBAL__N_13sByE
	.comm	_ZN12_GLOBAL__N_13sByE,2,2
	.local	_ZN12_GLOBAL__N_13sBxE
	.comm	_ZN12_GLOBAL__N_13sBxE,2,2
	.local	_ZN12_GLOBAL__N_13sAyE
	.comm	_ZN12_GLOBAL__N_13sAyE,2,2
	.local	_ZN12_GLOBAL__N_13sAxE
	.comm	_ZN12_GLOBAL__N_13sAxE,2,2
	.align	4
	.type	_ZN12_GLOBAL__N_112lastThemeGenE, @object
	.size	_ZN12_GLOBAL__N_112lastThemeGenE, 4
_ZN12_GLOBAL__N_112lastThemeGenE:
	.word	-1
	.local	_ZN12_GLOBAL__N_15noiseE
	.comm	_ZN12_GLOBAL__N_15noiseE,4,4
	.local	_ZN12_GLOBAL__N_17paletteE
	.comm	_ZN12_GLOBAL__N_17paletteE,4,4
	.ident	"GCC: (crosstool-NG esp-2021r2-patch5) 8.4.0"
