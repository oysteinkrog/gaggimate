	.file	"AnimRipples.cpp"
# GNU C++17 (crosstool-NG esp-2021r2-patch5) version 8.4.0 (xtensa-esp32s3-elf)
#	compiled by GNU C version 6.3.0 20170516, GMP version 6.2.1, MPFR version 4.1.0, MPC version 1.2.1, isl version isl-0.19-GMP

# GGC heuristics: --param ggc-min-expand=100 --param ggc-min-heapsize=131072
# options passed:  -I shim -imultilib no-rtti
# -iprefix c:\users\oystein\.platformio\packages\toolchain-xtensa-esp32s3\bin\../lib/gcc/xtensa-esp32s3-elf/8.4.0/
# ../../src/display/ui/default/bganim/AnimRipples.cpp -mlongcalls
# -auxbase-strip xtensa-asm/AnimRipples.S -O2 -std=gnu++17 -fno-exceptions
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
	.literal .LC0, 0x00000000
	.literal .LC1, _ZN12_GLOBAL__N_15g_tMsE
	.literal .LC2, 0x3c656042
	.literal .LC3, 0x3912ccf7
	.literal .LC4, 0x3fc90fda
	.literal .LC5, 0x4222f983
	.literal .LC6, _ZN12_GLOBAL__N_110g_cosTableE
	.literal .LC7, 0x3b08d181
	.literal .LC8, 0x41a00000
	.literal .LC9, 0x40200000
	.literal .LC10, 0x40400000
	.literal .LC11, _ZN12_GLOBAL__N_13g_nE
	.literal .LC12, _ZN12_GLOBAL__N_14g_cyE
	.literal .LC13, _ZN12_GLOBAL__N_13g_rE
	.literal .LC14, 0x41500000
	.literal .LC15, _ZN12_GLOBAL__N_14g_cxE
	.literal .LC16, _ZN12_GLOBAL__N_15g_icyE
	.literal .LC17, _ZN12_GLOBAL__N_15g_icxE
	.literal .LC18, _ZN12_GLOBAL__N_15g_ampE
	.literal .LC19, _ZN6bganim6BAYER4E
	.literal .LC20, 0x40f00000
	.literal .LC21, 0x3f0ccccd
	.literal .LC22, 0x437f0000
	.literal .LC23, _ZN12_GLOBAL__N_16envLUTE
	.literal .LC24, _ZN12_GLOBAL__N_16g_glowE
	.literal .LC25, _ZN12_GLOBAL__N_17troughFE
	.literal .LC26, _ZN12_GLOBAL__N_16crestFE
	.literal .LC27, 0x3e6e4bae
	.literal .LC28, 0x419cec4f
	.align	4
	.type	_ZN12_GLOBAL__N_14bandEPtiiijPKh, @function
_ZN12_GLOBAL__N_14bandEPtiiijPKh:
	entry	sp, 240	#
# ../../src/display/ui/default/bganim/AnimRipples.cpp:163: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
	s32i	a5, sp, 144	# %sfp, w
# ../../src/display/ui/default/bganim/AnimRipples.cpp:164:     for (int yy = 0; yy < rows; yy++) {
	blti	a4, 1, .L1	# rows,,
	s32i	a3, sp, 156	# %sfp, y0
	l32i	a6, sp, 156	# %sfp,
	slli	a3, a5, 1	#, w,
	add.n	a4, a4, a6	#, rows,
	s32i	a3, sp, 180	# %sfp,
	s32i	a2, sp, 172	# %sfp, dst
	s32i	a4, sp, 176	# %sfp,
.L19:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:167:         const float swell = sinRadLocal(g_cosTable, g_tMs * 0.00014f + y * 0.014f) * 2.5f;
	l32r	a11, .LC1	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:166:         const float vt = y * INV_ROWMAX;
	l32i	a8, sp, 156	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:167:         const float swell = sinRadLocal(g_cosTable, g_tMs * 0.00014f + y * 0.014f) * 2.5f;
	l32i.n	a2, a11, 0	# g_tMs, g_tMs
# ../../src/display/ui/default/bganim/AnimRipples.cpp:167:         const float swell = sinRadLocal(g_cosTable, g_tMs * 0.00014f + y * 0.014f) * 2.5f;
	l32r	a3, .LC2	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:166:         const float vt = y * INV_ROWMAX;
	float.s	f2, a8, 0	# _1,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:167:         const float swell = sinRadLocal(g_cosTable, g_tMs * 0.00014f + y * 0.014f) * 2.5f;
	wfr	f1, a3	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:167:         const float swell = sinRadLocal(g_cosTable, g_tMs * 0.00014f + y * 0.014f) * 2.5f;
	ufloat.s	f3, a2, 0	# tmp258, g_tMs
# ../../src/display/ui/default/bganim/AnimRipples.cpp:166:         const float vt = y * INV_ROWMAX;
	l32r	a2, .LC7	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:167:         const float swell = sinRadLocal(g_cosTable, g_tMs * 0.00014f + y * 0.014f) * 2.5f;
	mul.s	f0, f2, f1	# tmp260, _1,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:166:         const float vt = y * INV_ROWMAX;
	wfr	f4, a2	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:167:         const float swell = sinRadLocal(g_cosTable, g_tMs * 0.00014f + y * 0.014f) * 2.5f;
	l32r	a2, .LC3	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:166:         const float vt = y * INV_ROWMAX;
	mul.s	f1, f2, f4	# vt, _1,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:54: inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
	l32r	a3, .LC6	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:167:         const float swell = sinRadLocal(g_cosTable, g_tMs * 0.00014f + y * 0.014f) * 2.5f;
	wfr	f4, a2	#,
	madd.s	f0, f3, f4	# tmp262, tmp258,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:168:         int basePos = static_cast<int>(vt * 20.0f + swell + 3.0f);
	l32r	a2, .LC8	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:54: inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
	l32i.n	a4, a3, 0	# g_cosTable, g_cosTable
# ../../src/display/ui/default/bganim/AnimRipples.cpp:55: inline float sinRadLocal(const float *ct, float rad) { return cosRadLocal(ct, rad - 1.5707963f); }
	l32r	a3, .LC4	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:168:         int basePos = static_cast<int>(vt * 20.0f + swell + 3.0f);
	wfr	f3, a2	#,
	mul.s	f1, f1, f3	# tmp276, vt,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:55: inline float sinRadLocal(const float *ct, float rad) { return cosRadLocal(ct, rad - 1.5707963f); }
	wfr	f3, a3	#,
	sub.s	f0, f0, f3	# tmp264, tmp262,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:54: inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
	l32r	a3, .LC5	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:175:         themeRGB(basePos, baseC);
	movi.n	a2, 0	# tmp287,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:54: inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
	wfr	f3, a3	#,
	mul.s	f0, f0, f3	# tmp266, tmp264,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:175:         themeRGB(basePos, baseC);
	movi	a11, 0x80	#,
	add.n	a11, a11, sp	#,,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:54: inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
	trunc.s	a3, f0, 0	# tmp268, tmp266
# ../../src/display/ui/default/bganim/AnimRipples.cpp:54: inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
	extui	a3, a3, 0, 8	# tmp269, tmp268,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:54: inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
	slli	a3, a3, 2	# tmp272, tmp269,
	add.n	a3, a4, a3	# tmp273, g_cosTable, tmp272
# ../../src/display/ui/default/bganim/AnimRipples.cpp:168:         int basePos = static_cast<int>(vt * 20.0f + swell + 3.0f);
	lsi	f0, a3, 0	# *_193, *_193
	l32r	a3, .LC9	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:175:         themeRGB(basePos, baseC);
	ssi	f2, sp, 204	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:168:         int basePos = static_cast<int>(vt * 20.0f + swell + 3.0f);
	wfr	f3, a3	#,
	madd.s	f1, f0, f3	# tmp276, *_193,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:168:         int basePos = static_cast<int>(vt * 20.0f + swell + 3.0f);
	l32r	a3, .LC10	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:168:         int basePos = static_cast<int>(vt * 20.0f + swell + 3.0f);
	mov.s	f0, f1	# tmp278, tmp276
# ../../src/display/ui/default/bganim/AnimRipples.cpp:168:         int basePos = static_cast<int>(vt * 20.0f + swell + 3.0f);
	wfr	f1, a3	#,
	add.s	f0, f0, f1	# tmp281, tmp278,
	movi.n	a3, 0x1f	# tmp283,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:168:         int basePos = static_cast<int>(vt * 20.0f + swell + 3.0f);
	trunc.s	a10, f0, 0	# _35, tmp281
	min	a10, a10, a3	# _35, _35, tmp283
# ../../src/display/ui/default/bganim/AnimRipples.cpp:175:         themeRGB(basePos, baseC);
	max	a10, a10, a2	#, _35, tmp287
	call8	_ZN6bganim8themeRGBEiPh		#
# ../../src/display/ui/default/bganim/AnimRipples.cpp:189:         for (int i = 0; i < g_n; i++) {
	l32r	a6, .LC11	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:176:         const float baseR = baseC[0], baseG = baseC[1], baseB = baseC[2];
	l8ui	a5, sp, 128	# baseC, baseC
# ../../src/display/ui/default/bganim/AnimRipples.cpp:176:         const float baseR = baseC[0], baseG = baseC[1], baseB = baseC[2];
	l8ui	a4, sp, 129	# baseC, baseC
# ../../src/display/ui/default/bganim/AnimRipples.cpp:176:         const float baseR = baseC[0], baseG = baseC[1], baseB = baseC[2];
	l8ui	a3, sp, 130	# baseC, baseC
# ../../src/display/ui/default/bganim/AnimRipples.cpp:189:         for (int i = 0; i < g_n; i++) {
	l32i.n	a8, a6, 0	# g_n, g_n$2_270
# ../../src/display/ui/default/bganim/AnimRipples.cpp:176:         const float baseR = baseC[0], baseG = baseC[1], baseB = baseC[2];
	ufloat.s	f6, a5, 0	# baseR, baseC
# ../../src/display/ui/default/bganim/AnimRipples.cpp:176:         const float baseR = baseC[0], baseG = baseC[1], baseB = baseC[2];
	ufloat.s	f7, a4, 0	# baseG, baseC
# ../../src/display/ui/default/bganim/AnimRipples.cpp:176:         const float baseR = baseC[0], baseG = baseC[1], baseB = baseC[2];
	ufloat.s	f8, a3, 0	# baseB, baseC
# ../../src/display/ui/default/bganim/AnimRipples.cpp:189:         for (int i = 0; i < g_n; i++) {
	lsi	f2, sp, 204	#,
	bgei	a8, 1, .L3	# g_n$2_270,,
.L16:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:222:             for (int x = 0; x < w; x++) {
	l32i	a8, sp, 144	# %sfp,
	bgei	a8, 1, .L73	#,,
	j	.L4		#
.L3:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:200:             const float wMinus1 = static_cast<float>(w - 1);
	l32i	a6, sp, 144	# %sfp,
	l32r	a11, .LC14	#,
	slli	a3, a8, 2	# _374, g_n$2_270,
	addi.n	a6, a6, -1	#,,
	l32r	a5, .LC12	#, tmp469
	l32r	a4, .LC13	#, tmp467
	wfr	f5, a11	# tmp474,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:188:         int nrr = 0;
	s32i	a2, sp, 148	# %sfp, ivtmp$76
# ../../src/display/ui/default/bganim/AnimRipples.cpp:200:             const float wMinus1 = static_cast<float>(w - 1);
	s32i	a6, sp, 152	# %sfp,
	ssi	f6, sp, 160	# %sfp, baseR
	ssi	f7, sp, 164	# %sfp, baseG
	ssi	f8, sp, 168	# %sfp, baseB
	mov.n	a7, a3	# _374, _374
.L15:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:190:             const float dy = y - g_cy[i];
	add.n	a6, a5, a2	# tmp292, tmp469, ivtmp$76
# ../../src/display/ui/default/bganim/AnimRipples.cpp:190:             const float dy = y - g_cy[i];
	lsi	f0, a6, 0	# MEM[base: _377, offset: 0B], MEM[base: _377, offset: 0B]
# ../../src/display/ui/default/bganim/AnimRipples.cpp:191:             const float outer = g_r[i] + HALFW;
	add.n	a6, a4, a2	# tmp295, tmp467, ivtmp$76
	lsi	f3, a6, 0	# MEM[base: _376, offset: 0B], _18
# ../../src/display/ui/default/bganim/AnimRipples.cpp:190:             const float dy = y - g_cy[i];
	sub.s	f0, f2, f0	# dy, _1, MEM[base: _377, offset: 0B]
# ../../src/display/ui/default/bganim/AnimRipples.cpp:191:             const float outer = g_r[i] + HALFW;
	add.s	f1, f3, f5	# outer, _18, tmp474
# ../../src/display/ui/default/bganim/AnimRipples.cpp:192:             if (fabsf(dy) > outer) {
	abs.s	f4, f0	# tmp298, dy
# ../../src/display/ui/default/bganim/AnimRipples.cpp:192:             if (fabsf(dy) > outer) {
	olt.s	b0, f1, f4	#, outer, tmp298
	bt	b0, .L6	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:195:             const float half = sqrtf(outer * outer - dy * dy);
	mul.s	f1, f1, f1	# tmp299, outer, outer
# ../../src/display/ui/default/bganim/AnimRipples.cpp:195:             const float half = sqrtf(outer * outer - dy * dy);
	ssi	f2, sp, 204	#,
	ssi	f3, sp, 196	#,
	ssi	f5, sp, 200	#,
	msub.s	f1, f0, f0	# tmp299, dy, dy
	rfr	a10, f1	#, tmp299
	call8	sqrtf		#
# ../../src/display/ui/default/bganim/AnimRipples.cpp:198:             const float leftF = g_cx[i] - half;
	l32r	a8, .LC15	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:195:             const float half = sqrtf(outer * outer - dy * dy);
	wfr	f1, a10	# half,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:198:             const float leftF = g_cx[i] - half;
	add.n	a3, a2, a8	# tmp301, ivtmp$76,
	lsi	f0, a3, 0	# MEM[base: _372, offset: 0B], _23
# ../../src/display/ui/default/bganim/AnimRipples.cpp:201:             const int x0 = static_cast<int>(leftF > 0.0f ? leftF : 0.0f);
	l32r	a3, .LC0	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:198:             const float leftF = g_cx[i] - half;
	sub.s	f4, f0, f1	# leftF, _23, half
# ../../src/display/ui/default/bganim/AnimRipples.cpp:200:             const float wMinus1 = static_cast<float>(w - 1);
	l32i	a11, sp, 152	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:199:             const float rightF = g_cx[i] + half;
	add.s	f0, f0, f1	# rightF, _23, half
# ../../src/display/ui/default/bganim/AnimRipples.cpp:201:             const int x0 = static_cast<int>(leftF > 0.0f ? leftF : 0.0f);
	wfr	f1, a3	#,
	olt.s	b0, f1, f4	#,, leftF
# ../../src/display/ui/default/bganim/AnimRipples.cpp:200:             const float wMinus1 = static_cast<float>(w - 1);
	float.s	f6, a11, 0	# wMinus1,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:201:             const int x0 = static_cast<int>(leftF > 0.0f ? leftF : 0.0f);
	lsi	f2, sp, 204	#,
	lsi	f3, sp, 196	#,
	lsi	f5, sp, 200	#,
	bf	b0, .L74	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:201:             const int x0 = static_cast<int>(leftF > 0.0f ? leftF : 0.0f);
	trunc.s	a9, f4, 0	# iftmp$3_138, leftF
	j	.L7		#
.L74:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:201:             const int x0 = static_cast<int>(leftF > 0.0f ? leftF : 0.0f);
	movi.n	a9, 0	# iftmp$3_138,
.L7:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:202:             const int x1 = static_cast<int>(rightF < wMinus1 ? rightF : wMinus1);
	olt.s	b0, f0, f6	#, rightF, wMinus1
	bf	b0, .L75	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:202:             const int x1 = static_cast<int>(rightF < wMinus1 ? rightF : wMinus1);
	trunc.s	a12, f0, 0	# iftmp$4_139, rightF
	j	.L11		#
.L75:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:202:             const int x1 = static_cast<int>(rightF < wMinus1 ? rightF : wMinus1);
	trunc.s	a12, f6, 0	# iftmp$4_139, wMinus1
.L11:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:203:             if (x1 >= x0) {
	blt	a12, a9, .L6	# iftmp$4_139, iftmp$3_138,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:207:                 const int idx0 = x0 - g_icx[i];
	l32r	a6, .LC17	#, tmp309
# ../../src/display/ui/default/bganim/AnimRipples.cpp:206:                 const int idy = y - g_icy[i];
	l32r	a8, .LC16	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:207:                 const int idx0 = x0 - g_icx[i];
	add.n	a6, a2, a6	# tmp308, ivtmp$76, tmp309
# ../../src/display/ui/default/bganim/AnimRipples.cpp:206:                 const int idy = y - g_icy[i];
	add.n	a3, a2, a8	# tmp305, ivtmp$76,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:207:                 const int idx0 = x0 - g_icx[i];
	l32i.n	a13, a6, 0	# MEM[base: _370, offset: 0B], MEM[base: _370, offset: 0B]
# ../../src/display/ui/default/bganim/AnimRipples.cpp:206:                 const int idy = y - g_icy[i];
	l32i.n	a3, a3, 0	# MEM[base: _371, offset: 0B], MEM[base: _371, offset: 0B]
	l32i	a6, sp, 156	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:207:                 const int idx0 = x0 - g_icx[i];
	sub	a13, a9, a13	# idx0, iftmp$3_138, MEM[base: _370, offset: 0B]
# ../../src/display/ui/default/bganim/AnimRipples.cpp:206:                 const int idy = y - g_icy[i];
	sub	a3, a6, a3	# idy,, MEM[base: _371, offset: 0B]
# ../../src/display/ui/default/bganim/AnimRipples.cpp:208:                 const int dist2 = idx0 * idx0 + idy * idy;
	mull	a3, a3, a3	# tmp312, idy, idy
# ../../src/display/ui/default/bganim/AnimRipples.cpp:208:                 const int dist2 = idx0 * idx0 + idy * idy;
	mull	a6, a13, a13	# tmp311, idx0, idx0
# ../../src/display/ui/default/bganim/AnimRipples.cpp:209:                 int r0 = static_cast<int>(sqrtf(static_cast<float>(dist2)));
	s32i	a9, sp, 184	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:208:                 const int dist2 = idx0 * idx0 + idy * idy;
	add.n	a3, a6, a3	# dist2, tmp311, tmp312
# ../../src/display/ui/default/bganim/AnimRipples.cpp:209:                 int r0 = static_cast<int>(sqrtf(static_cast<float>(dist2)));
	float.s	f0, a3, 0	#, dist2
	s32i	a12, sp, 188	#,
	rfr	a10, f0	#,
	s32i	a13, sp, 192	#,
	ssi	f2, sp, 204	#,
	ssi	f3, sp, 196	#,
	ssi	f5, sp, 200	#,
	call8	sqrtf		#
# ../../src/display/ui/default/bganim/AnimRipples.cpp:209:                 int r0 = static_cast<int>(sqrtf(static_cast<float>(dist2)));
	wfr	f1, a10	#,
	trunc.s	a10, f1, 0	# r0,
	lsi	f2, sp, 204	#,
	lsi	f3, sp, 196	#,
	l32i	a9, sp, 184	#,
	l32i	a12, sp, 188	#,
	l32i	a13, sp, 192	#,
	lsi	f5, sp, 200	#,
	j	.L12		#
.L49:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:211:                     r0++;
	mov.n	a10, a6	# r0, _31
.L12:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:210:                 while ((r0 + 1) * (r0 + 1) <= dist2) {
	addi.n	a6, a10, 1	# _31, r0,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:210:                 while ((r0 + 1) * (r0 + 1) <= dist2) {
	mull	a8, a6, a6	# _33, _31, _31
# ../../src/display/ui/default/bganim/AnimRipples.cpp:210:                 while ((r0 + 1) * (r0 + 1) <= dist2) {
	bge	a3, a8, .L49	# dist2, _33,
	j	.L84		#
.L14:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:214:                     r0--;
	addi.n	a10, a10, -1	# r0, r0,
.L84:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:213:                 while (r0 * r0 > dist2) {
	mull	a6, a10, a10	# _34, r0, r0
# ../../src/display/ui/default/bganim/AnimRipples.cpp:213:                 while (r0 * r0 > dist2) {
	blt	a3, a6, .L14	# dist2, _34,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:216:                 rr[nrr++] = {g_r[i], g_amp[i], x0, x1, idx0, dist2, r0, r0 * r0};
	l32r	a14, .LC18	#, tmp315
# ../../src/display/ui/default/bganim/AnimRipples.cpp:216:                 rr[nrr++] = {g_r[i], g_amp[i], x0, x1, idx0, dist2, r0, r0 * r0};
	l32i	a11, sp, 148	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:216:                 rr[nrr++] = {g_r[i], g_amp[i], x0, x1, idx0, dist2, r0, r0 * r0};
	add.n	a14, a2, a14	# tmp314, ivtmp$76, tmp315
# ../../src/display/ui/default/bganim/AnimRipples.cpp:216:                 rr[nrr++] = {g_r[i], g_amp[i], x0, x1, idx0, dist2, r0, r0 * r0};
	slli	a8, a11, 5	# tmp316,,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:216:                 rr[nrr++] = {g_r[i], g_amp[i], x0, x1, idx0, dist2, r0, r0 * r0};
	lsi	f0, a14, 0	# MEM[base: _373, offset: 0B], _36
# ../../src/display/ui/default/bganim/AnimRipples.cpp:216:                 rr[nrr++] = {g_r[i], g_amp[i], x0, x1, idx0, dist2, r0, r0 * r0};
	add.n	a8, sp, a8	# tmp317,, tmp316
# ../../src/display/ui/default/bganim/AnimRipples.cpp:216:                 rr[nrr++] = {g_r[i], g_amp[i], x0, x1, idx0, dist2, r0, r0 * r0};
	addi.n	a11, a11, 1	#,,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:216:                 rr[nrr++] = {g_r[i], g_amp[i], x0, x1, idx0, dist2, r0, r0 * r0};
	ssi	f3, a8, 0	# rr[nrr_282].r, _18
	ssi	f0, a8, 4	# rr[nrr_282].amp, _36
	s32i.n	a9, a8, 8	# rr[nrr_282].x0, iftmp$3_138
	s32i.n	a12, a8, 12	# rr[nrr_282].x1, iftmp$4_139
	s32i.n	a13, a8, 16	# rr[nrr_282].curDx, idx0
	s32i.n	a3, a8, 20	# rr[nrr_282].curDist2, dist2
	s32i.n	a10, a8, 24	# rr[nrr_282].curR, r0
	s32i.n	a6, a8, 28	# rr[nrr_282].curR2, _34
# ../../src/display/ui/default/bganim/AnimRipples.cpp:216:                 rr[nrr++] = {g_r[i], g_amp[i], x0, x1, idx0, dist2, r0, r0 * r0};
	s32i	a11, sp, 148	# %sfp,
.L6:
	addi.n	a2, a2, 4	# ivtmp$76, ivtmp$76,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:189:         for (int i = 0; i < g_n; i++) {
	bne	a7, a2, .L15	# _374, ivtmp$76,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:221:         if (nrr == 0) {
	l32i	a2, sp, 148	# %sfp,
	lsi	f6, sp, 160	# %sfp, baseR
	lsi	f7, sp, 164	# %sfp, baseG
	lsi	f8, sp, 168	# %sfp, baseB
	bnez.n	a2, .L76	#,
	j	.L16		#
.L4:
	l32i	a3, sp, 156	# %sfp,
	l32i	a4, sp, 172	# %sfp,
	l32i	a6, sp, 180	# %sfp,
	addi.n	a3, a3, 1	#,,
	add.n	a4, a4, a6	#,,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:164:     for (int yy = 0; yy < rows; yy++) {
	l32i	a8, sp, 176	# %sfp,
	s32i	a3, sp, 156	# %sfp,
	s32i	a4, sp, 172	# %sfp,
	bne	a8, a3, .L19	#,,
	j	.L1		#
.L73:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:223:                 const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	l32i	a2, sp, 156	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	l32r	a11, .LC22	#,
	l32r	a6, .LC20	#,
	l32r	a13, .LC19	#, tmp472
	l32r	a8, .LC21	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:223:                 const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	extui	a7, a2, 0, 2	# tmp339,,
	l32r	a3, .LC0	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	wfr	f3, a11	# tmp522,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:223:                 const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	slli	a7, a7, 2	# _42, tmp339,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:222:             for (int x = 0; x < w; x++) {
	movi.n	a4, 0	# x,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	movi	a9, -0x800	# prephitmp_508,
	wfr	f10, a6	# tmp470,
	l32i	a5, sp, 144	# %sfp, w
# ../../src/display/ui/default/bganim/AnimRipples.cpp:223:                 const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	l32i	a6, sp, 172	# %sfp, ivtmp$39
	wfr	f9, a8	# tmp471,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:223:                 const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	add.n	a7, a13, a7	# tmp342, tmp472, _42
	wfr	f2, a3	# tmp475,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.n	a8, a4	# prephitmp_508, x
	mov.n	a11, a9	# tmp524, prephitmp_508
	mov.s	f5, f3	# tmp525, tmp522
	movi	a10, 0x7e0	# tmp526,
	mov.s	f4, f3	# tmp527, tmp522
	movi.n	a13, 0x1f	# tmp528,
.L27:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:223:                 const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	extui	a2, a4, 0, 2	# tmp341, x,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:223:                 const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	add.n	a2, a7, a2	# tmp343, tmp342, tmp341
	l8ui	a2, a2, 0	# BAYER4, tmp344
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.n	a3, a8	# prephitmp_508, prephitmp_508
# ../../src/display/ui/default/bganim/AnimRipples.cpp:223:                 const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	float.s	f0, a2, 0	# tmp345, tmp344
	sub.s	f0, f0, f10	# tmp346, tmp345, tmp470
# ../../src/display/ui/default/bganim/AnimRipples.cpp:223:                 const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	mul.s	f0, f0, f9	# dith, tmp346, tmp471
# ../../src/display/ui/default/bganim/AnimRipples.cpp:224:                 row[x] = rgb565(clamp8f(baseR + dith), clamp8f(baseG + dith), clamp8f(baseB + dith));
	add.s	f1, f6, f0	# _49, baseR, dith
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f1, f2	#, _49, tmp475
	bt	b0, .L20	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f3, f1	#, tmp522, _49
	mov.n	a3, a9	# prephitmp_508, prephitmp_508
	bt	b0, .L20	#,
	utrunc.s	a3, f1, 0	# tmp352, _49
	extui	a3, a3, 0, 8	# tmp353, tmp352
	slli	a3, a3, 8	# tmp354, tmp353,
	and	a3, a3, a11	# tmp357, tmp354, tmp524
	sext	a3, a3, 15	# prephitmp_508, tmp357
.L20:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:224:                 row[x] = rgb565(clamp8f(baseR + dith), clamp8f(baseG + dith), clamp8f(baseB + dith));
	add.s	f1, f7, f0	# _51, baseG, dith
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f1, f2	#, _51, tmp475
	bt	b0, .L21	#,
	olt.s	b0, f5, f1	#, tmp525, _51
	bf	b0, .L77	#,
	or	a3, a3, a10	# tmp361, prephitmp_508, tmp526
	sext	a3, a3, 15	# prephitmp_508, tmp361
	j	.L21		#
.L77:
	utrunc.s	a2, f1, 0	# tmp363, _51
	extui	a2, a2, 0, 8	# tmp364, tmp363
	slli	a2, a2, 3	# tmp365, tmp364,
	and	a2, a2, a10	# tmp368, tmp365, tmp526
	or	a3, a3, a2	# prephitmp_508, prephitmp_508, tmp368
.L21:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:224:                 row[x] = rgb565(clamp8f(baseR + dith), clamp8f(baseG + dith), clamp8f(baseB + dith));
	add.s	f0, f8, f0	# _53, baseB, dith
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f0, f2	#, _53, tmp475
	bt	b0, .L24	#,
	olt.s	b0, f4, f0	#, tmp527, _53
	bf	b0, .L78	#,
	or	a3, a3, a13	# tmp374, prephitmp_508, tmp528
	sext	a3, a3, 15	# prephitmp_508, tmp374
	j	.L24		#
.L78:
	utrunc.s	a2, f0, 0	# tmp376, _53
	extui	a2, a2, 3, 5	# tmp378, tmp376,,
	or	a3, a3, a2	# prephitmp_508, prephitmp_508, tmp378
.L24:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:224:                 row[x] = rgb565(clamp8f(baseR + dith), clamp8f(baseG + dith), clamp8f(baseB + dith));
	s16i	a3, a6, 0	# MEM[base: _299, offset: 0B], prephitmp_508
# ../../src/display/ui/default/bganim/AnimRipples.cpp:222:             for (int x = 0; x < w; x++) {
	addi.n	a4, a4, 1	# x, x,
	addi.n	a6, a6, 2	# ivtmp$39, ivtmp$39,
	addi.n	a5, a5, -1	# w, w,
	bnez.n	a5, .L27	# w,
	j	.L4		#
.L76:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:228:         for (int x = 0; x < w; x++) {
	l32i	a2, sp, 144	# %sfp,
	blti	a2, 1, .L4	#,,
	l32i	a6, sp, 148	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:270:             const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	l32i	a3, sp, 156	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:241:                     hAcc += rr[i].amp * cosRadLocal(g_cosTable, delta * WAVEFREQ) * env;
	l32r	a11, .LC6	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:270:             const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	extui	a2, a3, 0, 2	# tmp390,,
	slli	a3, a6, 5	# tmp391,,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:240:                     const float env = envLUT[static_cast<int>(ad * ENV_SCALE)];
	l32r	a6, .LC23	#, tmp381
# ../../src/display/ui/default/bganim/AnimRipples.cpp:241:                     hAcc += rr[i].amp * cosRadLocal(g_cosTable, delta * WAVEFREQ) * env;
	l32i.n	a11, a11, 0	# g_cosTable,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:240:                     const float env = envLUT[static_cast<int>(ad * ENV_SCALE)];
	l32i.n	a6, a6, 0	# envLUT,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:261:                     cr += g * crestF[0];
	l32r	a4, .LC26	#, tmp387
# ../../src/display/ui/default/bganim/AnimRipples.cpp:265:                     cr += g * troughF[0];
	l32r	a5, .LC25	#, tmp384
# ../../src/display/ui/default/bganim/AnimRipples.cpp:240:                     const float env = envLUT[static_cast<int>(ad * ENV_SCALE)];
	s32i	a6, sp, 148	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:259:                 const float g = hAcc * g_glow;
	l32r	a6, .LC24	#, tmp383
# ../../src/display/ui/default/bganim/AnimRipples.cpp:241:                     hAcc += rr[i].amp * cosRadLocal(g_cosTable, delta * WAVEFREQ) * env;
	s32i	a11, sp, 152	# %sfp,
	l32r	a11, .LC21	#,
	l32r	a13, .LC19	#, tmp472
# ../../src/display/ui/default/bganim/AnimRipples.cpp:259:                 const float g = hAcc * g_glow;
	lsi	f12, a6, 0	# g_glow, g_glow$12_92
# ../../src/display/ui/default/bganim/AnimRipples.cpp:265:                     cr += g * troughF[0];
	lsi	f0, a5, 0	# troughF,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:266:                     cg += g * troughF[1];
	lsi	f1, a5, 4	# troughF,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:267:                     cb += g * troughF[2];
	lsi	f3, a5, 8	# troughF,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:261:                     cr += g * crestF[0];
	lsi	f15, a4, 0	# crestF, _93
# ../../src/display/ui/default/bganim/AnimRipples.cpp:262:                     cg += g * crestF[1];
	lsi	f14, a4, 4	# crestF, _95
# ../../src/display/ui/default/bganim/AnimRipples.cpp:263:                     cb += g * crestF[2];
	lsi	f13, a4, 8	# crestF, _97
	l32r	a6, .LC20	#,
	l32r	a4, .LC0	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:270:             const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	slli	a2, a2, 2	# _106, tmp390,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:228:         for (int x = 0; x < w; x++) {
	movi.n	a8, 0	# x,
	wfr	f9, a11	# tmp471,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:270:             const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	l32i	a11, sp, 172	# %sfp, ivtmp$60
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	movi	a12, -0x800	# prephitmp_452,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:265:                     cr += g * troughF[0];
	ssi	f0, sp, 160	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:266:                     cg += g * troughF[1];
	ssi	f1, sp, 164	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:267:                     cb += g * troughF[2];
	ssi	f3, sp, 168	# %sfp,
	wfr	f2, a4	# tmp475,
	wfr	f10, a6	# tmp470,
	mov.n	a15, sp	# _387,
	add.n	a10, sp, a3	# _384,, tmp391
# ../../src/display/ui/default/bganim/AnimRipples.cpp:270:             const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	add.n	a13, a13, a2	# tmp533, tmp472, _106
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.n	a14, a8	# prephitmp_452, x
.L47:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:228:         for (int x = 0; x < w; x++) {
	mov.n	a5, a15	# ivtmp$57, _387
# ../../src/display/ui/default/bganim/AnimRipples.cpp:230:             float hAcc = 0;
	mov.s	f3, f2	# hAcc, tmp475
.L36:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:232:                 if (x < rr[i].x0 || x > rr[i].x1) {
	l32i.n	a2, a5, 8	# MEM[base: _401, offset: 8B], MEM[base: _401, offset: 8B]
	blt	a8, a2, .L28	# x, MEM[base: _401, offset: 8B],
# ../../src/display/ui/default/bganim/AnimRipples.cpp:232:                 if (x < rr[i].x0 || x > rr[i].x1) {
	l32i.n	a2, a5, 12	# MEM[base: _401, offset: 12B], MEM[base: _401, offset: 12B]
	blt	a2, a8, .L28	# MEM[base: _401, offset: 12B], x,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:237:                 const float delta = static_cast<float>(rr[i].curR) - rr[i].r;
	l32i.n	a4, a5, 24	# MEM[base: _401, offset: 24B], _60
# ../../src/display/ui/default/bganim/AnimRipples.cpp:237:                 const float delta = static_cast<float>(rr[i].curR) - rr[i].r;
	lsi	f1, a5, 0	# MEM[base: _401, offset: 0B], MEM[base: _401, offset: 0B]
# ../../src/display/ui/default/bganim/AnimRipples.cpp:237:                 const float delta = static_cast<float>(rr[i].curR) - rr[i].r;
	float.s	f0, a4, 0	# tmp394, _60
# ../../src/display/ui/default/bganim/AnimRipples.cpp:237:                 const float delta = static_cast<float>(rr[i].curR) - rr[i].r;
	sub.s	f0, f0, f1	# delta, tmp394, MEM[base: _401, offset: 0B]
# ../../src/display/ui/default/bganim/AnimRipples.cpp:238:                 const float ad = fabsf(delta);
	abs.s	f1, f0	# ad, delta
# ../../src/display/ui/default/bganim/AnimRipples.cpp:239:                 if (ad <= HALFW) {
	ole.s	b0, f1, f5	#, ad, tmp474
	bf	b0, .L29	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:241:                     hAcc += rr[i].amp * cosRadLocal(g_cosTable, delta * WAVEFREQ) * env;
	l32r	a2, .LC27	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:240:                     const float env = envLUT[static_cast<int>(ad * ENV_SCALE)];
	l32i	a6, sp, 148	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:241:                     hAcc += rr[i].amp * cosRadLocal(g_cosTable, delta * WAVEFREQ) * env;
	wfr	f4, a2	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:240:                     const float env = envLUT[static_cast<int>(ad * ENV_SCALE)];
	l32r	a2, .LC28	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:241:                     hAcc += rr[i].amp * cosRadLocal(g_cosTable, delta * WAVEFREQ) * env;
	mul.s	f0, f0, f4	# tmp397, delta,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:240:                     const float env = envLUT[static_cast<int>(ad * ENV_SCALE)];
	wfr	f4, a2	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:54: inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
	l32r	a2, .LC5	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:240:                     const float env = envLUT[static_cast<int>(ad * ENV_SCALE)];
	mul.s	f1, f1, f4	# tmp408, ad,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:54: inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
	wfr	f4, a2	#,
	mul.s	f0, f0, f4	# tmp399, tmp397,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:241:                     hAcc += rr[i].amp * cosRadLocal(g_cosTable, delta * WAVEFREQ) * env;
	lsi	f11, a5, 4	# MEM[base: _401, offset: 4B], MEM[base: _401, offset: 4B]
# ../../src/display/ui/default/bganim/AnimRipples.cpp:240:                     const float env = envLUT[static_cast<int>(ad * ENV_SCALE)];
	trunc.s	a3, f1, 0	# tmp410, tmp408
# ../../src/display/ui/default/bganim/AnimRipples.cpp:54: inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
	trunc.s	a2, f0, 0	# tmp401, tmp399
# ../../src/display/ui/default/bganim/AnimRipples.cpp:240:                     const float env = envLUT[static_cast<int>(ad * ENV_SCALE)];
	slli	a3, a3, 2	# tmp411, tmp410,
	add.n	a3, a6, a3	# tmp412,, tmp411
# ../../src/display/ui/default/bganim/AnimRipples.cpp:54: inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
	extui	a2, a2, 0, 8	# tmp402, tmp401,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:54: inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
	l32i	a6, sp, 152	# %sfp,
	slli	a2, a2, 2	# tmp403, tmp402,
	add.n	a2, a6, a2	# tmp404,, tmp403
# ../../src/display/ui/default/bganim/AnimRipples.cpp:241:                     hAcc += rr[i].amp * cosRadLocal(g_cosTable, delta * WAVEFREQ) * env;
	lsi	f1, a2, 0	# *_251, *_251
# ../../src/display/ui/default/bganim/AnimRipples.cpp:241:                     hAcc += rr[i].amp * cosRadLocal(g_cosTable, delta * WAVEFREQ) * env;
	lsi	f0, a3, 0	# *_68, *_68
# ../../src/display/ui/default/bganim/AnimRipples.cpp:241:                     hAcc += rr[i].amp * cosRadLocal(g_cosTable, delta * WAVEFREQ) * env;
	mul.s	f4, f1, f11	# tmp405, *_251, MEM[base: _401, offset: 4B]
# ../../src/display/ui/default/bganim/AnimRipples.cpp:241:                     hAcc += rr[i].amp * cosRadLocal(g_cosTable, delta * WAVEFREQ) * env;
	madd.s	f3, f4, f0	# hAcc, tmp405, *_68
.L29:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:247:                 rr[i].curDist2 += 2 * rr[i].curDx + 1;
	l32i.n	a3, a5, 16	# MEM[base: _401, offset: 16B], _74
# ../../src/display/ui/default/bganim/AnimRipples.cpp:247:                 rr[i].curDist2 += 2 * rr[i].curDx + 1;
	l32i.n	a7, a5, 20	# MEM[base: _401, offset: 20B], MEM[base: _401, offset: 20B]
# ../../src/display/ui/default/bganim/AnimRipples.cpp:247:                 rr[i].curDist2 += 2 * rr[i].curDx + 1;
	slli	a6, a3, 1	# tmp414, _74,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:247:                 rr[i].curDist2 += 2 * rr[i].curDx + 1;
	addi.n	a6, a6, 1	# tmp415, tmp414,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:249:                 while (rr[i].curDist2 >= rr[i].curR2 + 2 * rr[i].curR + 1) {
	l32i.n	a2, a5, 28	# MEM[base: _401, offset: 28B], _84
# ../../src/display/ui/default/bganim/AnimRipples.cpp:247:                 rr[i].curDist2 += 2 * rr[i].curDx + 1;
	add.n	a6, a6, a7	# _77, tmp415, MEM[base: _401, offset: 20B]
# ../../src/display/ui/default/bganim/AnimRipples.cpp:248:                 rr[i].curDx += 1;
	addi.n	a7, a3, 1	# tmp417, _74,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:249:                 while (rr[i].curDist2 >= rr[i].curR2 + 2 * rr[i].curR + 1) {
	slli	a3, a4, 1	# _275, _60,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:248:                 rr[i].curDx += 1;
	s32i.n	a7, a5, 16	# MEM[base: _401, offset: 16B], tmp417
# ../../src/display/ui/default/bganim/AnimRipples.cpp:247:                 rr[i].curDist2 += 2 * rr[i].curDx + 1;
	s32i.n	a6, a5, 20	# MEM[base: _401, offset: 20B], _77
# ../../src/display/ui/default/bganim/AnimRipples.cpp:249:                 while (rr[i].curDist2 >= rr[i].curR2 + 2 * rr[i].curR + 1) {
	add.n	a7, a2, a3	# tmp418, _84, _275
	addi.n	a4, a4, 1	# ivtmp$50, _60,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:249:                 while (rr[i].curDist2 >= rr[i].curR2 + 2 * rr[i].curR + 1) {
	blt	a7, a6, .L33	# tmp418, _77,
.L34:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:253:                 while (rr[i].curDist2 < rr[i].curR2) {
	blt	a6, a2, .L79	# _77, _84,
	j	.L28		#
.L33:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:250:                     rr[i].curR2 += 2 * rr[i].curR + 1;
	addi.n	a7, a3, 1	# tmp420, _275,
	add.n	a2, a2, a7	# _84, _84, tmp420
	addi.n	a3, a3, 2	# _275, _275,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:249:                 while (rr[i].curDist2 >= rr[i].curR2 + 2 * rr[i].curR + 1) {
	add.n	a7, a2, a3	# tmp422, _84, _275
# ../../src/display/ui/default/bganim/AnimRipples.cpp:251:                     rr[i].curR++;
	mov.n	a9, a4	# _85, ivtmp$50
	addi.n	a4, a4, 1	# ivtmp$50, ivtmp$50,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:249:                 while (rr[i].curDist2 >= rr[i].curR2 + 2 * rr[i].curR + 1) {
	blt	a7, a6, .L33	# tmp422, _77,
	s32i.n	a9, a5, 24	# MEM[base: _401, offset: 24B], _85
	s32i.n	a2, a5, 28	# MEM[base: _401, offset: 28B], _84
	j	.L34		#
.L79:
	l32i.n	a4, a5, 24	# MEM[base: _401, offset: 24B], rr_I_curR_lsm$33
	slli	a3, a4, 1	# tmp423, rr_I_curR_lsm$33,
	addi.n	a3, a3, -1	# ivtmp$45, tmp423,
.L35:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:254:                     rr[i].curR2 -= 2 * rr[i].curR - 1;
	sub	a2, a2, a3	# _84, _84, ivtmp$45
# ../../src/display/ui/default/bganim/AnimRipples.cpp:255:                     rr[i].curR--;
	addi.n	a4, a4, -1	# rr_I_curR_lsm$33, rr_I_curR_lsm$33,
	addi	a3, a3, -2	# ivtmp$45, ivtmp$45,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:253:                 while (rr[i].curDist2 < rr[i].curR2) {
	blt	a6, a2, .L35	# _77, _84,
	s32i.n	a4, a5, 24	# MEM[base: _401, offset: 24B], rr_I_curR_lsm$33
	s32i.n	a2, a5, 28	# MEM[base: _401, offset: 28B], _84
.L28:
	addi	a5, a5, 32	# ivtmp$57, ivtmp$57,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:231:             for (int i = 0; i < nrr; i++) {
	bne	a10, a5, .L36	# _384, ivtmp$57,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:258:             if (hAcc != 0) {
	oeq.s	b0, f3, f2	#, hAcc, tmp475
	bt	b0, .L52	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:259:                 const float g = hAcc * g_glow;
	mul.s	f0, f12, f3	# g, g_glow$12_92, hAcc
# ../../src/display/ui/default/bganim/AnimRipples.cpp:261:                     cr += g * crestF[0];
	mov.s	f3, f6	# cr, baseR
# ../../src/display/ui/default/bganim/AnimRipples.cpp:260:                 if (g > 0) {
	olt.s	b0, f2, f0	#, tmp475, g
	bf	b0, .L80	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:262:                     cg += g * crestF[1];
	mov.s	f1, f7	# cg, baseG
# ../../src/display/ui/default/bganim/AnimRipples.cpp:263:                     cb += g * crestF[2];
	mov.s	f4, f8	# cb, baseB
# ../../src/display/ui/default/bganim/AnimRipples.cpp:261:                     cr += g * crestF[0];
	madd.s	f3, f15, f0	# cr, _93, g
# ../../src/display/ui/default/bganim/AnimRipples.cpp:262:                     cg += g * crestF[1];
	madd.s	f1, f14, f0	# cg, _95, g
# ../../src/display/ui/default/bganim/AnimRipples.cpp:263:                     cb += g * crestF[2];
	madd.s	f4, f13, f0	# cb, _97, g
	j	.L37		#
.L80:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:265:                     cr += g * troughF[0];
	lsi	f1, sp, 160	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:266:                     cg += g * troughF[1];
	lsi	f4, sp, 164	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:265:                     cr += g * troughF[0];
	madd.s	f3, f1, f0	# cr,, g
# ../../src/display/ui/default/bganim/AnimRipples.cpp:267:                     cb += g * troughF[2];
	lsi	f11, sp, 168	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:266:                     cg += g * troughF[1];
	mov.s	f1, f7	# cg, baseG
	madd.s	f1, f4, f0	# cg,, g
# ../../src/display/ui/default/bganim/AnimRipples.cpp:267:                     cb += g * troughF[2];
	mov.s	f4, f8	# cb, baseB
	madd.s	f4, f11, f0	# cb,, g
	j	.L37		#
.L52:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:229:             float cr = baseR, cg = baseG, cb = baseB;
	mov.s	f4, f8	# cb, baseB
# ../../src/display/ui/default/bganim/AnimRipples.cpp:229:             float cr = baseR, cg = baseG, cb = baseB;
	mov.s	f1, f7	# cg, baseG
# ../../src/display/ui/default/bganim/AnimRipples.cpp:229:             float cr = baseR, cg = baseG, cb = baseB;
	mov.s	f3, f6	# cr, baseR
.L37:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:270:             const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	extui	a2, a8, 0, 2	# tmp427, x,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:270:             const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	add.n	a2, a13, a2	# tmp429, tmp533, tmp427
	l8ui	a2, a2, 0	# BAYER4, tmp430
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.n	a3, a14	# prephitmp_452, prephitmp_452
# ../../src/display/ui/default/bganim/AnimRipples.cpp:270:             const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	float.s	f0, a2, 0	# tmp431, tmp430
	sub.s	f0, f0, f10	# tmp432, tmp431, tmp470
# ../../src/display/ui/default/bganim/AnimRipples.cpp:270:             const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
	mul.s	f0, f0, f9	# dith, tmp432, tmp471
# ../../src/display/ui/default/bganim/AnimRipples.cpp:271:             row[x] = rgb565(clamp8f(cr + dith), clamp8f(cg + dith), clamp8f(cb + dith));
	add.s	f3, f3, f0	# _113, cr, dith
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f3, f2	#, _113, tmp475
	bt	b0, .L40	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	l32r	a2, .LC22	#,
	mov.n	a3, a12	# prephitmp_452, prephitmp_452
	wfr	f11, a2	#,
	olt.s	b0, f11, f3	#,, _113
	bt	b0, .L40	#,
	utrunc.s	a3, f3, 0	# tmp438, _113
	movi	a4, -0x800	#,
	extui	a3, a3, 0, 8	# tmp439, tmp438
	slli	a3, a3, 8	# tmp440, tmp439,
	and	a3, a3, a4	# tmp443, tmp440,
	sext	a3, a3, 15	# prephitmp_452, tmp443
.L40:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:271:             row[x] = rgb565(clamp8f(cr + dith), clamp8f(cg + dith), clamp8f(cb + dith));
	add.s	f1, f1, f0	# _115, cg, dith
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f1, f2	#, _115, tmp475
	bt	b0, .L41	#,
	l32r	a2, .LC22	#,
	wfr	f3, a2	#,
	olt.s	b0, f3, f1	#,, _115
	bf	b0, .L81	#,
	movi	a2, 0x7e0	# tmp447,
	or	a3, a3, a2	# prephitmp_452, prephitmp_452, tmp447
	j	.L41		#
.L81:
	utrunc.s	a2, f1, 0	# tmp449, _115
	movi	a4, 0x7e0	# tmp452,
	extui	a2, a2, 0, 8	# tmp450, tmp449
	slli	a2, a2, 3	# tmp451, tmp450,
	and	a2, a2, a4	# tmp454, tmp451, tmp452
	or	a3, a3, a2	# prephitmp_452, prephitmp_452, tmp454
.L41:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:271:             row[x] = rgb565(clamp8f(cr + dith), clamp8f(cg + dith), clamp8f(cb + dith));
	add.s	f0, f4, f0	# _117, cb, dith
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f0, f2	#, _117, tmp475
	bt	b0, .L44	#,
	l32r	a2, .LC22	#,
	wfr	f1, a2	#,
	olt.s	b0, f1, f0	#,, _117
	bf	b0, .L82	#,
	movi.n	a2, 0x1f	# tmp460,
	or	a3, a3, a2	# prephitmp_452, prephitmp_452, tmp460
	j	.L44		#
.L82:
	utrunc.s	a2, f0, 0	# tmp462, _117
	extui	a2, a2, 3, 5	# tmp464, tmp462,,
	or	a3, a3, a2	# prephitmp_452, prephitmp_452, tmp464
.L44:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:271:             row[x] = rgb565(clamp8f(cr + dith), clamp8f(cg + dith), clamp8f(cb + dith));
	s16i	a3, a11, 0	# MEM[base: _380, offset: 0B], prephitmp_452
# ../../src/display/ui/default/bganim/AnimRipples.cpp:228:         for (int x = 0; x < w; x++) {
	l32i	a3, sp, 144	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:228:         for (int x = 0; x < w; x++) {
	addi.n	a8, a8, 1	# x, x,
	addi.n	a11, a11, 2	# ivtmp$60, ivtmp$60,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:228:         for (int x = 0; x < w; x++) {
	bne	a3, a8, .L47	#, x,
	j	.L4		#
.L1:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:274: }
	retw.n
	.size	_ZN12_GLOBAL__N_14bandEPtiiijPKh, .-_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.global	__divsf3
	.section	.text._ZN12_GLOBAL__N_15frameEjiiPKh,"ax",@progbits
	.literal_position
	.literal .LC29, 0x3f800000
	.literal .LC30, _ZN12_GLOBAL__N_15g_tMsE
	.literal .LC31, _ZN12_GLOBAL__N_112lastThemeGenE
	.literal .LC32, _ZN12_GLOBAL__N_16crestFE
	.literal .LC33, _ZN12_GLOBAL__N_17troughFE
	.literal .LC34, 0x3f266666
	.literal .LC35, 0x3df5c28f
	.literal .LC36, _ZN12_GLOBAL__N_110nextDropMsE
	.literal .LC37, _ZN12_GLOBAL__N_17ripplesE
	.literal .LC38, _ZN12_GLOBAL__N_13rngE
	.literal .LC39, 0x33800000
	.literal .LC40, 0x3f666666
	.literal .LC41, 0x3f0ccccd
	.literal .LC42, 0x42c80000
	.literal .LC43, 0xc6435000
	.literal .LC44, 0x465ac000
	.literal .LC45, 0x43430000
	.literal .LC46, 0x41c80000
	.literal .LC47, 0xc099999a
	.literal .LC48, 0x40e00000
	.literal .LC49, _ZN12_GLOBAL__N_16g_glowE
	.literal .LC50, 0x3f933333
	.literal .LC51, 0x3eb33333
	.literal .LC52, _ZN12_GLOBAL__N_13g_nE
	.literal .LC53, 0x3a83126f
	.literal .LC54, 0x3e3851ec
	.literal .LC55, 0x00000000
	.literal .LC56, 0x3c03126f
	.literal .LC57, _ZN12_GLOBAL__N_14g_cxE
	.literal .LC58, _ZN12_GLOBAL__N_14g_cyE
	.literal .LC59, _ZN12_GLOBAL__N_15g_icxE
	.literal .LC60, 0x3f000000
	.literal .LC61, _ZN12_GLOBAL__N_15g_icyE
	.literal .LC62, _ZN12_GLOBAL__N_13g_rE
	.literal .LC63, _ZN12_GLOBAL__N_15g_ampE
	.literal .LC64, _ZN12_GLOBAL__N_17ripplesE+64
	.align	4
	.type	_ZN12_GLOBAL__N_15frameEjiiPKh, @function
_ZN12_GLOBAL__N_15frameEjiiPKh:
	entry	sp, 80	#
# ../../src/display/ui/default/bganim/AnimRipples.cpp:116:     g_tMs = tMs;
	l32r	a6, .LC30	#, tmp147
	s32i.n	a2, a6, 0	# g_tMs, tMs
# ../../src/display/ui/default/bganim/AnimRipples.cpp:117:     if (themeGen() != lastThemeGen) {
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimRipples.cpp:117:     if (themeGen() != lastThemeGen) {
	l32r	a6, .LC31	#, tmp265
# ../../src/display/ui/default/bganim/AnimRipples.cpp:117:     if (themeGen() != lastThemeGen) {
	l32i.n	a7, a6, 0	# lastThemeGen, lastThemeGen
	beq	a10, a7, .L87	#, lastThemeGen,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:108:     themeRGB(255, c);
	mov.n	a11, sp	#,
	movi	a10, 0xff	#,
	call8	_ZN6bganim8themeRGBEiPh		#
	l32r	a8, .LC34	#,
	l32r	a9, .LC32	#, ivtmp$103
	wfr	f3, a8	# tmp260,
	l32r	a8, .LC35	#,
	l32r	a10, .LC33	#, ivtmp$104
	mov.n	a11, sp	# ivtmp$102,
	wfr	f2, a8	# tmp261,
	addi.n	a7, a9, 12	# _194, ivtmp$103,
.L88:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:110:         crestF[ch] = c[ch] * 0.65f;
	l8ui	a8, a11, 0	# MEM[base: _198, offset: 0B], MEM[base: _198, offset: 0B]
	addi.n	a11, a11, 1	# ivtmp$102, ivtmp$102,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:110:         crestF[ch] = c[ch] * 0.65f;
	float.s	f0, a8, 0	# tmp151, MEM[base: _198, offset: 0B]
	mul.s	f0, f0, f3	# _130, tmp151, tmp260
# ../../src/display/ui/default/bganim/AnimRipples.cpp:111:         troughF[ch] = crestF[ch] * 0.12f;
	mul.s	f1, f0, f2	# tmp153, _130, tmp261
# ../../src/display/ui/default/bganim/AnimRipples.cpp:110:         crestF[ch] = c[ch] * 0.65f;
	ssi	f0, a9, 0	# MEM[base: _197, offset: 0B], _130
	addi.n	a9, a9, 4	# ivtmp$103, ivtmp$103,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:111:         troughF[ch] = crestF[ch] * 0.12f;
	ssi	f1, a10, 0	# MEM[base: _196, offset: 0B], tmp153
	addi.n	a10, a10, 4	# ivtmp$104, ivtmp$104,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:109:     for (int ch = 0; ch < 3; ch++) {
	bne	a7, a9, .L88	# _194, ivtmp$103,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:119:         lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimRipples.cpp:119:         lastThemeGen = themeGen();
	s32i.n	a10, a6, 0	# lastThemeGen,
.L87:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:122:     if (tMs >= nextDropMs) {
	l32r	a12, .LC36	#, tmp258
	l32r	a7, .LC37	#, ivtmp$85
# ../../src/display/ui/default/bganim/AnimRipples.cpp:122:     if (tMs >= nextDropMs) {
	l32i.n	a6, a12, 0	# nextDropMs, nextDropMs
	bgeu	a2, a6, .L108	# tMs, nextDropMs,
	l32r	a6, .LC64	#, _214
	j	.L89		#
.L108:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:121:     const float interval = lerpf(14000.0f, 1500.0f, p[1] / 100.0f);
	l8ui	a10, a5, 1	# MEM[(const uint8_t *)p_63(D) + 1B], _4
	mov.n	a8, a7	# ivtmp$92, ivtmp$85
	addi	a6, a7, 64	# _214, ivtmp$85,
	movi.n	a9, 4	# tmp271,
	loop	a9, .L92_LEND	# tmp271,
.L92:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:124:             if (!r.active) {
	l8ui	a11, a8, 12	# MEM[base: __for_begin_155, offset: 12B], MEM[base: __for_begin_155, offset: 12B]
	bnez.n	a11, .L90	# MEM[base: __for_begin_155, offset: 12B],
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	l32r	a9, .LC38	#, tmp266
# ../../src/display/ui/default/bganim/AnimRipples.cpp:125:                 r = {nextRandf(rng) * w, nextRandf(rng) * h, tMs, true};
	float.s	f2, a3, 0	# tmp170, w
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	l32i.n	a13, a9, 0	# rng, _100
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	l32r	a11, .LC39	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a3, a13, 13	# tmp160, _100,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a3, a3, a13	# _102, tmp160, _100
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a13, a3, 17, 15	# tmp161, _102,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a3, a13, a3	# _104, tmp161, _102
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a13, a3, 5	# tmp162, _104,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a13, a13, a3	# _106, tmp162, _104
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	wfr	f3, a11	# tmp262,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a11, a13, 13	# tmp163, _106,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a11, a11, a13	# _92, tmp163, _106
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a3, a11, 17, 15	# tmp164, _92,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a3, a3, a11	# _94, tmp164, _92
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a11, a3, 5	# tmp165, _94,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a11, a11, a3	# pretmp_231, tmp165, _94
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a3, a11, 8	# tmp172, pretmp_231,
	srli	a13, a13, 8	# tmp166, _106,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f1, a3, 0	# tmp173, tmp172
	float.s	f0, a13, 0	# tmp167, tmp166
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f4, f1, f3	# tmp174, tmp173, tmp262
	mul.s	f0, f0, f3	# tmp168, tmp167, tmp262
# ../../src/display/ui/default/bganim/AnimRipples.cpp:125:                 r = {nextRandf(rng) * w, nextRandf(rng) * h, tMs, true};
	float.s	f1, a4, 0	# tmp176, h
# ../../src/display/ui/default/bganim/AnimRipples.cpp:125:                 r = {nextRandf(rng) * w, nextRandf(rng) * h, tMs, true};
	movi.n	a3, 1	# tmp178,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:125:                 r = {nextRandf(rng) * w, nextRandf(rng) * h, tMs, true};
	mul.s	f1, f4, f1	# tmp177, tmp174, tmp176
# ../../src/display/ui/default/bganim/AnimRipples.cpp:125:                 r = {nextRandf(rng) * w, nextRandf(rng) * h, tMs, true};
	mul.s	f2, f0, f2	# tmp171, tmp168, tmp170
# ../../src/display/ui/default/bganim/AnimRipples.cpp:125:                 r = {nextRandf(rng) * w, nextRandf(rng) * h, tMs, true};
	s32i.n	a2, a8, 8	# __for_begin_155->birthMs, tMs
	s8i	a3, a8, 12	# __for_begin_155->active, tmp178
	ssi	f1, a8, 4	# __for_begin_155->cy, tmp177
	ssi	f2, a8, 0	# __for_begin_155->cx, tmp171
# ../../src/display/ui/default/bganim/AnimRipples.cpp:126:                 break;
	j	.L91		#
.L90:
	addi	a8, a8, 16	# ivtmp$92, ivtmp$92,
	.L92_LEND:		#
	l32r	a9, .LC38	#, tmp266
	l32r	a3, .LC39	#,
	l32i.n	a11, a9, 0	# rng, pretmp_231
	wfr	f3, a3	# tmp262,
.L91:
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a3, a11, 13	# tmp180, pretmp_231,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a3, a3, a11	# _112, tmp180, pretmp_231
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a4, a3, 17, 15	# tmp181, _112,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a3, a4, a3	# _114, tmp181, _112
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a4, a3, 5	# tmp182, _114,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a4, a4, a3	# _116, tmp182, _114
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a3, a4, 8	# tmp185, _116,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f0, a3, 0	# tmp186, tmp185
# ../../src/display/ui/default/bganim/AnimRipples.cpp:129:         nextDropMs = tMs + static_cast<uint32_t>(interval * (0.55f + 0.9f * nextRandf(rng)));
	l32r	a3, .LC41	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	mul.s	f0, f0, f3	# tmp187, tmp186, tmp262
# ../../src/display/ui/default/bganim/AnimRipples.cpp:129:         nextDropMs = tMs + static_cast<uint32_t>(interval * (0.55f + 0.9f * nextRandf(rng)));
	wfr	f2, a3	#,
	l32r	a3, .LC40	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:121:     const float interval = lerpf(14000.0f, 1500.0f, p[1] / 100.0f);
	float.s	f1, a10, 0	#, _4
# ../../src/display/ui/default/bganim/AnimRipples.cpp:129:         nextDropMs = tMs + static_cast<uint32_t>(interval * (0.55f + 0.9f * nextRandf(rng)));
	wfr	f3, a3	#,
	madd.s	f2, f0, f3	#, tmp187,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:121:     const float interval = lerpf(14000.0f, 1500.0f, p[1] / 100.0f);
	l32r	a11, .LC42	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	s32i.n	a4, a9, 0	# rng, _116
# ../../src/display/ui/default/bganim/AnimRipples.cpp:121:     const float interval = lerpf(14000.0f, 1500.0f, p[1] / 100.0f);
	rfr	a10, f1	#,
	s32i.n	a12, sp, 32	#,
	ssi	f2, sp, 36	#, tmp19
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:50: inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
	l32r	a3, .LC44	#,
	wfr	f2, a10	#,
	wfr	f1, a3	# tmp194,
	l32r	a3, .LC43	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:129:         nextDropMs = tMs + static_cast<uint32_t>(interval * (0.55f + 0.9f * nextRandf(rng)));
	lsi	f0, sp, 36	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:50: inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
	wfr	f3, a3	#,
	madd.s	f1, f2, f3	# tmp194,,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:129:         nextDropMs = tMs + static_cast<uint32_t>(interval * (0.55f + 0.9f * nextRandf(rng)));
	l32i.n	a12, sp, 32	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:129:         nextDropMs = tMs + static_cast<uint32_t>(interval * (0.55f + 0.9f * nextRandf(rng)));
	mul.s	f0, f0, f1	# tmp197, tmp189, tmp194
# ../../src/display/ui/default/bganim/AnimRipples.cpp:129:         nextDropMs = tMs + static_cast<uint32_t>(interval * (0.55f + 0.9f * nextRandf(rng)));
	utrunc.s	a4, f0, 0	# tmp198, tmp197
# ../../src/display/ui/default/bganim/AnimRipples.cpp:129:         nextDropMs = tMs + static_cast<uint32_t>(interval * (0.55f + 0.9f * nextRandf(rng)));
	add.n	a4, a4, a2	# tmp199, tmp198, tMs
# ../../src/display/ui/default/bganim/AnimRipples.cpp:129:         nextDropMs = tMs + static_cast<uint32_t>(interval * (0.55f + 0.9f * nextRandf(rng)));
	s32i.n	a4, a12, 0	# nextDropMs, tmp199
.L89:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:131:     const float speed = lerpf(25.0f, 220.0f, p[0] / 100.0f);
	l8ui	a10, a5, 0	# *p_63(D), *p_63(D)
# ../../src/display/ui/default/bganim/AnimRipples.cpp:131:     const float speed = lerpf(25.0f, 220.0f, p[0] / 100.0f);
	l32r	a11, .LC42	#,
	float.s	f0, a10, 0	#, *p_63(D)
	rfr	a10, f0	#,
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:50: inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
	l32r	a4, .LC46	#,
	wfr	f2, a10	#,
	wfr	f1, a4	#,
	l32r	a4, .LC45	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:132:     const float life = lerpf(7.0f, 2.2f, p[2] / 100.0f);
	l8ui	a3, a5, 2	# MEM[(const uint8_t *)p_63(D) + 2B], MEM[(const uint8_t *)p_63(D) + 2B]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:50: inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
	wfr	f3, a4	#,
	madd.s	f1, f2, f3	#,,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:132:     const float life = lerpf(7.0f, 2.2f, p[2] / 100.0f);
	float.s	f0, a3, 0	#, MEM[(const uint8_t *)p_63(D) + 2B]
	l32r	a11, .LC42	#,
	rfr	a10, f0	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:50: inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
	ssi	f1, sp, 28	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:132:     const float life = lerpf(7.0f, 2.2f, p[2] / 100.0f);
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:50: inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
	l32r	a4, .LC48	#,
	wfr	f2, a10	#,
	wfr	f1, a4	#,
	l32r	a4, .LC47	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:133:     g_glow = 0.35f + 1.15f * (p[3] / 100.0f);
	l8ui	a3, a5, 3	# MEM[(const uint8_t *)p_63(D) + 3B], MEM[(const uint8_t *)p_63(D) + 3B]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:50: inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
	wfr	f3, a4	#,
	madd.s	f1, f2, f3	#,,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:133:     g_glow = 0.35f + 1.15f * (p[3] / 100.0f);
	float.s	f0, a3, 0	#, MEM[(const uint8_t *)p_63(D) + 3B]
	l32r	a11, .LC42	#,
	rfr	a10, f0	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:50: inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
	ssi	f1, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:133:     g_glow = 0.35f + 1.15f * (p[3] / 100.0f);
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimRipples.cpp:133:     g_glow = 0.35f + 1.15f * (p[3] / 100.0f);
	l32r	a3, .LC51	#,
	wfr	f1, a10	#,
	wfr	f0, a3	# tmp214,
	l32r	a3, .LC50	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:135:     g_n = 0;
	l32r	a4, .LC52	#, tmp264
# ../../src/display/ui/default/bganim/AnimRipples.cpp:133:     g_glow = 0.35f + 1.15f * (p[3] / 100.0f);
	wfr	f2, a3	#,
	madd.s	f0, f1, f2	# tmp214,,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:133:     g_glow = 0.35f + 1.15f * (p[3] / 100.0f);
	l32r	a3, .LC49	#, tmp210
	ssi	f0, a3, 0	# g_glow, tmp214
# ../../src/display/ui/default/bganim/AnimRipples.cpp:135:     g_n = 0;
	movi.n	a3, 0	# tmp218,
	s32i.n	a3, a4, 0	# g_n, tmp218
.L100:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:137:         if (!r.active) {
	l8ui	a8, a7, 12	# MEM[base: _216, offset: 12B], MEM[base: _216, offset: 12B]
	beqz.n	a8, .L94	# MEM[base: _216, offset: 12B],
# ../../src/display/ui/default/bganim/AnimRipples.cpp:140:         const float ageS = (tMs - r.birthMs) * 0.001f;
	l32i.n	a8, a7, 8	# MEM[base: _216, offset: 8B], MEM[base: _216, offset: 8B]
# ../../src/display/ui/default/bganim/AnimRipples.cpp:140:         const float ageS = (tMs - r.birthMs) * 0.001f;
	l32r	a3, .LC53	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:140:         const float ageS = (tMs - r.birthMs) * 0.001f;
	sub	a8, a2, a8	# tmp220, tMs, MEM[base: _216, offset: 8B]
# ../../src/display/ui/default/bganim/AnimRipples.cpp:140:         const float ageS = (tMs - r.birthMs) * 0.001f;
	ufloat.s	f0, a8, 0	# tmp222, tmp220
# ../../src/display/ui/default/bganim/AnimRipples.cpp:140:         const float ageS = (tMs - r.birthMs) * 0.001f;
	wfr	f1, a3	#,
	mul.s	f0, f0, f1	# ageS, tmp222,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:141:         if (ageS >= life) {
	lsi	f1, sp, 16	# %sfp,
	ole.s	b0, f1, f0	#,, ageS
	bf	b0, .L109	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:142:             r.active = false;
	movi.n	a5, 0	# tmp224,
	s8i	a5, a7, 12	# MEM[base: _216, offset: 12B], tmp224
# ../../src/display/ui/default/bganim/AnimRipples.cpp:143:             continue;
	j	.L94		#
.L109:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:145:         const float radius = speed * ageS;
	lsi	f2, sp, 28	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:146:         const float rise = ageS < 0.18f ? ageS / 0.18f : 1.0f;
	l32r	a3, .LC54	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:145:         const float radius = speed * ageS;
	mul.s	f2, f0, f2	#, ageS,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:146:         const float rise = ageS < 0.18f ? ageS / 0.18f : 1.0f;
	wfr	f3, a3	#,
	olt.s	b0, f0, f3	#, ageS,
	mov.n	a11, a3	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:145:         const float radius = speed * ageS;
	ssi	f2, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:146:         const float rise = ageS < 0.18f ? ageS / 0.18f : 1.0f;
	rfr	a10, f0	#, ageS
	bf	b0, .L110	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:146:         const float rise = ageS < 0.18f ? ageS / 0.18f : 1.0f;
	ssi	f0, sp, 36	#,
	call8	__divsf3		#
	s32i.n	a10, sp, 24	# %sfp,
	lsi	f0, sp, 36	#,
	j	.L97		#
.L110:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:146:         const float rise = ageS < 0.18f ? ageS / 0.18f : 1.0f;
	l32r	a3, .LC29	#,
	s32i.n	a3, sp, 24	# %sfp, tmp20
.L97:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:147:         const float amp = rise * expf(-ageS / life);
	neg.s	f0, f0	#, ageS
	l32i.n	a11, sp, 16	# %sfp,
	rfr	a10, f0	#,
	call8	__divsf3		#
	call8	expf		#
# ../../src/display/ui/default/bganim/AnimRipples.cpp:148:         if (radius <= 0 || amp < 0.008f) {
	l32r	a3, .LC55	#,
	lsi	f1, sp, 20	# %sfp,
	wfr	f2, a3	#,
	ole.s	b0, f1, f2	#,,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:147:         const float amp = rise * expf(-ageS / life);
	wfr	f0, a10	# _39,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:148:         if (radius <= 0 || amp < 0.008f) {
	bt	b0, .L94	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:147:         const float amp = rise * expf(-ageS / life);
	lsi	f1, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:148:         if (radius <= 0 || amp < 0.008f) {
	l32r	a3, .LC56	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:147:         const float amp = rise * expf(-ageS / life);
	mul.s	f0, f0, f1	# amp, _39,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:148:         if (radius <= 0 || amp < 0.008f) {
	wfr	f2, a3	#,
	olt.s	b0, f0, f2	#, amp,
	bt	b0, .L94	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:155:         g_icx[g_n] = static_cast<int>(r.cx + 0.5f);
	l32r	a8, .LC60	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:151:         g_cx[g_n] = r.cx;
	lsi	f3, a7, 0	# MEM[base: _216, offset: 0B], _41
# ../../src/display/ui/default/bganim/AnimRipples.cpp:152:         g_cy[g_n] = r.cy;
	lsi	f2, a7, 4	# MEM[base: _216, offset: 4B], _42
# ../../src/display/ui/default/bganim/AnimRipples.cpp:155:         g_icx[g_n] = static_cast<int>(r.cx + 0.5f);
	wfr	f1, a8	# tmp242,
	add.s	f4, f3, f1	# tmp241, _41, tmp242
# ../../src/display/ui/default/bganim/AnimRipples.cpp:156:         g_icy[g_n] = static_cast<int>(r.cy + 0.5f);
	add.s	f1, f2, f1	# tmp247, _42, tmp242
# ../../src/display/ui/default/bganim/AnimRipples.cpp:151:         g_cx[g_n] = r.cx;
	l32i.n	a8, a4, 0	# g_n, g_n$20_40
# ../../src/display/ui/default/bganim/AnimRipples.cpp:151:         g_cx[g_n] = r.cx;
	l32r	a15, .LC57	#, tmp232
# ../../src/display/ui/default/bganim/AnimRipples.cpp:152:         g_cy[g_n] = r.cy;
	l32r	a14, .LC58	#, tmp235
# ../../src/display/ui/default/bganim/AnimRipples.cpp:155:         g_icx[g_n] = static_cast<int>(r.cx + 0.5f);
	l32r	a12, .LC59	#, tmp238
# ../../src/display/ui/default/bganim/AnimRipples.cpp:156:         g_icy[g_n] = static_cast<int>(r.cy + 0.5f);
	l32r	a11, .LC61	#, tmp244
# ../../src/display/ui/default/bganim/AnimRipples.cpp:157:         g_r[g_n] = radius;
	l32r	a10, .LC62	#, tmp250
# ../../src/display/ui/default/bganim/AnimRipples.cpp:158:         g_amp[g_n] = amp;
	l32r	a3, .LC63	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:151:         g_cx[g_n] = r.cx;
	slli	a5, a8, 2	# tmp233, g_n$20_40,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:156:         g_icy[g_n] = static_cast<int>(r.cy + 0.5f);
	trunc.s	a9, f1, 0	# tmp249, tmp247
# ../../src/display/ui/default/bganim/AnimRipples.cpp:155:         g_icx[g_n] = static_cast<int>(r.cx + 0.5f);
	trunc.s	a13, f4, 0	# tmp243, tmp241
# ../../src/display/ui/default/bganim/AnimRipples.cpp:157:         g_r[g_n] = radius;
	lsi	f1, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:151:         g_cx[g_n] = r.cx;
	add.n	a15, a15, a5	# tmp234, tmp232, tmp233
# ../../src/display/ui/default/bganim/AnimRipples.cpp:152:         g_cy[g_n] = r.cy;
	add.n	a14, a14, a5	# tmp237, tmp235, tmp233
# ../../src/display/ui/default/bganim/AnimRipples.cpp:155:         g_icx[g_n] = static_cast<int>(r.cx + 0.5f);
	add.n	a12, a12, a5	# tmp240, tmp238, tmp233
# ../../src/display/ui/default/bganim/AnimRipples.cpp:156:         g_icy[g_n] = static_cast<int>(r.cy + 0.5f);
	add.n	a11, a11, a5	# tmp246, tmp244, tmp233
# ../../src/display/ui/default/bganim/AnimRipples.cpp:157:         g_r[g_n] = radius;
	add.n	a10, a10, a5	# tmp252, tmp250, tmp233
# ../../src/display/ui/default/bganim/AnimRipples.cpp:159:         g_n++;
	addi.n	a8, a8, 1	# tmp257, g_n$20_40,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:158:         g_amp[g_n] = amp;
	add.n	a5, a3, a5	# tmp255,, tmp233
# ../../src/display/ui/default/bganim/AnimRipples.cpp:151:         g_cx[g_n] = r.cx;
	ssi	f3, a15, 0	# g_cx, _41
# ../../src/display/ui/default/bganim/AnimRipples.cpp:152:         g_cy[g_n] = r.cy;
	ssi	f2, a14, 0	# g_cy, _42
# ../../src/display/ui/default/bganim/AnimRipples.cpp:155:         g_icx[g_n] = static_cast<int>(r.cx + 0.5f);
	s32i.n	a13, a12, 0	# g_icx, tmp243
# ../../src/display/ui/default/bganim/AnimRipples.cpp:156:         g_icy[g_n] = static_cast<int>(r.cy + 0.5f);
	s32i.n	a9, a11, 0	# g_icy, tmp249
# ../../src/display/ui/default/bganim/AnimRipples.cpp:157:         g_r[g_n] = radius;
	ssi	f1, a10, 0	# g_r,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:158:         g_amp[g_n] = amp;
	ssi	f0, a5, 0	# g_amp, amp
# ../../src/display/ui/default/bganim/AnimRipples.cpp:159:         g_n++;
	s32i.n	a8, a4, 0	# g_n, tmp257
.L94:
	addi	a7, a7, 16	# ivtmp$85, ivtmp$85,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:136:     for (auto &r : ripples) {
	bne	a6, a7, .L100	# _214, ivtmp$85,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:161: }
	retw.n
	.size	_ZN12_GLOBAL__N_15frameEjiiPKh, .-_ZN12_GLOBAL__N_15frameEjiiPKh
	.section	.text._ZN12_GLOBAL__N_14initEii,"ax",@progbits
	.literal_position
	.literal .LC65, _ZN12_GLOBAL__N_16envLUTE
	.literal .LC66, 0x437f0000
	.literal .LC67, 0x3f800000
	.literal .LC68, _ZN12_GLOBAL__N_110g_cosTableE
	.literal .LC69, _ZN12_GLOBAL__N_16initedE
	.literal .LC70, _ZN12_GLOBAL__N_17ripplesE
	.literal .LC71, _ZN12_GLOBAL__N_13rngE
	.literal .LC72, _ZN12_GLOBAL__N_110nextDropMsE
	.literal .LC73, 0x33800000
	.literal .LC74, 0x44fa0000
	.align	4
	.type	_ZN12_GLOBAL__N_14initEii, @function
_ZN12_GLOBAL__N_14initEii:
	entry	sp, 32	#
# ../../src/display/ui/default/bganim/AnimRipples.cpp:78:     if (envLUT == nullptr) {
	l32r	a3, .LC65	#, tmp70
# ../../src/display/ui/default/bganim/AnimRipples.cpp:78:     if (envLUT == nullptr) {
	l32i.n	a2, a3, 0	# envLUT, envLUT
	bnez.n	a2, .L112	# envLUT,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:79:         envLUT = static_cast<float *>(alloc(256 * sizeof(float)));
	movi	a10, 0x400	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimRipples.cpp:79:         envLUT = static_cast<float *>(alloc(256 * sizeof(float)));
	s32i.n	a10, a3, 0	# envLUT, _2
# ../../src/display/ui/default/bganim/AnimRipples.cpp:80:         if (envLUT == nullptr) {
	beqz.n	a10, .L116	# _2,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:84:             const float n = i / 255.0f;
	l32r	a4, .LC66	#, tmp108
	mov.n	a3, a10	# ivtmp$111, _2
.L114:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:84:             const float n = i / 255.0f;
	float.s	f0, a2, 0	#, i
	mov.n	a11, a4	#, tmp108
	rfr	a10, f0	#,
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimRipples.cpp:85:             envLUT[i] = 1.0f - n * n;
	l32r	a5, .LC67	#,
	wfr	f1, a10	#,
	wfr	f0, a5	# tmp75,
	msub.s	f0, f1, f1	# tmp75,,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:83:         for (int i = 0; i < 256; i++) {
	addi.n	a2, a2, 1	# i, i,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:85:             envLUT[i] = 1.0f - n * n;
	ssi	f0, a3, 0	# MEM[base: _67, offset: 0B], tmp75
	addi.n	a3, a3, 4	# ivtmp$111, ivtmp$111,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:83:         for (int i = 0; i < 256; i++) {
	bnei	a2, 256, .L114	# i,,
.L112:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:88:     if (g_cosTable == nullptr) {
	l32r	a2, .LC68	#, tmp77
# ../../src/display/ui/default/bganim/AnimRipples.cpp:88:     if (g_cosTable == nullptr) {
	l32i.n	a3, a2, 0	# g_cosTable, g_cosTable
	bnez.n	a3, .L115	# g_cosTable,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:92:         g_cosTable = cosTableF();
	call8	_ZN6bganim9cosTableFEv		#
# ../../src/display/ui/default/bganim/AnimRipples.cpp:92:         g_cosTable = cosTableF();
	s32i.n	a10, a2, 0	# g_cosTable,
.L115:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:94:     if (!inited) {
	l32r	a4, .LC69	#, tmp80
	l8ui	a2, a4, 0	# inited, <retval>
# ../../src/display/ui/default/bganim/AnimRipples.cpp:94:     if (!inited) {
	bnez.n	a2, .L113	# <retval>,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	l32r	a10, .LC71	#, tmp91
# ../../src/display/ui/default/bganim/AnimRipples.cpp:97:             r.active = false;
	l32r	a9, .LC70	#, tmp83
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	l32i.n	a8, a10, 0	# rng, _32
# ../../src/display/ui/default/bganim/AnimRipples.cpp:97:             r.active = false;
	s8i	a2, a9, 12	# MEM[(struct Ripple &)&ripples].active, <retval>
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	slli	a3, a8, 13	# tmp92, _32,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:43:     s ^= s << 13;
	xor	a3, a3, a8	# _34, tmp92, _32
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	extui	a8, a3, 17, 15	# tmp93, _34,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:44:     s ^= s >> 17;
	xor	a3, a8, a3	# _36, tmp93, _34
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	slli	a8, a3, 5	# tmp94, _36,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	xor	a8, a8, a3	# _38, tmp94, _36
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	srli	a3, a8, 8	# tmp97, _38,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	float.s	f0, a3, 0	# tmp98, tmp97
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	l32r	a3, .LC73	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:97:             r.active = false;
	s8i	a2, a9, 28	# MEM[(struct Ripple &)&ripples + 16].active, <retval>
# ../../src/display/ui/default/bganim/BgAnimCommon.h:48: inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }
	wfr	f1, a3	#,
	mul.s	f0, f0, f1	# tmp99, tmp98,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:99:         nextDropMs = 600 + static_cast<uint32_t>(nextRandf(rng) * 2000);
	l32r	a3, .LC74	#,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:97:             r.active = false;
	s8i	a2, a9, 44	# MEM[(struct Ripple &)&ripples + 32].active, <retval>
# ../../src/display/ui/default/bganim/AnimRipples.cpp:99:         nextDropMs = 600 + static_cast<uint32_t>(nextRandf(rng) * 2000);
	wfr	f1, a3	#,
	mul.s	f0, f0, f1	# tmp101, tmp99,
# ../../src/display/ui/default/bganim/AnimRipples.cpp:97:             r.active = false;
	s8i	a2, a9, 60	# MEM[(struct Ripple &)&ripples + 48].active, <retval>
# ../../src/display/ui/default/bganim/AnimRipples.cpp:99:         nextDropMs = 600 + static_cast<uint32_t>(nextRandf(rng) * 2000);
	movi	a2, 0x258	# tmp104,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:45:     s ^= s << 5;
	s32i.n	a8, a10, 0	# rng, _38
# ../../src/display/ui/default/bganim/AnimRipples.cpp:99:         nextDropMs = 600 + static_cast<uint32_t>(nextRandf(rng) * 2000);
	utrunc.s	a3, f0, 0	# tmp103, tmp101
# ../../src/display/ui/default/bganim/AnimRipples.cpp:99:         nextDropMs = 600 + static_cast<uint32_t>(nextRandf(rng) * 2000);
	add.n	a3, a3, a2	# tmp105, tmp103, tmp104
# ../../src/display/ui/default/bganim/AnimRipples.cpp:95:         inited = true;
	movi.n	a2, 1	# tmp82,
	s8i	a2, a4, 0	# inited, tmp82
# ../../src/display/ui/default/bganim/AnimRipples.cpp:99:         nextDropMs = 600 + static_cast<uint32_t>(nextRandf(rng) * 2000);
	l32r	a2, .LC72	#, tmp96
	s32i.n	a3, a2, 0	# nextDropMs, tmp105
	movi.n	a2, 1	# <retval>,
	j	.L113		#
.L116:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:81:             return false;
	mov.n	a2, a10	# <retval>, _2
.L113:
# ../../src/display/ui/default/bganim/AnimRipples.cpp:102: }
	retw.n
	.size	_ZN12_GLOBAL__N_14initEii, .-_ZN12_GLOBAL__N_14initEii
	.global	bg_anim_ripples
	.section	.rodata.str1.4,"aMS",@progbits,1
	.align	4
.LC75:
	.string	"ripples"
	.align	4
.LC76:
	.string	"Ripples"
	.align	4
.LC77:
	.string	"speed"
	.align	4
.LC78:
	.string	"Ring speed"
	.align	4
.LC79:
	.string	"rate"
	.align	4
.LC80:
	.string	"Drop rate"
	.align	4
.LC81:
	.string	"decay"
	.align	4
.LC82:
	.string	"Fade"
	.align	4
.LC83:
	.string	"glow"
	.align	4
.LC84:
	.string	"Glow"
	.section	.rodata
	.align	4
	.type	bg_anim_ripples, @object
	.size	bg_anim_ripples, 68
bg_anim_ripples:
# id:
	.word	.LC75
# name:
	.word	.LC76
# params:
# key:
	.word	.LC77
# label:
	.word	.LC78
# def:
	.byte	50
	.zero	3
# key:
	.word	.LC79
# label:
	.word	.LC80
# def:
	.byte	40
	.zero	3
# key:
	.word	.LC81
# label:
	.word	.LC82
# def:
	.byte	50
	.zero	3
# key:
	.word	.LC83
# label:
	.word	.LC84
# def:
	.byte	50
	.zero	3
# init:
	.word	_ZN12_GLOBAL__N_14initEii
# frame:
	.word	_ZN12_GLOBAL__N_15frameEjiiPKh
# band:
	.word	_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.local	_ZN12_GLOBAL__N_15g_tMsE
	.comm	_ZN12_GLOBAL__N_15g_tMsE,4,4
	.data
	.align	4
	.type	_ZN12_GLOBAL__N_16g_glowE, @object
	.size	_ZN12_GLOBAL__N_16g_glowE, 4
_ZN12_GLOBAL__N_16g_glowE:
	.word	1065353216
	.local	_ZN12_GLOBAL__N_15g_icyE
	.comm	_ZN12_GLOBAL__N_15g_icyE,16,4
	.local	_ZN12_GLOBAL__N_15g_icxE
	.comm	_ZN12_GLOBAL__N_15g_icxE,16,4
	.local	_ZN12_GLOBAL__N_15g_ampE
	.comm	_ZN12_GLOBAL__N_15g_ampE,16,4
	.local	_ZN12_GLOBAL__N_13g_rE
	.comm	_ZN12_GLOBAL__N_13g_rE,16,4
	.local	_ZN12_GLOBAL__N_14g_cyE
	.comm	_ZN12_GLOBAL__N_14g_cyE,16,4
	.local	_ZN12_GLOBAL__N_14g_cxE
	.comm	_ZN12_GLOBAL__N_14g_cxE,16,4
	.local	_ZN12_GLOBAL__N_13g_nE
	.comm	_ZN12_GLOBAL__N_13g_nE,4,4
	.align	4
	.type	_ZN12_GLOBAL__N_17troughFE, @object
	.size	_ZN12_GLOBAL__N_17troughFE, 12
_ZN12_GLOBAL__N_17troughFE:
	.word	1088421888
	.word	1094713344
	.word	1097859072
	.align	4
	.type	_ZN12_GLOBAL__N_16crestFE, @object
	.size	_ZN12_GLOBAL__N_16crestFE, 12
_ZN12_GLOBAL__N_16crestFE:
	.word	1115160576
	.word	1120141312
	.word	1123942400
	.align	4
	.type	_ZN12_GLOBAL__N_112lastThemeGenE, @object
	.size	_ZN12_GLOBAL__N_112lastThemeGenE, 4
_ZN12_GLOBAL__N_112lastThemeGenE:
	.word	-1
	.local	_ZN12_GLOBAL__N_16initedE
	.comm	_ZN12_GLOBAL__N_16initedE,1,1
	.local	_ZN12_GLOBAL__N_110g_cosTableE
	.comm	_ZN12_GLOBAL__N_110g_cosTableE,4,4
	.local	_ZN12_GLOBAL__N_16envLUTE
	.comm	_ZN12_GLOBAL__N_16envLUTE,4,4
	.align	4
	.type	_ZN12_GLOBAL__N_13rngE, @object
	.size	_ZN12_GLOBAL__N_13rngE, 4
_ZN12_GLOBAL__N_13rngE:
	.word	12648430
	.local	_ZN12_GLOBAL__N_110nextDropMsE
	.comm	_ZN12_GLOBAL__N_110nextDropMsE,4,4
	.local	_ZN12_GLOBAL__N_17ripplesE
	.comm	_ZN12_GLOBAL__N_17ripplesE,64,4
	.ident	"GCC: (crosstool-NG esp-2021r2-patch5) 8.4.0"
