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

// The settings writer's gate for the two theme-library fields (WebUIPlugin.cpp).
// No theme library is loaded on the host (BgAnimThemes.cpp is excluded the same
// way), so there is nothing a name could validate against; false leaves the
// stored setting unchanged rather than accepting an unchecked string.
bool bg_library_valid(const char *) { return false; }
bool bg_map_valid(const char *) { return false; }
bool bg_ref_valid(const char *) { return false; }

// The built-in theme table lives in BgAnimThemes.cpp, which is excluded here
// too. The settings writer only reads the count to range-check a built-in
// index before mirroring it into bgAnimTheme, and zero makes every index fail
// that check, which is the same "leave the stored setting alone" answer the
// two validators above give.
int bg_theme_count() { return 0; }
