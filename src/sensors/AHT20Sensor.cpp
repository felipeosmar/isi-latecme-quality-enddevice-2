#include "AHT20Sensor.h"

namespace Sensors {

    AHT20Sensor::AHT20Sensor(Communication::I2CManager* manager)
        : ITemperatureHumiditySensor(manager, SensorType::AHT20, AHT20_ADDR) {

        sensorInfo.name = "AHT20";
        sensorInfo.version = "1.0";
        sensorInfo.minTemp = -40.0f;
        sensorInfo.maxTemp = 85.0f;
        sensorInfo.minHumidity = 0.0f;
        sensorInfo.maxHumidity = 100.0f;
        sensorInfo.tempAccuracy = 0.3f;
        sensorInfo.humidityAccuracy = 2.0f;
    }

    bool AHT20Sensor::detectSensor() {
        uint8_t status;
        if (!readStatus(&status)) {
            return false;
        }

        Serial.print(F("AHT20 status: 0x"));
        Serial.println(status, HEX);

        // Initialize if not calibrated
        if (!isCalibrated()) {
            Serial.println(F("AHT20: Not calibrated, initializing..."));
            return initializeSensor();
        }

        return true;
    }

    bool AHT20Sensor::readRawData() {
        // Send measurement command with parameters
        uint8_t params[2] = {0x33, 0x00};
        if (!writeCommand(AHT20_CMD_MEASURE, params, 2)) {
            Serial.println(F("AHT20: Failed to send measurement command"));
            return false;
        }

        // Wait for measurement completion
        if (!waitForMeasurement()) {
            Serial.println(F("AHT20: Measurement timeout"));
            return false;
        }

        // Read measurement data
        auto error = i2cManager->readWithRetry(sensorInfo.address, rawData, 6);
        if (error != Communication::I2CError::NONE) {
            Serial.print(F("AHT20: Read error "));
            Serial.println(static_cast<int>(error));
            return false;
        }

        return true;
    }

    bool AHT20Sensor::validateData() {
        // Check status byte (first byte should have bit 7 = 0 when measurement complete)
        if (rawData[0] & 0x80) {
            Serial.println(F("AHT20: Measurement not complete"));
            return false;
        }

        // Convert raw data
        convertRawData();

        // Validate ranges
        if (temperature < sensorInfo.minTemp || temperature > sensorInfo.maxTemp) {
            Serial.print(F("AHT20: Temperature out of range: "));
            Serial.println(temperature);
            return false;
        }

        if (humidity < sensorInfo.minHumidity || humidity > sensorInfo.maxHumidity) {
            Serial.print(F("AHT20: Humidity out of range: "));
            Serial.println(humidity);
            return false;
        }

        return true;
    }

    void AHT20Sensor::softReset() {
        Serial.println(F("AHT20: Performing soft reset"));
        writeCommand(AHT20_CMD_SOFT_RESET);
        delay(20); // Wait for reset
        initializeSensor(); // Re-initialize after reset
    }

    bool AHT20Sensor::initializeSensor() {
        uint8_t params[2] = {0x08, 0x00};
        if (!writeCommand(AHT20_CMD_INIT, params, 2)) {
            return false;
        }

        delay(10); // Wait for initialization

        // Check if calibrated after initialization
        uint8_t status;
        if (!readStatus(&status)) {
            return false;
        }

        bool calibrated = (status & 0x08) != 0;
        Serial.print(F("AHT20: Calibration status: "));
        Serial.println(calibrated ? "OK" : "Failed");

        return calibrated;
    }

    bool AHT20Sensor::readStatus(uint8_t* status) {
        if (!writeCommand(AHT20_CMD_STATUS)) {
            return false;
        }

        auto error = i2cManager->readWithRetry(sensorInfo.address, status, 1);
        return (error == Communication::I2CError::NONE);
    }

    bool AHT20Sensor::isCalibrated() {
        uint8_t status;
        if (!readStatus(&status)) {
            return false;
        }
        return (status & 0x08) != 0; // Bit 3 indicates calibration status
    }

    bool AHT20Sensor::writeCommand(uint8_t command, const uint8_t* params, uint8_t paramCount) {
        uint8_t buffer[8]; // Max command + params
        buffer[0] = command;

        if (params && paramCount > 0) {
            memcpy(&buffer[1], params, paramCount);
        }

        auto error = i2cManager->writeWithRetry(sensorInfo.address, buffer, 1 + paramCount);
        return (error == Communication::I2CError::NONE);
    }

    void AHT20Sensor::convertRawData() {
        // Extract humidity (20 bits)
        uint32_t humRaw = ((uint32_t)rawData[1] << 12) |
                         ((uint32_t)rawData[2] << 4) |
                         ((rawData[3] & 0xF0) >> 4);

        // Extract temperature (20 bits)
        uint32_t tempRaw = (((uint32_t)rawData[3] & 0x0F) << 16) |
                          ((uint32_t)rawData[4] << 8) |
                          rawData[5];

        // Convert to actual values
        humidity = (humRaw * 100.0f) / 1048576.0f; // 2^20 = 1048576
        temperature = (tempRaw * 200.0f) / 1048576.0f - 50.0f;

        // Clamp humidity to valid range
        if (humidity < 0.0f) humidity = 0.0f;
        if (humidity > 100.0f) humidity = 100.0f;
    }

    bool AHT20Sensor::waitForMeasurement() {
        // Wait up to 80ms for measurement completion
        for (int i = 0; i < 16; i++) { // 16 * 5ms = 80ms max
            delay(5);

            uint8_t status;
            if (!readStatus(&status)) {
                continue;
            }

            // Check if measurement is complete (bit 7 = 0)
            if ((status & 0x80) == 0) {
                return true;
            }
        }

        return false; // Timeout
    }

}