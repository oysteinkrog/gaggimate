#ifndef GAGGIMATE_BREW_GUARD_H
#define GAGGIMATE_BREW_GUARD_H

#include <stdint.h>

// Controller-side bound on a brew the display started (gm-warz).
//
// The controller has no brew process of its own. The display drives the pump
// and the brew valve with per-output commands and resends them only when they
// change, so until this guard existed nothing on the controller could end a
// brew: on 2026-10-01 a stray tap started one on the bench and the real pump
// ran for about 65 minutes. The ping watchdog did not help, because the
// display kept pinging the whole time.
//
// A brew, here, is the period the brew valve (relay index 0) is commanded
// open. Steam and hot water run the pump with the valve closed and keep their
// own display-side limits; the ping watchdog still covers them if the display
// goes quiet. The guard ends a brew in two cases:
//
//   - TimeLimit: the valve has been open longer than the maximum.
//   - LinkLost: the BLE link to the display dropped, or the ping watchdog
//     fired, while the valve was open.
//
// Either way the stop latches. Until the display itself commands the valve
// closed, valve-open and non-zero pump commands are refused. That matters
// because the display resends its whole output state after every reconnect:
// without the latch a link that came back would restart the pump of a brew
// the controller had just ended. A display on this firmware's protocol ends
// its own process when it receives ERROR_CODE_BREW_STOPPED, which sends the
// valve-closed command that clears the latch.
//
// Pure logic with the clock passed in, so tools/brew_guard_check can run it on
// the host. Not thread safe: the caller serialises every call, together with
// the output change the call decides, under one lock.

namespace gm_safety {

// Longest a brew may keep the valve open before the controller ends it.
// 300 s is the conservative first choice and NOT a decision: the owner has not
// set this default yet (gm-warz asks for "well under 10 minutes", default to
// decide). The display caps its own brew at 300 s too (BREW_MAX_DURATION_MS in
// src/display/core/constants.h), so a display brew that runs to its own limit
// meets this one at about the same moment; both stop paths close the valve,
// so the overlap is harmless, but a longer real shot needs both raised.
constexpr uint32_t BREW_MAX_DURATION_MS = 300000;

enum class BrewStop : uint8_t { None = 0, TimeLimit = 1, LinkLost = 2 };

inline const char *brewStopName(BrewStop s) {
    switch (s) {
    case BrewStop::TimeLimit:
        return "time limit";
    case BrewStop::LinkLost:
        return "display link lost";
    default:
        return "none";
    }
}

class BrewGuard {
  public:
    explicit BrewGuard(uint32_t maxMs = BREW_MAX_DURATION_MS) : _maxMs(maxMs) {}

    // A brew-valve command from the display. Returns the valve state to apply:
    // an open command while latched is refused (false). A close command ends
    // the brew and clears the latch.
    bool onValveCommand(bool open, uint32_t nowMs) {
        if (!open) {
            _brewing = false;
            _latch = BrewStop::None;
            _reportPending = false;
            return false;
        }
        if (_latch != BrewStop::None) {
            return false;
        }
        if (!_brewing) {
            _brewing = true;
            _startMs = nowMs;
        }
        return true;
    }

    // Whether a pump command with this demand may be applied as sent. A
    // demand of zero (pump off) is always allowed; a non-zero demand is
    // refused while latched, and the caller applies pump off instead.
    bool allowPumpDemand(bool nonZero) const { return !nonZero || _latch == BrewStop::None; }

    // Called periodically. Returns true once, on the poll where the brew
    // passes the maximum; the caller then stops the pump and closes the valve.
    bool poll(uint32_t nowMs) {
        if (!_brewing) {
            return false;
        }
        // Unsigned subtraction stays correct across a millis() wrap.
        if (static_cast<uint32_t>(nowMs - _startMs) < _maxMs) {
            return false;
        }
        stop(BrewStop::TimeLimit);
        return true;
    }

    // The link to the display dropped (or the ping watchdog fired). Returns
    // true when a brew was running; the caller then stops the outputs.
    bool onLinkDown() {
        if (!_brewing) {
            return false;
        }
        stop(BrewStop::LinkLost);
        return true;
    }

    // The reason to report to the display, once per stop; None when there is
    // nothing new. The caller asks only while the link is up, so a LinkLost
    // stop is reported after the reconnect.
    BrewStop takeReport() {
        if (!_reportPending) {
            return BrewStop::None;
        }
        _reportPending = false;
        return _latch;
    }

    bool brewing() const { return _brewing; }
    BrewStop latched() const { return _latch; }
    uint32_t maxMs() const { return _maxMs; }

  private:
    void stop(BrewStop why) {
        _brewing = false;
        _latch = why;
        _reportPending = true;
    }

    uint32_t _maxMs;
    uint32_t _startMs = 0;
    bool _brewing = false;
    bool _reportPending = false;
    BrewStop _latch = BrewStop::None;
};

} // namespace gm_safety

#endif // GAGGIMATE_BREW_GUARD_H
