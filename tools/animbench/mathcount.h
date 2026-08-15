// Force-included (-include) into every bganim TU by the animbench Makefile:
// rewrites libm calls to counting wrappers so the bench can report exactly
// how many transcendentals each animation burns per frame. On the ESP32-S3
// these are soft-float library calls costing roughly:
//   sinf/cosf ~150 cy   expf/exp2f/powf/logf ~150-400 cy   atan2f ~300 cy
//   sqrtf ~90 cy        fmodf/floorf ~40 cy   float divide ~30-50 cy
// against a TOTAL budget of ~34 cycles/pixel (480x480 @ 30 fps @ 240 MHz).
#pragma once
#include <math.h>

namespace mathcount {
struct Counters {
    unsigned long long sinf_n, cosf_n, sqrtf_n, expf_n, exp2f_n, powf_n;
    unsigned long long atan2f_n, fmodf_n, logf_n, floorf_n, tanhf_n, lroundf_n;
};
extern Counters g;
void reset();
} // namespace mathcount

float mc_sinf(float);
float mc_cosf(float);
float mc_sqrtf(float);
float mc_expf(float);
float mc_exp2f(float);
float mc_powf(float, float);
float mc_atan2f(float, float);
float mc_fmodf(float, float);
float mc_logf(float);
float mc_floorf(float);
float mc_tanhf(float);
long mc_lroundf(float);

#ifndef MATHCOUNT_IMPL
#define sinf mc_sinf
#define cosf mc_cosf
#define sqrtf mc_sqrtf
#define expf mc_expf
#define exp2f mc_exp2f
#define powf mc_powf
#define atan2f mc_atan2f
#define fmodf mc_fmodf
#define logf mc_logf
#define floorf mc_floorf
#define tanhf mc_tanhf
#define lroundf mc_lroundf
#endif
