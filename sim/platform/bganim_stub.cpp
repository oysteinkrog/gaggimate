// Host stubs for the background-animation allocation counters. Every bganim
// translation unit except BgAnimThemes.cpp is wrapped in #ifndef GAGGIMATE_SIM,
// so on the host they compile to nothing, but WebUIPlugin's heap diagnostics
// read these unconditionally. Zero is the truthful answer throughout: with no
// animation tables allocated (the render loop is device-only), no pool has
// handed anything out and nothing has ever overflowed to PSRAM.
//
// The gradient functions that used to be stubbed here are gone (gm-nov3.3).
// BgAnimThemes.cpp now compiles on the host, so bg_theme_*, bg_custom_valid,
// bg_library_valid, bg_map_valid and bg_ref_valid are the real rules on the
// simulator too. The stubs answered "no theme table" and "every write is
// invalid", which made the simulator's gradient tests unable to fail: a web
// save the firmware would have accepted was rejected by the host build, and a
// test that checked what happened afterwards passed for the wrong reason.
#include <display/ui/default/bganim/BgAnim.h>
#include <display/ui/default/bganim/BgAnimCommon.h>

namespace bganim {

size_t g_allocSram = 0;
size_t g_allocPsram = 0;

size_t hotUsed() { return 0; }
size_t hotShared() { return 0; }
size_t hotPeak() { return 0; }
uint32_t hotFailCount() { return 0; }

} // namespace bganim
