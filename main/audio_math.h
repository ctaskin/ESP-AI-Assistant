#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct {
    float taps[63], delay[63];
    unsigned cursor, phase, up, down;
} audio_resampler_t;
void audio_resampler_init(audio_resampler_t *s, unsigned input_rate, unsigned output_rate);
// Stateful 16<->24 kHz rational resampling with a 7 kHz anti-alias filter.
// Returns samples written, or SIZE_MAX if output capacity is insufficient.
size_t audio_resample(audio_resampler_t *s, const int16_t *input, size_t n, int16_t *out, size_t cap);
int audio_level(const int16_t *pcm, size_t n);
