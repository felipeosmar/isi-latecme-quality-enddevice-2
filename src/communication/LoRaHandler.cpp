#include "LoRaHandler.h"

namespace Communication {

    LoRaHandler::LoRaHandler(Configuration::ConfigManager& config, Sensors::SensorManager& sensors)
        : localAddress(0x01),
          destination(0xFF),
          configManager(config),
          sensorManager(sensors),
          previousTransmission(0),
          packetCounter(0) {
    }

    bool LoRaHandler::begin() {
        Serial.println(F("Initializing LoRa..."));

        // Setup SPI
        SPI.begin(Hardware::LORA_SCK, Hardware::LORA_MISO, Hardware::LORA_MOSI, Hardware::LORA_SS);

        // Setup LoRa module
        LoRa.setPins(Hardware::LORA_SS, Hardware::LORA_RST, Hardware::LORA_DI0);

        if (!LoRa.begin(Hardware::LORA_BAND)) {
            Serial.println(F("Starting LoRa failed!"));
            return false;
        }

        // Set sync word to match gateway (0x12 is default for private networks)
        LoRa.setSyncWord(0x12);

        localAddress = configManager.getConfig().loraLocalAddress;
        Serial.print(F("LoRa initialized. Local address: 0x"));
        Serial.println(localAddress, HEX);
        Serial.println(F("LoRa SyncWord: 0x12"));

        return true;
    }

    bool LoRaHandler::sendData() {
        unsigned long currentTime = millis();
        int interval = getRandomizedInterval();

        if (currentTime - previousTransmission < interval) {
            return false;
        }

        previousTransmission = currentTime;

        // Create data packet (sensors already read in main loop)
        JsonDocument doc = createDataPacket();

        // Serialize to string
        String message;
        serializeJson(doc, message);

        // Send message
        bool success = sendMessage(message);
        printf("LoRa message %s\n", message.c_str());
        if (success) {
            packetCounter++;
            Serial.print(F("Sent packet #"));
            Serial.print(packetCounter);
            Serial.print(F(": "));
            Serial.println(message);
        }

        return success;
    }

    bool LoRaHandler::sendMessage(const String& message) {
        if (message.length() > Hardware::MAX_PACKET_LENGTH) {
            Serial.println(F("Message too long!"));
            return false;
        }

        LoRa.beginPacket();
        LoRa.print(message);  // Send only JSON payload
        LoRa.endPacket(false);  // false = non-blocking mode

        return true;
    }

    JsonDocument LoRaHandler::createDataPacket() {
        JsonDocument doc;

        // Map BOARD_MODEL to model name
        const char* modelName;
        Configuration::BoardType boardType = configManager.getConfig().getBoardType();
        switch (boardType) {
            case Configuration::BoardType::JVTECHv40_SHT20:
            case Configuration::BoardType::JVTECHv40_SHT30:
            case Configuration::BoardType::JVTECHv40_SHT40:
            case Configuration::BoardType::JVTECHv40_AM2315C:
                modelName = "ED-LoRa-D3A2";
                break;
            case Configuration::BoardType::HeltecLoRaV2_SHT20:
            case Configuration::BoardType::HeltecLoRaV2_SHT30:
            case Configuration::BoardType::HeltecLoRaV2_SHT40:
            case Configuration::BoardType::HeltecLoRaV2_AM2315C:
                modelName = "Heltec-LoraV4";
                break;
            default:
                modelName = "ESP32-LoRa";
                break;
        }

        // Add fields in specific order for compatibility
        doc["model"] = modelName;
        doc["id"] = nodeId;
        doc["loraadd"] = localAddress;
        doc["hn"] = configManager.getConfig().hostname;
        doc["ut"] = millis() / 1000; // Uptime in seconds

        // Internal chip temperature
        auto tempSensor = sensorManager.getTemperatureSensor();
        if (tempSensor && tempSensor->isEnabled()) {
            doc["t_ic"] = Configuration::roundToOneDecimal(tempSensor->getTemperature());
        } else {
            doc["t_ic"] = 0;
        }

        // System information
        doc["f_h"] = ESP.getFreeHeap();
        doc["tamper"] = false;

        // Temperature and humidity from main sensor
        doc["temp"] = sensorManager.getCorrectedTemperature();
        doc["hum"] = sensorManager.getCorrectedHumidity();

        // Note: rssi, snr, pferror, and packetSize are added by the gateway

        return doc;
    }

    int LoRaHandler::getRandomizedInterval() {
        int baseInterval = configManager.getConfig().readInterval;
        int randomOffset = random(0, 1000);
        return baseInterval + randomOffset;
    }

}