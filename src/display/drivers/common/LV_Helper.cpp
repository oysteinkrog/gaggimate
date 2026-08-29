/**
 * @file      LV_Helper.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  Shenzhen Xin Yuan Electronic Technology Co.,
 * Ltd
 * @date      2024-01-22
 *
 */
#include "LV_Helper.h"
#ifdef GM_TOUCH_PROBE
#include "esp_log.h"
#include "esp_timer.h"
#endif

#if LV_VERSION_CHECK(9, 0, 0)
#error "Currently not supported 9.x"
#endif

static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_color_t *buf = NULL;
static lv_color_t *buf1 = NULL;

// Set when LVGL renders straight into the panel's own framebuffers instead of
// into draw buffers of its own.
//
// The copy this removes was the single largest source of visible corruption on
// the RGB panel. esp_lcd_panel_draw_bitmap has two branches: handed a pointer
// that is NOT one of its framebuffers it does a CPU copy of the rectangle into
// fbs[cur_fb_index] -- the buffer the scan-out is reading at that instant -- so
// every LVGL redraw raced the beam, and the bigger the redraw the worse it
// looked. Handed a pointer that IS one of its framebuffers it copies nothing:
// it records the index, and lcd_rgb_panel_fill_bounce_buffer picks the change
// up only when bounce_pos_px wraps, which is a frame boundary. Tearing stops
// being unlikely and becomes structurally impossible.
//
// It is also most of a screen transition's cost: the old path wrote every
// changed pixel to PSRAM twice, once when LVGL rendered it and once when the
// driver copied it, and PSRAM writes cost double on this part because the
// write-allocate line is fetched before it is overwritten.
static Display *s_board = nullptr;
static lv_color_t *s_fb[2] = {nullptr, nullptr};
static bool s_fbOwned = false;   // the panel framebuffers are LVGL's to render into
static bool s_fbActive = false;  // ...and LVGL holds them right now
static lv_color_t *s_scratch = nullptr;
static uint32_t s_scratchPx = 0;

static volatile bool s_suppressFlush = false;

// Accumulated invalid regions while suppressed, as a small list rather than
// one bounding box. The box was the display's 650 ms UI pass in disguise: a
// temperature label top-left and a status bar bottom-right union into the
// whole screen, so every overlay refresh re-rendered and re-scanned all
// 230,400 pixels no matter how few had changed.
static lv_area_t s_dirtyRects[GM_DIRTY_RECT_CAP];
static int s_dirtyN = 0;

static inline void dirtyReset() { s_dirtyN = 0; }

static inline bool dirtyEmpty() { return s_dirtyN == 0; }

static inline int64_t rectArea(const lv_area_t &a) {
    return static_cast<int64_t>(a.x2 - a.x1 + 1) * (a.y2 - a.y1 + 1);
}

static inline lv_area_t rectUnion(const lv_area_t &a, const lv_area_t &b) {
    lv_area_t u = a;
    if (b.x1 < u.x1)
        u.x1 = b.x1;
    if (b.y1 < u.y1)
        u.y1 = b.y1;
    if (b.x2 > u.x2)
        u.x2 = b.x2;
    if (b.y2 > u.y2)
        u.y2 = b.y2;
    return u;
}

static inline bool rectsTouch(const lv_area_t &a, const lv_area_t &b) {
    return !(b.x1 > a.x2 + 1 || b.x2 + 1 < a.x1 || b.y1 > a.y2 + 1 || b.y2 + 1 < a.y1);
}

void lvgl_helper_rect_add(lv_area_t *list, int *n, int cap, const lv_area_t &r) {
    if (r.x1 > r.x2 || r.y1 > r.y2) {
        return;
    }
    lv_area_t add = r;
    for (;;) {
        // Union into anything it overlaps or touches, then keep folding: the
        // grown rectangle may now reach entries it previously missed, and
        // leaving those separate would double-scan the overlap.
        for (int i = 0; i < *n;) {
            if (rectsTouch(list[i], add)) {
                add = rectUnion(list[i], add);
                list[i] = list[*n - 1];
                (*n)--;
                i = 0; // the union may reach entries already passed
                continue;
            }
            i++;
        }
        if (*n < cap) {
            list[(*n)++] = add;
            return;
        }
        // Full: pull out the entry whose bounding box grows least, fold it
        // into the candidate, and go around again -- the grown box may now
        // touch other entries, and appending without re-folding would leave
        // overlapping entries that get rendered twice.
        int best = 0;
        int64_t bestGrowth = INT64_MAX;
        for (int i = 0; i < *n; i++) {
            const int64_t growth = rectArea(rectUnion(list[i], add)) - rectArea(list[i]);
            if (growth < bestGrowth) {
                bestGrowth = growth;
                best = i;
            }
        }
        add = rectUnion(list[best], add);
        list[best] = list[*n - 1];
        (*n)--;
    }
}

void lvgl_helper_suppress_flush(bool suppress) {
    s_suppressFlush = suppress;
    // Whatever accumulated across a transition describes the other mode's
    // ownership of the panel and must not be carried over. Note this runs
    // before the same-state guard below, so a redundant call silently drops
    // unconsumed debt: call this only on real transitions (the current
    // callers in DefaultUI are all isActive()-guarded pairs).
    dirtyReset();

    if (!s_fbOwned || suppress == !s_fbActive) {
        return;
    }
    // Suppressing flushes is not enough once LVGL renders into the panel's own
    // framebuffers: dropping the flush stops the flip, but the rendering itself
    // would still be writing the memory the animation is drawing plasma into.
    // So hand the buffers over properly. LVGL spends the takeover rendering
    // into a small scratch buffer it never shows, which is what it was already
    // doing before, only into a screen-sized buffer instead of this one. The
    // areas it reports are the point: the overlay compositor needs to know what
    // the widgets did while it owned the screen.
    lv_disp_t *disp = lv_disp_get_default();
    if (disp == nullptr) {
        return;
    }
    if (suppress) {
        s_fbActive = false;
        lv_disp_draw_buf_init(&draw_buf, s_scratch, NULL, s_scratchPx);
        disp_drv.direct_mode = 0;
    } else {
        s_fbActive = true;
        lv_disp_draw_buf_init(&draw_buf, s_fb[0], s_fb[1], static_cast<uint32_t>(disp_drv.hor_res) * disp_drv.ver_res);
        disp_drv.direct_mode = 1;
    }
    // Resets inv_areas and invalidates the active screen, which is exactly what
    // taking the framebuffers back needs: both of them hold plasma, and the
    // full repaint this schedules is what LVGL then feeds to refr_sync_areas so
    // the second buffer is brought up to date before it is ever presented.
    lv_disp_drv_update(disp, &disp_drv);
}

int lvgl_helper_take_dirty_rects(lv_area_t *out, int maxN) {
    if (out == nullptr || maxN <= 0) {
        return 0;
    }
    int n = s_dirtyN < maxN ? s_dirtyN : maxN;
    for (int i = 0; i < n; i++) {
        out[i] = s_dirtyRects[i];
    }
    // maxN < s_dirtyN never happens with both sides sized GM_DIRTY_RECT_CAP,
    // but if a caller passes a smaller array, fold the tail into the last
    // entry rather than dropping debt.
    for (int i = n; i < s_dirtyN; i++) {
        out[n - 1] = rectUnion(out[n - 1], s_dirtyRects[i]);
    }
    dirtyReset();
    return n;
}

#ifdef GM_TOUCH_PROBE
// Touch-to-pixel latency probe, bench builds only. touchpad_read stamps the
// press and release edges as the indev first sees them; the present that
// carries the resulting redraw closes the interval. What it cannot see is
// the stage before software: the controller chip's own scan/report cadence
// sits between finger and edge, and the panel scan adds 0-20 ms after the
// flip. Those bounds are known; this measures the part the firmware owns.
volatile int64_t g_probeEdgeUs = 0;
volatile bool g_probeEdgeIsPress = false;
std::atomic<int64_t> g_probePublishUs{0};
std::atomic<bool> g_probePublishIsPress{false};
#endif

/* Display flushing */
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    if (s_suppressFlush) {
        // Sleep animation owns the panel; keep LVGL rendering into the draw
        // buffer (so the content is current when flushing resumes) but don't
        // push — a flushed rect would flicker against the next plasma frame.
        // Record what was redrawn so the overlay can refresh just that much
        // instead of re-rendering the whole screen on a timer.
        lvgl_helper_rect_add(s_dirtyRects, &s_dirtyN, GM_DIRTY_RECT_CAP, *area);
#ifdef GM_TOUCH_PROBE
        // The refresh stats say LVGL renders exactly the full screen every
        // cycle; these lines show the shape of what it actually flushes, to
        // identify the full-screen invalidator. 16 rects per 5 s window.
        {
            static int64_t flushLogWindowUs = 0;
            static int flushLogN = 0;
            const int64_t nowUs = esp_timer_get_time();
            if (nowUs - flushLogWindowUs > 5000000) {
                flushLogWindowUs = nowUs;
                flushLogN = 0;
            }
            if (flushLogN < 16) {
                flushLogN++;
                ESP_LOGI("TouchProbe", "GM_FLUSH: %d,%d..%d,%d", (int)area->x1, (int)area->y1, (int)area->x2,
                         (int)area->y2);
            }
        }
#endif
        lv_disp_flush_ready(disp_drv);
        return;
    }
    if (s_fbActive) {
        // Nothing to push: LVGL rendered into the framebuffer itself. What is
        // left is to make it the one the panel scans, and only once the frame
        // is finished. LVGL calls this once per unjoined invalid area, and the
        // buffer is not whole until the last of them; flipping early would put
        // a half-drawn frame on the panel. Until the flip the panel keeps
        // showing the other buffer, which is a finished picture.
        if (lv_disp_flush_is_last(disp_drv)) {
            const int index = (color_p == s_fb[1]) ? 1 : 0;
            // Whole screen, not `area`. In direct mode LVGL sets buf_area to
            // the full display before every flush and narrows only clip_area,
            // so `area` says nothing about what was redrawn and cannot be used
            // to bound a writeback. Which costs nothing here: the range only
            // feeds esp_lcd's cache writeback, and in bounce-buffer mode it
            // does not write back at all, because the refill memcpy reads the
            // framebuffer through the same cache the CPU just wrote it with.
            static_cast<Display *>(disp_drv->user_data)->presentFrameBuffer(index, 0, disp_drv->ver_res);
#ifdef GM_TOUCH_PROBE
            if (g_probeEdgeUs != 0) {
                const int64_t dt = esp_timer_get_time() - g_probeEdgeUs;
                g_probeEdgeUs = 0;
                ESP_LOGI("TouchProbe", "GM_TOUCHLAT: %s->present %lld us",
                         g_probeEdgeIsPress ? "press" : "release", (long long)dt);
            }
#endif
        }
        lv_disp_flush_ready(disp_drv);
        return;
    }
    static_cast<Display *>(disp_drv->user_data)->pushColors(area->x1, area->y1, area->x2 + 1, area->y2 + 1, (uint16_t *)color_p);
    lv_disp_flush_ready(disp_drv);
}

/*Read the touchpad*/
static void touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    static int16_t x, y;
    uint8_t touched = static_cast<Display *>(indev_driver->user_data)->getPoint(&x, &y, 1);
#ifdef GM_TOUCH_PROBE
    static bool wasTouched = false;
    if (touched != wasTouched) {
        wasTouched = touched;
        g_probeEdgeUs = esp_timer_get_time();
        g_probeEdgeIsPress = touched;
        // Force a small redraw on every edge so the interval always measures
        // the render pipeline. Without this a tap that hits nothing reactive
        // produces no redraw, and the probe closes on the next unrelated
        // widget update instead. The patch sits in the panel's top-left
        // corner, which the round bezel hides.
        lv_disp_t *d = lv_disp_get_default();
        if (d != nullptr) {
            lv_area_t a = {0, 0, 19, 19};
            _lv_inv_area(d, &a);
        }
    }
#endif
    if (touched) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
        return;
    }
    data->state = LV_INDEV_STATE_REL;
}

#if LV_USE_LOG
void lv_log_print_g_cb(const char *buf) {
    Serial.println(buf);
    Serial.flush();
}
#endif

String lvgl_helper_get_fs_filename(String filename) {
    static String path;
    path = String("A") + ":" + (filename);
    return path;
}

const char *lvgl_helper_get_fs_filename(const char *filename) {
    static String path;
    path = String("A") + ":" + String(filename);
    return path.c_str();
}

void beginLvglHelper(Display &board, bool debug) {

    lv_init();

#if LV_USE_LOG
    if (debug) {
        lv_log_register_print_cb(lv_log_print_g_cb);
    }
#endif

    const uint32_t screenPx = static_cast<uint32_t>(board.width()) * board.height();
    size_t lv_buffer_size = screenPx * sizeof(lv_color_t);

    // Two framebuffers the caller may write means LVGL can render into them and
    // never copy. Asked of the panel rather than read off supportsDirectMode(),
    // which answers a different question: the AMOLED panel sets it with a
    // single buffer and a real push, and must keep that path.
    s_board = &board;
    s_fb[0] = reinterpret_cast<lv_color_t *>(board.directFrameBuffer(0));
    s_fb[1] = reinterpret_cast<lv_color_t *>(board.directFrameBuffer(1));
    if (board.frameBufferCount() >= 2 && s_fb[0] != nullptr && s_fb[1] != nullptr) {
        // Small, because it is only ever rendered into while something else
        // owns the panel and nothing that lands in it is shown. LVGL needs it
        // to keep reporting redrawn areas to the overlay compositor; the pixels
        // are a byproduct. 40 rows is a partial buffer by LVGL's own sizing
        // guidance and costs a little over a tenth of a screen.
        s_scratchPx = static_cast<uint32_t>(board.width()) * 40;
        s_scratch = (lv_color_t *)ps_malloc(s_scratchPx * sizeof(lv_color_t));
    }
    if (s_scratch != nullptr) {
        s_fbOwned = true;
        s_fbActive = true;
        lv_disp_draw_buf_init(&draw_buf, s_fb[0], s_fb[1], screenPx);
    } else {
        // No usable pair, or no scratch to hand LVGL during a takeover. Either
        // way run the copying path whole rather than half of it: without the
        // scratch buffer the sleep animation and LVGL would render the same
        // memory at the same time.
        s_fb[0] = s_fb[1] = nullptr;
        buf = (lv_color_t *)ps_malloc(lv_buffer_size);
        assert(buf);

        if (!board.supportsDirectMode()) {
            buf1 = (lv_color_t *)ps_malloc(lv_buffer_size);
            assert(buf1);
        }

        lv_disp_draw_buf_init(&draw_buf, buf, buf1, screenPx);
    }

    /*Initialize the display*/
    lv_disp_drv_init(&disp_drv);
    /* display resolution */
    disp_drv.hor_res = board.width();
    disp_drv.ver_res = board.height();
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 0;
    disp_drv.direct_mode = s_fbOwned ? 1 : board.supportsDirectMode();
    disp_drv.user_data = &board;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    indev_drv.user_data = &board;
    lv_indev_drv_register(&indev_drv);
}
