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

// Rows dirtied through the cache across the current frame's flushes, merged so
// presentFrameBuffer can be told the truth once instead of per area.
static lv_coord_t s_flushY0 = LV_COORD_MAX;
static lv_coord_t s_flushY1 = 0;

static volatile bool s_suppressFlush = false;

// Accumulated invalid region while suppressed. x1 > x2 encodes "empty".
static lv_area_t s_dirty = {1, 1, 0, 0};

static inline void dirtyReset() {
    s_dirty.x1 = 1;
    s_dirty.y1 = 1;
    s_dirty.x2 = 0;
    s_dirty.y2 = 0;
}

static inline bool dirtyEmpty() { return s_dirty.x1 > s_dirty.x2 || s_dirty.y1 > s_dirty.y2; }

void lvgl_helper_suppress_flush(bool suppress) {
    s_suppressFlush = suppress;
    // Whatever accumulated across a transition describes the other mode's
    // ownership of the panel and must not be carried over.
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
    s_flushY0 = LV_COORD_MAX;
    s_flushY1 = 0;
    // Resets inv_areas and invalidates the active screen, which is exactly what
    // taking the framebuffers back needs: both of them hold plasma, and the
    // full repaint this schedules is what LVGL then feeds to refr_sync_areas so
    // the second buffer is brought up to date before it is ever presented.
    lv_disp_drv_update(disp, &disp_drv);
}

bool lvgl_helper_take_dirty(lv_area_t *out) {
    if (out == nullptr || dirtyEmpty()) {
        return false;
    }
    *out = s_dirty;
    dirtyReset();
    return true;
}

/* Display flushing */
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    if (s_suppressFlush) {
        // Sleep animation owns the panel; keep LVGL rendering into the draw
        // buffer (so the content is current when flushing resumes) but don't
        // push — a flushed rect would flicker against the next plasma frame.
        // Record what was redrawn so the overlay can refresh just that much
        // instead of re-rendering the whole screen on a timer.
        if (dirtyEmpty()) {
            s_dirty = *area;
        } else {
            if (area->x1 < s_dirty.x1)
                s_dirty.x1 = area->x1;
            if (area->y1 < s_dirty.y1)
                s_dirty.y1 = area->y1;
            if (area->x2 > s_dirty.x2)
                s_dirty.x2 = area->x2;
            if (area->y2 > s_dirty.y2)
                s_dirty.y2 = area->y2;
        }
        lv_disp_flush_ready(disp_drv);
        return;
    }
    if (s_fbActive) {
        // Nothing to push: LVGL rendered into the framebuffer itself. What is
        // left is to make it the one the panel scans, and that is worth doing
        // once per frame rather than once per area, so the flip waits for the
        // last part. Until then the panel keeps showing the other buffer, which
        // is a whole and finished picture.
        if (area->y1 < s_flushY0) {
            s_flushY0 = area->y1;
        }
        if (area->y2 + 1 > s_flushY1) {
            s_flushY1 = area->y2 + 1;
        }
        if (lv_disp_flush_is_last(disp_drv)) {
            const int index = (color_p == s_fb[1]) ? 1 : 0;
            // The dirty range is the rows LVGL wrote through the cache, which
            // is what esp_lcd would have to write back. In bounce-buffer mode
            // it writes nothing back at all -- the refill memcpy reads the
            // framebuffer through the same cache -- but the range is still the
            // honest answer for a panel configured without bounce buffers.
            static_cast<Display *>(disp_drv->user_data)->presentFrameBuffer(index, s_flushY0, s_flushY1);
            s_flushY0 = LV_COORD_MAX;
            s_flushY1 = 0;
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
