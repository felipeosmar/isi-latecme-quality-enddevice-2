#ifndef PIN_DEFINITIONS_H
#define PIN_DEFINITIONS_H

namespace Hardware {
    // LoRa SPI pins - Configured via PlatformIO environment
    #ifdef LORA_SCK_PIN
        constexpr int LORA_SCK = LORA_SCK_PIN;
    #else
        constexpr int LORA_SCK = 5;   // Default GPIO5  -- SX1278's SCK
    #endif

    #ifdef LORA_MISO_PIN
        constexpr int LORA_MISO = LORA_MISO_PIN;
    #else
        constexpr int LORA_MISO = 19; // Default GPIO19 -- SX1278's MISO
    #endif

    #ifdef LORA_MOSI_PIN
        constexpr int LORA_MOSI = LORA_MOSI_PIN;
    #else
        constexpr int LORA_MOSI = 27; // Default GPIO27 -- SX1278's MOSI
    #endif

    #ifdef LORA_SS_PIN
        constexpr int LORA_SS = LORA_SS_PIN;
    #else
        constexpr int LORA_SS = 18;   // Default GPIO18 -- SX1278's CS
    #endif

    #ifdef LORA_RST_PIN
        constexpr int LORA_RST = LORA_RST_PIN;
    #else
        constexpr int LORA_RST = 14;  // Default GPIO14 -- SX1278's RESET
    #endif

    #ifdef LORA_DI0_PIN
        constexpr int LORA_DI0 = LORA_DI0_PIN;
    #else
        constexpr int LORA_DI0 = 26;  // Default GPIO26 -- SX1278's IRQ(Interrupt Request)
    #endif

    // LoRa configuration
    constexpr long LORA_BAND = 915E6;
    constexpr int MAX_PACKET_LENGTH = 255;

    // Control pins - Configured via PlatformIO environment
    #ifdef BUTTON_PIN
        constexpr int BUTTON_PIN_VALUE = BUTTON_PIN;
        #undef BUTTON_PIN
        constexpr int BUTTON_PIN = BUTTON_PIN_VALUE;
    #else
        constexpr int BUTTON_PIN = 0;  // Default GPIO0
    #endif

    #ifdef LED_PIN
        constexpr int LED_PIN_VALUE = LED_PIN;
        #undef LED_PIN
        constexpr int LED_PIN = LED_PIN_VALUE;
    #else
        constexpr int LED_PIN = 25;    // Default GPIO25
    #endif

    // Display OLED I2C pins - Configured via PlatformIO environment
    #ifdef OLED_SDA_PIN
        constexpr int OLED_SDA = OLED_SDA_PIN;
    #else
        constexpr int OLED_SDA = 4;  // Default GPIO4 -- SDA
    #endif

    #ifdef OLED_SCL_PIN
        constexpr int OLED_SCL = OLED_SCL_PIN;
    #else
        constexpr int OLED_SCL = 15; // Default GPIO15 -- SCL
    #endif

    #ifdef OLED_RST_PIN
        constexpr int OLED_RST = OLED_RST_PIN;
    #else
        constexpr int OLED_RST = 16; // Default GPIO16 -- RESET
    #endif

    #ifdef OLED_ADDRESS
        constexpr int OLED_ADDRESS_VALUE = OLED_ADDRESS;
        #undef OLED_ADDRESS
        constexpr int OLED_ADDRESS = OLED_ADDRESS_VALUE;
    #else
        constexpr int OLED_ADDRESS = 0x3C; // Default I2C Address
    #endif

    // Digital input pins
    constexpr int DIGITAL_PIN_1 = 32;
    constexpr int DIGITAL_PIN_2 = 13;
    constexpr int DIGITAL_PIN_3 = 34;

    // Analog input pins
    constexpr int ANALOG_PIN_1 = 36;
    constexpr int ANALOG_PIN_2 = 33;

    // 1-Wire DS18B20 temperature sensor pin
    constexpr int DS18B20_PIN = 23;

    // Timing intervals (ms)
    constexpr unsigned long BLINK_INTERVAL = 2000;    // LED blink interval
    constexpr unsigned long LORA_INTERVAL = 30000;    // LoRa transmission interval
    constexpr unsigned long DHT_INTERVAL = 2000;      // DHT sensor read interval

    // Button press durations (ms)
    constexpr unsigned long BUTTON_CONFIG_DURATION = 2000;  // 2 seconds for config portal
    constexpr unsigned long BUTTON_RESTART_DURATION = 5000; // 5 seconds for restart
    constexpr unsigned long BUTTON_RESET_DURATION = 7000;   // 7 seconds for factory reset

    // Serial configuration
    constexpr int SERIAL_BAUD_RATE = 115200;

    // WiFi configuration
    constexpr int CONFIG_PORTAL_TIMEOUT = 90; // seconds
}

#endif // PIN_DEFINITIONS_H