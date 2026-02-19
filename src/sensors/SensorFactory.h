#ifndef SENSOR_FACTORY_H
#define SENSOR_FACTORY_H

#include "ITemperatureHumiditySensor.h"
#include "../communication/I2CManager.h"
#include <memory>
#include <vector>

namespace Sensors {

    struct SensorCandidate {
        SensorType type;
        uint8_t address;
        String name;
        int priority; // Higher number = higher priority
    };

    class SensorFactory {
    private:
        Communication::I2CManager* i2cManager;
        std::vector<SensorCandidate> knownSensors;

    public:
        SensorFactory(Communication::I2CManager* manager);

        // Factory methods
        std::unique_ptr<ITemperatureHumiditySensor> createSensor(SensorType type, uint8_t address);
        std::unique_ptr<ITemperatureHumiditySensor> createSensorByAddress(uint8_t address);
        std::vector<std::unique_ptr<ITemperatureHumiditySensor>> detectAndCreateSensors();

        // Detection methods
        std::vector<SensorCandidate> detectAvailableSensors();
        SensorCandidate findBestSensor();

        // Information
        std::vector<SensorCandidate> getSupportedSensors() const { return knownSensors; }
        bool isSensorSupported(uint8_t address);
        String getSensorName(uint8_t address);

    private:
        void initializeKnownSensors();
        std::unique_ptr<ITemperatureHumiditySensor> createSHT20Sensor(uint8_t address);
        std::unique_ptr<ITemperatureHumiditySensor> createSHT30Sensor(uint8_t address);
        std::unique_ptr<ITemperatureHumiditySensor> createSHT40Sensor(uint8_t address);
        std::unique_ptr<ITemperatureHumiditySensor> createSHTC3Sensor(uint8_t address);
        std::unique_ptr<ITemperatureHumiditySensor> createAHT20Sensor(uint8_t address);
        std::unique_ptr<ITemperatureHumiditySensor> createAHT25Sensor(uint8_t address);
    };

}

#endif