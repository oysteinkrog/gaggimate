// Host stubs for the panel pixel-clock control. PanelClock.cpp lives under
// src/display/drivers/, which the simulator's build_src_filter drops wholesale
// because everything else in there talks to the RGB LCD peripheral. The SDL
// window has no pixel clock, so setDiv() is a no-op and hasLiveControl() says
// "not adjustable", which is the same answer the device gives on ESP-IDF 4.4.
// Only the two entry points the simulator actually links against are defined;
// the rest of the header's API is unreferenced here.
#include <display/drivers/common/PanelClock.h>

namespace panelclock {

bool hasLiveControl() { return false; }
void setDiv(int) {}

} // namespace panelclock
