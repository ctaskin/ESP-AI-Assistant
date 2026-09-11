#include "ceko.h"
#include "capture_gate.h"
#include "wake_gate.h"
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"

// 300 ms of speech before the VAD opens, and 1 s of silence before the
// recognizer is put back to sleep.
#define WAKE_PREROLL_SAMPLES 4800
#define WAKE_HANGOVER_SAMPLES 16000

static const esp_afe_sr_iface_t *afe;
static esp_afe_sr_data_t *afe_data;
static const esp_mn_iface_t *mn;
static model_iface_data_t *mn_data;
static int16_t *recording;
static wake_gate_t wake;
static int16_t *mn_buf;
static int mn_size;
static size_t mn_filled;

static void microphone_task(void *arg) {
    int n = afe->get_feed_chunksize(afe_data);
    int16_t *buf = heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    assert(buf);
    for (;;) {
        if (board_audio_read(buf, n) != ESP_OK) {
            ceko_state_set(CEKO_ERROR); ceko_status_set("Mikrofon hatasi");
            ESP_LOGE("speech", "I2S capture failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        afe->feed(afe_data, buf);
    }
}

// Feeds the recognizer in its own chunk size. Returns true on the wake phrase.
static bool mn_feed(const int16_t *pcm, size_t n) {
    for (size_t pos = 0; pos < n;) {
        size_t take = (size_t)mn_size - mn_filled;
        if (take > n-pos) take = n-pos;
        memcpy(mn_buf+mn_filled, pcm+pos, take*2);
        mn_filled += take; pos += take;
        if (mn_filled != (size_t)mn_size) continue;
        mn_filled = 0;
        esp_mn_state_t result = mn->detect(mn_data, mn_buf);
        if (result == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *hits = mn->get_results(mn_data);
            bool hit = hits && hits->num > 0 && hits->command_id[0] == 1 &&
                hits->prob[0] >= CONFIG_CEKO_WAKE_CONFIDENCE / 100.0f;
            mn->clean(mn_data);
            if (hit) return true;
        } else if (result == ESP_MN_STATE_TIMEOUT) mn->clean(mn_data);
    }
    return false;
}

static void recognition_task(void *arg) {
    const size_t capacity = CONFIG_CEKO_MAX_RECORD_SECONDS * 16000U;
    mn_size = mn->get_samp_chunksize(mn_data);
    mn_buf = heap_caps_malloc(mn_size*2, MALLOC_CAP_SPIRAM);
    assert(mn_buf);
    size_t used = 0;
    capture_gate_t gate;
    ceko_state_t prev = CEKO_BOOT;
    for (;;) {
        afe_fetch_result_t *r = afe->fetch(afe_data);
        if (!r || r->ret_value != ESP_OK || r->data_size <= 0) continue;
        ceko_state_t state = ceko_state_get();
        if (state != prev) {
            mn->clean(mn_data); mn_filled = 0; wake_gate_reset(&wake);
            if (state == CEKO_LISTEN) {
                used = 0;
                capture_gate_init(&gate, CONFIG_CEKO_MAX_RECORD_SECONDS, CONFIG_CEKO_SILENCE_MS);
            }
            prev = state;
        }
        size_t n = r->data_size/2;
        if (state == CEKO_IDLE) {
            // Experimental command spotting. This is not a trained Turkish
            // WakeNet model; it is MultiNet driven by the local VAD so it does
            // not run during silence, and it needs on-device tuning.
            bool hit = false;
            switch (wake_gate_step(&wake, r->data, n, r->vad_state == VAD_SPEECH)) {
            case WAKE_FLUSH:
                hit = mn_feed(wake.pcm, wake.fill);
                wake_gate_consumed(&wake);
                if (!hit) hit = mn_feed(r->data, n);
                break;
            case WAKE_FEED:
                hit = mn_feed(r->data, n);
                break;
            case WAKE_STOP:
                mn->clean(mn_data); mn_filled = 0;
                break;
            case WAKE_SKIP:
                break;
            }
            if (hit) {
                // Never send the wake frame itself.
                mn->clean(mn_data); mn_filled = 0; wake_gate_reset(&wake);
                ceko_status_set("Dinliyorum"); ceko_state_set(CEKO_LISTEN);
                ESP_LOGI("speech", "Wake detected; capture gate opened");
            }
        } else if (state == CEKO_LISTEN) {
            size_t take = n < capacity-used ? n : capacity-used;
            memcpy(recording+used, r->data, take*2); used += take;
            capture_result_t result = capture_gate_feed(&gate, n, r->vad_state == VAD_SPEECH);
            if (result == CAPTURE_READY) {
                ceko_state_set(CEKO_THINK); ceko_status_set("Dusunuyorum");
                if (!realtime_submit(recording, used)) {
                    ceko_status_set("hey ceko"); ceko_state_set(CEKO_IDLE);
                }
            } else if (result == CAPTURE_EMPTY || result == CAPTURE_TOO_LONG) {
                // Do not charge for silence or silently submit truncated requests.
                ESP_LOGW("speech", "Capture discarded: %s", result == CAPTURE_EMPTY ? "no speech" : "too long");
                ceko_status_set("hey ceko"); ceko_state_set(CEKO_IDLE);
            }
        }
        // During THINK/SPEAK, AFE is drained but no microphone samples leave device.
    }
}

void speech_start(void) {
    srmodel_list_t *models = esp_srmodel_init("model");
    assert(models);
    char *name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    assert(name);
    mn = esp_mn_handle_from_name(name);
    assert(mn);
    mn_data = mn->create(name, 4000);
    assert(mn_data);
    ESP_ERROR_CHECK(esp_mn_commands_alloc(mn, mn_data));
    ESP_ERROR_CHECK(esp_mn_commands_clear());
    ESP_ERROR_CHECK(esp_mn_commands_add(1, CONFIG_CEKO_WAKE_PHRASE));
    esp_mn_error_t *bad = esp_mn_commands_update();
    if (bad && bad->num) {
        ESP_LOGE("speech", "Wake spelling cannot be parsed: %s", CONFIG_CEKO_WAKE_PHRASE);
        ceko_status_set("Uyandirma sozcugu hatasi"); ceko_state_set(CEKO_ERROR);
        // Keep the error face visible rather than entering a reboot loop.
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    mn->set_det_threshold(mn_data, CONFIG_CEKO_WAKE_CONFIDENCE/100.0f);
    afe_config_t *cfg = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    assert(cfg);
    cfg->aec_init = false; cfg->se_init = false; cfg->wakenet_init = false;
    cfg->vad_init = true; cfg->vad_model_name = NULL;
    cfg->vad_min_speech_ms = 64; cfg->vad_min_noise_ms = 64;
    cfg->vad_delay_ms = 0; cfg->agc_init = false;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe = esp_afe_handle_from_config(cfg);
    afe_data = afe->create_from_config(cfg);
    assert(afe_data);
    afe_config_free(cfg);
    recording = heap_caps_malloc(CONFIG_CEKO_MAX_RECORD_SECONDS*16000U*2, MALLOC_CAP_SPIRAM);
    assert(recording);
    int16_t *preroll = heap_caps_malloc(WAKE_PREROLL_SAMPLES*2, MALLOC_CAP_SPIRAM);
    assert(preroll);
    wake_gate_init(&wake, preroll, WAKE_PREROLL_SAMPLES, WAKE_HANGOVER_SAMPLES);
    ESP_LOGI("speech", "Ready: model=%s, free internal=%u PSRAM=%u bytes", name,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    assert(xTaskCreatePinnedToCore(microphone_task, "mic", 4096, NULL, 7, NULL, 0) == pdPASS);
    assert(xTaskCreatePinnedToCore(recognition_task, "wake_capture", 8192, NULL, 5, NULL, 1) == pdPASS);
}
