#ifndef CONTROLLEROTA_H
#define CONTROLLEROTA_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFiClientSecure.h>

constexpr char SERVICE_OTA_BLE_UUID[] = "fe590001-54ae-4a28-9f74-dfccb248601d";
constexpr char CHARACTERISTIC_OTA_BL_UUID_RX[] = "fe590002-54ae-4a28-9f74-dfccb248601d";
constexpr char CHARACTERISTIC_OTA_BL_UUID_TX[] = "fe590003-54ae-4a28-9f74-dfccb248601d";

constexpr uint16_t MTU = 120;
constexpr uint16_t PART_SIZE = 19000;
// How long the transfer waits on a silent controller before giving up. The
// board asks for the next part as soon as it has written the previous one, so
// this only trips when it has genuinely stopped answering.
constexpr uint32_t SIGNAL_TIMEOUT_MS = 60000;

using ctr_progress_callback_t = std::function<void(int progress)>;

class ControllerOTA {
  public:
    ControllerOTA() = default;
    ~ControllerOTA() = default;
    void init(NimBLEClient *client, const ctr_progress_callback_t &progress_callback);

    // True only if the controller actually took the image. Every failure path
    // returns false without putting bytes on the BLE link -- this pushes
    // firmware to the board that runs the heater and the pump, so "probably
    // fine" is not good enough here.
    bool update(WiFiClientSecure &wifi_client, const String &release_url);

  private:
    bool downloadFile(WiFiClientSecure &wifi_client, const String &release_url);
    bool runUpdate(Stream &in, uint32_t size);
    bool sendPart(Stream &in, uint32_t totalSize) const;
    // False when the controller did not acknowledge the write, so callers stop
    // rather than carry on with a gap in the image.
    bool sendData(uint8_t *data, uint16_t len) const;
    // False when the stream stopped producing bytes before len were read, so
    // callers can abort instead of shipping whatever was already in the buffer.
    bool fillBuffer(Stream &in, uint8_t *buffer, uint16_t len) const;
    void notifyUpdate() const;
    void onReceive(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify);

    NimBLEClient *client = nullptr;
    NimBLERemoteCharacteristic *txChar = nullptr;
    NimBLERemoteCharacteristic *rxChar = nullptr;

    ctr_progress_callback_t progressCallback = nullptr;

    bool interrupted = false;
    uint8_t lastSignal = 0x00;
    uint32_t currentPart = 0;
    uint32_t fileParts = 0;
};

#endif // CONTROLLEROTA_H
