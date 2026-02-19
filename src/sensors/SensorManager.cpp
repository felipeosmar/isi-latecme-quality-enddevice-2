#include "SensorManager.h"
#include "../config/Config.h"

namespace Sensors {

    SensorManager::SensorManager(Configuration::ConfigManager& config)
        : configManager(config), systemInitialized(false) {
    }

    bool SensorManager::begin() {
        Serial.println(F("SensorManager: Starting initialization..."));

        // Initialize I2C Manager
        if (!i2cManager.begin()) {
            Serial.println(F("SensorManager: I2C initialization failed"));
            return false;
        }

        systemInitialized = true;
        return true;
    }

    bool SensorManager::initializeAll() {
        if (!systemInitialized) {
            if (!begin()) return false;
        }

        Serial.println(F("SensorManager: Initializing all sensors..."));

        // Initialize legacy sensors (digital/analog)
        initializeLegacySensors();

        // Initialize temperature/humidity sensor
        if (!initializeTempHumSensor()) {
            Serial.println(F("SensorManager: Temperature/humidity sensor initialization failed"));
        }

        // Initialize DS18B20 sensor
        const auto& config = configManager.getConfig();
        ds18b20Sensor = std::unique_ptr<DS18B20Sensor>(new DS18B20Sensor(config.ds18b20Enabled));
        if (config.ds18b20Enabled) {
            if (!ds18b20Sensor->init()) {
                Serial.println(F("SensorManager: DS18B20 sensor initialization failed"));
            }
        }

        Serial.println(F("SensorManager: Initialization complete"));
        printSensorStatus();

        return true;
    }

    bool SensorManager::readAll() {
        if (!systemInitialized) return false;

        bool success = true;

        // Read legacy sensors
        if (digitalSensor1 && digitalSensor1->isEnabled() && !digitalSensor1->read()) {
            Serial.println(F("Failed to read Digital Sensor 1"));
            success = false;
        }
        if (digitalSensor2 && digitalSensor2->isEnabled() && !digitalSensor2->read()) {
            Serial.println(F("Failed to read Digital Sensor 2"));
            success = false;
        }
        if (digitalSensor3 && digitalSensor3->isEnabled() && !digitalSensor3->read()) {
            Serial.println(F("Failed to read Digital Sensor 3"));
            success = false;
        }
        if (analogSensor1 && analogSensor1->isEnabled() && !analogSensor1->read()) {
            Serial.println(F("Failed to read Analog Sensor 1"));
            success = false;
        }
        if (analogSensor2 && analogSensor2->isEnabled() && !analogSensor2->read()) {
            Serial.println(F("Failed to read Analog Sensor 2"));
            success = false;
        }
        if (internalTempSensor && internalTempSensor->isEnabled() && !internalTempSensor->read()) {
            Serial.println(F("Failed to read Internal Temperature Sensor"));
            success = false;
        }

        // Read temperature/humidity sensor
        if (tempHumSensor && tempHumSensor->isEnabled()) {
            if (!tempHumSensor->read()) {
                Serial.println(F("Failed to read temperature/humidity sensor"));
                success = false;
            }
        }

        // Read DS18B20 sensor
        if (ds18b20Sensor && ds18b20Sensor->isEnabled()) {
            if (!ds18b20Sensor->read()) {
                Serial.println(F("Failed to read DS18B20 sensor"));
                success = false;
            }
        }

        return success;
    }

    void SensorManager::addAllToJson(JsonDocument& doc) {
        // Add legacy sensor data
        if (digitalSensor1 && digitalSensor1->isEnabled()) {
            digitalSensor1->addToJson(doc);
        }
        if (digitalSensor2 && digitalSensor2->isEnabled()) {
            digitalSensor2->addToJson(doc);
        }
        if (digitalSensor3 && digitalSensor3->isEnabled()) {
            digitalSensor3->addToJson(doc);
        }
        if (analogSensor1 && analogSensor1->isEnabled()) {
            analogSensor1->addToJson(doc);
        }
        if (analogSensor2 && analogSensor2->isEnabled()) {
            analogSensor2->addToJson(doc);
        }
        if (internalTempSensor && internalTempSensor->isEnabled()) {
            internalTempSensor->addToJson(doc);
        }

        // Add temperature data - prioritize DS18B20 if enabled, otherwise use temp/hum sensor
        if (ds18b20Sensor && ds18b20Sensor->isEnabled() && ds18b20Sensor->isSensorFound()) {
            // Use DS18B20 temperature
            doc["temp"] = ds18b20Sensor->getTemperature();
            // Only add humidity if temp/hum sensor is also enabled
            if (tempHumSensor && tempHumSensor->isEnabled()) {
                doc["hum"] = getCorrectedHumidity();
            }
        } else if (tempHumSensor && tempHumSensor->isEnabled()) {
            // Use temperature/humidity sensor data with correction
            doc["temp"] = getCorrectedTemperature();
            doc["hum"] = getCorrectedHumidity();
        }
    }

    void SensorManager::updateSensorStates() {
        const auto& config = configManager.getConfig();

        // Update legacy sensor states
        if (digitalSensor1) digitalSensor1->setEnabled(config.digitalPin1Enabled);
        if (digitalSensor2) digitalSensor2->setEnabled(config.digitalPin2Enabled);
        if (digitalSensor3) digitalSensor3->setEnabled(config.digitalPin3Enabled);
        if (analogSensor1) analogSensor1->setEnabled(config.analogPin1Enabled);
        if (analogSensor2) analogSensor2->setEnabled(config.analogPin2Enabled);

        // Update temperature/humidity sensor state
        if (tempHumSensor) tempHumSensor->setEnabled(config.tempHumEnabled);

        // Update DS18B20 sensor state
        if (ds18b20Sensor) ds18b20Sensor->setEnabled(config.ds18b20Enabled);
    }

    float SensorManager::getCorrectedTemperature() {
        if (!tempHumSensor || !tempHumSensor->isEnabled()) {
            return 0.0f;
        }
        float rawTemp = tempHumSensor->getTemperature();
        float correctedTemp = rawTemp + configManager.getConfig().temperatureCorrection;
        return Configuration::roundToOneDecimal(correctedTemp);
    }

    float SensorManager::getCorrectedHumidity() {
        if (!tempHumSensor || !tempHumSensor->isEnabled()) {
            return 0.0f;
        }
        float rawHum = tempHumSensor->getHumidity();
        float correctedHum = rawHum + configManager.getConfig().humidityCorrection;

        // Ensure humidity stays within valid range (0-100%)
        if (correctedHum < 0.0f) correctedHum = 0.0f;
        if (correctedHum > 100.0f) correctedHum = 100.0f;

        return Configuration::roundToOneDecimal(correctedHum);
    }

    float SensorManager::getRawTemperature() {
        if (!tempHumSensor || !tempHumSensor->isEnabled()) {
            return 0.0f;
        }
        return tempHumSensor->getTemperature();
    }

    float SensorManager::getRawHumidity() {
        if (!tempHumSensor || !tempHumSensor->isEnabled()) {
            return 0.0f;
        }
        return tempHumSensor->getHumidity();
    }

    void SensorManager::printSensorStatus() {
        Serial.println(F("\n=== Sensor Status ==="));

        // I2C status
        i2cManager.printDeviceStatus();

        // Temperature/Humidity sensor
        Serial.println(F("Temperature/Humidity Sensor:"));
        if (tempHumSensor) {
            Serial.print(F("  "));
            Serial.print(tempHumSensor->getName());
            Serial.print(F(" - "));
            Serial.print(tempHumSensor->getStatusString());
            Serial.print(F(" - "));
            Serial.print(tempHumSensor->getTemperature());
            Serial.print(F("°C, "));
            Serial.print(tempHumSensor->getHumidity());
            Serial.println(F("%RH"));
        } else {
            Serial.println(F("  No sensor configured"));
        }

        Serial.println(F("=====================\n"));
    }

    void SensorManager::initializeLegacySensors() {
        const auto& deviceConfig = configManager.getConfig();

        // Create digital sensors
        digitalSensor1 = std::unique_ptr<DigitalSensor>(new DigitalSensor(
            Hardware::DIGITAL_PIN_1, "D1", "d1", deviceConfig.digitalPin1Enabled));
        digitalSensor1->init();

        digitalSensor2 = std::unique_ptr<DigitalSensor>(new DigitalSensor(
            Hardware::DIGITAL_PIN_2, "D2", "d2", deviceConfig.digitalPin2Enabled));
        digitalSensor2->init();

        digitalSensor3 = std::unique_ptr<DigitalSensor>(new DigitalSensor(
            Hardware::DIGITAL_PIN_3, "D3", "d3", deviceConfig.digitalPin3Enabled));
        digitalSensor3->init();

        // Create analog sensors
        analogSensor1 = std::unique_ptr<AnalogSensor>(new AnalogSensor(
            Hardware::ANALOG_PIN_1, "A1", "a1", deviceConfig.analogPin1Enabled));
        analogSensor1->init();

        analogSensor2 = std::unique_ptr<AnalogSensor>(new AnalogSensor(
            Hardware::ANALOG_PIN_2, "A2", "a2", deviceConfig.analogPin2Enabled));
        analogSensor2->init();

        // Create internal temperature sensor
        internalTempSensor = std::unique_ptr<InternalTemperatureSensor>(new InternalTemperatureSensor(true));
        internalTempSensor->init();
    }

    bool SensorManager::initializeTempHumSensor() {
        Serial.println(F("SensorManager: Initializing temperature/humidity sensor..."));

        #ifdef SENSOR_TYPE_SHT20
            Serial.println(F("Creating SHT20 sensor"));
            tempHumSensor = std::unique_ptr<ITemperatureHumiditySensor>(new SHT20Sensor(&i2cManager));
        #elif defined(SENSOR_TYPE_SHT30)
            Serial.println(F("Creating SHT30 sensor"));
            tempHumSensor = std::unique_ptr<ITemperatureHumiditySensor>(new SHT30Sensor(&i2cManager));
        #elif defined(SENSOR_TYPE_SHT40)
            Serial.println(F("Creating SHT40 sensor"));
            tempHumSensor = std::unique_ptr<ITemperatureHumiditySensor>(new SHT40Sensor(&i2cManager));
        #elif defined(SENSOR_TYPE_AM2315C)
            Serial.println(F("Creating AHT20 sensor (AM2315C compatible)"));
            tempHumSensor = std::unique_ptr<ITemperatureHumiditySensor>(new AHT20Sensor(&i2cManager));
        #else
            #warning "No temperature/humidity sensor type defined"
            Serial.println(F("No temperature/humidity sensor type defined"));
            return false;
        #endif

        if (tempHumSensor) {
            if (!tempHumSensor->init()) {
                Serial.println(F("Temperature/humidity sensor initialization failed"));
                return false;
            }
            Serial.println(F("Temperature/humidity sensor initialized successfully"));
            return true;
        }

        return false;
    }

}
