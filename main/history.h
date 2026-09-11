#pragma once
#include <stdbool.h>
#include <stddef.h>

// Short rolling transcript kept only to rebuild context after a reconnect.
// Bounded on purpose: it lives in RAM, is never written to flash, and is
// dropped when the device restarts.
#define HISTORY_TURNS 6
#define HISTORY_TEXT 192

typedef struct { char user[HISTORY_TEXT], reply[HISTORY_TEXT]; } history_turn_t;
typedef struct { history_turn_t turn[HISTORY_TURNS]; size_t count; size_t keep; } history_t;

void history_init(history_t *h, size_t keep);       // keep is clamped to HISTORY_TURNS
void history_clear(history_t *h);
void history_begin_turn(history_t *h);              // no-op when keep == 0
void history_add_user(history_t *h, const char *text);
void history_add_reply(history_t *h, const char *text);
bool history_empty(const history_t *h);
// Writes a recap of the stored turns, always NUL terminated. Returns its length.
size_t history_recap(const history_t *h, char *out, size_t cap);
// Appends as much of src as fits, never splitting a UTF-8 sequence. Returns new length.
size_t history_append(char *dst, size_t cap, const char *src);
