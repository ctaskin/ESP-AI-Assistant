#include "audio_math.h"
#include "capture_gate.h"
#include "ws_message.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void gate_tests(void) {
    capture_gate_t g;
    capture_gate_init(&g,20,1000);
    for(int i=0;i<249;++i) assert(capture_gate_feed(&g,320,false)==CAPTURE_MORE);
    assert(capture_gate_feed(&g,320,false)==CAPTURE_EMPTY);
    capture_gate_init(&g,20,1000);
    for(int i=0;i<30;++i) assert(capture_gate_feed(&g,320,true)==CAPTURE_MORE);
    for(int i=0;i<49;++i) assert(capture_gate_feed(&g,320,false)==CAPTURE_MORE);
    assert(capture_gate_feed(&g,320,false)==CAPTURE_READY);
    capture_gate_init(&g,5,1000);
    for(int i=0;i<249;++i) assert(capture_gate_feed(&g,320,true)==CAPTURE_MORE);
    assert(capture_gate_feed(&g,320,true)==CAPTURE_TOO_LONG);
}
static void websocket_tests(void) {
    ws_message_t m={0};
    assert(ws_message_feed(&m,1,true,7,0,"{\"a",3)==WS_MORE);
    assert(ws_message_feed(&m,1,true,7,3,"\":1}",4)==WS_COMPLETE);
    assert(!strcmp(m.data,"{\"a\":1}"));
    assert(ws_message_feed(&m,1,false,3,0,"abc",3)==WS_MORE);
    assert(ws_message_feed(&m,9,true,1,0,"p",1)==WS_IGNORE);
    assert(ws_message_feed(&m,0,false,2,0,"de",2)==WS_MORE);
    assert(ws_message_feed(&m,0,true,2,0,"fg",2)==WS_COMPLETE);
    assert(!strcmp(m.data,"abcdefg"));
    assert(ws_message_feed(&m,0,true,1,0,"!",1)==WS_INVALID);
    assert(ws_message_feed(&m,1,true,300000,0,"!",1)==WS_INVALID);
    assert(ws_message_feed(&m,1,true,4,0,"ab",2)==WS_MORE);
    assert(ws_message_feed(&m,1,true,4,3,"d",1)==WS_INVALID);
    ws_message_free(&m);
}
static void resampler_tests(void) {
    int16_t input[2400],whole[3600],chunked[3600];
    for(int i=0;i<2400;++i) input[i]=(int16_t)(12000*sin(i*2*3.14159265359*1000/24000));
    audio_resampler_t a,b;
    audio_resampler_init(&a,24000,16000); audio_resampler_init(&b,24000,16000);
    size_t n=audio_resample(&a,input,2400,whole,3600), total=0;
    assert(n==1600);
    for(int i=0;i<2400;) {
        int k=1+(i*7)%91; if(k>2400-i)k=2400-i;
        size_t got=audio_resample(&b,input+i,k,chunked+total,3600-total);
        assert(got!=SIZE_MAX); total+=got; i+=k;
    }
    assert(total==n && !memcmp(whole,chunked,n*2));
    audio_resampler_init(&a,16000,24000);
    assert(audio_resample(&a,input,1600,whole,3600)==2400);
    audio_resampler_t saved=a;
    assert(audio_resample(&a,input,1600,whole,1)==SIZE_MAX);
    assert(!memcmp(&a,&saved,sizeof a));
    assert(audio_level((int16_t[]){0,0},2)==0);
    assert(audio_level((int16_t[]){32767,-32768},2)==100);
    // Above-output-Nyquist tone should be attenuated substantially (anti-alias).
    audio_resampler_init(&a,24000,16000);
    for(int i=0;i<2400;++i) input[i]=(int16_t)(12000*sin(i*2*3.14159265359*10000/24000));
    n=audio_resample(&a,input,2400,whole,3600);
    double energy=0;
    for(size_t i=100;i<n;++i) energy+=(double)whole[i]*whole[i];
    assert(sqrt(energy/(n-100))<500);
}
int main(void) { gate_tests(); websocket_tests(); resampler_tests(); puts("PASS: capture gating, fragmented WebSocket assembly, streaming resampling, anti-aliasing, audio level"); }
