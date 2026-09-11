#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    size_t total, voiced, quiet;
    size_t max_samples, silence_samples, wait_samples;
} capture_gate_t;
typedef enum { CAPTURE_MORE, CAPTURE_READY, CAPTURE_EMPTY, CAPTURE_TOO_LONG } capture_result_t;
// wait_seconds: how long to wait for speech to start before giving up. The
// first capture after a wake uses a short window; a follow-up window after an
// answer uses its own, so a conversation can continue without the wake word.
void capture_gate_init(capture_gate_t *g, unsigned max_seconds, unsigned silence_ms, unsigned wait_seconds);
capture_result_t capture_gate_feed(capture_gate_t *g, size_t n, bool speech);
