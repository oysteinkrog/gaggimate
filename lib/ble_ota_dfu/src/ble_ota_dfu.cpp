/* Copyright 2022 Vincent Stragier */
#include "ble_ota_dfu.hpp"

#include <esp_ota_ops.h>

QueueHandle_t start_update_queue;
QueueHandle_t update_uploading_queue;

// Hex-dump the current value of a TX characteristic prior to notify().
// Enabled by defining DEBUG_BLE_OTA_DFU_TX at build time; compiles to a no-op
// otherwise. Replaces the former NimBLECharacteristicCallbacks::onNotify hook
// (removed from the base class in NimBLE-Arduino 2.x).
static inline void logTxPacket(BLECharacteristic *pChar) {
#ifdef DEBUG_BLE_OTA_DFU_TX
    if (pChar == nullptr) {
        return;
    }
    std::string value = pChar->getValue();
    uint16_t len = value.length();
    const auto *pData = reinterpret_cast<const uint8_t *>(value.data());
    ESP_LOGD(TAG, "TX on %s length=%u", pChar->getUUID().toString().c_str(), (unsigned)len);
    Serial.print("TX  ");
    for (uint16_t i = 0; i < len; i++) {
        Serial.printf("%02X ", pData[i]);
    }
    Serial.println();
#else
    (void)pChar;
#endif
}

void task_install_update(void *parameters) {
    FS file_system = FLASH;
    const char path[] = "/update.bin";
    uint8_t data = 0;
    BLE_OTA_DFU *OTA_DFU_BLE;
    OTA_DFU_BLE = reinterpret_cast<BLE_OTA_DFU *>(parameters);
    delay(100);

    // Wait for the upload to be completed
    bool start_update = false;
    xQueuePeek(start_update_queue, &start_update, portMAX_DELAY);
    while (!start_update) {
        delay(500);
        xQueuePeek(start_update_queue, &start_update, portMAX_DELAY);
    }

    ESP_LOGE(TAG, "Starting OTA update");

    // Open update.bin file.
    File update_binary = FLASH.open(path);

    // If the file cannot be loaded, return.
    if (!update_binary) {
        ESP_LOGE(TAG, "Could not load update.bin from spiffs root");
        vTaskDelete(NULL);
    }

    // Verify that the file is not a directory
    if (update_binary.isDirectory()) {
        ESP_LOGE(TAG, "Error, update.bin is not a file");
        update_binary.close();
        vTaskDelete(NULL);
    }

    // Get binary file size
    size_t update_size = update_binary.size();

    // Proceed to the update if the file is not empty
    if (update_size <= 0) {
        ESP_LOGE(TAG, "Error, update file is empty");
        update_binary.close();
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "Starting the update");
    // perform_update(update_binary, update_size);
    String result = (String) static_cast<char>(0x0F);

    // Init update
    //
    // Arduino's UpdateClass writes to whatever esp_ota_get_next_update_partition()
    // returns, and IDF returns the *running* partition when the table has only one
    // OTA slot. Beginning an update there erases the app currently executing, with
    // nothing to fall back to and no way in to reflash except USB. Refuse instead.
    // Every table a release ships has two slots, so this only trips on
    // single-slot development builds (see partitions/headless_8mb.csv).
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
    if (target == nullptr || target == running) {
        ESP_LOGE(TAG, "Refusing OTA: no spare app slot (running=%s)", running != nullptr ? running->label : "?");
        result += "Refusing OTA: this build has a single app slot, reflash over USB instead";
    } else if (Update.begin(update_size)) {
        // Perform the update
        size_t written = Update.writeStream(update_binary);

        ESP_LOGI(TAG, "Written: %d/%d. %s", written, update_size, written == update_size ? "Success!" : "Retry?");
        result +=
            "Written : " + String(written) + "/" + String(update_size) + " [" + String((written / update_size) * 100) + " %] \n";

        // Check update
        if (Update.end()) {
            ESP_LOGI(TAG, "OTA done!");
            result += "OTA Done: ";

            if (Update.isFinished()) {
                ESP_LOGI(TAG, "Update successfully completed. Rebooting...");
            } else {
                ESP_LOGE(TAG, "Update not finished? Something went wrong!");
            }

            result += Update.isFinished() ? "Success!\n" : "Failed!\n";
        } else {
            ESP_LOGE(TAG, "Error Occurred. Error #: %d", Update.getError());
            result += "Error #: " + String(Update.getError());
        }
    } else {
        ESP_LOGE(TAG, "Not enough space to begin BLE OTA DFU");
        result += "Not enough space to begin BLE OTA DFU";
    }

    update_binary.close();

    // When finished remove the binary from spiffs
    // to indicate the end of the process
    ESP_LOGI(TAG, "Removing update file");
    FLASH.remove(path);

    if (OTA_DFU_BLE->connected()) {
        // Return the result to the client (tells the client if the update was a
        // successfull or not)
        ESP_LOGI(TAG, "Sending result to client");
        OTA_DFU_BLE->send_OTA_DFU(result);
        // OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX->setValue(result);
        // OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX->notify();
        ESP_LOGE(TAG, "%s", result.c_str());
        ESP_LOGI(TAG, "Result sent to client");
        delay(5000);
    }

    ESP_LOGE(TAG, "Rebooting ESP32: complete OTA update");
    delay(5000);
    ESP.restart();

    // ESP_LOGI(TAG, "Installation is complete");
    vTaskDelete(NULL);
}

//    void onStatus(BLECharacteristic* pCharacteristic, Status s, uint32_t
//    code) {
//      Serial.print("Status ");
//      Serial.print(s);
//      Serial.print(" on characteristic ");
//      Serial.print(pCharacteristic->getUUID().toString().c_str());
//      Serial.print(" with code ");
//      Serial.println(code);
//    }

uint16_t BLEOverTheAirDeviceFirmwareUpdate::write_binary(fs::FS *file_system, const char *path, uint8_t *data, uint16_t length,
                                                         bool keep_open) {
    static File file;

    // Append data to the file

    if (!file_open) {
        ESP_LOGI(TAG, "Opening binary file %s\r\n", path);
        file = file_system->open(path, FILE_WRITE);
        // Only latch the flag on a file we actually got. Setting it
        // unconditionally meant a failed open was never retried: every later
        // call skipped the open, fell through to the !file check and returned 0
        // for the rest of the transfer.
        file_open = static_cast<bool>(file);
    }

    if (!file) {
        ESP_LOGE(TAG, "Failed to open the file to write");
        return 0;
    }

    size_t written = 0;
    if (data != nullptr) {
        ESP_LOGI(TAG, "Write binary file %s\r\n", path);
        written = file.write(data, length);
        if (written != length) {
            // The caller adds this return value to received_file_size, and that
            // running total is the only thing gating the install: reporting the
            // requested length after a short write let received_file_size reach
            // expected_file_size over a truncated update.bin, which was then
            // flashed as a complete image. Returning what landed keeps the total
            // short, so the install never starts and the display's OTA signal
            // timeout reports the failure.
            ESP_LOGE(TAG, "Short write to %s: %u of %u bytes", path, written, length);
        }
    }

    if (keep_open) {
        return static_cast<uint16_t>(written);
    } else {
        file.close();
        file_open = false;
        return 0;
    }
}

void BLEOverTheAirDeviceFirmwareUpdate::onWrite(BLECharacteristic *pCharacteristic, NimBLEConnInfo & /*connInfo*/) {
    // uint8_t *pData;
    std::string value = pCharacteristic->getValue();
    uint16_t len = value.length();
    // pData = pCharacteristic->getData();
    uint8_t *pData = (uint8_t *)value.data();

    // Check that data have been received. A zero-length write is legal on the
    // wire and the switch below dereferences pData[0] unconditionally.
    if (pData != NULL && len > 0) {
// #define DEBUG_BLE_OTA_DFU_RX
#ifdef DEBUG_BLE_OTA_DFU_RX
        ESP_LOGD(TAG, "Write callback for characteristic %s of data length %d", pCharacteristic->getUUID().toString().c_str(),
                 len);
        Serial.print("RX  ");
        for (uint16_t index = 0; index < len; index++) {
            Serial.printf("%02X ", pData[index]);
        }
        Serial.println();
#endif
        switch (pData[0]) {
            // Send total and used sizes
        case 0xEF: {
            FLASH.format();

            // Send flash size
            uint16_t total_size = FLASH.totalBytes();
            uint16_t used_size = FLASH.usedBytes();
            uint8_t flash_size[] = {0xEF,
                                    static_cast<uint8_t>(total_size >> 16),
                                    static_cast<uint8_t>(total_size >> 8),
                                    static_cast<uint8_t>(total_size),
                                    static_cast<uint8_t>(used_size >> 16),
                                    static_cast<uint8_t>(used_size >> 8),
                                    static_cast<uint8_t>(used_size)};
            OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX->setValue(flash_size, 7);
            logTxPacket(OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX);
            OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX->notify();
            delay(10);
        } break;

            // Write parts to RAM
        case 0xFB: {
            // Everything addressing `updater` here comes off the wire: pData[1]
            // is the sub-packet index and MTU was set by the peer in 0xFF, so
            // the destination offset was entirely peer-controlled against a
            // fixed 20000-byte buffer. A packet claiming index 255 with the
            // MTU the display actually uses already lands 10 KB past the end,
            // and `len - 2` underflows to 65535 on a 1-byte packet. Both wrote
            // straight over whatever follows the buffer in the object.
            if (len < 3) {
                ESP_LOGW(TAG, "Ignoring undersized part packet (%u bytes)", len);
                break;
            }
            if (MTU == 0) {
                // No valid 0xFF preceded this, so every sub-packet would stack
                // at offset 0 and assemble a garbage image that then gets flashed.
                ESP_LOGW(TAG, "Ignoring part packet before a valid MTU was negotiated");
                break;
            }
            const uint16_t payload = len - 2;
            const uint32_t offset = static_cast<uint32_t>(pData[1]) * MTU;
            if (offset > UPDATER_SIZE || payload > UPDATER_SIZE - offset) {
                ESP_LOGW(TAG, "Ignoring part packet outside the buffer: offset %u + %u > %u", offset, payload, UPDATER_SIZE);
                break;
            }
            // pData[1] is the position of the next part
            for (uint16_t index = 0; index < payload; index++) {
                updater[!selected_updater][offset + index] = pData[index + 2];
            }
        } break;

            // Write updater content to the flash
        case 0xFC: {
            if (len < 5) {
                ESP_LOGW(TAG, "Ignoring undersized flush packet (%u bytes)", len);
                break;
            }
            OTA_DFU_BLE->setUpdating(true);
            selected_updater = !selected_updater;
            write_len[selected_updater] = (pData[1] * 256) + pData[2];
            // write_binary() reads this many bytes out of `updater`, so a peer
            // asking for more than the buffer holds used to flush up to 45 KB of
            // adjacent memory into update.bin.
            if (write_len[selected_updater] > UPDATER_SIZE) {
                ESP_LOGW(TAG, "Clamping part length %u to the %u-byte buffer", write_len[selected_updater], UPDATER_SIZE);
                write_len[selected_updater] = UPDATER_SIZE;
            }
            current_progression = (pData[3] * 256) + pData[4];

            received_file_size += write_binary(&FLASH, "/update.bin", updater[selected_updater], write_len[selected_updater]);

            if ((current_progression < parts - 1) && !FASTMODE) {
                uint8_t progression[] = {0xF1, (uint8_t)((current_progression + 1) / 256),
                                         (uint8_t)((current_progression + 1) % 256)};
                OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX->setValue(progression, 3);
                logTxPacket(OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX);
                OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX->notify();
                delay(10);
            }

            ESP_LOGI(TAG, "Upload progress: %d/%d", current_progression + 1, parts);
            if (current_progression + 1 == parts) {
                // If all the file has been received, send the progression
                uint8_t progression[] = {0xF2, (uint8_t)((current_progression + 1) / 256),
                                         (uint8_t)((current_progression + 1) % 256)};
                OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX->setValue(progression, 3);
                logTxPacket(OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX);
                OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX->notify();
                delay(10);

                if (received_file_size != expected_file_size) {
                    // received_file_size += (pData[1] * 256) + pData[2];
                    received_file_size +=
                        write_binary(&FLASH, "/update.bin", updater[selected_updater], write_len[selected_updater]);

                    if (received_file_size > expected_file_size) {
                        ESP_LOGW(TAG, "Unexpected size:\n Expected: %d\nReceived: %d", expected_file_size, received_file_size);
                    }

                } else {
                    ESP_LOGI(TAG, "Installing update");

                    // Start the installation
                    write_binary(&FLASH, "/update.bin", nullptr, 0, false);
                    bool start_update = true;
                    xQueueOverwrite(start_update_queue, &start_update);
                }
            }
        } break;

            // Remove previous file and send transfer mode
        case 0xFD: {
            // Remove previous (failed?) update
            if (FLASH.exists("/update.bin")) {
                ESP_LOGI(TAG, "Removing previous update");
                FLASH.remove("/update.bin");
            }

            OTA_DFU_BLE->setUpdating(true);
            // Send mode ("fast" or "slow")
            uint8_t mode[] = {0xAA, FASTMODE};
            OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX->setValue(mode, 2);
            logTxPacket(OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX);
            OTA_DFU_BLE->pCharacteristic_BLE_OTA_DFU_TX->notify();
            delay(10);
        } break;

            // Keep track of the received file and of the expected file sizes
        case 0xFE:
            if (len < 5) {
                ESP_LOGW(TAG, "Ignoring undersized size packet (%u bytes)", len);
                break;
            }
            received_file_size = 0;
            expected_file_size = (pData[1] * 16777216) + (pData[2] * 65536) + (pData[3] * 256) + pData[4];

            ESP_LOGI(TAG, "Available space: %d\nFile Size: %d\n", FLASH.totalBytes() - FLASH.usedBytes(), expected_file_size);
            break;

            // Switch to update mode
        case 0xFF:
            if (len < 5) {
                ESP_LOGW(TAG, "Ignoring undersized mode packet (%u bytes)", len);
                break;
            }
            OTA_DFU_BLE->setUpdating(true);
            parts = (pData[1] * 256) + pData[2];
            MTU = (pData[3] * 256) + pData[4];
            // A single sub-packet has to fit in the buffer for any offset to be
            // meaningful. Rejected here as well as bounds-checked in 0xFB so a
            // nonsense MTU says so once instead of once per dropped packet.
            if (MTU == 0 || MTU > UPDATER_SIZE) {
                ESP_LOGW(TAG, "Refusing MTU %u: outside 1..%u", MTU, UPDATER_SIZE);
                MTU = 0;
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown command: %02X", pData[0]);
            break;
        }
        // ESP_LOGE(TAG, "Not stuck in loop");
    }
    delay(1);
}

bool BLE_OTA_DFU::configure_OTA(NimBLEServer *pServer) {
    // Init FLASH
#ifdef USE_SPIFFS
    if (!SPIFFS.begin(FORMAT_FLASH_IF_MOUNT_FAILED)) {
        ESP_LOGE(TAG, "SPIFFS Mount Failed");
        return false;
    }
    ESP_LOGI(TAG, "SPIFFS Mounted");
#else
    if (!FFat.begin()) {
        ESP_LOGE(TAG, "FFat Mount Failed");
        if (FORMAT_FLASH_IF_MOUNT_FAILED)
            FFat.format();
        return false;
    }
    ESP_LOGI(TAG, "FFat Mounted");
#endif

    // Get the pointer to the pServer
    this->pServer = pServer;
    // Create the BLE OTA DFU Service
    pServiceOTA = pServer->createService(SERVICE_OTA_BLE_UUID);

    if (pServiceOTA == nullptr) {
        return false;
    }

    BLECharacteristic *pCharacteristic_BLE_OTA_DFU_RX =
        pServiceOTA->createCharacteristic(CHARACTERISTIC_OTA_BL_UUID_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);

    if (pCharacteristic_BLE_OTA_DFU_RX == nullptr) {
        return false;
    }

    auto *bleOTACharacteristicCallbacksRX = new BLEOverTheAirDeviceFirmwareUpdate();
    bleOTACharacteristicCallbacksRX->OTA_DFU_BLE = this;
    pCharacteristic_BLE_OTA_DFU_RX->setCallbacks(bleOTACharacteristicCallbacksRX);

    pCharacteristic_BLE_OTA_DFU_TX = pServiceOTA->createCharacteristic(CHARACTERISTIC_OTA_BL_UUID_TX, NIMBLE_PROPERTY::NOTIFY);

    if (pCharacteristic_BLE_OTA_DFU_TX == nullptr) {
        return false;
    }

    // No-op under esp-nimble-cpp 2.x (services are registered by
    // NimBLEServer::start(), invoked from NimBLEAdvertising::start()), but kept
    // for symmetry with the comms service in BleServerTransport and to document
    // intent.
    pServiceOTA->start();
    return true;
}

void BLE_OTA_DFU::start_OTA() {
    bool state = false;
    initialize_queue(&start_update_queue, bool, &state, 1);
    initialize_queue(&update_uploading_queue, bool, &state, 1);
    // initialize_empty_queue(&update_queue, uint8_t, UPDATE_QUEUE_SIZE);

    ESP_LOGI(TAG, "Available heap memory: %d", ESP.getFreeHeap());
    // ESP_LOGE(TAG, "Available stack memory: %d",

    ESP_LOGI(TAG, "Available free space: %d", FLASH.totalBytes() - FLASH.usedBytes());
    // ESP_LOGE(TAG, "Available free blocks: %d", FLASH.blockCount());
    TaskHandle_t taskInstallUpdate = NULL;

    xTaskCreatePinnedToCoreAndAssert(task_install_update, "task_install_update", 5120, static_cast<void *>(this), 5,
                                     &taskInstallUpdate, 1);

    ESP_LOGI(TAG, "Available memory (after task started): %d", ESP.getFreeHeap());
}

bool BLE_OTA_DFU::begin(String local_name) {
    // Create the BLE Device
    BLEDevice::init(local_name.c_str());

    ESP_LOGI(TAG, "Starting BLE UART services");

    // Create the BLE Server
    pServer = BLEDevice::createServer();

    if (pServer == nullptr) {
        return false;
    }

    this->configure_OTA(pServer);

    // Start advertising
    pServer->getAdvertising()->addServiceUUID(pServiceOTA->getUUID());
    pServer->getAdvertising()->start();

    this->start_OTA();
    return true;
}

bool BLE_OTA_DFU::connected() {
    // True if connected
    return pServer->getConnectedCount() > 0;
}

bool BLE_OTA_DFU::isUpdating() const { return updating; }

void BLE_OTA_DFU::setUpdating(bool updating) { this->updating = updating; }

void BLE_OTA_DFU::send_OTA_DFU(uint8_t value) {
    uint8_t _value = value;
    this->pCharacteristic_BLE_OTA_DFU_TX->setValue(&_value, 1);
    logTxPacket(this->pCharacteristic_BLE_OTA_DFU_TX);
    this->pCharacteristic_BLE_OTA_DFU_TX->notify();
}

void BLE_OTA_DFU::send_OTA_DFU(uint8_t *value, size_t size) {
    this->pCharacteristic_BLE_OTA_DFU_TX->setValue(value, size);
    logTxPacket(this->pCharacteristic_BLE_OTA_DFU_TX);
    this->pCharacteristic_BLE_OTA_DFU_TX->notify();
}

void BLE_OTA_DFU::send_OTA_DFU(String value) {
    this->pCharacteristic_BLE_OTA_DFU_TX->setValue(value.c_str());
    logTxPacket(this->pCharacteristic_BLE_OTA_DFU_TX);
    this->pCharacteristic_BLE_OTA_DFU_TX->notify();
}
