
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
};

#endif // DRIVER_H
