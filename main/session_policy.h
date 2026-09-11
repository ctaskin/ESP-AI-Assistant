#pragma once
#include <stdbool.h>
#include <stdint.h>

// Persistent realtime session lifecycle, free of any provider or RTOS detail so
// the reconnect/renew rules can be tested on the host.
typedef struct {
    uint64_t max_session_ms;    // provider hard limit for a single session
    uint64_t renew_margin_ms;   // reopen this long before that limit
    uint64_t backoff_start_ms;  // first retry delay after a failure
    uint64_t backoff_max_ms;    // retry delay ceiling
} session_policy_cfg_t;

typedef enum { SESSION_WAIT, SESSION_CONNECT, SESSION_RENEW, SESSION_KEEP } session_action_t;

typedef struct {
    session_policy_cfg_t cfg;
    uint64_t opened_ms, retry_at_ms, backoff_ms;
    unsigned failures;
    bool open;
} session_policy_t;

void session_policy_init(session_policy_t *p, const session_policy_cfg_t *cfg);
void session_policy_opened(session_policy_t *p, uint64_t now_ms);
// failed=false marks a planned close (renew); the next connect is not delayed.
void session_policy_closed(session_policy_t *p, uint64_t now_ms, bool failed);
session_action_t session_policy_poll(const session_policy_t *p, uint64_t now_ms, bool busy, bool link_ready);
unsigned session_policy_failures(const session_policy_t *p);
