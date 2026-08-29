#include "halfres_expand.h"

namespace ovb {

// Byte-identical to the fill+copy loop pair in SleepAnimation::renderFrame's
// half-resolution expansion.
void expandRow_ref(const uint16_t *__restrict src, uint16_t *__restrict row0, uint16_t *__restrict row1, int rw) {
    uint32_t *__restrict d0 = reinterpret_cast<uint32_t *>(row0);
    uint32_t *__restrict d1 = reinterpret_cast<uint32_t *>(row1);
    for (int i = 0; i < rw; i++) {
        const uint32_t v = src[i];
        d0[i] = v | (v << 16);
    }
    for (int i = 0; i < rw; i++) {
        const uint32_t v = src[i];
        d1[i] = v | (v << 16);
    }
}

const ExpandRowVariant kExpandRowVariants[] = {
    {"ref_scalar", &expandRow_ref, true, false},
};
const int kExpandRowVariantCount = sizeof(kExpandRowVariants) / sizeof(kExpandRowVariants[0]);

} // namespace ovb
