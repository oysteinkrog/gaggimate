// GCC 14.2 baseline before the hand kernel, kept verbatim for QEMU.
// The 27-instruction loop reloads dOuter via a spilled pointer bias.
	.global crescentWeightsGcc
	.section	.text.crescentWeightsGcc,"ax",@progbits
	.align	4
	.type	crescentWeightsGcc, @function
crescentWeightsGcc:
.Lgcc_FB1781:
	entry	sp, 48	#
.Lgcc_CFI0:
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:144:                                                                const uint16_t *sm, int n) {
	l32i	a8, sp, 56	# n, n
	l32i	a11, sp, 48	# inv, inv
	l32i	a12, sp, 52	# sm, sm
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:145:     for (int x = 0; x < n; ++x) {
	blti	a8, 1, .Lgcc_1	# n,,
	addx2	a9, a8, a2	#, _89, n, out
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:148:         uo = uo < 0 ? 0 : (uo > 256 ? 256 : uo);
	sub	a9, a9, a2	# tmp115, _89, ivtmp$131
	addi	a9, a9, -2	# tmp116, tmp115,
	srli	a9, a9, 1	# tmp117, tmp116,
	add.n	a8, a2, a4	# _95, ivtmp$131, dOuter
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:145:     for (int x = 0; x < n; ++x) {
	mov.n	a10, a2	# ivtmp$131, out
	s32i	a8, sp, 12	# %sfp, _95
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:146:         int uo = 128 + ((qOuter * inv) >> 16);
	movi	a15, 0x80	# tmp91,
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:148:         uo = uo < 0 ? 0 : (uo > 256 ? 256 : uo);
	movi	a14, 0x100	# tmp92,
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:148:         uo = uo < 0 ? 0 : (uo > 256 ? 256 : uo);
	movi.n	a13, 0	# tmp100,
	addi.n	a9, a9, 1	# tmp114, tmp117,
	loop	a9, .Lgcc_3_LEND	# tmp114,
.Lgcc_3:
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:147:         int uc = 128 + ((((qCut >> 8) - r2) * inv) >> 16);
	srai	a4, a5, 8	# _3, qCut,
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:147:         int uc = 128 + ((((qCut >> 8) - r2) * inv) >> 16);
	sub	a4, a4, a7	# _4, _3, tmp83
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:146:         int uo = 128 + ((qOuter * inv) >> 16);
	mull	a8, a11, a3	# _1, inv, qOuter
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:147:         int uc = 128 + ((((qCut >> 8) - r2) * inv) >> 16);
	mull	a4, a4, a11	# _5, _4, inv
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:146:         int uo = 128 + ((qOuter * inv) >> 16);
	srai	a8, a8, 16	# _2, _1,
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:147:         int uc = 128 + ((((qCut >> 8) - r2) * inv) >> 16);
	srai	a4, a4, 16	# _6, _5,
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:146:         int uo = 128 + ((qOuter * inv) >> 16);
	add.n	a8, a8, a15	# _7, _2, tmp91
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:147:         int uc = 128 + ((((qCut >> 8) - r2) * inv) >> 16);
	add.n	a4, a4, a15	# _8, _6, tmp91
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:148:         uo = uo < 0 ? 0 : (uo > 256 ? 256 : uo);
	min	a8, a8, a14	# _7, _7, tmp92
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:149:         uc = uc < 0 ? 0 : (uc > 256 ? 256 : uc);
	min	a4, a4, a14	# _8, _8, tmp92
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:148:         uo = uo < 0 ? 0 : (uo > 256 ? 256 : uo);
	max	a8, a8, a13	# uo_41, _7, tmp100
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:149:         uc = uc < 0 ? 0 : (uc > 256 ? 256 : uc);
	max	a4, a4, a13	# uc_42, _8, tmp100
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:150:         out[x] = static_cast<uint16_t>((sm[uo] * sm[uc]) >> 8);
	extui	a8, a8, 0, 16	# uo_41, uo_41
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:150:         out[x] = static_cast<uint16_t>((sm[uo] * sm[uc]) >> 8);
	extui	a4, a4, 0, 16	# uc_42, uc_42
	addx2	a4, a4, a12	#, tmp108, uc_42, sm
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:150:         out[x] = static_cast<uint16_t>((sm[uo] * sm[uc]) >> 8);
	addx2	a8, a8, a12	#, tmp103, uo_41, sm
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:150:         out[x] = static_cast<uint16_t>((sm[uo] * sm[uc]) >> 8);
	l16ui	a2, a4, 0	# *_16,
	l16ui	a8, a8, 0	# *_11,
	l32i	a4, sp, 12	# %sfp, _95
	mul16u	a8, a8, a2	# _19, *_11, *_16
	sub	a4, a4, a10	# _93, _95, ivtmp$131
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:150:         out[x] = static_cast<uint16_t>((sm[uo] * sm[uc]) >> 8);
	srai	a8, a8, 8	# _20, _19,
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:150:         out[x] = static_cast<uint16_t>((sm[uo] * sm[uc]) >> 8);
	s16i	a8, a10, 0	# MEM[(uint16_t *)_98], _20
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:152:         qCut += dCut; dCut += 512;
	add.n	a5, a5, a6	# qCut, qCut, dCut
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:151:         qOuter += dOuter; dOuter -= 2;
	add.n	a3, a3, a4	# qOuter, qOuter, _93
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:152:         qCut += dCut; dCut += 512;
	addmi	a6, a6, 0x200	# dCut, dCut,
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:145:     for (int x = 0; x < n; ++x) {
	addi.n	a10, a10, 2	# ivtmp$131, ivtmp$131,
	.Lgcc_3_LEND:		#
.Lgcc_1:
# ../../src/display/ui/default/bganim/AnimCrescent.cpp:154: }
	retw.n
.Lgcc_FE1781:
	.size	crescentWeightsGcc, .-crescentWeightsGcc
