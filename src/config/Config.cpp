#include "Config.h"
#include <SPIFFS.h>
#include <FS.h>
#include <cstring>

namespace Configuration {

    // Function to convert board model string to enum
    BoardType getBoardTypeFromString(const char* boardModel) {
        if (strcmp(boardModel, "JVTECHv40-SHT20") == 0) {
            return BoardType::JVTECHv40_SHT20;
        } else if (strcmp(boardModel, "JVTECHv40-SHT30") == 0) {
            return BoardType::JVTECHv40_SHT30;
        } else if (strcmp(boardModel, "JVTECHv40-SHT40") == 0) {
            return BoardType::JVTECHv40_SHT40;
        } else if (strcmp(boardModel, "JVTECHv40-AM2315C") == 0) {
            return BoardType::JVTECHv40_AM2315C;
        } else if (strcmp(boardModel, "HeltecLoRaV2-SHT20") == 0) {
            return BoardType::HeltecLoRaV2_SHT20;
        } else if (strcmp(boardModel, "HeltecLoRaV2-SHT30") == 0) {
            return BoardType::HeltecLoRaV2_SHT30;
        } else if (strcmp(boardModel, "HeltecLoRaV2-SHT40") == 0) {
            return BoardType::HeltecLoRaV2_SHT40;
        } else if (strcmp(boardModel, "HeltecLoRaV2-AM2315C") == 0) {
            return BoardType::HeltecLoRaV2_AM2315C;
        } else {
            return BoardType::ESP32Board;
        }
    }

    DeviceConfig::DeviceConfig() {
        strcpy(hostname, "LoRaDevice");
        readInterval = 120000;
        loraLocalAddress = 0x01;

        tempHumEnabled = true;
        ds18b20Enabled = false;
        accEnabled = false;
        strcpy(accMainAxis, "X");
        magEnabled = false;
        gyroEnabled = false;
        digitalPin1Enabled = false;
        digitalPin2Enabled = false;
        digitalPin3Enabled = false;
        analogPin1Enabled = false;
        analogPin2Enabled = false;

        // Multi-sensor defaults
        strcpy(preferredSensor, "auto");
        enableSensorFallback = true;
        sensorHealthCheckInterval = 60;
        maxSensorErrors = 5;

        // Sensor correction defaults
        temperatureCorrection = 0.0;
        humidityCorrection = 0.0;
    }

    ConfigManager::ConfigManager() {
        setDefaults();
    }

    void ConfigManager::setDefaults() {
        config = DeviceConfig();
    }

    bool ConfigManager::loadConfiguration() {
        if (!SPIFFS.exists(CONFIG_FILE)) {
            Serial.println(F("Config file does not exist, using defaults"));
            setDefaults();
            return saveConfiguration();
        }

        File file = SPIFFS.open(CONFIG_FILE, FILE_READ);
        if (!file) {
            Serial.println(F("Failed to open config file"));
            return false;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            Serial.print(F("Failed to parse config file: "));
            Serial.println(error.c_str());
            return false;
        }

        if (!fromJson(doc)) {
            Serial.println(F("Failed to load config from JSON"));
            return false;
        }

        if (!validateConfig()) {
            Serial.println(F("Config validation failed, using defaults"));
            setDefaults();
            return false;
        }

        return true;
    }

    bool ConfigManager::saveConfiguration() {
        return saveConfiguration(config);
    }

    bool ConfigManager::saveConfiguration(const DeviceConfig& newConfig) {
        config = newConfig;

        File file = SPIFFS.open(CONFIG_FILE, FILE_WRITE);
        if (!file) {
            Serial.println(F("Failed to open config file for writing"));
            return false;
        }

        JsonDocument doc;
        toJson(doc);

        if (serializeJson(doc, file) == 0) {
            Serial.println(F("Failed to write config file"));
            file.close();
            return false;
        }

        file.close();
        Serial.println(F("Configuration saved successfully"));
        return true;
    }

    void ConfigManager::toJson(JsonDocument& doc) const {
        doc["hostname"] = config.hostname;
        doc["readinterval"] = config.readInterval;
        doc["loralocaladdress"] = config.loraLocalAddress;
        doc["temp_hum"] = config.tempHumEnabled;
        doc["ds18b20"] = config.ds18b20Enabled;
        doc["acc"] = config.accEnabled;
        doc["acc_main_axis"] = config.accMainAxis;
        doc["mag"] = config.magEnabled;
        doc["gyro"] = config.gyroEnabled;
        doc["d1"] = config.digitalPin1Enabled;
        doc["d2"] = config.digitalPin2Enabled;
        doc["d3"] = config.digitalPin3Enabled;
        doc["a1"] = config.analogPin1Enabled;
        doc["a2"] = config.analogPin2Enabled;

        // Multi-sensor configuration
        doc["preferred_sensor"] = config.preferredSensor;
        doc["enable_sensor_fallback"] = config.enableSensorFallback;
        doc["sensor_health_check_interval"] = config.sensorHealthCheckInterval;
        doc["max_sensor_errors"] = config.maxSensorErrors;

        // Sensor correction values
        doc["temperature_correction"] = config.temperatureCorrection;
        doc["humidity_correction"] = config.humidityCorrection;
    }

    bool ConfigManager::fromJson(const JsonDocument& doc) {
        strlcpy(config.hostname,
                doc["hostname"] | "LoRaDevice",
                sizeof(config.hostname));

        config.readInterval = doc["readinterval"] | 30000;
        config.loraLocalAddress = doc["loralocaladdress"] | 0x01;

        config.tempHumEnabled = doc["temp_hum"] | true;
        config.ds18b20Enabled = doc["ds18b20"] | false;
        config.accEnabled = doc["acc"] | false;

        strlcpy(config.accMainAxis,
                doc["acc_main_axis"] | "X",
                sizeof(config.accMainAxis));

        config.magEnabled = doc["mag"] | false;
        config.gyroEnabled = doc["gyro"] | false;
        config.digitalPin1Enabled = doc["d1"] | false;
        config.digitalPin2Enabled = doc["d2"] | false;
        config.digitalPin3Enabled = doc["d3"] | false;
        config.analogPin1Enabled = doc["a1"] | false;
        config.analogPin2Enabled = doc["a2"] | false;

        // Multi-sensor configuration
        strlcpy(config.preferredSensor,
                doc["preferred_sensor"] | "auto",
                sizeof(config.preferredSensor));

        config.enableSensorFallback = doc["enable_sensor_fallback"] | true;
        config.sensorHealthCheckInterval = doc["sensor_health_check_interval"] | 60;
        config.maxSensorErrors = doc["max_sensor_errors"] | 5;

        // Sensor correction values
        config.temperatureCorrection = doc["temperature_correction"] | 0.0;
        config.humidityCorrection = doc["humidity_correction"] | 0.0;

        return true;
    }

    bool ConfigManager::validateConfig() const {
        // Validate hostname length
        if (strlen(config.hostname) == 0 || strlen(config.hostname) >= 64) {
            return false;
        }

        // Validate read interval
        if (config.readInterval < 1000 || config.readInterval > 3600000) {
            return false;
        }

        // Validate LoRa address
        if (config.loraLocalAddress < 0x00 || config.loraLocalAddress > 0xFF) {
            return false;
        }

        // Validate accelerometer axis
        if (config.accEnabled) {
            if (strcmp(config.accMainAxis, "X") != 0 &&
                strcmp(config.accMainAxis, "Y") != 0 &&
                strcmp(config.accMainAxis, "Z") != 0) {
                return false;
            }
        }

        // Validate correction values (reasonable range)
        if (config.temperatureCorrection < -50.0 || config.temperatureCorrection > 50.0) {
            return false;
        }
        if (config.humidityCorrection < -50.0 || config.humidityCorrection > 50.0) {
            return false;
        }

        return true;
    }

    void ConfigManager::printConfig() const {
        Serial.println(F("=== Current Configuration ==="));
        Serial.print(F("Hostname: ")); Serial.println(config.hostname);
        Serial.print(F("Read Interval: ")); Serial.println(config.readInterval);
        Serial.print(F("LoRa Address: 0x")); Serial.println(config.loraLocalAddress, HEX);
        Serial.print(F("Temp/Hum: ")); Serial.println(config.tempHumEnabled ? F("Enabled") : F("Disabled"));
        Serial.print(F("Accelerometer: ")); Serial.println(config.accEnabled ? F("Enabled") : F("Disabled"));
        if (config.accEnabled) {
            Serial.print(F("  Main Axis: ")); Serial.println(config.accMainAxis);
        }
        Serial.print(F("Magnetometer: ")); Serial.println(config.magEnabled ? F("Enabled") : F("Disabled"));
        Serial.print(F("Gyroscope: ")); Serial.println(config.gyroEnabled ? F("Enabled") : F("Disabled"));
        Serial.print(F("Digital Pins: "));
        Serial.print(config.digitalPin1Enabled ? F("D1 ") : F(""));
        Serial.print(config.digitalPin2Enabled ? F("D2 ") : F(""));
        Serial.println(config.digitalPin3Enabled ? F("D3") : F(""));
        Serial.print(F("Analog Pins: "));
        Serial.print(config.analogPin1Enabled ? F("A1 ") : F(""));
        Serial.println(config.analogPin2Enabled ? F("A2") : F(""));
        Serial.print(F("Temperature Correction: ")); Serial.println(config.temperatureCorrection);
        Serial.print(F("Humidity Correction: ")); Serial.println(config.humidityCorrection);
        Serial.println(F("============================="));
    }

}