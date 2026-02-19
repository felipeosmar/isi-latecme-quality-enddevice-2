#ifndef ISENSOR_H
#define ISENSOR_H

#include <Arduino.h>
#include <ArduinoJson.h>

namespace Sensors {

    class ISensor {
    public:
        virtual ~ISensor() = default;

        virtual bool init() = 0;
        virtual bool read() = 0;
        virtual void addToJson(JsonDocument& doc) = 0;
        virtual bool isEnabled() const = 0;
        virtual const char* getName() const = 0;
    };

}

#endif