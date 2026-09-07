// Host checks for tickring::compositeRow and friends (gm-2cl.6).
//
// The reference for exactness is the path the ring took before the element:
// MeterTickCache's blit through lv_draw_sw_blend with set_px_cb writes the
// overlay pixel (colour, alpha (255 * s) >> 8) for sprite byte s, and the
// overlay blend then does dst = a == 255 ? c : blend565(c, dst, a). Both
// steps are reproduced here independently of the implementation under test.
#include <display/ui/default/TickRingElement.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

using tickring::Box;
using tickring::Sprites;

static int g_fail = 0;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            g_fail++;                                                                                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static uint16_t refBlend(uint16_t fg, uint16_t bg, uint8_t a) {
    const uint32_t inv = 256u - a;
    const uint32_t r = (((fg & 0xF800u) * a) + ((bg & 0xF800u) * inv)) >> 8;
    const uint32_t g = (((fg & 0x07E0u) * a) + ((bg & 0x07E0u) * inv)) >> 8;
    const uint32_t b = (((fg & 0x001Fu) * a) + ((bg & 0x001Fu) * inv)) >> 8;
    return static_cast<uint16_t>((r & 0xF800u) | (g & 0x07E0u) | (b & 0x001Fu));
}

// A ring of cnt ticks on a W x H panel: boxes around a circle, sprites
// filled with random coverage (0 is common, 255 present).
struct Ring {
    int cnt, side, W, H;
    std::vector<Box> boxes;
    std::vector<uint8_t> sprites;
    std::vector<uint8_t> spans;
    std::vector<uint64_t> rowMask;
    Sprites view;
    Ring(int cnt_, int side_, int W_, int H_, std::mt19937 &rng) : cnt(cnt_), side(side_), W(W_), H(H_) {
        boxes.resize(cnt);
        sprites.assign(static_cast<size_t>(cnt) * side * side, 0);
        std::uniform_int_distribution<int> cov(0, 300);
        for (int i = 0; i < cnt; i++) {
            const double ang = 6.2831853 * i / cnt;
            const int cx = W / 2 + static_cast<int>(200 * cos(ang));
            const int cy = H / 2 + static_cast<int>(200 * sin(ang));
            const int bw = side - 2 - (i % 3); // boxes narrower than the stride, like the real cache
            const int bh = side - 1 - (i % 2);
            boxes[i] = Box{static_cast<int16_t>(cx - bw / 2), static_cast<int16_t>(cy - bh / 2),
                           static_cast<int16_t>(cx - bw / 2 + bw - 1), static_cast<int16_t>(cy - bh / 2 + bh - 1)};
            for (int y = 0; y < bh; y++) {
                for (int x = 0; x < bw; x++) {
                    int v = cov(rng);
                    if (v > 255) {
                        v = (v & 1) ? 0 : 255;
                    }
                    sprites[static_cast<size_t>(i) * side * side + static_cast<size_t>(y) * side + x] =
                        static_cast<uint8_t>(v);
                }
            }
        }
        spans.assign(static_cast<size_t>(cnt) * side * 2, 0);
        for (int i = 0; i < cnt; i++) {
            tickring::buildRowSpans(sprites.data() + static_cast<size_t>(i) * side * side, side,
                                    spans.data() + static_cast<size_t>(i) * side * 2);
        }
        view.cnt = static_cast<uint16_t>(cnt);
        view.side = static_cast<uint16_t>(side);
        view.boxes = boxes.data();
        view.sprites = sprites.data();
        view.spans = spans.data();
        rowMask.assign(static_cast<size_t>(H), 0);
        tickring::buildRowMask(view, H, rowMask.data());
        view.rowMask = rowMask.data();
        view.maskRows = static_cast<uint16_t>(H);
    }
};

// Reference: paint the whole ring into a W x H frame the LVGL-plus-overlay way.
static void refFrame(const Ring &r, uint16_t lit, uint16_t unlit, int lo, int hi, std::vector<uint16_t> &fb) {
    for (int i = 0; i < r.cnt; i++) {
        const uint16_t c = (i >= lo && i < hi) ? lit : unlit;
        const Box &b = r.boxes[i];
        for (int y = b.y1; y <= b.y2; y++) {
            if (y < 0 || y >= r.H) {
                continue;
            }
            for (int x = b.x1; x <= b.x2; x++) {
                if (x < 0 || x >= r.W) {
                    continue;
                }
                const uint8_t s =
                    r.sprites[static_cast<size_t>(i) * r.side * r.side + static_cast<size_t>(y - b.y1) * r.side + (x - b.x1)];
                if (s == 0) {
                    continue;
                }
                const uint32_t a = (255u * s) >> 8; // what the masked fill writes to the overlay
                uint16_t &d = fb[static_cast<size_t>(y) * r.W + x];
                d = a == 255 ? c : refBlend(c, d, static_cast<uint8_t>(a));
            }
        }
    }
}

int main() {
    std::mt19937 rng(20260907);
    const int W = 480, H = 480;
    const uint16_t lit = 0xFD20, unlit = 0x4208;

    // 1. At rest (integer boundaries) the row compositor equals the reference,
    //    for every lit range shape: none, prefix, suffix, middle, all.
    {
        Ring r(62, 33, W, H, rng);
        const int ranges[][2] = {{0, 0}, {0, 20}, {40, 62}, {10, 30}, {0, 62}, {61, 62}};
        for (const auto &rg : ranges) {
            std::vector<uint16_t> bg(static_cast<size_t>(W) * H);
            for (auto &p : bg) {
                p = static_cast<uint16_t>(rng());
            }
            std::vector<uint16_t> ref = bg, got = bg, full = bg;
            refFrame(r, lit, unlit, rg[0], rg[1], ref);
            Sprites noSpans = r.view; // the fallback path scans every box row
            noSpans.spans = nullptr;
            noSpans.rowMask = nullptr;
            for (int y = 0; y < H; y++) {
                tickring::compositeRow(&got[static_cast<size_t>(y) * W], y, W, r.view, lit, unlit,
                                       static_cast<float>(rg[0]), static_cast<float>(rg[1]));
                tickring::compositeRow(&full[static_cast<size_t>(y) * W], y, W, noSpans, lit, unlit,
                                       static_cast<float>(rg[0]), static_cast<float>(rg[1]));
            }
            size_t diff = 0, diffFull = 0;
            for (size_t k = 0; k < ref.size(); k++) {
                diff += ref[k] != got[k];
                diffFull += ref[k] != full[k];
            }
            CHECK(diff == 0, "rest lo=%d hi=%d: %zu pixels differ", rg[0], rg[1], diff);
            CHECK(diffFull == 0, "rest (no spans) lo=%d hi=%d: %zu pixels differ", rg[0], rg[1], diffFull);
        }
    }

    // 2. Ticks partly off the panel: no out-of-bounds write (ASan) and the
    //    on-panel pixels still match.
    {
        Ring r(24, 33, 400, 400, rng);
        for (auto &b : r.boxes) {
            b.x1 = static_cast<int16_t>(b.x1 - 200);
            b.x2 = static_cast<int16_t>(b.x2 - 200);
            b.y1 = static_cast<int16_t>(b.y1 + 200);
            b.y2 = static_cast<int16_t>(b.y2 + 200);
        }
        tickring::buildRowMask(r.view, r.H, r.rowMask.data()); // boxes moved: the mask follows
        std::vector<uint16_t> ref(static_cast<size_t>(r.W) * r.H, 0x1234), got = ref;
        refFrame(r, lit, unlit, 5, 17, ref);
        for (int y = 0; y < r.H; y++) {
            tickring::compositeRow(&got[static_cast<size_t>(y) * r.W], y, r.W, r.view, lit, unlit, 5.0f, 17.0f);
        }
        CHECK(ref == got, "clipped ring differs");
    }

    // 3. Row independence: a row composited alone equals the same row inside
    //    a full pass (the interlaced path composites single rows).
    {
        Ring r(25, 33, W, H, rng);
        std::vector<uint16_t> full(static_cast<size_t>(W) * H, 0x8410), single = full;
        for (int y = 0; y < H; y++) {
            tickring::compositeRow(&full[static_cast<size_t>(y) * W], y, W, r.view, lit, unlit, 3.4f, 18.7f);
        }
        for (int y = H - 1; y >= 0; y -= 7) {
            tickring::compositeRow(&single[static_cast<size_t>(y) * W], y, W, r.view, lit, unlit, 3.4f, 18.7f);
            CHECK(memcmp(&full[static_cast<size_t>(y) * W], &single[static_cast<size_t>(y) * W], W * 2) == 0,
                  "row %d depends on other rows", y);
        }
    }

    // 4. The lit rule and the boundary colour.
    {
        CHECK(tickring::litFraction(4, 2.0f, 7.0f) == 1.0f, "inside");
        CHECK(tickring::litFraction(1, 2.0f, 7.0f) == 0.0f, "below");
        CHECK(tickring::litFraction(7, 2.0f, 7.0f) == 0.0f, "at hi");
        CHECK(std::abs(tickring::litFraction(6, 2.0f, 6.5f) - 0.5f) < 1e-6f, "half at hi");
        CHECK(std::abs(tickring::litFraction(2, 2.25f, 7.0f) - 0.75f) < 1e-6f, "three quarters at lo");
        CHECK(tickring::tickColor(3, 0.0f, 10.0f, lit, unlit) == lit, "lit colour exact");
        CHECK(tickring::tickColor(3, 5.0f, 10.0f, lit, unlit) == unlit, "unlit colour exact");
        const uint16_t mid = tickring::tickColor(5, 0.0f, 5.5f, lit, unlit);
        CHECK(mid != lit && mid != unlit, "boundary tick is a mix");
        CHECK(mid == tickring::blend565(lit, unlit, 128), "boundary mix is blend565 at 128");
    }

    // 4b. The page gain scales coverage the way blendRow<true> does:
    //     a = (a * gain) >> 8 before the blend, and nothing at gain 0.
    {
        Ring r(25, 33, W, H, rng);
        std::vector<uint16_t> bg(static_cast<size_t>(W) * H, 0x2104);
        std::vector<uint16_t> ref = bg, got = bg, zero = bg;
        const uint32_t gain = 96;
        for (int i = 0; i < r.cnt; i++) {
            const uint16_t c = (i >= 3 && i < 12) ? lit : unlit;
            const Box &b = r.boxes[i];
            for (int y = b.y1; y <= b.y2; y++) {
                for (int x = b.x1; x <= b.x2; x++) {
                    const uint8_t s = r.sprites[static_cast<size_t>(i) * r.side * r.side +
                                                static_cast<size_t>(y - b.y1) * r.side + (x - b.x1)];
                    if (s == 0) {
                        continue;
                    }
                    const uint32_t a = (((255u * s) >> 8) * gain) >> 8;
                    if (a == 0) {
                        continue;
                    }
                    uint16_t &d = ref[static_cast<size_t>(y) * W + x];
                    d = a == 255 ? c : refBlend(c, d, static_cast<uint8_t>(a));
                }
            }
        }
        for (int y = 0; y < H; y++) {
            tickring::compositeRow(&got[static_cast<size_t>(y) * W], y, W, r.view, lit, unlit, 3.0f, 12.0f, gain);
            tickring::compositeRow(&zero[static_cast<size_t>(y) * W], y, W, r.view, lit, unlit, 3.0f, 12.0f, 0);
        }
        CHECK(ref == got, "gain 96 differs from the scaled reference");
        CHECK(zero == bg, "gain 0 painted something");
    }

    // 4c. The span table brackets exactly the non-zero bytes of each row,
    //     with a sparse sprite (a diagonal) and an empty row.
    {
        const int side = 8;
        std::vector<uint8_t> sp(side * side, 0), spans(side * 2, 0xEE);
        for (int y = 0; y < side; y++) {
            if (y == 3) {
                continue; // empty row
            }
            sp[y * side + y] = 100;
            if (y + 2 < side) {
                sp[y * side + y + 2] = 7;
            }
        }
        tickring::buildRowSpans(sp.data(), side, spans.data());
        for (int y = 0; y < side; y++) {
            int first = side, last = 0;
            for (int x = 0; x < side; x++) {
                if (sp[y * side + x] != 0) {
                    first = first < x ? first : x;
                    last = x + 1;
                }
            }
            if (first >= last) {
                CHECK(spans[y * 2] >= spans[y * 2 + 1], "row %d should be empty", y);
            } else {
                CHECK(spans[y * 2] == first && spans[y * 2 + 1] == last, "row %d span %d..%d, got %d..%d", y, first,
                      last, spans[y * 2], spans[y * 2 + 1]);
            }
        }
    }

    // 5. bounds is the union of the boxes.
    {
        Ring r(25, 33, W, H, rng);
        Box bb;
        CHECK(tickring::bounds(r.view, bb), "bounds");
        for (const Box &b : r.boxes) {
            CHECK(b.x1 >= bb.x1 && b.y1 >= bb.y1 && b.x2 <= bb.x2 && b.y2 <= bb.y2, "box outside bounds");
        }
        Sprites empty;
        CHECK(!tickring::bounds(empty, bb), "empty bounds");
    }

    if (g_fail == 0) {
        printf("tickringbench: PASS\n");
        return 0;
    }
    printf("tickringbench: %d FAILED\n", g_fail);
    return 1;
}
