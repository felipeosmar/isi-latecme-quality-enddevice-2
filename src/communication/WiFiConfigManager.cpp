#include "WiFiConfigManager.h"

namespace Communication {

    WiFiConfigManager* WiFiConfigManager::instance = nullptr;

    WiFiConfigManager::WiFiConfigManager(Configuration::ConfigManager& config)
        : configManager(config), shouldSaveConfig(false), testMode(false) {
        instance = this;
    }

    WiFiConfigManager::~WiFiConfigManager() {
        cleanupParameters();
        instance = nullptr;
    }

    bool WiFiConfigManager::begin() {
        Serial.println(F("Initializing WiFi Configuration Manager..."));

        WiFi.mode(WIFI_STA);
        WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

        wm.setDebugOutput(true, WM_DEBUG_DEV);
        wm.setDarkMode(true);
        wm.setCaptivePortalEnable(true);
        wm.setAPClientCheck(true);
        wm.setScanDispPerc(true);
        wm.setCleanConnect(true);
        wm.setBreakAfterConfig(true);
        wm.setConfigPortalBlocking(false);
        wm.setConfigPortalTimeout(Hardware::CONFIG_PORTAL_TIMEOUT);

        setupParameters();
        setupCallbacks();

        const auto& config = configManager.getConfig();
        wm.setHostname(config.hostname);

        // Setup menu
        std::vector<const char*> menu = {
            "wifi", "wifinoscan", "info", "param", "close",
            "sep", "erase", "update", "restart", "exit"
        };
        wm.setMenu(menu);

        // Try to connect
        if (!wm.autoConnect(config.hostname)) {
            Serial.println(F("Failed to connect and hit timeout"));
            return false;
        }

        if (testMode) {
            delay(1000);
            Serial.println(F("TEST MODE ENABLED - Starting config portal"));
            wm.setConfigPortalTimeout(Hardware::CONFIG_PORTAL_TIMEOUT);
            wm.startConfigPortal("EndDeviceLoRa");
        } else {
            Serial.println(F("Connected to WiFi"));
        }

        return true;
    }

    void WiFiConfigManager::process() {
        wm.process();
    }

    bool WiFiConfigManager::startConfigPortal() {
        Serial.println(F("Starting config portal..."));

        if (WiFi.status() == WL_CONNECTED) {
            // If connected to WiFi, start web server on current IP
            Serial.println(F("WiFi connected - starting config portal on current IP"));
            Serial.print(F("Config portal available at: http://"));
            Serial.println(WiFi.localIP());

            // Use the on-demand portal (doesn't create AP)
            wm.setConfigPortalTimeout(0); // No timeout when using existing connection
            wm.startWebPortal();
            return true;
        } else {
            // If not connected, create AP with config portal
            Serial.println(F("WiFi not connected - creating AP for config portal"));
            wm.setConfigPortalTimeout(Hardware::CONFIG_PORTAL_TIMEOUT);
            return wm.startConfigPortal(configManager.getConfig().hostname);
        }
    }

    void WiFiConfigManager::resetSettings() {
        Serial.println(F("Resetting WiFi settings..."));
        wm.resetSettings();
    }

    void WiFiConfigManager::stopConfigPortal() {
        Serial.println(F("Stopping config portal..."));
        wm.stopWebPortal();
    }

    bool WiFiConfigManager::isConfigPortalRunning() const {
        // Cast away const since getWebPortalActive() is not const in WiFiManager library
        return const_cast<WiFiManager&>(wm).getWebPortalActive();
    }

    bool WiFiConfigManager::isConnected() const {
        return WiFi.status() == WL_CONNECTED;
    }

    String WiFiConfigManager::getMacAddress() const {
        return WiFi.macAddress();
    }

    void WiFiConfigManager::setupParameters() {
        cleanupParameters();

        const auto& config = configManager.getConfig();

        // Create parameters
        parameters.push_back(new WiFiManagerParameter(
            "hostname", "Hostname", config.hostname, 64));

        parameters.push_back(new WiFiManagerParameter(
            "loralocaladdress", "LoRa Address", String(config.loraLocalAddress).c_str(), 3));

        parameters.push_back(new WiFiManagerParameter(
            "readinterval", "Read Interval (ms)", String(config.readInterval).c_str(), 6));

        // Sensor checkboxes
        parameters.push_back(new WiFiManagerParameter(
            "temp_hum", "Temperature/Humidity", "1", 2,
            config.tempHumEnabled ? "type=\"checkbox\" checked" : "type=\"checkbox\"",
            WFM_LABEL_AFTER));

        parameters.push_back(new WiFiManagerParameter(
            "ds18b20", "DS18B20 Temperature", "1", 2,
            config.ds18b20Enabled ? "type=\"checkbox\" checked" : "type=\"checkbox\"",
            WFM_LABEL_AFTER));

        parameters.push_back(new WiFiManagerParameter(
            "acc", "Accelerometer", "1", 2,
            config.accEnabled ? "type=\"checkbox\" checked" : "type=\"checkbox\"",
            WFM_LABEL_AFTER));

        parameters.push_back(new WiFiManagerParameter(
            "acc_main_axis", "Acc Main Axis", config.accMainAxis, 2));

        parameters.push_back(new WiFiManagerParameter(
            "mag", "Magnetometer", "1", 2,
            config.magEnabled ? "type=\"checkbox\" checked" : "type=\"checkbox\"",
            WFM_LABEL_AFTER));

        parameters.push_back(new WiFiManagerParameter(
            "gyro", "Gyroscope", "1", 2,
            config.gyroEnabled ? "type=\"checkbox\" checked" : "type=\"checkbox\"",
            WFM_LABEL_AFTER));

        parameters.push_back(new WiFiManagerParameter(
            "d1", "Digital Input 1", "1", 2,
            config.digitalPin1Enabled ? "type=\"checkbox\" checked" : "type=\"checkbox\"",
            WFM_LABEL_AFTER));

        parameters.push_back(new WiFiManagerParameter(
            "d2", "Digital Input 2", "1", 2,
            config.digitalPin2Enabled ? "type=\"checkbox\" checked" : "type=\"checkbox\"",
            WFM_LABEL_AFTER));

        parameters.push_back(new WiFiManagerParameter(
            "d3", "Digital Input 3", "1", 2,
            config.digitalPin3Enabled ? "type=\"checkbox\" checked" : "type=\"checkbox\"",
            WFM_LABEL_AFTER));

        parameters.push_back(new WiFiManagerParameter(
            "a1", "Analog Input 1", "1", 2,
            config.analogPin1Enabled ? "type=\"checkbox\" checked" : "type=\"checkbox\"",
            WFM_LABEL_AFTER));

        parameters.push_back(new WiFiManagerParameter(
            "a2", "Analog Input 2", "1", 2,
            config.analogPin2Enabled ? "type=\"checkbox\" checked" : "type=\"checkbox\"",
            WFM_LABEL_AFTER));

        // Sensor correction parameters
        parameters.push_back(new WiFiManagerParameter(
            "temperature_correction", "<br><br>Temperature Correction (°C)",
            String(config.temperatureCorrection, 2).c_str(), 8,
            "type=\"number\" step=\"0.1\" min=\"-50\" max=\"50\""));

        parameters.push_back(new WiFiManagerParameter(
            "humidity_correction", "Humidity Correction (%)",
            String(config.humidityCorrection, 2).c_str(), 8,
            "type=\"number\" step=\"0.1\" min=\"-50\" max=\"50\""));

        // Add all parameters to WiFiManager
        for (auto param : parameters) {
            wm.addParameter(param);
        }
    }

    void WiFiConfigManager::setupCallbacks() {
        wm.setAPCallback(configModeCallback);
        wm.setSaveConfigCallback(saveWifiCallback);
        wm.setSaveParamsCallback(saveParamCallback);
    }

    void WiFiConfigManager::cleanupParameters() {
        for (auto param : parameters) {
            delete param;
        }
        parameters.clear();
    }

    // Static callback implementations
    void WiFiConfigManager::saveWifiCallback() {
        if (instance) {
            instance->handleSaveWifi();
        }
    }

    void WiFiConfigManager::saveParamCallback() {
        if (instance) {
            instance->handleSaveParams();
        }
    }

    void WiFiConfigManager::configModeCallback(WiFiManager* myWiFiManager) {
        if (instance) {
            instance->handleConfigMode(myWiFiManager);
        }
    }

    // Instance callback handlers
    void WiFiConfigManager::handleSaveWifi() {
        Serial.println(F("[CALLBACK] WiFi save callback fired"));
        shouldSaveConfig = true;
    }

    void WiFiConfigManager::handleSaveParams() {
        Serial.println(F("[CALLBACK] Parameter save callback fired"));
        updateConfigFromParams();
        configManager.saveConfiguration();
        wm.reboot();
    }

    void WiFiConfigManager::handleConfigMode(WiFiManager* myWiFiManager) {
        Serial.println(F("[CALLBACK] Config mode callback fired"));
        Serial.print(F("Config portal SSID: "));
        Serial.println(myWiFiManager->getConfigPortalSSID());
    }

    String WiFiConfigManager::getParamValue(const String& name) {
        String value;
        if (wm.server && wm.server->hasArg(name)) {
            value = wm.server->arg(name);
        }
        return value;
    }

    void WiFiConfigManager::updateConfigFromParams() {
        auto& config = configManager.getConfig();

        String hostname = getParamValue("hostname");
        if (!hostname.isEmpty()) {
            strlcpy(config.hostname, hostname.c_str(), sizeof(config.hostname));
        }

        String loraAddr = getParamValue("loralocaladdress");
        if (!loraAddr.isEmpty()) {
            config.loraLocalAddress = loraAddr.toInt();
        }

        String interval = getParamValue("readinterval");
        if (!interval.isEmpty()) {
            config.readInterval = interval.toInt();
        }

        config.tempHumEnabled = !getParamValue("temp_hum").isEmpty();
        config.ds18b20Enabled = !getParamValue("ds18b20").isEmpty();
        config.accEnabled = !getParamValue("acc").isEmpty();

        String axis = getParamValue("acc_main_axis");
        if (!axis.isEmpty()) {
            strlcpy(config.accMainAxis, axis.c_str(), sizeof(config.accMainAxis));
        }

        config.magEnabled = !getParamValue("mag").isEmpty();
        config.gyroEnabled = !getParamValue("gyro").isEmpty();
        config.digitalPin1Enabled = !getParamValue("d1").isEmpty();
        config.digitalPin2Enabled = !getParamValue("d2").isEmpty();
        config.digitalPin3Enabled = !getParamValue("d3").isEmpty();
        config.analogPin1Enabled = !getParamValue("a1").isEmpty();
        config.analogPin2Enabled = !getParamValue("a2").isEmpty();

        // Sensor correction values
        String tempCorrection = getParamValue("temperature_correction");
        if (!tempCorrection.isEmpty()) {
            config.temperatureCorrection = tempCorrection.toFloat();
        }

        String humCorrection = getParamValue("humidity_correction");
        if (!humCorrection.isEmpty()) {
            config.humidityCorrection = humCorrection.toFloat();
        }
    }

}