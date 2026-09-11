cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ceko VERSION 0.2.0)

# Fail here with a readable reason instead of deep inside a managed component.
idf_build_get_property(ceko_target IDF_TARGET)
if(NOT ceko_target STREQUAL "esp32s3")
    message(FATAL_ERROR
        "Hedef yonga '${ceko_target}'. Bu proje yalnizca ESP32-S3 icindir. "
        "'rm -f sdkconfig && idf.py set-target esp32s3' calistir.")
endif()

# ESP-IDF 6.0 moved esp_driver_gpio and friends into the legacy "driver"
# component's PRIV_REQUIRES, so they are no longer re-exported. A managed
# component that still asks only for "driver" then fails on driver/gpio.h or
# driver/i2c_master.h. esp_codec_dev >= 1.4 selects the esp_driver_* components
# itself; this bridge covers versions and components that have not migrated.
# Remove a name from the list once its component handles IDF 6 on its own.
idf_build_get_property(ceko_idf_major IDF_VERSION_MAJOR)
if(ceko_idf_major GREATER_EQUAL 6)
    idf_build_get_property(ceko_components BUILD_COMPONENTS)
    foreach(ceko_legacy espressif__esp_codec_dev espressif__esp_lcd_sh8601)
        if("${ceko_legacy}" IN_LIST ceko_components)
            idf_component_get_property(ceko_lib ${ceko_legacy} COMPONENT_LIB)
            target_link_libraries(${ceko_lib} PRIVATE
                idf::esp_driver_gpio idf::esp_driver_i2c idf::esp_driver_i2s idf::esp_driver_spi)
        endif()
    endforeach()
endif()
