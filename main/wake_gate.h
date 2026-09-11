#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Decides when the command recognizer is allowed to run. MultiNet7 cannot keep
// up when it is fed every frame: it starves the idle task and the AFE feed ring
// overflows. It therefore runs only while the local VAD hears speech, and a
// short pre-roll is replayed first so the beginning of the phrase is not lost.
typedef enum { WAKE_SKIP, WAKE_FLUSH, WAKE_FEED, WAKE_STOP } wake_action_t;

typedef struct {
    int16_t *pcm;               // caller owned pre-roll storage
    size_t cap, fill;
    size_t hangover_samples, quiet;
    bool running;
} wake_gate_t;

void wake_gate_init(wake_gate_t *g, int16_t *storage, size_t cap, size_t hangover_samples);
void wake_gate_reset(wake_gate_t *g);
// WAKE_SKIP: nothing to recognize. WAKE_FLUSH: feed pcm[0..fill) first, then this
// chunk. WAKE_FEED: feed this chunk. WAKE_STOP: speech ended, reset the model.
wake_action_t wake_gate_step(wake_gate_t *g, const int16_t *pcm, size_t n, bool speech);
void wake_gate_consumed(wake_gate_t *g);    // call after feeding the pre-roll
