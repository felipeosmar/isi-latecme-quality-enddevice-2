#include "AnalogSensor.h"

namespace Sensors {

    AnalogSensor::AnalogSensor(int pin, const String& name, const String& jsonKey, bool enabled)
        : pin(pin), value(0), enabled(enabled), name(name), jsonKey(jsonKey) {
    }

    bool AnalogSensor::init() {
        if (enabled) {
            pinMode(pin, INPUT);
            Serial.print(F("Initialized analog sensor "));
            Serial.print(name);
            Serial.print(F(" on pin "));
            Serial.println(pin);
        }
        return true;
    }

    bool AnalogSensor::read() {
        if (!enabled) return false;

        value = analogRead(pin);
        return true;
    }

    void AnalogSensor::addToJson(JsonDocument& doc) {
        if (enabled) {
            doc[jsonKey] = value;
        }
    }

}