#include "SHT20Sensor.h"
#include <Wire.h>

namespace Sensors {

    SHT20Sensor::SHT20Sensor(Communication::I2CManager* manager)
        : ITemperatureHumiditySensor(manager, SensorType::SHT20, SHT20_ADDR),
          userRegister(0), useClockStretching(false) {

        sensorInfo.name = "SHT20";
        sensorInfo.version = "1.0";
        sensorInfo.minTemp = -40.0f;
        sensorInfo.maxTemp = 125.0f;
        sensorInfo.minHumidity = 0.0f;
        sensorInfo.maxHumidity = 100.0f;
        sensorInfo.tempAccuracy = 0.3f;
        sensorInfo.humidityAccuracy = 3.0f;
    }

    bool SHT20Sensor::detectSensor() {
        // Try to read user register to detect sensor
        if (!readUserRegister()) {
            return false;
        }

        Serial.print(F("SHT20 user register: 0x"));
        Serial.println(userRegister, HEX);

        return true;
    }

    bool SHT20Sensor::readRawData() {
        // Read temperature first
        if (!readTemperature()) {
            Serial.println(F("SHT20: Failed to read temperature"));
            return false;
        }

        // Read humidity
        if (!readHumidity()) {
            Serial.println(F("SHT20: Failed to read humidity"));
            return false;
        }

        return true;
    }

    bool SHT20Sensor::validateData() {
        // Validate ranges
        if (temperature < sensorInfo.minTemp || temperature > sensorInfo.maxTemp) {
            Serial.print(F("SHT20: Temperature out of range: "));
            Serial.println(temperature);
            return false;
        }

        if (humidity < sensorInfo.minHumidity || humidity > sensorInfo.maxHumidity) {
            Serial.print(F("SHT20: Humidity out of range: "));
            Serial.println(humidity);
            return false;
        }

        return true;
    }

    void SHT20Sensor::softReset() {
        Serial.println(F("SHT20: Performing soft reset"));
        writeCommand(SHT20_CMD_SOFT_RESET);
        delay(15); // Wait for reset to complete
    }

    bool SHT20Sensor::readUserRegister() {
        if (!writeCommand(SHT20_CMD_READ_USER_REG)) {
            return false;
        }

        auto error = i2cManager->readWithRetry(sensorInfo.address, &userRegister, 1);
        return (error == Communication::I2CError::NONE);
    }

    bool SHT20Sensor::writeUserRegister(uint8_t value) {
        uint8_t data[2] = {SHT20_CMD_WRITE_USER_REG, value};
        auto error = i2cManager->writeWithRetry(sensorInfo.address, data, 2);
        if (error == Communication::I2CError::NONE) {
            userRegister = value;
            return true;
        }
        return false;
    }

    bool SHT20Sensor::setBatteryStatus(bool lowBattery) {
        if (!readUserRegister()) return false;

        uint8_t newRegister = userRegister;
        if (lowBattery) {
            newRegister |= 0x40; // Set bit 6
        } else {
            newRegister &= ~0x40; // Clear bit 6
        }

        return writeUserRegister(newRegister);
    }

    bool SHT20Sensor::setHeater(bool enable) {
        if (!readUserRegister()) return false;

        uint8_t newRegister = userRegister;
        if (enable) {
            newRegister |= 0x04; // Set bit 2
        } else {
            newRegister &= ~0x04; // Clear bit 2
        }

        return writeUserRegister(newRegister);
    }

    bool SHT20Sensor::readTemperature() {
        uint16_t rawValue;
        uint8_t command = useClockStretching ? SHT20_CMD_TEMP_HOLD : SHT20_CMD_TEMP_NOHOLD;

        if (!readWithCRC(command, &rawValue)) {
            return false;
        }

        // Convert to temperature in Celsius
        // Formula: T = -46.85 + 175.72 * St / 2^16
        temperature = -46.85f + 175.72f * rawValue / 65536.0f;

        return true;
    }

    bool SHT20Sensor::readHumidity() {
        uint16_t rawValue;
        uint8_t command = useClockStretching ? SHT20_CMD_HUM_HOLD : SHT20_CMD_HUM_NOHOLD;

        if (!readWithCRC(command, &rawValue)) {
            return false;
        }

        // Convert to relative humidity
        // Formula: RH = -6 + 125 * Srh / 2^16
        humidity = -6.0f + 125.0f * rawValue / 65536.0f;

        // Clamp to valid range
        if (humidity < 0.0f) humidity = 0.0f;
        if (humidity > 100.0f) humidity = 100.0f;

        return true;
    }

    bool SHT20Sensor::writeCommand(uint8_t command) {
        auto error = i2cManager->writeWithRetry(sensorInfo.address, &command, 1);
        return (error == Communication::I2CError::NONE);
    }

    bool SHT20Sensor::readWithCRC(uint8_t command, uint16_t* value) {
        // Send command using Wire directly for SHT20 compatibility
        Wire.beginTransmission(sensorInfo.address);
        Wire.write(command);
        uint8_t error = Wire.endTransmission();

        if (error != 0) {
            Serial.print(F("SHT20: Command write failed, error: "));
            Serial.println(error);
            return false;
        }

        // Wait for measurement to complete (non-blocking)
        // SHT20 needs up to 85ms for measurement
        unsigned long startTime = millis();
        while (millis() - startTime < 85) {
            yield();  // Feed watchdog
            delay(5);
        }

        // Request 3 bytes (MSB, LSB, CRC)
        Wire.requestFrom(sensorInfo.address, (uint8_t)3);

        if (Wire.available() != 3) {
            Serial.print(F("SHT20: Expected 3 bytes, got "));
            Serial.println(Wire.available());
            return false;
        }

        uint8_t data[3];
        data[0] = Wire.read(); // MSB
        data[1] = Wire.read(); // LSB
        data[2] = Wire.read(); // CRC

        // Validate CRC
        if (!validateCRC(data, 2, data[2])) {
            Serial.println(F("SHT20: CRC validation failed"));
            return false;
        }

        *value = (data[0] << 8) | data[1];
        return true;
    }

    uint8_t SHT20Sensor::calculateCRC(const uint8_t* data, uint8_t length) {
        uint8_t crc = 0x00; // Initial value for SHT20

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

    bool SHT20Sensor::validateCRC(const uint8_t* data, uint8_t length, uint8_t crc) {
        return calculateCRC(data, length) == crc;
    }

}