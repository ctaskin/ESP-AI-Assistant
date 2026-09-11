#include "capture_gate.h"
void capture_gate_init(capture_gate_t *g, unsigned seconds, unsigned silence_ms) {
    *g = (capture_gate_t){ .max_samples = seconds*16000U, .silence_samples = silence_ms*16U, .wait_samples = 5*16000U };
}
capture_result_t capture_gate_feed(capture_gate_t *g, size_t n, bool speech) {
    g->total += n;
    if (speech) { g->voiced += n; g->quiet = 0; } else g->quiet += n;
    if (g->voiced >= 3200 && g->quiet >= g->silence_samples) return CAPTURE_READY;
    if (g->total >= g->max_samples) return CAPTURE_TOO_LONG;
    if (g->voiced < 3200 && g->total >= g->wait_samples) return CAPTURE_EMPTY;
    return CAPTURE_MORE;
}
