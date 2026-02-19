#include "DigitalSensor.h"

namespace Sensors {

    DigitalSensor::DigitalSensor(int pin, const String& name, const String& jsonKey, bool enabled)
        : pin(pin), value(false), enabled(enabled), name(name), jsonKey(jsonKey) {
    }

    bool DigitalSensor::init() {
        if (enabled) {
            pinMode(pin, INPUT);
            Serial.print(F("Initialized digital sensor "));
            Serial.print(name);
            Serial.print(F(" on pin "));
            Serial.println(pin);
        }
        return true;
    }

    bool DigitalSensor::read() {
        if (!enabled) return false;

        value = digitalRead(pin);
        return true;
    }

    void DigitalSensor::addToJson(JsonDocument& doc) {
        if (enabled) {
            doc[jsonKey] = value;
        }
    }

}