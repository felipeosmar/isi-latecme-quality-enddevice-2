#include "SHT40Sensor.h"

namespace Sensors {

    SHT40Sensor::SHT40Sensor(Communication::I2CManager* manager, uint8_t address)
        : ITemperatureHumiditySensor(manager, SensorType::SHT40, address),
          measurementCommand(SHT40_CMD_MEAS_HIGHREP), serialNumber(0) {

        sensorInfo.name = "SHT40";
        sensorInfo.version = "1.0";
        sensorInfo.minTemp = -40.0f;
        sensorInfo.maxTemp = 125.0f;
        sensorInfo.minHumidity = 0.0f;
        sensorInfo.maxHumidity = 100.0f;
        sensorInfo.tempAccuracy = 0.2f;
        sensorInfo.humidityAccuracy = 1.8f;
    }

    bool SHT40Sensor::detectSensor() {
        // Try to read serial number to detect sensor
        if (!readSerialNumber()) {
            return false;
        }

        Serial.print(F("SHT40 serial number: 0x"));
        Serial.println(serialNumber, HEX);

        return true;
    }

    bool SHT40Sensor::readRawData() {
        // Send measurement command
        if (!writeCommand(measurementCommand)) {
            Serial.println(F("SHT40: Failed to send measurement command"));
            return false;
        }

        // Wait for measurement based on repeatability mode
        delay(getMeasurementDelay());

        // Read 6 bytes: temp MSB, temp LSB, temp CRC, hum MSB, hum LSB, hum CRC
        auto error = i2cManager->readWithRetry(sensorInfo.address, rawData, 6);
        if (error != Communication::I2CError::NONE) {
            Serial.print(F("SHT40: Read error "));
            Serial.println(static_cast<int>(error));
            return false;
        }

        return true;
    }

    bool SHT40Sensor::validateData() {
        // Validate temperature CRC
        if (!validateCRC(&rawData[0], rawData[2])) {
            Serial.println(F("SHT40: Temperature CRC error"));
            return false;
        }

        // Validate humidity CRC
        if (!validateCRC(&rawData[3], rawData[5])) {
            Serial.println(F("SHT40: Humidity CRC error"));
            return false;
        }

        // Convert raw data to temperature and humidity
        convertRawData();

        // Validate ranges
        if (temperature < sensorInfo.minTemp || temperature > sensorInfo.maxTemp) {
            Serial.print(F("SHT40: Temperature out of range: "));
            Serial.println(temperature);
            return false;
        }

        if (humidity < sensorInfo.minHumidity || humidity > sensorInfo.maxHumidity) {
            Serial.print(F("SHT40: Humidity out of range: "));
            Serial.println(humidity);
            return false;
        }

        return true;
    }

    void SHT40Sensor::softReset() {
        Serial.println(F("SHT40: Performing soft reset"));
        writeCommandWithDelay(SHT40_CMD_SOFT_RESET, 100);
    }

    bool SHT40Sensor::readSerialNumber() {
        if (!writeCommand(SHT40_CMD_READ_SERIAL)) {
            return false;
        }

        uint8_t serialData[6];
        auto error = i2cManager->readWithRetry(sensorInfo.address, serialData, 6);
        if (error != Communication::I2CError::NONE) {
            return false;
        }

        // Validate CRCs
        if (!validateCRC(&serialData[0], serialData[2]) ||
            !validateCRC(&serialData[3], serialData[5])) {
            Serial.println(F("SHT40: Serial number CRC error"));
            return false;
        }

        // Combine serial number
        serialNumber = ((uint32_t)serialData[0] << 24) |
                      ((uint32_t)serialData[1] << 16) |
                      ((uint32_t)serialData[3] << 8) |
                      serialData[4];

        return true;
    }

    bool SHT40Sensor::activateHeater(uint8_t heaterCommand) {
        if (!writeCommand(heaterCommand)) {
            return false;
        }

        // Wait for heater operation (1s or 100ms depending on command)
        uint16_t heaterDelay = (heaterCommand == SHT40_CMD_HEATER_HIGH_100MS ||
                               heaterCommand == SHT40_CMD_HEATER_MED_100MS ||
                               heaterCommand == SHT40_CMD_HEATER_LOW_100MS) ? 110 : 1100;

        delay(heaterDelay);

        // Read data after heater operation
        uint8_t heaterData[6];
        auto error = i2cManager->readWithRetry(sensorInfo.address, heaterData, 6);
        if (error != Communication::I2CError::NONE) {
            return false;
        }

        // Store the heated measurement data
        memcpy(rawData, heaterData, 6);

        return validateData();
    }

    void SHT40Sensor::setMeasurementMode(bool highRepeatability) {
        if (highRepeatability) {
            measurementCommand = SHT40_CMD_MEAS_HIGHREP;
        } else {
            measurementCommand = SHT40_CMD_MEAS_LOWREP;
        }
    }

    bool SHT40Sensor::writeCommand(uint8_t command) {
        auto error = i2cManager->writeWithRetry(sensorInfo.address, &command, 1);
        return (error == Communication::I2CError::NONE);
    }

    bool SHT40Sensor::writeCommandWithDelay(uint8_t command, uint16_t delayMs) {
        if (!writeCommand(command)) {
            return false;
        }
        delay(delayMs);
        return true;
    }

    uint8_t SHT40Sensor::calculateCRC(const uint8_t* data, uint8_t length) {
        uint8_t crc = 0xFF; // Initial value

        for (uint8_t i = 0; i < length; i++) {
            crc ^= data[i];
            for (uint8_t bit = 8; bit > 0; --bit) {
                if (crc & 0x80) {
                    crc = (crc << 1) ^ 0x31; // Polynomial: x^8 + x^5 + x^4 + 1
                } else {
                    crc = (crc << 1);
                }
            }
        }

        return crc;
    }

    bool SHT40Sensor::validateCRC(const uint8_t* data, uint8_t crc) {
        return calculateCRC(data, 2) == crc;
    }

    void SHT40Sensor::convertRawData() {
        // Convert temperature (16-bit)
        uint16_t tempRaw = (rawData[0] << 8) | rawData[1];
        temperature = -45.0f + 175.0f * tempRaw / 65535.0f;

        // Convert humidity (16-bit)
        uint16_t humRaw = (rawData[3] << 8) | rawData[4];
        humidity = -6.0f + 125.0f * humRaw / 65535.0f;

        // Clamp humidity to valid range
        if (humidity < 0.0f) humidity = 0.0f;
        if (humidity > 100.0f) humidity = 100.0f;
    }

    uint16_t SHT40Sensor::getMeasurementDelay() {
        switch (measurementCommand) {
            case SHT40_CMD_MEAS_HIGHREP: return 10; // High repeatability: ~8.2ms
            case SHT40_CMD_MEAS_MEDREP: return 5;   // Medium repeatability: ~4.5ms
            case SHT40_CMD_MEAS_LOWREP: return 2;   // Low repeatability: ~1.7ms
            default: return 10;
        }
    }

}