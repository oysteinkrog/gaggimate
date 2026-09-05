// Host stubs for the panel pixel-clock control. PanelClock.cpp lives under
// src/display/drivers/, which the simulator's build_src_filter drops wholesale
// because everything else in there talks to the RGB LCD peripheral. The SDL
// window has no pixel clock, so setDiv() is a no-op and hasLiveControl() says
// "not adjustable", which is the same answer the device gives on ESP-IDF 4.4.
// The scan-out counters below (WebUIPlugin's /api/debug/scanout and /api/debug/heap)
// have no SDL equivalent either: there is no bounce buffer to slip and no VSYNC
// to measure margin against, so every one of them reports the healthy-forever
// answer (zero events, zero margin buckets) rather than a fabricated rate.
// Only the entry points the simulator actually links against are defined;
// the rest of the header's API is unreferenced here.
#include <display/drivers/common/PanelClock.h>

#include <cstring>

namespace panelclock {

bool hasLiveControl() { return false; }
void setDiv(int) {}

void scanoutStats(uint32_t *frames, uint32_t *refills, uint32_t *slips) {
    if (frames)
        *frames = 0;
    if (refills)
        *refills = 0;
    if (slips)
        *slips = 0;
}
uint32_t phyTrackDeferred() { return 0; }
void scanoutMargin(uint32_t *lastUs, uint32_t *minUs, uint32_t *maxUs, uint32_t *buckets) {
    if (lastUs)
        *lastUs = 0;
    if (minUs)
        *minUs = 0;
    if (maxUs)
        *maxUs = 0;
    if (buckets)
        std::memset(buckets, 0, SCANOUT_MARGIN_BUCKETS * sizeof(uint32_t));
}
void scanoutReset() {}
void scanoutMark(int) {}
void setLagThresholdUs(uint32_t) {}
size_t scanoutSlipLog(ScanoutSlip *, size_t) { return 0; }

} // namespace panelclock
