#ifndef DS18B20_SENSOR_H
#define DS18B20_SENSOR_H

#include "ISensor.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "../hardware/PinDefinitions.h"

namespace Sensors {

    class DS18B20Sensor : public ISensor {
    private:
        OneWire oneWire;
        DallasTemperature sensors;
        float temperature;
        bool enabled;
        bool sensorFound;

    public:
        DS18B20Sensor(bool enabled = true);
        bool init() override;
        bool read() override;
        void addToJson(JsonDocument& doc) override;
        bool isEnabled() const override { return enabled; }
        const char* getName() const override { return "DS18B20"; }
        float getTemperature() const { return temperature; }
        void setEnabled(bool state) { enabled = state; }
        bool isSensorFound() const { return sensorFound; }
    };

}

#endif // DS18B20_SENSOR_H
