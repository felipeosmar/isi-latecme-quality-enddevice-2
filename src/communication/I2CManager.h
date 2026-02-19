#ifndef I2C_MANAGER_H
#define I2C_MANAGER_H

#include <Wire.h>
#include <Arduino.h>
#include <vector>
#include "../hardware/PinDefinitions.h"

namespace Communication {

    struct I2CDevice {
        uint8_t address;
        String description;
        bool responsive;
        unsigned long lastSeen;
        int errorCount;
    };

    enum class I2CError {
        NONE = 0,
        TIMEOUT = 263,
        NACK = 2,
        CHECKSUM = 1,
        UNKNOWN = 255
    };

    class I2CManager {
    private:
        static constexpr int SCAN_TIMEOUT = 1000;
        static constexpr int MAX_RETRIES = 3;
        static constexpr int RETRY_DELAY = 100;

        std::vector<I2CDevice> detectedDevices;
        bool initialized;
        unsigned long lastScanTime;
        static constexpr unsigned long SCAN_INTERVAL = 60000; // 60 seconds

    public:
        I2CManager();

        bool begin();
        std::vector<uint8_t> scanBus();
        bool testDevice(uint8_t address);
        bool writeDevice(uint8_t address, uint8_t* data, size_t length);
        bool readDevice(uint8_t address, uint8_t* buffer, size_t length);

        // Robust communication with retry
        I2CError writeWithRetry(uint8_t address, uint8_t* data, size_t length);
        I2CError readWithRetry(uint8_t address, uint8_t* buffer, size_t length);

        // Device management
        void updateDeviceStatus();
        std::vector<I2CDevice> getDetectedDevices() const { return detectedDevices; }
        bool isDeviceAvailable(uint8_t address);
        String getDeviceDescription(uint8_t address);

        // Statistics
        int getErrorCount(uint8_t address);
        void resetErrorCount(uint8_t address);
        void printDeviceStatus();

    private:
        String getKnownDeviceName(uint8_t address);
        void addOrUpdateDevice(uint8_t address, bool responsive);
    };

}

#endif