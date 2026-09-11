#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    size_t total, voiced, quiet;
    size_t max_samples, silence_samples, wait_samples;
} capture_gate_t;
typedef enum { CAPTURE_MORE, CAPTURE_READY, CAPTURE_EMPTY, CAPTURE_TOO_LONG } capture_result_t;
void capture_gate_init(capture_gate_t *g, unsigned max_seconds, unsigned silence_ms);
capture_result_t capture_gate_feed(capture_gate_t *g, size_t n, bool speech);
