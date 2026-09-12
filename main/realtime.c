#include "ceko.h"
#include "audio_math.h"
#include "history.h"
#include "rt_proto.h"
#include "session_policy.h"
#include "ws_message.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "mbedtls/base64.h"
#include "cJSON.h"

#define TAG "realtime"
#define EV_CONNECTED BIT0
#define EV_READY BIT1
#define EV_TURN_DONE BIT2
#define EV_TURN_FAIL BIT3
#define EV_LINK_DOWN BIT4
#define AUDIO_BLOCK 320
#define SEND_SAMPLES 1600          // 100 ms of captured 16 kHz audio per message
#define SEND_PCM_CAP 2400          // same 100 ms after resampling to 24 kHz
#define SEND_B64_CAP 6401
#define CONNECT_TIMEOUT_MS 15000
#define SETUP_TIMEOUT_MS 10000
#define RESPONSE_TIMEOUT_MS 90000
#define STALL_TIMEOUT_MS 20000
// A server side search holds the turn silent while it runs, and the service
// sends nothing at all in the meantime. Giving up at 20 s threw away answers
// that arrived seconds later, already paid for.
#define TOOL_RESPONSE_TIMEOUT_MS 180000
#define TOOL_STALL_TIMEOUT_MS 60000
#if CONFIG_CEKO_WEB_SEARCH
#define QUIET_TIMEOUT_MS 45000
#else
#define QUIET_TIMEOUT_MS STALL_TIMEOUT_MS
#endif

typedef struct { const int16_t *pcm; size_t n; } utterance_t;
typedef struct { int16_t *pcm; size_t n; } playback_t;

static const rt_provider_t *prov;
static QueueHandle_t requests, audio_queue;
static SemaphoreHandle_t played;
static EventGroupHandle_t events;
static atomic_bool abort_audio, turn_active, closing, tool_active;
static atomic_uint_fast32_t last_activity_ms;
static ws_message_t message;
static audio_resampler_t downsampler;
static size_t received_samples;
static esp_websocket_client_handle_t ws;
static char *auth_headers;
static history_t history;
static char resume_handle[512];
static session_policy_t policy;

static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time()/1000); }
static bool link_ready(void) { return ceko_wifi_ready() && time(NULL) > 1700000000; }

static void fail_turn(const char *status, const char *code) {
    ESP_LOGE(TAG,"%s (%s)",status,code?code:"-");
    ceko_status_set(status); atomic_store(&abort_audio,true);
    xEventGroupSetBits(events,EV_TURN_FAIL);
}
// Separate the failure classes: a rejected key, a missing model and a quota
// stop all arrive as an HTTP status on the upgrade handshake, while a
// certificate problem never reaches HTTP at all.
static const char *connection_error_text(const esp_websocket_error_codes_t *e) {
    ESP_LOGE(TAG,
        "WebSocket error: type=%d http_status=%d tls_esp_err=%s tls_stack_err=%d tls_cert_flags=0x%08x sock_errno=%d",
        (int)e->error_type, e->esp_ws_handshake_status_code,
        esp_err_to_name(e->esp_tls_last_esp_err), e->esp_tls_stack_err,
        (unsigned)e->esp_tls_cert_verify_flags, e->esp_transport_sock_errno);
    switch (e->esp_ws_handshake_status_code) {
    case 401: return "API anahtari reddedildi (401)";
    case 403: return "Erisim yok (403)";
    case 404: return "Model bulunamadi (404)";
    case 429: return "Kota veya hiz siniri (429)";
    default: break;
    }
    // Either the root is absent from the certificate bundle or something on the
    // network is terminating TLS with a certificate of its own.
    if (e->esp_tls_stack_err || e->esp_tls_cert_verify_flags) return "TLS: sertifika dogrulanamadi";
    return "Baglanti hatasi";
}
static void link_lost(const char *why) {
    if (atomic_load(&closing)) return;
    if (atomic_load(&turn_active)) fail_turn(why,NULL); else ESP_LOGW(TAG,"%s",why);
    xEventGroupSetBits(events,EV_LINK_DOWN);
}
static bool queue_audio(const int16_t *p,size_t n) {
    while (n) {
        size_t take=n>AUDIO_BLOCK?AUDIO_BLOCK:n;
        playback_t item={.pcm=heap_caps_malloc(take*2,MALLOC_CAP_SPIRAM),.n=take};
        if (!item.pcm) return false;
        memcpy(item.pcm,p,take*2);
        if (xQueueSend(audio_queue,&item,pdMS_TO_TICKS(2000))!=pdTRUE) { free(item.pcm); return false; }
        p+=take; n-=take;
    }
    return true;
}
static void playback_task(void *arg) {
    playback_t item;
    for (;;) {
        if (xQueueReceive(audio_queue,&item,pdMS_TO_TICKS(40))!=pdTRUE) { ceko_level_set(0); continue; }
        if (!item.pcm) {
            // Fill DMA with zeros so old samples cannot repeat on underrun.
            int16_t silence[160]={0};
            for (int i=0;i<8;++i) board_audio_write(silence,160);
            ceko_level_set(0); xSemaphoreGive(played); continue;
        }
        if (!atomic_load(&abort_audio)) {
            ceko_state_set(CEKO_SPEAK); ceko_status_set("Ceko");
            ceko_level_set(audio_level(item.pcm,item.n));
            if (board_audio_write(item.pcm,item.n)!=ESP_OK) fail_turn("Hoparlor hatasi",NULL);
        }
        free(item.pcm);
    }
}

// ---- sink: called from the WebSocket event task -------------------------------
// History is only read by the network task while no session is open, so these
// callbacks never race with recap building.
static void on_ready(void) { xEventGroupSetBits(events,EV_READY); }
static void on_turn_done(void) { xEventGroupSetBits(events,EV_TURN_DONE); }
static void on_user_text(const char *text) { history_add_user(&history,text); }
static void on_reply_text(const char *text) { history_add_reply(&history,text); }
static void on_usage(int total_tokens) { ESP_LOGI(TAG,"usage total_tokens=%d",total_tokens); }
static void on_tool_activity(void) {
    if (atomic_exchange(&tool_active,true)) return;
    ESP_LOGI(TAG,"tool running; waiting longer for this answer");
    // A search can hold the answer for half a minute; say so on the face
    // instead of leaving a silent "Dusunuyorum".
    ceko_status_set("Ariyorum");
}
static void on_resume_handle(const char *handle) {
    size_t n = strlen(handle);
    if (n >= sizeof resume_handle) { resume_handle[0]=0; ESP_LOGW(TAG,"resume handle too long"); return; }
    memcpy(resume_handle,handle,n+1);
}
static void on_audio(const char *b64) {
    if (atomic_load(&abort_audio)) return;
    size_t len=strlen(b64), cap=(len/4)*3+4, bytes=0;
    uint8_t *decoded=heap_caps_malloc(cap,MALLOC_CAP_SPIRAM);
    if (!decoded) { fail_turn("Ses bellek hatasi",NULL); return; }
    int rc=mbedtls_base64_decode(decoded,cap,&bytes,(const uint8_t *)b64,len);
    if (rc || (bytes&1)) fail_turn("Ses veri hatasi",NULL);
    else {
        // Hard ceiling bounds cost/playback and prevents an endless response.
        received_samples+=bytes/2;
        if (received_samples>(size_t)prov->recv_rate*60) fail_turn("Yanit cok uzun",NULL);
        else {
            int16_t out[686];
            for (size_t pos=0;pos<bytes/2 && !atomic_load(&abort_audio);) {
                size_t n=bytes/2-pos; if(n>1024)n=1024;
                size_t count=audio_resample(&downsampler,(int16_t *)decoded+pos,n,out,686);
                if (count==SIZE_MAX || !queue_audio(out,count)) fail_turn("Ses kuyrugu doldu",NULL);
                pos+=n;
            }
        }
    }
    free(decoded);
}
static const rt_sink_t sink = {
    .ready=on_ready, .audio=on_audio, .turn_done=on_turn_done, .failure=fail_turn,
    .user_text=on_user_text, .reply_text=on_reply_text, .resume_handle=on_resume_handle, .usage=on_usage,
    .tool_activity=on_tool_activity,
};
static void on_json(const char *json) {
    cJSON *root=cJSON_Parse(json);
    if (!root) { fail_turn("API veri hatasi",NULL); return; }
    atomic_store(&last_activity_ms,(uint32_t)(esp_timer_get_time()/1000));
    prov->handle(root,&sink);
    cJSON_Delete(root);
}
static void websocket_event(void *arg,esp_event_base_t base,int32_t id,void *event_data) {
    esp_websocket_event_data_t *e=event_data;
    if (id==WEBSOCKET_EVENT_CONNECTED) xEventGroupSetBits(events,EV_CONNECTED);
    else if (id==WEBSOCKET_EVENT_ERROR) link_lost(connection_error_text(&e->error_handle));
    else if (id==WEBSOCKET_EVENT_DISCONNECTED) link_lost("Baglanti kesildi");
    else if (id==WEBSOCKET_EVENT_DATA) {
        if (e->data_len<0 || e->payload_len<0 || e->payload_offset<0) { link_lost("WebSocket veri hatasi"); return; }
        ws_result_t result=ws_message_feed(&message,e->op_code,e->fin,e->payload_len,e->payload_offset,e->data_ptr,e->data_len);
        if (result==WS_COMPLETE) on_json(message.data);
        else if (result==WS_INVALID) link_lost("WebSocket mesaj hatasi");
    }
}

// ---- transport helpers used by the provider files -----------------------------
bool rt_send_text(void *handle,const char *text) {
    size_t n=strlen(text);
    return esp_websocket_client_send_text((esp_websocket_client_handle_t)handle,text,n,pdMS_TO_TICKS(5000))==(int)n;
}
bool rt_send_json(void *handle,cJSON *obj) {
    char *json=cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return false;
    bool ok=rt_send_text(handle,json); cJSON_free(json); return ok;
}
const rt_provider_t *rt_provider(void) {
#if CONFIG_CEKO_PROVIDER_GEMINI
    return &rt_gemini_provider;
#else
    return &rt_openai_provider;
#endif
}

// ---- session lifecycle --------------------------------------------------------
static bool wait_bits(EventBits_t want,uint32_t timeout_ms) {
    EventBits_t r=xEventGroupWaitBits(events,want|EV_LINK_DOWN|EV_TURN_FAIL,pdFALSE,pdFALSE,pdMS_TO_TICKS(timeout_ms));
    return (r&want) && !(r&(EV_LINK_DOWN|EV_TURN_FAIL));
}
static void close_session(void) {
    atomic_store(&closing,true);
    if (ws) { esp_websocket_client_stop(ws); esp_websocket_client_destroy(ws); ws=NULL; }
    if (auth_headers) { memset(auth_headers,0,strlen(auth_headers)); free(auth_headers); auth_headers=NULL; }
    ws_message_free(&message);
    xEventGroupClearBits(events,EV_CONNECTED|EV_READY|EV_LINK_DOWN|EV_TURN_DONE|EV_TURN_FAIL);
    atomic_store(&closing,false);
}
static bool open_session(void) {
    // A failed turn may have left its bits set; they must not abort the connect.
    xEventGroupClearBits(events,EV_CONNECTED|EV_READY|EV_LINK_DOWN|EV_TURN_DONE|EV_TURN_FAIL);
    const char *err=prov->config_error();
    if (err) { ESP_LOGE(TAG,"%s",err); ceko_status_set(err); return false; }
    char uri[288];
    if (!prov->uri(uri,sizeof uri)) { ESP_LOGE(TAG,"uri too long"); return false; }
    if (prov->auth_header) {
        auth_headers=prov->auth_header();
        if (!auth_headers) { ESP_LOGE(TAG,"out of memory"); return false; }
    }
    esp_websocket_client_config_t conf={ .uri=uri,.headers=auth_headers,.crt_bundle_attach=esp_crt_bundle_attach,
        .disable_auto_reconnect=true,.network_timeout_ms=10000,.buffer_size=4096,.task_stack=12288,
        .ping_interval_sec=20 };
    ws=esp_websocket_client_init(&conf);
    if (!ws) { ESP_LOGE(TAG,"client init failed"); return false; }
    if (esp_websocket_register_events(ws,WEBSOCKET_EVENT_ANY,websocket_event,NULL)!=ESP_OK ||
        esp_websocket_client_start(ws)!=ESP_OK) { ESP_LOGE(TAG,"client start failed"); return false; }
    if (!wait_bits(EV_CONNECTED,CONNECT_TIMEOUT_MS)) { ESP_LOGE(TAG,"connect timeout"); return false; }
    char *recap=heap_caps_malloc(2048,MALLOC_CAP_SPIRAM);
    if (!recap) { ESP_LOGE(TAG,"out of memory"); return false; }
    history_recap(&history,recap,2048);
    // One unsupported field rejects the whole configuration, so ask for less on
    // each attempt rather than leaving the device without a session.
    bool ready=false;
    for (unsigned level=0;level<RT_SETUP_LEVELS && !ready;++level) {
        xEventGroupClearBits(events,EV_READY|EV_TURN_FAIL);
        if (!prov->setup(ws,recap,resume_handle,level)) { ESP_LOGE(TAG,"setup send failed"); break; }
        ready=wait_bits(EV_READY,SETUP_TIMEOUT_MS);
        if (ready) {
            if (level) ESP_LOGW(TAG,"session configured without optional fields (level %u)",level);
        } else {
            if (xEventGroupGetBits(events)&EV_LINK_DOWN) break;
            ESP_LOGW(TAG,"setup rejected at level %u; retrying with fewer options",level);
        }
    }
    free(recap);
    if (!ready) { ESP_LOGE(TAG,"setup failed"); return false; }
    if (ceko_state_get()==CEKO_IDLE) ceko_status_set(CEKO_WAKE_HINT);
    ESP_LOGI(TAG,"session open (%s), free PSRAM=%u",prov->name,(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return true;
}
// Reconnects immediately, ignoring the backoff, because someone is waiting.
static bool ensure_session(void) {
    if (ws && (xEventGroupGetBits(events)&EV_READY) && !(xEventGroupGetBits(events)&EV_LINK_DOWN)) return true;
    close_session();
    session_policy_closed(&policy,now_ms(),false);
    if (!link_ready()) { fail_turn("Wi-Fi / saat hazir degil",NULL); return false; }
    if (!open_session()) { close_session(); session_policy_closed(&policy,now_ms(),true); return false; }
    session_policy_opened(&policy,now_ms());
    return true;
}
static bool send_utterance(utterance_t u) {
    bool resample=prov->send_rate!=16000;
    audio_resampler_t up;
    if (resample) audio_resampler_init(&up,16000,prov->send_rate);
    int16_t *pcm=heap_caps_malloc(SEND_PCM_CAP*2,MALLOC_CAP_SPIRAM);
    uint8_t *b64=heap_caps_malloc(SEND_B64_CAP,MALLOC_CAP_SPIRAM);
    if (!pcm || !b64) { free(pcm); free(b64); return false; }
    bool ok=prov->turn_begin(ws);
    for (size_t pos=0;pos<u.n && ok;) {
        size_t n=u.n-pos; if(n>SEND_SAMPLES)n=SEND_SAMPLES;
        const int16_t *src=u.pcm+pos; size_t samples=n, encoded=0;
        if (resample) {
            samples=audio_resample(&up,u.pcm+pos,n,pcm,SEND_PCM_CAP);
            src=pcm;
            if (samples==SIZE_MAX) { ok=false; break; }
        }
        if (mbedtls_base64_encode(b64,SEND_B64_CAP,&encoded,(const uint8_t *)src,samples*2)) { ok=false; break; }
        b64[encoded]=0;
        ok=prov->audio_chunk(ws,(char *)b64); pos+=n;
    }
    free(pcm); free(b64);
    return ok && prov->turn_end(ws);
}
static bool run_turn(utterance_t u) {
    atomic_store(&abort_audio,false); atomic_store(&tool_active,false); received_samples=0;
    xEventGroupClearBits(events,EV_TURN_DONE|EV_TURN_FAIL);
    audio_resampler_init(&downsampler,prov->recv_rate,16000);
    history_begin_turn(&history);
    atomic_store(&turn_active,true);
    bool ok=send_utterance(u);
    if (ok) {
        atomic_store(&last_activity_ms,(uint32_t)(esp_timer_get_time()/1000));
        int64_t started=esp_timer_get_time();
        while (!(xEventGroupGetBits(events)&(EV_TURN_DONE|EV_TURN_FAIL))) {
            int64_t now=esp_timer_get_time();
            bool tool=atomic_load(&tool_active);
            int64_t total=tool ? TOOL_RESPONSE_TIMEOUT_MS : RESPONSE_TIMEOUT_MS;
            uint32_t quiet=tool ? TOOL_STALL_TIMEOUT_MS : QUIET_TIMEOUT_MS;
            if (now-started>total*1000 || (uint32_t)((uint32_t)(now/1000)-atomic_load(&last_activity_ms))>quiet) {
                // Stop the service from finishing an answer nobody will hear.
                if (prov->turn_cancel) prov->turn_cancel(ws);
                fail_turn("Yanit zaman asimi", tool ? "tool" : "quiet"); break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        ok=(xEventGroupGetBits(events)&EV_TURN_DONE) && !atomic_load(&abort_audio);
        if (ok && !received_samples) { fail_turn("Sesli yanit gelmedi",NULL); ok=false; }
    } else if (!atomic_load(&abort_audio)) fail_turn("Soru gonderilemedi",NULL);
    atomic_store(&turn_active,false);
    return ok;
}
static void serve(utterance_t u) {
    bool ok=ensure_session() && run_turn(u);
    if (!ok) atomic_store(&abort_audio,true);
    playback_t end={0};
    xQueueSend(audio_queue,&end,portMAX_DELAY);
    xSemaphoreTake(played,portMAX_DELAY);
    ok = ok && !atomic_load(&abort_audio);
    if (!ok) { ceko_state_set(CEKO_ERROR); vTaskDelay(pdMS_TO_TICKS(2500)); }
    // Acoustic tail cooldown; capture task continues draining microphone locally.
    ceko_state_set(CEKO_THINK); ceko_level_set(0); vTaskDelay(pdMS_TO_TICKS(600));
#if CONFIG_CEKO_FOLLOWUP_SECONDS > 0
    if (ok) {
        // Leave a follow-up window open so a conversation does not need the wake
        // word for every turn; the capture gate closes it by itself when nothing
        // is said. The recording buffer is free again here: it was encoded and
        // sent before playback began and nothing below touches it.
        ceko_status_set("Dinliyorum"); ceko_state_set(CEKO_LISTEN);
    } else
#endif
    {
        // After a failure go straight back to idle instead of listening on.
        ceko_status_set(CEKO_WAKE_HINT); ceko_state_set(CEKO_IDLE);
    }
    // A broken link is dropped here so the idle loop can rebuild it in advance.
    if (xEventGroupGetBits(events)&EV_LINK_DOWN) {
        close_session(); session_policy_closed(&policy,now_ms(),true);
    }
}
static void network_task(void *arg) {
    utterance_t u;
    for (;;) {
        if (ws && (xEventGroupGetBits(events)&EV_LINK_DOWN)) {
            close_session(); session_policy_closed(&policy,now_ms(),true);
        }
        switch (session_policy_poll(&policy,now_ms(),false,link_ready())) {
        case SESSION_CONNECT:
            if (open_session()) session_policy_opened(&policy,now_ms());
            else { close_session(); session_policy_closed(&policy,now_ms(),true); }
            break;
        case SESSION_RENEW:
            // Rolling over while idle keeps the next question fast and, on
            // providers that support it, keeps context through the handle.
            ESP_LOGI(TAG,"renewing session");
            close_session(); session_policy_closed(&policy,now_ms(),false);
            break;
        default:
            // Silence here used to be unexplainable: say what is missing.
            if (!link_ready()) {
                static uint64_t announced;
                if (now_ms()-announced > 5000) {
                    announced=now_ms();
                    ESP_LOGI(TAG,"waiting: wifi=%s clock=%s",ceko_wifi_ready()?"ok":"no",
                             time(NULL)>1700000000?"ok":"no (NTP)");
                }
            }
            break;
        }
        if (xQueueReceive(requests,&u,pdMS_TO_TICKS(200))==pdTRUE) serve(u);
    }
}
void realtime_start(void) {
    prov=rt_provider();
    requests=xQueueCreate(1,sizeof(utterance_t));
    audio_queue=xQueueCreate(128,sizeof(playback_t));
    played=xSemaphoreCreateBinary(); events=xEventGroupCreate();
    assert(requests && audio_queue && played && events);
    history_init(&history,CONFIG_CEKO_MEMORY_TURNS);
    session_policy_cfg_t cfg={ .max_session_ms=prov->max_session_ms, .renew_margin_ms=prov->renew_margin_ms,
        .backoff_start_ms=2000, .backoff_max_ms=60000 };
    session_policy_init(&policy,&cfg);
    ESP_LOGI(TAG,"provider=%s memory_turns=%d",prov->name,CONFIG_CEKO_MEMORY_TURNS);
    assert(xTaskCreate(playback_task,"speaker",4096,NULL,6,NULL)==pdPASS);
    assert(xTaskCreate(network_task,"realtime",12288,NULL,4,NULL)==pdPASS);
}
bool realtime_submit(const int16_t *p,size_t n) {
    utterance_t u={p,n}; return xQueueSend(requests,&u,0)==pdTRUE;
}
const char *realtime_config_error(void) { return rt_provider()->config_error(); }
