#include "TemperatureSensor.h"
#include "../config/Config.h"

namespace Sensors {

    InternalTemperatureSensor::InternalTemperatureSensor(bool enabled)
        : temperature(0.0f), enabled(enabled) {
    }

    bool InternalTemperatureSensor::init() {
        if (enabled) {
            Serial.println(F("Initialized internal temperature sensor"));
        }
        return true;
    }

    bool InternalTemperatureSensor::read() {
        if (!enabled) return false;

        temperature = readInternalTemperature();
        return true;
    }

    void InternalTemperatureSensor::addToJson(JsonDocument& doc) {
        if (enabled) {
            doc["t_ic"] = Configuration::roundToOneDecimal(temperature);
        }
    }

    float InternalTemperatureSensor::readInternalTemperature() {
        SET_PERI_REG_BITS(SENS_SAR_MEAS_WAIT2_REG, SENS_FORCE_XPD_SAR, 3,
                          SENS_FORCE_XPD_SAR_S);
        SET_PERI_REG_BITS(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_CLK_DIV, 10,
                          SENS_TSENS_CLK_DIV_S);
        CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
        CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
        SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP_FORCE);
        SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
        ets_delay_us(100);
        SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
        ets_delay_us(5);
        float temp_f = (float)GET_PERI_REG_BITS2(SENS_SAR_SLAVE_ADDR3_REG,
                                                 SENS_TSENS_OUT, SENS_TSENS_OUT_S);
        float temp_c = (temp_f - 32) / 1.8;
        return temp_c;
    }

}