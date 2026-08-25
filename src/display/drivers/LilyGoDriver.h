#ifndef LILYGODRIVER_H
#define LILYGODRIVER_H

#include "Driver.h"
#include <display/drivers/LilyGo-T-RGB/LilyGo_RGBPanel.h>

constexpr uint8_t LILYGO_DETECT_PIN = 8;

class LilyGoDriver : public Driver {
  public:
    bool isCompatible() override;
    void init() override;
    void setBrightness(int brightness) override { panel.setBrightness(brightness); };
    bool supportsSDCard() override;
    bool installSDCard() override;
    Display *getDisplay() override { return &panel; }
    void stopPanel() override { panel.stopPanel(); }

    static LilyGoDriver *getInstance() {
        if (instance == nullptr) {
            instance = new LilyGoDriver();
        }
        return instance;
    };

    // The instance if one was already created, and null otherwise. getInstance()
    // constructs on demand, which is the wrong question for a caller that wants
    // to know whether THIS panel is the one running: asking would make the
    // answer yes. Nothing creates a LilyGoDriver unless isCompatible() picked it.
    static LilyGoDriver *peekInstance() { return instance; }

    // Live ST7701S tuning, exposed because the registers behind them are
    // swept against the panel rather than chosen in advance. See the comment
    // on LilyGo_RGBPanel::setVcom.
    void setPanelVcom(int vcoms) override { panel.setVcom(static_cast<uint8_t>(vcoms)); }
    void setPanelInversion(uint8_t invset0) { panel.setInversion(invset0); }

  private:
    static LilyGoDriver *instance;
    LilyGo_RGBPanel panel;

    LilyGoDriver(){};
};

#endif // LILYGODRIVER_H
