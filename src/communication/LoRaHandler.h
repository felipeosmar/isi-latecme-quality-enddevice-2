#ifndef LORA_HANDLER_H
#define LORA_HANDLER_H

#include <Arduino.h>
#include <LoRa.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "../hardware/PinDefinitions.h"
#include "../config/Config.h"
#include "../sensors/SensorManager.h"

namespace Communication {

    class LoRaHandler {
    private:
        byte localAddress;
        byte destination;
        String nodeId;
        Configuration::ConfigManager& configManager;
        Sensors::SensorManager& sensorManager;

        unsigned long previousTransmission;
        int packetCounter;

    public:
        LoRaHandler(Configuration::ConfigManager& config, Sensors::SensorManager& sensors);

        bool begin();
        bool sendData();
        bool sendMessage(const String& message);

        void setLocalAddress(byte address) { localAddress = address; }
        void setDestination(byte dest) { destination = dest; }
        void setNodeId(const String& id) { nodeId = id; }

        int getPacketRssi() const { return LoRa.packetRssi(); }
        float getPacketSnr() const { return LoRa.packetSnr(); }
        int getPacketCounter() const { return packetCounter; }

        void startReceive() { LoRa.receive(); }

    private:
        JsonDocument createDataPacket();
        int getRandomizedInterval();
    };

}

#endif