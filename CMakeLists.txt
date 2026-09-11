cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# project.cmake dahil edildiginde IDF_TARGET ve IDF_VERSION_* degiskenleri
# hazirdir; build ozellikleri de bu nokta ile project() arasinda degistirilebilir.

# Ceko yalnizca ESP32-S3 icin gecerlidir: SH8601 QSPI ekran, oktal PSRAM ve
# MultiNet7 baska bir yongada calismaz. Hedef sdkconfig'ten gelir ve sdkconfig
# varsa sdkconfig.defaults hic uygulanmaz; bu yuzden burada acikca kontrol
# ediyoruz, aksi halde hata cok sonra ve anlasilmaz bicimde ciktiyordu.
if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR
        "Ceko yalnizca esp32s3 hedefini destekler, mevcut hedef: '${IDF_TARGET}'. "
        "Proje kokundeki eski bir sdkconfig hedefi sabitliyor olabilir. Duzeltmek icin: "
        "'idf.py fullclean && rm -f sdkconfig && idf.py set-target esp32s3'.")
endif()

# Proje ESP-IDF 6.x'e tasindi: cJSON cekirdekten cikarildi ve eski "driver"
# bileseni bolundu. 5.x ile derlemek bu iki degisiklikten dolayi artik calismaz.
if(IDF_VERSION_MAJOR LESS 6)
    message(FATAL_ERROR
        "Ceko ESP-IDF 6.x bekler, bulunan surum: "
        "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}. "
        "ESP-IDF 6.1 ortamini etkinlestirip yeniden dene.")
endif()

# IDF 6 gecis serimi.
# IDF 6'da eski "driver" bileseni bolundu: artik yalnizca i2c/touch_sensor/twai
# basliklarini aktariyor ve esp_driver_gpio'yu PRIV_REQUIRES olarak tutuyor
# (components/driver/CMakeLists.txt). Hala "REQUIRES driver" yazip IDF 5
# semsiyesine guvenen ucuncu taraf bilesenler -- ornegin esp_codec_dev 1.3.x --
# bu yuzden driver/gpio.h, driver/i2s_std.h ve driver/i2c_master.h bulamiyor.
# Ayrilmis surucu bilesenlerini ortak gereksinim listesine ekleyerek basliklari
# geri veriyoruz; component.cmake bu listeden bilesenin kendisini cikardigi icin
# dongusel bagimlilik olusmaz. Tum bagimliliklar IDF 6'ya uygun surumlere
# gecince bu blok kaldirilabilir.
idf_build_set_property(__COMPONENT_REQUIRES_COMMON esp_driver_gpio APPEND)
idf_build_set_property(__COMPONENT_REQUIRES_COMMON esp_driver_i2c APPEND)
idf_build_set_property(__COMPONENT_REQUIRES_COMMON esp_driver_i2s APPEND)

project(ceko VERSION 0.1.0)
