#include "MeterTickCache.h"

#include <stdlib.h>
#include <string.h>
#ifndef GAGGIMATE_SIM
#include <esp_heap_caps.h>
#endif

extern "C" {
#include <draw/sw/lv_draw_sw.h>
#include <core/lv_refr.h>
}

namespace meterticks {
namespace {

constexpr int kSlots = 4;
constexpr int kMaxTicks = 64; // one bit per tick in Slot::built
constexpr int kMaxSide = 64;  // sprite side cap; the scratch is sized to it

struct Slot {
    Key key{};
    bool inUse = false;
    uint16_t side = 0; // sprite stride and height, fixed per meter
    // One PSRAM block per slot: cnt boxes, then cnt sprites of side^2
    // bytes. Nothing per tick lives in the internal pool (CLAUDE.md, DRAM
    // section: BSS is the same pool the WiFi TX copies come from).
    uint8_t *block = nullptr;
    size_t blockBytes = 0;
    lv_area_t *boxes = nullptr;
    uint8_t *sprites = nullptr;
    uint64_t built = 0;
};

Slot g_slots[kSlots];
int g_nextEvict = 0;
uint8_t *g_scratch = nullptr; // kMaxSide^2 RGB565+A8 pixels
uint32_t g_bytes = 0;

void *allocBig(size_t n) {
#ifdef GAGGIMATE_SIM
    return malloc(n);
#else
    // PSRAM only: the internal pool is the WiFi TX budget (CLAUDE.md, DRAM
    // section), and a sprite read once per tick per refresh does not need it.
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}

Slot *findOrCreate(const Key &key) {
    for (Slot &s : g_slots) {
        if (s.inUse && s.key == key) {
            return &s;
        }
    }
    // A meter that moved or was re-scaled leaves its old slot behind; reuse
    // that slot for the same object first, then a free one, then round-robin.
    Slot *victim = nullptr;
    for (Slot &s : g_slots) {
        if (s.inUse && s.key.obj == key.obj) {
            victim = &s;
            break;
        }
    }
    if (victim == nullptr) {
        for (Slot &s : g_slots) {
            if (!s.inUse) {
                victim = &s;
                break;
            }
        }
    }
    if (victim == nullptr) {
        victim = &g_slots[g_nextEvict];
        g_nextEvict = (g_nextEvict + 1) % kSlots;
    }
    // Pill ticks span tickLength along their axis with 2 px of margin each
    // side and 1 for rounding; dots are never wider than that.
    const int side = key.tickLength + 8;
    if (key.cnt == 0 || key.cnt > kMaxTicks || side > kMaxSide) {
        return nullptr;
    }
    const size_t boxBytes = static_cast<size_t>(key.cnt) * sizeof(lv_area_t);
    const size_t bytes = boxBytes + static_cast<size_t>(key.cnt) * side * side;
    if (victim->block != nullptr && victim->blockBytes != bytes) {
        free(victim->block);
        g_bytes -= static_cast<uint32_t>(victim->blockBytes);
        victim->block = nullptr;
        victim->blockBytes = 0;
    }
    if (victim->block == nullptr) {
        victim->block = static_cast<uint8_t *>(allocBig(bytes));
        if (victim->block == nullptr) {
            victim->inUse = false;
            return nullptr;
        }
        victim->blockBytes = bytes;
        g_bytes += static_cast<uint32_t>(bytes);
    }
    victim->boxes = reinterpret_cast<lv_area_t *>(victim->block);
    victim->sprites = victim->block + boxBytes;
    victim->key = key;
    victim->side = static_cast<uint16_t>(side);
    victim->built = 0;
    victim->inUse = true;
    return victim;
}

// Renders the tick through the caller's primitive into the scratch, with
// the same anti-aliasing the direct draw gets, and keeps only the coverage.
bool buildSprite(Slot &s, int i, const lv_area_t &box, RenderFn render, void *user) {
    const int w = lv_area_get_width(&box);
    const int h = lv_area_get_height(&box);
    if (w <= 0 || h <= 0 || w > s.side || h > s.side) {
        return false;
    }
    if (g_scratch == nullptr) {
        g_scratch = static_cast<uint8_t *>(allocBig(static_cast<size_t>(kMaxSide) * kMaxSide * 3));
        if (g_scratch == nullptr) {
            return false;
        }
        g_bytes += kMaxSide * kMaxSide * 3;
    }
    lv_disp_t *cur = _lv_refr_get_disp_refreshing();
    if (cur == nullptr || cur->driver == nullptr || cur->driver->draw_ctx_init == nullptr) {
        return false;
    }
    memset(g_scratch, 0, static_cast<size_t>(w) * h * 3);

    lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = static_cast<lv_coord_t>(w);
    drv.ver_res = static_cast<lv_coord_t>(h);
    drv.antialiasing = cur->driver->antialiasing;
    lv_disp_drv_use_generic_set_px_cb(&drv, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_disp_t fake;
    lv_memset_00(&fake, sizeof(lv_disp_t));
    fake.driver = &drv;

    lv_draw_ctx_t *ctx = static_cast<lv_draw_ctx_t *>(lv_mem_alloc(cur->driver->draw_ctx_size));
    if (ctx == nullptr) {
        return false;
    }
    cur->driver->draw_ctx_init(&drv, ctx);
    drv.draw_ctx = ctx;
    lv_area_t area = box;
    ctx->clip_area = &area;
    ctx->buf_area = &area;
    ctx->buf = g_scratch;

    _lv_refr_set_disp_refreshing(&fake);
    render(ctx, user);
    _lv_refr_set_disp_refreshing(cur);

    cur->driver->draw_ctx_deinit(&drv, ctx);
    lv_mem_free(ctx);

    uint8_t *sprite = s.sprites + static_cast<size_t>(i) * s.side * s.side;
    memset(sprite, 0, static_cast<size_t>(s.side) * s.side);
    for (int y = 0; y < h; y++) {
        const uint8_t *src = g_scratch + static_cast<size_t>(y) * w * 3 + 2;
        uint8_t *dst = sprite + static_cast<size_t>(y) * s.side;
        for (int x = 0; x < w; x++) {
            const uint8_t a = src[x * 3];
            // (255 * (a + 1)) >> 8 == a for a <= 254, so the blit reproduces
            // the alpha the direct draw wrote. 255 cannot come out of a
            // masked fill; keep it as is if it ever does.
            dst[x] = a == 0 ? 0 : (a == 255 ? 255 : static_cast<uint8_t>(a + 1));
        }
    }
    s.boxes[i] = box;
    s.built |= (1ull << i);
    return true;
}

} // namespace

bool draw(lv_draw_ctx_t *draw_ctx, const Key &key, int i, const lv_area_t &box, lv_color_t color, RenderFn render,
          void *user) {
    if (draw_ctx == nullptr || i < 0 || i >= kMaxTicks || lv_draw_mask_get_cnt() != 0) {
        return false;
    }
    Slot *s = findOrCreate(key);
    if (s == nullptr) {
        return false;
    }
    const bool have = (s->built & (1ull << i)) != 0;
    if (!have || s->boxes[i].x1 != box.x1 || s->boxes[i].y1 != box.y1 || s->boxes[i].x2 != box.x2 ||
        s->boxes[i].y2 != box.y2) {
        if (!buildSprite(*s, i, box, render, user)) {
            return false;
        }
    }
    lv_area_t maskArea = {box.x1, box.y1, static_cast<lv_coord_t>(box.x1 + s->side - 1),
                          static_cast<lv_coord_t>(box.y1 + s->side - 1)};
    lv_draw_sw_blend_dsc_t d;
    lv_memset_00(&d, sizeof(d));
    d.blend_area = &box;
    d.mask_buf = s->sprites + static_cast<size_t>(i) * s->side * s->side;
    d.mask_area = &maskArea;
    d.mask_res = LV_DRAW_MASK_RES_CHANGED;
    d.color = color;
    d.opa = LV_OPA_COVER;
    d.blend_mode = LV_BLEND_MODE_NORMAL;
    lv_draw_sw_blend(draw_ctx, &d);
    return true;
}

uint32_t bytesAllocated() { return g_bytes; }

} // namespace meterticks
