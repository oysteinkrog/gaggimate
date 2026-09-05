// Storage for the LV_Helper.h globals DefaultUI.cpp and WebUIPlugin.cpp read
// and write regardless of platform. LV_Helper.h is portable and is included
// unchanged on both sides; LV_Helper.cpp (the definitions) lives under
// drivers/ with the rest of the panel hardware code, which the simulator's
// build_src_filter drops wholesale, so this replaces just that one
// translation unit. Initial values match LV_Helper.cpp's so a reader
// comparing sim and device boot state sees the same numbers.
//
// The sim has no touch driver yet (TouchInject lands in a later bead) and
// never calls DefaultUI::serviceTouchMap or serviceUiAnimTest (both guarded
// out under GAGGIMATE_SIM), so g_touchMapReq/g_touchLog and friends are read
// by WebUIPlugin's debug routes but never serviced here; those routes report
// "nothing pending" until a later bead wires up the sim UI task.
#include <display/drivers/common/LV_Helper.h>

volatile int64_t g_touchEdgeAtUs = 0;
volatile int64_t g_overlayMinRefreshUs = 250000; // DefaultUI's constructor sets OVERLAY_MIN_REFRESH_US
volatile int g_uiAnimTestReq = 0;
OverlayStats g_overlayStats;
TouchLogEntry g_touchLog[TOUCHLOG_N];
volatile uint32_t g_touchLogCount = 0;
volatile int g_touchMapReq = 0;
volatile bool g_touchMapLoad = false;
char *g_touchMapBuf = nullptr;
volatile uint32_t g_touchMapLen = 0;
