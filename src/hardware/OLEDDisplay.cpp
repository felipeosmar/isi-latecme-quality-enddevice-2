#include "OLEDDisplay.h"
#include "PinDefinitions.h"
#include "../sensors/SensorManager.h"
#include <WiFi.h>

namespace Hardware {

    OLEDDisplay::OLEDDisplay()
        : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, Hardware::OLED_RST),
          initialized(false),
          lastUpdate(0),
          updateInterval(1000),
          currentPage(0),
          sensorManager(nullptr) {
        strcpy(hostname, "Device");  // Default hostname
    }

    bool OLEDDisplay::begin() {
        Wire.begin(Hardware::OLED_SDA, Hardware::OLED_SCL);

        if (!display.begin(SSD1306_SWITCHCAPVCC, Hardware::OLED_ADDRESS)) {
            Serial.println(F("OLED: SSD1306 allocation failed"));
            return false;
        }

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.cp437(true);

        initialized = true;
        showBootScreen();

        Serial.println(F("OLED: Display initialized successfully"));
        return true;
    }

    void OLEDDisplay::update() {
        if (!initialized) return;

        unsigned long now = millis();
        if (now - lastUpdate < updateInterval) return;

        lastUpdate = now;

        display.clearDisplay();

        switch (currentPage) {
            case 0:
                drawSensorData();
                break;
            case 1:
                drawWiFiStatus();
                break;
            case 2:
                drawLoRaStatus();
                break;
        }

        drawFooter();
        display.display();
    }

    void OLEDDisplay::clear() {
        if (!initialized) return;
        display.clearDisplay();
        display.display();
    }

    void OLEDDisplay::showBootScreen() {
        if (!initialized) return;

        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.println(F("LoRa IoT"));
        display.setTextSize(1);
        display.setCursor(0, 20);
        display.println(F("End Device"));
        display.setCursor(0, 35);
        display.print(F("MAC: "));
        display.println(WiFi.macAddress());
        display.setCursor(0, 50);
        display.println(F("Initializing..."));
        display.display();

        delay(2000);
    }

    void OLEDDisplay::showStatus(const char* status) {
        if (!initialized) return;

        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 28);
        display.println(status);
        display.display();
    }

    void OLEDDisplay::showError(const char* error) {
        if (!initialized) return;

        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println(F("ERROR:"));
        display.setCursor(0, 15);
        display.println(error);
        display.display();
    }

    void OLEDDisplay::nextPage() {
        currentPage = (currentPage + 1) % MAX_PAGES;
    }

    void OLEDDisplay::setBrightness(uint8_t brightness) {
        if (!initialized) return;
        display.ssd1306_command(SSD1306_SETCONTRAST);
        display.ssd1306_command(brightness);
    }

    void OLEDDisplay::setHostname(const char* name) {
        if (name != nullptr) {
            strncpy(hostname, name, sizeof(hostname) - 1);
            hostname[sizeof(hostname) - 1] = '\0';
        }
    }

    void OLEDDisplay::setSensorManager(Sensors::SensorManager* manager) {
        sensorManager = manager;
    }


    void OLEDDisplay::drawFooter() {
        display.drawLine(0, 54, SCREEN_WIDTH, 54, SSD1306_WHITE);
        display.setTextSize(1);

        // Show hostname on left side
        display.setCursor(0, 56);
        display.print(hostname);

        // Show IP address on right side if connected
        if (WiFi.status() == WL_CONNECTED) {
            String ipStr = WiFi.localIP().toString();
            int ipWidth = ipStr.length() * 6; // Approximate character width
            display.setCursor(SCREEN_WIDTH - ipWidth, 56);
            display.print(ipStr);
        }
    }

    void OLEDDisplay::drawWiFiStatus() {
        display.setTextSize(1);
        display.setCursor(0, 5);
        display.print(F("WiFi: "));
        if (WiFi.status() == WL_CONNECTED) {
            display.println(F("Connected"));
            display.setCursor(0, 20);
            display.print(F("IP: "));
            display.println(WiFi.localIP());
            display.setCursor(0, 35);
            display.print(F("RSSI: "));
            display.print(WiFi.RSSI());
            display.println(F(" dBm"));
        } else {
            display.println(F("Disconnected"));
            display.setCursor(0, 20);
            display.println(F("Connecting..."));
        }
    }

    void OLEDDisplay::drawLoRaStatus() {
        display.setTextSize(1);
        display.setCursor(0, 5);
        display.println(F("LoRa Status:"));
        display.setCursor(0, 20);
        display.println(F("Freq: 915 MHz"));
        display.setCursor(0, 35);
        display.println(F("Ready to TX/RX"));
    }

    void OLEDDisplay::drawSensorData() {
        if (!sensorManager) {
            display.setTextSize(1);
            display.setCursor(0, 20);
            display.println(F("No sensor manager"));
            return;
        }

        auto ds18b20 = sensorManager->getDS18B20Sensor();
        auto activeSensor = sensorManager->getSHTSensor();

        // Check if DS18B20 is enabled and available
        bool useDS18B20 = (ds18b20 && ds18b20->isEnabled() && ds18b20->isSensorFound());

        if (useDS18B20 || (activeSensor && activeSensor->isEnabled())) {
            float temp = useDS18B20 ? ds18b20->getTemperature() : sensorManager->getCorrectedTemperature();
            float hum = (activeSensor && activeSensor->isEnabled()) ? sensorManager->getCorrectedHumidity() : 0.0f;

            // Draw temperature side (left half)
            drawThermometerIcon(5, 5);
            display.setTextSize(3);
            display.setCursor(5, 32);
            display.print(temp, 1);
            display.setTextSize(1);
            display.setCursor(5, 50);
            display.print(F("C"));

            // Show sensor type indicator
            display.setCursor(5, 16);
            if (useDS18B20) {
                display.print(F("DS18B20"));
            } else {
                display.print(F("I2C"));
            }

            // Draw humidity side (right half) if available
            if (activeSensor && activeSensor->isEnabled()) {
                drawHumidityIcon(90, 5);
                display.setTextSize(3);
                display.setCursor(90, 32);
                display.print((int)hum);
                display.setTextSize(1);
                display.setCursor(90, 50);
                display.print(F("%RH"));
            }
        } else {
            // Show sensor status
            display.setTextSize(1);
            display.setCursor(0, 5);
            display.println(F("Sensor Status:"));

            if (!activeSensor) {
                display.setCursor(0, 20);
                display.println(F("No temp/hum sensor"));
            } else {
                display.setCursor(0, 20);
                display.print(activeSensor->getName());
                display.print(F(": "));
                if (!activeSensor->isEnabled()) {
                    display.println(F("Disabled"));
                } else {
                    display.println(F("Error"));
                }
            }

            display.setCursor(0, 35);
            display.print(F("Uptime: "));
            unsigned long uptime = millis() / 1000;
            display.print(uptime / 60);
            display.print(F("m"));
        }
    }

    void OLEDDisplay::updateWiFiStatus(bool connected, const char* ip) {
        // Status is updated in real-time during draw cycle
    }

    void OLEDDisplay::updateLoRaStatus(bool connected, int rssi, float snr) {
        // Status is updated in real-time during draw cycle
    }

    void OLEDDisplay::updateSensorData(float temperature, int digitalInputs, int analogInputs) {
        // Sensor data is updated in real-time during draw cycle
    }

    void OLEDDisplay::updatePacketStats(int sent, int received, int errors) {
        // Packet stats could be displayed in footer or dedicated page
    }

    void OLEDDisplay::drawThermometerIcon(int x, int y) {
        // Larger thermometer icon 32x25 pixels
        // Draw thermometer bulb (circle at bottom)
        display.fillCircle(x + 16, y + 19, 7, SSD1306_WHITE);

        // Draw thermometer tube (rectangle)
        display.fillRect(x + 12, y + 2, 8, 17, SSD1306_WHITE);
        display.fillRect(x + 13, y + 3, 6, 15, SSD1306_BLACK);

        // Draw mercury/liquid level
        display.fillRect(x + 14, y + 12, 4, 8, SSD1306_WHITE);

        // Draw scale marks
        display.drawLine(x + 8, y + 6, x + 11, y + 6, SSD1306_WHITE);
        display.drawLine(x + 8, y + 10, x + 11, y + 10, SSD1306_WHITE);
        display.drawLine(x + 8, y + 14, x + 11, y + 14, SSD1306_WHITE);
    }

    void OLEDDisplay::drawHumidityIcon(int x, int y) {
        // Larger water drop icon 32x25 pixels
        // Draw water drop shape
        display.fillCircle(x + 16, y + 17, 8, SSD1306_WHITE);
        display.fillTriangle(x + 16, y + 2, x + 8, y + 12, x + 24, y + 12, SSD1306_WHITE);

        // Add highlight to make it look more like a water drop
        display.fillCircle(x + 13, y + 14, 3, SSD1306_BLACK);
    }

}