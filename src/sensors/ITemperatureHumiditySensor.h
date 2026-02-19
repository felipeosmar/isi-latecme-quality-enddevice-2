#ifndef ITEMPERATURE_HUMIDITY_SENSOR_H
#define ITEMPERATURE_HUMIDITY_SENSOR_H

#include "ISensor.h"
#include "../communication/I2CManager.h"

namespace Sensors {

    enum class SensorType {
        UNKNOWN = 0,
        SHT20,
        SHT30,
        SHT40,
        SHTC3,
        AHT20,
        AHT25,
        DHT22_I2C
    };

    enum class SensorStatus {
        UNINITIALIZED = 0,
        INITIALIZED,
        ERROR,
        NOT_FOUND,
        CHECKSUM_ERROR,
        TIMEOUT
    };

    struct SensorInfo {
        SensorType type;
        uint8_t address;
        String name;
        String version;
        float minTemp;
        float maxTemp;
        float minHumidity;
        float maxHumidity;
        float tempAccuracy;
        float humidityAccuracy;
    };

    class ITemperatureHumiditySensor : public ISensor {
    protected:
        Communication::I2CManager* i2cManager;
        SensorInfo sensorInfo;
        SensorStatus status;
        float temperature;
        float humidity;
        bool enabled;
        unsigned long lastReadTime;
        int errorCount;
        static constexpr unsigned long READ_INTERVAL = 2000; // 2 seconds minimum

    public:
        ITemperatureHumiditySensor(Communication::I2CManager* manager, SensorType type, uint8_t address);
        virtual ~ITemperatureHumiditySensor() = default;

        // ISensor interface implementation
        bool init() override;
        bool read() override;
        void addToJson(JsonDocument& doc) override;
        bool isEnabled() const override { return enabled; }
        const char* getName() const override { return sensorInfo.name.c_str(); }

        // Temperature/Humidity specific interface
        virtual bool detectSensor() = 0;
        virtual bool readRawData() = 0;
        virtual bool validateData() = 0;
        virtual void softReset() = 0;

        // Getters
        float getTemperature() const { return temperature; }
        float getHumidity() const { return humidity; }
        SensorType getSensorType() const { return sensorInfo.type; }
        SensorStatus getStatus() const { return status; }
        SensorInfo getSensorInfo() const { return sensorInfo; }
        int getErrorCount() const { return errorCount; }
        bool isInitialized() const { return status == SensorStatus::INITIALIZED; }

        // Control
        void setEnabled(bool state) { enabled = state; }
        void resetErrorCount() { errorCount = 0; }

        // Diagnostics
        bool healthCheck();
        String getStatusString() const;
        void printDiagnostics() const;

    protected:
        bool isReadyToRead() const;
        void updateStatus(SensorStatus newStatus);
        void incrementErrorCount();
    };

}

#endif