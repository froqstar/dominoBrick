#include <math.h>
#include <stdio.h>
#include "dsp_goertzel.h"
#include "config.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main(void)
{
    int N = SAMPLES_PER_SYMBOL;
    static float s[800];
    float f0 = AUDIO_CENTER_HZ - (NUM_TONES / 2) * TONE_SPACING_HZ + 9 * TONE_SPACING_HZ + 35.0f;
    for (int k = 0; k < N; k++) {
        s[k] = sinf(2.0f * M_PI * f0 * k / SAMPLE_RATE_HZ);
    }
    for (float off = -40.0f; off <= 140.0f; off += 20.0f) {
        float base = AUDIO_CENTER_HZ - (NUM_TONES / 2) * TONE_SPACING_HZ + off;
        float peak = 0;
        int bi = 0;
        for (int i = 0; i < NUM_TONES; i++) {
            float p = goertzel_mag(s, N, base + i * TONE_SPACING_HZ, SAMPLE_RATE_HZ);
            if (p > peak) {
                peak = p;
                bi = i;
            }
        }
        printf("off=%6.1f peak=%.1f bin=%d\n", off, peak, bi);
    }
    printf("span=%f\n", afc_acquire_span(s, N, N / 8, SAMPLE_RATE_HZ));
    return 0;
}
