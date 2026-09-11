#include "audio_math.h"
#include <assert.h>
#include <math.h>
#include <string.h>
void audio_resampler_init(audio_resampler_t *s, unsigned in, unsigned out) {
    assert((in==16000 && out==24000)||(in==24000 && out==16000));
    memset(s,0,sizeof *s);
    s->up = in==16000?3:2; s->down = out==24000?2:3;
    float sum=0, fc=7000.0f/48000.0f;
    for (int i=0;i<63;++i) {
        int x=i-31;
        float sinc=x==0?2*fc:sinf(2*3.14159265359f*fc*x)/(3.14159265359f*x);
        s->taps[i]=sinc*(0.54f-0.46f*cosf(2*3.14159265359f*i/62)); sum+=s->taps[i];
    }
    for (int i=0;i<63;++i) s->taps[i]*=s->up/sum;
}
size_t audio_resample(audio_resampler_t *s,const int16_t *in,size_t n,int16_t *out,size_t cap) {
    if (n > (SIZE_MAX-s->down)/s->up || cap < (n*s->up+s->down-1)/s->down) return SIZE_MAX;
    size_t written=0;
    for (size_t i=0;i<n;++i) for (unsigned z=0;z<s->up;++z) {
        s->delay[s->cursor]=z==0?in[i]:0;
        if (s->phase==0) {
            float v=0;
            for (unsigned k=0;k<63;++k) v+=s->taps[k]*s->delay[(s->cursor+63-k)%63];
            out[written++]=(int16_t)lrintf(fmaxf(-32768,fminf(32767,v)));
        }
        s->cursor=(s->cursor+1)%63; s->phase=(s->phase+1)%s->down;
    }
    return written;
}
int audio_level(const int16_t *p,size_t n) {
    if (!n) return 0;
    double sq=0;
    for (size_t i=0;i<n;++i) sq+=(double)p[i]*p[i];
    double rms=sqrt(sq/n)/32768;
    double level=(20*log10(rms+1e-8)+48)/36;
    return (int)(fmax(0,fmin(1,level))*100);
}
