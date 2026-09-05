#pragma once
// Synthetic touch injection: /api/debug/tap on the bench and the simulator
// drives one scripted tap through the same code the device's touch read
// callback and the simulator's mouse read callback poll each cycle, so a
// scripted request exercises the real UI code path on both venues instead of
// a separate test-only route into LVGL.
//
// Compiled under GM_TOUCH_PROBE (bench builds) or GAGGIMATE_SIM (the desktop
// simulator); otherwise an empty inline stub, so a read callback can call
// these unconditionally and production carries none of it. The two macros
// are never both defined for one env.
#include <cstdint>

#if defined(GM_TOUCH_PROBE) || defined(GAGGIMATE_SIM)
#define GM_TOUCH_INJECT 1
#else
#define GM_TOUCH_INJECT 0
#endif

// Bounds /api/debug/tap and touchInjectRequest both enforce.
constexpr int16_t TOUCH_INJECT_MIN_COORD = 0;
constexpr int16_t TOUCH_INJECT_MAX_COORD = 479;
constexpr uint32_t TOUCH_INJECT_MIN_HOLD_MS = 20;
constexpr uint32_t TOUCH_INJECT_MAX_HOLD_MS = 10000;
constexpr uint32_t TOUCH_INJECT_DEFAULT_HOLD_MS = 80;

#if GM_TOUCH_INJECT

// Queue a synthetic tap at (x, y) held for holdMs. Returns false, with
// nothing queued, when a request is already in flight or an argument is out
// of the bounds above. The route validates range itself before calling this
// so it can tell the two rejection reasons apart (400 vs 409); this also
// re-checks range so no other caller can queue an out-of-bounds tap.
bool touchInjectRequest(int16_t x, int16_t y, uint32_t holdMs);

// Polled first by touchpad_read (device) and mouse_read (sim), ahead of
// either's own read of real hardware/SDL state. While a tap is in flight
// this overrides the sample and returns true; the caller must not fall
// through to its real read when it does. The hold clock starts at the poll
// that first reports the press, not at request time, so a request that
// lands between polls never shortens the observed hold. After holdMs the
// next poll reports one released sample at the same point; the poll after
// that ends the injection and returns false, so real input resumes there.
bool touchInjectPoll(int16_t &x, int16_t &y, bool &pressed);

// State for /api/debug/tap's GET-without-arguments response. remainingMs is
// the time left before the release sample goes out (0 once it has, or when
// idle); pressedAtMs/releasedAtMs are the millis() timestamps the poll
// observed them at (0 before that stage is reached).
void touchInjectState(bool &active, uint32_t &remainingMs, uint32_t &pressedAtMs, uint32_t &releasedAtMs);

#else

inline bool touchInjectRequest(int16_t, int16_t, uint32_t) { return false; }
inline bool touchInjectPoll(int16_t &, int16_t &, bool &) { return false; }
inline void touchInjectState(bool &active, uint32_t &remainingMs, uint32_t &pressedAtMs, uint32_t &releasedAtMs) {
    active = false;
    remainingMs = 0;
    pressedAtMs = 0;
    releasedAtMs = 0;
}

#endif
