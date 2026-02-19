#ifndef DIGITAL_SENSOR_H
#define DIGITAL_SENSOR_H

#include "ISensor.h"

namespace Sensors {

    class DigitalSensor : public ISensor {
    private:
        int pin;
        bool value;
        bool enabled;
        String name;
        String jsonKey;

    public:
        DigitalSensor(int pin, const String& name, const String& jsonKey, bool enabled = false);

        bool init() override;
        bool read() override;
        void addToJson(JsonDocument& doc) override;
        bool isEnabled() const override { return enabled; }
        const char* getName() const override { return name.c_str(); }

        void setEnabled(bool state) { enabled = state; }
        bool getValue() const { return value; }
    };

}

#endif