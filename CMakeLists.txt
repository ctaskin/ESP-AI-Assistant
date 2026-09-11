cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ceko VERSION 0.2.0)

# Fail here with a readable reason instead of deep inside a managed component.
idf_build_get_property(ceko_idf_major IDF_VERSION_MAJOR)
idf_build_get_property(ceko_idf_minor IDF_VERSION_MINOR)
if(ceko_idf_major GREATER_EQUAL 6)
    message(FATAL_ERROR
        "Ceko ESP-IDF 5.5.x ile derlenir; bulunan surum ${ceko_idf_major}.${ceko_idf_minor}. "
        "IDF 6.0'da eski 'driver' bileseni esp_driver_gpio/esp_driver_i2c basliklarini artik "
        "disari acmiyor, bu yuzden esp_codec_dev 'driver/gpio.h' ve 'driver/i2c_master.h' "
        "bulamiyor. ESP-IDF 5.5.x kur ve 'idf.py set-target esp32s3' ile yeniden basla.")
endif()
idf_build_get_property(ceko_target IDF_TARGET)
if(NOT ceko_target STREQUAL "esp32s3")
    message(FATAL_ERROR
        "Hedef yonga '${ceko_target}'. Bu proje yalnizca ESP32-S3 icindir. "
        "'rm -f sdkconfig && idf.py set-target esp32s3' calistir.")
endif()
