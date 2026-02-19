#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <memory>
#include <ArduinoJson.h>
#include "ITemperatureHumiditySensor.h"
#include "DigitalSensor.h"
#include "AnalogSensor.h"
#include "TemperatureSensor.h"
#include "../config/Config.h"
#include "../communication/I2CManager.h"
#include "../hardware/PinDefinitions.h"

// Include sensor headers based on build flags
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

#include "DS18B20Sensor.h"

namespace Sensors {

    class SensorManager {
    private:
        Configuration::ConfigManager& configManager;
        Communication::I2CManager i2cManager;

        // Single temperature/humidity sensor
        std::unique_ptr<ITemperatureHumiditySensor> tempHumSensor;

        // DS18B20 1-Wire temperature sensor
        std::unique_ptr<DS18B20Sensor> ds18b20Sensor;

        // Legacy sensors (digital/analog)
        std::unique_ptr<DigitalSensor> digitalSensor1;
        std::unique_ptr<DigitalSensor> digitalSensor2;
        std::unique_ptr<DigitalSensor> digitalSensor3;
        std::unique_ptr<AnalogSensor> analogSensor1;
        std::unique_ptr<AnalogSensor> analogSensor2;
        std::unique_ptr<InternalTemperatureSensor> internalTempSensor;

        bool systemInitialized;

    public:
        explicit SensorManager(Configuration::ConfigManager& config);

        // System lifecycle
        bool begin();
        bool initializeAll();
        bool readAll();
        void addAllToJson(JsonDocument& doc);
        void updateSensorStates();

        // Sensor access
        ITemperatureHumiditySensor* getSHTSensor() { return tempHumSensor.get(); }
        DS18B20Sensor* getDS18B20Sensor() { return ds18b20Sensor.get(); }
        DigitalSensor* getDigitalSensor1() { return digitalSensor1.get(); }
        DigitalSensor* getDigitalSensor2() { return digitalSensor2.get(); }
        DigitalSensor* getDigitalSensor3() { return digitalSensor3.get(); }
        AnalogSensor* getAnalogSensor1() { return analogSensor1.get(); }
        AnalogSensor* getAnalogSensor2() { return analogSensor2.get(); }
        InternalTemperatureSensor* getTemperatureSensor() { return internalTempSensor.get(); }

        // Corrected sensor readings
        float getCorrectedTemperature();
        float getCorrectedHumidity();
        float getRawTemperature();
        float getRawHumidity();

        // Information and diagnostics
        void printSensorStatus();
        Communication::I2CManager* getI2CManager() { return &i2cManager; }

    private:
        void initializeLegacySensors();
        bool initializeTempHumSensor();
    };

}

#endif // SENSOR_MANAGER_H
