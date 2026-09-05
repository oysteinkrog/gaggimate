// Desktop screen driver: an SDL2 window standing in for the device panel, wired
// into LVGL as the display + pointer (mouse) input device. Implements the same
// Driver interface the hardware panels use.
#pragma once

#include <display/drivers/Driver.h>

class SdlDriver : public Driver {
  public:
    bool isCompatible() override { return true; }
    void init() override; // SDL_Init + window + LVGL display/indev registration
    void setBrightness(int) override {}
    bool supportsSDCard() override { return false; }
    bool installSDCard() override { return false; }

    static SdlDriver *getInstance() {
        if (instance == nullptr)
            instance = new SdlDriver();
        return instance;
    }

    // Driven from the simulator main loop (main thread).
    void pumpAndRender(); // poll SDL events + present the LVGL framebuffer
    bool shouldQuit() const;
    void screenshot(const char *path); // writes a BMP of the current frame

    // Scripted-input test hook (sim/main.cpp --script mode): sets the mouse
    // point LVGL's indev reads on the next pumpAndRender(), same as a real
    // SDL_MOUSEMOTION/BUTTONDOWN/UP would. Not used by interactive runs.
    void injectPointer(int x, int y, bool pressed);

  private:
    static SdlDriver *instance;
    SdlDriver() = default;
};
