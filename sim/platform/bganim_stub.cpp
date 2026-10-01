// Host stubs for the background-animation allocation counters. Every bganim
// translation unit is wrapped in #ifndef GAGGIMATE_SIM, so on the host they
// compile to nothing, but WebUIPlugin's heap diagnostics read these
// unconditionally. Zero is the truthful answer throughout: with no animation
// tables allocated (the render loop itself is a later bead), no pool has
// handed anything out and nothing has ever overflowed to PSRAM.
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

// The settings writer's gate for the two theme-library fields (WebUIPlugin.cpp)
// is the real one. BgAnimThemes.cpp is parsing and tables with no Arduino or
// ESP-IDF in it (the native settings test includes it the same way), but it is
// wrapped in #ifndef GAGGIMATE_SIM like every bganim unit, so it is included
// here with the flag lifted. A stub that refused every value made the handler
// answer 422 to every web save on the simulator once a refused field became
// an error (gm-nov3.26), and a stub that accepted every value could not test
// the refusal.
#pragma push_macro("GAGGIMATE_SIM")
#undef GAGGIMATE_SIM
#include <display/ui/default/bganim/BgAnimThemes.cpp>
#pragma pop_macro("GAGGIMATE_SIM")
