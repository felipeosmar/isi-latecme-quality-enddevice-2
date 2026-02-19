#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>

namespace Configuration {

    // Board model as string constant
    #ifdef BOARD_MODEL
        #define STRINGIFY(x) #x
        #define TOSTRING(x) STRINGIFY(x)
        static constexpr const char* BOARD_MODEL_NAME = TOSTRING(BOARD_MODEL);
    #else
        static constexpr const char* BOARD_MODEL_NAME = "ESP32Board";
    #endif

    // Board type enumeration for numeric identification
    enum class BoardType : int {
        ESP32Board = 0,
        JVTECHv40_SHT20 = 1,
        JVTECHv40_SHT30 = 2,
        JVTECHv40_SHT40 = 3,
        JVTECHv40_AM2315C = 4,
        HeltecLoRaV2_SHT20 = 5,
        HeltecLoRaV2_SHT30 = 6,
        HeltecLoRaV2_SHT40 = 7,
        HeltecLoRaV2_AM2315C = 8
    };

    // Function to convert board model string to enum
    BoardType getBoardTypeFromString(const char* boardModel);

    // Utility function to round float values to one decimal place (optimized for ESP32)
    inline float roundToOneDecimal(float value) {
        // Use fast integer math instead of heavy floating point operations
        // Multiply by 10, round, then divide by 10
        return static_cast<float>(static_cast<int>(value * 10.0f + 0.5f)) / 10.0f;
    }

    struct DeviceConfig {
        char hostname[64];
        // Board Model and Sensor - Configured via PlatformIO environment
        static constexpr const char* BOARD_MODEL_STR = BOARD_MODEL_NAME;
        int readInterval;
        int loraLocalAddress;

        // Sensor flags
        bool tempHumEnabled;
        bool ds18b20Enabled;
        bool accEnabled;
        char accMainAxis[2];
        bool magEnabled;
        bool gyroEnabled;
        bool digitalPin1Enabled;
        bool digitalPin2Enabled;
        bool digitalPin3Enabled;
        bool analogPin1Enabled;
        bool analogPin2Enabled;

        // Multi-sensor configuration
        char preferredSensor[16];     // "auto", "sht30", "sht40", "sht20", "aht20"
        bool enableSensorFallback;
        int sensorHealthCheckInterval; // seconds
        int maxSensorErrors;

        // Sensor correction values
        float temperatureCorrection;  // Correction value for temperature (°C)
        float humidityCorrection;     // Correction value for humidity (%)

        // Default constructor with default values
        DeviceConfig();

        // Helper methods for board type
        BoardType getBoardType() const {
            return getBoardTypeFromString(BOARD_MODEL_STR);
        }
        
        const char* getBoardModelString() const {
            return BOARD_MODEL_STR;
        }
    };

    class ConfigManager {
    private:
        static constexpr const char* CONFIG_FILE = "/config.json";
        DeviceConfig config;

    public:
        ConfigManager();

        bool loadConfiguration();
        bool saveConfiguration();
        bool saveConfiguration(const DeviceConfig& newConfig);

        DeviceConfig& getConfig() { return config; }
        const DeviceConfig& getConfig() const { return config; }

        void setDefaults();
        bool validateConfig() const;

        // Helper methods for JSON serialization
        void toJson(JsonDocument& doc) const;
        bool fromJson(const JsonDocument& doc);

        void printConfig() const;
    };

}

#endif // CONFIG_H