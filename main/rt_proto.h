#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

// Everything the transport in realtime.c needs to talk to one voice service.
// Two implementations exist: rt_openai.c and rt_gemini.c; the active one is
// chosen in menuconfig.
typedef struct {
    void (*ready)(void);                   // session accepted our configuration
    void (*audio)(const char *b64);        // one base64 PCM chunk of the answer
    void (*turn_done)(void);               // answer finished normally
    void (*failure)(const char *status, const char *code);  // status is shown, code only logged
    void (*user_text)(const char *text);   // transcript of the question
    void (*reply_text)(const char *text);  // transcript of the answer
    void (*resume_handle)(const char *handle);  // server side context handle, if any
    void (*usage)(int total_tokens);
} rt_sink_t;

// Setup levels: 0 asks for everything, each further level drops the optional
// fields a strict account or model may reject, so a session still comes up.
#define RT_SETUP_LEVELS 4

typedef struct {
    const char *name;
    unsigned send_rate, recv_rate;   // PCM sample rates this service expects/produces
    uint64_t max_session_ms;         // how long one session may stay open
    uint64_t renew_margin_ms;        // reopen this early, while the device is idle
    const char *(*config_error)(void);          // NULL when menuconfig values are usable
    bool (*uri)(char *out, size_t cap);
    char *(*auth_header)(void);                 // malloc'd or NULL when unused
    bool (*setup)(void *ws, const char *recap, const char *resume_handle, unsigned level);
    bool (*turn_begin)(void *ws);
    bool (*audio_chunk)(void *ws, const char *b64);
    bool (*turn_end)(void *ws);
    void (*handle)(cJSON *root, const rt_sink_t *sink);
} rt_provider_t;

extern const rt_provider_t rt_openai_provider;
extern const rt_provider_t rt_gemini_provider;
const rt_provider_t *rt_provider(void);

// Implemented by realtime.c for the provider files.
bool rt_send_text(void *ws, const char *text);
bool rt_send_json(void *ws, cJSON *obj);   // always takes ownership of obj
