set(robot_requires
    driver
    esp_event
    esp_eth
    esp_netif
    esp_wifi
    esp_http_server
    esp_timer
    esp_websocket_client
    esp_driver_jpeg
    esp_video
    json
    mbedtls
    nvs_flash
)

if(CONFIG_ROBOT_NETWORK_WAVESHARE_P4_ETH)
    list(APPEND robot_requires ip101)
endif()

if(CONFIG_ROBOT_WIFI6_KIT_ENABLE_HOSTED)
    list(APPEND robot_requires esp_wifi_remote esp_hosted)
endif()

idf_component_register(
    SRCS
        "main.c"
        "board_diagnostics.c"
        "board_i2c.c"
        "behavior_manager.c"
        "audio_codec.c"
        "camera_ov5647.c"
        "console_control.c"
        "image_sampler.c"
        "network_manager.c"
        "qwen_realtime_client.c"
        "robot_camera.c"
        "robot_tools.c"
        "safety_guard.c"
        "secret_manager.c"
        "sensor_cache.c"
        "stm32_link.c"
        "web_debug.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        ${robot_requires}
)
