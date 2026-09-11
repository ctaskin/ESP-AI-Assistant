#include "ws_message.h"
#include <stdlib.h>
#include <string.h>
#define WS_MAX_MESSAGE (256U*1024U)
void ws_message_free(ws_message_t *s) { free(s->data); memset(s,0,sizeof *s); }
ws_result_t ws_message_feed(ws_message_t *s,int op,bool fin,size_t total,size_t off,const char *data,size_t n) {
    if (op>=8) return WS_IGNORE; // interleaved ping/pong/close does not reset a text message
    if ((op!=0 && op!=1)||off>total||n>total-off||total>WS_MAX_MESSAGE) return WS_INVALID;
    if (!off) {
        if (op==1) {
            if (s->open) return WS_INVALID;
            s->open=true; s->len=0;
        } else if (!s->open) return WS_INVALID;
        s->frame_pos=0; s->frame_size=total;
    }
    if (!s->open||s->frame_size!=total||s->frame_pos!=off||n>WS_MAX_MESSAGE-s->len) return WS_INVALID;
    size_t needed=s->len+n+1;
    if (needed>s->cap) {
        size_t cap=(needed+4095)&~(size_t)4095;
        char *p=realloc(s->data,cap); if (!p) return WS_INVALID;
        s->data=p; s->cap=cap;
    }
    if(n) memcpy(s->data+s->len,data,n);
    s->len+=n; s->frame_pos+=n; s->data[s->len]=0;
    if (s->frame_pos==total && fin) { s->open=false; return WS_COMPLETE; }
    return WS_MORE;
}
