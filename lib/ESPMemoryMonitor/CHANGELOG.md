# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]
### Added
- More ESPMemoryMonitor examples: manual sampling without the background task and a shutdown-hook sketch that captures a final snapshot.
- `MemoryMonitorConfig::usePSRAMBuffers` to prefer PSRAM for monitor-owned long-lived containers via `ESPBufferManager` with automatic fallback to normal heap.
- Routed additional hot-path transient monitor scratch containers (threshold/task events, task-status capture, scope/tag threshold staging, and window analytics scratch vectors) through the same `ESPBufferManager` policy.
- Added internal/public model separation for history/scope/tag/task/leak state: monitor internals now use PSRAM-policy-aware storage models and convert to existing public API structs at return/callback boundaries.
- Switched sampler task lifecycle to native FreeRTOS task handling (`xTaskCreatePinnedToCore`/`vTaskDelete`).
- Added lifecycle teardown tests in `test/test_memory_monitor_lifecycle` covering pre-init `deinit()`, idempotent teardown, re-init, and destructor behavior.
### Fixed
- The failed-allocation ring has one consumer. `sampleNow()` from a task other than the running sampler no longer drains it, and a drain already in progress makes a second caller skip, so each failure is delivered once. `snapshotNow()` is new: a read-only copy for display that touches neither the ring, the history nor the callbacks.
- `installPanicHook` is now `installShutdownHook` (and `PanicCallback` is `ShutdownCallback`). It registers an ESP-IDF shutdown handler, which runs on `esp_restart()` and never on a panic, so the old name claimed crash coverage it never had.
- The failed-allocation hook no longer takes the monitor mutex or calls the user callback inside the failing allocation. It records the event in a fixed lock-free ring (8 slots, overflow counted by `failedAllocDropped()`) and the sampler delivers it on its next pass, at most one sample interval later. Before, an allocation failing on the sampler task while `sampleNow()` held the mutex deadlocked that task against itself. `failedAllocCount()` reports the total.
- Mark the failed-allocation hook instance pointer as static so Arduino builds compile the callback correctly.
- `deinit()` now releases monitor-owned container capacity (not just entries), fully clears runtime state, and resets config/runtime flags for deterministic re-init.

## [1.0.0] - 2025-12-02
### Added
- Initial ESPMemoryMonitor library with snapshot API, background sampler task, and ring-buffer history for DRAM + PSRAM.
- Per-region warn/critical thresholds with hysteresis and callbacks.
- Optional fragmentation scores, min-ever-free tracking, per-task stack high-water marks, and failed-allocation event hook.
- Basic Arduino example sketch demonstrating sampling and alerts.

[Unreleased]: https://github.com/ESPToolKit/esp-memoryMonitor/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/ESPToolKit/esp-memoryMonitor/releases/tag/v1.0.0
