#ifndef NETWORKWATCHDOGPLUGIN_H
#define NETWORKWATCHDOGPLUGIN_H

#include "../core/Plugin.h"
#include <WiFiUdp.h>

struct Event;

/**
 * Detects a silently wedged IP stack and recovers from it so the network never
 * stays dead.
 */
class NetworkWatchdogPlugin : public Plugin {
  public:
    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override;

  private:
    static constexpr unsigned long PROBE_PERIOD = 2000;
    static constexpr unsigned long STATS_PERIOD = 5000;
    static constexpr unsigned long DEAD_AFTER = 15000;
    static constexpr unsigned long REBOOT_AFTER = 30000;
    static constexpr uint16_t PROBE_PORT = 9;

    // Ceiling on how long a run of out-of-buffer probes may hold the recovery
    // timer off. Long enough to cover a browser loading the whole UI in two
    // tabs, which is the worst burst this device sees, and short enough that a
    // stack wedged in a permanent ENOMEM still gets reconnected.
    static constexpr unsigned long BUSY_GRACE_MAX = 45000;

    // What a single probe established.
    enum class Probe : uint8_t {
        Alive, // the datagram went out, so egress works
        Dead,  // the send failed for a reason that suggests the stack is stuck
        Busy,  // the send failed for want of a buffer, which proves the opposite
    };

    static constexpr unsigned long MIN_UPTIME_FOR_REBOOT = 15UL * 60 * 1000; // 15 min
    static constexpr unsigned long MIN_STANDBY_FOR_REBOOT = 5UL * 60 * 1000; // 5 min

    Controller *controller = nullptr;
    WiFiUDP _probe;
    bool _socketReady = false;
    unsigned long _lastProbe = 0;
    unsigned long _lastStats = 0;
    unsigned long _lastAlive = 0;
    unsigned long _standbySince = 0;
    unsigned long _busyGrace = 0;
    uint8_t _stage = 0;
    bool _rebootHeld = false;

    bool networkShouldBeUp() const;
    Probe probeEgress();
    bool rebootAllowed(unsigned long now) const;
    void logStats(const char *reason);
    void recover(unsigned long now, unsigned long deadForMs);
};

#endif // NETWORKWATCHDOGPLUGIN_H
