	.file	"AnimAurora.cpp"
# GNU C++17 (crosstool-NG esp-2021r2-patch5) version 8.4.0 (xtensa-esp32s3-elf)
#	compiled by GNU C version 6.3.0 20170516, GMP version 6.2.1, MPFR version 4.1.0, MPC version 1.2.1, isl version isl-0.19-GMP

# GGC heuristics: --param ggc-min-expand=100 --param ggc-min-heapsize=131072
# options passed:  -I shim -imultilib no-rtti
# -iprefix c:\users\oystein\.platformio\packages\toolchain-xtensa-esp32s3\bin\../lib/gcc/xtensa-esp32s3-elf/8.4.0/
# ../../src/display/ui/default/bganim/AnimAurora.cpp -mlongcalls
# -auxbase-strip xtensa-asm/AnimAurora.S -O2 -std=gnu++17 -fno-exceptions
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
	.global	__fixsfdi
	.section	.text._ZN12_GLOBAL__N_14bandEPtiiijPKh,"ax",@progbits
	.literal_position
	.literal .LC0, _ZN12_GLOBAL__N_13g_tE
	.literal .LC1, _ZN12_GLOBAL__N_15wLut1E
	.literal .LC2, _ZN12_GLOBAL__N_15wLut2E
	.literal .LC3, _ZN12_GLOBAL__N_15sqLUTE
	.literal .LC4, 0x3f000000
	.literal .LC5, 0x3ee147ae
	.literal .LC6, 0x3cac0831
	.literal .LC7, 0x3fc90fda
	.literal .LC8, 0x4222f983
	.literal .LC9, _ZN12_GLOBAL__N_14g_A1E
	.literal .LC10, 0x3c54fdf4
	.literal .LC11, 0x3fd9999a
	.literal .LC12, _ZN12_GLOBAL__N_14g_A2E
	.literal .LC13, 0x3b088889
	.literal .LC14, 0x3ea3d70a
	.literal .LC15, 0x3f969696
	.literal .LC16, 0x3f800000
	.literal .LC17, 0x00000000
	.literal .LC18, 0x45800000
	.literal .LC19, _ZN12_GLOBAL__N_19g_inten14E
	.literal .LC20, 0x41200000
	.literal .LC21, _ZN12_GLOBAL__N_17glowLUTE
	.literal .LC22, 0x3df5c28f
	.literal .LC23, 0x4722f983
	.literal .LC24, 0x3d8f5c29
	.literal .LC25, _ZN6bganim6BAYER8E
	.align	4
	.type	_ZN12_GLOBAL__N_14bandEPtiiijPKh, @function
_ZN12_GLOBAL__N_14bandEPtiiijPKh:
	entry	sp, 624	#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:100: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
	s32i	a5, sp, 560	# %sfp, w
# ../../src/display/ui/default/bganim/AnimAurora.cpp:101:     const float t = g_t;
	l32r	a5, .LC0	#, tmp184
# ../../src/display/ui/default/bganim/AnimAurora.cpp:100: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
# ../../src/display/ui/default/bganim/AnimAurora.cpp:101:     const float t = g_t;
	lsi	f0, a5, 0	# g_t,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:106:     const int32_t *__restrict w1 = wLut1;
	l32r	a5, .LC1	#, tmp185
# ../../src/display/ui/default/bganim/AnimAurora.cpp:101:     const float t = g_t;
	ssi	f0, sp, 552	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:106:     const int32_t *__restrict w1 = wLut1;
	l32i.n	a5, a5, 0	# wLut1,
	s32i	a5, sp, 528	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:107:     const int32_t *__restrict w2 = wLut2;
	l32r	a5, .LC2	#, tmp186
	l32i.n	a5, a5, 0	# wLut2,
	s32i	a5, sp, 532	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:108:     const uint16_t *__restrict sq = sqLUT; // sq[v] valid for v in [-V_MAX, V_MAX], 0 for v<0
	l32r	a5, .LC3	#, tmp187
	l32i.n	a5, a5, 0	# sqLUT,
	s32i	a5, sp, 536	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:114:     const float *__restrict ct = cosTableF();
	call8	_ZN6bganim9cosTableFEv		#
	s32i	a10, sp, 556	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:127:     for (int ry = 0; ry < rows; ry++) {
	blti	a4, 1, .L1	# rows,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:129:         const float warp1 = rowSin(y * 0.021f + t * 0.5f) * g_A1;
	l32r	a5, .LC4	#,
	lsi	f1, sp, 552	# %sfp,
	wfr	f2, a5	#,
	mul.s	f1, f1, f2	#,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	l32r	a5, .LC5	#,
	lsi	f0, sp, 552	# %sfp,
	l32i	a9, sp, 560	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:129:         const float warp1 = rowSin(y * 0.021f + t * 0.5f) * g_A1;
	ssi	f1, sp, 568	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	wfr	f1, a5	#,
	mul.s	f0, f0, f1	#,,
	add.n	a4, a4, a3	#, rows, y0
	slli	a9, a9, 1	#,,
	s32i	a4, sp, 576	# %sfp,
	ssi	f0, sp, 572	# %sfp,
	s32i	a3, sp, 540	# %sfp, y0
	s32i	a9, sp, 580	# %sfp,
	s32i	a2, sp, 548	# %sfp, dst
# ../../src/display/ui/default/bganim/AnimAurora.cpp:125:     int lastBgIdx = -1;
	movi.n	a5, -1	# lastBgIdx,
	movi.n	a4, 0	# _342,
.L8:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:129:         const float warp1 = rowSin(y * 0.021f + t * 0.5f) * g_A1;
	l32i	a2, sp, 540	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:129:         const float warp1 = rowSin(y * 0.021f + t * 0.5f) * g_A1;
	l32r	a3, .LC9	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:129:         const float warp1 = rowSin(y * 0.021f + t * 0.5f) * g_A1;
	float.s	f2, a2, 0	# _1,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	l32r	a2, .LC10	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:129:         const float warp1 = rowSin(y * 0.021f + t * 0.5f) * g_A1;
	lsi	f5, a3, 0	# g_A1, g_A1
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	wfr	f1, a2	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:129:         const float warp1 = rowSin(y * 0.021f + t * 0.5f) * g_A1;
	l32r	a2, .LC6	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	mul.s	f0, f2, f1	# tmp203, _1,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:129:         const float warp1 = rowSin(y * 0.021f + t * 0.5f) * g_A1;
	wfr	f3, a2	#,
	lsi	f1, sp, 568	# %sfp, tmp190
# ../../src/display/ui/default/bganim/AnimAurora.cpp:131:         const float yn = y * (1.0f / 480.0f); // was a divide (__divsf3 libcall on device)
	l32r	a2, .LC13	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:129:         const float warp1 = rowSin(y * 0.021f + t * 0.5f) * g_A1;
	madd.s	f1, f2, f3	# tmp190, _1,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:131:         const float yn = y * (1.0f / 480.0f); // was a divide (__divsf3 libcall on device)
	wfr	f3, a2	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:116:     auto rowSin = [&rowCos](float rad) { return rowCos(rad - 1.5707963f); };
	l32r	a2, .LC7	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:131:         const float yn = y * (1.0f / 480.0f); // was a divide (__divsf3 libcall on device)
	mul.s	f2, f2, f3	# yn, _1,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:116:     auto rowSin = [&rowCos](float rad) { return rowCos(rad - 1.5707963f); };
	wfr	f6, a2	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	lsi	f3, sp, 572	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:132:         float env = 1.0f - fabsf(yn - 0.32f) * (1.0f / 0.85f);
	l32r	a2, .LC14	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	sub.s	f0, f0, f3	# tmp205, tmp203,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:116:     auto rowSin = [&rowCos](float rad) { return rowCos(rad - 1.5707963f); };
	sub.s	f1, f1, f6	# tmp192, tmp190,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:132:         float env = 1.0f - fabsf(yn - 0.32f) * (1.0f / 0.85f);
	wfr	f6, a2	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	l32r	a2, .LC11	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:132:         float env = 1.0f - fabsf(yn - 0.32f) * (1.0f / 0.85f);
	sub.s	f3, f2, f6	# tmp220, yn,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	wfr	f6, a2	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:115:     auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
	l32r	a2, .LC8	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	add.s	f0, f0, f6	# tmp206, tmp205,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:115:     auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
	wfr	f6, a2	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:132:         float env = 1.0f - fabsf(yn - 0.32f) * (1.0f / 0.85f);
	l32r	a2, .LC16	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:115:     auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
	mul.s	f1, f1, f6	# tmp194, tmp192,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:132:         float env = 1.0f - fabsf(yn - 0.32f) * (1.0f / 0.85f);
	wfr	f6, a2	#,
	l32r	a2, .LC15	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:132:         float env = 1.0f - fabsf(yn - 0.32f) * (1.0f / 0.85f);
	abs.s	f3, f3	# tmp222, tmp220
# ../../src/display/ui/default/bganim/AnimAurora.cpp:132:         float env = 1.0f - fabsf(yn - 0.32f) * (1.0f / 0.85f);
	wfr	f7, a2	#,
	msub.s	f6, f3, f7	#, tmp222,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:116:     auto rowSin = [&rowCos](float rad) { return rowCos(rad - 1.5707963f); };
	l32r	a2, .LC7	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	l32r	a3, .LC12	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:115:     auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
	l32i	a9, sp, 556	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:132:         float env = 1.0f - fabsf(yn - 0.32f) * (1.0f / 0.85f);
	mov.s	f3, f6	# env,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:116:     auto rowSin = [&rowCos](float rad) { return rowCos(rad - 1.5707963f); };
	wfr	f6, a2	#,
	sub.s	f0, f0, f6	# tmp208, tmp206,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	lsi	f4, a3, 0	# g_A2, g_A2
# ../../src/display/ui/default/bganim/AnimAurora.cpp:115:     auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
	l32r	a3, .LC8	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:115:     auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
	trunc.s	a2, f1, 0	# tmp196, tmp194
# ../../src/display/ui/default/bganim/AnimAurora.cpp:115:     auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
	wfr	f1, a3	#,
	mul.s	f0, f0, f1	# tmp210, tmp208,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:115:     auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
	extui	a2, a2, 0, 8	# tmp197, tmp196,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:115:     auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
	slli	a2, a2, 2	# tmp198, tmp197,
	add.n	a2, a9, a2	# tmp199,, tmp198
# ../../src/display/ui/default/bganim/AnimAurora.cpp:129:         const float warp1 = rowSin(y * 0.021f + t * 0.5f) * g_A1;
	lsi	f1, a2, 0	# *_159, *_159
# ../../src/display/ui/default/bganim/AnimAurora.cpp:115:     auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
	trunc.s	a2, f0, 0	# tmp212, tmp210
# ../../src/display/ui/default/bganim/AnimAurora.cpp:129:         const float warp1 = rowSin(y * 0.021f + t * 0.5f) * g_A1;
	mul.s	f0, f1, f5	# warp1, *_159, g_A1
# ../../src/display/ui/default/bganim/AnimAurora.cpp:115:     auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
	extui	a2, a2, 0, 8	# tmp213, tmp212,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:115:     auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
	slli	a2, a2, 2	# tmp214, tmp213,
	add.n	a2, a9, a2	# tmp215,, tmp214
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	lsi	f1, a2, 0	# *_150, *_150
# ../../src/display/ui/default/bganim/AnimAurora.cpp:133:         env = env < 0 ? 0 : env * env;
	l32r	a2, .LC17	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	mul.s	f4, f1, f4	#, *_150, g_A2
# ../../src/display/ui/default/bganim/AnimAurora.cpp:133:         env = env < 0 ? 0 : env * env;
	wfr	f1, a2	#,
	olt.s	b0, f3, f1	#, env,
	mov.n	a12, a4	# _342, _342
# ../../src/display/ui/default/bganim/AnimAurora.cpp:130:         const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
	ssi	f4, sp, 564	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:133:         env = env < 0 ? 0 : env * env;
	bt	b0, .L3	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:133:         env = env < 0 ? 0 : env * env;
	mul.s	f3, f3, f3	# tmp226, env, env
	l32r	a2, .LC18	#,
	wfr	f1, a2	#,
	mul.s	f3, f3, f1	# tmp227, tmp226,
	trunc.s	a12, f3, 0	# _342, tmp227
.L3:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:139:         const int bgIdx = static_cast<int>(yn * 10.0f); // sky sits in the darkest ~4% of the theme
	l32r	a2, .LC20	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:137:         const int32_t rowScale = (envQ12 * g_inten14) >> 12;
	l32r	a3, .LC19	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:139:         const int bgIdx = static_cast<int>(yn * 10.0f); // sky sits in the darkest ~4% of the theme
	wfr	f1, a2	#,
	mul.s	f2, f2, f1	# tmp232, yn,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:137:         const int32_t rowScale = (envQ12 * g_inten14) >> 12;
	l32i.n	a2, a3, 0	# g_inten14, g_inten14
	mull	a12, a12, a2	# tmp230, _342, g_inten14
# ../../src/display/ui/default/bganim/AnimAurora.cpp:139:         const int bgIdx = static_cast<int>(yn * 10.0f); // sky sits in the darkest ~4% of the theme
	trunc.s	a9, f2, 0	#, tmp232
# ../../src/display/ui/default/bganim/AnimAurora.cpp:137:         const int32_t rowScale = (envQ12 * g_inten14) >> 12;
	srai	a12, a12, 12	# rowScale, tmp230,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:139:         const int bgIdx = static_cast<int>(yn * 10.0f); // sky sits in the darkest ~4% of the theme
	s32i	a9, sp, 544	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:140:         if (bgIdx != lastBgIdx) {
	beq	a9, a5, .L4	#, lastBgIdx,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:142:             themeRGB(bgIdx, bg);
	mov.n	a10, a9	#,
	addmi	a11, sp, 0x200	#,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:143:             const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;
	movi	a3, 0x201	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:143:             const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;
	movi	a5, 0x202	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:142:             themeRGB(bgIdx, bg);
	s32i	a12, sp, 588	#,
	ssi	f0, sp, 584	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:143:             const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;
	addmi	a2, sp, 0x200	#,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:142:             themeRGB(bgIdx, bg);
	call8	_ZN6bganim8themeRGBEiPh		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:143:             const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;
	add.n	a3, a3, sp	#,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:143:             const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;
	add.n	a5, a5, sp	#,,
	l32r	a9, .LC21	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:143:             const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;
	l8ui	a13, a2, 0	# bg, bg
# ../../src/display/ui/default/bganim/AnimAurora.cpp:143:             const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;
	l8ui	a11, a3, 0	# bg, bg
# ../../src/display/ui/default/bganim/AnimAurora.cpp:143:             const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;
	l8ui	a10, a5, 0	# bg, bg
# ../../src/display/ui/default/bganim/AnimAurora.cpp:158:                 rowLUT[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
	lsi	f0, sp, 584	#,
	l32i	a12, sp, 588	#,
	l32i.n	a8, a9, 0	# glowLUT, ivtmp$53
# ../../src/display/ui/default/bganim/AnimAurora.cpp:143:             const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;
	srai	a13, a13, 3	# bgR5, bg,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:143:             const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;
	srai	a11, a11, 2	# bgG6, bg,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:143:             const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;
	srai	a10, a10, 3	# bgB5, bg,
	mov.n	a7, sp	# ivtmp$54,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:158:                 rowLUT[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
	movi.n	a9, 0x1f	# tmp249,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:158:                 rowLUT[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
	movi.n	a14, 0x3f	# tmp257,
	movi	a6, 0x100	# tmp330,
	loop	a6, .L5_LEND	# tmp330,
.L5:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:145:                 const uint16_t glow = glowLUT[i];
	l16ui	a5, a8, 0	# MEM[base: _322, offset: 0B], glow
	addi.n	a8, a8, 2	# ivtmp$53, ivtmp$53,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:146:                 int r = bgR5 + ((glow >> 11) & 0x1F);
	srli	a3, a5, 11	# tmp243, glow,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:146:                 int r = bgR5 + ((glow >> 11) & 0x1F);
	add.n	a3, a3, a13	# r, tmp243, bgR5
# ../../src/display/ui/default/bganim/AnimAurora.cpp:147:                 int g = bgG6 + ((glow >> 5) & 0x3F);
	extui	a15, a5, 5, 6	# tmp245, glow,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:148:                 int b = bgB5 + (glow & 0x1F);
	extui	a5, a5, 0, 5	# tmp247, glow,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:158:                 rowLUT[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
	min	a2, a3, a9	# r, r, tmp249
# ../../src/display/ui/default/bganim/AnimAurora.cpp:147:                 int g = bgG6 + ((glow >> 5) & 0x3F);
	add.n	a15, a15, a11	# g, tmp245, bgG6
# ../../src/display/ui/default/bganim/AnimAurora.cpp:148:                 int b = bgB5 + (glow & 0x1F);
	add.n	a5, a5, a10	# b, tmp247, bgB5
# ../../src/display/ui/default/bganim/AnimAurora.cpp:158:                 rowLUT[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
	slli	a2, a2, 11	# tmp250, r,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:158:                 rowLUT[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
	min	a5, a5, a9	# b, b, tmp249
# ../../src/display/ui/default/bganim/AnimAurora.cpp:158:                 rowLUT[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
	min	a3, a15, a14	# g, g, tmp257
# ../../src/display/ui/default/bganim/AnimAurora.cpp:158:                 rowLUT[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
	or	a2, a2, a5	# tmp255, tmp250, b
# ../../src/display/ui/default/bganim/AnimAurora.cpp:158:                 rowLUT[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
	slli	a3, a3, 5	# tmp258, g,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:158:                 rowLUT[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
	or	a2, a2, a3	# tmp261, tmp255, tmp258
# ../../src/display/ui/default/bganim/AnimAurora.cpp:158:                 rowLUT[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
	s16i	a2, a7, 0	# MEM[base: _321, offset: 0B], tmp261
	addi.n	a7, a7, 2	# ivtmp$54, ivtmp$54,
	.L5_LEND:		#
.L4:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:164:         uint32_t ph1 = static_cast<uint32_t>(static_cast<int64_t>((warp1 + t * 0.12f) * TICKS));
	l32r	a2, .LC22	#,
	lsi	f1, sp, 552	# %sfp,
	wfr	f2, a2	#,
	madd.s	f0, f1, f2	# tmp262,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:164:         uint32_t ph1 = static_cast<uint32_t>(static_cast<int64_t>((warp1 + t * 0.12f) * TICKS));
	l32r	a2, .LC23	#,
	s32i	a12, sp, 588	#,
	wfr	f1, a2	#,
	mul.s	f0, f0, f1	#, tmp262,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:170:         for (int x = 0; x < w; x++) {
	movi.n	a6, 0	# x,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:164:         uint32_t ph1 = static_cast<uint32_t>(static_cast<int64_t>((warp1 + t * 0.12f) * TICKS));
	rfr	a10, f0	#,
	call8	__fixsfdi		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:165:         uint32_t ph2 = static_cast<uint32_t>(static_cast<int64_t>((warp2 + t * 0.07f) * TICKS));
	l32r	a2, .LC24	#,
	lsi	f1, sp, 552	# %sfp,
	lsi	f0, sp, 564	# %sfp, tmp267
	wfr	f2, a2	#,
	madd.s	f0, f1, f2	# tmp267,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:165:         uint32_t ph2 = static_cast<uint32_t>(static_cast<int64_t>((warp2 + t * 0.07f) * TICKS));
	l32r	a2, .LC23	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:164:         uint32_t ph1 = static_cast<uint32_t>(static_cast<int64_t>((warp1 + t * 0.12f) * TICKS));
	mov.n	a5, a10	# ph1,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:165:         uint32_t ph2 = static_cast<uint32_t>(static_cast<int64_t>((warp2 + t * 0.07f) * TICKS));
	wfr	f1, a2	#,
	mul.s	f0, f0, f1	#, tmp267,
	rfr	a10, f0	#,
	call8	__fixsfdi		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:166:         const int dbase = (y & 7) * 8; // row-constant Bayer row offset
	l32i	a3, sp, 540	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:170:         for (int x = 0; x < w; x++) {
	l32i	a9, sp, 560	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:166:         const int dbase = (y & 7) * 8; // row-constant Bayer row offset
	extui	a11, a3, 0, 3	# tmp272,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:166:         const int dbase = (y & 7) * 8; // row-constant Bayer row offset
	slli	a2, a11, 3	# _38, tmp272,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:170:         for (int x = 0; x < w; x++) {
	l32i	a7, sp, 548	# %sfp, ivtmp$46
	l32i	a12, sp, 588	#,
	blti	a9, 1, .L9	#,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:177:             inten += (bayerRow[x & 7] - 32) >> 2;      // ordered dither
	l32r	a11, .LC25	#, tmp311
# ../../src/display/ui/default/bganim/AnimAurora.cpp:174:             ph1 += STEP1;
	movi	a15, 0x43c	# tmp284,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:175:             ph2 += STEP2;
	movi	a14, 0x2c5	# tmp285,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:177:             inten += (bayerRow[x & 7] - 32) >> 2;      // ordered dither
	add.n	a11, a11, a2	# tmp292, tmp311, _38
	movi	a13, 0xff	# tmp297,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:183:             row[x] = rowLUT[inten];
	mov.n	a8, a9	# w,
	j	.L6		#
.L9:
	l32i	a2, sp, 540	# %sfp,
	l32i	a3, sp, 548	# %sfp,
	l32i	a5, sp, 580	# %sfp,
	addi.n	a2, a2, 1	#,,
	add.n	a3, a3, a5	#,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:127:     for (int ry = 0; ry < rows; ry++) {
	l32i	a9, sp, 576	# %sfp,
	s32i	a2, sp, 540	# %sfp,
	s32i	a3, sp, 548	# %sfp,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:139:         const int bgIdx = static_cast<int>(yn * 10.0f); // sky sits in the darkest ~4% of the theme
	l32i	a5, sp, 544	# %sfp, lastBgIdx
# ../../src/display/ui/default/bganim/AnimAurora.cpp:127:     for (int ry = 0; ry < rows; ry++) {
	bne	a9, a2, .L8	#,,
	j	.L1		#
.L6:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:173:             const int32_t v = (w1[(ph1 >> 8) & 1023] + w2[(ph2 >> 8) & 1023]) >> 7; // ~±4112
	extui	a2, a5, 8, 10	# tmp274, ph1,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:173:             const int32_t v = (w1[(ph1 >> 8) & 1023] + w2[(ph2 >> 8) & 1023]) >> 7; // ~±4112
	l32i	a9, sp, 528	# %sfp,
	slli	a2, a2, 2	# tmp275, tmp274,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:173:             const int32_t v = (w1[(ph1 >> 8) & 1023] + w2[(ph2 >> 8) & 1023]) >> 7; // ~±4112
	extui	a3, a10, 8, 10	# tmp278, ph2,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:173:             const int32_t v = (w1[(ph1 >> 8) & 1023] + w2[(ph2 >> 8) & 1023]) >> 7; // ~±4112
	add.n	a2, a9, a2	# tmp276,, tmp275
# ../../src/display/ui/default/bganim/AnimAurora.cpp:173:             const int32_t v = (w1[(ph1 >> 8) & 1023] + w2[(ph2 >> 8) & 1023]) >> 7; // ~±4112
	l32i	a9, sp, 532	# %sfp,
	slli	a3, a3, 2	# tmp279, tmp278,
	add.n	a3, a9, a3	# tmp280,, tmp279
# ../../src/display/ui/default/bganim/AnimAurora.cpp:173:             const int32_t v = (w1[(ph1 >> 8) & 1023] + w2[(ph2 >> 8) & 1023]) >> 7; // ~±4112
	l32i.n	a9, a3, 0	# *_62, *_62
	l32i.n	a2, a2, 0	# *_57, *_57
# ../../src/display/ui/default/bganim/AnimAurora.cpp:177:             inten += (bayerRow[x & 7] - 32) >> 2;      // ordered dither
	extui	a3, a6, 0, 3	# tmp290, x,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:173:             const int32_t v = (w1[(ph1 >> 8) & 1023] + w2[(ph2 >> 8) & 1023]) >> 7; // ~±4112
	add.n	a2, a2, a9	# tmp281, *_57, *_62
# ../../src/display/ui/default/bganim/AnimAurora.cpp:173:             const int32_t v = (w1[(ph1 >> 8) & 1023] + w2[(ph2 >> 8) & 1023]) >> 7; // ~±4112
	srai	a2, a2, 7	# v, tmp281,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:176:             int32_t inten = (sq[v] * rowScale) >> 12; // branchless clip+square via signed-indexed LUT
	l32i	a9, sp, 536	# %sfp,
	slli	a2, a2, 1	# tmp286, v,
	add.n	a2, a9, a2	# tmp287,, tmp286
	l16ui	a2, a2, 0	# *_67, *_67
# ../../src/display/ui/default/bganim/AnimAurora.cpp:177:             inten += (bayerRow[x & 7] - 32) >> 2;      // ordered dither
	add.n	a3, a11, a3	# tmp293, tmp292, tmp290
	l8ui	a3, a3, 0	# *_73, *_73
# ../../src/display/ui/default/bganim/AnimAurora.cpp:176:             int32_t inten = (sq[v] * rowScale) >> 12; // branchless clip+square via signed-indexed LUT
	mull	a2, a2, a12	# tmp289, *_67, rowScale
# ../../src/display/ui/default/bganim/AnimAurora.cpp:177:             inten += (bayerRow[x & 7] - 32) >> 2;      // ordered dither
	addi	a3, a3, -32	# tmp295, *_73,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:176:             int32_t inten = (sq[v] * rowScale) >> 12; // branchless clip+square via signed-indexed LUT
	srai	a2, a2, 12	# inten, tmp289,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:177:             inten += (bayerRow[x & 7] - 32) >> 2;      // ordered dither
	srai	a3, a3, 2	# tmp296, tmp295,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:177:             inten += (bayerRow[x & 7] - 32) >> 2;      // ordered dither
	add.n	a2, a3, a2	# _103, tmp296, inten
	min	a2, a2, a13	# _103, _103, tmp297
# ../../src/display/ui/default/bganim/AnimAurora.cpp:183:             row[x] = rowLUT[inten];
	max	a2, a2, a4	# inten, _103, _342
	slli	a2, a2, 1	# tmp300, inten,
	add.n	a2, sp, a2	# tmp301,, tmp300
# ../../src/display/ui/default/bganim/AnimAurora.cpp:183:             row[x] = rowLUT[inten];
	l16ui	a2, a2, 0	# rowLUT,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:174:             ph1 += STEP1;
	add.n	a5, a5, a15	# ph1, ph1, tmp284
# ../../src/display/ui/default/bganim/AnimAurora.cpp:183:             row[x] = rowLUT[inten];
	s16i	a2, a7, 0	# MEM[base: _329, offset: 0B], tmp302
# ../../src/display/ui/default/bganim/AnimAurora.cpp:175:             ph2 += STEP2;
	add.n	a10, a10, a14	# ph2, ph2, tmp285
# ../../src/display/ui/default/bganim/AnimAurora.cpp:170:         for (int x = 0; x < w; x++) {
	addi.n	a6, a6, 1	# x, x,
	addi.n	a7, a7, 2	# ivtmp$46, ivtmp$46,
	addi.n	a8, a8, -1	# w, w,
	bnez.n	a8, .L6	# w,
	j	.L9		#
.L1:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:186: }
	retw.n
	.size	_ZN12_GLOBAL__N_14bandEPtiiijPKh, .-_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.global	__divsf3
	.section	.text._ZN12_GLOBAL__N_112buildGlowLUTEv,"ax",@progbits
	.literal_position
	.literal .LC26, 0x437f0000
	.literal .LC27, 0x400ccccd
	.literal .LC28, 0x3f800000
	.literal .LC29, 0x00000000
	.literal .LC30, _ZN12_GLOBAL__N_17glowLUTE
	.align	4
	.type	_ZN12_GLOBAL__N_112buildGlowLUTEv, @function
_ZN12_GLOBAL__N_112buildGlowLUTEv:
	entry	sp, 64	#
	l32r	a2, .LC29	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:47: void buildGlowLUT() {
	movi.n	a6, 0	# ivtmp$65,
	l32r	a4, .LC28	#, tmp133
	l32r	a3, .LC30	#, tmp135
	wfr	f2, a2	# tmp134,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	movi	a5, -0x800	# prephitmp_80,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:48:     for (int i = 0; i < 256; i++) {
	mov.n	a7, a6	# i, ivtmp$65
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.n	a2, a6	# prephitmp_80, ivtmp$65
.L24:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:50:         themeRGB(40 + ((i * 215) >> 8), c);
	srai	a10, a6, 8	# tmp82, ivtmp$65,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:50:         themeRGB(40 + ((i * 215) >> 8), c);
	mov.n	a11, sp	#,
	addi	a10, a10, 40	#, tmp82,
	ssi	f2, sp, 16	#,
	call8	_ZN6bganim8themeRGBEiPh		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:51:         const float scale = fminf(1.0f, (i / 255.0f) * 2.2f);
	float.s	f0, a7, 0	#, i
	l32r	a11, .LC26	#,
	rfr	a10, f0	#,
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:51:         const float scale = fminf(1.0f, (i / 255.0f) * 2.2f);
	l32r	a8, .LC27	#,
	wfr	f1, a10	#,
	wfr	f3, a8	#,
	mul.s	f1, f1, f3	#,,
	mov.n	a10, a4	#, tmp133
	rfr	a11, f1	#,
	call8	fminf		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:52:         glowLUT[i] = rgb565(clamp8f(c[0] * scale), clamp8f(c[1] * scale), clamp8f(c[2] * scale));
	l8ui	a8, sp, 0	# c, c
# ../../src/display/ui/default/bganim/AnimAurora.cpp:51:         const float scale = fminf(1.0f, (i / 255.0f) * 2.2f);
	wfr	f1, a10	# scale,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:52:         glowLUT[i] = rgb565(clamp8f(c[0] * scale), clamp8f(c[1] * scale), clamp8f(c[2] * scale));
	float.s	f0, a8, 0	# tmp90, c
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	lsi	f2, sp, 16	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:52:         glowLUT[i] = rgb565(clamp8f(c[0] * scale), clamp8f(c[1] * scale), clamp8f(c[2] * scale));
	mul.s	f0, f0, f1	# _10, tmp90, scale
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	mov.n	a10, a2	# prephitmp_80, prephitmp_80
	olt.s	b0, f0, f2	#, _10, tmp134
	bt	b0, .L17	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	l32r	a8, .LC26	#,
	mov.n	a10, a5	# prephitmp_80, prephitmp_80
	wfr	f3, a8	#,
	olt.s	b0, f3, f0	#,, _10
	bt	b0, .L17	#,
	utrunc.s	a8, f0, 0	# tmp94, _10
	movi	a9, -0x800	#,
	extui	a8, a8, 0, 8	# tmp95, tmp94
	slli	a8, a8, 8	# tmp96, tmp95,
	and	a8, a8, a9	# tmp99, tmp96,
	sext	a10, a8, 15	# prephitmp_80, tmp99
.L17:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:52:         glowLUT[i] = rgb565(clamp8f(c[0] * scale), clamp8f(c[1] * scale), clamp8f(c[2] * scale));
	l8ui	a8, sp, 1	# c, c
# ../../src/display/ui/default/bganim/AnimAurora.cpp:52:         glowLUT[i] = rgb565(clamp8f(c[0] * scale), clamp8f(c[1] * scale), clamp8f(c[2] * scale));
	float.s	f0, a8, 0	# tmp101, c
	mul.s	f0, f0, f1	# _15, tmp101, scale
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f0, f2	#, _15, tmp134
	bt	b0, .L18	#,
	l32r	a8, .LC26	#,
	wfr	f3, a8	#,
	olt.s	b0, f3, f0	#,, _15
	bf	b0, .L30	#,
	movi	a8, 0x7e0	# tmp105,
	or	a10, a10, a8	# prephitmp_80, prephitmp_80, tmp105
	j	.L18		#
.L30:
	utrunc.s	a8, f0, 0	# tmp107, _15
	movi	a11, 0x7e0	# tmp110,
	extui	a8, a8, 0, 8	# tmp108, tmp107
	slli	a8, a8, 3	# tmp109, tmp108,
	and	a8, a8, a11	# tmp112, tmp109, tmp110
	or	a10, a10, a8	# prephitmp_80, prephitmp_80, tmp112
.L18:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:52:         glowLUT[i] = rgb565(clamp8f(c[0] * scale), clamp8f(c[1] * scale), clamp8f(c[2] * scale));
	l8ui	a8, sp, 2	# c, c
# ../../src/display/ui/default/bganim/AnimAurora.cpp:52:         glowLUT[i] = rgb565(clamp8f(c[0] * scale), clamp8f(c[1] * scale), clamp8f(c[2] * scale));
	float.s	f0, a8, 0	# tmp116, c
	mul.s	f0, f0, f1	# _20, tmp116, scale
# ../../src/display/ui/default/bganim/BgAnimCommon.h:52: inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }
	olt.s	b0, f0, f2	#, _20, tmp134
	bt	b0, .L21	#,
	l32r	a8, .LC26	#,
	wfr	f1, a8	#,
	olt.s	b0, f1, f0	#,, _20
	bf	b0, .L31	#,
	movi.n	a8, 0x1f	# tmp120,
	or	a10, a10, a8	# prephitmp_80, prephitmp_80, tmp120
	j	.L21		#
.L31:
	utrunc.s	a8, f0, 0	# tmp122, _20
	extui	a8, a8, 3, 5	# tmp124, tmp122,,
	or	a10, a10, a8	# prephitmp_80, prephitmp_80, tmp124
.L21:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:52:         glowLUT[i] = rgb565(clamp8f(c[0] * scale), clamp8f(c[1] * scale), clamp8f(c[2] * scale));
	l32i.n	a8, a3, 0	# glowLUT, glowLUT
	slli	a11, a7, 1	# tmp129, i,
	add.n	a8, a8, a11	# tmp130, glowLUT, tmp129
	s16i	a10, a8, 0	# *_25, prephitmp_80
# ../../src/display/ui/default/bganim/AnimAurora.cpp:48:     for (int i = 0; i < 256; i++) {
	addi.n	a7, a7, 1	# i, i,
	movi	a8, 0xd7	# tmp131,
	add.n	a6, a6, a8	# ivtmp$65, ivtmp$65, tmp131
# ../../src/display/ui/default/bganim/AnimAurora.cpp:48:     for (int i = 0; i < 256; i++) {
	bnei	a7, 256, .L24	# i,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:54: }
	retw.n
	.size	_ZN12_GLOBAL__N_112buildGlowLUTEv, .-_ZN12_GLOBAL__N_112buildGlowLUTEv
	.section	.text._ZN12_GLOBAL__N_15frameEjiiPKh,"ax",@progbits
	.literal_position
	.literal .LC31, _ZN12_GLOBAL__N_112lastThemeGenE
	.literal .LC32, 0x3d610e10
	.literal .LC33, _ZN12_GLOBAL__N_13g_tE
	.literal .LC34, 0x3a83126f
	.literal .LC35, 0x3ee66666
	.literal .LC36, 0x42c80000
	.literal .LC37, _ZN12_GLOBAL__N_14g_A1E
	.literal .LC38, 0x4019999a
	.literal .LC39, 0x3f19999a
	.literal .LC40, _ZN12_GLOBAL__N_14g_A2E
	.literal .LC41, 0x3fcccccd
	.literal .LC42, 0x3ecccccd
	.literal .LC43, _ZN12_GLOBAL__N_19g_inten14E
	.literal .LC44, 0x3fb33333
	.literal .LC45, 0x43800000
	.align	4
	.type	_ZN12_GLOBAL__N_15frameEjiiPKh, @function
_ZN12_GLOBAL__N_15frameEjiiPKh:
	entry	sp, 32	#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:90:     if (themeGen() != lastThemeGen) {
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:90:     if (themeGen() != lastThemeGen) {
	l32r	a3, .LC31	#, tmp72
# ../../src/display/ui/default/bganim/AnimAurora.cpp:90:     if (themeGen() != lastThemeGen) {
	l32i.n	a8, a3, 0	# lastThemeGen, lastThemeGen
	beq	a10, a8, .L33	#, lastThemeGen,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:91:         buildGlowLUT();
	call8	_ZN12_GLOBAL__N_112buildGlowLUTEv		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:92:         lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:92:         lastThemeGen = themeGen();
	s32i.n	a10, a3, 0	# lastThemeGen,
.L33:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:94:     g_t = (tMs * 0.001f) * 0.45f * speedMul(p[0]);
	l8ui	a8, a5, 0	# *p_32(D), *p_32(D)
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	l32r	a3, .LC32	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	addi	a8, a8, -50	# tmp76, *p_32(D),
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	float.s	f0, a8, 0	# tmp77, tmp76
	wfr	f1, a3	#,
	mul.s	f0, f0, f1	#, tmp77,
	rfr	a10, f0	#,
	call8	exp2f		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:94:     g_t = (tMs * 0.001f) * 0.45f * speedMul(p[0]);
	ufloat.s	f0, a2, 0	# tmp81, tMs
	l32r	a2, .LC34	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:94:     g_t = (tMs * 0.001f) * 0.45f * speedMul(p[0]);
	l32r	a3, .LC35	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:94:     g_t = (tMs * 0.001f) * 0.45f * speedMul(p[0]);
	wfr	f1, a2	#,
	mul.s	f0, f0, f1	# tmp82, tmp81,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:94:     g_t = (tMs * 0.001f) * 0.45f * speedMul(p[0]);
	wfr	f1, a3	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:95:     g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
	l8ui	a2, a5, 2	# MEM[(const uint8_t *)p_32(D) + 2B], MEM[(const uint8_t *)p_32(D) + 2B]
# ../../src/display/ui/default/bganim/AnimAurora.cpp:95:     g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
	l32r	a11, .LC36	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:94:     g_t = (tMs * 0.001f) * 0.45f * speedMul(p[0]);
	mul.s	f0, f0, f1	# tmp84, tmp82,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:94:     g_t = (tMs * 0.001f) * 0.45f * speedMul(p[0]);
	wfr	f1, a10	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:95:     g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
	float.s	f2, a2, 0	#, MEM[(const uint8_t *)p_32(D) + 2B]
# ../../src/display/ui/default/bganim/AnimAurora.cpp:94:     g_t = (tMs * 0.001f) * 0.45f * speedMul(p[0]);
	l32r	a2, .LC33	#, tmp80
# ../../src/display/ui/default/bganim/AnimAurora.cpp:94:     g_t = (tMs * 0.001f) * 0.45f * speedMul(p[0]);
	mul.s	f0, f0, f1	# tmp86, tmp84,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:95:     g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
	rfr	a10, f2	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:94:     g_t = (tMs * 0.001f) * 0.45f * speedMul(p[0]);
	ssi	f0, a2, 0	# g_t, tmp86
# ../../src/display/ui/default/bganim/AnimAurora.cpp:95:     g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:95:     g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
	l32r	a2, .LC39	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:95:     g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
	wfr	f0, a10	# tmp89,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:95:     g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
	wfr	f1, a2	# tmp91,
	l32r	a2, .LC38	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:97:     g_inten14 = static_cast<int32_t>((p[1] / 100.0f) * 1.4f * 256.0f);
	l8ui	a10, a5, 1	# MEM[(const uint8_t *)p_32(D) + 1B], MEM[(const uint8_t *)p_32(D) + 1B]
# ../../src/display/ui/default/bganim/AnimAurora.cpp:95:     g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
	wfr	f3, a2	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:96:     g_A2 = 0.4f + (p[2] / 100.0f) * 1.6f;
	l32r	a2, .LC42	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:95:     g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
	madd.s	f1, f0, f3	# tmp91, tmp89,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:96:     g_A2 = 0.4f + (p[2] / 100.0f) * 1.6f;
	wfr	f2, a2	#,
	l32r	a2, .LC41	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:97:     g_inten14 = static_cast<int32_t>((p[1] / 100.0f) * 1.4f * 256.0f);
	l32r	a11, .LC36	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:96:     g_A2 = 0.4f + (p[2] / 100.0f) * 1.6f;
	wfr	f3, a2	#,
	madd.s	f2, f0, f3	#, tmp89,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:95:     g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
	l32r	a2, .LC37	#, tmp90
	ssi	f1, a2, 0	# g_A1, tmp91
# ../../src/display/ui/default/bganim/AnimAurora.cpp:96:     g_A2 = 0.4f + (p[2] / 100.0f) * 1.6f;
	mov.s	f0, f2	# tmp95,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:96:     g_A2 = 0.4f + (p[2] / 100.0f) * 1.6f;
	l32r	a2, .LC40	#, tmp94
# ../../src/display/ui/default/bganim/AnimAurora.cpp:97:     g_inten14 = static_cast<int32_t>((p[1] / 100.0f) * 1.4f * 256.0f);
	float.s	f2, a10, 0	#, MEM[(const uint8_t *)p_32(D) + 1B]
# ../../src/display/ui/default/bganim/AnimAurora.cpp:96:     g_A2 = 0.4f + (p[2] / 100.0f) * 1.6f;
	ssi	f0, a2, 0	# g_A2, tmp95
# ../../src/display/ui/default/bganim/AnimAurora.cpp:97:     g_inten14 = static_cast<int32_t>((p[1] / 100.0f) * 1.4f * 256.0f);
	rfr	a10, f2	#,
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:97:     g_inten14 = static_cast<int32_t>((p[1] / 100.0f) * 1.4f * 256.0f);
	l32r	a2, .LC44	#,
	wfr	f3, a10	#,
	wfr	f1, a2	#,
	mul.s	f0, f3, f1	# tmp102,,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:97:     g_inten14 = static_cast<int32_t>((p[1] / 100.0f) * 1.4f * 256.0f);
	l32r	a2, .LC45	#,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:97:     g_inten14 = static_cast<int32_t>((p[1] / 100.0f) * 1.4f * 256.0f);
	l32r	a3, .LC43	#, tmp98
# ../../src/display/ui/default/bganim/AnimAurora.cpp:97:     g_inten14 = static_cast<int32_t>((p[1] / 100.0f) * 1.4f * 256.0f);
	wfr	f1, a2	#,
	mul.s	f0, f0, f1	# tmp104, tmp102,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:97:     g_inten14 = static_cast<int32_t>((p[1] / 100.0f) * 1.4f * 256.0f);
	trunc.s	a2, f0, 0	# tmp106, tmp104
	s32i.n	a2, a3, 0	# g_inten14, tmp106
# ../../src/display/ui/default/bganim/AnimAurora.cpp:98: }
	retw.n
	.size	_ZN12_GLOBAL__N_15frameEjiiPKh, .-_ZN12_GLOBAL__N_15frameEjiiPKh
	.section	.text._ZN12_GLOBAL__N_14initEii,"ax",@progbits
	.literal_position
	.literal .LC46, -4112
	.literal .LC47, _ZN12_GLOBAL__N_17glowLUTE
	.literal .LC48, _ZN12_GLOBAL__N_15wLut1E
	.literal .LC49, 4096
	.literal .LC50, _ZN12_GLOBAL__N_15wLut2E
	.literal .LC51, _ZN12_GLOBAL__N_112sqLUTStorageE
	.literal .LC52, 16450
	.literal .LC53, _ZN12_GLOBAL__N_15sqLUTE
	.literal .LC54, 8224
	.literal .LC55, -8224
	.literal .LC56, 4113
	.literal .LC57, _ZN12_GLOBAL__N_112lastThemeGenE
	.align	4
	.type	_ZN12_GLOBAL__N_14initEii, @function
_ZN12_GLOBAL__N_14initEii:
	entry	sp, 32	#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:57:     const int16_t *lut = sinLut();
	call8	_ZN6bganim6sinLutEv		#
	mov.n	a4, a10	# lut,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:58:     if (lut == nullptr) {
	bnez.n	a10, .L35	# lut,
.L41:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:59:         return false;
	movi.n	a2, 0	# <retval>,
	j	.L36		#
.L35:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:61:     if (glowLUT == nullptr) {
	l32r	a2, .LC47	#, tmp124
# ../../src/display/ui/default/bganim/AnimAurora.cpp:61:     if (glowLUT == nullptr) {
	l32i.n	a3, a2, 0	# glowLUT, glowLUT
	bnez.n	a3, .L37	# glowLUT,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:62:         glowLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
	movi	a10, 0x200	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:62:         glowLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
	s32i.n	a10, a2, 0	# glowLUT,
.L37:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:64:     if (wLut1 == nullptr) {
	l32r	a3, .LC48	#, tmp127
# ../../src/display/ui/default/bganim/AnimAurora.cpp:64:     if (wLut1 == nullptr) {
	l32i.n	a5, a3, 0	# wLut1, wLut1
	bnez.n	a5, .L38	# wLut1,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:65:         wLut1 = static_cast<int32_t *>(alloc(SIN_N * sizeof(int32_t)));
	l32r	a10, .LC49	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:65:         wLut1 = static_cast<int32_t *>(alloc(SIN_N * sizeof(int32_t)));
	s32i.n	a10, a3, 0	# wLut1,
.L38:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:67:     if (wLut2 == nullptr) {
	l32r	a5, .LC50	#, tmp123
# ../../src/display/ui/default/bganim/AnimAurora.cpp:67:     if (wLut2 == nullptr) {
	l32i.n	a6, a5, 0	# wLut2, wLut2
	bnez.n	a6, .L39	# wLut2,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:68:         wLut2 = static_cast<int32_t *>(alloc(SIN_N * sizeof(int32_t)));
	l32r	a10, .LC49	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:68:         wLut2 = static_cast<int32_t *>(alloc(SIN_N * sizeof(int32_t)));
	s32i.n	a10, a5, 0	# wLut2,
.L39:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:70:     if (sqLUTStorage == nullptr) {
	l32r	a6, .LC51	#, tmp125
# ../../src/display/ui/default/bganim/AnimAurora.cpp:70:     if (sqLUTStorage == nullptr) {
	l32i.n	a8, a6, 0	# sqLUTStorage, sqLUTStorage
	bnez.n	a8, .L40	# sqLUTStorage,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:71:         sqLUTStorage = static_cast<uint16_t *>(alloc((2 * V_MAX + 1) * sizeof(uint16_t)));
	l32r	a10, .LC52	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:72:         sqLUT = sqLUTStorage + V_MAX;
	l32r	a8, .LC54	#, tmp95
# ../../src/display/ui/default/bganim/AnimAurora.cpp:72:         sqLUT = sqLUTStorage + V_MAX;
	l32r	a9, .LC53	#, tmp93
# ../../src/display/ui/default/bganim/AnimAurora.cpp:72:         sqLUT = sqLUTStorage + V_MAX;
	add.n	a8, a10, a8	# tmp94, _8, tmp95
# ../../src/display/ui/default/bganim/AnimAurora.cpp:71:         sqLUTStorage = static_cast<uint16_t *>(alloc((2 * V_MAX + 1) * sizeof(uint16_t)));
	s32i.n	a10, a6, 0	# sqLUTStorage, _8
# ../../src/display/ui/default/bganim/AnimAurora.cpp:72:         sqLUT = sqLUTStorage + V_MAX;
	s32i.n	a8, a9, 0	# sqLUT, tmp94
.L40:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:74:     if (glowLUT == nullptr || wLut1 == nullptr || wLut2 == nullptr || sqLUTStorage == nullptr) {
	l32i.n	a2, a2, 0	# glowLUT, glowLUT
	beqz.n	a2, .L41	# glowLUT,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:74:     if (glowLUT == nullptr || wLut1 == nullptr || wLut2 == nullptr || sqLUTStorage == nullptr) {
	l32i.n	a12, a3, 0	# wLut1, wLut1$23_11
# ../../src/display/ui/default/bganim/AnimAurora.cpp:74:     if (glowLUT == nullptr || wLut1 == nullptr || wLut2 == nullptr || sqLUTStorage == nullptr) {
	beqz.n	a12, .L41	# wLut1$23_11,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:74:     if (glowLUT == nullptr || wLut1 == nullptr || wLut2 == nullptr || sqLUTStorage == nullptr) {
	l32i.n	a13, a5, 0	# wLut2, wLut2$24_12
# ../../src/display/ui/default/bganim/AnimAurora.cpp:74:     if (glowLUT == nullptr || wLut1 == nullptr || wLut2 == nullptr || sqLUTStorage == nullptr) {
	beqz.n	a13, .L41	# wLut2$24_12,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:74:     if (glowLUT == nullptr || wLut1 == nullptr || wLut2 == nullptr || sqLUTStorage == nullptr) {
	l32i.n	a2, a6, 0	# sqLUTStorage, sqLUTStorage
	beqz.n	a2, .L41	# sqLUTStorage,
	mov.n	a10, a4	# ivtmp$81, lut
	movi	a14, 0x400	# tmp135,
	loop	a14, .L42_LEND	# tmp135,
.L42:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:78:         wLut1[i] = static_cast<int32_t>(lut[i]) * W1;
	l16si	a9, a10, 0	# MEM[base: _64, offset: 0B], _17
	addi.n	a10, a10, 2	# ivtmp$81, ivtmp$81,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:79:         wLut2[i] = static_cast<int32_t>(lut[i]) * W2;
	slli	a8, a9, 6	# tmp109, _17,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:78:         wLut1[i] = static_cast<int32_t>(lut[i]) * W1;
	slli	a11, a9, 2	# tmp104, _17,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:79:         wLut2[i] = static_cast<int32_t>(lut[i]) * W2;
	add.n	a8, a8, a9	# tmp110, tmp109, _17
# ../../src/display/ui/default/bganim/AnimAurora.cpp:78:         wLut1[i] = static_cast<int32_t>(lut[i]) * W1;
	add.n	a11, a11, a9	# tmp105, tmp104, _17
# ../../src/display/ui/default/bganim/AnimAurora.cpp:79:         wLut2[i] = static_cast<int32_t>(lut[i]) * W2;
	slli	a8, a8, 1	# tmp111, tmp110,
	add.n	a8, a8, a9	# tmp112, tmp111, _17
# ../../src/display/ui/default/bganim/AnimAurora.cpp:78:         wLut1[i] = static_cast<int32_t>(lut[i]) * W1;
	slli	a9, a11, 7	# tmp106, tmp105,
	sub	a11, a9, a11	# tmp107, tmp106, tmp105
# ../../src/display/ui/default/bganim/AnimAurora.cpp:79:         wLut2[i] = static_cast<int32_t>(lut[i]) * W2;
	slli	a9, a8, 2	# tmp113, tmp112,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:78:         wLut1[i] = static_cast<int32_t>(lut[i]) * W1;
	s32i.n	a11, a12, 0	# MEM[base: _63, offset: 0B], tmp107
# ../../src/display/ui/default/bganim/AnimAurora.cpp:79:         wLut2[i] = static_cast<int32_t>(lut[i]) * W2;
	sub	a8, a9, a8	# tmp114, tmp113, tmp112
# ../../src/display/ui/default/bganim/AnimAurora.cpp:79:         wLut2[i] = static_cast<int32_t>(lut[i]) * W2;
	s32i.n	a8, a13, 0	# MEM[base: _85, offset: 0B], tmp114
	addi.n	a12, a12, 4	# ivtmp$82, ivtmp$82,
	addi.n	a13, a13, 4	# ivtmp$83, ivtmp$83,
	.L42_LEND:		#
	l32r	a2, .LC53	#, tmp115
# ../../src/display/ui/default/bganim/AnimAurora.cpp:82:         sqLUT[v] = v < 0 ? 0 : static_cast<uint16_t>((v * v) >> 12);
	movi.n	a10, 0	# iftmp$32_34,
	l32i.n	a9, a2, 0	# sqLUT, sqLUT
	l32r	a2, .LC55	#, tmp117
# ../../src/display/ui/default/bganim/AnimAurora.cpp:81:     for (int32_t v = -V_MAX; v <= V_MAX; v++) {
	l32r	a8, .LC46	#, v
	add.n	a9, a9, a2	# ivtmp$73, sqLUT, tmp117
	l32r	a12, .LC56	#, tmp126
# ../../src/display/ui/default/bganim/AnimAurora.cpp:82:         sqLUT[v] = v < 0 ? 0 : static_cast<uint16_t>((v * v) >> 12);
	mov.n	a13, a10	# iftmp$32_34, iftmp$32_34
	j	.L43		#
.L45:
	mov.n	a10, a13	# iftmp$32_34, iftmp$32_34
	bltz	a8, .L44	# v,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:82:         sqLUT[v] = v < 0 ? 0 : static_cast<uint16_t>((v * v) >> 12);
	extui	a10, a11, 12, 16	# iftmp$32_34, tmp118,,
.L44:
	addi.n	a9, a9, 2	# ivtmp$73, ivtmp$73,
.L43:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:81:     for (int32_t v = -V_MAX; v <= V_MAX; v++) {
	addi.n	a8, a8, 1	# v, v,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:82:         sqLUT[v] = v < 0 ? 0 : static_cast<uint16_t>((v * v) >> 12);
	s16i	a10, a9, 0	# MEM[base: _32, offset: 0B], iftmp$32_34
# ../../src/display/ui/default/bganim/AnimAurora.cpp:82:         sqLUT[v] = v < 0 ? 0 : static_cast<uint16_t>((v * v) >> 12);
	mull	a11, a8, a8	# tmp118, v, v
# ../../src/display/ui/default/bganim/AnimAurora.cpp:81:     for (int32_t v = -V_MAX; v <= V_MAX; v++) {
	bne	a8, a12, .L45	# v, tmp126,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:84:     buildGlowLUT();
	call8	_ZN12_GLOBAL__N_112buildGlowLUTEv		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:85:     lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimAurora.cpp:85:     lastThemeGen = themeGen();
	l32r	a3, .LC57	#, tmp121
# ../../src/display/ui/default/bganim/AnimAurora.cpp:86:     return true;
	movi.n	a2, 1	# <retval>,
# ../../src/display/ui/default/bganim/AnimAurora.cpp:85:     lastThemeGen = themeGen();
	s32i.n	a10, a3, 0	# lastThemeGen,
.L36:
# ../../src/display/ui/default/bganim/AnimAurora.cpp:87: }
	retw.n
	.size	_ZN12_GLOBAL__N_14initEii, .-_ZN12_GLOBAL__N_14initEii
	.global	bg_anim_aurora
	.section	.rodata.str1.4,"aMS",@progbits,1
	.align	4
.LC58:
	.string	"aurora"
	.align	4
.LC59:
	.string	"Aurora"
	.align	4
.LC60:
	.string	"speed"
	.align	4
.LC61:
	.string	"Speed"
	.align	4
.LC62:
	.string	"intensity"
	.align	4
.LC63:
	.string	"Intensity"
	.align	4
.LC64:
	.string	"waviness"
	.align	4
.LC65:
	.string	"Waviness"
	.section	.rodata
	.align	4
	.type	bg_anim_aurora, @object
	.size	bg_anim_aurora, 68
bg_anim_aurora:
# id:
	.word	.LC58
# name:
	.word	.LC59
# params:
# key:
	.word	.LC60
# label:
	.word	.LC61
# def:
	.byte	50
	.zero	3
# key:
	.word	.LC62
# label:
	.word	.LC63
# def:
	.byte	55
	.zero	3
# key:
	.word	.LC64
# label:
	.word	.LC65
# def:
	.byte	50
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
	.local	_ZN12_GLOBAL__N_19g_inten14E
	.comm	_ZN12_GLOBAL__N_19g_inten14E,4,4
	.local	_ZN12_GLOBAL__N_14g_A2E
	.comm	_ZN12_GLOBAL__N_14g_A2E,4,4
	.local	_ZN12_GLOBAL__N_14g_A1E
	.comm	_ZN12_GLOBAL__N_14g_A1E,4,4
	.local	_ZN12_GLOBAL__N_13g_tE
	.comm	_ZN12_GLOBAL__N_13g_tE,4,4
	.local	_ZN12_GLOBAL__N_15sqLUTE
	.comm	_ZN12_GLOBAL__N_15sqLUTE,4,4
	.local	_ZN12_GLOBAL__N_112sqLUTStorageE
	.comm	_ZN12_GLOBAL__N_112sqLUTStorageE,4,4
	.local	_ZN12_GLOBAL__N_15wLut2E
	.comm	_ZN12_GLOBAL__N_15wLut2E,4,4
	.local	_ZN12_GLOBAL__N_15wLut1E
	.comm	_ZN12_GLOBAL__N_15wLut1E,4,4
	.data
	.align	4
	.type	_ZN12_GLOBAL__N_112lastThemeGenE, @object
	.size	_ZN12_GLOBAL__N_112lastThemeGenE, 4
_ZN12_GLOBAL__N_112lastThemeGenE:
	.word	-1
	.local	_ZN12_GLOBAL__N_17glowLUTE
	.comm	_ZN12_GLOBAL__N_17glowLUTE,4,4
	.ident	"GCC: (crosstool-NG esp-2021r2-patch5) 8.4.0"
