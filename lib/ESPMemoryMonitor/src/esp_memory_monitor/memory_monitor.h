#pragma once

#include <Arduino.h>
#if defined(__has_include)
#if __has_include(<ArduinoJson.h>)
#include <ArduinoJson.h>
#define ESPMM_HAS_ARDUINOJSON 1
#endif
#endif

#ifndef ESPMM_HAS_ARDUINOJSON
#define ESPMM_HAS_ARDUINOJSON 0
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

extern "C" {
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
}

#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "memory_monitor_allocator.h"

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

struct TagBudget {
    size_t warnBytes = 0;
    size_t criticalBytes = 0;
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
    // GaggiMate change from upstream. Off, a sample reads only the heap's own
    // free-bytes and min-ever-free counters, which are O(1). On, it additionally
    // calls heap_caps_get_info(), which walks every block in the heap to find
    // the largest free one, and that is the only way to get largestFreeBlock or
    // fragmentation.
    //
    // The walk is not cheap and on this board it is not harmless. Measured on an
    // ESP32-S3 with 4.5 MB of octal PSRAM free: 209-341 us for the internal heap
    // and 1030-1253 us for the PSRAM heap, about 1.3 ms per sample. The RGB
    // panel's bounce buffer must be refilled from PSRAM every 162 us, and both
    // heaps' block headers are read over the same MSPI controller the scan-out
    // streams pixels through, so a walk starves the refill for roughly eight
    // deadlines in a row. The result was one visibly displaced frame per sample,
    // at exactly the sample interval -- confirmed by moving the interval from 60 s
    // to 20 s and watching the glitch period follow.
    //
    // Defaulted true so upstream behaviour is unchanged for anyone else; this
    // project turns it off and asks for fragmentation on demand instead.
    bool enableHeapWalk = true;
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
    // GaggiMate change from upstream. Puts the sampler task's stack in PSRAM
    // (xTaskCreatePinnedToCoreWithCaps). The sampler only reads heap counters
    // and logs, so it never runs with the flash cache disabled, which is the
    // condition a PSRAM stack must never meet. Internal DRAM is the scarce
    // resource on the ESP32-S3 display; 4 KB of it matters.
    bool stackInPSRAM = false;
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
    eTaskState state = eInvalid;
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

using MemoryTag = uint16_t;
constexpr MemoryTag kInvalidMemoryTag = 0;

struct ScopeStats {
    std::string name;
    MemoryTag tag = kInvalidMemoryTag;
    size_t startInternalFreeBytes = 0;
    size_t startPsramFreeBytes = 0;
    size_t endInternalFreeBytes = 0;
    size_t endPsramFreeBytes = 0;
    int64_t deltaInternalBytes = 0;
    int64_t deltaPsramBytes = 0;
    uint64_t durationUs = 0;
    uint64_t startedUs = 0;
    uint64_t endedUs = 0;
};

struct TagUsage {
    MemoryTag tag = kInvalidMemoryTag;
    std::string name;
    size_t totalInternalBytes = 0;
    size_t totalPsramBytes = 0;
    ThresholdState state{ThresholdState::Normal};
};

struct TagThresholdEvent {
    TagUsage usage;
    TagBudget budget;
    ScopeStats lastScope;
};

enum class StackState {
    Safe = 0,
    Warn,
    Critical,
};

struct TaskStackThreshold {
    size_t warnBytes = 0;
    size_t criticalBytes = 0;
};

struct TaskStackEvent {
    TaskStackUsage usage;
    StackState state{StackState::Safe};
    bool appeared = false;
    bool disappeared = false;
};

struct LeakCheckDelta {
    MemoryRegion region{MemoryRegion::Internal};
    float averageStartFreeBytes = 0.0f;
    float averageEndFreeBytes = 0.0f;
    float averageStartFragmentation = 0.0f;
    float averageEndFragmentation = 0.0f;
    float deltaFreeBytes = 0.0f;
    float deltaFragmentation = 0.0f;
};

struct LeakCheckResult {
    std::string fromLabel;
    std::string toLabel;
    std::vector<LeakCheckDelta> deltas;
    bool leakSuspected = false;
};

using ThresholdCallback = std::function<void(const ThresholdEvent &)>;
using SampleCallback = std::function<void(const MemorySnapshot &)>;
using FailedAllocCallback = std::function<void(const FailedAllocEvent &)>;
using ScopeCallback = std::function<void(const ScopeStats &)>;
using TagThresholdCallback = std::function<void(const TagThresholdEvent &)>;
using TaskStackThresholdCallback = std::function<void(const TaskStackEvent &)>;
using LeakCheckCallback = std::function<void(const LeakCheckResult &)>;
using ShutdownCallback = std::function<void(const MemorySnapshot &)>;

class MemoryScope;
class ESPMemoryMonitor;

class MemoryScope {
  public:
    MemoryScope() = default;
    ~MemoryScope();

    MemoryScope(const MemoryScope &) = delete;
    MemoryScope &operator=(const MemoryScope &) = delete;
    MemoryScope(MemoryScope &&other) noexcept;
    MemoryScope &operator=(MemoryScope &&other) noexcept;

    ScopeStats end();
    bool active() const { return _monitor != nullptr && !_ended; }

  private:
    friend class ESPMemoryMonitor;
    MemoryScope(ESPMemoryMonitor *monitor, std::string name, MemoryTag tag, size_t startInternal, size_t startPsram,
                uint64_t startUs);

    ESPMemoryMonitor *_monitor = nullptr;
    std::string _name;
    MemoryTag _tag = kInvalidMemoryTag;
    size_t _startInternal = 0;
    size_t _startPsram = 0;
    uint64_t _startUs = 0;
    bool _ended = false;
};

class ESPMemoryMonitor {
  public:
    friend class MemoryScope;
    ESPMemoryMonitor();
    ~ESPMemoryMonitor();

    bool init(const MemoryMonitorConfig &config = MemoryMonitorConfig{});
    void deinit();
    bool isInitialized() const { return _initialized; }

    // Take a sample and run everything a sample drives: history, threshold
    // and stack callbacks, the sample callback, and delivery of the failed
    // allocations the heap hook recorded. The sampler task calls this; with
    // the sampler disabled, the application calls it instead. While the
    // sampler runs, a call from any other task still samples but leaves the
    // failed-allocation ring to the sampler, so each event is delivered once.
    MemorySnapshot sampleNow();
    // A current reading for display, safe from any task: regions (with window
    // statistics from the history) and stacks, as a copy. It appends nothing
    // to the history, fires no callback and delivers no failed-allocation
    // event, so a web request polling it cannot reorder or steal what the
    // sampler reports.
    MemorySnapshot snapshotNow() const;
    std::vector<MemorySnapshot> history() const;
    MemoryMonitorConfig currentConfig() const;

    void onSample(SampleCallback callback);
    void onThreshold(ThresholdCallback callback);
    void onFailedAlloc(FailedAllocCallback callback);
    // Failed allocations recorded by the heap hook since init(), and how many
    // of them the delivery ring dropped because the sampler had not drained it.
    // The callback runs on the sampler task, at most sampleIntervalMs after
    // the failure, never inside the failing allocation.
    uint32_t failedAllocCount() const { return _allocFailCount.load(std::memory_order_relaxed); }
    uint32_t failedAllocDropped() const { return _allocDropped.load(std::memory_order_relaxed); }
    void onScope(ScopeCallback callback);
    void onTagThreshold(TagThresholdCallback callback);
    void onTaskStackThreshold(TaskStackThresholdCallback callback);
    void onLeakCheck(LeakCheckCallback callback);

    MemoryScope beginScope(const std::string &name, MemoryTag tag = kInvalidMemoryTag);
    MemoryTag registerTag(const std::string &name);
    bool setTagBudget(MemoryTag tag, const TagBudget &budget);
    std::vector<ScopeStats> scopeHistory() const;
    std::vector<TagUsage> tagUsage() const;

    LeakCheckResult markLeakCheckPoint(const std::string &label);

    bool setTaskStackThreshold(const std::string &taskName, const TaskStackThreshold &threshold);

    // Registers with esp_register_shutdown_handler, so the callback runs on
    // esp_restart() (a requested restart, an OTA reboot) and never on a panic,
    // an abort or a watchdog reset: the panic handler does not call shutdown
    // handlers. It is a last snapshot before a planned reboot, not a crash
    // report; for a crash, read the core dump.
    bool installShutdownHook(ShutdownCallback callback = nullptr);
    void uninstallShutdownHook();

  private:
    enum class AllocHookType {
        None = 0,
        WithArg,
        NoArg,
    };

    struct LockGuard {
        explicit LockGuard(SemaphoreHandle_t handle) : _handle(handle) {
            if (_handle != nullptr) {
                xSemaphoreTake(_handle, portMAX_DELAY);
            }
        }

        ~LockGuard() {
            if (_handle != nullptr) {
                xSemaphoreGive(_handle);
            }
        }

        LockGuard(const LockGuard &) = delete;
        LockGuard &operator=(const LockGuard &) = delete;

      private:
        SemaphoreHandle_t _handle;
    };

    static void samplerTaskThunk(void *arg);
    void samplerTaskLoop();

    static void allocFailedHook(size_t requestedBytes, uint32_t caps, const char *functionName);
    static ESPMemoryMonitor *_failedAllocInstance;

    struct InternalTaskStackUsage {
        InternalTaskStackUsage() = default;
        explicit InternalTaskStackUsage(bool usePSRAMBuffers) : name(MemoryMonitorAllocator<char>(usePSRAMBuffers)) {}

        MemoryMonitorString name;
        size_t freeHighWaterBytes = 0;
        eTaskState state = eInvalid;
        UBaseType_t priority = 0;
        TaskHandle_t handle = nullptr;
    };

    struct InternalMemorySnapshot {
        InternalMemorySnapshot() = default;
        explicit InternalMemorySnapshot(bool usePSRAMBuffers)
            : regions(MemoryMonitorAllocator<RegionStats>(usePSRAMBuffers)),
              stacks(MemoryMonitorAllocator<InternalTaskStackUsage>(usePSRAMBuffers)) {}

        uint64_t timestampUs = 0;
        MemoryMonitorVector<RegionStats> regions;
        MemoryMonitorVector<InternalTaskStackUsage> stacks;
    };

    struct InternalScopeStats {
        InternalScopeStats() = default;
        explicit InternalScopeStats(bool usePSRAMBuffers) : name(MemoryMonitorAllocator<char>(usePSRAMBuffers)) {}

        MemoryMonitorString name;
        MemoryTag tag = kInvalidMemoryTag;
        size_t startInternalFreeBytes = 0;
        size_t startPsramFreeBytes = 0;
        size_t endInternalFreeBytes = 0;
        size_t endPsramFreeBytes = 0;
        int64_t deltaInternalBytes = 0;
        int64_t deltaPsramBytes = 0;
        uint64_t durationUs = 0;
        uint64_t startedUs = 0;
        uint64_t endedUs = 0;
    };

    struct InternalTagUsage {
        InternalTagUsage() = default;
        explicit InternalTagUsage(bool usePSRAMBuffers) : name(MemoryMonitorAllocator<char>(usePSRAMBuffers)) {}

        MemoryTag tag = kInvalidMemoryTag;
        MemoryMonitorString name;
        size_t totalInternalBytes = 0;
        size_t totalPsramBytes = 0;
        ThresholdState state{ThresholdState::Normal};
    };

    struct InternalLeakCheckResult {
        InternalLeakCheckResult() = default;
        explicit InternalLeakCheckResult(bool usePSRAMBuffers)
            : fromLabel(MemoryMonitorAllocator<char>(usePSRAMBuffers)), toLabel(MemoryMonitorAllocator<char>(usePSRAMBuffers)),
              deltas(MemoryMonitorAllocator<LeakCheckDelta>(usePSRAMBuffers)) {}

        MemoryMonitorString fromLabel;
        MemoryMonitorString toLabel;
        MemoryMonitorVector<LeakCheckDelta> deltas;
        bool leakSuspected = false;
    };

    InternalMemorySnapshot captureSnapshot() const;
    RegionStats captureRegion(uint32_t caps, MemoryRegion region) const;
    MemoryMonitorVector<InternalTaskStackUsage> captureStacks() const;
    ScopeStats finalizeScope(const MemoryScope &scope);
    ScopeStats toPublicScopeStats(const InternalScopeStats &stats) const;
    TagUsage toPublicTagUsage(const InternalTagUsage &usage) const;
    TaskStackUsage toPublicTaskStackUsage(const InternalTaskStackUsage &usage) const;
    MemorySnapshot toPublicSnapshot(const InternalMemorySnapshot &snapshot) const;
    LeakCheckResult toPublicLeakCheckResult(const InternalLeakCheckResult &result) const;

    void appendHistoryLocked(const InternalMemorySnapshot &snapshot);
    MemoryMonitorVector<ThresholdEvent> evaluateThresholdsLocked(const InternalMemorySnapshot &snapshot);
    ThresholdState evaluateState(ThresholdState current, const RegionThreshold &threshold, size_t freeBytes) const;
    void handleAllocEvent(size_t requestedBytes, uint32_t caps, const char *functionName);
    void drainAllocEvents(const FailedAllocCallback &cb);

    void enrichSnapshotLocked(InternalMemorySnapshot &snapshot) const;
    void appendScopeLocked(const InternalScopeStats &stats);
    MemoryMonitorVector<TagThresholdEvent> evaluateTagThresholdsLocked(const InternalScopeStats &stats);
    ThresholdState evaluateRisingState(ThresholdState current, const TagBudget &budget, size_t usageBytes) const;
    StackState computeStackState(const InternalTaskStackUsage &usage) const;
    void trackTasksLocked(const InternalMemorySnapshot &snapshot, MemoryMonitorVector<TaskStackEvent> &events);
    InternalLeakCheckResult buildLeakCheckLocked(const std::string &label);
    void runShutdownHook();
    static void shutdownThunk();

    bool registerFailedAllocCallback();
    void unregisterFailedAllocCallback();
    bool registerShutdownHandler();
    void unregisterShutdownHandler();
    void resetOwnedContainers();

    MemoryMonitorConfig _config{};
    bool _initialized = false;
    bool _running = false;
    bool _usePSRAMBuffers = false;
    SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _samplerTask = nullptr;
    volatile bool _samplerExited = false; // set by the sampler once its loop has returned
    MemoryMonitorDeque<InternalMemorySnapshot> _history;
    std::array<ThresholdState, 2> _thresholdStates{ThresholdState::Normal, ThresholdState::Normal};
    SampleCallback _sampleCallback;
    ThresholdCallback _thresholdCallback;
    FailedAllocCallback _allocCallback;
    // Failed-allocation events are recorded here by the heap hook and delivered
    // by the sampler. The hook runs synchronously inside the failing
    // heap_caps_malloc, on whatever task made the call, so it must not take
    // _mutex (the sampler holds it while it grows its own containers, and an
    // allocation failing there would deadlock the task against itself) and
    // must not allocate. Multi-producer: a slot is claimed with a CAS on the
    // head, filled, then published through its sequence word. Single
    // consumer: drainAllocEvents holds _allocDraining for the whole drain, so
    // a second caller skips instead of reading a slot twice.
    static constexpr uint32_t kAllocRingSize = 8;
    FailedAllocEvent _allocRing[kAllocRingSize]{};
    std::atomic<uint32_t> _allocRingSeq[kAllocRingSize]{};
    std::atomic<uint32_t> _allocHead{0};
    std::atomic<uint32_t> _allocTail{0};
    std::atomic<uint32_t> _allocFailCount{0};
    std::atomic<uint32_t> _allocDropped{0};
    std::atomic<bool> _allocDraining{false};
    ScopeCallback _scopeCallback;
    TagThresholdCallback _tagThresholdCallback;
    TaskStackThresholdCallback _taskStackCallback;
    LeakCheckCallback _leakCallback;
    ShutdownCallback _shutdownCallback;
    MemoryMonitorDeque<InternalScopeStats> _scopeHistory;
    MemoryMonitorVector<InternalTagUsage> _tagUsage;
    MemoryMonitorVector<TagBudget> _tagBudgets;
    MemoryMonitorUnorderedMap<MemoryMonitorString, TaskStackThreshold> _taskThresholds;
    MemoryMonitorUnorderedMap<TaskHandle_t, InternalTaskStackUsage> _knownTasks;
    MemoryMonitorDeque<InternalLeakCheckResult> _leakHistory;
    MemoryMonitorDeque<uint64_t> _leakCheckpoints;
    bool _shutdownHookInstalled = false;
};

#if ESPMM_HAS_ARDUINOJSON
void toJson(const MemorySnapshot &snap, JsonDocument &doc);
#endif
