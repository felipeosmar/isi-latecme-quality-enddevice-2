#ifndef TEMPERATURE_SENSOR_H
#define TEMPERATURE_SENSOR_H

#include "ISensor.h"
#include "rom/ets_sys.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"

namespace Sensors {

    class InternalTemperatureSensor : public ISensor {
    private:
        float temperature;
        bool enabled;

    public:
        InternalTemperatureSensor(bool enabled = true);
        bool init() override;
        bool read() override;
        void addToJson(JsonDocument& doc) override;
        bool isEnabled() const override { return enabled; }
        const char* getName() const override { return "Internal Temperature"; }
        float getTemperature() const { return temperature; }
        void setEnabled(bool state) { enabled = state; }

    private:
        float readInternalTemperature();
    };

}

#endif