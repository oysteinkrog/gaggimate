#ifndef BGANIM_CLOCK_H
#define BGANIM_CLOCK_H

// Animation time for frame(), built from wrapped millis() deltas
// (gm-bzu.50).
//
// The old pattern was `const float t = tMs * speed;` and then sin(w * t + p).
// A float has a 24-bit mantissa, so float(tMs) steps by 32 ms after 3.1 days
// of uptime and by 256 ms after 24.9 days, and motion moves in visible jumps
// long before millis() wraps at 49.7 days. Multiplying by speed also made
// the phase jump whenever the speed setting changed, and at the wrap itself
// every oscillator jumped to its t = 0 phase.
//
// AnimClock fixes all three:
//   - it advances by the wrapped 32-bit delta between calls, so the wrap is
//     one ordinary step;
//   - it scales each step by the speed in force for that step, so a speed
//     change bends the motion instead of moving it;
//   - it keeps time as a 64-bit integer (Q16 milliseconds of animation
//     time), so it never loses precision.
//
// Each oscillator keeps its own phase: its rate is a Q48 fraction of a turn
// per animation millisecond, and the 64-bit product with the clock wraps
// modulo 2^64, which is a whole number of turns for every rate at once. No
// shared modulus is needed, so no oscillator jumps when another one wraps.
//
// The first call after construction takes a zero step, so the picture
// depends only on the time elapsed since the clock started, never on the
// absolute millis() value. An animation keeps its clock in a file-scope
// variable that release() does not reset, so the full/half resolution
// switch (release(), then init() at the new size) keeps the motion going
// where it was. A delta that reads as negative (the device animtest feeds
// its own time base) counts as zero.
//
// Note for the kdev rig (tools/kblob): an animation with a clock carries
// state in frame(), so its blob and the firmware's band() differ by their
// own histories; compare blobref against blob for these.

#include <stdint.h>

namespace bganim {

struct AnimClock {
    uint64_t simQ16 = 0; // animation milliseconds since the first call, Q16
    uint32_t lastMs = 0;
    bool started = false;

    // Advances by the time since the previous call, scaled by speed, and
    // returns that wall-clock step in milliseconds (0 on the first call).
    uint32_t advance(uint32_t tMs, float speed) {
        uint32_t dt = 0;
        if (started) {
            const int32_t d = static_cast<int32_t>(tMs - lastMs);
            dt = d > 0 ? static_cast<uint32_t>(d) : 0;
        }
        started = true;
        lastMs = tMs;
        const uint32_t speedQ16 = speed > 0.0f ? static_cast<uint32_t>(speed * 65536.0f + 0.5f) : 0;
        simQ16 += static_cast<uint64_t>(dt) * speedQ16;
        return dt;
    }

    // Whole animation milliseconds, modulo 2^32. A uint32 phase computed as
    // ms() * rateQ32 stays continuous across this wrap too.
    uint32_t ms() const { return static_cast<uint32_t>(simQ16 >> 16); }
};

// An oscillator's rate, from radians per animation millisecond, as a Q48
// fraction of a turn. A negative rate is stored in two's complement and
// turns the other way. constexpr so a constant rate costs nothing at run
// time; a run-time rate pays one double conversion where it is set.
constexpr uint64_t oscRateQ48(double radPerMs) {
    return static_cast<uint64_t>(
        static_cast<int64_t>(radPerMs * (281474976710656.0 / 6.283185307179586) + (radPerMs >= 0 ? 0.5 : -0.5)));
}

// The oscillator's phase now, as a Q32 fraction of a turn.
inline uint32_t oscTurnQ32(const AnimClock &c, uint64_t rateQ48) {
    return static_cast<uint32_t>((c.simQ16 * rateQ48) >> 32);
}

// The same phase in radians, in [0, 2*pi].
inline float oscRad(const AnimClock &c, uint64_t rateQ48) {
    return static_cast<float>(oscTurnQ32(c, rateQ48)) * (6.2831853f / 4294967296.0f);
}

} // namespace bganim

#endif // BGANIM_CLOCK_H
