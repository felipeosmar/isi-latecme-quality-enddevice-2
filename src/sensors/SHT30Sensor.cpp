#include "SHT30Sensor.h"

namespace Sensors {

    SHT30Sensor::SHT30Sensor(Communication::I2CManager* manager, uint8_t address)
        : ITemperatureHumiditySensor(manager, SensorType::SHT30, address),
          measurementCommand(SHT30_CMD_MEAS_HIGHREP) {

        sensorInfo.name = "SHT30";
        sensorInfo.version = "1.0";
        sensorInfo.minTemp = -40.0f;
        sensorInfo.maxTemp = 125.0f;
        sensorInfo.minHumidity = 0.0f;
        sensorInfo.maxHumidity = 100.0f;
        sensorInfo.tempAccuracy = 0.2f;
        sensorInfo.humidityAccuracy = 2.0f;
    }

    bool SHT30Sensor::detectSensor() {
        // Try to read status register to detect sensor
        uint16_t status;
        if (!readStatus(&status)) {
            return false;
        }

        // SHT30 should respond with valid status
        Serial.print(F("SHT30 status: 0x"));
        Serial.println(status, HEX);

        return true;
    }

    bool SHT30Sensor::readRawData() {
        // Send measurement command
        if (!writeCommand(measurementCommand)) {
            Serial.println(F("SHT30: Failed to send measurement command"));
            return false;
        }

        // Wait for measurement (depends on repeatability mode)
        delay(15); // High repeatability takes ~15ms

        // Read 6 bytes: temp MSB, temp LSB, temp CRC, hum MSB, hum LSB, hum CRC
        auto error = i2cManager->readWithRetry(sensorInfo.address, rawData, 6);
        if (error != Communication::I2CError::NONE) {
            Serial.print(F("SHT30: Read error "));
            Serial.println(static_cast<int>(error));
            return false;
        }

        return true;
    }

    bool SHT30Sensor::validateData() {
        // Validate temperature CRC
        if (!validateCRC(&rawData[0], rawData[2])) {
            Serial.println(F("SHT30: Temperature CRC error"));
            return false;
        }

        // Validate humidity CRC
        if (!validateCRC(&rawData[3], rawData[5])) {
            Serial.println(F("SHT30: Humidity CRC error"));
            return false;
        }

        // Convert raw data to temperature and humidity
        convertRawData();

        // Validate ranges
        if (temperature < sensorInfo.minTemp || temperature > sensorInfo.maxTemp) {
            Serial.print(F("SHT30: Temperature out of range: "));
            Serial.println(temperature);
            return false;
        }

        if (humidity < sensorInfo.minHumidity || humidity > sensorInfo.maxHumidity) {
            Serial.print(F("SHT30: Humidity out of range: "));
            Serial.println(humidity);
            return false;
        }

        return true;
    }

    void SHT30Sensor::softReset() {
        Serial.println(F("SHT30: Performing soft reset"));
        writeCommandWithDelay(SHT30_CMD_SOFTRESET, 100);
        clearStatus(); // Clear any error flags
    }

    bool SHT30Sensor::readStatus(uint16_t* status) {
        if (!writeCommand(SHT30_CMD_READSTATUS)) {
            return false;
        }

        uint8_t statusData[3];
        auto error = i2cManager->readWithRetry(sensorInfo.address, statusData, 3);
        if (error != Communication::I2CError::NONE) {
            return false;
        }

        // Validate CRC
        if (!validateCRC(&statusData[0], statusData[2])) {
            Serial.println(F("SHT30: Status CRC error"));
            return false;
        }

        *status = (statusData[0] << 8) | statusData[1];
        return true;
    }

    bool SHT30Sensor::clearStatus() {
        return writeCommandWithDelay(SHT30_CMD_CLEARSTATUS, 10);
    }

    bool SHT30Sensor::enableHeater(bool enable) {
        uint16_t command = enable ? SHT30_CMD_HEATER_ENABLE : SHT30_CMD_HEATER_DISABLE;
        return writeCommandWithDelay(command, 10);
    }

    void SHT30Sensor::setMeasurementMode(bool highRepeatability, bool useClockStretching) {
        if (useClockStretching) {
            measurementCommand = highRepeatability ? SHT30_CMD_MEAS_HIGHREP_STRETCH : SHT30_CMD_MEAS_LOWREP_STRETCH;
        } else {
            measurementCommand = highRepeatability ? SHT30_CMD_MEAS_HIGHREP : SHT30_CMD_MEAS_LOWREP;
        }
    }

    bool SHT30Sensor::writeCommand(uint16_t command) {
        uint8_t commandBytes[2];
        commandBytes[0] = command >> 8;
        commandBytes[1] = command & 0xFF;

        auto error = i2cManager->writeWithRetry(sensorInfo.address, commandBytes, 2);
        return (error == Communication::I2CError::NONE);
    }

    bool SHT30Sensor::writeCommandWithDelay(uint16_t command, uint16_t delayMs) {
        if (!writeCommand(command)) {
            return false;
        }
        delay(delayMs);
        return true;
    }

    uint8_t SHT30Sensor::calculateCRC(const uint8_t* data, uint8_t length) {
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

    bool SHT30Sensor::validateCRC(const uint8_t* data, uint8_t crc) {
        return calculateCRC(data, 2) == crc;
    }

    void SHT30Sensor::convertRawData() {
        // Convert temperature (16-bit)
        uint16_t tempRaw = (rawData[0] << 8) | rawData[1];
        temperature = -45.0f + 175.0f * tempRaw / 65535.0f;

        // Convert humidity (16-bit)
        uint16_t humRaw = (rawData[3] << 8) | rawData[4];
        humidity = 100.0f * humRaw / 65535.0f;
    }

}