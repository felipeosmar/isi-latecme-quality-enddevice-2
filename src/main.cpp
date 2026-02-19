#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoOTA.h>

#include "hardware/PinDefinitions.h"
#include "hardware/OLEDDisplay.h"
#include "config/Config.h"
#include "sensors/SensorManager.h"
#include "communication/LoRaHandler.h"
#include "communication/WiFiConfigManager.h"
#include "utils/Timer.h"

// Global instances
Configuration::ConfigManager configManager;
Sensors::SensorManager sensorManager(configManager);
Communication::LoRaHandler loraHandler(configManager, sensorManager);
Communication::WiFiConfigManager wifiManager(configManager);
Hardware::OLEDDisplay oledDisplay;

// Timers and controls
Utils::Timer ledTimer(Hardware::BLINK_INTERVAL);
Utils::Timer loraTimer(Hardware::LORA_INTERVAL);
Utils::Timer sensorTimer(5000); // Read sensors every 5 seconds
Utils::ButtonDebouncer button(Hardware::BUTTON_PIN);

// LED state and timing
bool ledState = false;
unsigned long ledOnTime = 0;
unsigned long ledCycleTime = Hardware::BLINK_INTERVAL;

// Function declarations
void handleButton();
void blinkLed();
void initializeSystem();
void printSystemInfo();

void setup() {
    // Initialize serial
    Serial.begin(Hardware::SERIAL_BAUD_RATE);
    Serial.setDebugOutput(true);
    Serial.println(F("\n=== LoRa End Device Starting ==="));

    // Initialize LED
    pinMode(Hardware::LED_PIN, OUTPUT);
    digitalWrite(Hardware::LED_PIN, LOW);

    // Initialize OLED display
    Serial.println(F("Initializing OLED display..."));
    if (!oledDisplay.begin()) {
        Serial.println(F("OLED display initialization failed"));
    }

    // Initialize system
    initializeSystem();

    // Set hostname and sensor manager to OLED display
    oledDisplay.setHostname(configManager.getConfig().hostname);
    oledDisplay.setSensorManager(&sensorManager);

    // Print system information
    printSystemInfo();

    // Initialize OTA if enabled
    #ifdef USEOTA
        ArduinoOTA.begin();
        Serial.println(F("OTA enabled"));
    #endif

    Serial.println(F("=== Setup Complete ===\n"));
}

void loop() {
    // Feed watchdog to prevent timeout
    yield();

    // Handle OTA updates
    #ifdef USEOTA
        ArduinoOTA.handle();
    #endif

    // Process WiFi manager
    wifiManager.process();

    // Read sensors periodically
    if (sensorTimer.isReady()) {
        Serial.println(F("Reading sensors..."));
        sensorManager.readAll();
        yield();  // Feed watchdog after sensor reading
    }

    // Update OLED display
    oledDisplay.update();

    // Blink status LED
    blinkLed();

    // Send LoRa data
    if (loraTimer.isReady()) {
        Serial.println(F("Sending LoRa data..."));
        loraHandler.sendData();
        loraHandler.startReceive();
        yield();  // Feed watchdog after LoRa transmission
    }

    // Handle button presses
    handleButton();

    // Small delay to prevent tight loop
    delay(10);
}

void initializeSystem() {
    Serial.println(F("Initializing file system..."));
    if (!SPIFFS.begin(true)) {
        Serial.println(F("SPIFFS Mount Failed!"));
        ESP.restart();
    }

    Serial.println(F("Loading configuration..."));
    if (!configManager.loadConfiguration()) {
        Serial.println(F("Failed to load configuration, using defaults"));
        configManager.setDefaults();
        configManager.saveConfiguration();
    }

    // Print loaded configuration
    configManager.printConfig();

    // Update timer intervals from config
    loraTimer.setInterval(configManager.getConfig().readInterval);

    Serial.println(F("Initializing sensor system..."));
    if (!sensorManager.begin()) {
        Serial.println(F("Warning: Sensor system failed to initialize"));
    }

    if (!sensorManager.initializeAll()) {
        Serial.println(F("Warning: Some sensors failed to initialize"));
    }

    Serial.println(F("Initializing LoRa..."));
    if (!loraHandler.begin()) {
        Serial.println(F("LoRa initialization failed!"));
        // Continue anyway - device can still work via WiFi
    }

    // Set LoRa node ID to MAC address
    loraHandler.setNodeId(WiFi.macAddress());

    Serial.println(F("Initializing WiFi..."));
    if (!wifiManager.begin()) {
        Serial.println(F("WiFi initialization failed"));
    }
}

void handleButton() {
    button.update();

    if (button.wasPressed()) {
        Serial.println(F("Button pressed"));
    }

    if (button.wasReleased()) {
        unsigned long duration = button.getPressedDuration();
        Serial.print(F("Button released after "));
        Serial.print(duration);
        Serial.println(F(" ms"));

        if (duration >= Hardware::BUTTON_RESET_DURATION) {
            Serial.println(F("=== FACTORY RESET ==="));
            SPIFFS.format();
            wifiManager.resetSettings();
            delay(1000);
            ESP.restart();
        } else if (duration >= Hardware::BUTTON_RESTART_DURATION) {
            Serial.println(F("=== RESTARTING DEVICE ==="));
            delay(1000);
            ESP.restart();
        } else if (duration >= Hardware::BUTTON_CONFIG_DURATION) {
            Serial.println(F("=== OPENING CONFIG PORTAL ==="));

            if (wifiManager.isConfigPortalRunning()) {
                // If portal is running, stop it
                Serial.println(F("Stopping config portal"));
                wifiManager.stopConfigPortal();
                oledDisplay.showStatus("Config portal stopped");
                delay(2000);
            } else {
                // Start config portal
                if (WiFi.status() == WL_CONNECTED) {
                    // Show IP address on OLED
                    String msg = "Config: http://" + WiFi.localIP().toString();
                    oledDisplay.showStatus(msg.c_str());
                } else {
                    oledDisplay.showStatus("Config AP: EndDevice");
                }
                wifiManager.startConfigPortal();
            }
        } else {
            // Short press - cycle through OLED pages
            Serial.println(F("Cycling OLED page"));
            oledDisplay.nextPage();
        }
    }
}

void blinkLed() {
    unsigned long currentTime = millis();
    unsigned long elapsed = (currentTime - ledOnTime) % ledCycleTime;

    // LED on for 10% of cycle time (100ms out of 1000ms)
    bool shouldBeOn = elapsed < (ledCycleTime / 10);

    if (shouldBeOn != ledState) {
        ledState = shouldBeOn;
        digitalWrite(Hardware::LED_PIN, ledState);

        if (shouldBeOn) {
            ledOnTime = currentTime;
        }
    }
}

void printSystemInfo() {
    Serial.println(F("\n=== System Information ==="));
    Serial.print(F("Chip ID: "));
    Serial.println(ESP.getChipModel());
    Serial.print(F("MAC Address: "));
    Serial.println(WiFi.macAddress());
    Serial.print(F("Free Heap: "));
    Serial.println(ESP.getFreeHeap());
    Serial.print(F("Flash Size: "));
    Serial.println(ESP.getFlashChipSize());
    Serial.println(F("==========================\n"));
}