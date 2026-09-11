#include "wake_gate.h"
#include <string.h>

static void keep(wake_gate_t *g, const int16_t *pcm, size_t n) {
    if (!g->pcm || !g->cap) return;
    if (n >= g->cap) { memcpy(g->pcm, pcm+(n-g->cap), g->cap*2); g->fill = g->cap; return; }
    if (g->fill + n > g->cap) {
        size_t drop = g->fill + n - g->cap;
        memmove(g->pcm, g->pcm+drop, (g->fill-drop)*2);
        g->fill -= drop;
    }
    memcpy(g->pcm + g->fill, pcm, n*2);
    g->fill += n;
}
void wake_gate_init(wake_gate_t *g, int16_t *storage, size_t cap, size_t hangover_samples) {
    *g = (wake_gate_t){ .pcm = storage, .cap = cap, .hangover_samples = hangover_samples };
}
void wake_gate_reset(wake_gate_t *g) { g->fill = 0; g->quiet = 0; g->running = false; }
void wake_gate_consumed(wake_gate_t *g) { g->fill = 0; }
wake_action_t wake_gate_step(wake_gate_t *g, const int16_t *pcm, size_t n, bool speech) {
    if (!g->running) {
        if (!speech) { keep(g, pcm, n); return WAKE_SKIP; }
        g->running = true; g->quiet = 0;
        return WAKE_FLUSH;
    }
    g->quiet = speech ? 0 : g->quiet + n;
    if (g->quiet < g->hangover_samples) return WAKE_FEED;
    wake_gate_reset(g);
    return WAKE_STOP;
}
