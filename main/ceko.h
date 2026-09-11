#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

// Shown on the idle face. Must match the WakeNet model selected under
// menuconfig > ESP Speech Recognition > Load Multiple Wake Words.
#define CEKO_WAKE_HINT "hi esp"

// How long a fresh capture waits for speech to start before giving up. The
// follow-up window after an answer has its own length, CEKO_FOLLOWUP_SECONDS.
#define CEKO_SPEECH_START_SECONDS 5

typedef enum { CEKO_BOOT, CEKO_IDLE, CEKO_LISTEN, CEKO_THINK, CEKO_SPEAK, CEKO_ERROR } ceko_state_t;
void ceko_state_set(ceko_state_t state);
ceko_state_t ceko_state_get(void);
// Opens a capture only from IDLE. In THINK/SPEAK the single recording buffer is
// loaned to the network task, so a capture must never be started there.
bool ceko_state_try_listen(void);
void ceko_level_set(int level);
int ceko_level_get(void);
void ceko_status_set(const char *text);
void ceko_status_get(char *out, size_t size);
bool ceko_wifi_ready(void);

void board_power_init(void);
void board_audio_init(void);
// Valid only after board_audio_init(); the touch panel shares the codec bus.
i2c_master_bus_handle_t board_i2c_bus(void);
esp_err_t board_audio_read(int16_t *mono, size_t count);
esp_err_t board_audio_write(const int16_t *mono, size_t count);
void face_start(void);
void touch_start(void);
void speech_start(void);
void realtime_start(void);
// Ownership: one static capture buffer is loaned until state returns to IDLE.
bool realtime_submit(const int16_t *pcm16, size_t count);
