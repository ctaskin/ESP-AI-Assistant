#include "ceko.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static atomic_int state = CEKO_BOOT, level;
static atomic_bool online;
static SemaphoreHandle_t status_lock;
static char status[80] = "Basliyorum";
void ceko_state_set(ceko_state_t s) { atomic_store(&state, s); }
ceko_state_t ceko_state_get(void) { return atomic_load(&state); }
void ceko_level_set(int n) { atomic_store(&level, n < 0 ? 0 : n > 100 ? 100 : n); }
int ceko_level_get(void) { return atomic_load(&level); }
bool ceko_wifi_ready(void) { return atomic_load(&online); }
void ceko_status_set(const char *s) {
    xSemaphoreTake(status_lock, portMAX_DELAY);
    snprintf(status, sizeof status, "%s", s);
    xSemaphoreGive(status_lock);
}
void ceko_status_get(char *s, size_t n) {
    xSemaphoreTake(status_lock, portMAX_DELAY);
    snprintf(s, n, "%s", status);
    xSemaphoreGive(status_lock);
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&online, false);
        esp_wifi_connect(); // driver scan/connect interval prevents a busy loop
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&online, true);
        // The first SNTP request goes out before the interface has an address
        // and then backs off for up to a minute. Ask again now that DNS works,
        // otherwise the first session waits half a minute on the clock.
        esp_sntp_restart();
    }
}
static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_config_t conf = {0};
    snprintf((char *)conf.sta.ssid, sizeof conf.sta.ssid, "%s", CONFIG_CEKO_WIFI_SSID);
    snprintf((char *)conf.sta.password, sizeof conf.sta.password, "%s", CONFIG_CEKO_WIFI_PASSWORD);
    conf.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &conf));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}
void app_main(void) {
    status_lock = xSemaphoreCreateMutex();
    assert(status_lock);
    board_power_init();
    face_start();
#ifdef CONFIG_CEKO_FACE_DEMO
    for (;;) {
        ceko_state_set(CEKO_IDLE); ceko_status_set("hey ceko"); vTaskDelay(pdMS_TO_TICKS(6000));
        ceko_state_set(CEKO_LISTEN); ceko_status_set("Dinliyorum"); vTaskDelay(pdMS_TO_TICKS(3000));
        ceko_state_set(CEKO_THINK); ceko_status_set("Dusunuyorum"); vTaskDelay(pdMS_TO_TICKS(2000));
        ceko_state_set(CEKO_SPEAK); ceko_status_set("Ceko");
        for (int i=0; i<100; ++i) { ceko_level_set((i*37)%90); vTaskDelay(pdMS_TO_TICKS(40)); }
        ceko_level_set(0);
    }
#endif
    esp_err_t err = nvs_flash_init();
    // Do not erase user NVS automatically. Report incompatible partition state.
    if (err != ESP_OK) {
        ceko_state_set(CEKO_ERROR); ceko_status_set("NVS hatasi: seri log");
        ESP_LOGE("ceko", "NVS: %s; back up settings before erase-flash", esp_err_to_name(err));
        return;
    }
    const char *provider_error = realtime_config_error();
    if (!strlen(CONFIG_CEKO_WIFI_SSID) || provider_error) {
        ceko_state_set(CEKO_ERROR); ceko_status_set(provider_error ? provider_error : "menuconfig > Ceko");
        ESP_LOGE("ceko", "Set Wi-Fi and the service API key with idf.py menuconfig: %s",
                 provider_error ? provider_error : "Wi-Fi SSID missing");
        return;
    }
    board_audio_init();
    wifi_init();
    realtime_start();
    speech_start();
    ceko_status_set("hey ceko");
    ceko_state_set(CEKO_IDLE);
}
