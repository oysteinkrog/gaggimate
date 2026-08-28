"""Patch the ESP-IDF RGB LCD driver so a blocked interrupt stops displacing the panel.

The fault
---------
In bounce-buffer mode ``lcd_rgb_panel_eof_handler`` refills exactly one bounce
buffer per callback, and the GDMA end-of-frame interrupt is a latched flag
rather than a counter (``esp_hw_support/dma/gdma.c`` reads an ``intr_status``
bitmask, clears it once, and runs the callback once).  Two EOFs arriving while
that handler is blocked therefore collapse into one callback.  The driver ends
the frame having counted fewer EOFs than it expected, and the VSYNC handler
responds by resetting the DMA and restarting the transmission.  That restart is
what reaches the panel as a block of lines displaced vertically; upstream
documents the side effect in a comment above
``lcd_rgb_panel_try_restart_transmission``.

The fix, in three parts
-----------------------
1. Ask the DMA where it is.  ``GDMA_OUT_DSCR_BF0_CHn`` tracks the descriptor the
   channel has moved past and keeps updating whether or not the interrupt was
   serviced, so the change in it between two callbacks says how many buffers
   were really consumed.  Refill that many and a coalesced interrupt costs
   nothing.  The reading is modulo the pool size, which is exactly enough: a lag
   of a whole pool or more has already drained every buffer, so no refill could
   have rescued it either way.

2. Square up against the beam at VSYNC instead of restarting.  A frame is
   exactly ``expect_eof_count`` bounce buffers, which is a hard invariant, so a
   frame that counted fewer knows precisely how many it missed.  Advancing the
   fill position over that many buffers without copying re-anchors the driver to
   the beam using arithmetic that cannot be wrong, and costs one frame of stale
   pixels in the band that was missed.  Restarting the DMA is neither necessary
   nor safe here: the DMA reads bounce buffers out of internal SRAM and is never
   itself starved, so its position stays locked to the beam for as long as the
   panel runs.  Only the software fill position can drift, and only software
   needs correcting.

3. Eight bounce buffers rather than two, at identical total memory.  Two give
   the refill a single buffer of slack, and two is also the point where the
   residue in part 1 carries no information at all.  The pool always holds the
   same 16 scanlines however it is divided, so what splitting it buys is the
   fraction of that time the refill may use: a refill must land before the DMA
   works through every other buffer, which is (N-1)/N of the pool.  Eight raises
   that from three quarters to seven eighths, at twice the interrupt rate.

The patch is applied to the framework package in place, because PlatformIO
builds ESP-IDF components from the shared package directory.  It is idempotent
and every hunk is anchored on exact upstream text, so an IDF upgrade that moves
this code fails the build loudly rather than silently reverting the fix.
"""

import os

# Eight buffers of GM_LCD_BOUNCE_LINES scanlines each. Keep lines x buffers at
# 15,360 bytes: platformio.ini documents the WiFi cliff that budget sits above,
# and works through what splitting the same bytes more ways buys and costs.
BOUNCE_BUF_NUM = 8

MARKER = "GM_RGB_CATCHUP_PATCH"
M = MARKER

HUNKS = [
    # ---- 1. buffer count ---------------------------------------------------
    (
        "#define RGB_LCD_PANEL_BOUNCE_BUF_NUM     2 // bounce buffer number",
        f"// {M}: four buffers, not two. Two give the refill one buffer of slack,\n"
        "// and two is also the point at which the DMA-position residue below carries no\n"
        "// information: a DMA that ran a full pool ahead reads identically to one that\n"
        "// has not moved.\n"
        f"#define RGB_LCD_PANEL_BOUNCE_BUF_NUM     {BOUNCE_BUF_NUM} // bounce buffer number",
    ),
    # ---- 2. includes -------------------------------------------------------
    (
        '#include "hal/dma_types.h"',
        '#include "hal/dma_types.h"\n'
        f'#include "soc/gdma_reg.h"      // {M}: live TX descriptor register\n'
        f'#include "esp_memory_utils.h"  // {M}: sanity-check that register\n'
        f'#include "esp_cpu.h"           // {M}: separate a slow refill from a blocked one\n'
        f'#include "esp_rom_sys.h"       // {M}: cycles per microsecond',
    ),
    # ---- 3. counters -------------------------------------------------------
    (
        'static const char *TAG = "lcd_panel.rgb";',
        f"// {M}: scan-out counters, read by the application over\n"
        "// /api/debug/scanout so the effect of this patch is measured and not assumed.\n"
        "volatile uint32_t gm_rgb_restart_count = 0;  // full DMA restarts, which displace the panel\n"
        "volatile uint32_t gm_rgb_catchup_count = 0;  // callbacks that found the DMA had run ahead\n"
        "volatile uint32_t gm_rgb_catchup_bufs = 0;   // extra buffers refilled to catch up\n"
        "volatile uint32_t gm_rgb_catchup_max = 0;    // deepest lag recovered, in buffers\n"
        "volatile uint32_t gm_rgb_resync_count = 0;   // frames squared up against the beam at VSYNC\n"
        "volatile uint32_t gm_rgb_resync_bufs = 0;    // buffers those frames had missed\n"
        "volatile uint32_t gm_rgb_resync_max = 0;     // worst single frame, in buffers\n"
        "volatile uint32_t gm_rgb_over_count = 0;     // frames that counted MORE than a frame holds\n"
        "volatile uint32_t gm_rgb_over_bufs = 0;      // excess buffers those frames were rewound by\n"
        "volatile uint32_t gm_rgb_flash_skip_bufs = 0; // buffers advanced uncopied while the flash cache was down\n"
        "\n"
        f"// {M}: raised by scripts/patch_flash_cache_flag.py's patch on\n"
        "// spi_flash/cache_utils.c while a flash operation has the cache down. While it\n"
        "// is up the PSRAM framebuffer is unreadable, so the EOF handler advances past\n"
        "// buffers without copying instead of faulting. Only meaningful once\n"
        "// CONFIG_LCD_RGB_ISR_IRAM_SAFE lets this handler run in that window at all;\n"
        "// linking fails loudly here if the cache_utils patch did not apply.\n"
        "extern volatile bool gm_flash_cache_down;\n"
        "volatile uint32_t gm_rgb_eof_expect = 0;     // buffers in one frame\n"
        "volatile uint32_t gm_rgb_eof_last = 0;       // buffers the last frame counted\n"
        "volatile uint32_t gm_rgb_eof_min = 0xFFFFFFFFu;\n"
        "volatile uint32_t gm_rgb_eof_max = 0;\n"
        "\n"
        "// Is the refill SLOW or is it BLOCKED? The two look identical from the frame\n"
        "// counters and want opposite fixes: a slow refill is PSRAM bandwidth and wants\n"
        "// less traffic, a blocked one is interrupt latency and wants a higher priority.\n"
        "// gm_rgb_busy is how long the handler spends copying, gm_rgb_gap is how long it\n"
        "// waited to be called. Both in microseconds, bucketed by GM_RGB_HIST_US.\n"
        "#define GM_RGB_HIST_N 24\n"
        "#define GM_RGB_HIST_US 32\n"
        "volatile uint32_t gm_rgb_busy_hist[GM_RGB_HIST_N] = {0};\n"
        "volatile uint32_t gm_rgb_gap_hist[GM_RGB_HIST_N] = {0};\n"
        "volatile uint32_t gm_rgb_busy_max = 0;\n"
        "volatile uint32_t gm_rgb_gap_max = 0;\n"
        "volatile uint32_t gm_rgb_cyc_per_us = 240;\n"
        "\n"
        "// Implemented by the application (PanelClock.cpp) so an event can be logged\n"
        "// against whatever else was running. Weak so the driver still links without it.\n"
        "extern void gm_rgb_restart_hook(uint32_t missed) __attribute__((weak));\n"
        "\n"
        'static const char *TAG = "lcd_panel.rgb";',
    ),
    # ---- 4. panel state ----------------------------------------------------
    (
        "    size_t expect_eof_count;        // record the number of DMA EOF event we expected to receive",
        "    size_t expect_eof_count;        // record the number of DMA EOF event we expected to receive\n"
        f"    int gm_dma_chan_id;             // {M}: GDMA TX channel, -1 if unknown\n"
        f"    int gm_last_dma_idx;            // {M}: bounce buffer the DMA was on last callback, -1 if unknown\n"
        f"    size_t gm_eof_frame;            // {M}: buffers counted since the last VSYNC\n"
        f"    uint32_t gm_last_entry_cyc;     // {M}: cycle count at the previous handler entry",
    ),
    # ---- 5. capture the GDMA channel index ---------------------------------
    (
        '    ESP_RETURN_ON_ERROR(LCD_GDMA_NEW_CHANNEL(&dma_chan_config, &rgb_panel->dma_chan), TAG, "alloc DMA channel failed");\n'
        "    gdma_connect(rgb_panel->dma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0));",

        '    ESP_RETURN_ON_ERROR(LCD_GDMA_NEW_CHANNEL(&dma_chan_config, &rgb_panel->dma_chan), TAG, "alloc DMA channel failed");\n'
        "    gdma_connect(rgb_panel->dma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0));\n"
        f"    // {M}: remember which TX channel this is so the EOF handler can read\n"
        "    // its live descriptor register. Only AHB group 0 has the register layout assumed\n"
        "    // below; anything else leaves the catch-up off and stock behaviour in place.\n"
        "    rgb_panel->gm_dma_chan_id = -1;\n"
        "    rgb_panel->gm_last_dma_idx = -1;\n"
        "    {\n"
        "        int gm_group_id = -1;\n"
        "        int gm_chan_id = -1;\n"
        "        if (gdma_get_group_channel_id(rgb_panel->dma_chan, &gm_group_id, &gm_chan_id) == ESP_OK && gm_group_id == 0) {\n"
        "            rgb_panel->gm_dma_chan_id = gm_chan_id;\n"
        "        } else {\n"
        f'            ESP_LOGW(TAG, "{M}: no live DMA position, EOF catch-up disabled");\n'
        "        }\n"
        "    }",
    ),
    # ---- 6. a fill that can advance the position without copying -----------
    (
        "static IRAM_ATTR bool lcd_rgb_panel_fill_bounce_buffer(esp_rgb_panel_t *panel, uint8_t *buffer)\n"
        "{\n"
        "    bool need_yield = false;\n"
        "    int bytes_per_pixel = panel->fb_bits_per_pixel / 8;\n"
        "    if (unlikely(panel->num_fbs == 0)) {",

        f"// {M}: `copy` false advances the driver's position past a buffer\n"
        "// without rewriting it. Two callers need that. The EOF handler needs it when the\n"
        "// DMA has drained the whole pool, because the oldest buffer in the backlog is\n"
        "// the one being read right now and copying into it would tear. The VSYNC handler\n"
        "// needs it to skip over buffers whose moment on screen has already passed.\n"
        "// Either way the position must still move: not moving it puts every later buffer\n"
        "// at the wrong scanline for the rest of the frame.\n"
        "static IRAM_ATTR bool lcd_rgb_panel_fill_bounce_buffer_ex(esp_rgb_panel_t *panel, uint8_t *buffer, bool copy)\n"
        "{\n"
        "    bool need_yield = false;\n"
        "    int bytes_per_pixel = panel->fb_bits_per_pixel / 8;\n"
        "    if (!copy) {\n"
        "        goto advance;\n"
        "    }\n"
        "    if (unlikely(panel->num_fbs == 0)) {",
    ),
    (
        "    panel->bounce_pos_px += panel->bb_size / bytes_per_pixel;",
        "advance:\n"
        "    panel->bounce_pos_px += panel->bb_size / bytes_per_pixel;",
    ),
    (
        "    // Preload the next bit of buffer to the cache memory, this can improve the performance\n"
        "    if (panel->num_fbs > 0 && panel->flags.fb_behind_cache) {",
        "    // Preload the next bit of buffer to the cache memory, this can improve the performance\n"
        "    if (copy && panel->num_fbs > 0 && panel->flags.fb_behind_cache) {",
    ),
    # ---- 7. position reader, and the catch-up itself -----------------------
    (
        "static IRAM_ATTR bool lcd_rgb_panel_eof_handler(gdma_channel_handle_t dma_chan, gdma_event_data_t *event_data, void *user_data)\n"
        "{\n"
        "    bool need_yield = false;\n"
        "    esp_rgb_panel_t *rgb_panel = (esp_rgb_panel_t *)user_data;\n"
        "\n"
        "    if (rgb_panel->bb_size) {\n"
        "        // in bounce buffer mode, the DMA EOF means time to fill the finished bounce buffer\n"
        "        // Figure out which bounce buffer to write to\n"
        "        portENTER_CRITICAL_ISR(&rgb_panel->spinlock);\n"
        "        int bb = rgb_panel->bb_eof_count % RGB_LCD_PANEL_BOUNCE_BUF_NUM;\n"
        "        rgb_panel->bb_eof_count++;\n"
        "        portEXIT_CRITICAL_ISR(&rgb_panel->spinlock);\n"
        "        need_yield = lcd_rgb_panel_fill_bounce_buffer(rgb_panel, rgb_panel->bounce_buffer[bb]);\n"
        "    } else {",

        f"// {M}: the ordinary full copy, so existing call sites read unchanged.\n"
        "static IRAM_ATTR bool lcd_rgb_panel_fill_bounce_buffer(esp_rgb_panel_t *panel, uint8_t *buffer)\n"
        "{\n"
        "    return lcd_rgb_panel_fill_bounce_buffer_ex(panel, buffer, true);\n"
        "}\n"
        "\n"
        f"// {M}: advance the fill position over `n` buffers without writing them.\n"
        "static IRAM_ATTR bool gm_rgb_skip_buffers(esp_rgb_panel_t *panel, uint32_t n)\n"
        "{\n"
        "    bool need_yield = false;\n"
        "    for (uint32_t i = 0; i < n; i++) {\n"
        "        portENTER_CRITICAL_ISR(&panel->spinlock);\n"
        "        int bb = panel->bb_eof_count % RGB_LCD_PANEL_BOUNCE_BUF_NUM;\n"
        "        panel->bb_eof_count++;\n"
        "        portEXIT_CRITICAL_ISR(&panel->spinlock);\n"
        "        if (lcd_rgb_panel_fill_bounce_buffer_ex(panel, panel->bounce_buffer[bb], false)) {\n"
        "            need_yield = true;\n"
        "        }\n"
        "    }\n"
        "    return need_yield;\n"
        "}\n"
        "\n"
        f"// {M}: which bounce buffer is the DMA working on right now?\n"
        "//\n"
        "// GDMA_OUT_DSCR_BF0_CHn holds the descriptor the TX channel most recently moved\n"
        "// past. Unlike OUT_EOF_DES_ADDR it is not latched by the EOF interrupt, so it\n"
        "// still reads true after an interrupt was missed. Only the change between two\n"
        "// readings is ever used, so the register's one-descriptor lag behind the\n"
        "// channel's true position cancels out and needs no correcting.\n"
        "//\n"
        "// The descriptor carries its own buffer pointer, so mapping it back to a bounce\n"
        "// buffer needs no assumption about the link list's item stride. Returns -1\n"
        "// whenever the answer is not trustworthy; the caller then behaves exactly like\n"
        "// the unpatched driver.\n"
        "static IRAM_ATTR int gm_rgb_dma_buf_index(esp_rgb_panel_t *panel)\n"
        "{\n"
        "#if CONFIG_IDF_TARGET_ESP32S3\n"
        "    if (panel->gm_dma_chan_id < 0) {\n"
        "        return -1;\n"
        "    }\n"
        "    // GDMA_OUT_DSCR_BF0_CH1_REG - GDMA_OUT_DSCR_BF0_CH0_REG is the channel stride.\n"
        "    uint32_t dscr = REG_READ(GDMA_OUT_DSCR_BF0_CH0_REG +\n"
        "                             (uint32_t)panel->gm_dma_chan_id *\n"
        "                             (GDMA_OUT_DSCR_BF0_CH1_REG - GDMA_OUT_DSCR_BF0_CH0_REG));\n"
        "    void *dscr_ptr = (void *)(uintptr_t)dscr;\n"
        "    if ((dscr & 0x3) != 0 || !esp_ptr_internal(dscr_ptr)) {\n"
        "        return -1;\n"
        "    }\n"
        "    uint8_t *buf = (uint8_t *)(((const dma_descriptor_t *)dscr_ptr)->buffer);\n"
        "    for (int i = 0; i < RGB_LCD_PANEL_BOUNCE_BUF_NUM; i++) {\n"
        "        if (buf >= panel->bounce_buffer[i] && buf < panel->bounce_buffer[i] + panel->bb_size) {\n"
        "            return i;\n"
        "        }\n"
        "    }\n"
        "#endif\n"
        "    return -1;\n"
        "}\n"
        "\n"
        "static IRAM_ATTR bool lcd_rgb_panel_eof_handler(gdma_channel_handle_t dma_chan, gdma_event_data_t *event_data, void *user_data)\n"
        "{\n"
        "    bool need_yield = false;\n"
        "    esp_rgb_panel_t *rgb_panel = (esp_rgb_panel_t *)user_data;\n"
        "\n"
        "    if (rgb_panel->bb_size) {\n"
        "        // in bounce buffer mode, the DMA EOF means time to fill the finished bounce buffer\n"
        "        //\n"
        f"        // {M}: one callback does not always mean one buffer. The EOF\n"
        "        // interrupt is a latched flag, so two EOFs arriving while this handler is\n"
        "        // blocked produce a single callback. Ask the DMA where it actually got to\n"
        "        // and refill every buffer it has passed.\n"
        "        const uint32_t gm_entry_cyc = esp_cpu_get_cycle_count();\n"
        "        if (rgb_panel->gm_last_entry_cyc) {\n"
        "            uint32_t g = (gm_entry_cyc - rgb_panel->gm_last_entry_cyc) / gm_rgb_cyc_per_us;\n"
        "            if (g > gm_rgb_gap_max) {\n"
        "                gm_rgb_gap_max = g;\n"
        "            }\n"
        "            uint32_t b = g / GM_RGB_HIST_US;\n"
        "            gm_rgb_gap_hist[b < GM_RGB_HIST_N ? b : GM_RGB_HIST_N - 1]++;\n"
        "        }\n"
        "        rgb_panel->gm_last_entry_cyc = gm_entry_cyc;\n"
        "        int fills = 1;\n"
        "        int idx = gm_rgb_dma_buf_index(rgb_panel);\n"
        "        if (idx >= 0) {\n"
        "            if (rgb_panel->gm_last_dma_idx >= 0) {\n"
        "                int advanced = idx - rgb_panel->gm_last_dma_idx;\n"
        "                if (advanced < 0) {\n"
        "                    advanced += RGB_LCD_PANEL_BOUNCE_BUF_NUM;\n"
        "                }\n"
        "                // The register delta IS the count, zero included. Flooring it at\n"
        "                // one per callback double-counts: when the EOF interrupt beats the\n"
        "                // register update, that callback fills one buffer against a stale\n"
        "                // baseline and the next callback's delta counts the same buffer\n"
        "                // again. Each double count walks the fill position one buffer\n"
        "                // AHEAD of the beam, the one direction the VSYNC square-up did not\n"
        "                // correct, so the error accumulated: a stable vertical wrap of the\n"
        "                // whole picture, two lines deeper per event, top of the image\n"
        "                // drawing at the bottom of the panel (over_count climbing while\n"
        "                // resyncs stayed calm was the counter signature). A genuine zero\n"
        "                // costs nothing: the buffer that raised this EOF is filled by\n"
        "                // whichever callback first sees its register movement, one slot\n"
        "                // of pool slack later. A full-pool lap also reads as zero and is\n"
        "                // likewise the VSYNC square-up's to repair, as before.\n"
        "                fills = advanced;\n"
        "            }\n"
        "            rgb_panel->gm_last_dma_idx = idx;\n"
        "        }\n"
        "        if (fills > 1) {\n"
        "            gm_rgb_catchup_count++;\n"
        "            gm_rgb_catchup_bufs += (uint32_t)(fills - 1);\n"
        "            if ((uint32_t)fills > gm_rgb_catchup_max) {\n"
        "                gm_rgb_catchup_max = (uint32_t)fills;\n"
        "            }\n"
        "        }\n"
        "        rgb_panel->gm_eof_frame += (size_t)fills;\n"
        "        for (int i = 0; i < fills; i++) {\n"
        "            // Figure out which bounce buffer to write to\n"
        "            portENTER_CRITICAL_ISR(&rgb_panel->spinlock);\n"
        "            int bb = rgb_panel->bb_eof_count % RGB_LCD_PANEL_BOUNCE_BUF_NUM;\n"
        "            rgb_panel->bb_eof_count++;\n"
        "            portEXIT_CRITICAL_ISR(&rgb_panel->spinlock);\n"
        "            // While a flash op has the cache down the PSRAM framebuffer is\n"
        "            // unreadable, so advance without copying: a few scanlines go stale\n"
        "            // for one frame, position tracking stays exact, and the alternative\n"
        "            // (running the memcpy) is a fault, not a glitch.\n"
        "            if (gm_flash_cache_down) {\n"
        "                gm_rgb_flash_skip_bufs++;\n"
        "                if (lcd_rgb_panel_fill_bounce_buffer_ex(rgb_panel, rgb_panel->bounce_buffer[bb], false)) {\n"
        "                    need_yield = true;\n"
        "                }\n"
        "            } else if (lcd_rgb_panel_fill_bounce_buffer(rgb_panel, rgb_panel->bounce_buffer[bb])) {\n"
        "                need_yield = true;\n"
        "            }\n"
        "        }\n"
        "        {\n"
        "            uint32_t bu = (esp_cpu_get_cycle_count() - gm_entry_cyc) / gm_rgb_cyc_per_us;\n"
        "            if (bu > gm_rgb_busy_max) {\n"
        "                gm_rgb_busy_max = bu;\n"
        "            }\n"
        "            uint32_t b = bu / GM_RGB_HIST_US;\n"
        "            gm_rgb_busy_hist[b < GM_RGB_HIST_N ? b : GM_RGB_HIST_N - 1]++;\n"
        "        }\n"
        "    } else {",
    ),
    # ---- 8. VSYNC: square up against the beam instead of restarting --------
    (
        "    portENTER_CRITICAL_ISR(&panel->spinlock);\n"
        "    if (panel->flags.need_restart) {\n"
        "        panel->flags.need_restart = false;\n"
        "        do_restart = true;\n"
        "    }\n"
        "    if (panel->bb_eof_count < panel->expect_eof_count) {\n"
        "        do_restart = true;\n"
        "    }\n"
        "    panel->bb_eof_count = 0;\n"
        "    portEXIT_CRITICAL_ISR(&panel->spinlock);\n"
        "#endif // CONFIG_LCD_RGB_RESTART_IN_VSYNC\n"
        "\n"
        "    if (!do_restart) {\n"
        "        return;\n"
        "    }\n"
        "\n"
        "    if (panel->bb_size) {\n"
        "        // Catch de-synced frame buffer and reset if needed.\n"
        "        if (panel->bounce_pos_px > bb_size_px * 2) {\n"
        "            panel->bounce_pos_px = 0;\n"
        "        }\n"
        "        // Pre-fill bounce buffer 0, if the EOF ISR didn't do that already\n"
        "        if (panel->bounce_pos_px < bb_size_px) {\n"
        "            lcd_rgb_panel_fill_bounce_buffer(panel, panel->bounce_buffer[0]);\n"
        "        }\n"
        "    }",

        "    portENTER_CRITICAL_ISR(&panel->spinlock);\n"
        "    if (panel->flags.need_restart) {\n"
        "        panel->flags.need_restart = false;\n"
        "        do_restart = true;\n"
        "    }\n"
        f"    // {M}: the count is kept per frame in its own field now, so the\n"
        "    // buffer index carried by bb_eof_count is free-running and its phase against\n"
        "    // the DMA survives a frame boundary by construction.\n"
        "    size_t gm_counted = panel->gm_eof_frame;\n"
        "    panel->gm_eof_frame = 0;\n"
        "    portEXIT_CRITICAL_ISR(&panel->spinlock);\n"
        "#endif // CONFIG_LCD_RGB_RESTART_IN_VSYNC\n"
        "\n"
        "    gm_rgb_eof_expect = (uint32_t)panel->expect_eof_count;\n"
        "    gm_rgb_eof_last = (uint32_t)gm_counted;\n"
        "    if (gm_rgb_eof_last < gm_rgb_eof_min) {\n"
        "        gm_rgb_eof_min = gm_rgb_eof_last;\n"
        "    }\n"
        "    if (gm_rgb_eof_last > gm_rgb_eof_max) {\n"
        "        gm_rgb_eof_max = gm_rgb_eof_last;\n"
        "    }\n"
        "\n"
        f"    // {M}: square the fill position up against the beam.\n"
        "    //\n"
        "    // A frame is exactly expect_eof_count bounce buffers. That is a hard invariant\n"
        "    // of the scan, so a frame that counted fewer knows exactly how many buffers\n"
        "    // went by unrefilled, with no estimate involved. Advancing over that many\n"
        "    // without copying puts bounce_pos_px back under the beam and costs one frame\n"
        "    // of stale pixels in the band that was missed.\n"
        "    //\n"
        "    // What upstream does here instead is reset the DMA and restart it, and that\n"
        "    // is what displaces the picture: the beam is already partway into the new\n"
        "    // frame, so resending from the top shifts everything below. It is also\n"
        "    // unnecessary. The DMA reads bounce buffers out of internal SRAM and is never\n"
        "    // starved, so its position stays locked to the beam for as long as the panel\n"
        "    // runs. Only the software fill position can drift, and only it needs fixing.\n"
        "    if (panel->bb_size && panel->expect_eof_count && gm_counted != panel->expect_eof_count) {\n"
        "        if (gm_counted < panel->expect_eof_count) {\n"
        "            const uint32_t missed = (uint32_t)(panel->expect_eof_count - gm_counted);\n"
        "            gm_rgb_resync_count++;\n"
        "            gm_rgb_resync_bufs += missed;\n"
        "            if (missed > gm_rgb_resync_max) {\n"
        "                gm_rgb_resync_max = missed;\n"
        "            }\n"
        "            if (gm_rgb_restart_hook) {\n"
        "                gm_rgb_restart_hook(missed);\n"
        "            }\n"
        "            gm_rgb_skip_buffers(panel, missed);\n"
        "        } else {\n"
        "            // Counted MORE buffers than the frame holds: the fill position is\n"
        "            // ahead of the beam by the excess, so every refill lands one slot\n"
        "            // later than the beam will read it. That is not a shift, it is a\n"
        "            // stable scroll: both counters stay self-consistent afterwards, so\n"
        "            // nothing downstream ever flags it, and the picture wraps two lines\n"
        "            // deeper per excess buffer until reboot. Rewind the fill position\n"
        "            // by the excess to put the next refill back under the beam; one\n"
        "            // frame shows a band of year-old pixels, against a wrap that lasts\n"
        "            // forever. The exact-delta counting in the EOF handler should make\n"
        "            // this branch unreachable; it stays because this failure mode is\n"
        "            // silent, stable and cumulative, the worst kind to leave untended.\n"
        "            const uint32_t excess = (uint32_t)(gm_counted - panel->expect_eof_count);\n"
        "            gm_rgb_over_count++;\n"
        "            gm_rgb_over_bufs += excess;\n"
        "            portENTER_CRITICAL_ISR(&panel->spinlock);\n"
        "            int fb_len_px = (int)(panel->fb_size / (panel->fb_bits_per_pixel / 8));\n"
        "            panel->bounce_pos_px -= (int)excess * bb_size_px;\n"
        "            while (panel->bounce_pos_px < 0) {\n"
        "                panel->bounce_pos_px += fb_len_px;\n"
        "            }\n"
        "            if (panel->bb_eof_count >= excess) {\n"
        "                panel->bb_eof_count -= excess;\n"
        "            }\n"
        "            portEXIT_CRITICAL_ISR(&panel->spinlock);\n"
        "        }\n"
        "        // The stall spanned the last reading, and it has just been accounted for\n"
        "        // here, so start the next delta from where the DMA is now.\n"
        "        panel->gm_last_dma_idx = gm_rgb_dma_buf_index(panel);\n"
        "    }\n"
        "\n"
        "    if (!do_restart) {\n"
        "        return;\n"
        "    }\n"
        "\n"
        "    // Only an explicit esp_lcd_rgb_panel_restart still gets here.\n"
        "    gm_rgb_restart_count++;\n"
        "    (void)bb_size_px;\n"
        "    if (panel->bb_size) {\n"
        "        panel->bounce_pos_px = 0;\n"
        "        panel->gm_last_dma_idx = -1;\n"
        "        panel->gm_eof_frame = 0;\n"
        "        // Pre-fill bounce buffer 0 before the DMA is started, so it has valid data\n"
        "        // the moment it runs; the rest are filled below while buffer 0 goes out.\n"
        "        lcd_rgb_panel_fill_bounce_buffer(panel, panel->bounce_buffer[0]);\n"
        "    }",
    ),
    (
        "    if (panel->bb_size) {\n"
        "        // Fill 2nd bounce buffer while 1st is being sent out, if needed.\n"
        "        if (panel->bounce_pos_px < bb_size_px * 2) {\n"
        "            lcd_rgb_panel_fill_bounce_buffer(panel, panel->bounce_buffer[1]);\n"
        "        }\n"
        "    }",

        f"    // {M}: fill the remaining buffers while buffer 0 is being sent out.\n"
        "    if (panel->bb_size) {\n"
        "        for (int i = 1; i < RGB_LCD_PANEL_BOUNCE_BUF_NUM; i++) {\n"
        "            lcd_rgb_panel_fill_bounce_buffer(panel, panel->bounce_buffer[i]);\n"
        "        }\n"
        "    }",
    ),
    # ---- 9. start path: prefill every buffer -------------------------------
    (
        "    if (rgb_panel->bb_size) {\n"
        "        rgb_panel->bounce_pos_px = 0;\n"
        "        lcd_rgb_panel_fill_bounce_buffer(rgb_panel, rgb_panel->bounce_buffer[0]);\n"
        "        lcd_rgb_panel_fill_bounce_buffer(rgb_panel, rgb_panel->bounce_buffer[1]);\n"
        "    }",

        f"    // {M}: prefill the whole pool, however many buffers it holds.\n"
        "    if (rgb_panel->bb_size) {\n"
        "        rgb_panel->bounce_pos_px = 0;\n"
        "        rgb_panel->gm_last_dma_idx = -1;\n"
        "        rgb_panel->gm_eof_frame = 0;\n"
        "        rgb_panel->gm_last_entry_cyc = 0;\n"
        "        gm_rgb_cyc_per_us = esp_rom_get_cpu_ticks_per_us();\n"
        "        for (int i = 0; i < RGB_LCD_PANEL_BOUNCE_BUF_NUM; i++) {\n"
        "            lcd_rgb_panel_fill_bounce_buffer(rgb_panel, rgb_panel->bounce_buffer[i]);\n"
        "        }\n"
        "    }",
    ),
    # ---- 10. the restart node has to raise an EOF of its own ---------------
    (
        "        gdma_buffer_mount_config_t restart_buffer_mount_cfg = {\n"
        "            .buffer = rgb_panel->bounce_buffer[0] + restart_skip_bytes,\n"
        "            .length = MIN(LCD_DMA_DESCRIPTOR_BUFFER_MAX_SIZE, rgb_panel->bb_size) - restart_skip_bytes,\n"
        "        };",

        f"        // {M}: the restart node stands in for bounce buffer 0, so it has\n"
        "        // to raise buffer 0's EOF the way the node it replaces would have.\n"
        "        //\n"
        "        // Upstream gets away with omitting this only by accident of size: at the\n"
        "        // stock bounce depth a buffer spans two descriptors, the restart node\n"
        "        // replaces just the first, and the second still carries the EOF. Once a\n"
        "        // bounce buffer fits in one descriptor, which is the case here, the concat\n"
        "        // below skips straight to buffer 1 and buffer 0's EOF disappears. Every\n"
        "        // frame then counts one short, which asks for another restart, for as long\n"
        "        // as the panel runs.\n"
        "        gdma_buffer_mount_config_t restart_buffer_mount_cfg = {\n"
        "            .buffer = rgb_panel->bounce_buffer[0] + restart_skip_bytes,\n"
        "            .length = MIN(LCD_DMA_DESCRIPTOR_BUFFER_MAX_SIZE, rgb_panel->bb_size) - restart_skip_bytes,\n"
        "            .flags = {\n"
        "                .mark_eof = true,\n"
        "            },\n"
        "        };",
    ),
]


def find_driver():
    """Locate esp_lcd_panel_rgb.c in the framework package PlatformIO is using."""
    candidates = []
    env_root = os.environ.get("IDF_PATH")
    if env_root:
        candidates.append(os.path.join(env_root, "components", "esp_lcd", "rgb", "esp_lcd_panel_rgb.c"))
    home = os.path.expanduser("~")
    for base in (os.environ.get("PLATFORMIO_CORE_DIR"), os.path.join(home, ".platformio")):
        if not base:
            continue
        candidates.append(os.path.join(base, "packages", "framework-espidf", "components",
                                       "esp_lcd", "rgb", "esp_lcd_panel_rgb.c"))
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


def apply(path):
    with open(path, "r", encoding="utf-8", newline="") as handle:
        text = handle.read()

    if MARKER in text:
        return "already patched"

    for index, (old, new) in enumerate(HUNKS, start=1):
        count = text.count(old)
        if count != 1:
            raise SystemExit(
                "patch_esp_lcd_rgb: hunk %d matched %d times in %s, expected exactly 1.\n"
                "The driver has changed upstream. Re-derive the patch against the new "
                "source rather than loosening this check: a silently skipped hunk brings "
                "back the display displacement it exists to fix." % (index, count, path)
            )
        text = text.replace(old, new)

    with open(path, "w", encoding="utf-8", newline="") as handle:
        handle.write(text)
    return "patched"


def main():
    path = find_driver()
    if path is None:
        raise SystemExit("patch_esp_lcd_rgb: could not find esp_lcd_panel_rgb.c")
    print("patch_esp_lcd_rgb: %s (%s)" % (apply(path), path))


main()
