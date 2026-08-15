#define MATHCOUNT_IMPL
#include "mathcount.h"

// The Makefile force-includes mathcount.h into every TU — including this one,
// where that happens BEFORE the MATHCOUNT_IMPL define above (so the macros are
// active and `return sinf(x)` would expand to `return mc_sinf(x)`: infinite
// recursion). Undo them explicitly so the wrappers call the real libm.
#undef sinf
#undef cosf
#undef sqrtf
#undef expf
#undef exp2f
#undef powf
#undef atan2f
#undef fmodf
#undef logf
#undef floorf
#undef tanhf
#undef lroundf

namespace mathcount {
Counters g{};
void reset() { g = Counters{}; }
} // namespace mathcount

using mathcount::g;

float mc_sinf(float x) {
    g.sinf_n++;
    return sinf(x);
}
float mc_cosf(float x) {
    g.cosf_n++;
    return cosf(x);
}
float mc_sqrtf(float x) {
    g.sqrtf_n++;
    return sqrtf(x);
}
float mc_expf(float x) {
    g.expf_n++;
    return expf(x);
}
float mc_exp2f(float x) {
    g.exp2f_n++;
    return exp2f(x);
}
float mc_powf(float x, float y) {
    g.powf_n++;
    return powf(x, y);
}
float mc_atan2f(float y, float x) {
    g.atan2f_n++;
    return atan2f(y, x);
}
float mc_fmodf(float x, float y) {
    g.fmodf_n++;
    return fmodf(x, y);
}
float mc_logf(float x) {
    g.logf_n++;
    return logf(x);
}
float mc_floorf(float x) {
    g.floorf_n++;
    return floorf(x);
}
float mc_tanhf(float x) {
    g.tanhf_n++;
    return tanhf(x);
}
long mc_lroundf(float x) {
    g.lroundf_n++;
    return lroundf(x);
}
