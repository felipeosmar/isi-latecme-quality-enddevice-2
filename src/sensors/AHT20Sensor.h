#ifndef AHT20_SENSOR_H
#define AHT20_SENSOR_H

#include "ITemperatureHumiditySensor.h"

namespace Sensors {

    class AHT20Sensor : public ITemperatureHumiditySensor {
    private:
        // AHT20 I2C Address
        static constexpr uint8_t AHT20_ADDR = 0x38;

        // Commands
        static constexpr uint8_t AHT20_CMD_INIT = 0xBE;
        static constexpr uint8_t AHT20_CMD_MEASURE = 0xAC;
        static constexpr uint8_t AHT20_CMD_SOFT_RESET = 0xBA;
        static constexpr uint8_t AHT20_CMD_STATUS = 0x71;

        uint8_t rawData[6];

    public:
        AHT20Sensor(Communication::I2CManager* manager);

        // ITemperatureHumiditySensor interface
        bool detectSensor() override;
        bool readRawData() override;
        bool validateData() override;
        void softReset() override;

        // AHT20 specific methods
        bool initializeSensor();
        bool readStatus(uint8_t* status);
        bool isCalibrated();

    private:
        bool writeCommand(uint8_t command, const uint8_t* params = nullptr, uint8_t paramCount = 0);
        void convertRawData();
        bool waitForMeasurement();
    };

}

#endif