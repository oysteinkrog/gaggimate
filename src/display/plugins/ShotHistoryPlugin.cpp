#include "ShotHistoryPlugin.h"

#include <LittleFS.h>
#include <SD_MMC.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <display/core/Controller.h>
#include <display/core/ProfileManager.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/utils.h>
#include <display/drivers/common/PanelClock.h>
#include <display/models/shot_log_format.h>
#include <display/util/PsramAllocator.h>
#include <display/util/SafeReplace.h>
#include <esp32-hal-psram.h>
#ifndef GAGGIMATE_SIM
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr float TEMP_SCALE = 10.0f;
constexpr float PRESSURE_SCALE = 10.0f;
constexpr float FLOW_SCALE = 100.0f;
constexpr float WEIGHT_SCALE = 10.0f;
constexpr float RESISTANCE_SCALE = 100.0f;

constexpr uint16_t TEMP_MAX_VALUE = 2000;    // 200.0 °C
constexpr uint16_t PRESSURE_MAX_VALUE = 200; // 20.0 bar
constexpr uint16_t WEIGHT_MAX_VALUE = 10000; // 1000.0 g
constexpr uint16_t RESISTANCE_MAX_VALUE = 0xFFFF;
constexpr int16_t FLOW_MIN_VALUE = -2000; // -20.00 ml/s
constexpr int16_t FLOW_MAX_VALUE = 2000;  //  20.00 ml/s

// Largest believable change in scale weight within one sample interval. A real
// espresso never adds >5 g in 250 ms (that is already 20 ml/s, the saturation
// point of the vf field); anything larger is a scale/BLE glitch and must not be
// folded into the flow EMA, where a single bad reading would otherwise pin vf at
// the ±20 floor for seconds while the EMA bleeds off. See GM-110.
constexpr float MAX_PLAUSIBLE_WEIGHT_DELTA = 5.0f; // grams per sample

// How long after a shot's finalisation the next shot's log is pre-opened
// (gm-0api). The walks the prepare costs land on core 0 at the render task's
// priority, so they wait until the finished screen has had its time.
constexpr unsigned long PREPARE_DELAY_AFTER_SHOT_MS = 20000;

uint16_t encodeUnsigned(float value, float scale, uint16_t maxValue) {
    if (!std::isfinite(value)) {
        return 0;
    }
    float scaled = value * scale;
    if (scaled < 0.0f) {
        scaled = 0.0f;
    }
    scaled += 0.5f;
    uint32_t fixed = static_cast<uint32_t>(scaled);
    if (fixed > maxValue) {
        fixed = maxValue;
    }
    return static_cast<uint16_t>(fixed);
}

int16_t encodeSigned(float value, float scale, int16_t minValue, int16_t maxValue) {
    if (!std::isfinite(value)) {
        return 0;
    }
    float scaled = value * scale;
    if (scaled >= 0.0f) {
        scaled += 0.5f;
    } else {
        scaled -= 0.5f;
    }
    int32_t fixed = static_cast<int32_t>(scaled);
    if (fixed < minValue) {
        fixed = minValue;
    }
    if (fixed > maxValue) {
        fixed = maxValue;
    }
    return static_cast<int16_t>(fixed);
}

String padId(String id, int length = 6) {
    while (id.length() < length) {
        id = "0" + id;
    }
    return id;
}

// A shot id as the web client sends it: digits only, at most 12 of them. The
// id becomes part of a path under /h, so anything else is refused.
bool validShotId(const String &id) {
    if (id.length() == 0 || id.length() > 12) {
        return false;
    }
    for (size_t i = 0; i < id.length(); i++) {
        if (id[i] < '0' || id[i] > '9') {
            return false;
        }
    }
    return true;
}

// Lists the names in /h without opening the entries. File::openNextFile()
// opens every entry it returns, and on FAT each open is a linear scan of the
// directory, so a walk that way is quadratic in the shot count (the boot sat
// in one for eight minutes on the bench card, see CLAUDE.md). readdir() reads
// the names straight out of the directory clusters. The simulator's FS shim
// has no mount point and keeps the File walk.
void listHistoryNames(fs::FS &fs, std::vector<String> &names) {
#ifndef GAGGIMATE_SIM
    const char *mount = fs.mountpoint();
    if (mount != nullptr) {
        const String path = String(mount) + "/h";
        DIR *d = opendir(path.c_str());
        if (d == nullptr) {
            if (errno != ENOENT) {
                ESP_LOGW("ShotHistoryPlugin", "opendir %s: %d", path.c_str(), errno);
            }
            return;
        }
        while (const dirent *e = readdir(d)) {
            names.emplace_back(e->d_name);
        }
        closedir(d);
        return;
    }
#endif
    File directory = fs.open("/h");
    if (!directory || !directory.isDirectory()) {
        if (directory) {
            directory.close();
        }
        return;
    }
    File file = directory.openNextFile();
    while (file) {
        names.emplace_back(file.name());
        file.close();
        file = directory.openNextFile();
    }
    directory.close();
}

// Size of a file under /h, or -1. One lookup; the caller asks only for the
// oldest shots, whose entries sit at the start of the directory.
long historyFileSize(fs::FS &fs, const String &name) {
#ifndef GAGGIMATE_SIM
    const char *mount = fs.mountpoint();
    if (mount != nullptr) {
        struct stat st {};
        const String path = String(mount) + "/h/" + name;
        if (stat(path.c_str(), &st) != 0) {
            return -1;
        }
        return static_cast<long>(st.st_size);
    }
#endif
    File f = fs.open("/h/" + name, "r");
    if (!f) {
        return -1;
    }
    const long size = static_cast<long>(f.size());
    f.close();
    return size;
}
} // namespace

ShotHistoryPlugin ShotHistory;

void ShotHistoryPlugin::setup(Controller *c, PluginManager *pm) {
    controller = c;
    pluginManager = pm;
    if (controller->isSDCard()) {
        fs = &SD_MMC;
        ESP_LOGI("ShotHistoryPlugin", "Logging shot history to SD card");
    }
    // A FAT lookup in /h walks the whole directory (about 2 s a walk on a card
    // with 3,000 shots), so on the SD card the next shot's log and index.bin
    // are opened at idle and held through the shot (gm-0api). LittleFS keeps
    // the open-per-use path. The simulator holds them too, so the host build
    // runs the same code the board does.
    holdHandles = controller->isSDCard();
#ifdef GAGGIMATE_SIM
    holdHandles = true;
#endif
    // The first shot's log and the index are opened on the history task's
    // first idle pass (record()). Armed here rather than in loopTask because
    // the simulator never runs that task; its main loop calls record().
    armPrepare(0);
    pm->on("controller:brew:start", [this](Event const &) { startRecording(); });
    pm->on("controller:brew:end", [this](Event const &) { endRecording(); });
    pm->on("controller:brew:clear", [this](Event const &) { endExtendedRecording(); });
    pm->on("controller:volumetric-measurement:estimation:change",
           [this](Event const &event) { currentEstimatedWeight = event.getFloat("value"); });
    pm->on("controller:volumetric-measurement:active:change",
           [this](Event const &event) { currentActiveWeight = event.getFloat("value"); });
    pm->on("boiler:currentTemperature:change", [this](Event const &event) { currentTemperature = event.getFloat("value"); });
    pm->on("pump:puck-resistance:change", [this](Event const &event) { currentPuckResistance = event.getFloat("value"); });
    // Initialize rebuild state
    rebuildInProgress = false;
    // Leftover from the abandoned separate recent-shots index; aggregates now live in index.bin.
    if (fs->exists("/h/recent.bin")) {
        fs->remove("/h/recent.bin");
    }
    // A notes save interrupted between its .tmp and the rename leaves a .bak
    // or a .tmp under /h; settle them before the history is read.
    saferep::recoverReplace(*fs, "/h", ".json", "ShotHistoryPlugin");
    // record() samples into a 4 KB buffer and flushes it to the filesystem;
    // the index rebuild has its own, larger task. Measured idle high-water
    // mark was ~500 B used of 9 KB; 6 KB leaves the flush path ample room and
    // gives 3 KB back to the internal heap.
    xTaskCreatePinnedToCore(loopTask, "ShotHistoryPlugin::loop", configMINIMAL_STACK_SIZE * 4, this, 1, &taskHandle, 0);
}

void ShotHistoryPlugin::record() {
    bool shouldRecord = recording || extendedRecording;

    if (shouldRecord && (controller->getMode() == MODE_BREW || extendedRecording)) {
        if (!isFileOpen) {
            if (preparedLog && preparedId == currentId) {
                // Pre-opened at idle (prepareNextShot): no directory walk here.
                currentFile = preparedLog;
                preparedLog = File();
                preparedId = "";
                ESP_LOGI("ShotHistoryPlugin", "Shot %s uses the pre-opened log", currentId.c_str());
            } else {
                if (holdHandles) {
                    ESP_LOGW("ShotHistoryPlugin", "Shot %s opens its log during the shot (prepared: %s)", currentId.c_str(),
                             preparedId.length() > 0 ? preparedId.c_str() : "none");
                }
                // No prepared file, or one for another id (a rebuild moved the
                // counter): the old path, with its walks.
                dropPreparedLog(true);
                if (!fs->exists("/h")) {
                    fs->mkdir("/h");
                }
                currentFile = fs->open("/h/" + currentId + ".slog", FILE_WRITE);
            }
            if (currentFile) {
                isFileOpen = true;
                // Prepare header
                memset(&header, 0, sizeof(header));
                header.magic = SHOT_LOG_MAGIC;
                header.version = SHOT_LOG_VERSION;
                header.reserved0 = (uint8_t)SHOT_LOG_SAMPLE_SIZE; // record sample size actually used
                header.headerSize = SHOT_LOG_HEADER_SIZE;
                header.sampleInterval = SHOT_LOG_SAMPLE_INTERVAL_MS;
                header.fieldsMask = SHOT_LOG_FIELDS_MASK_ALL;
                header.startEpoch = getTime();
                Profile profile = controller->getProfileManager()->getSelectedProfile();
                strncpy(header.profileId, profile.id.c_str(), sizeof(header.profileId) - 1);
                header.profileId[sizeof(header.profileId) - 1] = '\0';
                strncpy(header.profileName, profile.label.c_str(), sizeof(header.profileName) - 1);
                header.profileName[sizeof(header.profileName) - 1] = '\0';
                header.phaseTransitionCount = 0; // Initialize phase transition count
                // Brew delay (ms) the shot ran with; round and clamp into the uint16_t field
                double delayMs = currentBrewDelay > 0.0 ? currentBrewDelay + 0.5 : 0.0;
                header.brewDelayMs = delayMs > 65535.0 ? 65535 : static_cast<uint16_t>(delayMs);
                logWriteFailed = false;
                // Write header placeholder
                if (currentFile.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) != sizeof(header)) {
                    ESP_LOGE("ShotHistoryPlugin", "Could not write the shot log header for %s", currentId.c_str());
                    logWriteFailed = true;
                }
            }
        }
        // Active-scale weight flow (vf): derive from the same non-negative weight we
        // store in sample.v so the two can never disagree, and skip the EMA update
        // on an implausible single-sample jump so one bad scale reading cannot
        // saturate vf for seconds. See GM-110.
        const float activeWeight = currentActiveWeight > 0.0f ? currentActiveWeight : 0.0f;
        const float activeDiff = activeWeight - lastActiveWeight;
        const bool plausibleActiveDelta = fabsf(activeDiff) <= MAX_PLAUSIBLE_WEIGHT_DELTA;
        if (plausibleActiveDelta) {
            const float activeFlow = activeDiff / (SHOT_LOG_SAMPLE_INTERVAL_MS / 1000.0f);
            currentActiveFlow = currentActiveFlow * 0.75f + activeFlow * 0.25f;
        }
        lastActiveWeight = activeWeight;

        ShotLogSample sample{};
        uint32_t tick = sampleCount <= 0xFFFF ? sampleCount : 0xFFFF;
        sample.t = static_cast<uint16_t>(tick);
        sample.tt = encodeUnsigned(controller->getTargetTemp(), TEMP_SCALE, TEMP_MAX_VALUE);
        sample.ct = encodeUnsigned(currentTemperature, TEMP_SCALE, TEMP_MAX_VALUE);
        sample.tp = encodeUnsigned(controller->getTargetPressure(), PRESSURE_SCALE, PRESSURE_MAX_VALUE);
        sample.cp = encodeUnsigned(controller->getCurrentPressure(), PRESSURE_SCALE, PRESSURE_MAX_VALUE);
        sample.fl = encodeSigned(controller->getCurrentPumpFlow(), FLOW_SCALE, FLOW_MIN_VALUE, FLOW_MAX_VALUE);
        sample.tf = encodeSigned(controller->getTargetFlow(), FLOW_SCALE, FLOW_MIN_VALUE, FLOW_MAX_VALUE);
        sample.pf = encodeSigned(controller->getCurrentPuckFlow(), FLOW_SCALE, FLOW_MIN_VALUE, FLOW_MAX_VALUE);
        sample.vf = encodeSigned(currentActiveFlow, FLOW_SCALE, FLOW_MIN_VALUE, FLOW_MAX_VALUE);
        sample.v = encodeUnsigned(activeWeight, WEIGHT_SCALE, WEIGHT_MAX_VALUE);
        sample.ev = encodeUnsigned(currentEstimatedWeight, WEIGHT_SCALE, WEIGHT_MAX_VALUE);
        sample.pr = encodeUnsigned(currentPuckResistance, RESISTANCE_SCALE, RESISTANCE_MAX_VALUE);
        sample.si = getSystemInfo(); // Pack system state information
        // Apply the same spike rejection to the final-weight maximum. A real
        // sustained step is accepted on the following stable sample, whereas a
        // one-sample excursion cannot permanently inflate the shot total.
        if (plausibleActiveDelta && activeWeight > maxRecordedWeight) {
            maxRecordedWeight = activeWeight;
        }

        // Track phase transitions
        if (controller->getMode() == MODE_BREW) {
            // Deref under the process lock — other tasks delete the process at any time (GM-147).
            std::lock_guard<std::recursive_mutex> guard(controller->getProcessLock());
            Process *process = controller->getProcess();
            if (process != nullptr && process->getType() == MODE_BREW) {
                auto *brewProcess = static_cast<BrewProcess *>(process);
                uint8_t currentPhase = static_cast<uint8_t>(brewProcess->phaseIndex);

                // Check for phase transition
                if (currentPhase != lastRecordedPhase) {
                    recordPhaseTransition(currentPhase, sampleCount, static_cast<uint8_t>(brewProcess->lastExitReason));
                    lastRecordedPhase = currentPhase;
                }
            }
        }

        if (isFileOpen) {
            if (ioBufferPos + sizeof(sample) > sizeof(ioBuffer)) {
                flushBuffer();
            }
            memcpy(ioBuffer + ioBufferPos, &sample, sizeof(sample));
            ioBufferPos += sizeof(sample);
            sampleCount++;

            // Track running aggregates for the rolling recent-shots buffer.
            tempSumScaled += sample.ct;
            tempSampleCount++;
            if (sample.cp > maxPressureScaled) {
                maxPressureScaled = sample.cp;
            }
            if (sample.fl > 0) {
                flowSumScaled += sample.fl;
                positiveFlowCount++;
            }
        }

        // Check for early index insertion (once per shot after 7.5s)
        if (!indexEntryCreated && (millis() - shotStart) > 7500) {
            indexEntryCreated = createEarlyIndexEntry();
        }

        // Check for weight stabilization during extended recording
        if (extendedRecording) {
            const unsigned long now = millis();

            bool canProcessWeight = (controller != nullptr);
            if (canProcessWeight) {
                canProcessWeight = controller->isVolumetricAvailable();
            }

            if (!canProcessWeight) {
                // If BLE connection is unstable, end extended recording early
                extendedRecording = false;
                return;
            }

            const float weightDiff = abs(currentActiveWeight - lastStableWeight);

            if (weightDiff < WEIGHT_STABILIZATION_THRESHOLD) {
                if (lastWeightChangeTime == 0) {
                    lastWeightChangeTime = now;
                }
                // Weight has been stable for the threshold time, stop extended recording
                if (now - lastWeightChangeTime >= WEIGHT_STABILIZATION_TIME) {
                    extendedRecording = false;
                }
            } else {
                // Weight changed, reset stabilization timer
                lastWeightChangeTime = 0;
                lastStableWeight = currentActiveWeight;
            }

            // Also stop extended recording after maximum duration
            if (now - extendedRecordingStart >= EXTENDED_RECORDING_DURATION) {
                extendedRecording = false;
            }
        }
    }
    if (!recording && !extendedRecording && isFileOpen) {
        flushBuffer();
        // Patch header with sampleCount and duration
        header.sampleCount = sampleCount;
        header.durationMs = millis() - shotStart;
        header.finalExitReason = finalExitReason; // why the shot ended (last phase exit or manual abort)
        float finalWeight = maxRecordedWeight;
        header.finalWeight = finalWeight > 0.0f ? encodeUnsigned(finalWeight, WEIGHT_SCALE, WEIGHT_MAX_VALUE) : 0;
        if (!currentFile.seek(0, SeekSet) ||
            currentFile.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) != sizeof(header)) {
            ESP_LOGE("ShotHistoryPlugin", "Could not finalize the shot log header for %s", currentId.c_str());
            logWriteFailed = true;
        }
        currentFile.close();
        isFileOpen = false;
        unsigned long duration = header.durationMs;
        // A log that did not fully land is discarded rather than published. The
        // header carries sampleCount from the in-memory counter, not from what
        // reached flash, so a truncated file would be indexed as a complete shot
        // and every reader would walk off the end of it.
        if (logWriteFailed) {
            ESP_LOGE("ShotHistoryPlugin", "Discarding shot log %s: it was not written completely", currentId.c_str());
            fs->remove("/h/" + currentId + ".slog");
            if (indexEntryCreated) {
                markIndexDeleted(currentId.toInt());
            }
        } else if (duration <= 7500) { // Exclude failed shots and flushes
            fs->remove("/h/" + currentId + ".slog");

            // If we created an early index entry, mark it as deleted
            if (indexEntryCreated) {
                markIndexDeleted(currentId.toInt());
            }
        } else {
            controller->getSettings().setHistoryIndex(controller->getSettings().getHistoryIndex() + 1);
            cleanupHistory();

            // Always create a complete index entry via upsert.
            // If an early entry exists, it gets overwritten with final data.
            // If no early entry exists, a new one is appended.
            ShotIndexEntry indexEntry{};
            indexEntry.id = currentId.toInt();
            indexEntry.timestamp = header.startEpoch;
            indexEntry.duration = header.durationMs;
            indexEntry.volume = header.finalWeight;
            indexEntry.rating = 0;
            indexEntry.flags = SHOT_FLAG_COMPLETED;
            strncpy(indexEntry.profileId, header.profileId, sizeof(indexEntry.profileId) - 1);
            indexEntry.profileId[sizeof(indexEntry.profileId) - 1] = '\0';
            strncpy(indexEntry.profileName, header.profileName, sizeof(indexEntry.profileName) - 1);
            indexEntry.profileName[sizeof(indexEntry.profileName) - 1] = '\0';
            indexEntry.avgTemp = tempSampleCount ? static_cast<uint16_t>(tempSumScaled / tempSampleCount) : 0;
            indexEntry.maxPressure = maxPressureScaled;
            indexEntry.avgFlow = positiveFlowCount ? static_cast<uint16_t>(flowSumScaled / positiveFlowCount) : 0;

            if (!appendToIndex(indexEntry)) {
                ESP_LOGE("ShotHistoryPlugin", "CRITICAL: Failed to add completed shot %u to index", indexEntry.id);
            }

            // Notify clients the shot is actually persisted. The brew process's
            // isActive/isFinished state (used elsewhere for UI) can go inactive
            // well before extended recording (BLE scale weight settling, see
            // endRecording()) finishes writing this entry, so the dashboard
            // listens for this event instead of inferring timing from that state.
            if (pluginManager) {
                Event savedEvent;
                savedEvent.id = "evt:history-shot-saved";
                savedEvent.setInt("id", indexEntry.id);
                pluginManager->trigger(savedEvent);
            }
        }
        // The shot's last index write is done. The index stays held: the
        // first version released it here and reopened it at the next
        // prepare, and that reopen is three directory walks of /h (the
        // Arduino File's two stats and FatFs's own lookup, about 2 s each on
        // a 3,000 shot card) that landed on the finished screen 20 s after
        // every shot, next to the five the log pre-open costs (gm-5x0v,
        // measured 2026-10-04: the animation at 0.6 to 1.6 fps for two
        // windows of about 2 s). rebuildIndex and ensureIndexExists close
        // the handle before they remove the file.
        armPrepare(PREPARE_DELAY_AFTER_SHOT_MS);
    }
    if (!recording && !extendedRecording && !isFileOpen) {
        preparePending();
    }
}

void ShotHistoryPlugin::armPrepare(unsigned long delayMs) {
    if (!holdHandles) {
        return;
    }
    prepareDueAt = millis() + delayMs;
    prepareArmed = true;
}

void ShotHistoryPlugin::preparePending() {
    if (!prepareArmed || static_cast<long>(millis() - prepareDueAt) < 0) {
        return;
    }
    prepareArmed = false;
    prepareNextShot();
}

void ShotHistoryPlugin::prepareNextShot() {
    // A no-op once done; the simulator reaches this before any other caller.
    syncNextIdWithIndex();
    const String nextId = padId(String(controller->getSettings().getHistoryIndex()));
    if (!(preparedLog && preparedId == nextId)) {
        dropPreparedLog(true);
        if (!fs->exists("/h")) {
            fs->mkdir("/h");
        }
        // FILE_WRITE truncates. A file under the next id that already holds
        // data is not ours to empty at idle (the counter can trail the card
        // after an NVS reset with no index); the shot's own open still takes
        // it, as it always has.
        const long existing = historyFileSize(*fs, nextId + ".slog");
        if (existing > 0) {
            ESP_LOGW("ShotHistoryPlugin", "Not pre-opening shot log %s: a %ld byte file is already there", nextId.c_str(),
                     existing);
        } else {
            preparedLog = fs->open("/h/" + nextId + ".slog", FILE_WRITE);
            if (preparedLog) {
                preparedId = nextId;
                ESP_LOGI("ShotHistoryPlugin", "Pre-opened shot log %s for the next shot", nextId.c_str());
            } else {
                ESP_LOGW("ShotHistoryPlugin", "Could not pre-open shot log %s; the shot will open it", nextId.c_str());
            }
        }
    }
    std::lock_guard<std::recursive_mutex> guard(indexLock);
    if (ensureIndexExists()) {
        holdIndex();
    }
}

void ShotHistoryPlugin::dropPreparedLog(bool removeFile) {
    if (preparedLog) {
        preparedLog.close();
        preparedLog = File();
        // It was created empty by prepareNextShot and nothing wrote to it.
        if (removeFile && preparedId.length() > 0) {
            fs->remove("/h/" + preparedId + ".slog");
        }
    }
    preparedId = "";
}

void ShotHistoryPlugin::holdIndex() {
    std::lock_guard<std::recursive_mutex> guard(indexLock);
    wantIndexHeld = true;
    if (heldIndex) {
        return;
    }
    heldIndex = fs->open("/h/index.bin", "r+");
    if (!heldIndex) {
        ESP_LOGW("ShotHistoryPlugin", "Could not hold index.bin open; index writes open it per use");
    }
}

void ShotHistoryPlugin::releaseHeldIndex() {
    std::lock_guard<std::recursive_mutex> guard(indexLock);
    wantIndexHeld = false;
    if (heldIndex) {
        heldIndex.close();
        heldIndex = File();
    }
}

ShotHistoryPlugin::IndexAccess::IndexAccess(ShotHistoryPlugin &plugin, const char *mode) : p(plugin) {
    if (p.heldIndex) {
        f = p.heldIndex;
        held = true;
        f.seek(0, SeekSet);
    } else {
        f = p.fs->open("/h/index.bin", mode);
    }
}

ShotHistoryPlugin::IndexAccess::~IndexAccess() {
    if (held) {
        // fflush plus fsync: the directory entry's size is written, so an
        // fopen on another task sees what was appended. Nothing to do and no
        // I/O when the access only read.
        f.flush();
    } else if (f) {
        f.close();
    }
}

void ShotHistoryPlugin::startRecording() {
    {
        // Deref under the process lock — other tasks delete the process at any time (GM-147).
        std::lock_guard<std::recursive_mutex> guard(controller->getProcessLock());
        Process *process = controller->getProcess();
        if (process != nullptr && process->getType() == MODE_BREW) {
            BrewProcess *brewProcess = static_cast<BrewProcess *>(process);
            if (brewProcess->isUtility()) {
                return;
            }
            // Capture initial volumetric mode state (brew by weight vs brew by time)
            shotStartedVolumetric = brewProcess->target == ProcessTarget::VOLUMETRIC;
            // Capture the brew delay the shot runs with (fixed at process construction)
            currentBrewDelay = brewProcess->brewDelay;
        }
    }
    // A no-op once the loop task has done it; a brew started in the first
    // seconds after boot waits for it here rather than taking a stale id.
    syncNextIdWithIndex();
    currentId = padId(String(controller->getSettings().getHistoryIndex()));
    shotStart = millis();
    lastWeightChangeTime = 0;
    extendedRecordingStart = 0;
    currentActiveWeight = 0.0f;
    lastStableWeight = 0.0f;
    currentEstimatedWeight = 0.0f;
    currentActiveFlow = 0.0f;
    lastActiveWeight = 0.0f;
    maxRecordedWeight = 0.0f;
    currentProfileName = controller->getProfileManager()->getSelectedProfile().label;
    recording = true;
    extendedRecording = false;
    indexEntryCreated = false; // Reset flag for new shot
    sampleCount = 0;
    ioBufferPos = 0;
    tempSumScaled = 0;
    tempSampleCount = 0;
    maxPressureScaled = 0;
    flowSumScaled = 0;
    positiveFlowCount = 0;

    // Reset phase tracking for new shot
    lastRecordedPhase = 0xFF;                                      // Invalid value to detect first phase
    finalExitReason = static_cast<uint8_t>(PhaseExitReason::NONE); // Reset shot-end reason
}

unsigned long ShotHistoryPlugin::getTime() {
    time_t now;
    time(&now);
    return now;
}

void ShotHistoryPlugin::endRecording() {
    // Capture how the shot ended: if the process ran to completion, reuse the last phase's exit reason;
    // if it was still running when stopped, the user aborted it. getLastProcess() is the just-ended brew
    // process here (deactivate() moves currentProcess -> lastProcess before firing controller:brew:end).
    if (controller != nullptr) {
        // Deref under the process lock — other tasks delete the process at any time (GM-147).
        std::lock_guard<std::recursive_mutex> guard(controller->getProcessLock());
        Process *last = controller->getLastProcess();
        if (last != nullptr && last->getType() == MODE_BREW) {
            auto *brewProcess = static_cast<BrewProcess *>(last);
            PhaseExitReason reason =
                brewProcess->processPhase == ProcessPhase::FINISHED ? brewProcess->lastExitReason : PhaseExitReason::ABORTED;
            finalExitReason = static_cast<uint8_t>(reason);
        }
    }

    if (recording && controller && controller->isVolumetricAvailable() && currentActiveWeight > 0) {
        // Start extended recording for any shot with active weight data
        extendedRecording = true;
        extendedRecordingStart = millis();
        lastStableWeight = currentActiveWeight;
        lastWeightChangeTime = 0;
    }

    // Notify clients immediately, without waiting for the history file write
    // (which can lag behind by the extended-recording window above). Pressure
    // and flow are already final at this point: the pump is off, so any
    // further samples recorded during extended recording have cp/fl at or
    // near zero and cannot change the running max/average.
    if (pluginManager) {
        Event statsEvent;
        statsEvent.id = "evt:shot-finished-stats";
        statsEvent.setFloat("maxPressure", maxPressureScaled > 0 ? maxPressureScaled / PRESSURE_SCALE : 0.0f);
        statsEvent.setFloat("avgFlow",
                            positiveFlowCount > 0 ? (flowSumScaled / static_cast<float>(positiveFlowCount)) / FLOW_SCALE : 0.0f);
        pluginManager->trigger(statsEvent);
    }

    recording = false;
}

void ShotHistoryPlugin::endExtendedRecording() {
    if (extendedRecording) {
        extendedRecording = false;
    }
}

void ShotHistoryPlugin::recordPhaseTransition(uint8_t phaseNumber, uint16_t sampleIndex, uint8_t reason) {
    // Only record if we have space and a valid header
    if (header.phaseTransitionCount >= 12 || !isFileOpen) {
        return;
    }

    // Get current profile to extract phase name
    Profile profile = controller->getProfileManager()->getSelectedProfile();
    PhaseTransition &transition = header.phaseTransitions[header.phaseTransitionCount];

    transition.sampleIndex = sampleIndex;
    transition.phaseNumber = phaseNumber;
    transition.transitionReason = reason; // PhaseExitReason for why the previous phase ended

    // Get phase name from profile
    if (phaseNumber < profile.phases.size()) {
        strncpy(transition.phaseName, profile.phases[phaseNumber].name.c_str(), sizeof(transition.phaseName) - 1);
        transition.phaseName[sizeof(transition.phaseName) - 1] = '\0';
    } else {
        // Fallback to generic name
        snprintf(transition.phaseName, sizeof(transition.phaseName), "Phase %d", phaseNumber + 1);
    }

    header.phaseTransitionCount++;

    ESP_LOGD("ShotHistoryPlugin", "Recorded phase transition to phase %d (%s) at sample %d", phaseNumber, transition.phaseName,
             sampleIndex);
}

uint16_t ShotHistoryPlugin::getSystemInfo() {
    uint16_t systemInfo = 0;

    // Bit 0: Shot started in volumetric mode
    if (shotStartedVolumetric) {
        systemInfo |= SYSTEM_INFO_SHOT_STARTED_VOLUMETRIC;
    }

    // Bit 1: Currently in volumetric mode (check current process if active)
    if (controller != nullptr) {
        // Deref under the process lock — other tasks delete the process at any time (GM-147).
        std::lock_guard<std::recursive_mutex> guard(controller->getProcessLock());
        Process *process = controller->getProcess();
        if (process != nullptr && process->getType() == MODE_BREW) {
            auto *brewProcess = static_cast<BrewProcess *>(process);
            bool currentlyVolumetric = brewProcess->target == ProcessTarget::VOLUMETRIC &&
                                       brewProcess->currentPhase.hasVolumetricTarget() && controller->isVolumetricAvailable();
            if (currentlyVolumetric) {
                systemInfo |= SYSTEM_INFO_CURRENTLY_VOLUMETRIC;
            }
        }
    }

    // Bit 2: Bluetooth scale connected
    if (controller != nullptr && controller->isBluetoothScaleHealthy()) {
        systemInfo |= SYSTEM_INFO_BLUETOOTH_SCALE_CONNECTED;
    }

    // Bit 3: Volumetric available
    if (controller != nullptr && controller->isVolumetricAvailable()) {
        systemInfo |= SYSTEM_INFO_VOLUMETRIC_AVAILABLE;
    }

    // Bit 4: Extended recording active
    if (extendedRecording) {
        systemInfo |= SYSTEM_INFO_EXTENDED_RECORDING;
    }

    // Bits 5-7: Brew process/target state used by v6 analysis.
    if (controller != nullptr) {
        std::lock_guard<std::recursive_mutex> guard(controller->getProcessLock());
        Process *process = controller->getProcess();
        if (process != nullptr && process->getType() == MODE_BREW) {
            systemInfo |= SYSTEM_INFO_PROCESS_IS_BREW;
            auto *brewProcess = static_cast<BrewProcess *>(process);
            if (brewProcess->target == ProcessTarget::VOLUMETRIC) {
                systemInfo |= SYSTEM_INFO_TARGET_IS_VOLUMETRIC;
            }
            if (brewProcess->currentPhase.hasVolumetricTarget()) {
                systemInfo |= SYSTEM_INFO_PHASE_HAS_VOLUMETRIC;
            }
        }
    }

    // Bit 8: Effective scale source connected/healthy (hardware or Bluetooth).
    if (controller != nullptr) {
        const VolumetricMeasurementSource activeSource = controller->getEffectiveScaleSource();
        if ((activeSource == VolumetricMeasurementSource::HARDWARE || activeSource == VolumetricMeasurementSource::BLUETOOTH) &&
            controller->isScaleSourceHealthy(activeSource)) {
            systemInfo |= SYSTEM_INFO_ACTIVE_SCALE_CONNECTED;
        }
    }

    return systemInfo;
}

void ShotHistoryPlugin::cleanupHistory() {
    size_t freeSpace = getFreeSpace();
    if (freeSpace > MIN_FREE_SPACE_BYTES) {
        return; // Enough space, nothing to do
    }

    // One directory walk for the names (listHistoryNames: readdir, never
    // openNextFile). Sizes are looked up only for the shots actually removed,
    // and the running estimate below replaces a getFreeSpace() per file:
    // usedBytes() walks every allocated block on LittleFS. A notes file is
    // removed only when the walk saw it, because removing a name that is not
    // there is a full walk of /h for nothing on FAT.
    std::vector<String> names;
    listHistoryNames(*fs, names);
    std::vector<String> slogFiles;
    std::vector<String> jsonFiles;
    for (const String &name : names) {
        if (name.endsWith(".slog")) {
            slogFiles.push_back(name);
        } else if (name.endsWith(".json")) {
            jsonFiles.push_back(name);
        }
    }
    names.clear();

    if (slogFiles.empty()) {
        return;
    }

    std::sort(slogFiles.begin(), slogFiles.end());
    std::sort(jsonFiles.begin(), jsonFiles.end());

    // Remove oldest files until the running estimate clears the threshold.
    // freeSpace is exact for the first comparison and an estimate thereafter
    // (it does not account for LittleFS block/wear-leveling overhead), so this
    // can stop a shot or two early or late versus a live re-query; it cannot
    // misreport or corrupt a shot. A size that cannot be read falls back to a
    // live re-query so the loop cannot run away.
    size_t removed = 0;
    for (size_t i = 0; i < slogFiles.size() && freeSpace <= MIN_FREE_SPACE_BYTES; i++) {
        const String &name = slogFiles[i];
        const String base = name.substring(0, name.lastIndexOf('.'));
        if (base.length() > 0) {
            markIndexDeleted(base.toInt());
        }
        const long size = historyFileSize(*fs, name);

        // Remove .slog and associated .json notes file
        fs->remove("/h/" + name);
        const String notesName = base + ".json";
        if (std::binary_search(jsonFiles.begin(), jsonFiles.end(), notesName)) {
            fs->remove("/h/" + notesName);
        }
        if (size >= 0) {
            freeSpace += static_cast<size_t>(size);
        } else {
            freeSpace = getFreeSpace();
        }
        removed++;
    }

    if (removed > 0) {
        ESP_LOGI("ShotHistoryPlugin", "Cleaned up %u old shots (estimated free space: %u bytes)", removed, freeSpace);
    }
}

size_t ShotHistoryPlugin::getFreeSpace() {
    if (controller->isSDCard()) {
        uint64_t total = SD_MMC.totalBytes();
        uint64_t used = SD_MMC.usedBytes();
        uint64_t free = total > used ? (total - used) : 0;
        // Cap to size_t max for consistency
        return free > SIZE_MAX ? SIZE_MAX : static_cast<size_t>(free);
    }
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    return total > used ? (total - used) : 0;
}

void ShotHistoryPlugin::handleRequest(JsonDocument &request, JsonDocument &response) {
    String type = request["tp"].as<String>();
    response["tp"] = String("res:") + type.substring(4);
    response["rid"] = request["rid"].as<String>();

    auto id = request["id"].as<String>();
    if ((type == "req:history:delete" || type == "req:history:notes:get" || type == "req:history:notes:save") &&
        !validShotId(id)) {
        response["error"] = F("Invalid shot id");
        return;
    }

    if (type == "req:history:delete") {
        String paddedId = padId(id);
        fs->remove("/h/" + paddedId + ".slog");
        fs->remove("/h/" + paddedId + ".json");

        // Mark as deleted in index
        markIndexDeleted(id.toInt());

        response["msg"] = "Ok";
    } else if (type == "req:history:notes:get") {
        JsonDocument notes(&psramAllocator);
        loadNotes(id, notes);
        response["notes"] = notes;
    } else if (type == "req:history:notes:save") {
        JsonDocument notes; // explicit document: variant->const JsonDocument& is ambiguous on clang
        notes.set(request["notes"]);
        if (!saveNotes(id, notes)) {
            // Same key the profile handlers use for a failed write. Reporting "Ok"
            // here told the user their notes were saved when the file was left
            // untouched. The index keeps its old rating and volume too, so it
            // never shows a rating the notes file does not hold.
            response["error"] = F("Notes save failed");
            return;
        }

        // The notes file is confirmed written: update rating and volume in the index.
        uint8_t rating = notes["rating"].as<uint8_t>();

        // Check if user provided a doseOut value to override volume
        uint16_t volume = 0;
        if (notes["doseOut"].is<String>() && !notes["doseOut"].as<String>().isEmpty()) {
            float doseOut = notes["doseOut"].as<String>().toFloat();
            if (doseOut > 0.0f) {
                volume = encodeUnsigned(doseOut, WEIGHT_SCALE, WEIGHT_MAX_VALUE);
            }
        }

        // Always use updateIndexMetadata - it handles both rating and optional volume
        updateIndexMetadata(id.toInt(), rating, volume);
        response["msg"] = "Ok";
    } else if (type == "req:history:rebuild") {
        // Rebuild is now handled asynchronously by WebUIPlugin
        // This path shouldn't be reached, but handle it just in case
        response["msg"] = "Use async rebuild";
    }
}

bool ShotHistoryPlugin::saveNotes(const String &id, const JsonDocument &notes) {
    const String target = "/h/" + id + ".json";
    // Same shape as ProfileManager::saveProfile: FILE_WRITE truncates the
    // existing notes before anything replaces them, so a short write used to
    // destroy the notes that were there and report nothing. Write beside it and
    // move it in only once the whole document is down.
    const String tmpPath = target + ".tmp";
    String notesStr;
    serializeJson(notes, notesStr);
#ifndef GAGGIMATE_SIM
    // Through the POSIX layer, so the close is checked: File::close() returns
    // nothing, and on FAT the close is where the directory entry and the last
    // cluster are written. A failed close used to count as a saved file.
    if (const char *mount = fs->mountpoint()) {
        const String full = String(mount) + tmpPath;
        FILE *fp = fopen(full.c_str(), "w");
        if (fp == nullptr) {
            ESP_LOGE("ShotHistoryPlugin", "Could not open notes file for shot %s: %d", id.c_str(), errno);
            return false;
        }
        const size_t written = fwrite(notesStr.c_str(), 1, notesStr.length(), fp);
        const bool flushed = fflush(fp) == 0 && fsync(fileno(fp)) == 0;
        const bool closed = fclose(fp) == 0;
        if (written != notesStr.length() || !flushed || !closed) {
            ESP_LOGE("ShotHistoryPlugin",
                     "Notes for shot %s not written (%u of %u bytes, flush %d, close %d); keeping the previous version",
                     id.c_str(), written, notesStr.length(), flushed, closed);
            fs->remove(tmpPath);
            return false;
        }
        // Old notes are kept as .json.bak until the new file is in place, and
        // rolled back if it is not (SafeReplace.h).
        return saferep::commitReplace(*fs, tmpPath, target, "ShotHistoryPlugin");
    }
#endif
    File file = fs->open(tmpPath, FILE_WRITE);
    if (!file) {
        ESP_LOGE("ShotHistoryPlugin", "Could not open notes file for shot %s", id.c_str());
        return false;
    }
    const size_t written = file.print(notesStr);
    file.close();
    if (written != notesStr.length()) {
        ESP_LOGE("ShotHistoryPlugin", "Wrote %u of %u bytes of notes for shot %s; keeping the previous version", written,
                 notesStr.length(), id.c_str());
        fs->remove(tmpPath);
        return false;
    }
    // Old notes are kept as .json.bak until the new file is in place, and
    // rolled back if it is not (SafeReplace.h).
    return saferep::commitReplace(*fs, tmpPath, target, "ShotHistoryPlugin");
}

void ShotHistoryPlugin::loadNotes(const String &id, JsonDocument &notes) {
    File file = fs->open("/h/" + id + ".json", "r");
    if (file) {
        String notesStr = file.readString();
        file.close();
        deserializeJson(notes, notesStr);
    }
}

void ShotHistoryPlugin::loopTask(void *arg) {
    auto *plugin = static_cast<ShotHistoryPlugin *>(arg);
    // Before any shot is recorded, and off the setup task: opening index.bin
    // on the SD card walks /h, which takes seconds on a large history.
    plugin->syncNextIdWithIndex();
    while (true) {
        plugin->record();
        // Use canonical interval from shot log format to avoid divergence.
        vTaskDelay(SHOT_LOG_SAMPLE_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

void ShotHistoryPlugin::flushBuffer() {
    if (isFileOpen && ioBufferPos > 0) {
        // A 4 KB LittleFS write disables the flash cache while it runs, and the
        // LCD bounce refill copies out of a PSRAM framebuffer, which is behind
        // that same cache. CONFIG_LCD_RGB_ISR_IRAM_SAFE keeps the refill ISR
        // alive through that window and it skips the copy instead of stalling
        // (gm_rgb_flash_skip_bufs counts those), so the panel holds sync and
        // briefly repeats stale scanlines rather than desyncing. A shot
        // recording is still the only thing on the display that writes flash
        // at a steady rate. Mark it, or the slip attribution log blames
        // whatever happened to run nearby: it carried exactly one FLASH marker
        // (a settings save) and therefore reported "flash: never" through a
        // whole investigation of slips on a live machine.
        panelclock::scanoutMark(panelclock::SCANOUT_ACT_FLASH);
        // Logged once per shot: this runs every 4 KB of samples, so a full
        // filesystem would otherwise repeat the same line for the rest of the
        // recording.
        if (currentFile.write(ioBuffer, ioBufferPos) != ioBufferPos && !logWriteFailed) {
            ESP_LOGE("ShotHistoryPlugin", "Short write flushing shot log %s; the recording will be discarded", currentId.c_str());
            logWriteFailed = true;
        }
        ioBufferPos = 0;
    }
}

// Index management methods
bool ShotHistoryPlugin::ensureIndexExists() {
    std::lock_guard<std::recursive_mutex> guard(indexLock);
    if (heldIndex) {
        // Held open: validate through the handle, no exists() walk.
        heldIndex.seek(0, SeekSet);
        ShotIndexHeader hdr{};
        if (heldIndex.read(reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr)) == sizeof(hdr) && hdr.magic == SHOT_INDEX_MAGIC) {
            return true;
        }
        ESP_LOGW("ShotHistoryPlugin", "Corrupt index file detected (bad magic), recreating");
        heldIndex.close();
        heldIndex = File();
        fs->remove("/h/index.bin");
    } else if (fs->exists("/h/index.bin")) {
        // Validate existing index header
        File indexFile = fs->open("/h/index.bin", "r");
        if (indexFile) {
            ShotIndexHeader hdr{};
            bool valid =
                (indexFile.read(reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr)) == sizeof(hdr) && hdr.magic == SHOT_INDEX_MAGIC);
            indexFile.close();
            if (valid) {
                return true;
            }
            ESP_LOGW("ShotHistoryPlugin", "Corrupt index file detected (bad magic), recreating");
            fs->remove("/h/index.bin");
        }
    }

    // Create new empty index
    File indexFile = fs->open("/h/index.bin", FILE_WRITE);
    if (!indexFile) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to create index file");
        return false;
    }

    ShotIndexHeader header{};
    header.magic = SHOT_INDEX_MAGIC;
    header.version = SHOT_INDEX_VERSION;
    header.entrySize = SHOT_INDEX_ENTRY_SIZE;
    header.entryCount = 0;
    header.nextId = controller->getSettings().getHistoryIndex();

    const bool headerWritten = writeIndexHeader(indexFile, header);
    indexFile.close();
    if (!headerWritten) {
        // A file with no valid magic fails validation on the next call and gets
        // recreated, so leaving it behind would make every append attempt run
        // through a create that reports success and an open that then fails to
        // read the header. Remove it and say so.
        fs->remove("/h/index.bin");
        return false;
    }

    ESP_LOGI("ShotHistoryPlugin", "Created new index file");
    // A held handle closed above for a corrupt file is reopened on the new one.
    if (wantIndexHeld) {
        holdIndex();
    }
    return true;
}

bool ShotHistoryPlugin::appendToIndex(const ShotIndexEntry &entry) {
    std::lock_guard<std::recursive_mutex> guard(indexLock);
    if (!ensureIndexExists()) {
        return false;
    }

    IndexAccess access(*this, "r+");
    File &indexFile = access.file();
    if (!indexFile) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to open index file for append");
        return false;
    }

    ShotIndexHeader header{};
    if (!readIndexHeader(indexFile, header)) {
        return false;
    }

    // Check for existing entry with same ID - update in place (upsert)
    int existingPos = findEntryPosition(indexFile, header, entry.id);
    if (existingPos >= 0) {
        if (writeEntryAtPosition(indexFile, existingPos, entry)) {
            ESP_LOGD("ShotHistoryPlugin", "Updated existing index entry for shot %u", entry.id);
            return true;
        }
        ESP_LOGE("ShotHistoryPlugin", "Failed to update existing index entry for shot %u", entry.id);
        return false;
    }

    // An id below the last entry's breaks the order the binary search needs.
    // Refusing it would lose the shot, so it is appended and the index is
    // marked for the linear fallback until the next rebuild sorts it.
    if (header.entryCount > 0) {
        ShotIndexEntry last{};
        const size_t lastPos = sizeof(ShotIndexHeader) + static_cast<size_t>(header.entryCount - 1) * sizeof(ShotIndexEntry);
        if (readEntryAtPosition(indexFile, lastPos, last) && entry.id < last.id) {
            ESP_LOGW("ShotHistoryPlugin", "Shot %u is out of id order (last entry is %u); lookups fall back to a linear scan",
                     entry.id, last.id);
            indexOrderBroken = true;
        }
    }

    // Append entry
    indexFile.seek(0, SeekEnd);
    size_t written = indexFile.write(reinterpret_cast<const uint8_t *>(&entry), sizeof(entry));
    if (written != sizeof(entry)) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to write index entry for shot %u", entry.id);
        return false;
    }

    // Update header. The entry bytes are already on disk at this point, so a
    // failure here leaves entryCount one short of what the file holds: the shot
    // just recorded is invisible to every reader, and the next append seeks to
    // SeekEnd and writes past the orphan, so the count stays permanently out of
    // step with the offsets. nextId does not advance either, so the following
    // shot reuses this one's id. Returning true here reported all of that as a
    // successful append.
    header.entryCount++;
    if (entry.id + 1 > header.nextId) {
        header.nextId = entry.id + 1;
    }
    if (!writeIndexHeader(indexFile, header)) {
        ESP_LOGE("ShotHistoryPlugin", "Wrote index entry for shot %u but could not update the header", entry.id);
        return false;
    }

    ESP_LOGD("ShotHistoryPlugin", "Appended shot %u to index", entry.id);
    return true;
}

void ShotHistoryPlugin::updateIndexMetadata(uint32_t shotId, uint8_t rating, uint16_t volume) {
    std::lock_guard<std::recursive_mutex> guard(indexLock);
    IndexAccess access(*this, "r+");
    File &indexFile = access.file();
    if (!indexFile) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to open index file for metadata update");
        return;
    }

    ShotIndexHeader header{};
    if (!readIndexHeader(indexFile, header)) {
        return;
    }

    int entryPos = findEntryPosition(indexFile, header, shotId);
    if (entryPos >= 0) {
        ShotIndexEntry entry{};
        if (readEntryAtPosition(indexFile, entryPos, entry)) {
            entry.rating = rating;
            if (volume > 0) {
                entry.volume = volume;
            }
            if (rating > 0) {
                entry.flags |= SHOT_FLAG_HAS_NOTES;
            }

            if (writeEntryAtPosition(indexFile, entryPos, entry)) {
                ESP_LOGD("ShotHistoryPlugin", "Updated metadata for shot %u: rating=%u, volume=%u", shotId, rating, volume);
            }
        }
    } else {
        ESP_LOGW("ShotHistoryPlugin", "Shot %u not found in index for metadata update", shotId);
    }
}

void ShotHistoryPlugin::markIndexDeleted(uint32_t shotId) {
    std::lock_guard<std::recursive_mutex> guard(indexLock);
    IndexAccess access(*this, "r+");
    File &indexFile = access.file();
    if (!indexFile) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to open index file for deletion marking");
        return;
    }

    ShotIndexHeader header{};
    if (!readIndexHeader(indexFile, header)) {
        return;
    }

    // Find ALL entries with this shot ID and mark them as deleted
    uint32_t duplicatesFound = 0;

    for (uint32_t i = 0; i < header.entryCount; i++) {
        size_t entryPos = sizeof(ShotIndexHeader) + i * sizeof(ShotIndexEntry);
        ShotIndexEntry entry{};
        if (readEntryAtPosition(indexFile, entryPos, entry)) {
            if (entry.id == shotId) {
                duplicatesFound++;

                // Mark this entry as deleted
                entry.flags |= SHOT_FLAG_DELETED;

                if (writeEntryAtPosition(indexFile, entryPos, entry)) {
                    ESP_LOGD("ShotHistoryPlugin", "Marked shot %u as deleted in index (duplicate #%u)", shotId, duplicatesFound);
                }
            }
        }
    }

    if (duplicatesFound == 0) {
        ESP_LOGW("ShotHistoryPlugin", "Shot %u not found in index for deletion marking", shotId);
    } else if (duplicatesFound > 1) {
        ESP_LOGW("ShotHistoryPlugin", "Found and marked %u duplicate entries for shot %u as deleted", duplicatesFound, shotId);
    }
}

size_t ShotHistoryPlugin::readRecentEntries(ShotIndexEntry *outEntries, size_t maxCount) {
    std::lock_guard<std::recursive_mutex> guard(indexLock);
    IndexAccess access(*this, "r");
    File &indexFile = access.file();
    if (!indexFile) {
        return 0;
    }

    ShotIndexHeader header{};
    if (!readIndexHeader(indexFile, header)) {
        return 0;
    }

    // Entries are appended in id order, so walking backwards yields newest first.
    size_t found = 0;
    for (uint32_t i = header.entryCount; i > 0 && found < maxCount; i--) {
        size_t entryPos = sizeof(ShotIndexHeader) + (i - 1) * sizeof(ShotIndexEntry);
        ShotIndexEntry entry{};
        if (!readEntryAtPosition(indexFile, entryPos, entry)) {
            break;
        }
        if (entry.flags & SHOT_FLAG_DELETED) {
            continue;
        }
        outEntries[found++] = entry;
    }

    return found;
}

bool ShotHistoryPlugin::snapshotIndex(uint8_t **outBuf, size_t *outLen) {
    *outBuf = nullptr;
    *outLen = 0;
    std::lock_guard<std::recursive_mutex> guard(indexLock);
    IndexAccess access(*this, "r");
    File &indexFile = access.file();
    if (!indexFile) {
        return false;
    }
    ShotIndexHeader header{};
    if (!readIndexHeader(indexFile, header)) {
        return false;
    }
    const size_t wanted = sizeof(ShotIndexHeader) + static_cast<size_t>(header.entryCount) * sizeof(ShotIndexEntry);
    auto *buf = static_cast<uint8_t *>(ps_malloc(wanted));
    if (buf == nullptr) {
        ESP_LOGE("ShotHistoryPlugin", "No memory for a %u byte index snapshot", static_cast<unsigned>(wanted));
        return false;
    }
    const size_t got = indexFile.read(buf + sizeof(ShotIndexHeader), wanted - sizeof(ShotIndexHeader));
    // A file shorter than its header claims (an append whose header write
    // landed but whose entry did not) is served as the entries it holds.
    const uint32_t entries = static_cast<uint32_t>(got / sizeof(ShotIndexEntry));
    if (entries != header.entryCount) {
        ESP_LOGW("ShotHistoryPlugin", "index.bin header says %u entries, file holds %u", header.entryCount, entries);
        header.entryCount = entries;
    }
    memcpy(buf, &header, sizeof(header));
    *outBuf = buf;
    *outLen = sizeof(ShotIndexHeader) + static_cast<size_t>(entries) * sizeof(ShotIndexEntry);
    return true;
}

void ShotHistoryPlugin::syncNextIdWithIndex() {
    std::lock_guard<std::recursive_mutex> guard(indexLock);
    if (nextIdSynced) {
        return;
    }
    nextIdSynced = true;
    IndexAccess access(*this, "r");
    File &indexFile = access.file();
    if (!indexFile) {
        return;
    }
    ShotIndexHeader header{};
    if (!readIndexHeader(indexFile, header)) {
        return;
    }
    // One sequential read of the whole index, eight entries at a time (1 KB
    // of this task's stack), for the highest id and whether the ids ascend.
    ShotIndexEntry batch[8];
    uint32_t maxId = 0;
    uint32_t prevId = 0;
    bool any = false;
    bool sorted = true;
    uint32_t read = 0;
    while (read < header.entryCount) {
        const uint32_t want = std::min<uint32_t>(8, header.entryCount - read);
        const size_t got = indexFile.read(reinterpret_cast<uint8_t *>(batch), want * sizeof(ShotIndexEntry));
        const uint32_t n = static_cast<uint32_t>(got / sizeof(ShotIndexEntry));
        for (uint32_t k = 0; k < n; k++) {
            const uint32_t id = batch[k].id;
            if (any && id < prevId) {
                sorted = false;
            }
            if (!any || id > maxId) {
                maxId = id;
            }
            prevId = id;
            any = true;
        }
        read += n;
        if (n < want) {
            break;
        }
    }

    if (!sorted) {
        ESP_LOGW("ShotHistoryPlugin", "index.bin is not in id order; lookups fall back to a linear scan until a rebuild");
        indexOrderBroken = true;
    }
    uint32_t next = header.nextId;
    if (any && maxId + 1 > next) {
        next = maxId + 1;
    }
    const int current = controller->getSettings().getHistoryIndex();
    if (current < 0 || static_cast<uint32_t>(current) < next) {
        ESP_LOGW("ShotHistoryPlugin", "Shot counter %d is at or below ids already in the index; raising it to %u", current, next);
        controller->getSettings().setHistoryIndex(static_cast<int>(next));
    }
}

void ShotHistoryPlugin::startAsyncRebuild() {
    if (!rebuildInProgress) {
        rebuildInProgress = true; // Set immediately to prevent multiple rebuilds
        ESP_LOGI("ShotHistoryPlugin", "Starting immediate async rebuild task");

        // Create a dedicated task for rebuild instead of using the existing loop
        xTaskCreatePinnedToCore(
            [](void *param) {
                auto *plugin = static_cast<ShotHistoryPlugin *>(param);
                ESP_LOGI("ShotHistoryPlugin", "Rebuild task started");
                plugin->rebuildIndex();
                plugin->rebuildInProgress = false;
                ESP_LOGI("ShotHistoryPlugin", "Rebuild task completed");
                vTaskDelete(NULL); // Delete this task when done
            },
            "ShotHistoryRebuild",
            configMINIMAL_STACK_SIZE * 8, // Larger stack for file operations
            this,
            2, // Higher priority than normal
            NULL, 0);
    } else {
        ESP_LOGW("ShotHistoryPlugin", "Rebuild already in progress, ignoring request");
    }
}

void ShotHistoryPlugin::rebuildIndex() {
    ESP_LOGI("ShotHistoryPlugin", "Starting index rebuild...");

    // Send scanning event
    if (pluginManager) {
        Event startEvent;
        startEvent.id = "evt:history-rebuild-progress";
        startEvent.setInt("total", 0);
        startEvent.setInt("current", 0);
        startEvent.setString("status", "scanning");
        pluginManager->trigger(startEvent);
    }

    // Delete existing index and create a new empty one. The entries below
    // are replayed in id order, so the fallback flag no longer applies.
    bool created;
    {
        std::lock_guard<std::recursive_mutex> guard(indexLock);
        // A handle held across a shot must not outlive the file it points at.
        // ensureIndexExists reopens it on the new file if a prepare wants it.
        if (heldIndex) {
            heldIndex.close();
            heldIndex = File();
        }
        fs->remove("/h/index.bin");
        created = ensureIndexExists();
        indexOrderBroken = false;
    }
    if (!created) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to create index during rebuild");
        // Emit error event
        if (pluginManager) {
            Event errorEvent;
            errorEvent.id = "evt:history-rebuild-progress";
            errorEvent.setInt("total", 0);
            errorEvent.setInt("current", 0);
            errorEvent.setString("status", "error");
            pluginManager->trigger(errorEvent);
        }
        return;
    }

    // Collect all .slog names with readdir (listHistoryNames); only the files
    // whose headers are read below are opened.
    std::vector<String> names;
    listHistoryNames(*fs, names);
    // The notes names come from the same walk: an exists() per shot was one
    // more full walk of /h for every shot without notes.
    std::vector<String> slogFiles;
    std::vector<String> jsonFiles;
    for (const String &name : names) {
        if (name.endsWith(".slog")) {
            slogFiles.push_back(name);
        } else if (name.endsWith(".json")) {
            jsonFiles.push_back(name);
        }
    }
    names.clear();

    // Sort files to maintain order
    std::sort(slogFiles.begin(), slogFiles.end());
    std::sort(jsonFiles.begin(), jsonFiles.end());

    ESP_LOGI("ShotHistoryPlugin", "Rebuilding index from %d shot files", slogFiles.size());

    // Emit start event with total file count
    if (pluginManager) {
        Event startEvent;
        startEvent.id = "evt:history-rebuild-progress";
        startEvent.setInt("total", (int)slogFiles.size());
        startEvent.setInt("current", 0);
        startEvent.setString("status", "started");
        pluginManager->trigger(startEvent);
    }

    int currentIndex = 0;
    // historyIndex is the id the next shot takes, so it must end above the
    // highest id on the card, not equal to it.
    uint32_t nextId = controller->getSettings().getHistoryIndex();
    for (const String &fileName : slogFiles) {
        currentIndex++;
        File shotFile = fs->open("/h/" + fileName, "r");
        if (!shotFile) {
            continue;
        }

        // Read shot header
        ShotLogHeader shotHeader{};
        if (shotFile.read(reinterpret_cast<uint8_t *>(&shotHeader), sizeof(shotHeader)) != sizeof(shotHeader) ||
            shotHeader.magic != SHOT_LOG_MAGIC) {
            shotFile.close();
            continue;
        }

        // Extract shot ID from filename
        int start = fileName.lastIndexOf('/') + 1;
        int end = fileName.lastIndexOf('.');
        uint32_t shotId = fileName.substring(start, end).toInt();
        if (shotId + 1 > nextId) {
            nextId = shotId + 1;
        }

        // Create index entry
        ShotIndexEntry entry{};
        entry.id = shotId;
        entry.timestamp = shotHeader.startEpoch;
        entry.duration = shotHeader.durationMs;
        entry.volume = shotHeader.finalWeight;
        entry.rating = 0; // Will be updated if notes exist
        entry.flags = SHOT_FLAG_COMPLETED;
        strncpy(entry.profileId, shotHeader.profileId, sizeof(entry.profileId) - 1);
        entry.profileId[sizeof(entry.profileId) - 1] = '\0';
        strncpy(entry.profileName, shotHeader.profileName, sizeof(entry.profileName) - 1);
        entry.profileName[sizeof(entry.profileName) - 1] = '\0';

        // Check for incomplete shots
        if (shotHeader.sampleCount == 0) {
            entry.flags &= ~SHOT_FLAG_COMPLETED;
        }

        // Recompute the per-shot aggregates from the sample records (same math
        // as the running sums in record()).
        {
            uint32_t tempSum = 0, tempCount = 0, flowSum = 0, flowCount = 0;
            uint16_t maxPressure = 0;
            ShotLogSample sample{};
            shotFile.seek(shotHeader.headerSize, SeekSet);
            for (uint32_t s = 0; s < shotHeader.sampleCount; s++) {
                if (shotFile.read(reinterpret_cast<uint8_t *>(&sample), sizeof(sample)) != sizeof(sample)) {
                    break;
                }
                tempSum += sample.ct;
                tempCount++;
                if (sample.cp > maxPressure) {
                    maxPressure = sample.cp;
                }
                if (sample.fl > 0) {
                    flowSum += sample.fl;
                    flowCount++;
                }
            }
            entry.avgTemp = tempCount ? static_cast<uint16_t>(tempSum / tempCount) : 0;
            entry.maxPressure = maxPressure;
            entry.avgFlow = flowCount ? static_cast<uint16_t>(flowSum / flowCount) : 0;
        }

        // Check for notes and extract rating and volume override
        const String notesName = String(shotId, 10) + ".json";
        const String notesPath = "/h/" + notesName;
        if (std::binary_search(jsonFiles.begin(), jsonFiles.end(), notesName)) {
            entry.flags |= SHOT_FLAG_HAS_NOTES;

            File notesFile = fs->open(notesPath, "r");
            if (notesFile) {
                String notesStr = notesFile.readString();
                notesFile.close();

                JsonDocument notesDoc(&psramAllocator);
                if (deserializeJson(notesDoc, notesStr) == DeserializationError::Ok) {
                    entry.rating = notesDoc["rating"].as<uint8_t>();

                    // Check if user provided a doseOut value to override volume
                    if (notesDoc["doseOut"].is<String>() && !notesDoc["doseOut"].as<String>().isEmpty()) {
                        float doseOut = notesDoc["doseOut"].as<String>().toFloat();
                        if (doseOut > 0.0f) {
                            entry.volume = encodeUnsigned(doseOut, WEIGHT_SCALE, WEIGHT_MAX_VALUE);
                        }
                    }
                }
            }
        }

        shotFile.close();

        // Append to index
        appendToIndex(entry);

        // Emit progress update with adaptive frequency
        // Update every file for small rebuilds, every few files for larger ones
        int updateFrequency = slogFiles.size() <= 20 ? 1 : (slogFiles.size() <= 100 ? 3 : 5);
        if (pluginManager && (currentIndex % updateFrequency == 0 || currentIndex == slogFiles.size())) {
            Event progressEvent;
            progressEvent.id = "evt:history-rebuild-progress";
            progressEvent.setInt("total", (int)slogFiles.size());
            progressEvent.setInt("current", currentIndex);
            progressEvent.setString("status", "processing");
            pluginManager->trigger(progressEvent);
            ESP_LOGI("ShotHistoryPlugin", "Rebuild progress: %d/%d", currentIndex, (int)slogFiles.size());

            // Small delay to allow UI updates and prevent overwhelming the system
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (nextId > static_cast<uint32_t>(controller->getSettings().getHistoryIndex())) {
        controller->getSettings().setHistoryIndex(static_cast<int>(nextId));
    }

    // Emit completion event
    if (pluginManager) {
        Event completionEvent;
        completionEvent.id = "evt:history-rebuild-progress";
        completionEvent.setInt("total", (int)slogFiles.size());
        completionEvent.setInt("current", (int)slogFiles.size());
        completionEvent.setString("status", "completed");
        pluginManager->trigger(completionEvent);
    }

    ESP_LOGI("ShotHistoryPlugin", "Index rebuild completed");
}

// Index helper functions
bool ShotHistoryPlugin::readIndexHeader(File &indexFile, ShotIndexHeader &header) {
    if (indexFile.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header)) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to read index header");
        return false;
    }
    if (header.magic != SHOT_INDEX_MAGIC) {
        ESP_LOGE("ShotHistoryPlugin", "Invalid index magic: 0x%08X", header.magic);
        return false;
    }
    return true;
}

bool ShotHistoryPlugin::writeIndexHeader(File &indexFile, const ShotIndexHeader &header) {
    if (!indexFile.seek(0, SeekSet)) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to seek to index header");
        return false;
    }
    if (indexFile.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) != sizeof(header)) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to write index header");
        return false;
    }
    return true;
}

int ShotHistoryPlugin::findEntryPosition(File &indexFile, const ShotIndexHeader &header, uint32_t shotId) {
    // Entries are appended in increasing shot-ID order and never reordered:
    // Settings::historyIndex only advances (see appendToIndex, startRecording), it is
    // raised above every id in the index at boot (syncNextIdWithIndex), upserts rewrite
    // an existing slot in place, and rebuildIndex() replays .slog files sorted by the
    // numeric ID in their filename. readRecentEntries() depends on the same order to walk
    // newest-first. So the index is a sorted array and a binary search finds any shot in
    // O(log entryCount). If an out-of-order entry was seen anyway (indexOrderBroken), a
    // miss falls back to a linear scan so a present shot is still found.
    std::lock_guard<std::recursive_mutex> guard(indexLock);
    int32_t lo = 0;
    int32_t hi = static_cast<int32_t>(header.entryCount) - 1;
    while (lo <= hi) {
        int32_t mid = lo + (hi - lo) / 2;
        size_t entryPos = sizeof(ShotIndexHeader) + static_cast<size_t>(mid) * sizeof(ShotIndexEntry);

        ShotIndexEntry entry{};
        if (!readEntryAtPosition(indexFile, entryPos, entry)) {
            ESP_LOGW("ShotHistoryPlugin", "Failed to read entry at position %d", mid);
            return -1;
        }

        if (entry.id == shotId) {
            return static_cast<int>(entryPos);
        }
        if (entry.id < shotId) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (!indexOrderBroken) {
        return -1;
    }
    for (uint32_t i = 0; i < header.entryCount; i++) {
        size_t entryPos = sizeof(ShotIndexHeader) + static_cast<size_t>(i) * sizeof(ShotIndexEntry);
        ShotIndexEntry entry{};
        if (!readEntryAtPosition(indexFile, entryPos, entry)) {
            return -1;
        }
        if (entry.id == shotId) {
            ESP_LOGD("ShotHistoryPlugin", "Shot %u found by linear scan at %u", shotId, i);
            return static_cast<int>(entryPos);
        }
    }
    return -1;
}

bool ShotHistoryPlugin::readEntryAtPosition(File &indexFile, size_t position, ShotIndexEntry &entry) {
    indexFile.seek(position, SeekSet);
    if (indexFile.read(reinterpret_cast<uint8_t *>(&entry), sizeof(entry)) != sizeof(entry)) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to read entry at position %zu", position);
        return false;
    }
    return true;
}

bool ShotHistoryPlugin::writeEntryAtPosition(File &indexFile, size_t position, const ShotIndexEntry &entry) {
    indexFile.seek(position, SeekSet);
    if (indexFile.write(reinterpret_cast<const uint8_t *>(&entry), sizeof(entry)) != sizeof(entry)) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to write entry at position %zu", position);
        return false;
    }
    return true;
}

bool ShotHistoryPlugin::createEarlyIndexEntry() {
    Profile profile = controller->getProfileManager()->getSelectedProfile();

    ShotIndexEntry indexEntry{};
    indexEntry.id = currentId.toInt();
    indexEntry.timestamp = header.startEpoch;
    indexEntry.duration = 0; // Will be overwritten on completion
    indexEntry.volume = 0;   // Will be overwritten on completion
    indexEntry.rating = 0;
    indexEntry.flags = 0; // No SHOT_FLAG_COMPLETED - indicates in-progress shot
    strncpy(indexEntry.profileId, profile.id.c_str(), sizeof(indexEntry.profileId) - 1);
    indexEntry.profileId[sizeof(indexEntry.profileId) - 1] = '\0';
    strncpy(indexEntry.profileName, profile.label.c_str(), sizeof(indexEntry.profileName) - 1);
    indexEntry.profileName[sizeof(indexEntry.profileName) - 1] = '\0';

    bool success = appendToIndex(indexEntry);
    if (success) {
        ESP_LOGD("ShotHistoryPlugin", "Created early index entry for shot %u", indexEntry.id);
    } else {
        ESP_LOGE("ShotHistoryPlugin", "Failed to create early index entry for shot %u", indexEntry.id);
    }
    return success;
}
