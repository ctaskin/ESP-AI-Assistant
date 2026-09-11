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
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"

static const esp_afe_sr_iface_t *afe;
static esp_afe_sr_data_t *afe_data;
static const esp_mn_iface_t *mn;
static model_iface_data_t *mn_data;
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
        // no wake word can match, whatever the spelling or threshold is; check
        // codec, I2S slot and gain before touching wake settings.
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
    int mn_size = mn->get_samp_chunksize(mn_data);
    int16_t *mn_buf = heap_caps_malloc(mn_size*2, MALLOC_CAP_SPIRAM);
    assert(mn_buf);
    size_t filled = 0, used = 0;
    capture_gate_t gate;
    ceko_state_t prev = CEKO_BOOT;
    for (;;) {
        afe_fetch_result_t *r = afe->fetch(afe_data);
        if (!r || r->ret_value != ESP_OK || r->data_size <= 0) continue;
        ceko_state_t state = ceko_state_get();
        if (state != prev) {
            mn->clean(mn_data); filled = 0;
            if (state == CEKO_LISTEN) {
                used = 0;
                capture_gate_init(&gate, CONFIG_CEKO_MAX_RECORD_SECONDS, CONFIG_CEKO_SILENCE_MS);
            }
            prev = state;
        }
        size_t n = r->data_size/2;
        if (state == CEKO_IDLE) {
            // Experimental command spotting: continuously reset MultiNet timeouts.
            // This is not a trained Turkish WakeNet model and needs on-device tuning.
            for (size_t pos=0; pos<n;) {
                size_t take = (size_t)mn_size-filled;
                if (take > n-pos) take = n-pos;
                memcpy(mn_buf+filled, r->data+pos, take*2);
                filled += take; pos += take;
                if (filled != (size_t)mn_size) continue;
                filled = 0;
                esp_mn_state_t result = mn->detect(mn_data, mn_buf);
                if (result == ESP_MN_STATE_DETECTED) {
                    esp_mn_results_t *hits = mn->get_results(mn_data);
                    bool hit = hits && hits->num > 0 && hits->command_id[0] >= 1 &&
                        hits->prob[0] >= CONFIG_CEKO_WAKE_CONFIDENCE / 100.0f;
                    // Report near misses too: a rejected candidate means MultiNet
                    // hears the phrase but not confidently enough, which is a
                    // different problem from hearing nothing at all.
                    if (hits && hits->num > 0)
                        ESP_LOGI("speech", "MultiNet candidate id=%d prob=%.2f threshold=%.2f -> %s",
                                 hits->command_id[0], (double)hits->prob[0],
                                 CONFIG_CEKO_WAKE_CONFIDENCE / 100.0, hit ? "wake" : "rejected");
                    mn->clean(mn_data);
                    if (hit) {
                        ceko_status_set("Dinliyorum"); ceko_state_set(CEKO_LISTEN);
                        ESP_LOGI("speech", "Wake detected; capture gate opened");
                        break; // Never send the wake frame itself.
                    }
                } else if (result == ESP_MN_STATE_TIMEOUT) mn->clean(mn_data);
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
    // The MultiNet model is English. Which spelling comes closest to Turkish
    // "ceko" cannot be derived on paper, so the setting holds a ';' separated
    // list and every entry is registered as its own command. Any of them wakes,
    // and the hit is logged so the working spelling can be kept.
    char spellings[160];
    snprintf(spellings, sizeof spellings, "%s", CONFIG_CEKO_WAKE_PHRASE);
    int added = 0;
    for (char *save = NULL, *tok = strtok_r(spellings, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
        while (*tok == ' ') ++tok;
        size_t len = strlen(tok);
        while (len && tok[len-1] == ' ') tok[--len] = 0;
        if (!len) continue;
        ESP_ERROR_CHECK(esp_mn_commands_add(++added, tok));
        ESP_LOGI("speech", "Wake spelling %d: \"%s\"", added, tok);
    }
    esp_mn_error_t *bad = esp_mn_commands_update();
    if (bad && bad->num)
        ESP_LOGE("speech", "%d wake spelling(s) rejected by MultiNet; the rest stay active", bad->num);
    if (!added || (bad && bad->num >= added)) {
        // Never block here: touch wake and capture both depend on the tasks
        // started below, and they must run even with no usable spelling.
        ESP_LOGE("speech", "No usable wake spelling; only touch can start listening");
        ceko_status_set("Uyandirma sozcugu hatasi");
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
    ESP_LOGI("speech", "Ready: model=%s, free internal=%u PSRAM=%u bytes", name,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    assert(xTaskCreatePinnedToCore(microphone_task, "mic", 4096, NULL, 7, NULL, 0) == pdPASS);
    assert(xTaskCreatePinnedToCore(recognition_task, "wake_capture", 8192, NULL, 5, NULL, 1) == pdPASS);
}
