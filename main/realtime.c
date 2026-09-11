#include "ceko.h"
#include "audio_math.h"
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

#define EV_CONNECTED BIT0
#define EV_CONFIGURED BIT1
#define EV_DONE BIT2
#define EV_FAILED BIT3
#define AUDIO_BLOCK 320
typedef struct { const int16_t *pcm; size_t n; } utterance_t;
typedef struct { int16_t *pcm; size_t n; } playback_t;
static QueueHandle_t requests, audio_queue;
static SemaphoreHandle_t played;
static EventGroupHandle_t events;
static atomic_bool abort_audio;
static atomic_uint_fast32_t last_activity_ms;
static ws_message_t message;
static audio_resampler_t downsampler;
static size_t received_samples;

static void fail(const char *reason) {
    ESP_LOGE("realtime", "%s", reason);
    ceko_status_set(reason); atomic_store(&abort_audio,true);
    xEventGroupSetBits(events,EV_FAILED);
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
            if (board_audio_write(item.pcm,item.n)!=ESP_OK) fail("Hoparlor hatasi");
        }
        free(item.pcm);
    }
}
static const char *string_field(cJSON *object,const char *key) {
    cJSON *x=cJSON_GetObjectItemCaseSensitive(object,key);
    return cJSON_IsString(x)?x->valuestring:NULL;
}
static void on_json(const char *json) {
    cJSON *root=cJSON_Parse(json);
    if (!root) { fail("API veri hatasi"); return; }
    const char *type=string_field(root,"type");
    if (!type) { cJSON_Delete(root); return; }
    atomic_store(&last_activity_ms,(uint32_t)(esp_timer_get_time()/1000));
    if (!strcmp(type,"session.updated")) xEventGroupSetBits(events,EV_CONFIGURED);
    else if (!strcmp(type,"error")) {
        // Log only the error code, never a returned payload that might contain user audio.
        const char *code=string_field(cJSON_GetObjectItemCaseSensitive(root,"error"),"code");
        ESP_LOGE("realtime","OpenAI error code: %s",code?code:"unknown");
        fail("API hatasi: seri log");
    } else if (!strcmp(type,"response.output_audio.delta") || !strcmp(type,"response.audio.delta")) {
        const char *b64=string_field(root,"delta");
        if (b64 && !atomic_load(&abort_audio)) {
            size_t len=strlen(b64), cap=(len/4)*3+4, bytes=0;
            uint8_t *decoded=heap_caps_malloc(cap,MALLOC_CAP_SPIRAM);
            if (!decoded) { fail("Ses bellek hatasi"); cJSON_Delete(root); return; }
            int rc=mbedtls_base64_decode(decoded,cap,&bytes,(const uint8_t *)b64,len);
            if (rc || (bytes&1)) fail("Ses veri hatasi");
            else {
                // Hard ceiling bounds cost/playback and prevents an endless response.
                received_samples+=bytes/2;
                if (received_samples>24000U*60) fail("Yanit cok uzun");
                else {
                    int16_t out[686];
                    for (size_t pos=0;pos<bytes/2 && !atomic_load(&abort_audio);) {
                        size_t n=bytes/2-pos; if(n>1024)n=1024;
                        size_t count=audio_resample(&downsampler,(int16_t *)decoded+pos,n,out,686);
                        if (count==SIZE_MAX || !queue_audio(out,count)) fail("Ses kuyrugu doldu");
                        pos+=n;
                    }
                }
            }
            free(decoded);
        }
    } else if (!strcmp(type,"response.done")) {
        cJSON *response=cJSON_GetObjectItemCaseSensitive(root,"response");
        const char *status=string_field(response,"status");
        if (!status || (strcmp(status,"completed") && strcmp(status,"incomplete"))) fail("Yanit tamamlanamadi");
        else if (!received_samples) fail("Sesli yanit gelmedi");
        else xEventGroupSetBits(events,EV_DONE);
        cJSON *usage=cJSON_GetObjectItemCaseSensitive(response,"usage");
        cJSON *total=cJSON_GetObjectItemCaseSensitive(usage,"total_tokens");
        if (cJSON_IsNumber(total)) ESP_LOGI("realtime","Usage total_tokens=%d",total->valueint);
    }
    cJSON_Delete(root);
}
static void websocket_event(void *arg,esp_event_base_t base,int32_t id,void *event_data) {
    esp_websocket_event_data_t *e=event_data;
    if (id==WEBSOCKET_EVENT_CONNECTED) xEventGroupSetBits(events,EV_CONNECTED);
    else if (id==WEBSOCKET_EVENT_ERROR) fail("Baglanti hatasi");
    else if (id==WEBSOCKET_EVENT_DISCONNECTED) {
        if (!(xEventGroupGetBits(events)&EV_DONE)) fail("Baglanti kesildi");
    } else if (id==WEBSOCKET_EVENT_DATA && !atomic_load(&abort_audio)) {
        if (e->data_len<0 || e->payload_len<0 || e->payload_offset<0) { fail("WebSocket veri hatasi"); return; }
        ws_result_t result=ws_message_feed(&message,e->op_code,e->fin,e->payload_len,e->payload_offset,e->data_ptr,e->data_len);
        if (result==WS_COMPLETE) on_json(message.data);
        else if (result==WS_INVALID) fail("WebSocket mesaj hatasi");
    }
}
static bool send_text(esp_websocket_client_handle_t ws,const char *text) {
    if (atomic_load(&abort_audio)) return false;
    size_t n=strlen(text);
    return esp_websocket_client_send_text(ws,text,n,pdMS_TO_TICKS(5000))==(int)n;
}
static bool send_json(esp_websocket_client_handle_t ws,cJSON *obj) {
    char *json=cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return false;
    bool ok=send_text(ws,json); cJSON_free(json); return ok;
}
static bool configure(esp_websocket_client_handle_t ws) {
    cJSON *root=cJSON_CreateObject();
    cJSON_AddStringToObject(root,"type","session.update");
    cJSON *session=cJSON_AddObjectToObject(root,"session");
    cJSON_AddStringToObject(session,"type","realtime");
    cJSON_AddStringToObject(session,"instructions",
        "Adin Ceko. Turkce konusan sicak, dogal bir masaustu sesli asistansin. "
        "Turkce cevap ver. Kullanici baska bir dil isterse o dili kullan. "
        "Cevaplarini genelde 1-3 cumle tut. Emin olmadiginda acikca soyle. "
        "Kullanicinin soylemedigi bir istegi uydurma. Ses anlasilmiyorsa tekrar etmesini iste.");
    cJSON *modalities=cJSON_AddArrayToObject(session,"output_modalities");
    cJSON_AddItemToArray(modalities,cJSON_CreateString("audio"));
    cJSON_AddNumberToObject(session,"max_output_tokens",400);
    cJSON *audio=cJSON_AddObjectToObject(session,"audio");
    cJSON *input=cJSON_AddObjectToObject(audio,"input");
    cJSON *inf=cJSON_AddObjectToObject(input,"format");
    cJSON_AddStringToObject(inf,"type","audio/pcm"); cJSON_AddNumberToObject(inf,"rate",24000);
    // Local VAD ends the utterance. Manual commit is NOT push-to-talk.
    cJSON_AddNullToObject(input,"turn_detection");
    cJSON *output=cJSON_AddObjectToObject(audio,"output");
    cJSON *outf=cJSON_AddObjectToObject(output,"format");
    cJSON_AddStringToObject(outf,"type","audio/pcm"); cJSON_AddNumberToObject(outf,"rate",24000);
    cJSON_AddStringToObject(output,"voice",CONFIG_CEKO_VOICE);
    return send_json(ws,root);
}
static bool wait_event(EventBits_t bit,uint32_t timeout_ms) {
    EventBits_t r=xEventGroupWaitBits(events,bit|EV_FAILED,pdFALSE,pdFALSE,pdMS_TO_TICKS(timeout_ms));
    return (r&bit) && !(r&EV_FAILED);
}
static bool send_utterance(esp_websocket_client_handle_t ws,utterance_t u) {
    audio_resampler_t up;
    audio_resampler_init(&up,16000,24000);
    int16_t *pcm=heap_caps_malloc(2400*2,MALLOC_CAP_SPIRAM);
    uint8_t *b64=heap_caps_malloc(6401,MALLOC_CAP_SPIRAM);
    if (!pcm || !b64) { free(pcm); free(b64); return false; }
    bool ok=true;
    for (size_t pos=0;pos<u.n && ok;) {
        size_t n=u.n-pos; if(n>1600)n=1600;
        size_t out=audio_resample(&up,u.pcm+pos,n,pcm,2400), encoded=0;
        if (out==SIZE_MAX || mbedtls_base64_encode(b64,6401,&encoded,(uint8_t *)pcm,out*2)) { ok=false; break; }
        b64[encoded]=0;
        cJSON *root=cJSON_CreateObject();
        cJSON_AddStringToObject(root,"type","input_audio_buffer.append");
        cJSON_AddStringToObject(root,"audio",(char *)b64);
        ok=send_json(ws,root); pos+=n;
    }
    free(pcm); free(b64);
    return ok && send_text(ws,"{\"type\":\"input_audio_buffer.commit\"}") &&
        send_text(ws,"{\"type\":\"response.create\"}");
}
static void network_task(void *arg) {
    utterance_t u;
    for (;;) {
        xQueueReceive(requests,&u,portMAX_DELAY);
        atomic_store(&abort_audio,false); received_samples=0;
        xEventGroupClearBits(events,EV_CONNECTED|EV_CONFIGURED|EV_DONE|EV_FAILED);
        audio_resampler_init(&downsampler,24000,16000);
        int64_t deadline=esp_timer_get_time()+15000000;
        while ((!ceko_wifi_ready() || time(NULL)<1700000000) && esp_timer_get_time()<deadline) vTaskDelay(pdMS_TO_TICKS(100));
        esp_websocket_client_handle_t ws=NULL;
        char *headers=NULL;
        bool ok=false;
        if (!ceko_wifi_ready() || time(NULL)<1700000000) { fail("Wi-Fi / saat hazir degil"); goto cleanup; }
        const char *key=CONFIG_CEKO_OPENAI_API_KEY;
        if (strchr(key,'\r')||strchr(key,'\n')) { fail("API anahtari gecersiz"); goto cleanup; }
        headers=malloc(strlen(key)+32);
        if (!headers) { fail("Bellek yetersiz"); goto cleanup; }
        sprintf(headers,"Authorization: Bearer %s\r\n",key);
        char uri[192];
        if (snprintf(uri,sizeof uri,"wss://api.openai.com/v1/realtime?model=%s",CONFIG_CEKO_MODEL)>=(int)sizeof uri) {
            fail("Model adi cok uzun"); goto cleanup;
        }
        esp_websocket_client_config_t conf={ .uri=uri,.headers=headers,.crt_bundle_attach=esp_crt_bundle_attach,
            .disable_auto_reconnect=true,.network_timeout_ms=10000,.buffer_size=4096,.task_stack=12288 };
        ws=esp_websocket_client_init(&conf);
        if (!ws) { fail("Baglanti baslatilamadi"); goto cleanup; }
        if (esp_websocket_register_events(ws,WEBSOCKET_EVENT_ANY,websocket_event,NULL)!=ESP_OK ||
            esp_websocket_client_start(ws)!=ESP_OK) { fail("Baglanti baslatilamadi"); goto cleanup; }
        if (!wait_event(EV_CONNECTED,15000) || !configure(ws) || !wait_event(EV_CONFIGURED,10000) || !send_utterance(ws,u)) {
            if (!atomic_load(&abort_audio)) fail("API baslatma zaman asimi");
            goto cleanup;
        }
        atomic_store(&last_activity_ms,(uint32_t)(esp_timer_get_time()/1000));
        deadline=esp_timer_get_time()+90000000;
        while (!(xEventGroupGetBits(events)&(EV_DONE|EV_FAILED))) {
            int64_t now=esp_timer_get_time();
            if (now>deadline || (uint32_t)((uint32_t)(now/1000)-atomic_load(&last_activity_ms))>20000) { fail("Yanit zaman asimi"); break; }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        ok=(xEventGroupGetBits(events)&EV_DONE) && !atomic_load(&abort_audio);
cleanup:
        // Stop callbacks before freeing assembly state or inserting playback end marker.
        if (ws) { esp_websocket_client_stop(ws); esp_websocket_client_destroy(ws); }
        if (headers) { memset(headers,0,strlen(headers)); free(headers); }
        ws_message_free(&message);
        if (!ok) atomic_store(&abort_audio,true);
        playback_t end={0};
        xQueueSend(audio_queue,&end,portMAX_DELAY);
        xSemaphoreTake(played,portMAX_DELAY);
        ok = ok && !atomic_load(&abort_audio);
        if (!ok) { ceko_state_set(CEKO_ERROR); vTaskDelay(pdMS_TO_TICKS(2500)); }
        // Acoustic tail cooldown; capture task continues draining microphone locally.
        ceko_state_set(CEKO_THINK); ceko_level_set(0); vTaskDelay(pdMS_TO_TICKS(600));
        ceko_status_set("hey ceko"); ceko_state_set(CEKO_IDLE);
    }
}
void realtime_start(void) {
    requests=xQueueCreate(1,sizeof(utterance_t));
    audio_queue=xQueueCreate(128,sizeof(playback_t));
    played=xSemaphoreCreateBinary(); events=xEventGroupCreate();
    assert(requests && audio_queue && played && events);
    assert(xTaskCreate(playback_task,"speaker",4096,NULL,6,NULL)==pdPASS);
    assert(xTaskCreate(network_task,"openai",12288,NULL,4,NULL)==pdPASS);
}
bool realtime_submit(const int16_t *p,size_t n) {
    utterance_t u={p,n}; return xQueueSend(requests,&u,0)==pdTRUE;
}
