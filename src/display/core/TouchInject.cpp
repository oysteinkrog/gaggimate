#include "TouchInject.h"

#if GM_TOUCH_INJECT

#include <Arduino.h>          // millis()
#include <freertos/FreeRTOS.h> // portMUX_TYPE / portENTER_CRITICAL / portEXIT_CRITICAL

namespace {

enum class Stage : uint8_t {
    Idle,     // no request in flight
    Queued,   // requested, not yet polled: the hold clock has not started
    Pressed,  // a poll has stamped pressedAtMs; overriding with pressed=true
    Released, // holdMs elapsed; one poll has emitted pressed=false
};

struct State {
    Stage stage = Stage::Idle;
    int16_t x = 0;
    int16_t y = 0;
    uint32_t holdMs = 0;
    uint32_t pressedAtMs = 0;
    uint32_t releasedAtMs = 0;
};

State s_state;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

} // namespace

bool touchInjectRequest(int16_t x, int16_t y, uint32_t holdMs) {
    if (x < TOUCH_INJECT_MIN_COORD || x > TOUCH_INJECT_MAX_COORD || y < TOUCH_INJECT_MIN_COORD ||
        y > TOUCH_INJECT_MAX_COORD || holdMs < TOUCH_INJECT_MIN_HOLD_MS || holdMs > TOUCH_INJECT_MAX_HOLD_MS) {
        return false;
    }
    bool queued = false;
    portENTER_CRITICAL(&s_mux);
    if (s_state.stage == Stage::Idle) {
        s_state.stage = Stage::Queued;
        s_state.x = x;
        s_state.y = y;
        s_state.holdMs = holdMs;
        s_state.pressedAtMs = 0;
        s_state.releasedAtMs = 0;
        queued = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return queued;
}

bool touchInjectPoll(int16_t &x, int16_t &y, bool &pressed) {
    bool active = false;
    const uint32_t now = millis();
    portENTER_CRITICAL(&s_mux);
    switch (s_state.stage) {
    case Stage::Idle:
        break;
    case Stage::Queued:
        s_state.stage = Stage::Pressed;
        s_state.pressedAtMs = now;
        x = s_state.x;
        y = s_state.y;
        pressed = true;
        active = true;
        break;
    case Stage::Pressed:
        if (now - s_state.pressedAtMs >= s_state.holdMs) {
            s_state.stage = Stage::Released;
            s_state.releasedAtMs = now;
            x = s_state.x;
            y = s_state.y;
            pressed = false;
        } else {
            x = s_state.x;
            y = s_state.y;
            pressed = true;
        }
        active = true;
        break;
    case Stage::Released:
        // The release sample already went out on the previous poll; this is
        // the poll after that, so hand real input back from here on.
        s_state.stage = Stage::Idle;
        break;
    }
    portEXIT_CRITICAL(&s_mux);
    return active;
}

void touchInjectState(bool &active, uint32_t &remainingMs, uint32_t &pressedAtMs, uint32_t &releasedAtMs) {
    State snapshot;
    portENTER_CRITICAL(&s_mux);
    snapshot = s_state;
    portEXIT_CRITICAL(&s_mux);

    active = snapshot.stage != Stage::Idle;
    pressedAtMs = snapshot.pressedAtMs;
    releasedAtMs = snapshot.releasedAtMs;
    switch (snapshot.stage) {
    case Stage::Idle:
    case Stage::Released:
        remainingMs = 0;
        break;
    case Stage::Queued:
        remainingMs = snapshot.holdMs;
        break;
    case Stage::Pressed: {
        const uint32_t elapsed = millis() - snapshot.pressedAtMs;
        remainingMs = elapsed < snapshot.holdMs ? snapshot.holdMs - elapsed : 0;
        break;
    }
    }
}

#endif // GM_TOUCH_INJECT
