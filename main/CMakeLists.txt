set(srcs
    "main.c"
    "lorawan/lorawan_handler.cpp"
    "sensors/sensor_manager.c"
    "sensors/sht20_driver.c"
    "sensors/sht3x_driver.c"
    "sensors/am2315c_driver.c"
    "payload/cayenne_lpp.c"
    "interface/buzzer.c"
    "interface/status_led.c"
    "config/config_manager.c"
    "health/health_monitor.c"
    "logs/log_buffer.c"
    "alarm/alarm_manager.c"
    "interface/button_handler.c"
)

if(CONFIG_THERMOCOUPLE_ENABLED)
    list(APPEND srcs "sensors/max6675_driver.c")
endif()

if(CONFIG_OLED_ENABLED)
    list(APPEND srcs "display/oled_display.c")
endif()

if(CONFIG_WIFI_ENABLED)
    list(APPEND srcs
        "wifi/wifi_manager.c"
        "webserver/web_server.c"
        "webserver/handlers/auth.c"
        "webserver/handlers/static_files.c"
        "webserver/handlers/api_files.c"
        "webserver/handlers/api_system.c"
        "webserver/handlers/api_wifi.c"
        "webserver/handlers/api_lorawan.c"
        "webserver/handlers/api_sensors.c"
        "webserver/handlers/api_ota.c"
        "update/auto_updater.c"
        "clock_sync/clock_sync.c"
    )
endif()

set(requires
    driver
    nvs_flash
    json
    mbedtls
    esp_timer
    freertos
)

if(CONFIG_WIFI_ENABLED)
    list(APPEND requires
        esp_wifi
        esp_http_server
        app_update
        esp_http_client
    )
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
        "."
        "lorawan"
        "sensors"
        "payload"
        "display"
        "wifi"
        "webserver"
        "webserver/handlers"
        "config"
        "health"
        "logs"
        "interface"
        "clock_sync"
        "alarm"
        "update"
    REQUIRES ${requires}
)
