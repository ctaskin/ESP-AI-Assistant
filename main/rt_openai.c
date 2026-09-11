#include "rt_proto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"

#define TAG "rt_openai"
// Documented ceiling for one Realtime session is 60 minutes; renew earlier so
// the rollover always happens while the device is idle.
#define OPENAI_SESSION_MS (60ULL*60*1000)
#define OPENAI_RENEW_MARGIN_MS (5ULL*60*1000)

static const char INSTRUCTIONS[] =
    "Adin Ceko. Turkce konusan sicak, dogal bir masaustu sesli asistansin. "
    "Turkce cevap ver. Kullanici baska bir dil isterse o dili kullan. "
    "Cevaplarini genelde 1-3 cumle tut. Emin olmadiginda acikca soyle. "
    "Kullanicinin soylemedigi bir istegi uydurma. Ses anlasilmiyorsa tekrar etmesini iste. "
    "Ayni oturumda konusma devam eder; kullanicinin az once soylediklerini hatirla.";

static const char *config_error(void) {
    if (!strlen(CONFIG_CEKO_OPENAI_API_KEY)) return "menuconfig > OpenAI key";
    if (strchr(CONFIG_CEKO_OPENAI_API_KEY,'\r') || strchr(CONFIG_CEKO_OPENAI_API_KEY,'\n')) return "API anahtari gecersiz";
    if (!strlen(CONFIG_CEKO_MODEL)) return "menuconfig > model";
    return NULL;
}
static bool uri(char *out, size_t cap) {
    return snprintf(out,cap,"wss://api.openai.com/v1/realtime?model=%s",CONFIG_CEKO_MODEL) < (int)cap;
}
static char *auth_header(void) {
    const char *key = CONFIG_CEKO_OPENAI_API_KEY;
    char *h = malloc(strlen(key)+32);
    if (h) sprintf(h,"Authorization: Bearer %s\r\n",key);
    return h;
}
static void add_tools(cJSON *session) {
#if CONFIG_CEKO_WEB_SEARCH
    cJSON *tools = cJSON_AddArrayToObject(session,"tools");
    if (strlen(CONFIG_CEKO_OPENAI_MCP_URL)) {
        // The service calls the MCP server itself; the board never makes the request.
        cJSON *mcp = cJSON_CreateObject();
        cJSON_AddStringToObject(mcp,"type","mcp");
        cJSON_AddStringToObject(mcp,"server_label",CONFIG_CEKO_OPENAI_MCP_LABEL);
        cJSON_AddStringToObject(mcp,"server_url",CONFIG_CEKO_OPENAI_MCP_URL);
        cJSON_AddStringToObject(mcp,"require_approval","never");
        if (strlen(CONFIG_CEKO_OPENAI_MCP_TOKEN)) cJSON_AddStringToObject(mcp,"authorization",CONFIG_CEKO_OPENAI_MCP_TOKEN);
        cJSON_AddItemToArray(tools,mcp);
    } else {
        // Hosted web search on the Realtime endpoint is not documented as GA;
        // if the account rejects it the session.update fails visibly in the log.
        cJSON *search = cJSON_CreateObject();
        cJSON_AddStringToObject(search,"type","web_search");
        cJSON_AddItemToArray(tools,search);
    }
#else
    (void)session;
#endif
}
static bool setup(void *ws, const char *recap, const char *resume_handle) {
    (void)resume_handle;   // OpenAI has no server side resume; recap carries context
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root,"type","session.update");
    cJSON *session = cJSON_AddObjectToObject(root,"session");
    cJSON_AddStringToObject(session,"type","realtime");
    cJSON_AddStringToObject(session,"instructions",INSTRUCTIONS);
    cJSON *modalities = cJSON_AddArrayToObject(session,"output_modalities");
    cJSON_AddItemToArray(modalities,cJSON_CreateString("audio"));
    cJSON_AddNumberToObject(session,"max_output_tokens",400);
    if (strlen(CONFIG_CEKO_REASONING_EFFORT)) {
        cJSON *reasoning = cJSON_AddObjectToObject(session,"reasoning");
        cJSON_AddStringToObject(reasoning,"effort",CONFIG_CEKO_REASONING_EFFORT);
    }
    cJSON *audio = cJSON_AddObjectToObject(session,"audio");
    cJSON *input = cJSON_AddObjectToObject(audio,"input");
    cJSON *inf = cJSON_AddObjectToObject(input,"format");
    cJSON_AddStringToObject(inf,"type","audio/pcm"); cJSON_AddNumberToObject(inf,"rate",24000);
    // Local VAD ends the utterance. Manual commit is NOT push-to-talk.
    cJSON_AddNullToObject(input,"turn_detection");
#if CONFIG_CEKO_MEMORY_TURNS > 0
    // Transcripts are only used to rebuild context after a reconnect. They are
    // billed separately; set memory turns to 0 in menuconfig to switch them off.
    cJSON *transcription = cJSON_AddObjectToObject(input,"transcription");
    cJSON_AddStringToObject(transcription,"model",CONFIG_CEKO_TRANSCRIBE_MODEL);
#endif
    cJSON *output = cJSON_AddObjectToObject(audio,"output");
    cJSON *outf = cJSON_AddObjectToObject(output,"format");
    cJSON_AddStringToObject(outf,"type","audio/pcm"); cJSON_AddNumberToObject(outf,"rate",24000);
    cJSON_AddStringToObject(output,"voice",CONFIG_CEKO_VOICE);
    add_tools(session);
    if (!rt_send_json(ws,root)) return false;
    if (!recap || !recap[0]) return true;
    cJSON *item_root = cJSON_CreateObject();
    cJSON_AddStringToObject(item_root,"type","conversation.item.create");
    cJSON *item = cJSON_AddObjectToObject(item_root,"item");
    cJSON_AddStringToObject(item,"type","message");
    cJSON_AddStringToObject(item,"role","user");
    cJSON *content = cJSON_AddArrayToObject(item,"content");
    cJSON *part = cJSON_CreateObject();
    cJSON_AddStringToObject(part,"type","input_text");
    cJSON_AddStringToObject(part,"text",recap);
    cJSON_AddItemToArray(content,part);
    return rt_send_json(ws,item_root);
}
static bool turn_begin(void *ws) { (void)ws; return true; }
static bool audio_chunk(void *ws, const char *b64) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root,"type","input_audio_buffer.append");
    cJSON_AddStringToObject(root,"audio",b64);
    return rt_send_json(ws,root);
}
static bool turn_end(void *ws) {
    return rt_send_text(ws,"{\"type\":\"input_audio_buffer.commit\"}") &&
           rt_send_text(ws,"{\"type\":\"response.create\"}");
}
static const char *string_field(cJSON *object, const char *key) {
    cJSON *x = cJSON_GetObjectItemCaseSensitive(object,key);
    return cJSON_IsString(x) ? x->valuestring : NULL;
}
static void handle(cJSON *root, const rt_sink_t *sink) {
    const char *type = string_field(root,"type");
    if (!type) return;
    if (!strcmp(type,"session.updated")) { sink->ready(); return; }
    if (!strcmp(type,"error")) {
        // Log only the error code, never a payload that might contain user audio.
        sink->failure("API hatasi: seri log", string_field(cJSON_GetObjectItemCaseSensitive(root,"error"),"code"));
        return;
    }
    if (!strcmp(type,"response.output_audio.delta") || !strcmp(type,"response.audio.delta")) {
        const char *b64 = string_field(root,"delta");
        if (b64) sink->audio(b64);
        return;
    }
    if (!strcmp(type,"response.output_audio_transcript.delta") || !strcmp(type,"response.audio_transcript.delta")) {
        const char *text = string_field(root,"delta");
        if (text) sink->reply_text(text);
        return;
    }
    if (!strcmp(type,"conversation.item.input_audio_transcription.completed")) {
        const char *text = string_field(root,"transcript");
        if (text) sink->user_text(text);
        return;
    }
    if (!strcmp(type,"response.mcp_call.in_progress") || !strcmp(type,"response.web_search_call.in_progress")) {
        ESP_LOGI(TAG,"tool call in progress");
        return;
    }
    if (!strcmp(type,"response.done")) {
        cJSON *response = cJSON_GetObjectItemCaseSensitive(root,"response");
        cJSON *usage = cJSON_GetObjectItemCaseSensitive(response,"usage");
        cJSON *total = cJSON_GetObjectItemCaseSensitive(usage,"total_tokens");
        if (cJSON_IsNumber(total)) sink->usage(total->valueint);
        const char *status = string_field(response,"status");
        if (!status || (strcmp(status,"completed") && strcmp(status,"incomplete")))
            sink->failure("Yanit tamamlanamadi", status);
        else sink->turn_done();
    }
}
const rt_provider_t rt_openai_provider = {
    .name = "openai", .send_rate = 24000, .recv_rate = 24000,
    .max_session_ms = OPENAI_SESSION_MS, .renew_margin_ms = OPENAI_RENEW_MARGIN_MS,
    .config_error = config_error, .uri = uri, .auth_header = auth_header,
    .setup = setup, .turn_begin = turn_begin, .audio_chunk = audio_chunk,
    .turn_end = turn_end, .handle = handle,
};
