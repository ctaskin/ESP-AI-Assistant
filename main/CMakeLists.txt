set(ceko_srcs "app_main.c" "board.c" "face.c" "speech.c" "realtime.c" "audio_math.c"
    "capture_gate.c" "ws_message.c" "history.c" "session_policy.c" "wake_gate.c")
# Only the selected voice service is built into the firmware.
if(CONFIG_CEKO_PROVIDER_GEMINI)
    list(APPEND ceko_srcs "rt_gemini.c")
else()
    list(APPEND ceko_srcs "rt_openai.c")
endif()
idf_component_register(SRCS ${ceko_srcs}
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_timer esp_lcd esp_driver_spi esp_driver_gpio esp_driver_i2c esp_driver_i2s mbedtls)
