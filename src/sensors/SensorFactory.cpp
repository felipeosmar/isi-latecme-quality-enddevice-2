#include "SensorFactory.h"

// Conditional sensor includes based on build configuration
#ifdef SENSOR_TYPE_SHT20
    #include "SHT20Sensor.h"
#endif

#ifdef SENSOR_TYPE_SHT30
    #include "SHT30Sensor.h"
#endif

#ifdef SENSOR_TYPE_SHT40
    #include "SHT40Sensor.h"
#endif

#ifdef SENSOR_TYPE_AM2315C
    #include "AHT20Sensor.h"
#endif

namespace Sensors {

    SensorFactory::SensorFactory(Communication::I2CManager* manager)
        : i2cManager(manager) {
        initializeKnownSensors();
    }

    std::unique_ptr<ITemperatureHumiditySensor> SensorFactory::createSensor(SensorType type, uint8_t address) {
        if (!i2cManager) {
            Serial.println(F("SensorFactory: I2C Manager not available"));
            return nullptr;
        }

        switch (type) {
            case SensorType::SHT20:
                return createSHT20Sensor(address);
            case SensorType::SHT30:
                return createSHT30Sensor(address);
            case SensorType::SHT40:
                return createSHT40Sensor(address);
            case SensorType::AHT20:
                return createAHT20Sensor(address);
            case SensorType::AHT25:
                return createAHT25Sensor(address);
            default:
                Serial.print(F("SensorFactory: Unsupported sensor type "));
                Serial.println(static_cast<int>(type));
                return nullptr;
        }
    }

    std::unique_ptr<ITemperatureHumiditySensor> SensorFactory::createSensorByAddress(uint8_t address) {
        for (const auto& candidate : knownSensors) {
            if (candidate.address == address) {
                Serial.print(F("SensorFactory: Creating "));
                Serial.print(candidate.name);
                Serial.print(F(" at address 0x"));
                Serial.println(address, HEX);
                return createSensor(candidate.type, address);
            }
        }

        Serial.print(F("SensorFactory: No known sensor for address 0x"));
        Serial.println(address, HEX);
        return nullptr;
    }

    std::vector<std::unique_ptr<ITemperatureHumiditySensor>> SensorFactory::detectAndCreateSensors() {
        std::vector<std::unique_ptr<ITemperatureHumiditySensor>> sensors;

        Serial.println(F("SensorFactory: Detecting available sensors..."));

        auto availableSensors = detectAvailableSensors();

        for (const auto& candidate : availableSensors) {
            auto sensor = createSensor(candidate.type, candidate.address);
            if (sensor && sensor->init()) {
                Serial.print(F("SensorFactory: Successfully created "));
                Serial.println(candidate.name);
                sensors.push_back(std::move(sensor));
            } else {
                Serial.print(F("SensorFactory: Failed to initialize "));
                Serial.println(candidate.name);
            }
        }

        Serial.print(F("SensorFactory: Created "));
        Serial.print(sensors.size());
        Serial.println(F(" working sensors"));

        return sensors;
    }

    std::vector<SensorCandidate> SensorFactory::detectAvailableSensors() {
        std::vector<SensorCandidate> availableSensors;

        // Get list of devices found on I2C bus
        auto detectedDevices = i2cManager->getDetectedDevices();

        for (const auto& device : detectedDevices) {
            if (!device.responsive) continue;

            // Check if this address corresponds to a known sensor
            for (const auto& knownSensor : knownSensors) {
                if (knownSensor.address == device.address) {
                    availableSensors.push_back(knownSensor);
                    Serial.print(F("SensorFactory: Found candidate "));
                    Serial.print(knownSensor.name);
                    Serial.print(F(" at 0x"));
                    Serial.println(device.address, HEX);
                    break;
                }
            }
        }

        // Sort by priority (highest first)
        std::sort(availableSensors.begin(), availableSensors.end(),
                 [](const SensorCandidate& a, const SensorCandidate& b) {
                     return a.priority > b.priority;
                 });

        return availableSensors;
    }

    SensorCandidate SensorFactory::findBestSensor() {
        auto availableSensors = detectAvailableSensors();

        if (availableSensors.empty()) {
            Serial.println(F("SensorFactory: No sensors detected"));
            SensorCandidate unknown;
            unknown.name = "Unknown";
            unknown.type = SensorType::UNKNOWN;
            unknown.address = 0;
            unknown.priority = 0;
            return unknown;
        }

        // Return the highest priority sensor
        auto best = availableSensors[0];
        Serial.print(F("SensorFactory: Best sensor is "));
        Serial.print(best.name);
        Serial.print(F(" at 0x"));
        Serial.println(best.address, HEX);

        return best;
    }

    bool SensorFactory::isSensorSupported(uint8_t address) {
        for (const auto& sensor : knownSensors) {
            if (sensor.address == address) {
                return true;
            }
        }
        return false;
    }

    String SensorFactory::getSensorName(uint8_t address) {
        for (const auto& sensor : knownSensors) {
            if (sensor.address == address) {
                return sensor.name;
            }
        }
        return "Unknown";
    }

    void SensorFactory::initializeKnownSensors() {
        knownSensors.clear();

        // Only add the sensor type configured in build flags
        #ifdef SENSOR_TYPE_SHT20
            SensorCandidate sht20;
            sht20.name = "SHT20";
            sht20.type = SensorType::SHT20;
            sht20.address = 0x40;
            sht20.priority = 100;
            knownSensors.push_back(sht20);
            Serial.println(F("SensorFactory: Configured for SHT20 sensor"));
        #endif

        #ifdef SENSOR_TYPE_SHT30
            SensorCandidate sht30_44;
            sht30_44.name = "SHT30";
            sht30_44.type = SensorType::SHT30;
            sht30_44.address = 0x44;
            sht30_44.priority = 100;
            knownSensors.push_back(sht30_44);

            SensorCandidate sht30_45;
            sht30_45.name = "SHT30";
            sht30_45.type = SensorType::SHT30;
            sht30_45.address = 0x45;
            sht30_45.priority = 100;
            knownSensors.push_back(sht30_45);
            Serial.println(F("SensorFactory: Configured for SHT30 sensor"));
        #endif

        #ifdef SENSOR_TYPE_SHT40
            SensorCandidate sht40_44;
            sht40_44.name = "SHT40";
            sht40_44.type = SensorType::SHT40;
            sht40_44.address = 0x44;
            sht40_44.priority = 100;
            knownSensors.push_back(sht40_44);

            SensorCandidate sht40_45;
            sht40_45.name = "SHT40";
            sht40_45.type = SensorType::SHT40;
            sht40_45.address = 0x45;
            sht40_45.priority = 100;
            knownSensors.push_back(sht40_45);
            Serial.println(F("SensorFactory: Configured for SHT40 sensor"));
        #endif

        #ifdef SENSOR_TYPE_AM2315C
            SensorCandidate am2315c;
            am2315c.name = "AM2315C";
            am2315c.type = SensorType::AHT20;  // AM2315C is AHT20 compatible
            am2315c.address = 0x38;
            am2315c.priority = 100;
            knownSensors.push_back(am2315c);
            Serial.println(F("SensorFactory: Configured for AM2315C sensor"));
        #endif

        Serial.print(F("SensorFactory: Initialized with "));
        Serial.print(knownSensors.size());
        Serial.println(F(" configured sensor types"));
    }

    std::unique_ptr<ITemperatureHumiditySensor> SensorFactory::createSHT20Sensor(uint8_t address) {
        #ifdef SENSOR_TYPE_SHT20
            return std::unique_ptr<ITemperatureHumiditySensor>(new SHT20Sensor(i2cManager));
        #else
            Serial.println(F("SensorFactory: SHT20 not configured in build"));
            return nullptr;
        #endif
    }

    std::unique_ptr<ITemperatureHumiditySensor> SensorFactory::createSHT30Sensor(uint8_t address) {
        #ifdef SENSOR_TYPE_SHT30
            return std::unique_ptr<ITemperatureHumiditySensor>(new SHT30Sensor(i2cManager, address));
        #else
            Serial.println(F("SensorFactory: SHT30 not configured in build"));
            return nullptr;
        #endif
    }

    std::unique_ptr<ITemperatureHumiditySensor> SensorFactory::createSHT40Sensor(uint8_t address) {
        #ifdef SENSOR_TYPE_SHT40
            return std::unique_ptr<ITemperatureHumiditySensor>(new SHT40Sensor(i2cManager, address));
        #else
            Serial.println(F("SensorFactory: SHT40 not configured in build"));
            return nullptr;
        #endif
    }

    std::unique_ptr<ITemperatureHumiditySensor> SensorFactory::createSHTC3Sensor(uint8_t address) {
        // SHTC3 not implemented yet
        Serial.println(F("SensorFactory: SHTC3 not implemented"));
        return nullptr;
    }

    std::unique_ptr<ITemperatureHumiditySensor> SensorFactory::createAHT20Sensor(uint8_t address) {
        #ifdef SENSOR_TYPE_AM2315C
            return std::unique_ptr<ITemperatureHumiditySensor>(new AHT20Sensor(i2cManager));
        #else
            Serial.println(F("SensorFactory: AHT20/AM2315C not configured in build"));
            return nullptr;
        #endif
    }

    std::unique_ptr<ITemperatureHumiditySensor> SensorFactory::createAHT25Sensor(uint8_t address) {
        #ifdef SENSOR_TYPE_AM2315C
            // AHT25 uses same implementation as AHT20
            return std::unique_ptr<ITemperatureHumiditySensor>(new AHT20Sensor(i2cManager));
        #else
            Serial.println(F("SensorFactory: AHT25/AM2315C not configured in build"));
            return nullptr;
        #endif
    }

}