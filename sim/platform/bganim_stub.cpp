// Host stubs for the background-animation allocation counters. Every bganim
// translation unit is wrapped in #ifndef GAGGIMATE_SIM, so on the host they
// compile to nothing, but WebUIPlugin's heap diagnostics read these two
// counters unconditionally. Zero is the truthful answer: with no animation
// tables allocated, neither pool has handed anything out.
#include <cstddef>

namespace bganim {

size_t g_allocSram = 0;
size_t g_allocPsram = 0;

} // namespace bganim
