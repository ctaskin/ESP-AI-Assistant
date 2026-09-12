#include "ceko.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_check.h"
#include "es8311_codec.h"

// Waveshare S3_AMOLED_1_32 reference board, not the 1.43-inch board.
static i2s_chan_handle_t tx, rx;
static i2c_master_bus_handle_t i2c_bus;
static esp_codec_dev_handle_t codec;

void board_power_init(void) {
    gpio_config_t cfg = { .pin_bit_mask = 1ULL << 18, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_set_level(18, 1));
}

void board_audio_init(void) {
    i2c_master_bus_config_t b = {
        .i2c_port = I2C_NUM_0, .sda_io_num = 47, .scl_io_num = 48,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&b, &i2c_bus));
    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ch.dma_desc_num = 6;
    ch.dma_frame_num = 160;
    ch.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&ch, &tx, &rx));
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = 38, .bclk = 39, .ws = 41, .dout = 42, .din = 40 },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    ESP_ERROR_CHECK(i2s_channel_enable(rx));
    audio_codec_i2s_cfg_t data_cfg = { .port = I2S_NUM_0, .tx_handle = tx, .rx_handle = rx };
    audio_codec_i2c_cfg_t ctrl_cfg = { .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = i2c_bus };
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&data_cfg);
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&ctrl_cfg);
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    assert(data && ctrl && gpio);
    es8311_codec_cfg_t es = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .ctrl_if = ctrl, .gpio_if = gpio, .pa_pin = 46, .use_mclk = true,
        .hw_gain.pa_gain = 6,
    };
    const audio_codec_if_t *iface = es8311_codec_new(&es);
    assert(iface);
    esp_codec_dev_cfg_t dev = { .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, .codec_if = iface, .data_if = data };
    codec = esp_codec_dev_new(&dev);
    assert(codec);
    esp_codec_dev_sample_info_t fs = { .sample_rate = 16000, .channel = 2, .bits_per_sample = 16 };
    ESP_ERROR_CHECK(esp_codec_dev_open(codec, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(codec, CONFIG_CEKO_SPEAKER_VOLUME));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(codec, CONFIG_CEKO_MIC_GAIN));
}

i2c_master_bus_handle_t board_i2c_bus(void) { return i2c_bus; }

esp_err_t board_audio_read(int16_t *mono, size_t count) {
    int16_t stereo[320];
    while (count) {
        size_t n = count > 160 ? 160 : count;
        size_t got = 0, target = n * 4;
        while (got < target) {
            size_t bytes = 0;
            esp_err_t err = i2s_channel_read(rx, (uint8_t *)stereo + got, target - got, &bytes, 1000);
            if (err != ESP_OK || bytes == 0) return err == ESP_OK ? ESP_FAIL : err;
            got += bytes;
        }
        for (size_t i = 0; i < n; ++i) {
#ifdef CONFIG_CEKO_MIC_RIGHT_SLOT
            mono[i] = stereo[2*i + 1];
#else
            mono[i] = stereo[2*i];
#endif
        }
        mono += n; count -= n;
    }
    return ESP_OK;
}

esp_err_t board_audio_write(const int16_t *mono, size_t count) {
    int16_t stereo[320];
    while (count) {
        size_t n = count > 160 ? 160 : count;
        for (size_t i = 0; i < n; ++i) stereo[2*i] = stereo[2*i+1] = mono[i];
        size_t sent = 0, target = n * 4;
        while (sent < target) {
            size_t bytes = 0;
            esp_err_t err = i2s_channel_write(tx, (uint8_t *)stereo + sent, target - sent, &bytes, 1000);
            if (err != ESP_OK || !bytes) return err == ESP_OK ? ESP_FAIL : err;
            sent += bytes;
        }
        mono += n; count -= n;
    }
    return ESP_OK;
}
