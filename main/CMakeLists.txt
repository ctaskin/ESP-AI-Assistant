# lwip: app_main.c dogrudan esp_sntp.h iceriyor. IDF 6'da esp_netif artik lwip'i
# genel (REQUIRES) degil ozel (PRIV_REQUIRES esp_netif_stack) bagimlilik olarak
# tuttugu icin baslik kendiliginden gelmiyor; acikca istiyoruz.
idf_component_register(SRCS "app_main.c" "board.c" "face.c" "speech.c" "realtime.c" "audio_math.c" "capture_gate.c" "ws_message.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif lwip nvs_flash esp_timer esp_lcd esp_driver_spi esp_driver_gpio esp_driver_i2c esp_driver_i2s mbedtls)
