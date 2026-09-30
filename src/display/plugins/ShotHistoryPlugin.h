#ifndef SHOTHISTORYPLUGIN_H
#define SHOTHISTORYPLUGIN_H

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <display/core/Plugin.h>
#include <display/core/utils.h>
#include <display/models/shot_log_format.h>
#include <mutex>

constexpr size_t SHOT_HISTORY_INTERVAL = 100;
constexpr size_t MIN_FREE_SPACE_BYTES = 500 * 1024;         // 500 KB reserved free space
constexpr unsigned long EXTENDED_RECORDING_DURATION = 3000; // 3 seconds
constexpr unsigned long WEIGHT_STABILIZATION_TIME = 1000;   // 1 second
constexpr float WEIGHT_STABILIZATION_THRESHOLD = 0.1f;      // 0.1g threshold

class ShotHistoryPlugin : public Plugin {
  public:
    ShotHistoryPlugin() = default;

    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override{};

    void record();

    void handleRequest(JsonDocument &request, JsonDocument &response);

    // Index management methods
    bool appendToIndex(const ShotIndexEntry &entry);
    void updateIndexMetadata(uint32_t shotId, uint8_t rating, uint16_t volume);
    void markIndexDeleted(uint32_t shotId);
    void rebuildIndex();
    void startAsyncRebuild();
    bool ensureIndexExists();

    // Read up to maxCount most recent non-deleted index entries, newest first.
    // Returns the number of entries written to outEntries.
    size_t readRecentEntries(ShotIndexEntry *outEntries, size_t maxCount);

    // Copies index.bin into one PSRAM buffer under the index lock, so the
    // header and the entries in the copy agree: entryCount is clamped to the
    // whole entries actually read, and *outLen is the header plus exactly
    // those entries. A shot appended while the copy is taken is either wholly
    // in it or wholly out of it. False when there is no readable index or the
    // buffer cannot be allocated. The caller frees *outBuf with free().
    bool snapshotIndex(uint8_t **outBuf, size_t *outLen);

    // Raises Settings::historyIndex above every id in index.bin, once per
    // boot. After an NVS reset that kept the SD history, the counter would
    // otherwise restart at 0 and new shots would take ids below the ones on
    // the card, which breaks the id order findEntryPosition relies on.
    void syncNextIdWithIndex();

  private:
    // Index helper functions
    bool readIndexHeader(File &indexFile, ShotIndexHeader &header);
    // Writes the header at offset 0. False on a short write, which leaves the
    // on-disk entryCount disagreeing with the entries that follow it.
    bool writeIndexHeader(File &indexFile, const ShotIndexHeader &header);
    int findEntryPosition(File &indexFile, const ShotIndexHeader &header, uint32_t shotId);
    bool readEntryAtPosition(File &indexFile, size_t position, ShotIndexEntry &entry);
    bool writeEntryAtPosition(File &indexFile, size_t position, const ShotIndexEntry &entry);
    bool createEarlyIndexEntry();

    // False when the notes did not reach flash; the previous notes are left intact.
    bool saveNotes(const String &id, const JsonDocument &notes);
    void loadNotes(const String &id, JsonDocument &notes);
    void startRecording();

    uint16_t getSystemInfo(); // Helper to pack system state bits

    unsigned long getTime();

    void endRecording();
    void endExtendedRecording();
    void cleanupHistory();
    size_t getFreeSpace();

    void recordPhaseTransition(uint8_t phaseNumber, uint16_t sampleIndex,
                               uint8_t reason); // Helper for phase transitions

    Controller *controller = nullptr;
    PluginManager *pluginManager = nullptr;
    FS *fs = &LittleFS;
    String currentId = "";
    bool isFileOpen = false;
    File currentFile;
    ShotLogHeader header{};
    uint32_t sampleCount = 0;
    uint8_t ioBuffer[4096];
    size_t ioBufferPos = 0; // bytes used

    bool recording = false;
    bool extendedRecording = false;
    bool indexEntryCreated = false; // Track if early index entry was created
    // Set when any write to the current .slog came up short (a full filesystem
    // is the realistic cause). The header records sampleCount independently of
    // what actually landed, so a truncated log would otherwise be published as
    // a complete shot and read past its own end.
    bool logWriteFailed = false;
    bool shotStartedVolumetric = false; // Track initial volumetric mode
    double currentBrewDelay = 0.0;      // Brew delay (ms) the active shot was started with
    unsigned long shotStart = 0;
    unsigned long extendedRecordingStart = 0;
    unsigned long lastWeightChangeTime = 0;
    float currentTemperature = 0.0f;
    float currentActiveWeight = 0.0f;
    float lastStableWeight = 0.0f;
    float lastActiveWeight = 0.0f;
    float currentActiveFlow = 0.0f;
    float currentEstimatedWeight = 0.0f;
    float currentPuckResistance = 0.0f;
    float maxRecordedWeight = 0.0f;
    String currentProfileName;

    // Phase transition tracking (v5+)
    uint8_t lastRecordedPhase = 0xFF; // Invalid initial value to detect first phase
    uint8_t finalExitReason = 0;      // Reason the shot ended (PhaseExitReason); captured at brew end

    // Running aggregates for the current shot, used to populate the index
    // entry at completion without re-parsing the .slog file (see record()).
    uint32_t tempSumScaled = 0; // sum of sample.ct (°C * 10)
    uint32_t tempSampleCount = 0;
    uint16_t maxPressureScaled = 0; // max of sample.cp (bar * 10)
    uint32_t flowSumScaled = 0;     // sum of positive sample.fl (ml/s * 100)
    uint32_t positiveFlowCount = 0;

    // Async rebuild state
    bool rebuildInProgress = false;

    // Serialises every read and write of index.bin across the recording
    // task, the history worker and the rebuild task. Recursive because
    // appendToIndex calls ensureIndexExists and rebuildIndex calls both.
    std::recursive_mutex indexLock;
    // Set once syncNextIdWithIndex has run (under indexLock).
    bool nextIdSynced = false;
    // Set when index.bin holds an entry whose id is below the one before it.
    // The binary search in findEntryPosition can then miss a present shot,
    // so a miss falls back to a linear scan. Cleared by a rebuild, which
    // writes the entries in id order. Under indexLock.
    bool indexOrderBroken = false;

    TaskHandle_t taskHandle;
    void flushBuffer();
    static void loopTask(void *arg);
};

extern ShotHistoryPlugin ShotHistory;

#endif // SHOTHISTORYPLUGIN_H
