#include "LedControlPlugin.h"
#include <display/core/Controller.h>
#include <display/core/Event.h>
#include <display/util/ColorConversion.h>

void LedControlPlugin::setup(Controller *controller, PluginManager *pluginManager) {
    this->controller = controller;
    pluginManager->on("controller:ready", [this](Event const) { initialized = true; });
}

void LedControlPlugin::loop() {
    if (!initialized) {
        return;
    }
    if (lastUpdate + UPDATE_INTERVAL < millis()) {
        lastUpdate = millis();
        updateControl();
    }
}

void LedControlPlugin::updateControl() {
    // Snapshot the colours under the guard: a web save replaces them on
    // async_tcp, and the guard keeps the set from one save.
    String active;
    String finished;
    String error;
    String idle;
    int ext;
    {
        Settings &settings = this->controller->getSettings();
        Settings::Guard guard(settings);
        active = settings.getSunriseActive();
        finished = settings.getSunriseFinished();
        error = settings.getSunriseError();
        idle = settings.getSunriseIdle();
        ext = settings.getSunriseExtBrightness();
    }
    int mode = this->controller->getMode();
    if (mode == MODE_STANDBY) {
        sendControl(0, 0, 0, 0, 0);
        return;
    }
    if (this->controller->isActive() && mode == MODE_BREW) {
        sendControl(active, ext);
        return;
    }
    bool lastWasBrew;
    {
        // Deref under the process lock — other tasks delete the process at any time (GM-147).
        std::lock_guard<std::recursive_mutex> guard(controller->getProcessLock());
        Process *last = controller->getLastProcess();
        lastWasBrew = last != nullptr && last->getType() == MODE_BREW;
    }
    if (lastWasBrew && mode == MODE_BREW) {
        sendControl(finished, ext);
        return;
    }
    if (this->controller->isLowWaterLevel() || this->controller->isErrorState()) {
        sendControl(error, ext);
        return;
    }
    sendControl(idle, ext);
}

void LedControlPlugin::sendControl(String hexColor, uint8_t ext) {
    ColorConversion::Rgbw duty = ColorConversion::fromHex(hexColor);
    sendControl(duty.r, duty.g, duty.b, duty.w, ext);
}

void LedControlPlugin::sendControl(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t ext) {
    if (r == last_r && g == last_g && b == last_b && w == last_w && ext == last_ext)
        return;

    // Send every channel as one snapshot. A single message keeps the outbound
    // coalescing queue from collapsing per-channel updates down to one channel.
    const uint8_t extInv = 255 - ext;
    const LedChannelCommand channels[] = {
        {0, r}, {1, g}, {2, b}, {3, w}, {4, extInv}, {5, extInv}, {6, extInv}, {7, extInv},
    };
    this->controller->getClientController()->sendLedControl(channels, sizeof(channels) / sizeof(channels[0]));

    last_r = r;
    last_g = g;
    last_b = b;
    last_w = w;
    last_ext = ext;
}
