cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# After project.cmake is included, IDF_TARGET and IDF_VERSION_* are set and the
# build properties can still be changed; both are needed before project().

# Ceko is an ESP32-S3 project only: the SH8601 QSPI panel, the octal PSRAM and
# the speech models do not exist elsewhere. The target comes from sdkconfig, and
# sdkconfig.defaults is not applied at all when an sdkconfig already exists, so
# check it here instead of failing much later and unreadably.
if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR
        "Ceko yalnizca esp32s3 hedefini destekler, mevcut hedef: '${IDF_TARGET}'. "
        "Proje kokundeki eski bir sdkconfig hedefi sabitliyor olabilir. Duzeltmek icin: "
        "'rm -rf build sdkconfig && idf.py set-target esp32s3'. "
        "Burada 'idf.py fullclean' kullanma: bu hata configure'u project() oncesinde "
        "durdurdugu icin build/ gecerli bir CMakeCache.txt olmadan kalir ve fullclean "
        "boyle bir dizini silmeyi reddeder.")
endif()

# The project moved to ESP-IDF 6.x: cJSON left the core and the legacy "driver"
# component was split. Building with 5.x no longer works because of those two.
if(IDF_VERSION_MAJOR LESS 6)
    message(FATAL_ERROR
        "Ceko ESP-IDF 6.x bekler, bulunan surum: "
        "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}. "
        "ESP-IDF 6.1 ortamini etkinlestirip yeniden dene.")
endif()

# IDF 6 migration shim.
# In IDF 6 the legacy "driver" component was split: it now exports only the
# i2c/touch_sensor/twai headers and keeps esp_driver_gpio in PRIV_REQUIRES
# (components/driver/CMakeLists.txt). Third party components that still write
# "REQUIRES driver" and rely on the IDF 5 umbrella - esp_codec_dev 1.3.x for
# example - therefore cannot find driver/gpio.h, driver/i2s_std.h or
# driver/i2c_master.h. Adding the split driver components to the common
# requirement list gives those headers back; component.cmake removes the
# component itself from that list, so no dependency cycle appears. This block
# can go once every dependency ships an IDF 6 compatible version.
idf_build_set_property(__COMPONENT_REQUIRES_COMMON esp_driver_gpio APPEND)
idf_build_set_property(__COMPONENT_REQUIRES_COMMON esp_driver_i2c APPEND)
idf_build_set_property(__COMPONENT_REQUIRES_COMMON esp_driver_i2s APPEND)

project(ceko VERSION 0.2.0)

# api.openai.com and generativelanguage.googleapis.com both end their chain with
# a GTS root cross-signed by the retired GlobalSign Root CA, which is no longer
# in the certificate bundle. Without this option the bundle only looks up the
# issuer of the last certificate and reports "No matching trusted root
# certificate found". sdkconfig.defaults enables it, but an existing sdkconfig
# overrides defaults, so say so loudly rather than failing at the handshake.
if(NOT CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY)
    message(WARNING
        "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY kapali. "
        "TLS el sikismasi 'No matching trusted root certificate found' ile duracak. "
        "Acmak icin: menuconfig > Component config > mbedTLS > Certificate Bundle, "
        "veya sdkconfig icindeki satiri elle =y yap.")
endif()
