idf_component_register(SRCS "app_main.c" "board.c" "face.c" "speech.c" "realtime.c" "audio_math.c" "capture_gate.c" "ws_message.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_timer esp_lcd esp_driver_spi esp_driver_gpio esp_driver_i2c esp_driver_i2s mbedtls)

# Ceko yalnızca ESP32-S3 ve ESP-IDF 5.5.x ile doğrulandı (CEKO_HANDOFF.md §5).
# Yanlış hedef ya da IDF 6.x ile derleme, bağımlılıkların içinde kaynağı zor
# anlaşılan hatalara dönüşüyor; bu yüzden yapılandırma aşamasında durduruyoruz.
if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR
        "Ceko yalnızca esp32s3 hedefini destekler, mevcut hedef: '${IDF_TARGET}'. "
        "Proje kökündeki eski bir sdkconfig hedefi sabitliyor olabilir; "
        "sdkconfig.defaults yalnızca sdkconfig yokken uygulanır. Düzeltmek için: "
        "'idf.py fullclean && rm -f sdkconfig && idf.py set-target esp32s3'.")
endif()

if(DEFINED IDF_VERSION_MAJOR AND IDF_VERSION_MAJOR GREATER_EQUAL 6)
    message(FATAL_ERROR
        "Ceko ESP-IDF 5.5.x ile doğrulandı, bulunan sürüm: "
        "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}. "
        "IDF 6.x'te 'driver' bileşeni bölündüğü ve gpio başlıklarını artık "
        "aktarmadığı için esp_codec_dev 1.3.x derlenmiyor "
        "(driver/gpio.h: No such file or directory). "
        "ESP-IDF 5.5.2 ortamını etkinleştirip yeniden dene.")
endif()
