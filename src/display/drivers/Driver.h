
#ifndef DRIVER_H
#define DRIVER_H

class Display;

class Driver {
  public:
    virtual ~Driver() = default;

    virtual bool isCompatible();
    virtual void init();
    virtual void setBrightness(int brightness);
    virtual bool supportsSDCard();
    virtual bool installSDCard();
    // Direct panel access for code that renders outside the LVGL pipeline
    // (e.g. the standby animation). May return nullptr.
    virtual Display *getDisplay() { return nullptr; }
    // Permanently stop panel scan-out (until reboot). Used during display OTA:
    // RGB scan-out from PSRAM and sustained flash writes contend on the S3's
    // shared memory bus and the OTA download starves and aborts.
    virtual void stopPanel() {}
    // Panel VCOM, as the controller's own register value, or ignored by panels
    // that do not expose one. Tunable because the value that nulls inversion
    // flicker moves with the glass's temperature and with the individual unit,
    // so there is no single right answer to compile in.
    virtual void setPanelVcom(int) {}
};

#endif // DRIVER_H
