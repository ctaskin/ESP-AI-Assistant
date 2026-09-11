#include "ceko.h"
#include "sdkconfig.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

// Waveshare ESP32-S3-Touch-AMOLED-1.32 reference (Example/ESP-IDF, user_config.h
// and components/lcd_touch_bsp): the touch controller shares the codec I2C bus
// (SDA 47 / SCL 48) at address 0x15, reset on GPIO 7, interrupt on GPIO 6.
// The vendor BSP polls and leaves the interrupt pin unused, so we poll too.
#define TOUCH_ADDR      0x15
#define TOUCH_RST_PIN   7
#define TOUCH_SPEED_HZ  300000
#define POLL_MS         50
#define STATUS_REG      0x02

static i2c_master_dev_handle_t dev;

// Vendor read: two bytes from 0x02, where byte 0 is the contact count and the
// top two bits of byte 1 are the event code. Event 0x01 is the lift-up report,
// which the vendor driver treats as "no longer touching".
static bool touch_pressed(void) {
    uint8_t reg = STATUS_REG, status[2] = {0};
    if (i2c_master_transmit_receive(dev, &reg, 1, status, sizeof status, pdMS_TO_TICKS(100)) != ESP_OK)
        return false;
    return status[0] != 0 && (status[1] >> 6) != 0x01;
}

static void touch_task(void *arg) {
    bool was_pressed = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        bool pressed = touch_pressed();
        // Act on the press edge only, so holding a finger down cannot restart
        // the capture over and over.
        if (pressed && !was_pressed) {
            if (ceko_state_try_listen()) {
                ceko_status_set("Dinliyorum");
                ESP_LOGI("touch", "Touch wake: capture gate opened");
            } else {
                // THINK/SPEAK own the recording buffer; a touch there is dropped
                // rather than cutting the answer short.
                ESP_LOGI("touch", "Touch ignored, state %d is busy", (int)ceko_state_get());
            }
        }
        was_pressed = pressed;
    }
}

void touch_start(void) {
    i2c_master_bus_handle_t bus = board_i2c_bus();
    if (!bus) {
        ESP_LOGE("touch", "I2C bus not ready; call board_audio_init() first");
        return;
    }

    gpio_config_t rst = { .pin_bit_mask = 1ULL << TOUCH_RST_PIN, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&rst));
    // Vendor reset sequence: high, low, high with 200 ms between edges.
    gpio_set_level(TOUCH_RST_PIN, 1); vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(TOUCH_RST_PIN, 0); vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(TOUCH_RST_PIN, 1); vTaskDelay(pdMS_TO_TICKS(200));

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_ADDR,
        .scl_speed_hz = TOUCH_SPEED_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &dev);
    if (err != ESP_OK) {
        ESP_LOGE("touch", "i2c_master_bus_add_device: %s", esp_err_to_name(err));
        return;
    }

    // Probe once at boot. Without this a wiring or address mismatch would show
    // up only as a touch that silently never wakes.
    uint8_t reg = STATUS_REG, probe[2] = {0};
    err = i2c_master_transmit_receive(dev, &reg, 1, probe, sizeof probe, pdMS_TO_TICKS(200));
    if (err == ESP_OK) {
        ESP_LOGI("touch", "Touch panel answered at 0x%02X; tap the screen to listen", TOUCH_ADDR);
    } else {
        // Keep polling anyway: the panel may simply need longer after reset.
        ESP_LOGW("touch", "Touch panel silent at 0x%02X (%s); wake by touch may not work",
                 TOUCH_ADDR, esp_err_to_name(err));
    }

    assert(xTaskCreate(touch_task, "touch", 3072, NULL, 4, NULL) == pdPASS);
}
