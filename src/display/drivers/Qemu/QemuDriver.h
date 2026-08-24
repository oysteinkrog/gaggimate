#ifndef QEMUDRIVER_H
#define QEMUDRIVER_H

#ifdef GAGGIMATE_QEMU

#include "QemuPanel.h"
#include <display/drivers/Driver.h>

// Panel driver for the Espressif QEMU esp32s3 machine. Selected explicitly by
// Controller::setupPanel(); it is never part of the hardware detection chain,
// because every real probe (LilyGo's detect pin, the Amoled I2C scan) needs
// peripherals QEMU does not emulate.
class QemuDriver : public Driver {
  public:
    bool isCompatible() override { return true; }
    void init() override;
    // No backlight in the emulated device.
    void setBrightness(int brightness) override { (void)brightness; }
    bool supportsSDCard() override { return false; }
    bool installSDCard() override { return false; }
    Display *getDisplay() override { return &panel; }

    static QemuDriver *getInstance() {
        if (instance == nullptr) {
            instance = new QemuDriver();
        }
        return instance;
    }

  private:
    static QemuDriver *instance;
    QemuPanel panel;

    QemuDriver() = default;
};

#endif // GAGGIMATE_QEMU
#endif // QEMUDRIVER_H
