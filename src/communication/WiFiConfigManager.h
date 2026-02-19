#ifndef WIFI_CONFIG_MANAGER_H
#define WIFI_CONFIG_MANAGER_H

#include <WiFiManager.h>
#include <vector>
#include "../config/Config.h"
#include "../hardware/PinDefinitions.h"

namespace Communication {

    class WiFiConfigManager {
    private:
        WiFiManager wm;
        Configuration::ConfigManager& configManager;

        // WiFiManager parameters
        std::vector<WiFiManagerParameter*> parameters;

        bool shouldSaveConfig;
        bool testMode;

        // Callbacks
        static WiFiConfigManager* instance;

    public:
        explicit WiFiConfigManager(Configuration::ConfigManager& config);
        ~WiFiConfigManager();

        bool begin();
        void process();
        bool startConfigPortal();
        void stopConfigPortal();
        void resetSettings();
        bool isConfigPortalRunning() const;

        void setTestMode(bool enabled) { testMode = enabled; }
        bool isConnected() const;
        String getMacAddress() const;

    private:
        void setupParameters();
        void setupCallbacks();
        void cleanupParameters();

        // Static callback wrappers
        static void saveWifiCallback();
        static void saveParamCallback();
        static void configModeCallback(WiFiManager* myWiFiManager);

        // Instance methods called by static callbacks
        void handleSaveWifi();
        void handleSaveParams();
        void handleConfigMode(WiFiManager* myWiFiManager);

        String getParamValue(const String& name);
        void updateConfigFromParams();
    };

}

#endif