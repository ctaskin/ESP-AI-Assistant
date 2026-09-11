#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef struct { char *data; size_t len, cap, frame_pos, frame_size; bool open; } ws_message_t;
typedef enum { WS_MORE, WS_COMPLETE, WS_IGNORE, WS_INVALID } ws_result_t;
ws_result_t ws_message_feed(ws_message_t *s,int opcode,bool fin,size_t frame_size,size_t offset,const char *data,size_t len);
void ws_message_free(ws_message_t *s);
