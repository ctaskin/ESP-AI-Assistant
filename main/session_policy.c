#include "session_policy.h"

static uint64_t renew_at(const session_policy_t *p) {
    uint64_t max = p->cfg.max_session_ms, margin = p->cfg.renew_margin_ms;
    if (!max) return UINT64_MAX;                 // no lifetime limit
    if (margin >= max) margin = max/2;           // keep the window usable
    return p->opened_ms + (max - margin);
}
void session_policy_init(session_policy_t *p, const session_policy_cfg_t *cfg) {
    *p = (session_policy_t){ .cfg = *cfg, .backoff_ms = cfg->backoff_start_ms };
}
void session_policy_opened(session_policy_t *p, uint64_t now_ms) {
    p->open = true; p->opened_ms = now_ms; p->retry_at_ms = now_ms;
    p->backoff_ms = p->cfg.backoff_start_ms; p->failures = 0;
}
void session_policy_closed(session_policy_t *p, uint64_t now_ms, bool failed) {
    p->open = false;
    if (!failed) { p->backoff_ms = p->cfg.backoff_start_ms; p->retry_at_ms = now_ms; p->failures = 0; return; }
    p->retry_at_ms = now_ms + p->backoff_ms;
    if (p->failures < 1000) ++p->failures;
    uint64_t next = p->backoff_ms ? p->backoff_ms*2 : p->cfg.backoff_start_ms;
    p->backoff_ms = next > p->cfg.backoff_max_ms ? p->cfg.backoff_max_ms : next;
}
session_action_t session_policy_poll(const session_policy_t *p, uint64_t now_ms, bool busy, bool link_ready) {
    if (!p->open) {
        if (!link_ready) return SESSION_WAIT;
        return now_ms >= p->retry_at_ms ? SESSION_CONNECT : SESSION_WAIT;
    }
    // Never drop a session in the middle of a question, even past its lifetime:
    // the server closes it first and the turn fails with a real error instead.
    if (busy) return SESSION_KEEP;
    if (!link_ready || now_ms >= renew_at(p)) return SESSION_RENEW;
    return SESSION_KEEP;
}
unsigned session_policy_failures(const session_policy_t *p) { return p->failures; }
