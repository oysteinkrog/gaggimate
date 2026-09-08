// Storage for the LV_Helper.h globals DefaultUI.cpp and WebUIPlugin.cpp read
// and write regardless of platform. LV_Helper.h is portable and is included
// unchanged on both sides; LV_Helper.cpp (the definitions) lives under
// drivers/ with the rest of the panel hardware code, which the simulator's
// build_src_filter drops wholesale, so this replaces just that one
// translation unit. Initial values match LV_Helper.cpp's so a reader
// comparing sim and device boot state sees the same numbers.
//
// Touch injection (TouchInject.h) lives outside this stub; g_touchLog and
// friends are read by WebUIPlugin's debug routes but nothing on the sim
// writes touch edges into g_touchLog yet, so /api/debug/touchlog stays empty
// there. g_touchMapReq/g_touchMapPending ARE serviced on the sim as of
// gm-flw.15: DefaultUI::serviceTouchMap runs under GAGGIMATE_SIM too, called
// from DefaultUI::loop every pass same as the device.
#include <display/drivers/common/LV_Helper.h>

volatile int64_t g_touchEdgeAtUs = 0;
TouchHitHook g_touchHitHook = nullptr;
volatile bool g_pressPlateActive = false;
volatile int64_t g_overlayMinRefreshUs = 250000; // DefaultUI's constructor sets OVERLAY_MIN_REFRESH_US
volatile int g_uiAnimTestReq = 0;
volatile int g_dialElementsReq = 1;
volatile int g_textElementsReq = 1;
volatile int g_textEaseReq = 1;
volatile int g_textDbg[8] = {0};
TextElemDbg g_textElemDbg[6];
DirtyLogEntry g_dirtyLog[DIRTYLOG_N];
volatile uint32_t g_dirtyLogCount = 0;
OverlayStats g_overlayStats;
TouchLogEntry g_touchLog[TOUCHLOG_N];
volatile uint32_t g_touchLogCount = 0;
volatile int g_touchMapReq = 0;
volatile bool g_touchMapLoad = false;
volatile bool g_touchMapPending = false;
char *g_touchMapBuf = nullptr;
volatile uint32_t g_touchMapLen = 0;
