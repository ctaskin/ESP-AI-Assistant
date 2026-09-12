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

// Appended only when a search tool is actually attached, so the model is never
// told to use a tool it does not have.
static const char SEARCH_HINT[] =
    " Guncel bilgi, tarih, fiyat veya haber gerektiginde web arama aracini kullan "
    "ve kaynagi kisaca soyle. Aramadan emin olamadigin guncel bilgiyi uydurma.";
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
// Realtime supports remote MCP servers and custom functions. A hosted
// web_search tool is not documented for this endpoint, so the MCP server is the
// dependable route; the board never makes the search request itself.
static bool search_tool_enabled(void) {
#if CONFIG_CEKO_WEB_SEARCH
#if CONFIG_CEKO_OPENAI_HOSTED_WEB_SEARCH
    return true;
#else
    return strlen(CONFIG_CEKO_OPENAI_MCP_URL) > 0;
#endif
#else
    return false;
#endif
}
static void add_tools(cJSON *session) {
#if CONFIG_CEKO_WEB_SEARCH
    if (!search_tool_enabled()) return;          // no web access configured
    cJSON *tools = cJSON_AddArrayToObject(session,"tools");
    cJSON *tool = cJSON_CreateObject();
    if (strlen(CONFIG_CEKO_OPENAI_MCP_URL)) {
        cJSON_AddStringToObject(tool,"type","mcp");
        cJSON_AddStringToObject(tool,"server_label",CONFIG_CEKO_OPENAI_MCP_LABEL);
        cJSON_AddStringToObject(tool,"server_url",CONFIG_CEKO_OPENAI_MCP_URL);
        cJSON_AddStringToObject(tool,"require_approval","never");
        if (strlen(CONFIG_CEKO_OPENAI_MCP_TOKEN)) {
            // Documented form: a bearer token in the request headers.
            char bearer[256];
            snprintf(bearer,sizeof bearer,"Bearer %s",CONFIG_CEKO_OPENAI_MCP_TOKEN);
            cJSON *headers = cJSON_AddObjectToObject(tool,"headers");
            cJSON_AddStringToObject(headers,"Authorization",bearer);
        }
    } else {
        // Experimental: accounts that reject it answer invalid_value and setup
        // retries without any tool.
        cJSON_AddStringToObject(tool,"type","web_search");
    }
    cJSON_AddItemToArray(tools,tool);
#else
    (void)session;
#endif
}
static bool setup(void *ws, const char *recap, const char *resume_handle, unsigned level) {
    (void)resume_handle;   // OpenAI has no server side resume; recap carries context
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root,"type","session.update");
    cJSON *session = cJSON_AddObjectToObject(root,"session");
    cJSON_AddStringToObject(session,"type","realtime");
    if (level < 1 && search_tool_enabled()) {
        char instructions[sizeof INSTRUCTIONS + sizeof SEARCH_HINT];
        snprintf(instructions,sizeof instructions,"%s%s",INSTRUCTIONS,SEARCH_HINT);
        cJSON_AddStringToObject(session,"instructions",instructions);
    } else {
        cJSON_AddStringToObject(session,"instructions",INSTRUCTIONS);
    }
    cJSON *modalities = cJSON_AddArrayToObject(session,"output_modalities");
    cJSON_AddItemToArray(modalities,cJSON_CreateString("audio"));
    cJSON_AddNumberToObject(session,"max_output_tokens",400);
    if (level < 2 && strlen(CONFIG_CEKO_REASONING_EFFORT)) {
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
    if (level < 3) {
        cJSON *transcription = cJSON_AddObjectToObject(input,"transcription");
        cJSON_AddStringToObject(transcription,"model",CONFIG_CEKO_TRANSCRIBE_MODEL);
    }
#endif
    cJSON *output = cJSON_AddObjectToObject(audio,"output");
    cJSON *outf = cJSON_AddObjectToObject(output,"format");
    cJSON_AddStringToObject(outf,"type","audio/pcm"); cJSON_AddNumberToObject(outf,"rate",24000);
    cJSON_AddStringToObject(output,"voice",CONFIG_CEKO_VOICE);
    if (level < 1) add_tools(session);
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
// Give up cleanly instead of leaving the service generating an answer nobody
// will hear and still be billed for.
static bool turn_cancel(void *ws) {
    return rt_send_text(ws,"{\"type\":\"response.cancel\"}");
}
static bool turn_end(void *ws) {
    return rt_send_text(ws,"{\"type\":\"input_audio_buffer.commit\"}") &&
           rt_send_text(ws,"{\"type\":\"response.create\"}");
}
static const char *string_field(cJSON *object, const char *key) {
    cJSON *x = cJSON_GetObjectItemCaseSensitive(object,key);
    return cJSON_IsString(x) ? x->valuestring : NULL;
}
static const char *text_or(const char *s) { return s ? s : "-"; }
static void handle(cJSON *root, const rt_sink_t *sink) {
    const char *type = string_field(root,"type");
    if (!type) return;
    if (!strcmp(type,"session.updated")) { sink->ready(); return; }
    if (!strcmp(type,"error")) {
        // Schema detail only: type, code, the rejected field and the service
        // message. Never a payload that might contain user audio.
        cJSON *err = cJSON_GetObjectItemCaseSensitive(root,"error");
        static char detail[224];
        snprintf(detail,sizeof detail,"%s/%s param=%s msg=%.110s",
                 text_or(string_field(err,"type")), text_or(string_field(err,"code")),
                 text_or(string_field(err,"param")), text_or(string_field(err,"message")));
        sink->failure("API hatasi: seri log", detail);
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
    if (!strncmp(type,"response.mcp_call",17) || !strncmp(type,"mcp_list_tools",14) ||
        !strncmp(type,"response.web_search_call",23)) {
        // Visible proof that the search tool is reachable, and the signal that
        // this turn is allowed to stay quiet for longer.
        ESP_LOGI(TAG,"tool: %s",type);
        sink->tool_activity();
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
    .turn_end = turn_end, .turn_cancel = turn_cancel, .handle = handle,
};
