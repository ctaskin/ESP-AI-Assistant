#include "rt_proto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"

#define TAG "rt_gemini"
// The Live API keeps a single socket open for about ten minutes and then asks
// the client to reconnect; renew a minute early, using the resumption handle.
#define GEMINI_SESSION_MS (10ULL*60*1000)
#define GEMINI_RENEW_MARGIN_MS (60ULL*1000)

static const char INSTRUCTIONS[] =
    "Adin Ceko. Turkce konusan sicak, dogal bir masaustu sesli asistansin. "
    "Turkce cevap ver. Kullanici baska bir dil isterse o dili kullan. "
    "Cevaplarini genelde 1-3 cumle tut. Emin olmadiginda acikca soyle. "
    "Kullanicinin soylemedigi bir istegi uydurma. Ses anlasilmiyorsa tekrar etmesini iste. "
    "Guncel bilgi gerektiginde Google aramasini kullan ve kaynagi kisaca soyle. "
    "Ayni oturumda konusma devam eder; kullanicinin az once soylediklerini hatirla.";

static const char *config_error(void) {
    const char *key = CONFIG_CEKO_GEMINI_API_KEY;
    if (!strlen(key)) return "menuconfig > Gemini key";
    for (const char *c = key; *c; ++c)
        if (*c <= ' ' || *c > '~' || *c == '&' || *c == '?' || *c == '#') return "API anahtari gecersiz";
    if (!strlen(CONFIG_CEKO_GEMINI_MODEL)) return "menuconfig > model";
    return NULL;
}
static bool uri(char *out, size_t cap) {
    return snprintf(out,cap,
        "wss://generativelanguage.googleapis.com/ws/"
        "google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=%s",
        CONFIG_CEKO_GEMINI_API_KEY) < (int)cap;
}
static bool setup(void *ws, const char *recap, const char *resume_handle) {
    cJSON *root = cJSON_CreateObject();
    cJSON *s = cJSON_AddObjectToObject(root,"setup");
    char model[96];
    snprintf(model,sizeof model,"models/%s",CONFIG_CEKO_GEMINI_MODEL);
    cJSON_AddStringToObject(s,"model",model);
    cJSON *gen = cJSON_AddObjectToObject(s,"generationConfig");
    cJSON *modalities = cJSON_AddArrayToObject(gen,"responseModalities");
    cJSON_AddItemToArray(modalities,cJSON_CreateString("AUDIO"));
    cJSON_AddNumberToObject(gen,"maxOutputTokens",400);
    cJSON *speech = cJSON_AddObjectToObject(gen,"speechConfig");
    cJSON *voice = cJSON_AddObjectToObject(speech,"voiceConfig");
    cJSON *prebuilt = cJSON_AddObjectToObject(voice,"prebuiltVoiceConfig");
    cJSON_AddStringToObject(prebuilt,"voiceName",CONFIG_CEKO_GEMINI_VOICE);
    cJSON_AddStringToObject(speech,"languageCode","tr-TR");
    cJSON *sys = cJSON_AddObjectToObject(s,"systemInstruction");
    cJSON *parts = cJSON_AddArrayToObject(sys,"parts");
    cJSON *part = cJSON_CreateObject();
    cJSON_AddStringToObject(part,"text",INSTRUCTIONS);
    cJSON_AddItemToArray(parts,part);
#if CONFIG_CEKO_WEB_SEARCH
    // Search runs on Google's side; the board needs no second connection or key.
    cJSON *tools = cJSON_AddArrayToObject(s,"tools");
    cJSON *search = cJSON_CreateObject();
    cJSON_AddItemToObject(search,"googleSearch",cJSON_CreateObject());
    cJSON_AddItemToArray(tools,search);
#endif
    // Local wake word and VAD decide when a turn starts and ends.
    cJSON *realtime_cfg = cJSON_AddObjectToObject(s,"realtimeInputConfig");
    cJSON *vad = cJSON_AddObjectToObject(realtime_cfg,"automaticActivityDetection");
    cJSON_AddBoolToObject(vad,"disabled",true);
    cJSON *resume = cJSON_AddObjectToObject(s,"sessionResumption");
    if (resume_handle && resume_handle[0]) cJSON_AddStringToObject(resume,"handle",resume_handle);
    cJSON *compression = cJSON_AddObjectToObject(s,"contextWindowCompression");
    cJSON_AddItemToObject(compression,"slidingWindow",cJSON_CreateObject());
#if CONFIG_CEKO_MEMORY_TURNS > 0
    cJSON_AddItemToObject(s,"inputAudioTranscription",cJSON_CreateObject());
    cJSON_AddItemToObject(s,"outputAudioTranscription",cJSON_CreateObject());
#endif
    if (!rt_send_json(ws,root)) return false;
    // A valid handle restores context on the server, so only replay the recap
    // when we reconnected without one.
    if (!recap || !recap[0] || (resume_handle && resume_handle[0])) return true;
    cJSON *client = cJSON_CreateObject();
    cJSON *content = cJSON_AddObjectToObject(client,"clientContent");
    cJSON *turns = cJSON_AddArrayToObject(content,"turns");
    cJSON *turn = cJSON_CreateObject();
    cJSON_AddStringToObject(turn,"role","user");
    cJSON *turn_parts = cJSON_AddArrayToObject(turn,"parts");
    cJSON *turn_part = cJSON_CreateObject();
    cJSON_AddStringToObject(turn_part,"text",recap);
    cJSON_AddItemToArray(turn_parts,turn_part);
    cJSON_AddItemToArray(turns,turn);
    cJSON_AddBoolToObject(content,"turnComplete",false);
    return rt_send_json(ws,client);
}
static bool turn_begin(void *ws) {
    return rt_send_text(ws,"{\"realtimeInput\":{\"activityStart\":{}}}");
}
static bool audio_chunk(void *ws, const char *b64) {
    cJSON *root = cJSON_CreateObject();
    cJSON *realtime_input = cJSON_AddObjectToObject(root,"realtimeInput");
    cJSON *audio = cJSON_AddObjectToObject(realtime_input,"audio");
    cJSON_AddStringToObject(audio,"data",b64);
    cJSON_AddStringToObject(audio,"mimeType","audio/pcm;rate=16000");
    return rt_send_json(ws,root);
}
static bool turn_end(void *ws) {
    return rt_send_text(ws,"{\"realtimeInput\":{\"activityEnd\":{}}}");
}
static const char *string_field(cJSON *object, const char *key) {
    cJSON *x = cJSON_GetObjectItemCaseSensitive(object,key);
    return cJSON_IsString(x) ? x->valuestring : NULL;
}
static void transcript(cJSON *parent, const char *key, void (*out)(const char *)) {
    const char *text = string_field(cJSON_GetObjectItemCaseSensitive(parent,key),"text");
    if (text) out(text);
}
static void handle(cJSON *root, const rt_sink_t *sink) {
    if (cJSON_GetObjectItemCaseSensitive(root,"setupComplete")) { sink->ready(); return; }
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root,"error");
    if (cJSON_IsObject(error)) {
        cJSON *code = cJSON_GetObjectItemCaseSensitive(error,"code");
        char text[16] = "unknown";
        if (cJSON_IsNumber(code)) snprintf(text,sizeof text,"%d",code->valueint);
        sink->failure("API hatasi: seri log", text);
        return;
    }
    cJSON *resume = cJSON_GetObjectItemCaseSensitive(root,"sessionResumptionUpdate");
    if (cJSON_IsObject(resume)) {
        const char *handle_text = string_field(resume,"newHandle");
        cJSON *resumable = cJSON_GetObjectItemCaseSensitive(resume,"resumable");
        if (handle_text && cJSON_IsTrue(resumable)) sink->resume_handle(handle_text);
        return;
    }
    if (cJSON_GetObjectItemCaseSensitive(root,"goAway")) {
        ESP_LOGW(TAG,"server asked to reconnect");
        return;
    }
    cJSON *usage = cJSON_GetObjectItemCaseSensitive(root,"usageMetadata");
    if (cJSON_IsObject(usage)) {
        cJSON *total = cJSON_GetObjectItemCaseSensitive(usage,"totalTokenCount");
        if (cJSON_IsNumber(total)) sink->usage(total->valueint);
    }
    cJSON *server_content = cJSON_GetObjectItemCaseSensitive(root,"serverContent");
    if (!cJSON_IsObject(server_content)) return;
    transcript(server_content,"inputTranscription",sink->user_text);
    transcript(server_content,"outputTranscription",sink->reply_text);
    cJSON *model_turn = cJSON_GetObjectItemCaseSensitive(server_content,"modelTurn");
    cJSON *parts = cJSON_GetObjectItemCaseSensitive(model_turn,"parts"), *part = NULL;
    cJSON_ArrayForEach(part,parts) {
        cJSON *inline_data = cJSON_GetObjectItemCaseSensitive(part,"inlineData");
        const char *b64 = string_field(inline_data,"data");
        if (b64) sink->audio(b64);
    }
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(server_content,"turnComplete"))) sink->turn_done();
}
const rt_provider_t rt_gemini_provider = {
    .name = "gemini", .send_rate = 16000, .recv_rate = 24000,
    .max_session_ms = GEMINI_SESSION_MS, .renew_margin_ms = GEMINI_RENEW_MARGIN_MS,
    .config_error = config_error, .uri = uri, .auth_header = NULL,
    .setup = setup, .turn_begin = turn_begin, .audio_chunk = audio_chunk,
    .turn_end = turn_end, .handle = handle,
};
