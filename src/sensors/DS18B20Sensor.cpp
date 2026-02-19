#include "DS18B20Sensor.h"
#include "../config/Config.h"

namespace Sensors {

    DS18B20Sensor::DS18B20Sensor(bool enabled)
        : oneWire(Hardware::DS18B20_PIN),
          sensors(&oneWire),
          temperature(0.0f),
          enabled(enabled),
          sensorFound(false) {
    }

    bool DS18B20Sensor::init() {
        Serial.println(F("DS18B20: Initializing sensor..."));

        sensors.begin();

        // Check if any devices are found on the bus
        int deviceCount = sensors.getDeviceCount();

        if (deviceCount == 0) {
            Serial.println(F("DS18B20: No devices found on the 1-Wire bus"));
            sensorFound = false;
            return false;
        }

        Serial.print(F("DS18B20: Found "));
        Serial.print(deviceCount);
        Serial.println(F(" device(s)"));

        // Set resolution to 12 bits (0.0625°C precision)
        sensors.setResolution(12);

        // Set wait mode (non-blocking reads)
        sensors.setWaitForConversion(false);

        sensorFound = true;
        Serial.println(F("DS18B20: Initialized successfully"));

        return true;
    }

    bool DS18B20Sensor::read() {
        if (!enabled || !sensorFound) {
            return false;
        }

        // Request temperature reading
        sensors.requestTemperatures();

        // Wait for conversion to complete (750ms for 12-bit resolution)
        // Using non-blocking approach with yield() to prevent watchdog timeout
        unsigned long startTime = millis();
        unsigned long conversionTime = 750;

        while (millis() - startTime < conversionTime) {
            yield();  // Feed watchdog and allow other tasks
            delay(10);  // Small delay to reduce CPU usage
        }

        // Read temperature from the first sensor (index 0)
        float tempC = sensors.getTempCByIndex(0);

        // Check if reading is valid (DS18B20 returns -127°C on error)
        if (tempC == DEVICE_DISCONNECTED_C) {
            Serial.println(F("DS18B20: Error reading temperature - sensor disconnected"));
            return false;
        }

        temperature = Configuration::roundToOneDecimal(tempC);

        Serial.print(F("DS18B20: Temperature = "));
        Serial.print(temperature);
        Serial.println(F("°C"));

        return true;
    }

    void DS18B20Sensor::addToJson(JsonDocument& doc) {
        if (enabled && sensorFound) {
            doc["ds18b20"] = temperature;
        }
    }

}
