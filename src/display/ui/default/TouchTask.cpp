#include "TouchTask.h"
#ifndef GAGGIMATE_SIM

#include <Arduino.h>
#include <atomic>
#include <display/core/TouchInject.h>
#include <display/drivers/common/Display.h>
#include <display/drivers/common/LV_Helper.h>
#include <display/ui/default/SleepAnimation.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

namespace touchtask {
namespace {

constexpr uint32_t kPeriodMs = 5;
constexpr int kReleaseSamples = 3;
constexpr uint32_t kStackBytes = 4096;

Display *s_display = nullptr;
SleepAnimation *s_anim = nullptr;
int s_plateElement = -1;
TaskHandle_t s_task = nullptr;
std::atomic<bool> s_running{false};
std::atomic<bool> s_pollEnabled{true};

// The sample, seqlock: odd while being written.
std::atomic<uint32_t> s_sampleSeq{0};
Sample s_sample;
std::atomic<uint32_t> s_sampleCount{0};

// The edge queue (TouchTask.h, kMaxTransitions): the task appends, the UI
// task (latest()) takes from the front. Both run on core 1; a spinlock
// covers the few instructions either side needs. The task writes the
// sample before it queues the edge, so once the queue is empty the live
// sample is never older than the last edge LVGL was given; at worst it is
// the edge still being queued, which LVGL then sees twice in a row and
// ignores as no change.
struct Transition {
    int16_t x;
    int16_t y;
    uint8_t pressed;
    uint8_t synthetic;
    int64_t tUs;
};
portMUX_TYPE s_queueMux = portMUX_INITIALIZER_UNLOCKED;
Transition s_queue[kMaxTransitions];
int s_queueCount = 0;
std::atomic<uint32_t> s_queueDropped{0};

void queueEdge(const Transition &t) {
    taskENTER_CRITICAL(&s_queueMux);
    if (s_queueCount == kMaxTransitions) {
        // Edges alternate, so a press followed by its release is always
        // present in a full queue. Removing the oldest such pair keeps the
        // alternation and what LVGL has already been told consistent.
        int drop = 0;
        for (int i = 0; i + 1 < s_queueCount; i++) {
            if (s_queue[i].pressed && !s_queue[i + 1].pressed) {
                drop = i;
                break;
            }
        }
        for (int i = drop; i + 2 < s_queueCount; i++) {
            s_queue[i] = s_queue[i + 2];
        }
        s_queueCount -= 2;
        s_queueDropped.fetch_add(1);
    }
    s_queue[s_queueCount++] = t;
    taskEXIT_CRITICAL(&s_queueMux);
}

bool takeEdge(Transition &t) {
    bool got = false;
    taskENTER_CRITICAL(&s_queueMux);
    if (s_queueCount > 0) {
        t = s_queue[0];
        for (int i = 1; i < s_queueCount; i++) {
            s_queue[i - 1] = s_queue[i];
        }
        s_queueCount--;
        got = true;
    }
    taskEXIT_CRITICAL(&s_queueMux);
    return got;
}

void clearEdges() {
    taskENTER_CRITICAL(&s_queueMux);
    s_queueCount = 0;
    taskEXIT_CRITICAL(&s_queueMux);
}

// The hit map: two PSRAM buffers, the header names the live one. The writer
// (UI task) fills the other buffer, then publishes index and generation in
// one store; the reader retries when the generation moved under it.
struct Header {
    uint32_t gen;     // even: stable
    uint8_t index;    // live buffer
    uint8_t plateOn;
    uint8_t n;
    uint8_t outset;
    uint16_t color;
};
std::atomic<uint64_t> s_header{0};
HitRect *s_maps[2] = {nullptr, nullptr};

// A plain pack/unpack pair; the bit layout is private to this file.
uint64_t pack(const Header &h) {
    uint64_t v = h.gen;
    v |= static_cast<uint64_t>(h.index & 1) << 32;
    v |= static_cast<uint64_t>(h.plateOn & 1) << 33;
    v |= static_cast<uint64_t>(h.n) << 40;
    v |= static_cast<uint64_t>(h.color) << 48;
    v |= static_cast<uint64_t>(h.outset & 0x3F) << 34;
    return v;
}
Header unpack(uint64_t v) {
    Header h;
    h.gen = static_cast<uint32_t>(v);
    h.index = static_cast<uint8_t>((v >> 32) & 1);
    h.plateOn = static_cast<uint8_t>((v >> 33) & 1);
    h.outset = static_cast<uint8_t>((v >> 34) & 0x3F);
    h.n = static_cast<uint8_t>((v >> 40) & 0xFF);
    h.color = static_cast<uint16_t>(v >> 48);
    return h;
}

// Topmost rectangle containing (x, y): the list is in tree order, so the
// last match is the deepest, latest sibling, which is what
// lv_indev_search_obj returns.
bool hitTest(int16_t x, int16_t y, Header &hdr, HitRect &out) {
    for (int attempt = 0; attempt < 4; attempt++) {
        hdr = unpack(s_header.load());
        const HitRect *map = s_maps[hdr.index];
        if (map == nullptr || hdr.n == 0) {
            return false;
        }
        bool found = false;
        for (int i = hdr.n - 1; i >= 0; i--) {
            const HitRect &r = map[i];
            if (x >= r.x1 && x <= r.x2 && y >= r.y1 && y <= r.y2) {
                out = r;
                found = true;
                break;
            }
        }
        if (unpack(s_header.load()).gen == hdr.gen) {
            return found;
        }
    }
    return false;
}

void writePlate(const HitRect &r, const Header &hdr, int64_t tUs) {
    SleepAnimation::ElementDesc e;
    e.type = SleepAnimation::ElementType::RoundRect;
    e.alpha = LV_OPA_40;
    e.color = hdr.color;
    const int w = r.px2 - r.px1 + 1;
    const int h = r.py2 - r.py1 + 1;
    e.x = static_cast<int16_t>(r.px1 - hdr.outset);
    e.y = static_cast<int16_t>(r.py1 - hdr.outset);
    e.w = static_cast<int16_t>(w + 2 * hdr.outset);
    e.h = static_cast<int16_t>(h + 2 * hdr.outset);
    const int side = e.w < e.h ? e.w : e.h;
    int rad = side / 4;
    if (rad > 16) {
        rad = 16;
    }
    e.radius = static_cast<uint8_t>(rad);
    e.tUs = tUs;
    s_anim->setElement(s_plateElement, e);
}

void taskMain(void *) {
    bool wasPressed = false;
    int emptyRun = 0;
    bool plateUp = false;
    uint32_t pressGen = 0;
    uint32_t seq = 0;
    for (;;) {
        if (!s_pollEnabled.load()) {
            // Paused (debug A/B): touchpad_read falls back to reading the
            // controller itself, as it did before the task existed.
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        int16_t x = 0;
        int16_t y = 0;
        bool pressed = false;
        bool synthetic = false;
        {
            int16_t ix, iy;
            bool ip;
            if (touchInjectPoll(ix, iy, ip)) {
                x = ix;
                y = iy;
                pressed = ip;
                synthetic = true;
            } else {
                int16_t px = 0, py = 0;
                pressed = s_display->getPoint(&px, &py, 1) != 0;
                if (pressed) {
                    x = px;
                    y = py;
                }
            }
        }
        const int64_t now = esp_timer_get_time();
        // The GT911 rewrites its status register every 10 ms and the driver
        // clears it after every read, so a 5 ms poll reads "no touch" on
        // every other sample of a steady press. A release counts only when
        // the controller has reported no touch kReleaseSamples times in a
        // row (15 ms); a single empty sample keeps the last point.
        if (!pressed && wasPressed && !synthetic) {
            if (++emptyRun < kReleaseSamples) {
                pressed = true;
                x = s_sample.x;
                y = s_sample.y;
            }
        } else {
            emptyRun = 0;
        }
        if (!pressed && wasPressed) {
            // Keep the release at the last pressed point, as touchpad_read
            // did with its static x/y.
            x = s_sample.x;
            y = s_sample.y;
        }
        seq++;
        s_sampleSeq.fetch_add(1);
        s_sample.x = x;
        s_sample.y = y;
        s_sample.pressed = pressed ? 1 : 0;
        s_sample.synthetic = synthetic ? 1 : 0;
        s_sample.seq = seq;
        s_sample.tUs = now;
        s_sampleSeq.fetch_add(1);
        s_sampleCount.fetch_add(1);

        if (pressed != wasPressed) {
            // After the sample, never before: see the queue's comment.
            queueEdge(Transition{x, y, static_cast<uint8_t>(pressed ? 1 : 0), static_cast<uint8_t>(synthetic ? 1 : 0), now});
            wasPressed = pressed;
            g_touchEdgeAtUs = now;
            if (pressed) {
                Header hdr;
                HitRect r;
                if (hitTest(x, y, hdr, r) && hdr.plateOn && r.plate) {
                    writePlate(r, hdr, now);
                    plateUp = true;
                    pressGen = hdr.gen;
#ifdef GM_TOUCH_PROBE
                    g_probeElemEdgeUs = now;
#endif
                }
            } else if (plateUp) {
                s_anim->clearElement(s_plateElement);
                plateUp = false;
            }
        } else if (pressed && plateUp) {
            // A screen change under the finger: the plate's object is gone.
            if (unpack(s_header.load()).gen != pressGen) {
                s_anim->clearElement(s_plateElement);
                plateUp = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kPeriodMs));
    }
}

} // namespace

bool start(Display *display, SleepAnimation *anim, int plateElement) {
    if (s_running.load() || display == nullptr || anim == nullptr) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        if (s_maps[i] == nullptr) {
            s_maps[i] = static_cast<HitRect *>(heap_caps_malloc(sizeof(HitRect) * kMaxHitRects, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (s_maps[i] == nullptr) {
                return false;
            }
        }
    }
    s_display = display;
    s_anim = anim;
    s_plateElement = plateElement;
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(taskMain, "touch", kStackBytes, nullptr, 2, &s_task, 1,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ok = xTaskCreatePinnedToCore(taskMain, "touch", kStackBytes, nullptr, 2, &s_task, 1);
    }
    if (ok != pdPASS) {
        s_task = nullptr;
        return false;
    }
    s_running.store(true);
    return true;
}

bool running() { return s_running.load(); }

void setPollEnabled(bool on) {
    if (on && !s_pollEnabled.load()) {
        // Edges queued before the pause are stale; LVGL read the controller
        // itself in between.
        clearEdges();
    }
    s_pollEnabled.store(on);
}
bool pollEnabled() { return s_pollEnabled.load(); }

bool latest(Sample &out) {
    if (!s_running.load() || !s_pollEnabled.load()) {
        return false;
    }
    for (;;) {
        const uint32_t s1 = s_sampleSeq.load();
        if (s1 & 1u) {
            continue;
        }
        out = s_sample;
        if (s_sampleSeq.load() == s1) {
            break;
        }
    }
    // UI task only (LVGL's read callback). A queued edge comes first, in
    // order, whatever the finger is doing now; the live state only once the
    // queue is empty. So a tap shorter than one UI pass still becomes
    // PRESSED then RELEASED in LVGL, and so do two or more of them.
    Transition t;
    if (takeEdge(t)) {
        out.x = t.x;
        out.y = t.y;
        out.pressed = t.pressed;
        out.synthetic = t.synthetic;
        out.tUs = t.tUs;
    }
    return true;
}

void publishHitMap(const HitRect *rects, int n, bool plateOn, uint16_t plateColor565, int outset) {
    if (s_maps[0] == nullptr || s_maps[1] == nullptr) {
        return;
    }
    if (n > kMaxHitRects) {
        n = kMaxHitRects;
    }
    if (n < 0) {
        n = 0;
    }
    Header hdr = unpack(s_header.load());
    // The generation is how the task tells a screen change from the same
    // screen republished: it clears a held plate when the generation moves
    // under the finger. So an unchanged map must keep its generation, or the
    // plate would be cleared by the next UI pass, about 25 ms after a press.
    const uint8_t outsetClamped = static_cast<uint8_t>(outset < 0 ? 0 : (outset > 63 ? 63 : outset));
    if (hdr.gen != 0 && hdr.n == n && hdr.plateOn == (plateOn ? 1 : 0) && hdr.color == plateColor565 &&
        hdr.outset == outsetClamped &&
        (n == 0 || memcmp(s_maps[hdr.index], rects, sizeof(HitRect) * static_cast<size_t>(n)) == 0)) {
        return;
    }
    const uint8_t back = hdr.index ^ 1;
    if (n > 0) {
        memcpy(s_maps[back], rects, sizeof(HitRect) * static_cast<size_t>(n));
    }
    hdr.index = back;
    hdr.n = static_cast<uint8_t>(n);
    hdr.plateOn = plateOn ? 1 : 0;
    hdr.color = plateColor565;
    hdr.outset = outsetClamped;
    hdr.gen += 2;
    s_header.store(pack(hdr));
}

uint32_t hitMapGeneration() { return unpack(s_header.load()).gen; }
int hitMapCount() { return unpack(s_header.load()).n; }
uint32_t sampleCount() { return s_sampleCount.load(); }
uint32_t transitionsDropped() { return s_queueDropped.load(); }
uint32_t stackHighWaterBytes() {
    return s_task != nullptr ? static_cast<uint32_t>(uxTaskGetStackHighWaterMark(s_task)) * sizeof(StackType_t) : 0;
}

} // namespace touchtask
#endif // !GAGGIMATE_SIM
