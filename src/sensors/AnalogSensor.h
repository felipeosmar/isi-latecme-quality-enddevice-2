#ifndef ANALOG_SENSOR_H
#define ANALOG_SENSOR_H

#include "ISensor.h"

namespace Sensors {

    class AnalogSensor : public ISensor {
    private:
        int pin;
        int value;
        bool enabled;
        String name;
        String jsonKey;

    public:
        AnalogSensor(int pin, const String& name, const String& jsonKey, bool enabled = false);

        bool init() override;
        bool read() override;
        void addToJson(JsonDocument& doc) override;
        bool isEnabled() const override { return enabled; }
        const char* getName() const override { return name.c_str(); }

        void setEnabled(bool state) { enabled = state; }
        int getValue() const { return value; }
    };

}

#endif