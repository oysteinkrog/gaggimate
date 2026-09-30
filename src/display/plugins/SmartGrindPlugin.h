#ifndef SMARTGRINDPLUGIN_H
#define SMARTGRINDPLUGIN_H
#include "../core/Plugin.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

const String COMMAND_ON = "Power%20On";
const String COMMAND_OFF = "Power%20off";

constexpr int SG_MODE_OFF = 0;
constexpr int SG_MODE_OFF_ON = 1;
constexpr int SG_MODE_ON_OFF = 2;

struct Event;

// Switches a Tasmota-style smart plug in front of the grinder over HTTP.
//
// The grind events fire on the task that changes the process state:
// controller:grind:end from Controller::loopLogic, ahead of loopControl,
// which sends the grinder-off command to the controller board. A blocking
// GET there (plus the 500 ms pause of the off-on mode, plus up to the
// connect timeout when the plug is unreachable) delayed that command and
// the keepalive ping by the same amount. So the handlers only post a job
// to a queue, and a worker task of this plugin does the HTTP work.
class SmartGrindPlugin : public Plugin {
  public:
    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override{};

  private:
    enum class Job : uint8_t {
        On,       // grind start in on-off mode
        Off,      // grind end in every other mode
        OffThenOn // grind end in off-on mode: off, 500 ms, on
    };

    void post(Job job);
    void run(Job job);
    void controlRelay(const String &command);
    static void workerMain(void *arg);

    Controller *controller = nullptr;
    QueueHandle_t queue = nullptr;
};

#endif // SMARTGRINDPLUGIN_H
