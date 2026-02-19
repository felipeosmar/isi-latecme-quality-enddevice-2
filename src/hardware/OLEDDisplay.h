#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>

// Forward declaration
namespace Sensors {
    class SensorManager;
}

namespace Hardware {

    class OLEDDisplay {
    private:
        static constexpr int SCREEN_WIDTH = 128;
        static constexpr int SCREEN_HEIGHT = 64;

        Adafruit_SSD1306 display;
        bool initialized;
        unsigned long lastUpdate;
        unsigned long updateInterval;
        int currentPage;
        static constexpr int MAX_PAGES = 3;
        char hostname[64];
        Sensors::SensorManager* sensorManager;

        void drawWiFiStatus();
        void drawLoRaStatus();
        void drawSensorData();
        void drawFooter();

        // Icon drawing methods
        void drawThermometerIcon(int x, int y);
        void drawHumidityIcon(int x, int y);

    public:
        OLEDDisplay();

        bool begin();
        void update();
        void clear();
        void showBootScreen();
        void showStatus(const char* status);
        void showError(const char* error);
        void nextPage();
        void setBrightness(uint8_t brightness);
        void setHostname(const char* name);
        void setSensorManager(Sensors::SensorManager* manager);

        // Display specific information
        void updateWiFiStatus(bool connected, const char* ip = nullptr);
        void updateLoRaStatus(bool connected, int rssi = 0, float snr = 0);
        void updateSensorData(float temperature, int digitalInputs, int analogInputs);
        void updatePacketStats(int sent, int received, int errors);

        bool isInitialized() const { return initialized; }
    };

}

#endif // OLED_DISPLAY_H