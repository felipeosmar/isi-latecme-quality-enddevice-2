#ifndef SHT30_SENSOR_H
#define SHT30_SENSOR_H

#include "ITemperatureHumiditySensor.h"

namespace Sensors {

    class SHT30Sensor : public ITemperatureHumiditySensor {
    private:
        // SHT30 I2C Commands
        static constexpr uint8_t SHT30_ADDR_DEFAULT = 0x44;
        static constexpr uint8_t SHT30_ADDR_ALT = 0x45;

        // Commands
        static constexpr uint16_t SHT30_CMD_MEAS_HIGHREP_STRETCH = 0x2C06;
        static constexpr uint16_t SHT30_CMD_MEAS_MEDREP_STRETCH = 0x2C0D;
        static constexpr uint16_t SHT30_CMD_MEAS_LOWREP_STRETCH = 0x2C10;
        static constexpr uint16_t SHT30_CMD_MEAS_HIGHREP = 0x2400;
        static constexpr uint16_t SHT30_CMD_MEAS_MEDREP = 0x240B;
        static constexpr uint16_t SHT30_CMD_MEAS_LOWREP = 0x2416;
        static constexpr uint16_t SHT30_CMD_READSTATUS = 0xF32D;
        static constexpr uint16_t SHT30_CMD_CLEARSTATUS = 0x3041;
        static constexpr uint16_t SHT30_CMD_SOFTRESET = 0x30A2;
        static constexpr uint16_t SHT30_CMD_HEATER_ENABLE = 0x306D;
        static constexpr uint16_t SHT30_CMD_HEATER_DISABLE = 0x3066;

        uint8_t rawData[6];
        uint16_t measurementCommand;

    public:
        SHT30Sensor(Communication::I2CManager* manager, uint8_t address = SHT30_ADDR_DEFAULT);

        // ITemperatureHumiditySensor interface
        bool detectSensor() override;
        bool readRawData() override;
        bool validateData() override;
        void softReset() override;

        // SHT30 specific methods
        bool readStatus(uint16_t* status);
        bool clearStatus();
        bool enableHeater(bool enable);
        void setMeasurementMode(bool highRepeatability = true, bool useClockStretching = false);

    private:
        bool writeCommand(uint16_t command);
        bool writeCommandWithDelay(uint16_t command, uint16_t delayMs);
        uint8_t calculateCRC(const uint8_t* data, uint8_t length);
        bool validateCRC(const uint8_t* data, uint8_t crc);
        void convertRawData();
    };

}

#endif