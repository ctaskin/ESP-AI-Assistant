#include "ceko.h"
#include "audio_math.h"
#include "capture_gate.h"
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_models.h"
#include "model_path.h"

static const esp_afe_sr_iface_t *afe;
static esp_afe_sr_data_t *afe_data;
static int16_t *recording;

static void microphone_task(void *arg) {
    int n = afe->get_feed_chunksize(afe_data);
    int16_t *buf = heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    assert(buf);
    int peak = 0;
    int64_t next_report = 0;
    for (;;) {
        if (board_audio_read(buf, n) != ESP_OK) {
            ceko_state_set(CEKO_ERROR); ceko_status_set("Mikrofon hatasi");
            ESP_LOGE("speech", "I2S capture failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        afe->feed(afe_data, buf);
        // Bring-up diagnostic. If this stays at 0 the microphone path is dead and
        // no wake word can match; check codec, I2S slot and gain before touching
        // wake settings.
        int level = audio_level(buf, n);
        if (level > peak) peak = level;
        int64_t now = esp_timer_get_time();
        if (now >= next_report) {
            if (next_report && ceko_state_get() == CEKO_IDLE)
                ESP_LOGI("speech", "Microphone peak level over 5 s: %d/100", peak);
            peak = 0;
            next_report = now + 5000000;
        }
    }
}

static void recognition_task(void *arg) {
    const size_t capacity = CONFIG_CEKO_MAX_RECORD_SECONDS * 16000U;
    size_t used = 0;
    capture_gate_t gate;
    ceko_state_t prev = CEKO_BOOT;
    for (;;) {
        afe_fetch_result_t *r = afe->fetch(afe_data);
        if (!r || r->ret_value != ESP_OK || r->data_size <= 0) continue;
        ceko_state_t state = ceko_state_get();
        if (state != prev) {
            if (state == CEKO_IDLE) {
                // Listen for the wake word only while idle, so Ceko's own reply
                // coming back through the microphone cannot wake it again.
                afe->enable_wakenet(afe_data);
            } else {
                afe->disable_wakenet(afe_data);
                if (state == CEKO_LISTEN) {
                    used = 0;
                    capture_gate_init(&gate, CONFIG_CEKO_MAX_RECORD_SECONDS, CONFIG_CEKO_SILENCE_MS);
                }
            }
            prev = state;
        }
        size_t n = r->data_size/2;
        if (state == CEKO_IDLE) {
            if (r->wakeup_state == WAKENET_DETECTED) {
                ESP_LOGI("speech", "Wake word detected (word index %d)", r->wake_word_index);
                if (ceko_state_try_listen()) ceko_status_set("Dinliyorum");
                continue; // Never send the frame that carried the wake word.
            }
        } else if (state == CEKO_LISTEN) {
            size_t take = n < capacity-used ? n : capacity-used;
            memcpy(recording+used, r->data, take*2); used += take;
            capture_result_t result = capture_gate_feed(&gate, n, r->vad_state == VAD_SPEECH);
            if (result == CAPTURE_READY) {
                ceko_state_set(CEKO_THINK); ceko_status_set("Dusunuyorum");
                if (!realtime_submit(recording, used)) {
                    ceko_status_set(CEKO_WAKE_HINT); ceko_state_set(CEKO_IDLE);
                }
            } else if (result == CAPTURE_EMPTY || result == CAPTURE_TOO_LONG) {
                // Do not charge for silence or silently submit truncated requests.
                ESP_LOGW("speech", "Capture discarded: %s", result == CAPTURE_EMPTY ? "no speech" : "too long");
                ceko_status_set(CEKO_WAKE_HINT); ceko_state_set(CEKO_IDLE);
            }
        }
        // During THINK/SPEAK, AFE is drained but no microphone samples leave device.
    }
}

void speech_start(void) {
    srmodel_list_t *models = esp_srmodel_init("model");
    assert(models);
    // WakeNet is the engine meant to run continuously and wake the device.
    // MultiNet is a command recognizer that Espressif documents as running
    // *after* a wake word, so it is no longer used here.
    char *wake_model = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);

    afe_config_t *cfg = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    assert(cfg);
    cfg->aec_init = false; cfg->se_init = false;
    cfg->vad_init = true; cfg->vad_model_name = NULL;
    cfg->vad_min_speech_ms = 64; cfg->vad_min_noise_ms = 64;
    cfg->vad_delay_ms = 0; cfg->agc_init = false;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    if (wake_model) {
        cfg->wakenet_init = true;
        cfg->wakenet_model_name = wake_model;
        // wakenet_mode is left at the value afe_config_init picked for this
        // model; Espressif's own tests do not override it.
    } else {
        cfg->wakenet_init = false;
        ESP_LOGE("speech", "No WakeNet model in the 'model' partition; only touch can start listening");
        ESP_LOGE("speech", "Select one under menuconfig > ESP Speech Recognition > Load Multiple Wake Words");
        ceko_status_set("Uyandirma modeli yok");
    }
    afe = esp_afe_handle_from_config(cfg);
    afe_data = afe->create_from_config(cfg);
    assert(afe_data);
    afe_config_free(cfg);

#if CONFIG_CEKO_WAKE_THRESHOLD > 0
    // 0 keeps the threshold the model ships with, which is what Espressif tunes.
    if (wake_model && afe->set_wakenet_threshold(afe_data, 1, CONFIG_CEKO_WAKE_THRESHOLD / 100.0f) != 1)
        ESP_LOGW("speech", "Could not set wake threshold; model default stays in effect");
#endif

    recording = heap_caps_malloc(CONFIG_CEKO_MAX_RECORD_SECONDS*16000U*2, MALLOC_CAP_SPIRAM);
    assert(recording);
    ESP_LOGI("speech", "Ready: wake=%s, free internal=%u PSRAM=%u bytes",
             wake_model ? wake_model : "(none, touch only)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    assert(xTaskCreatePinnedToCore(microphone_task, "mic", 4096, NULL, 7, NULL, 0) == pdPASS);
    assert(xTaskCreatePinnedToCore(recognition_task, "wake_capture", 8192, NULL, 5, NULL, 1) == pdPASS);
}
