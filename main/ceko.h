#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum { CEKO_BOOT, CEKO_IDLE, CEKO_LISTEN, CEKO_THINK, CEKO_SPEAK, CEKO_ERROR } ceko_state_t;
void ceko_state_set(ceko_state_t state);
ceko_state_t ceko_state_get(void);
void ceko_level_set(int level);
int ceko_level_get(void);
void ceko_status_set(const char *text);
void ceko_status_get(char *out, size_t size);
bool ceko_wifi_ready(void);

void board_power_init(void);
void board_audio_init(void);
esp_err_t board_audio_read(int16_t *mono, size_t count);
esp_err_t board_audio_write(const int16_t *mono, size_t count);
void face_start(void);
void speech_start(void);
void realtime_start(void);
// Ownership: one static capture buffer is loaned until state returns to IDLE.
bool realtime_submit(const int16_t *pcm16, size_t count);
