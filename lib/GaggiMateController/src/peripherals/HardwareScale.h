#ifndef HARDWARESCALE_H
#define HARDWARESCALE_H

#include <Arduino.h>
#include <atomic>
#include <functional>

constexpr float HARDWARE_SCALE_UNAVAILABLE = -9999.0f; // Sentinel value to signal scale not available

constexpr uint16_t HARDWARE_SCALE_DEFAULT_SAMPLE_RATE_SPS = 10;
constexpr float HARDWARE_SCALE_DEFAULT_FILTER_ALPHA_IDLE = 0.80f;
constexpr float HARDWARE_SCALE_DEFAULT_FILTER_ALPHA_ACTIVE = 0.80f;

struct HardwareScaleConfig {
    uint16_t sampleRateSps = HARDWARE_SCALE_DEFAULT_SAMPLE_RATE_SPS;
    float idleAlpha = HARDWARE_SCALE_DEFAULT_FILTER_ALPHA_IDLE;
    float activeAlpha = HARDWARE_SCALE_DEFAULT_FILTER_ALPHA_ACTIVE;
};

// Idle publishing uses spatial hysteresis rather than more temporal filtering,
// so a real weight change remains as responsive as the fast internal estimate.
constexpr float SCALE_DISPLAY_STEP_GRAMS = 0.10f;
constexpr float SCALE_DISPLAY_SWITCH_GRAMS = 0.07f;

// Track only small, stable, idle zero drift. Brew/water activity disables this
// path via setBrewingActive(), leaving shot measurements untouched.
constexpr float SCALE_ZERO_TRACK_WINDOW_GRAMS = 0.35f;
constexpr uint8_t SCALE_ZERO_TRACK_MEDIAN_SAMPLES = 3;
constexpr uint8_t SCALE_ZERO_TRACK_STABILITY_SAMPLES = 10;
constexpr float SCALE_ZERO_TRACK_MAX_RANGE_GRAMS = 0.15f;
constexpr float SCALE_ZERO_TRACK_ALPHA = 0.015f;
constexpr float SCALE_ZERO_TRACK_MAX_BIAS_GRAMS = 1.0f;

using scale_reading_callback_t =
    std::function<void(float weight, float cell1Weight, float cell2Weight, bool cell1Valid, bool cell2Valid)>;
using scale_configuration_callback_t = std::function<void(float scaleFactor1, float scaleFactor2)>;
using tare_result_callback_t = std::function<void(bool success)>;
using void_callback_t = std::function<void()>;

class HardwareScale {
  public:
    HardwareScale(uint8_t data_pin1, uint8_t data_pin2, uint8_t clock_pin, const scale_reading_callback_t &reading_callback,
                  const scale_configuration_callback_t &config_callback);
    ~HardwareScale() = default;

    struct RawReading {
        long value1;
        long value2;
    };

    void setup();
    void loop();
    float getWeight() const;
    inline RawReading getRawWeight() const { return _raw_weight; }
    void setScaleFactors(float scale_factor1, float scale_factor2);
    // Validates and queues the configuration; the scale task applies it before
    // its next conversion. Never blocks, so it is safe from the BLE dispatch task.
    void setConfiguration(float scale_factor1, float scale_factor2, uint16_t sample_rate_sps, float idle_alpha,
                          float active_alpha);
    void calibrateScale(uint8_t scale, float calibrationWeight);
    void setBrewingActive(bool active);
    bool isReady();
    bool isAvailable() const { return is_initialized; }
    // Queues a tare for the scale task and returns at once. The task runs it
    // before its next published reading (0.5 to about 1.2 s at 10 SPS) and then
    // calls the tare result callback on the scale task with the outcome.
    void requestTare();
    void onTareResult(tare_result_callback_t callback) { _tare_result_callback = std::move(callback); }

  private:
    std::atomic<bool> is_initialized;
    std::atomic<bool> _scale_factors_ready;
    uint8_t _data_pin1;
    uint8_t _data_pin2;
    uint8_t _clock_pin;
    RawReading _raw_weight;
    std::atomic<float> _weight{0.0f};
    float _scale_factor1;
    float _scale_factor2;
    float _offset1;
    float _offset2;
    bool _has_accepted_reading = false;
    bool _has_pending_outlier = false;
    float _previous_accepted_reading = 0.0f;
    float _last_accepted_reading = 0.0f;
    float _pending_outlier = 0.0f;
    unsigned long _read_failure_started_ms = 0;
    bool _read_fault_reported = false;
    // esp_timer microseconds, so a deadline left over from a brew weeks ago can
    // never look like a future one (a 32-bit millis() deadline did after 24.8
    // days). Guarded by _request_mux because a 64-bit store is not atomic here.
    int64_t _responsive_until_us = 0;
    std::atomic<bool> _tare_requested{false};
    bool _config_pending = false;       // guarded by _request_mux
    HardwareScaleConfig _pending_config; // guarded by _request_mux
    float _pending_scale_factor1 = 0.0f; // guarded by _request_mux
    float _pending_scale_factor2 = 0.0f; // guarded by _request_mux
    bool _has_published = false;
    float _last_published_output = 0.0f;
    float _published_weight = 0.0f;
    float _zero_bias = 0.0f;
    float _zero_median_samples[SCALE_ZERO_TRACK_MEDIAN_SAMPLES]{};
    uint8_t _zero_median_count = 0;
    uint8_t _zero_median_index = 0;
    float _zero_stability_samples[SCALE_ZERO_TRACK_STABILITY_SAMPLES]{};
    uint8_t _zero_stability_count = 0;
    uint8_t _zero_stability_index = 0;
    unsigned long _last_zero_tracking_observation_ms = 0;
    unsigned long _last_publish_ms = 0;
    unsigned long _last_conversion_us = 0;
    unsigned long _interval_log_started_ms = 0;
    uint32_t _interval_count = 0;
    uint64_t _interval_total_us = 0;
    uint32_t _interval_min_us = UINT32_MAX;
    uint32_t _interval_max_us = 0;
    HardwareScaleConfig _config;
    scale_reading_callback_t _reading_callback;
    scale_configuration_callback_t _configuration_callback;
    tare_result_callback_t _tare_result_callback;
    TaskHandle_t taskHandle;
    SemaphoreHandle_t _operation_mutex;
    portMUX_TYPE _read_mux = portMUX_INITIALIZER_UNLOCKED;
    // Short critical sections for requests from other tasks (tare, config,
    // brewing deadline). Never held across an HX711 read.
    mutable portMUX_TYPE _request_mux = portMUX_INITIALIZER_UNLOCKED;

    const char *LOG_TAG = "HardwareScale";
    [[noreturn]] static void loopTask(void *arg);

    RawReading readRaw();
    bool waitUntilReady(unsigned long timeoutMs) const;
    bool convertRawToWeight(const RawReading &raw, float &weight, float &cell1Weight, float &cell2Weight) const;
    bool acceptReading(float reading, float &accepted);
    bool isResponsive() const;
    unsigned long readyTimeoutMs() const;
    uint8_t tareSampleCount() const;
    uint8_t calibrationSampleCount() const;
    void recordConversionInterval();
    bool tareInternal(bool allowUnstableFallback);
    void serviceRequests();
    void applyConfiguration(float scale_factor1, float scale_factor2, const HardwareScaleConfig &config);
    void resetFilterState();
    void resetZeroTrackingHistory();
};

#endif // HARDWARESCALE_H
