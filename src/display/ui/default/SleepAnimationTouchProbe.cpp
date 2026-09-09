// The touch probe's frame hooks (GM_TOUCH_PROBE: display-loadtest,
// display-blestress, display-qemu). A publish or a plate write stamps an edge
// (LV_Helper.h, TouchTask.cpp); the render task records which frame first
// carries it and logs the GM_TOUCHLAT and GM_ELEM lines tools/touch_lat.py
// reads. Out of line: one call per frame, none per band.
#ifndef GAGGIMATE_SIM
#ifdef GM_TOUCH_PROBE

#include "SleepAnimation.h"
#include "SleepAnimationProbe.h"
#include <display/drivers/common/LV_Helper.h>
#include <esp_log.h>
#include <esp_timer.h>

// Edge stamp of a publish this frame's composite sampled; renderLoop logs it
// once the frame is presented. Render-task-private, so no volatile needed.
static int64_t s_probeFrameEdgeUs = 0;
static int64_t s_probeFrameElemUs = 0;
static bool s_probeFramePress = false;

void SleepAnimation::probeFrameSampled(int ofi) {
    // This sample is the moment a publish becomes part of a frame; a stamp
    // still pending here means this frame is the first to carry the response.
    if (g_probePublishUs != 0 && ofi >= 0) {
        s_probeFrameEdgeUs = g_probePublishUs;
        s_probeFramePress = g_probePublishIsPress;
        g_probePublishUs = 0;
    }
    // The touch task's plate write is part of this frame's element
    // evaluation (evaluateElements above), so the edge it stamped closes at
    // this frame's present.
    if (g_probeElemEdgeUs != 0) {
        s_probeFrameElemUs = g_probeElemEdgeUs;
        g_probeElemEdgeUs = 0;
    }
}

void SleepAnimation::probeFramePresented() {
    if (s_probeFrameEdgeUs != 0) {
        ESP_LOGI("TouchProbe", "GM_TOUCHLAT: %s->anim_frame %lld us", s_probeFramePress ? "press" : "release",
                 (long long)(esp_timer_get_time() - s_probeFrameEdgeUs));
        s_probeFrameEdgeUs = 0;
    }
    if (s_probeFrameElemUs != 0) {
        ESP_LOGI("TouchProbe", "GM_TOUCHLAT: press->anim_frame(elem) %lld us",
                 (long long)(esp_timer_get_time() - s_probeFrameElemUs));
        s_probeFrameElemUs = 0;
    }
}

void SleepAnimation::probeElemEdgeClose(bool vis) {
    if (vis && g_probeElemEdgeUs != 0) {
        // This frame's present carries the plate, so the edge closes there.
        s_probeFrameElemUs = g_probeElemEdgeUs;
        g_probeElemEdgeUs = 0;
    }
}

void SleepAnimation::probeElemShown(int slot, int64_t sinceUs) {
    ESP_LOGI("TouchProbe", "GM_ELEM: write->frame %lld us (slot %d)", (long long)sinceUs, slot);
}

#endif // GM_TOUCH_PROBE
#endif // GAGGIMATE_SIM
