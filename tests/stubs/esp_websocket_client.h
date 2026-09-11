#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
typedef struct esp_websocket_client *esp_websocket_client_handle_t;
typedef struct {
    const char *uri, *headers;
    esp_err_t (*crt_bundle_attach)(void *conf);
    bool disable_auto_reconnect;
    int network_timeout_ms, buffer_size, task_stack, ping_interval_sec;
} esp_websocket_client_config_t;
typedef struct {
    const char *data_ptr;
    int data_len, payload_len, payload_offset, op_code;
    bool fin;
} esp_websocket_event_data_t;
enum { WEBSOCKET_EVENT_ANY = -1, WEBSOCKET_EVENT_CONNECTED, WEBSOCKET_EVENT_DISCONNECTED,
       WEBSOCKET_EVENT_DATA, WEBSOCKET_EVENT_ERROR };
esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *config);
esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t client, int event, void (*handler)(void *, esp_event_base_t, int32_t, void *), void *arg);
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client);
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client);
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client);
int esp_websocket_client_send_text(esp_websocket_client_handle_t client, const char *data, int len, TickType_t timeout);
