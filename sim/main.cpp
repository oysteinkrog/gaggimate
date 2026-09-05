// Desktop simulator entry point. Runs the real display Controller on a single
// cooperative loop on the main thread (LVGL/SDL must stay on the main thread on
// macOS), driving the firmware's loop methods directly since the FreeRTOS tasks
// are no-ops in the simulator.
#include "ESPAsyncWebServer.h"
#include "SdlDriver.h"
#include <Arduino.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <display/core/Controller.h>
#include <display/plugins/ShotHistoryPlugin.h>
#include <display/ui/default/DefaultUI.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// The generated UI event handlers reference this global (see main.h on device).
Controller controller;

// Temporary verification harness (not part of the shipped sim story): drives a
// scripted tap+screenshot sequence for UI-transition testing, since the sim has
// no other way to inject input non-interactively. One instruction per line in
// the file passed to --script:
//   WAIT <ms>            sleep, simulated time
//   TAP <x> <y>          press for 150ms at (x,y), release, same as a real click
//   SHOT <path.bmp>       write the current frame
// Lines are executed one at a time, gated on simulated millis(), from the main
// loop below (see ScriptRunner::pump()).
struct ScriptStep {
    enum Kind { Wait, Tap, Shot } kind;
    int x = 0, y = 0;
    unsigned long ms = 0;
    std::string path;
};

class ScriptRunner {
  public:
    explicit ScriptRunner(const std::string &path) {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream iss(line);
            std::string op;
            iss >> op;
            ScriptStep step{};
            if (op == "WAIT") {
                step.kind = ScriptStep::Wait;
                iss >> step.ms;
            } else if (op == "TAP") {
                step.kind = ScriptStep::Tap;
                iss >> step.x >> step.y;
            } else if (op == "SHOT") {
                step.kind = ScriptStep::Shot;
                iss >> step.path;
            } else {
                continue;
            }
            steps_.push_back(step);
        }
    }

    bool done() const { return idx_ >= steps_.size(); }

    // Called every main-loop iteration. Advances through steps as their gating
    // condition (a wait deadline, or a tap's press/release timer) is met.
    void pump(SdlDriver *drv) {
        if (done())
            return;
        const unsigned long now = millis();
        if (waitingUntil_ != 0) {
            if (now < waitingUntil_)
                return;
            if (tapPressed_) {
                // Release phase of a TAP: lift the synthetic press, then fall
                // through to advance past this step immediately.
                drv->injectPointer(steps_[idx_].x, steps_[idx_].y, false);
                tapPressed_ = false;
            }
            waitingUntil_ = 0;
            idx_++;
            return;
        }
        const ScriptStep &step = steps_[idx_];
        switch (step.kind) {
        case ScriptStep::Wait:
            printf("[script] WAIT %lums @t=%lu\n", step.ms, now);
            waitingUntil_ = now + step.ms;
            break;
        case ScriptStep::Tap:
            printf("[script] TAP %d,%d @t=%lu\n", step.x, step.y, now);
            drv->injectPointer(step.x, step.y, true);
            tapPressed_ = true;
            waitingUntil_ = now + 150; // hold, then release on the next pump()
            break;
        case ScriptStep::Shot:
            printf("[script] SHOT %s @t=%lu\n", step.path.c_str(), now);
            drv->screenshot(step.path.c_str());
            idx_++;
            break;
        }
    }

  private:
    std::vector<ScriptStep> steps_;
    size_t idx_ = 0;
    unsigned long waitingUntil_ = 0;
    bool tapPressed_ = false;
};

int main(int argc, char **argv) {
    // Optional: `--screenshot <path> [delayMs]` renders for a bit, saves a BMP, exits.
    const char *shotPath = nullptr;
    unsigned long shotDelayMs = 4000;
    const char *scriptPath = nullptr;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shotPath = argv[++i];
            if (i + 1 < argc)
                shotDelayMs = strtoul(argv[i + 1], nullptr, 10);
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            scriptPath = argv[++i];
        }
    }
    ScriptRunner *script = scriptPath ? new ScriptRunner(scriptPath) : nullptr;

    controller.setup(); // builds the UI, installs the SDL driver, marks screen ready

    // The sim has a real network (the WebUI is reachable), so present as Wi-Fi
    // connected: seeding credentials sends setupWifi() down the STA path, and the
    // WiFi shim's begin() reports WL_CONNECTED. This makes the standby screen show
    // the clock and Wi-Fi icon (and avoids captive-portal AP mode). Seed only once.
    Settings &settings = controller.getSettings();
    if (settings.getWifiSsid().isEmpty())
        settings.setWifiSsid("GaggiMate-Sim");
    if (settings.getWifiPassword().isEmpty())
        settings.setWifiPassword("simulator");

    SdlDriver *drv = SdlDriver::getInstance();
    DefaultUI *ui = controller.getUI();
    const unsigned long start = millis();
    bool shotTaken = false;

    while (!drv->shouldQuit()) {
        controller.loop();      // connection lifecycle, comms pump, plugins
        controller.loopLogic(); // process + control logic (normally a FreeRTOS task)

        // Shot history sampling normally runs in its own FreeRTOS task (a no-op in
        // the sim), so drive record() here at its native cadence.
        {
            static unsigned long lastShotSample = 0;
            if (millis() - lastShotSample >= SHOT_LOG_SAMPLE_INTERVAL_MS) {
                lastShotSample = millis();
                ShotHistory.record();
            }
        }

        if (ui) {
            ui->loop();
            ui->loopProfiles();
        }
        gm_web_pump(); // service the embedded WebUI HTTP/WS server

        // Settings persistence normally runs in a deferred save task (a no-op in the
        // sim), so flush dirty settings to NVS periodically. save(true) is a cheap
        // no-op when nothing changed.
        {
            static unsigned long lastSave = 0;
            if (millis() - lastSave >= 2000) {
                lastSave = millis();
                controller.getSettings().save(true);
            }
        }

        drv->pumpAndRender();

        if (shotPath && !shotTaken && millis() - start >= shotDelayMs) {
            drv->screenshot(shotPath);
            shotTaken = true;
            break;
        }
        delay(5);
    }
    return 0;
}
