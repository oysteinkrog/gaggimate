// Generates blend_qemu_vectors.h: a fixed, deterministic set of test
// vectors and host-computed expected outputs for blendGroup8General's
// general (no lane opaque) arithmetic path, plus two full 8-pixel-group
// cases (colour buffer + dst-before + expected dst-after).
//
// Why this exists: opt-qemu (a sibling agent) has a working pattern for
// executing arbitrary EE.* asm in this repo's display-qemu QEMU env and
// checking the result against a scalar reference at boot
// (gm_pxbench_and_pietest_once() in the vendored LVGL, currently
// uncommitted/in-flux -- see their own recommendation: write a standalone
// function elsewhere, not in that file, following the same shape). This
// generator produces the "known input buffers + expected outputs" half of
// that shape so whoever writes the actual QEMU self-test doesn't have to
// re-derive test cases or re-run the host prover -- they can #include the
// output header directly. This program itself never touches pio, qemu, or
// the device; it only emits a header file, matching this directory's
// offline-only scope.
//
// Build and run:
//   g++ -O2 -std=c++17 -I. gen_qemu_vectors.cpp -o build/gen_qemu_vectors
//   ./build/gen_qemu_vectors > blend_qemu_vectors.h
#include "blend_model.h"
#include "blend_ref.h"
#include <cstdio>
#include <random>
#include <vector>

using namespace blendopt;

namespace {

struct PixelCase {
    uint16_t fg, bg;
    uint32_t a;
    uint16_t expect;
};

std::vector<PixelCase> buildPixelCases() {
    std::vector<PixelCase> out;
    // Directed: every boundary alpha (never 0 or 255 -- those never reach
    // this path) crossed with colour extremes, so the widest per-channel
    // sums this arithmetic will ever see are represented explicitly.
    const uint32_t boundaryAlphas[] = {1, 2, 127, 128, 253, 254};
    const uint16_t extremeColours[] = {
        0x0000, // black
        0xFFFF, // white (0xF81F | 0x07E0 masked in -- full white in 565)
        0xF800, // full red
        0x07E0, // full green
        0x001F, // full blue
        0xF81F, // full red+blue, no green
    };
    for (uint32_t a : boundaryAlphas) {
        for (uint16_t fg : extremeColours) {
            for (uint16_t bg : extremeColours) {
                const uint16_t want = blend565_ref(fg, bg, static_cast<uint8_t>(a));
                out.push_back({fg, bg, a, want});
            }
        }
    }
    // Randomized top-up on a fixed seed, so this header is reproducible
    // across regenerations. The directed set above is already 6*6*6=216
    // cases, past this floor, so in practice this loop adds nothing today
    // -- it's a floor for if the directed set ever shrinks, not a target.
    std::mt19937 rng(0xB1e4d0u);
    std::uniform_int_distribution<int> colourDist(0, 65535);
    std::uniform_int_distribution<int> alphaDist(1, 254);
    while (out.size() < 128) {
        const uint16_t fg = static_cast<uint16_t>(colourDist(rng));
        const uint16_t bg = static_cast<uint16_t>(colourDist(rng));
        const uint32_t a = static_cast<uint32_t>(alphaDist(rng));
        const uint16_t want = blend565_ref(fg, bg, static_cast<uint8_t>(a));
        out.push_back({fg, bg, a, want});
    }
    return out;
}

// One 8-pixel group's raw colour+alpha bytes (24 B, 3 B/px, matching the
// real overlay layout) plus the dst row content before and the expected
// content after -- exactly what a QEMU self-test needs to reproduce
// blendGroup8General's real input/output contract for one group.
struct GroupCase {
    const char *label;
    uint8_t colour[24]; // 8 x (loLE, hiLE, alpha)
    uint16_t dstBefore[8];
    uint16_t dstExpect[8];
};

void fillPixel(uint8_t *colour, int k, uint16_t c, uint8_t a) {
    colour[k * 3 + 0] = static_cast<uint8_t>(c & 0xFF);
    colour[k * 3 + 1] = static_cast<uint8_t>((c >> 8) & 0xFF);
    colour[k * 3 + 2] = a;
}

GroupCase buildGeneralGroup() {
    // All 8 lanes non-opaque, non-zero alpha, varied colours/alphas so
    // every lane exercises a different point in the arithmetic -- this is
    // the group blendGroup8General's vector arithmetic is actually for.
    GroupCase g{};
    g.label = "general_all_nonopaque";
    const uint16_t colours[8] = {0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0xF81F, 0x8410, 0x2104};
    const uint8_t alphas[8] = {1, 254, 127, 128, 2, 253, 64, 191};
    const uint16_t dst[8] = {0xFFFF, 0x0000, 0x07E0, 0xF800, 0xF81F, 0x001F, 0x2104, 0x8410};
    for (int k = 0; k < 8; k++) {
        fillPixel(g.colour, k, colours[k], alphas[k]);
        g.dstBefore[k] = dst[k];
        g.dstExpect[k] = blendPixelGeneral_model(colours[k], dst[k], alphas[k]);
    }
    return g;
}

GroupCase buildOpaqueGroup() {
    // All 8 lanes exactly a==255 -- the trivial copy path, included so the
    // same QEMU test can also confirm the group-dispatch DECISION (not
    // just the general arithmetic) picks the right path on real hardware.
    GroupCase g{};
    g.label = "all_opaque_copy";
    const uint16_t colours[8] = {0x1234, 0x5678, 0x9ABC, 0xDEF0, 0x0F0F, 0xF0F0, 0x1111, 0xEEEE};
    const uint16_t dst[8] = {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};
    for (int k = 0; k < 8; k++) {
        fillPixel(g.colour, k, colours[k], 255);
        g.dstBefore[k] = dst[k];
        g.dstExpect[k] = colours[k]; // opaque copy, not blend565_ref -- see blendRow's own a==255 special case
    }
    return g;
}

} // namespace

int main() {
    const auto pixels = buildPixelCases();
    const GroupCase groups[] = {buildGeneralGroup(), buildOpaqueGroup()};

    printf("// AUTO-GENERATED by tools/animbench/kernels-blend/gen_qemu_vectors.cpp.\n");
    printf("// Do not hand-edit -- regenerate instead. Fixed seed (0xB1e4d0), so this\n");
    printf("// file is reproducible across runs.\n");
    printf("//\n");
    printf("// Test vectors for blendGroup8General's general (no lane opaque) vector\n");
    printf("// arithmetic path, and for the group-dispatch decision itself, computed\n");
    printf("// against blend565_ref / blendPixelGeneral_model (host, proven bit-exact\n");
    printf("// against the real SleepAnimation.cpp blend565 -- see prove_layer1.cpp,\n");
    printf("// 23.2M cases, 0 failures). Intended for a standalone Xtensa/QEMU self-test\n");
    printf("// (see opt-qemu's gm_pxbench_and_pietest_once() pattern: known input\n");
    printf("// buffers, run the real EE.* asm from blend_group8.S / blend_pie_kernel.cpp,\n");
    printf("// compare against BLEND_PIXEL_CASES[i].expect / BLEND_GROUP_CASES[g]\n");
    printf("// .dstExpect, ESP_LOGW PASS/FAIL, hex-dump on mismatch) -- this header only\n");
    printf("// provides the data, it has no dependency on Xtensa, QEMU, or pio, and was\n");
    printf("// generated entirely on the host.\n");
    printf("#pragma once\n#include <cstdint>\n\n");
    printf("namespace blendopt {\n\n");

    printf("struct BlendPixelCase { uint16_t fg, bg; uint32_t a; uint16_t expect; };\n\n");
    printf("inline constexpr BlendPixelCase BLEND_PIXEL_CASES[%zu] = {\n", pixels.size());
    for (const auto &c : pixels) {
        printf("    {0x%04X, 0x%04X, %u, 0x%04X},\n", c.fg, c.bg, c.a, c.expect);
    }
    printf("};\n\n");

    printf("struct BlendGroupCase {\n");
    printf("    const char *label;\n");
    printf("    uint8_t colour[24];\n");
    printf("    uint16_t dstBefore[8];\n");
    printf("    uint16_t dstExpect[8];\n");
    printf("};\n\n");
    printf("inline constexpr BlendGroupCase BLEND_GROUP_CASES[%zu] = {\n", sizeof(groups) / sizeof(groups[0]));
    for (const auto &g : groups) {
        printf("    {\n        \"%s\",\n        {", g.label);
        for (int i = 0; i < 24; i++) {
            printf("0x%02X%s", g.colour[i], i + 1 < 24 ? ", " : "");
        }
        printf("},\n        {");
        for (int i = 0; i < 8; i++) {
            printf("0x%04X%s", g.dstBefore[i], i + 1 < 8 ? ", " : "");
        }
        printf("},\n        {");
        for (int i = 0; i < 8; i++) {
            printf("0x%04X%s", g.dstExpect[i], i + 1 < 8 ? ", " : "");
        }
        printf("},\n    },\n");
    }
    printf("};\n\n");
    printf("} // namespace blendopt\n");
    return 0;
}
