#ifdef GAGGIMATE_QEMU

#include "QemuTouch.h"

#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/uart_ll.h>

#include <atomic>
#include <cstdlib>
#include <cstring>

namespace {

constexpr const char *LOG_TAG = "QemuTouch";

// The IDF console. Records are read straight off the hardware RX FIFO (see
// touchTask); log output already writes the TX FIFO the same way, and the
// port's configuration, baud rate included, stays whatever the console set.
constexpr uart_port_t TOUCH_UART = UART_NUM_0;

// A press is only believed this long after the record that reported it. Without
// the expiry a bridge that is killed mid-drag would leave the UI stuck with a
// finger down forever, and the host sends at roughly display rate so a live
// press always refreshes well inside the window.
constexpr int64_t PRESS_TTL_US = 250000;

constexpr size_t MAX_RECORD = 24;
// Generous upper bound on a coordinate: the emulated panel device tops out at
// 800x600, so anything past this is a corrupt record rather than a large screen.
// Bounding here is what keeps a bogus value from wrapping through uint16_t and
// reaching LVGL as a negative int16_t.
constexpr long MAX_COORD = 4095;

// One 32-bit word so a reader cannot see a half-updated coordinate pair; the
// timestamp is separate but only ever moves forward, and a stale-by-one-frame
// timestamp is indistinguishable from the sample rate anyway.
std::atomic<uint32_t> g_point{0};
std::atomic<int64_t> g_pressedAtUs{0};

void publish(long x, long y, bool down) {
    if (!down) {
        g_pressedAtUs.store(0, std::memory_order_release);
        return;
    }
    const uint32_t packed = (static_cast<uint32_t>(static_cast<uint16_t>(x)) << 16) | static_cast<uint16_t>(y);
    g_point.store(packed, std::memory_order_release);
    g_pressedAtUs.store(esp_timer_get_time(), std::memory_order_release);
}

// ESC 'T' x ',' y ',' d '\n'. Anything that does not fit the shape is dropped
// silently: the console is a shared channel and stray bytes are not an error.
void parseRecord(const char *record) {
    char *end = nullptr;
    const long x = strtol(record, &end, 10);
    if (end == record || *end != ',') {
        return;
    }
    const char *second = end + 1;
    const long y = strtol(second, &end, 10);
    if (end == second || *end != ',') {
        return;
    }
    const char *third = end + 1;
    const long down = strtol(third, &end, 10);
    // The record must end here: trailing bytes mean the shape did not match,
    // not that the prefix happened to parse.
    if (end == third || *end != '\0') {
        return;
    }
    if (x < 0 || y < 0 || x > MAX_COORD || y > MAX_COORD || (down != 0 && down != 1)) {
        return;
    }
    publish(x, y, down != 0);
}

void touchTask(void *arg) {
    (void)arg;
    enum class State { Idle, SawEscape, Body };
    State state = State::Idle;
    char record[MAX_RECORD];
    size_t len = 0;

    uart_dev_t *const hw = UART_LL_GET_HW(TOUCH_UART);
    uint8_t chunk[64];
    for (;;) {
        // Polled, not the uart driver. uart_read_bytes with portMAX_DELAY
        // loops until it has the whole requested length, so a 64 byte chunk
        // sat on a 12 byte tap record until two more taps arrived and then
        // parsed all three inside one LVGL input poll, which read them as one
        // press. Reading one byte at a time fixed that and still lost every
        // tap after the first: this QEMU fork's UART model raises the RX
        // interrupt once for a sparse host trickle and then not again, so the
        // driver's ring buffer never saw a second delivery. Reading the FIFO
        // length every 5 ms depends on neither.
        const uint32_t avail = uart_ll_get_rxfifo_len(hw);
        if (avail == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        const uint32_t want = avail < sizeof(chunk) ? avail : sizeof(chunk);
        uart_ll_read_rxfifo(hw, chunk, want);
        for (uint32_t i = 0; i < want; i++) {
            const char c = static_cast<char>(chunk[i]);
            switch (state) {
            case State::Idle:
                if (c == '\x1b') {
                    state = State::SawEscape;
                }
                break;
            case State::SawEscape:
                // A second ESC restarts rather than aborts, so a truncated
                // record cannot swallow the one that follows it.
                state = (c == 'T') ? State::Body : (c == '\x1b' ? State::SawEscape : State::Idle);
                len = 0;
                break;
            case State::Body:
                // An ESC mid-body means the previous record was truncated. Start
                // over on it rather than folding it into the body, which would
                // also lose the record that follows.
                if (c == '\x1b') {
                    state = State::SawEscape;
                    len = 0;
                } else if (c == '\n' || c == '\r') {
                    record[len] = '\0';
                    parseRecord(record);
                    state = State::Idle;
                } else if (len + 1 >= sizeof(record)) {
                    state = State::Idle; // overlong: not a record we wrote
                } else {
                    record[len++] = c;
                }
                break;
            }
        }
    }
}

} // namespace

namespace qemutouch {

void begin() {
    static bool started = false;
    if (started) {
        return;
    }

    // No uart_driver_install: touchTask polls the RX FIFO itself (its comment
    // says why), so there is no ring buffer or ISR to set up.
    if (xTaskCreatePinnedToCore(touchTask, "QemuTouch", 2560, nullptr, 5, nullptr, 1) != pdPASS) {
        ESP_LOGE(LOG_TAG, "failed to start parser task");
        return;
    }

    started = true;
    ESP_LOGI(LOG_TAG, "listening for host touch records on UART%d", (int)TOUCH_UART);
}

bool read(int16_t *x, int16_t *y) {
    const int64_t pressedAt = g_pressedAtUs.load(std::memory_order_acquire);
    if (pressedAt == 0 || esp_timer_get_time() - pressedAt > PRESS_TTL_US) {
        return false;
    }
    const uint32_t packed = g_point.load(std::memory_order_acquire);
    if (x != nullptr) {
        *x = static_cast<int16_t>(packed >> 16);
    }
    if (y != nullptr) {
        *y = static_cast<int16_t>(packed & 0xffff);
    }
    return true;
}

} // namespace qemutouch

#endif // GAGGIMATE_QEMU
