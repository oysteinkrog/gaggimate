#pragma once
// Host stand-in for ESPToolKit/esp-memoryMonitor.
//
// The real library samples ESP-IDF heap regions through esp_heap_caps and runs
// its own sampler task, so it cannot build for platform = native. Without this
// shim src/display/core/MemoryMonitor.h fails to resolve its include and takes
// Controller.cpp and WebUIPlugin.cpp down with it, which is what broke the
// simulator build.
//
// Every type shape below is copied from the real header so the production code
// compiles unchanged; the behaviour is inert. On a desktop with gigabytes of
// heap there is nothing for the production thresholds (warn at 40 KB internal)
// to say. sampleNow() returns an empty snapshot, and the callers all already
// handle that -- WebUIPlugin's /api/debug/heap falls back to heap_caps_* when
// it finds no matching region, which is the path this takes.
//
// Keep in sync with .pio/libdeps/display/ESPMemoryMonitor if the production
// code starts using more of the API than is declared here. A missing member
// surfaces as a sim-only compile error, which is the intended failure mode.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

enum class MemoryRegion {
    Internal = 0,
    Psram = 1,
};

enum class ThresholdState {
    Normal = 0,
    Warn,
    Critical,
};

struct RegionThreshold {
    size_t warnBytes = 40 * 1024;
    size_t criticalBytes = 20 * 1024;
};

struct MemoryMonitorConfig {
    static constexpr BaseType_t any = tskNO_AFFINITY;

    uint32_t sampleIntervalMs = 1000;
    size_t historySize = 60;
    uint32_t stackSize = 4096 * sizeof(StackType_t);
    BaseType_t coreId = any;
    UBaseType_t priority = 1;
    uint32_t thresholdHysteresisBytes = 4 * 1024;
    RegionThreshold internal{};
    RegionThreshold psram{};
    bool enableSamplerTask = true;
    bool enableFragmentation = true;
    bool enableMinEverFree = true;
    bool enablePerTaskStacks = false;
    bool enableFailedAllocEvents = false;
    bool enableScopes = false;
    size_t maxScopesInHistory = 32;
    size_t windowStatsSize = 0;
    bool enableTaskTracking = false;
    size_t defaultTaskStackBytes = 4096;
    float stackWarnFraction = 0.25f;
    float stackCriticalFraction = 0.10f;
    size_t leakNoiseBytes = 1024;
    size_t maxLeakChecksInHistory = 16;
    bool usePSRAMBuffers = false;
};

struct WindowStats {
    size_t minFree = 0;
    size_t maxFree = 0;
    size_t avgFree = 0;
    float avgFragmentation = 0.0f;
};

struct RegionStats {
    MemoryRegion region{MemoryRegion::Internal};
    size_t freeBytes = 0;
    size_t minimumFreeBytes = 0;
    size_t largestFreeBlock = 0;
    float fragmentation = 0.0f;
    float freeBytesSlope = 0.0f;
    uint32_t secondsToWarn = 0;
    uint32_t secondsToCritical = 0;
    WindowStats window{};
};

struct ThresholdEvent {
    MemoryRegion region{MemoryRegion::Internal};
    ThresholdState state{ThresholdState::Normal};
    RegionStats stats{};
};

struct TaskStackUsage {
    std::string name;
    size_t freeHighWaterBytes = 0;
    UBaseType_t priority = 0;
    TaskHandle_t handle = nullptr;
};

struct FailedAllocEvent {
    size_t requestedBytes = 0;
    uint32_t caps = 0;
    const char *functionName = nullptr;
    uint64_t timestampUs = 0;
};

struct MemorySnapshot {
    uint64_t timestampUs = 0;
    std::vector<RegionStats> regions;
    std::vector<TaskStackUsage> stacks;
};

using ThresholdCallback = std::function<void(const ThresholdEvent &)>;
using SampleCallback = std::function<void(const MemorySnapshot &)>;
using FailedAllocCallback = std::function<void(const FailedAllocEvent &)>;
using PanicCallback = std::function<void(const MemorySnapshot &)>;

class ESPMemoryMonitor {
  public:
    bool init(const MemoryMonitorConfig &config = MemoryMonitorConfig{}) {
        _config = config;
        _initialized = true;
        return true;
    }
    void deinit() { _initialized = false; }
    bool isInitialized() const { return _initialized; }

    // Empty regions on purpose: callers fall back to a live heap_caps_* read
    // when a region is absent, which is the right answer on a host.
    MemorySnapshot sampleNow() { return MemorySnapshot{}; }
    std::vector<MemorySnapshot> history() const { return {}; }
    MemoryMonitorConfig currentConfig() const { return _config; }

    // Retained rather than dropped so a callback that captures by reference
    // does not look dead to a reader comparing sim and device behaviour. None
    // of them is ever invoked here.
    void onSample(SampleCallback cb) { _onSample = std::move(cb); }
    void onThreshold(ThresholdCallback cb) { _onThreshold = std::move(cb); }
    void onFailedAlloc(FailedAllocCallback cb) { _onFailedAlloc = std::move(cb); }
    void installPanicHook(PanicCallback cb) { _onPanic = std::move(cb); }

  private:
    MemoryMonitorConfig _config{};
    bool _initialized = false;
    SampleCallback _onSample;
    ThresholdCallback _onThreshold;
    FailedAllocCallback _onFailedAlloc;
    PanicCallback _onPanic;
};
