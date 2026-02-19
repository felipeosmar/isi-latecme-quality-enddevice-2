#ifndef SHT40_SENSOR_H
#define SHT40_SENSOR_H

#include "ITemperatureHumiditySensor.h"

namespace Sensors {

    class SHT40Sensor : public ITemperatureHumiditySensor {
    private:
        // SHT40 I2C Addresses
        static constexpr uint8_t SHT40_ADDR_DEFAULT = 0x44;
        static constexpr uint8_t SHT40_ADDR_ALT = 0x45;

        // Commands
        static constexpr uint8_t SHT40_CMD_MEAS_HIGHREP = 0xFD;
        static constexpr uint8_t SHT40_CMD_MEAS_MEDREP = 0xF6;
        static constexpr uint8_t SHT40_CMD_MEAS_LOWREP = 0xE0;
        static constexpr uint8_t SHT40_CMD_READ_SERIAL = 0x89;
        static constexpr uint8_t SHT40_CMD_SOFT_RESET = 0x94;

        // Heater commands (activate heater for 1s)
        static constexpr uint8_t SHT40_CMD_HEATER_HIGH_1S = 0x39;
        static constexpr uint8_t SHT40_CMD_HEATER_HIGH_100MS = 0x32;
        static constexpr uint8_t SHT40_CMD_HEATER_MED_1S = 0x2F;
        static constexpr uint8_t SHT40_CMD_HEATER_MED_100MS = 0x24;
        static constexpr uint8_t SHT40_CMD_HEATER_LOW_1S = 0x1E;
        static constexpr uint8_t SHT40_CMD_HEATER_LOW_100MS = 0x15;

        uint8_t rawData[6];
        uint8_t measurementCommand;
        uint32_t serialNumber;

    public:
        SHT40Sensor(Communication::I2CManager* manager, uint8_t address = SHT40_ADDR_DEFAULT);

        // ITemperatureHumiditySensor interface
        bool detectSensor() override;
        bool readRawData() override;
        bool validateData() override;
        void softReset() override;

        // SHT40 specific methods
        bool readSerialNumber();
        uint32_t getSerialNumber() const { return serialNumber; }
        bool activateHeater(uint8_t heaterCommand);
        void setMeasurementMode(bool highRepeatability = true);

    private:
        bool writeCommand(uint8_t command);
        bool writeCommandWithDelay(uint8_t command, uint16_t delayMs);
        uint8_t calculateCRC(const uint8_t* data, uint8_t length);
        bool validateCRC(const uint8_t* data, uint8_t crc);
        void convertRawData();
        uint16_t getMeasurementDelay();
    };

}

#endif