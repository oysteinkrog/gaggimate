	.file	"AnimLava.cpp"
# GNU C++17 (crosstool-NG esp-2021r2-patch5) version 8.4.0 (xtensa-esp32s3-elf)
#	compiled by GNU C version 6.3.0 20170516, GMP version 6.2.1, MPFR version 4.1.0, MPC version 1.2.1, isl version isl-0.19-GMP

# GGC heuristics: --param ggc-min-expand=100 --param ggc-min-heapsize=131072
# options passed:  -I shim -imultilib no-rtti
# -iprefix c:\users\oystein\.platformio\packages\toolchain-xtensa-esp32s3\bin\../lib/gcc/xtensa-esp32s3-elf/8.4.0/
# ../../src/display/ui/default/bganim/AnimLava.cpp -mlongcalls
# -auxbase-strip xtensa-asm/AnimLava.S -O2 -std=gnu++17 -fno-exceptions
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
	.literal .LC1, 0x437f0000
	.literal .LC2, _ZN12_GLOBAL__N_111g_intensityE
	.literal .LC3, _ZN12_GLOBAL__N_18fieldRowE
	.literal .LC4, _ZN12_GLOBAL__N_14blobE
	.literal .LC5, 0x3f800000
	.literal .LC6, 0x3f333333
	.literal .LC7, 0x3f19999a
	.literal .LC8, _ZN12_GLOBAL__N_110paletteLUTE
	.literal .LC9, 0x3fcccccd
	.literal .LC10, 0x431f6000
	.literal .LC11, _ZN12_GLOBAL__N_19ditherLUTE
	.literal .LC12, _ZN12_GLOBAL__N_14blobE+144
	.align	4
	.type	_ZN12_GLOBAL__N_14bandEPtiiijPKh, @function
_ZN12_GLOBAL__N_14bandEPtiiijPKh:
	entry	sp, 80	#
# ../../src/display/ui/default/bganim/AnimLava.cpp:133: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
	s32i.n	a5, sp, 8	# %sfp, w
# ../../src/display/ui/default/bganim/AnimLava.cpp:134:     const float intensity = g_intensity;
	l32r	a5, .LC2	#, tmp163
# ../../src/display/ui/default/bganim/AnimLava.cpp:133: void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
# ../../src/display/ui/default/bganim/AnimLava.cpp:134:     const float intensity = g_intensity;
	lsi	f6, a5, 0	# g_intensity, intensity
# ../../src/display/ui/default/bganim/AnimLava.cpp:135:     for (int row = 0; row < rows; row++) {
	blti	a4, 1, .L1	# rows,,
	l32i.n	a6, sp, 8	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:137:         memset(fieldRow, 0, w * sizeof(float));
	l32i.n	a13, sp, 8	# %sfp,
	l32r	a8, .LC5	#,
	addi	a5, a6, -2	# _150,,
	s32i.n	a3, sp, 0	# %sfp, y0
	slli	a13, a13, 2	#,,
	srli	a7, a5, 1	# tmp165, _150,
	l32r	a3, .LC3	#, tmp164
	wfr	f7, a8	# tmp255,
	s32i.n	a13, sp, 12	# %sfp,
	l32i.n	a8, sp, 8	# %sfp,
	l32i.n	a13, sp, 0	# %sfp,
	addi.n	a7, a7, 1	# tmp166, tmp165,
	movi.n	a6, -2	# tmp263,
	and	a6, a5, a6	# tmp264, _150, tmp263
	slli	a8, a8, 1	#,,
	l32i.n	a5, a3, 0	# fieldRow, pretmp_234
	s32i.n	a2, sp, 4	# %sfp, dst
	add.n	a4, a4, a13	#, rows,
	slli	a2, a7, 1	#, tmp166,
	l32r	a3, .LC12	#, _188
	s32i.n	a2, sp, 24	# %sfp,
	s32i.n	a8, sp, 20	# %sfp,
	s32i.n	a4, sp, 16	# %sfp,
	addi.n	a2, a6, 2	# _211, tmp264,
.L15:
	l32i.n	a12, sp, 12	# %sfp,
	movi.n	a11, 0	#,
	mov.n	a10, a5	#, pretmp_234
	ssi	f6, sp, 28	#,
	ssi	f7, sp, 32	#,
	call8	memset		#
# ../../src/display/ui/default/bganim/AnimLava.cpp:157:                 fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
	l32r	a8, .LC6	#,
	l32i.n	a4, sp, 0	# %sfp,
	l32r	a6, .LC0	#,
	l32r	a13, .LC7	#,
	wfr	f8, a8	# tmp257,
	float.s	f10, a4, 0	# _242,
	l32r	a8, .LC4	#, ivtmp$58
	lsi	f7, sp, 32	#,
	lsi	f6, sp, 28	#,
	wfr	f3, a6	# tmp256,
	wfr	f9, a13	# tmp259,
.L6:
# ../../src/display/ui/default/bganim/AnimLava.cpp:140:             const float dy = y - b.by;
	lsi	f2, a8, 4	# MEM[base: _194, offset: 4], MEM[base: _194, offset: 4]
# ../../src/display/ui/default/bganim/AnimLava.cpp:142:             if (dy2 >= b.R2) {
	lsi	f0, a8, 8	# MEM[base: _194, offset: 8], MEM[base: _194, offset: 8]
# ../../src/display/ui/default/bganim/AnimLava.cpp:140:             const float dy = y - b.by;
	sub.s	f2, f10, f2	# dy, _242, MEM[base: _194, offset: 4]
# ../../src/display/ui/default/bganim/AnimLava.cpp:141:             const float dy2 = dy * dy;
	mul.s	f2, f2, f2	# dy2, dy, dy
# ../../src/display/ui/default/bganim/AnimLava.cpp:142:             if (dy2 >= b.R2) {
	ole.s	b0, f0, f2	#, MEM[base: _194, offset: 8], dy2
	bf	b0, .L30	#,
.L7:
	addi	a8, a8, 24	# ivtmp$58, ivtmp$58,
# ../../src/display/ui/default/bganim/AnimLava.cpp:138:         for (int i = 0; i < NUM_BLOBS; i++) {
	bne	a3, a8, .L6	# _188, ivtmp$58,
	j	.L5		#
.L30:
# ../../src/display/ui/default/bganim/AnimLava.cpp:149:             for (int x = b.xlo; x <= b.xhi; x++) {
	l32i.n	a7, a8, 16	# MEM[base: _194, offset: 16], x
# ../../src/display/ui/default/bganim/AnimLava.cpp:149:             for (int x = b.xlo; x <= b.xhi; x++) {
	l32i.n	a6, a8, 20	# MEM[base: _194, offset: 20], _138
# ../../src/display/ui/default/bganim/AnimLava.cpp:145:             const float invR2 = b.invR2;
	lsi	f4, a8, 12	# MEM[base: _194, offset: 12], invR2
# ../../src/display/ui/default/bganim/AnimLava.cpp:146:             const float bx = b.bx;
	lsi	f5, a8, 0	# MEM[base: _194, offset: 0], bx
# ../../src/display/ui/default/bganim/AnimLava.cpp:149:             for (int x = b.xlo; x <= b.xhi; x++) {
	blt	a6, a7, .L7	# _138, x,
	slli	a4, a7, 2	# tmp173, x,
	addi.n	a6, a6, 1	# _201, _138,
	add.n	a4, a5, a4	# ivtmp$52, pretmp_234, tmp173
# ../../src/display/ui/default/bganim/AnimLava.cpp:157:                 fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
	sub	a6, a6, a7	# tmp266, _201, x
	loop	a6, .L11_LEND	# tmp266,
.L11:
# ../../src/display/ui/default/bganim/AnimLava.cpp:150:                 const float dx = x - bx;
	float.s	f0, a7, 0	# tmp174, x
# ../../src/display/ui/default/bganim/AnimLava.cpp:151:                 const float d2 = dx * dx + dy2;
	mov.s	f1, f2	#, dy2
# ../../src/display/ui/default/bganim/AnimLava.cpp:150:                 const float dx = x - bx;
	sub.s	f0, f0, f5	# dx, tmp174, bx
# ../../src/display/ui/default/bganim/AnimLava.cpp:151:                 const float d2 = dx * dx + dy2;
	madd.s	f1, f0, f0	#, dx, dx
	mov.s	f0, f1	# d2,
# ../../src/display/ui/default/bganim/AnimLava.cpp:152:                 const float tt = 1.0f - d2 * invR2;
	mov.s	f1, f7	# tt, tmp255
	msub.s	f1, f4, f0	# tt, invR2, d2
# ../../src/display/ui/default/bganim/AnimLava.cpp:153:                 if (tt <= 0) {
	ole.s	b0, f1, f3	#, tt, tmp256
	bt	b0, .L8	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:156:                 const float t3 = tt * tt * tt;
	mul.s	f0, f1, f1	# tmp178, tt, tt
# ../../src/display/ui/default/bganim/AnimLava.cpp:157:                 fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
	olt.s	b0, f8, f1	#, tmp257, tt
# ../../src/display/ui/default/bganim/AnimLava.cpp:157:                 fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
	lsi	f11, a4, 0	# MEM[base: _199, offset: 0B], _15
# ../../src/display/ui/default/bganim/AnimLava.cpp:157:                 fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
	mov.s	f13, f3	# iftmp$4_81, tmp256
# ../../src/display/ui/default/bganim/AnimLava.cpp:156:                 const float t3 = tt * tt * tt;
	mul.s	f0, f0, f1	# t3, tmp178, tt
# ../../src/display/ui/default/bganim/AnimLava.cpp:157:                 fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
	mul.s	f12, f6, f0	# _16, intensity, t3
# ../../src/display/ui/default/bganim/AnimLava.cpp:157:                 fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
	bf	b0, .L9	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:157:                 fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
	mul.s	f0, f0, f0	# tmp180, t3, t3
# ../../src/display/ui/default/bganim/AnimLava.cpp:157:                 fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
	mul.s	f0, f0, f6	# tmp181, tmp180, intensity
# ../../src/display/ui/default/bganim/AnimLava.cpp:157:                 fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
	mul.s	f13, f0, f9	# iftmp$4_81, tmp181, tmp259
.L9:
# ../../src/display/ui/default/bganim/AnimLava.cpp:157:                 fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
	add.s	f0, f12, f13	# tmp183, _16, iftmp$4_81
# ../../src/display/ui/default/bganim/AnimLava.cpp:157:                 fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
	add.s	f0, f0, f11	# tmp184, tmp183, _15
	ssi	f0, a4, 0	# MEM[base: _199, offset: 0B], tmp184
.L8:
# ../../src/display/ui/default/bganim/AnimLava.cpp:149:             for (int x = b.xlo; x <= b.xhi; x++) {
	addi.n	a7, a7, 1	# x, x,
	addi.n	a4, a4, 4	# ivtmp$52, ivtmp$52,
	.L11_LEND:		#
	j	.L7		#
.L5:
# ../../src/display/ui/default/bganim/AnimLava.cpp:161:         const int rowDitherBase = (y & 3) * 4;
	l32i.n	a4, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:165:         for (; x + 1 < w; x += 2) {
	l32i.n	a6, sp, 8	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:161:         const int rowDitherBase = (y & 3) * 4;
	extui	a9, a4, 0, 2	# tmp185,,
# ../../src/display/ui/default/bganim/AnimLava.cpp:161:         const int rowDitherBase = (y & 3) * 4;
	slli	a9, a9, 2	# rowDitherBase, tmp185,
# ../../src/display/ui/default/bganim/AnimLava.cpp:165:         for (; x + 1 < w; x += 2) {
	bgei	a6, 2, .L12	#,,
# ../../src/display/ui/default/bganim/AnimLava.cpp:162:         int x = 0;
	movi.n	a6, 0	# x,
	j	.L18		#
.L26:
# ../../src/display/ui/default/bganim/AnimLava.cpp:165:         for (; x + 1 < w; x += 2) {
	l32i.n	a6, sp, 24	# %sfp, x
.L18:
# ../../src/display/ui/default/bganim/AnimLava.cpp:192:         for (; x < w; x++) {
	l32i.n	a8, sp, 8	# %sfp,
	blt	a6, a8, .L13	# x,,
.L21:
	l32i.n	a13, sp, 0	# %sfp,
	l32i.n	a4, sp, 4	# %sfp,
	l32i.n	a6, sp, 20	# %sfp,
	addi.n	a13, a13, 1	#,,
	add.n	a4, a4, a6	#,,
# ../../src/display/ui/default/bganim/AnimLava.cpp:135:     for (int row = 0; row < rows; row++) {
	l32i.n	a8, sp, 16	# %sfp,
	s32i.n	a13, sp, 0	# %sfp,
	s32i.n	a4, sp, 4	# %sfp,
	bne	a8, a13, .L15	#,,
	j	.L1		#
.L12:
	l32r	a13, .LC10	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:188:             const uint16_t p0 = paletteLUT[idx0];
	l32r	a4, .LC8	#, tmp186
	wfr	f8, a13	# tmp249,
	l32r	a14, .LC1	#, _256
	l32i.n	a13, a4, 0	# paletteLUT, paletteLUT$13_42
	l32r	a4, .LC9	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:162:         int x = 0;
	movi.n	a6, 0	# x,
	l32r	a10, .LC11	#, tmp240
# ../../src/display/ui/default/bganim/AnimLava.cpp:188:             const uint16_t p0 = paletteLUT[idx0];
	l32i.n	a8, sp, 4	# %sfp, ivtmp$46
	wfr	f3, a4	# tmp245,
	mov.n	a7, a5	# ivtmp$45, pretmp_234
	wfr	f5, a14	# _258, _256
	mov.s	f9, f8	# tmp251, tmp249
	movi	a12, 0xff	# tmp200,
# ../../src/display/ui/default/bganim/AnimLava.cpp:189:             const uint16_t p1 = paletteLUT[idx1];
	mov.n	a11, a6	# tmp210, x
.L19:
	addi.n	a4, a6, 1	# _217, x,
# ../../src/display/ui/default/bganim/AnimLava.cpp:176:             int idx0 = static_cast<int>(f0 * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
	extui	a15, a6, 0, 2	# tmp194, x,
# ../../src/display/ui/default/bganim/AnimLava.cpp:166:             float f0 = fieldRow[x];
	lsi	f2, a7, 0	# MEM[base: _220, offset: 0B], f0
# ../../src/display/ui/default/bganim/AnimLava.cpp:177:             int idx1 = static_cast<int>(f1 * kFieldScale + ditherLUT[rowDitherBase + ((x + 1) & 3)]);
	extui	a4, a4, 0, 2	# tmp202, _217,
# ../../src/display/ui/default/bganim/AnimLava.cpp:176:             int idx0 = static_cast<int>(f0 * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
	add.n	a15, a15, a9	# tmp195, tmp194, rowDitherBase
# ../../src/display/ui/default/bganim/AnimLava.cpp:177:             int idx1 = static_cast<int>(f1 * kFieldScale + ditherLUT[rowDitherBase + ((x + 1) & 3)]);
	add.n	a4, a4, a9	# tmp203, tmp202, rowDitherBase
# ../../src/display/ui/default/bganim/AnimLava.cpp:176:             int idx0 = static_cast<int>(f0 * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
	slli	a15, a15, 2	# tmp196, tmp195,
# ../../src/display/ui/default/bganim/AnimLava.cpp:177:             int idx1 = static_cast<int>(f1 * kFieldScale + ditherLUT[rowDitherBase + ((x + 1) & 3)]);
	slli	a4, a4, 2	# tmp204, tmp203,
# ../../src/display/ui/default/bganim/AnimLava.cpp:167:             if (f0 > 1.6f) {
	olt.s	b0, f3, f2	#, tmp245, f0
# ../../src/display/ui/default/bganim/AnimLava.cpp:176:             int idx0 = static_cast<int>(f0 * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
	add.n	a15, a10, a15	# tmp197, tmp240, tmp196
# ../../src/display/ui/default/bganim/AnimLava.cpp:177:             int idx1 = static_cast<int>(f1 * kFieldScale + ditherLUT[rowDitherBase + ((x + 1) & 3)]);
	add.n	a4, a10, a4	# tmp205, tmp240, tmp204
	wfr	f0, a14	# _256, _256
	mov.s	f1, f5	# _258, _258
# ../../src/display/ui/default/bganim/AnimLava.cpp:165:         for (; x + 1 < w; x += 2) {
	addi.n	a6, a6, 2	# x, x,
# ../../src/display/ui/default/bganim/AnimLava.cpp:167:             if (f0 > 1.6f) {
	bt	b0, .L16	#,
	mul.s	f0, f2, f8	# _256, f0, tmp249
.L16:
# ../../src/display/ui/default/bganim/AnimLava.cpp:170:             float f1 = fieldRow[x + 1];
	lsi	f2, a7, 4	# MEM[base: _220, offset: 4B], f1
	addi.n	a7, a7, 8	# ivtmp$45, ivtmp$45,
# ../../src/display/ui/default/bganim/AnimLava.cpp:171:             if (f1 > 1.6f) {
	olt.s	b0, f3, f2	#, tmp245, f1
	bt	b0, .L17	#,
	mul.s	f1, f2, f9	# _258, f1, tmp251
.L17:
# ../../src/display/ui/default/bganim/AnimLava.cpp:177:             int idx1 = static_cast<int>(f1 * kFieldScale + ditherLUT[rowDitherBase + ((x + 1) & 3)]);
	lsi	f4, a4, 0	# ditherLUT, tmp207
# ../../src/display/ui/default/bganim/AnimLava.cpp:176:             int idx0 = static_cast<int>(f0 * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
	lsi	f2, a15, 0	# ditherLUT, tmp199
# ../../src/display/ui/default/bganim/AnimLava.cpp:177:             int idx1 = static_cast<int>(f1 * kFieldScale + ditherLUT[rowDitherBase + ((x + 1) & 3)]);
	add.s	f1, f1, f4	# tmp206, _258, tmp207
# ../../src/display/ui/default/bganim/AnimLava.cpp:176:             int idx0 = static_cast<int>(f0 * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
	add.s	f0, f0, f2	# tmp198, _256, tmp199
# ../../src/display/ui/default/bganim/AnimLava.cpp:177:             int idx1 = static_cast<int>(f1 * kFieldScale + ditherLUT[rowDitherBase + ((x + 1) & 3)]);
	trunc.s	a15, f1, 0	# _71, tmp206
# ../../src/display/ui/default/bganim/AnimLava.cpp:176:             int idx0 = static_cast<int>(f0 * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
	trunc.s	a4, f0, 0	# _146, tmp198
	min	a15, a15, a12	# _71, _71, tmp200
	min	a4, a4, a12	# _146, _146, tmp200
# ../../src/display/ui/default/bganim/AnimLava.cpp:189:             const uint16_t p1 = paletteLUT[idx1];
	max	a15, a15, a11	# idx1, _71, tmp210
# ../../src/display/ui/default/bganim/AnimLava.cpp:189:             const uint16_t p1 = paletteLUT[idx1];
	slli	a15, a15, 1	# tmp211, idx1,
# ../../src/display/ui/default/bganim/AnimLava.cpp:188:             const uint16_t p0 = paletteLUT[idx0];
	max	a4, a4, a11	# idx0, _146, tmp210
# ../../src/display/ui/default/bganim/AnimLava.cpp:189:             const uint16_t p1 = paletteLUT[idx1];
	add.n	a15, a13, a15	# tmp212, paletteLUT$13_42, tmp211
# ../../src/display/ui/default/bganim/AnimLava.cpp:188:             const uint16_t p0 = paletteLUT[idx0];
	slli	a4, a4, 1	# tmp217, idx0,
# ../../src/display/ui/default/bganim/AnimLava.cpp:190:             *reinterpret_cast<uint32_t *>(out + x) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
	l16ui	a15, a15, 0	# *_48, *_48
# ../../src/display/ui/default/bganim/AnimLava.cpp:188:             const uint16_t p0 = paletteLUT[idx0];
	add.n	a4, a13, a4	# tmp218, paletteLUT$13_42, tmp217
# ../../src/display/ui/default/bganim/AnimLava.cpp:190:             *reinterpret_cast<uint32_t *>(out + x) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
	l16ui	a4, a4, 0	# *_45, *_45
# ../../src/display/ui/default/bganim/AnimLava.cpp:190:             *reinterpret_cast<uint32_t *>(out + x) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
	slli	a15, a15, 16	# tmp214, *_48,
# ../../src/display/ui/default/bganim/AnimLava.cpp:190:             *reinterpret_cast<uint32_t *>(out + x) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
	or	a4, a15, a4	# tmp220, tmp214, *_45
# ../../src/display/ui/default/bganim/AnimLava.cpp:190:             *reinterpret_cast<uint32_t *>(out + x) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
	s32i.n	a4, a8, 0	# MEM[base: _216, offset: 0B], tmp220
	addi.n	a8, a8, 4	# ivtmp$46, ivtmp$46,
# ../../src/display/ui/default/bganim/AnimLava.cpp:165:         for (; x + 1 < w; x += 2) {
	bne	a6, a2, .L19	# x, _211,
	j	.L26		#
.L13:
# ../../src/display/ui/default/bganim/AnimLava.cpp:203:             out[x] = paletteLUT[idx];
	l32r	a4, .LC8	#, tmp221
	l32r	a8, .LC9	#,
	l32i.n	a12, a4, 0	# paletteLUT, paletteLUT$20_63
	l32r	a10, .LC11	#, tmp240
	l32i.n	a14, sp, 4	# %sfp, ivtmp$62
	l32i.n	a15, sp, 8	# %sfp, w
	wfr	f3, a8	# tmp245,
	movi	a11, 0xff	# tmp233,
	movi.n	a8, 0	# tmp235,
.L22:
# ../../src/display/ui/default/bganim/AnimLava.cpp:193:             float f = fieldRow[x];
	slli	a7, a6, 2	# tmp222, x,
	add.n	a7, a5, a7	# tmp223, pretmp_234, tmp222
# ../../src/display/ui/default/bganim/AnimLava.cpp:197:             int idx = static_cast<int>(f * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
	extui	a4, a6, 0, 2	# tmp227, x,
# ../../src/display/ui/default/bganim/AnimLava.cpp:193:             float f = fieldRow[x];
	lsi	f1, a7, 0	# MEM[base: _230, offset: 0B], f
# ../../src/display/ui/default/bganim/AnimLava.cpp:197:             int idx = static_cast<int>(f * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
	add.n	a4, a4, a9	# tmp228, tmp227, rowDitherBase
	l32r	a13, .LC1	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:203:             out[x] = paletteLUT[idx];
	slli	a7, a6, 1	# tmp238, x,
# ../../src/display/ui/default/bganim/AnimLava.cpp:197:             int idx = static_cast<int>(f * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
	slli	a4, a4, 2	# tmp229, tmp228,
# ../../src/display/ui/default/bganim/AnimLava.cpp:194:             if (f > 1.6f) {
	olt.s	b0, f3, f1	#, tmp245, f
# ../../src/display/ui/default/bganim/AnimLava.cpp:197:             int idx = static_cast<int>(f * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
	add.n	a4, a10, a4	# tmp230, tmp240, tmp229
# ../../src/display/ui/default/bganim/AnimLava.cpp:203:             out[x] = paletteLUT[idx];
	add.n	a7, a14, a7	# tmp239, ivtmp$62, tmp238
	wfr	f0, a13	# _260,
# ../../src/display/ui/default/bganim/AnimLava.cpp:192:         for (; x < w; x++) {
	addi.n	a6, a6, 1	# x, x,
# ../../src/display/ui/default/bganim/AnimLava.cpp:194:             if (f > 1.6f) {
	bt	b0, .L20	#,
	l32r	a13, .LC10	#,
	wfr	f2, a13	#,
	mul.s	f0, f1, f2	# _260, f,
.L20:
# ../../src/display/ui/default/bganim/AnimLava.cpp:197:             int idx = static_cast<int>(f * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
	lsi	f1, a4, 0	# ditherLUT, tmp232
	add.s	f0, f0, f1	# tmp231, _260, tmp232
# ../../src/display/ui/default/bganim/AnimLava.cpp:197:             int idx = static_cast<int>(f * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
	trunc.s	a4, f0, 0	# _125, tmp231
	min	a4, a4, a11	# _125, _125, tmp233
# ../../src/display/ui/default/bganim/AnimLava.cpp:203:             out[x] = paletteLUT[idx];
	max	a4, a4, a8	# idx, _125, tmp235
	slli	a4, a4, 1	# tmp236, idx,
	add.n	a4, a12, a4	# tmp237, paletteLUT$20_63, tmp236
	l16ui	a4, a4, 0	# *_66, _69
# ../../src/display/ui/default/bganim/AnimLava.cpp:203:             out[x] = paletteLUT[idx];
	s16i	a4, a7, 0	# MEM[base: _227, offset: 0B], _69
# ../../src/display/ui/default/bganim/AnimLava.cpp:192:         for (; x < w; x++) {
	beq	a15, a6, .L21	# w, x,
	j	.L22		#
.L1:
# ../../src/display/ui/default/bganim/AnimLava.cpp:206: }
	retw.n
	.size	_ZN12_GLOBAL__N_14bandEPtiiijPKh, .-_ZN12_GLOBAL__N_14bandEPtiiijPKh
	.global	__divsf3
	.section	.text._ZN12_GLOBAL__N_15frameEjiiPKh,"ax",@progbits
	.literal_position
	.literal .LC13, 0x3d610e10
	.literal .LC14, 0x391268a9
	.literal .LC15, 0x42c80000
	.literal .LC16, 0x3f19999a
	.literal .LC17, _ZN12_GLOBAL__N_111g_intensityE
	.literal .LC18, 0x3fa66666
	.literal .LC19, 0x3f000000
	.literal .LC20, _ZN12_GLOBAL__N_112lastThemeGenE
	.literal .LC21, _ZN12_GLOBAL__N_110paletteLUTE
	.literal .LC22, _ZN12_GLOBAL__N_17blobDefE
	.literal .LC23, _ZN12_GLOBAL__N_14blobE
	.literal .LC24, 0x3fc90fda
	.literal .LC25, 0x4222f983
	.literal .LC26, 0x3fd9999a
	.literal .LC27, 0x3f90a3d7
	.literal .LC28, 0x3f666666
	.literal .LC29, 0x3f800000
	.align	4
	.type	_ZN12_GLOBAL__N_15frameEjiiPKh, @function
_ZN12_GLOBAL__N_15frameEjiiPKh:
	entry	sp, 64	#
# ../../src/display/ui/default/bganim/AnimLava.cpp:99:     const float omega0 = 6.2831853f / 45000.0f * speedMul(p[0]); // 45s base cycle at speed 50
	l8ui	a4, a5, 0	# *p_70(D), *p_70(D)
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	addi	a4, a4, -50	# tmp159, *p_70(D),
# ../../src/display/ui/default/bganim/BgAnimCommon.h:123:     return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
	float.s	f0, a4, 0	# tmp160, tmp159
	l32r	a4, .LC13	#,
	wfr	f1, a4	#,
	mul.s	f0, f0, f1	#, tmp160,
	rfr	a10, f0	#,
	call8	exp2f		#
# ../../src/display/ui/default/bganim/AnimLava.cpp:99:     const float omega0 = 6.2831853f / 45000.0f * speedMul(p[0]); // 45s base cycle at speed 50
	l32r	a6, .LC14	#,
	wfr	f0, a10	#,
	wfr	f1, a6	#,
	mul.s	f0, f0, f1	#,,
# ../../src/display/ui/default/bganim/AnimLava.cpp:100:     const float sizeMul = 0.6f + (p[1] / 100.0f);
	l8ui	a4, a5, 1	# MEM[(const uint8_t *)p_70(D) + 1B], MEM[(const uint8_t *)p_70(D) + 1B]
# ../../src/display/ui/default/bganim/AnimLava.cpp:100:     const float sizeMul = 0.6f + (p[1] / 100.0f);
	l32r	a11, .LC15	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:99:     const float omega0 = 6.2831853f / 45000.0f * speedMul(p[0]); // 45s base cycle at speed 50
	ssi	f0, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:100:     const float sizeMul = 0.6f + (p[1] / 100.0f);
	float.s	f0, a4, 0	#, MEM[(const uint8_t *)p_70(D) + 1B]
	rfr	a10, f0	#,
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimLava.cpp:101:     g_intensity = 0.5f + (p[2] / 100.0f) * 1.3f;
	l8ui	a4, a5, 2	# MEM[(const uint8_t *)p_70(D) + 2B], MEM[(const uint8_t *)p_70(D) + 2B]
# ../../src/display/ui/default/bganim/AnimLava.cpp:100:     const float sizeMul = 0.6f + (p[1] / 100.0f);
	l32r	a5, .LC16	#,
	wfr	f1, a10	#,
	wfr	f2, a5	#,
	add.s	f1, f1, f2	#,,
# ../../src/display/ui/default/bganim/AnimLava.cpp:101:     g_intensity = 0.5f + (p[2] / 100.0f) * 1.3f;
	float.s	f0, a4, 0	#, MEM[(const uint8_t *)p_70(D) + 2B]
	l32r	a11, .LC15	#,
	rfr	a10, f0	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:100:     const float sizeMul = 0.6f + (p[1] / 100.0f);
	ssi	f1, sp, 8	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:101:     g_intensity = 0.5f + (p[2] / 100.0f) * 1.3f;
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimLava.cpp:101:     g_intensity = 0.5f + (p[2] / 100.0f) * 1.3f;
	l32r	a4, .LC19	#,
	wfr	f1, a10	#,
	wfr	f0, a4	# tmp172,
	l32r	a4, .LC18	#,
	wfr	f2, a4	#,
	madd.s	f0, f1, f2	# tmp172,,
# ../../src/display/ui/default/bganim/AnimLava.cpp:101:     g_intensity = 0.5f + (p[2] / 100.0f) * 1.3f;
	l32r	a4, .LC17	#, tmp168
	ssi	f0, a4, 0	# g_intensity, tmp172
# ../../src/display/ui/default/bganim/AnimLava.cpp:102:     if (themeGen() != lastThemeGen) {
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimLava.cpp:102:     if (themeGen() != lastThemeGen) {
	l32r	a4, .LC20	#, tmp175
# ../../src/display/ui/default/bganim/AnimLava.cpp:102:     if (themeGen() != lastThemeGen) {
	l32i.n	a5, a4, 0	# lastThemeGen, lastThemeGen
	beq	a10, a5, .L35	#, lastThemeGen,
# ../../src/display/ui/default/bganim/AnimLava.cpp:103:         buildThemeRamp(paletteLUT, 256);
	l32r	a5, .LC21	#, tmp177
	movi.n	a12, 0	#,
	l32i.n	a10, a5, 0	# paletteLUT,
	movi	a11, 0x100	#,
	call8	_ZN6bganim14buildThemeRampEPttb		#
# ../../src/display/ui/default/bganim/AnimLava.cpp:104:         lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimLava.cpp:104:         lastThemeGen = themeGen();
	s32i.n	a10, a4, 0	# lastThemeGen,
.L35:
# ../../src/display/ui/default/bganim/AnimLava.cpp:106:     const float t = tMs * omega0;
	ufloat.s	f0, a2, 0	#, tMs
# ../../src/display/ui/default/bganim/AnimLava.cpp:106:     const float t = tMs * omega0;
	lsi	f1, sp, 0	# %sfp,
	l32r	a2, .LC22	#, ivtmp$69
	mul.s	f1, f0, f1	#,,
	movi	a4, 0x1b0	# tmp179,
# ../../src/display/ui/default/bganim/AnimLava.cpp:126:             xhi = w - 1;
	addi.n	a8, a3, -1	#, w,
	l32r	a7, .LC23	#, ivtmp$70
# ../../src/display/ui/default/bganim/AnimLava.cpp:114:         b.invR2 = 1.0f / b.R2;
	l32r	a6, .LC29	#, tmp253
# ../../src/display/ui/default/bganim/AnimLava.cpp:106:     const float t = tMs * omega0;
	ssi	f0, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:106:     const float t = tMs * omega0;
	ssi	f1, sp, 0	# %sfp,
	add.n	a4, a2, a4	# _180, ivtmp$69, tmp179
	movi.n	a5, 0	# tmp245,
# ../../src/display/ui/default/bganim/AnimLava.cpp:126:             xhi = w - 1;
	s32i.n	a8, sp, 12	# %sfp,
.L37:
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	lsi	f0, a2, 0	# MEM[base: _166, offset: 0], MEM[base: _166, offset: 0]
	lsi	f1, a2, 32	# MEM[base: _166, offset: 32], MEM[base: _166, offset: 32]
	lsi	f3, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	l32r	a8, .LC24	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	madd.s	f1, f0, f3	# tmp180, MEM[base: _166, offset: 0],
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	wfr	f5, a8	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	lsi	f2, a2, 48	# MEM[base: _166, offset: 48], _18
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	lsi	f4, a2, 16	# MEM[base: _166, offset: 16], _19
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	sub.s	f1, f1, f5	# _131, tmp180,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	ssi	f2, sp, 24	#,
	ssi	f4, sp, 16	#,
	ssi	f1, sp, 20	#,
	call8	_ZN6bganim9cosTableFEv		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	l32r	a8, .LC25	#,
	lsi	f1, sp, 20	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	lsi	f3, a2, 4	# MEM[base: _166, offset: 4], MEM[base: _166, offset: 4]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	wfr	f7, a8	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	lsi	f5, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	mul.s	f1, f1, f7	# tmp184, _131,
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	mul.s	f3, f5, f3	# tmp191,, MEM[base: _166, offset: 4]
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	l32r	a8, .LC26	#,
	lsi	f0, a2, 36	# MEM[base: _166, offset: 36], MEM[base: _166, offset: 36]
	wfr	f5, a8	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	trunc.s	a8, f1, 0	# tmp186, tmp184
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	madd.s	f0, f3, f5	# tmp193, tmp191,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	l32r	a9, .LC24	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	extui	a8, a8, 0, 8	# tmp187, tmp186,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	slli	a8, a8, 2	# tmp188, tmp187,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	wfr	f1, a9	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	add.n	a10, a10, a8	# tmp189,, tmp188
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	sub.s	f0, f0, f1	# _122, tmp193,
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	lsi	f4, sp, 16	#,
	lsi	f1, a10, 0	# *_138, *_138
	lsi	f2, sp, 24	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	lsi	f6, a2, 20	# MEM[base: _166, offset: 20], _26
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	madd.s	f2, f4, f1	# _25, _19, *_138
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	ssi	f0, sp, 20	#,
	ssi	f6, sp, 16	#,
	ssi	f2, sp, 24	#,
	call8	_ZN6bganim9cosTableFEv		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	l32r	a8, .LC25	#,
	lsi	f0, sp, 20	#,
	wfr	f1, a8	#,
	mul.s	f0, f0, f1	# tmp197, _122,
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	lsi	f4, a2, 8	# MEM[base: _166, offset: 8], MEM[base: _166, offset: 8]
	lsi	f3, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	lsi	f1, a2, 40	# MEM[base: _166, offset: 40], MEM[base: _166, offset: 40]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	trunc.s	a8, f0, 0	# tmp199, tmp197
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	mul.s	f4, f3, f4	# tmp205,, MEM[base: _166, offset: 8]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	extui	a8, a8, 0, 8	# tmp200, tmp199,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	slli	a8, a8, 2	# tmp201, tmp200,
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	lsi	f2, sp, 24	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	add.n	a10, a10, a8	# tmp202,, tmp201
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	lsi	f5, a10, 0	# *_129, *_129
	lsi	f6, sp, 16	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	l32r	a8, .LC27	#,
	mov.s	f0, f1	# _40, MEM[base: _166, offset: 40]
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	mov.s	f1, f2	# tmp203, _25
	madd.s	f1, f6, f5	# tmp203, _26, *_129
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	wfr	f7, a8	#,
	madd.s	f0, f4, f7	# _40, tmp205,
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	lsi	f3, a2, 52	# MEM[base: _166, offset: 52], _34
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	lsi	f5, a2, 24	# MEM[base: _166, offset: 24], _35
# ../../src/display/ui/default/bganim/AnimLava.cpp:110:         b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
	ssi	f1, a7, 0	# MEM[base: _173, offset: 0], tmp203
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	ssi	f0, sp, 20	#,
	ssi	f3, sp, 28	#,
	ssi	f5, sp, 16	#,
	call8	_ZN6bganim9cosTableFEv		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	l32r	a8, .LC25	#,
	lsi	f0, sp, 20	#,
	wfr	f1, a8	#,
	mul.s	f0, f0, f1	# tmp209, _40,
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	lsi	f2, a2, 12	# MEM[base: _166, offset: 12], MEM[base: _166, offset: 12]
	lsi	f1, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	l32r	a9, .LC28	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	mul.s	f2, f1, f2	# tmp216,, MEM[base: _166, offset: 12]
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	trunc.s	a8, f0, 0	# tmp211, tmp209
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	lsi	f1, a2, 44	# MEM[base: _166, offset: 44], MEM[base: _166, offset: 44]
	wfr	f7, a9	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	extui	a8, a8, 0, 8	# tmp212, tmp211,
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	madd.s	f1, f2, f7	# tmp218, tmp216,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	slli	a8, a8, 2	# tmp213, tmp212,
	add.n	a10, a10, a8	# tmp214,, tmp213
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	l32r	a8, .LC24	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	lsi	f5, sp, 16	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	wfr	f0, a8	#,
	sub.s	f1, f1, f0	# _105, tmp218,
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	lsi	f3, sp, 28	#,
	lsi	f0, a10, 0	# *_120, *_120
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	lsi	f4, a2, 28	# MEM[base: _166, offset: 28], _43
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	madd.s	f3, f5, f0	# _42, _35, *_120
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	ssi	f1, sp, 20	#,
	ssi	f4, sp, 16	#,
	ssi	f3, sp, 28	#,
	call8	_ZN6bganim9cosTableFEv		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	l32r	a8, .LC25	#,
	lsi	f1, sp, 20	#,
	wfr	f0, a8	#,
	mul.s	f1, f1, f0	# tmp222, _105,
# ../../src/display/ui/default/bganim/AnimLava.cpp:112:         const float R = (d.R0 + d.Rpulse * fastSinRad(tMs * d.wR + d.phR)) * sizeMul;
	lsi	f5, a2, 64	# MEM[base: _166, offset: 64], MEM[base: _166, offset: 64]
	lsi	f0, a2, 68	# MEM[base: _166, offset: 68], MEM[base: _166, offset: 68]
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	lsi	f3, sp, 28	#,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	trunc.s	a8, f1, 0	# tmp224, tmp222
# ../../src/display/ui/default/bganim/AnimLava.cpp:112:         const float R = (d.R0 + d.Rpulse * fastSinRad(tMs * d.wR + d.phR)) * sizeMul;
	lsi	f1, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	extui	a8, a8, 0, 8	# tmp225, tmp224,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	slli	a8, a8, 2	# tmp226, tmp225,
	add.n	a10, a10, a8	# tmp227,, tmp226
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	lsi	f4, sp, 16	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:112:         const float R = (d.R0 + d.Rpulse * fastSinRad(tMs * d.wR + d.phR)) * sizeMul;
	madd.s	f0, f1, f5	# tmp230,, MEM[base: _166, offset: 64]
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	lsi	f1, a10, 0	# *_112, *_112
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	l32r	a8, .LC24	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	madd.s	f3, f4, f1	# _42, _43, *_112
# ../../src/display/ui/default/bganim/BgAnimCommon.h:58: inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }
	wfr	f5, a8	#,
	sub.s	f0, f0, f5	# _96, tmp230,
# ../../src/display/ui/default/bganim/AnimLava.cpp:112:         const float R = (d.R0 + d.Rpulse * fastSinRad(tMs * d.wR + d.phR)) * sizeMul;
	lsi	f2, a2, 56	# MEM[base: _166, offset: 56], _51
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	mov.s	f1, f3	# tmp228, _42
# ../../src/display/ui/default/bganim/AnimLava.cpp:112:         const float R = (d.R0 + d.Rpulse * fastSinRad(tMs * d.wR + d.phR)) * sizeMul;
	lsi	f3, a2, 60	# MEM[base: _166, offset: 60], _52
# ../../src/display/ui/default/bganim/AnimLava.cpp:111:         b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
	ssi	f1, a7, 4	# MEM[base: _173, offset: 4], tmp228
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	ssi	f0, sp, 20	#,
	ssi	f2, sp, 24	#,
	ssi	f3, sp, 28	#,
	call8	_ZN6bganim9cosTableFEv		#
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	l32r	a8, .LC25	#,
	lsi	f0, sp, 20	#,
	wfr	f1, a8	#,
	mul.s	f0, f0, f1	# tmp234, _96,
# ../../src/display/ui/default/bganim/AnimLava.cpp:112:         const float R = (d.R0 + d.Rpulse * fastSinRad(tMs * d.wR + d.phR)) * sizeMul;
	lsi	f3, sp, 28	#,
	lsi	f2, sp, 24	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:112:         const float R = (d.R0 + d.Rpulse * fastSinRad(tMs * d.wR + d.phR)) * sizeMul;
	lsi	f1, sp, 8	# %sfp,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	trunc.s	a8, f0, 0	# tmp236, tmp234
	addi	a2, a2, 72	# ivtmp$69, ivtmp$69,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	extui	a8, a8, 0, 8	# tmp237, tmp236,
# ../../src/display/ui/default/bganim/BgAnimCommon.h:57: inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
	slli	a8, a8, 2	# tmp238, tmp237,
	add.n	a10, a10, a8	# tmp239,, tmp238
# ../../src/display/ui/default/bganim/AnimLava.cpp:112:         const float R = (d.R0 + d.Rpulse * fastSinRad(tMs * d.wR + d.phR)) * sizeMul;
	lsi	f0, a10, 0	# *_103, *_103
# ../../src/display/ui/default/bganim/AnimLava.cpp:114:         b.invR2 = 1.0f / b.R2;
	mov.n	a10, a6	#, tmp253
# ../../src/display/ui/default/bganim/AnimLava.cpp:112:         const float R = (d.R0 + d.Rpulse * fastSinRad(tMs * d.wR + d.phR)) * sizeMul;
	madd.s	f2, f3, f0	# _51, _52, *_103
# ../../src/display/ui/default/bganim/AnimLava.cpp:112:         const float R = (d.R0 + d.Rpulse * fastSinRad(tMs * d.wR + d.phR)) * sizeMul;
	mul.s	f0, f2, f1	# R, tmp240,
# ../../src/display/ui/default/bganim/AnimLava.cpp:113:         b.R2 = R * R;
	mul.s	f1, f0, f0	# _59, R, R
# ../../src/display/ui/default/bganim/AnimLava.cpp:113:         b.R2 = R * R;
	ssi	f1, a7, 8	# MEM[base: _173, offset: 8], _59
# ../../src/display/ui/default/bganim/AnimLava.cpp:114:         b.invR2 = 1.0f / b.R2;
	rfr	a11, f1	#, _59
	ssi	f0, sp, 20	#,
	call8	__divsf3		#
# ../../src/display/ui/default/bganim/AnimLava.cpp:120:         int xlo = static_cast<int>(b.bx - R);
	lsi	f1, a7, 0	# MEM[base: _173, offset: 0], _61
# ../../src/display/ui/default/bganim/AnimLava.cpp:120:         int xlo = static_cast<int>(b.bx - R);
	lsi	f0, sp, 20	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:114:         b.invR2 = 1.0f / b.R2;
	s32i.n	a10, a7, 12	# MEM[base: _173, offset: 12],
# ../../src/display/ui/default/bganim/AnimLava.cpp:120:         int xlo = static_cast<int>(b.bx - R);
	sub.s	f2, f1, f0	# tmp243, _61, R
# ../../src/display/ui/default/bganim/AnimLava.cpp:121:         int xhi = static_cast<int>(b.bx + R);
	add.s	f0, f1, f0	# tmp244, _61, R
# ../../src/display/ui/default/bganim/AnimLava.cpp:120:         int xlo = static_cast<int>(b.bx - R);
	trunc.s	a10, f2, 0	# xlo, tmp243
# ../../src/display/ui/default/bganim/AnimLava.cpp:121:         int xhi = static_cast<int>(b.bx + R);
	trunc.s	a8, f0, 0	# xhi, tmp244
	max	a10, a10, a5	# xlo, xlo, tmp245
# ../../src/display/ui/default/bganim/AnimLava.cpp:125:         if (xhi > w - 1) {
	blt	a8, a3, .L36	# xhi, w,
# ../../src/display/ui/default/bganim/AnimLava.cpp:126:             xhi = w - 1;
	l32i.n	a8, sp, 12	# %sfp, xhi
.L36:
# ../../src/display/ui/default/bganim/AnimLava.cpp:128:         b.xlo = xlo;
	s32i.n	a10, a7, 16	# MEM[base: _173, offset: 16], xlo
# ../../src/display/ui/default/bganim/AnimLava.cpp:129:         b.xhi = xhi;
	s32i.n	a8, a7, 20	# MEM[base: _173, offset: 20], xhi
	addi	a7, a7, 24	# ivtmp$70, ivtmp$70,
# ../../src/display/ui/default/bganim/AnimLava.cpp:107:     for (int i = 0; i < NUM_BLOBS; i++) {
	bne	a2, a4, .L37	# ivtmp$69, _180,
# ../../src/display/ui/default/bganim/AnimLava.cpp:131: }
	retw.n
	.size	_ZN12_GLOBAL__N_15frameEjiiPKh, .-_ZN12_GLOBAL__N_15frameEjiiPKh
	.section	.text._ZN12_GLOBAL__N_14initEii,"ax",@progbits
	.literal_position
	.literal .LC30, 0x00000000
	.literal .LC31, 0x3f800000
	.literal .LC32, _ZN12_GLOBAL__N_110paletteLUTE
	.literal .LC33, _ZN12_GLOBAL__N_18fieldRowE
	.literal .LC34, _ZN12_GLOBAL__N_16initedE
	.literal .LC35, _ZN6bganim6BAYER4E
	.literal .LC36, _ZN12_GLOBAL__N_19ditherLUTE
	.literal .LC37, 0x3d800000
	.literal .LC38, 0xbf000000
	.literal .LC39, 0x3c000000
	.literal .LC40, 0x437f0000
	.literal .LC41, 0x3e0f5c29
	.literal .LC42, 0x3d8f5c29
	.literal .LC43, 0x3f000000
	.literal .LC44, 0x3e8f5c29
	.literal .LC45, 0x3e428f5c
	.literal .LC46, 0x3d4ccccd
	.literal .LC47, _ZN12_GLOBAL__N_17blobDefE
	.literal .LC48, 0x401998ff
	.literal .LC49, 0x3de147ae
	.literal .LC50, 0x3f0ccccd
	.literal .LC51, 0x3eb33333
	.literal .LC52, 0x3fb504f3
	.literal .LC53, -858993459
	.literal .LC55, 0x3db851ec
	.literal .LC56, 0x3f2147ae
	.literal .LC57, 0x3d23d70a
	.literal .LC58, 0x3e99999a
	.literal .LC59, 0x3fddb3d7
	.literal .LC60, 0x40066666
	.literal .LC61, 0x3fd9999a
	.literal .LC62, 0x3f19999a
	.literal .LC63, 0x37fba882
	.literal .LC64, 0x38e6afcd
	.literal .LC65, 0x402ccccd
	.literal .LC66, 0x3fa66666
	.literal .LC67, _ZN12_GLOBAL__N_112lastThemeGenE
	.align	4
	.type	_ZN12_GLOBAL__N_14initEii, @function
_ZN12_GLOBAL__N_14initEii:
	entry	sp, 80	#
# ../../src/display/ui/default/bganim/AnimLava.cpp:53:     if (paletteLUT == nullptr) {
	l32r	a5, .LC32	#, tmp225
# ../../src/display/ui/default/bganim/AnimLava.cpp:52: bool init(int w, int h) {
	mov.n	a7, a2	# w, w
# ../../src/display/ui/default/bganim/AnimLava.cpp:53:     if (paletteLUT == nullptr) {
	l32i.n	a4, a5, 0	# paletteLUT, paletteLUT
	bnez.n	a4, .L40	# paletteLUT,
# ../../src/display/ui/default/bganim/AnimLava.cpp:54:         paletteLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
	movi	a10, 0x200	#,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimLava.cpp:56:     if (fieldRow == nullptr) {
	l32r	a6, .LC33	#, tmp227
# ../../src/display/ui/default/bganim/AnimLava.cpp:54:         paletteLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
	s32i.n	a10, a5, 0	# paletteLUT, _2
# ../../src/display/ui/default/bganim/AnimLava.cpp:56:     if (fieldRow == nullptr) {
	l32i.n	a2, a6, 0	# fieldRow, fieldRow
	bnez.n	a2, .L41	# fieldRow,
.L51:
# ../../src/display/ui/default/bganim/AnimLava.cpp:57:         fieldRow = static_cast<float *>(alloc(w * sizeof(float)));
	slli	a10, a7, 2	#, w,
	call8	_ZN6bganim5allocEj		#
# ../../src/display/ui/default/bganim/AnimLava.cpp:59:     if (paletteLUT == nullptr || fieldRow == nullptr) {
	l32i.n	a4, a5, 0	# paletteLUT, paletteLUT$29_57
# ../../src/display/ui/default/bganim/AnimLava.cpp:57:         fieldRow = static_cast<float *>(alloc(w * sizeof(float)));
	s32i.n	a10, a6, 0	# fieldRow, _6
# ../../src/display/ui/default/bganim/AnimLava.cpp:59:     if (paletteLUT == nullptr || fieldRow == nullptr) {
	bnez.n	a4, .L42	# paletteLUT$29_57,
.L44:
# ../../src/display/ui/default/bganim/AnimLava.cpp:60:         return false;
	movi.n	a2, 0	# <retval>,
	j	.L53		#
.L42:
# ../../src/display/ui/default/bganim/AnimLava.cpp:59:     if (paletteLUT == nullptr || fieldRow == nullptr) {
	beqz.n	a10, .L44	# _6,
.L50:
# ../../src/display/ui/default/bganim/AnimLava.cpp:62:     if (!inited) {
	l32r	a5, .LC34	#, tmp115
	l8ui	a2, a5, 0	# inited, <retval>
# ../../src/display/ui/default/bganim/AnimLava.cpp:62:     if (!inited) {
	bnez.n	a2, .L53	# <retval>,
	l32r	a2, .LC37	#,
	l32r	a6, .LC38	#,
	wfr	f4, a2	# tmp203,
	l32r	a2, .LC39	#,
	wfr	f3, a6	# tmp205,
	l32r	a6, .LC40	#,
	wfr	f2, a2	# tmp206,
	l32r	a11, .LC35	#, ivtmp$90
# ../../src/display/ui/default/bganim/AnimLava.cpp:63:         inited = true;
	movi.n	a2, 1	# tmp117,
	l32r	a10, .LC36	#, ivtmp$91
	wfr	f1, a6	# tmp204,
	s8i	a2, a5, 0	# inited, tmp117
	movi.n	a9, 0x10	# tmp235,
	loop	a9, .L45_LEND	# tmp235,
.L45:
# ../../src/display/ui/default/bganim/AnimLava.cpp:65:             ditherLUT[k] = (BAYER4[k] / 16.0f - 0.5f) * (1.0f / 128.0f) * 255.0f;
	l8ui	a8, a11, 0	# MEM[base: _156, offset: 0B], MEM[base: _156, offset: 0B]
# ../../src/display/ui/default/bganim/AnimLava.cpp:65:             ditherLUT[k] = (BAYER4[k] / 16.0f - 0.5f) * (1.0f / 128.0f) * 255.0f;
	mov.s	f5, f3	#, tmp205
# ../../src/display/ui/default/bganim/AnimLava.cpp:65:             ditherLUT[k] = (BAYER4[k] / 16.0f - 0.5f) * (1.0f / 128.0f) * 255.0f;
	float.s	f0, a8, 0	# tmp119, MEM[base: _156, offset: 0B]
	addi.n	a11, a11, 1	# ivtmp$90, ivtmp$90,
# ../../src/display/ui/default/bganim/AnimLava.cpp:65:             ditherLUT[k] = (BAYER4[k] / 16.0f - 0.5f) * (1.0f / 128.0f) * 255.0f;
	madd.s	f5, f0, f4	#, tmp119, tmp203
# ../../src/display/ui/default/bganim/AnimLava.cpp:65:             ditherLUT[k] = (BAYER4[k] / 16.0f - 0.5f) * (1.0f / 128.0f) * 255.0f;
	mul.s	f0, f5, f2	# tmp123, tmp120, tmp206
# ../../src/display/ui/default/bganim/AnimLava.cpp:65:             ditherLUT[k] = (BAYER4[k] / 16.0f - 0.5f) * (1.0f / 128.0f) * 255.0f;
	mul.s	f0, f0, f1	# tmp125, tmp123, tmp204
# ../../src/display/ui/default/bganim/AnimLava.cpp:65:             ditherLUT[k] = (BAYER4[k] / 16.0f - 0.5f) * (1.0f / 128.0f) * 255.0f;
	ssi	f0, a10, 0	# MEM[base: _157, offset: 0B], tmp125
	addi.n	a10, a10, 4	# ivtmp$91, ivtmp$91,
	.L45_LEND:		#
# ../../src/display/ui/default/bganim/AnimLava.cpp:74:             d.ax1 = w * 0.14f;
	l32r	a2, .LC41	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:75:             d.ax2 = w * 0.07f;
	l32r	a6, .LC42	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:74:             d.ax1 = w * 0.14f;
	wfr	f5, a2	# tmp127,
# ../../src/display/ui/default/bganim/AnimLava.cpp:75:             d.ax2 = w * 0.07f;
	wfr	f4, a6	# tmp128,
# ../../src/display/ui/default/bganim/AnimLava.cpp:82:             d.cx = w * 0.5f + w * 0.28f * cosf(ga);
	l32r	a2, .LC43	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:82:             d.cx = w * 0.5f + w * 0.28f * cosf(ga);
	l32r	a6, .LC44	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:76:             d.ay1 = h * 0.14f;
	float.s	f0, a3, 0	# _34, h
# ../../src/display/ui/default/bganim/AnimLava.cpp:74:             d.ax1 = w * 0.14f;
	float.s	f1, a7, 0	# _31, w
# ../../src/display/ui/default/bganim/AnimLava.cpp:82:             d.cx = w * 0.5f + w * 0.28f * cosf(ga);
	wfr	f3, a2	# tmp131,
# ../../src/display/ui/default/bganim/AnimLava.cpp:82:             d.cx = w * 0.5f + w * 0.28f * cosf(ga);
	wfr	f2, a6	# tmp132,
# ../../src/display/ui/default/bganim/AnimLava.cpp:74:             d.ax1 = w * 0.14f;
	mul.s	f6, f1, f5	#, _31, tmp127
# ../../src/display/ui/default/bganim/AnimLava.cpp:75:             d.ax2 = w * 0.07f;
	mul.s	f7, f1, f4	#, _31, tmp128
# ../../src/display/ui/default/bganim/AnimLava.cpp:82:             d.cx = w * 0.5f + w * 0.28f * cosf(ga);
	mul.s	f8, f1, f3	#, _31, tmp131
# ../../src/display/ui/default/bganim/AnimLava.cpp:82:             d.cx = w * 0.5f + w * 0.28f * cosf(ga);
	mul.s	f12, f1, f2	#, _31, tmp132
# ../../src/display/ui/default/bganim/AnimLava.cpp:76:             d.ay1 = h * 0.14f;
	mul.s	f5, f0, f5	#, _34, tmp127
# ../../src/display/ui/default/bganim/AnimLava.cpp:77:             d.ay2 = h * 0.07f;
	mul.s	f4, f0, f4	#, _34, tmp128
# ../../src/display/ui/default/bganim/AnimLava.cpp:83:             d.cy = h * 0.5f + h * 0.28f * sinf(ga * 1.3f);
	mul.s	f3, f0, f3	#, _34, tmp131
# ../../src/display/ui/default/bganim/AnimLava.cpp:83:             d.cy = h * 0.5f + h * 0.28f * sinf(ga * 1.3f);
	mul.s	f2, f0, f2	#, _34, tmp132
# ../../src/display/ui/default/bganim/AnimLava.cpp:74:             d.ax1 = w * 0.14f;
	ssi	f6, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:75:             d.ax2 = w * 0.07f;
	ssi	f7, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:76:             d.ay1 = h * 0.14f;
	ssi	f5, sp, 8	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:77:             d.ay2 = h * 0.07f;
	ssi	f4, sp, 12	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:82:             d.cx = w * 0.5f + w * 0.28f * cosf(ga);
	ssi	f8, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:82:             d.cx = w * 0.5f + w * 0.28f * cosf(ga);
	ssi	f12, sp, 20	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:83:             d.cy = h * 0.5f + h * 0.28f * sinf(ga * 1.3f);
	ssi	f3, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:83:             d.cy = h * 0.5f + h * 0.28f * sinf(ga * 1.3f);
	ssi	f2, sp, 28	# %sfp,
	bge	a7, a3, .L47	# w, h,
	mov.s	f0, f1	# _31, _31
.L47:
# ../../src/display/ui/default/bganim/AnimLava.cpp:85:             d.R0 = m * 0.19f;
	l32r	a2, .LC45	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:86:             d.Rpulse = m * 0.05f;
	l32r	a3, .LC30	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:85:             d.R0 = m * 0.19f;
	wfr	f1, a2	#,
	mul.s	f1, f0, f1	#, _31,
# ../../src/display/ui/default/bganim/AnimLava.cpp:86:             d.Rpulse = m * 0.05f;
	l32r	a2, .LC46	#,
	l32r	a6, .LC31	#,
	wfr	f5, a3	# _154,
# ../../src/display/ui/default/bganim/AnimLava.cpp:85:             d.R0 = m * 0.19f;
	ssi	f1, sp, 32	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:86:             d.Rpulse = m * 0.05f;
	wfr	f1, a2	#,
	mul.s	f0, f0, f1	#, _31,
	l32r	a2, .LC47	#, ivtmp$81
	l32r	a3, .LC53	#, tmp213
	wfr	f6, a6	# _150,
	ssi	f0, sp, 36	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:67:         for (int i = 0; i < NUM_BLOBS; i++) {
	movi.n	a5, 0	# i,
	mov.s	f1, f5	# _17, _154
.L49:
# ../../src/display/ui/default/bganim/AnimLava.cpp:68:             const float ga = i * 2.39996323f; // golden angle spreads phases
	l32r	a6, .LC48	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:83:             d.cy = h * 0.5f + h * 0.28f * sinf(ga * 1.3f);
	lsi	f8, sp, 28	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:68:             const float ga = i * 2.39996323f; // golden angle spreads phases
	wfr	f2, a6	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:71:             d.fx2 = 1.41421356f * (0.35f + 0.05f * i);
	l32r	a6, .LC51	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:68:             const float ga = i * 2.39996323f; // golden angle spreads phases
	mul.s	f0, f1, f2	# ga, _17,
# ../../src/display/ui/default/bganim/AnimLava.cpp:71:             d.fx2 = 1.41421356f * (0.35f + 0.05f * i);
	wfr	f3, a6	# tmp143,
	l32r	a6, .LC46	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:83:             d.cy = h * 0.5f + h * 0.28f * sinf(ga * 1.3f);
	lsi	f7, sp, 24	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:71:             d.fx2 = 1.41421356f * (0.35f + 0.05f * i);
	wfr	f2, a6	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:73:             d.fy2 = 1.73205081f * (0.30f + 0.04f * i);
	l32r	a6, .LC58	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:71:             d.fx2 = 1.41421356f * (0.35f + 0.05f * i);
	madd.s	f3, f1, f2	# tmp143, _17,
# ../../src/display/ui/default/bganim/AnimLava.cpp:73:             d.fy2 = 1.73205081f * (0.30f + 0.04f * i);
	wfr	f2, a6	# tmp173,
	l32r	a6, .LC57	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:83:             d.cy = h * 0.5f + h * 0.28f * sinf(ga * 1.3f);
	madd.s	f7, f8, f5	#,, _154
# ../../src/display/ui/default/bganim/AnimLava.cpp:73:             d.fy2 = 1.73205081f * (0.30f + 0.04f * i);
	wfr	f4, a6	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:70:             d.fx1 = 0.55f + 0.11f * i;
	l32r	a6, .LC50	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	slli	a8, a5, 1	# tmp149, i,
# ../../src/display/ui/default/bganim/AnimLava.cpp:70:             d.fx1 = 0.55f + 0.11f * i;
	wfr	f11, a6	# tmp140,
	l32r	a6, .LC49	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	add.n	a8, a8, a5	# tmp150, tmp149, i
# ../../src/display/ui/default/bganim/AnimLava.cpp:70:             d.fx1 = 0.55f + 0.11f * i;
	wfr	f12, a6	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	muluh	a9, a8, a3	# tmp202, tmp150, tmp213
# ../../src/display/ui/default/bganim/AnimLava.cpp:71:             d.fx2 = 1.41421356f * (0.35f + 0.05f * i);
	l32r	a6, .LC52	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	srli	a9, a9, 2	# tmp151, tmp202,
# ../../src/display/ui/default/bganim/AnimLava.cpp:83:             d.cy = h * 0.5f + h * 0.28f * sinf(ga * 1.3f);
	mov.s	f5, f7	# tmp185,
# ../../src/display/ui/default/bganim/AnimLava.cpp:71:             d.fx2 = 1.41421356f * (0.35f + 0.05f * i);
	wfr	f7, a6	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	l32r	a6, .LC56	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	slli	a10, a9, 2	# tmp156, tmp151,
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	wfr	f10, a6	# tmp170,
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	add.n	a9, a10, a9	# tmp157, tmp156, tmp151
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	l32r	a6, .LC55	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	sub	a8, a8, a9	# tmp158, tmp150, tmp157
# ../../src/display/ui/default/bganim/AnimLava.cpp:73:             d.fy2 = 1.73205081f * (0.30f + 0.04f * i);
	madd.s	f2, f1, f4	# tmp173, _17,
# ../../src/display/ui/default/bganim/AnimLava.cpp:71:             d.fx2 = 1.41421356f * (0.35f + 0.05f * i);
	mul.s	f3, f3, f7	# tmp146, tmp143,
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	float.s	f4, a8, 0	# tmp169, tmp158
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	wfr	f7, a6	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:73:             d.fy2 = 1.73205081f * (0.30f + 0.04f * i);
	l32r	a6, .LC59	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	madd.s	f10, f4, f7	# tmp170, tmp169,
# ../../src/display/ui/default/bganim/AnimLava.cpp:73:             d.fy2 = 1.73205081f * (0.30f + 0.04f * i);
	wfr	f4, a6	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:79:             d.px2 = ga * 2.1f;
	l32r	a6, .LC60	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:73:             d.fy2 = 1.73205081f * (0.30f + 0.04f * i);
	mul.s	f2, f2, f4	# tmp176, tmp173,
# ../../src/display/ui/default/bganim/AnimLava.cpp:79:             d.px2 = ga * 2.1f;
	wfr	f4, a6	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:80:             d.py1 = ga * 1.7f;
	l32r	a6, .LC61	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:79:             d.px2 = ga * 2.1f;
	mul.s	f9, f0, f4	# tmp178, ga,
# ../../src/display/ui/default/bganim/AnimLava.cpp:80:             d.py1 = ga * 1.7f;
	wfr	f4, a6	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:81:             d.py2 = ga * 0.6f;
	l32r	a6, .LC62	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:80:             d.py1 = ga * 1.7f;
	mul.s	f8, f0, f4	# tmp180, ga,
# ../../src/display/ui/default/bganim/AnimLava.cpp:81:             d.py2 = ga * 0.6f;
	wfr	f4, a6	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:70:             d.fx1 = 0.55f + 0.11f * i;
	madd.s	f11, f1, f12	# tmp140, _17,
# ../../src/display/ui/default/bganim/AnimLava.cpp:81:             d.py2 = ga * 0.6f;
	mul.s	f7, f0, f4	# tmp182, ga,
# ../../src/display/ui/default/bganim/AnimLava.cpp:82:             d.cx = w * 0.5f + w * 0.28f * cosf(ga);
	lsi	f12, sp, 20	# %sfp,
	lsi	f4, sp, 16	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:87:             d.wR = 0.00011f + 0.00003f * i;
	l32r	a6, .LC64	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:82:             d.cx = w * 0.5f + w * 0.28f * cosf(ga);
	madd.s	f4, f12, f6	#,, _150
# ../../src/display/ui/default/bganim/AnimLava.cpp:73:             d.fy2 = 1.73205081f * (0.30f + 0.04f * i);
	ssi	f2, a2, 12	# MEM[base: _124, offset: 12], tmp176
# ../../src/display/ui/default/bganim/AnimLava.cpp:74:             d.ax1 = w * 0.14f;
	lsi	f2, sp, 0	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:70:             d.fx1 = 0.55f + 0.11f * i;
	ssi	f11, a2, 0	# MEM[base: _124, offset: 0], tmp140
# ../../src/display/ui/default/bganim/AnimLava.cpp:82:             d.cx = w * 0.5f + w * 0.28f * cosf(ga);
	mov.s	f6, f4	# tmp184,
# ../../src/display/ui/default/bganim/AnimLava.cpp:87:             d.wR = 0.00011f + 0.00003f * i;
	wfr	f4, a6	# tmp186,
	l32r	a6, .LC63	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:74:             d.ax1 = w * 0.14f;
	ssi	f2, a2, 16	# MEM[base: _124, offset: 16],
# ../../src/display/ui/default/bganim/AnimLava.cpp:87:             d.wR = 0.00011f + 0.00003f * i;
	wfr	f12, a6	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:88:             d.phR = ga * 2.7f;
	l32r	a6, .LC65	#,
# ../../src/display/ui/default/bganim/AnimLava.cpp:87:             d.wR = 0.00011f + 0.00003f * i;
	madd.s	f4, f1, f12	# tmp186, _17,
# ../../src/display/ui/default/bganim/AnimLava.cpp:88:             d.phR = ga * 2.7f;
	wfr	f12, a6	#,
	mul.s	f1, f0, f12	# tmp189, ga,
# ../../src/display/ui/default/bganim/AnimLava.cpp:75:             d.ax2 = w * 0.07f;
	lsi	f12, sp, 4	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:76:             d.ay1 = h * 0.14f;
	lsi	f2, sp, 8	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:75:             d.ax2 = w * 0.07f;
	ssi	f12, a2, 20	# MEM[base: _124, offset: 20],
# ../../src/display/ui/default/bganim/AnimLava.cpp:77:             d.ay2 = h * 0.07f;
	lsi	f12, sp, 12	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:76:             d.ay1 = h * 0.14f;
	ssi	f2, a2, 24	# MEM[base: _124, offset: 24],
# ../../src/display/ui/default/bganim/AnimLava.cpp:71:             d.fx2 = 1.41421356f * (0.35f + 0.05f * i);
	ssi	f3, a2, 4	# MEM[base: _124, offset: 4], tmp146
# ../../src/display/ui/default/bganim/AnimLava.cpp:72:             d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
	ssi	f10, a2, 8	# MEM[base: _124, offset: 8], tmp170
# ../../src/display/ui/default/bganim/AnimLava.cpp:77:             d.ay2 = h * 0.07f;
	ssi	f12, a2, 28	# MEM[base: _124, offset: 28],
# ../../src/display/ui/default/bganim/AnimLava.cpp:78:             d.px1 = ga;
	ssi	f0, a2, 32	# MEM[base: _124, offset: 32], ga
# ../../src/display/ui/default/bganim/AnimLava.cpp:86:             d.Rpulse = m * 0.05f;
	lsi	f2, sp, 36	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:85:             d.R0 = m * 0.19f;
	lsi	f0, sp, 32	# %sfp,
# ../../src/display/ui/default/bganim/AnimLava.cpp:79:             d.px2 = ga * 2.1f;
	ssi	f9, a2, 36	# MEM[base: _124, offset: 36], tmp178
# ../../src/display/ui/default/bganim/AnimLava.cpp:80:             d.py1 = ga * 1.7f;
	ssi	f8, a2, 40	# MEM[base: _124, offset: 40], tmp180
# ../../src/display/ui/default/bganim/AnimLava.cpp:81:             d.py2 = ga * 0.6f;
	ssi	f7, a2, 44	# MEM[base: _124, offset: 44], tmp182
# ../../src/display/ui/default/bganim/AnimLava.cpp:82:             d.cx = w * 0.5f + w * 0.28f * cosf(ga);
	ssi	f6, a2, 48	# MEM[base: _124, offset: 48], tmp184
# ../../src/display/ui/default/bganim/AnimLava.cpp:83:             d.cy = h * 0.5f + h * 0.28f * sinf(ga * 1.3f);
	ssi	f5, a2, 52	# MEM[base: _124, offset: 52], tmp185
# ../../src/display/ui/default/bganim/AnimLava.cpp:85:             d.R0 = m * 0.19f;
	ssi	f0, a2, 56	# MEM[base: _124, offset: 56],
# ../../src/display/ui/default/bganim/AnimLava.cpp:86:             d.Rpulse = m * 0.05f;
	ssi	f2, a2, 60	# MEM[base: _124, offset: 60],
# ../../src/display/ui/default/bganim/AnimLava.cpp:87:             d.wR = 0.00011f + 0.00003f * i;
	ssi	f4, a2, 64	# MEM[base: _124, offset: 64], tmp186
# ../../src/display/ui/default/bganim/AnimLava.cpp:88:             d.phR = ga * 2.7f;
	ssi	f1, a2, 68	# MEM[base: _124, offset: 68], tmp189
# ../../src/display/ui/default/bganim/AnimLava.cpp:67:         for (int i = 0; i < NUM_BLOBS; i++) {
	addi.n	a5, a5, 1	# i, i,
	addi	a2, a2, 72	# ivtmp$81, ivtmp$81,
# ../../src/display/ui/default/bganim/AnimLava.cpp:67:         for (int i = 0; i < NUM_BLOBS; i++) {
	beqi	a5, 6, .L48	# i,,
	l32r	a6, .LC48	#,
	float.s	f1, a5, 0	# _17, i
	wfr	f4, a6	#,
	mul.s	f0, f1, f4	# _146, _17,
	ssi	f1, sp, 44	#,
	rfr	a10, f0	#, _146
	ssi	f0, sp, 40	#,
	call8	cosf		#
	l32r	a6, .LC66	#,
	lsi	f0, sp, 40	#,
	wfr	f2, a6	#,
	mul.s	f0, f0, f2	#, _146,
	wfr	f6, a10	# _150,
	ssi	f6, sp, 40	#,
	rfr	a10, f0	#,
	call8	sinf		#
	wfr	f5, a10	# _154,
	lsi	f1, sp, 44	#,
	lsi	f6, sp, 40	#,
	j	.L49		#
.L48:
# ../../src/display/ui/default/bganim/AnimLava.cpp:90:         buildThemeRamp(paletteLUT, 256);
	movi.n	a12, 0	#,
	movi	a11, 0x100	#,
	mov.n	a10, a4	#, paletteLUT$29_57
	call8	_ZN6bganim14buildThemeRampEPttb		#
# ../../src/display/ui/default/bganim/AnimLava.cpp:91:         lastThemeGen = themeGen();
	call8	_ZN6bganim8themeGenEv		#
# ../../src/display/ui/default/bganim/AnimLava.cpp:91:         lastThemeGen = themeGen();
	l32r	a3, .LC67	#, tmp195
# ../../src/display/ui/default/bganim/AnimLava.cpp:93:     return true;
	movi.n	a2, 1	# <retval>,
# ../../src/display/ui/default/bganim/AnimLava.cpp:91:         lastThemeGen = themeGen();
	s32i.n	a10, a3, 0	# lastThemeGen,
	j	.L53		#
.L41:
# ../../src/display/ui/default/bganim/AnimLava.cpp:59:     if (paletteLUT == nullptr || fieldRow == nullptr) {
	mov.n	a4, a10	# paletteLUT$29_57, _2
# ../../src/display/ui/default/bganim/AnimLava.cpp:59:     if (paletteLUT == nullptr || fieldRow == nullptr) {
	beqz.n	a10, .L44	# paletteLUT$29_57,
	j	.L50		#
.L40:
# ../../src/display/ui/default/bganim/AnimLava.cpp:56:     if (fieldRow == nullptr) {
	l32r	a6, .LC33	#, tmp227
# ../../src/display/ui/default/bganim/AnimLava.cpp:56:     if (fieldRow == nullptr) {
	l32i.n	a2, a6, 0	# fieldRow, fieldRow
	bnez.n	a2, .L50	# fieldRow,
	j	.L51		#
.L53:
# ../../src/display/ui/default/bganim/AnimLava.cpp:94: }
	retw.n
	.size	_ZN12_GLOBAL__N_14initEii, .-_ZN12_GLOBAL__N_14initEii
	.global	bg_anim_lava
	.section	.rodata.str1.4,"aMS",@progbits,1
	.align	4
.LC68:
	.string	"lava"
	.align	4
.LC69:
	.string	"Lava"
	.align	4
.LC70:
	.string	"speed"
	.align	4
.LC71:
	.string	"Speed"
	.align	4
.LC72:
	.string	"scale"
	.align	4
.LC73:
	.string	"Blob size"
	.align	4
.LC74:
	.string	"glow"
	.align	4
.LC75:
	.string	"Glow"
	.section	.rodata
	.align	4
	.type	bg_anim_lava, @object
	.size	bg_anim_lava, 68
bg_anim_lava:
# id:
	.word	.LC68
# name:
	.word	.LC69
# params:
# key:
	.word	.LC70
# label:
	.word	.LC71
# def:
	.byte	50
	.zero	3
# key:
	.word	.LC72
# label:
	.word	.LC73
# def:
	.byte	50
	.zero	3
# key:
	.word	.LC74
# label:
	.word	.LC75
# def:
	.byte	60
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
	.data
	.align	4
	.type	_ZN12_GLOBAL__N_111g_intensityE, @object
	.size	_ZN12_GLOBAL__N_111g_intensityE, 4
_ZN12_GLOBAL__N_111g_intensityE:
	.word	1065353216
	.local	_ZN12_GLOBAL__N_16initedE
	.comm	_ZN12_GLOBAL__N_16initedE,1,1
	.align	4
	.type	_ZN12_GLOBAL__N_112lastThemeGenE, @object
	.size	_ZN12_GLOBAL__N_112lastThemeGenE, 4
_ZN12_GLOBAL__N_112lastThemeGenE:
	.word	-1
	.local	_ZN12_GLOBAL__N_19ditherLUTE
	.comm	_ZN12_GLOBAL__N_19ditherLUTE,64,4
	.local	_ZN12_GLOBAL__N_18fieldRowE
	.comm	_ZN12_GLOBAL__N_18fieldRowE,4,4
	.local	_ZN12_GLOBAL__N_110paletteLUTE
	.comm	_ZN12_GLOBAL__N_110paletteLUTE,4,4
	.local	_ZN12_GLOBAL__N_14blobE
	.comm	_ZN12_GLOBAL__N_14blobE,144,4
	.local	_ZN12_GLOBAL__N_17blobDefE
	.comm	_ZN12_GLOBAL__N_17blobDefE,432,4
	.ident	"GCC: (crosstool-NG esp-2021r2-patch5) 8.4.0"
