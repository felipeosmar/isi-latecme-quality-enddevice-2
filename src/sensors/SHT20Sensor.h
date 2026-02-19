#ifndef SHT20_SENSOR_H
#define SHT20_SENSOR_H

#include "ITemperatureHumiditySensor.h"

namespace Sensors {

    class SHT20Sensor : public ITemperatureHumiditySensor {
    private:
        // SHT20 I2C Address
        static constexpr uint8_t SHT20_ADDR = 0x40;

        // Commands
        static constexpr uint8_t SHT20_CMD_TEMP_HOLD = 0xE3;
        static constexpr uint8_t SHT20_CMD_TEMP_NOHOLD = 0xF3;
        static constexpr uint8_t SHT20_CMD_HUM_HOLD = 0xE5;
        static constexpr uint8_t SHT20_CMD_HUM_NOHOLD = 0xF5;
        static constexpr uint8_t SHT20_CMD_WRITE_USER_REG = 0xE6;
        static constexpr uint8_t SHT20_CMD_READ_USER_REG = 0xE7;
        static constexpr uint8_t SHT20_CMD_SOFT_RESET = 0xFE;

        uint8_t userRegister;
        bool useClockStretching;

    public:
        SHT20Sensor(Communication::I2CManager* manager);

        // ITemperatureHumiditySensor interface
        bool detectSensor() override;
        bool readRawData() override;
        bool validateData() override;
        void softReset() override;

        // SHT20 specific methods
        bool readUserRegister();
        bool writeUserRegister(uint8_t value);
        void setClockStretching(bool enable) { useClockStretching = enable; }
        bool setBatteryStatus(bool lowBattery);
        bool setHeater(bool enable);

    private:
        bool readTemperature();
        bool readHumidity();
        bool writeCommand(uint8_t command);
        bool readWithCRC(uint8_t command, uint16_t* value);
        uint8_t calculateCRC(const uint8_t* data, uint8_t length);
        bool validateCRC(const uint8_t* data, uint8_t length, uint8_t crc);
    };

}

#endif