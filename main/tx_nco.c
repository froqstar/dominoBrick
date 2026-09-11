#include "tx_nco.h"
#include "audio_io.h"
#include "config.h"
#include <math.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float phase;

void nco_symbol(float tone_hz, int n_samples)
{
    static uint8_t buf[1024];
    if (n_samples > (int)sizeof(buf)) {
        n_samples = sizeof(buf);
    }
    float step = 2.0f * (float)M_PI * tone_hz / SAMPLE_RATE_HZ;
    int ramp = n_samples / 10;
    if (ramp < 1) {
        ramp = 1;
    }
    for (int i = 0; i < n_samples; i++) {
        float env = 1.0f;
        if (i < ramp) {
            env = (float)i / ramp;
        } else if (i >= n_samples - ramp) {
            env = (float)(n_samples - 1 - i) / ramp;
        }
        float v = sinf(phase) * env;
        phase += step;
        if (phase > 2.0f * (float)M_PI) {
            phase -= 2.0f * (float)M_PI;
        }
        buf[i] = (uint8_t)(128 + (int)(v * 100));
    }
    audio_tx_stream(buf, n_samples);
}

void nco_idle(void)
{
    audio_tx_idle();
}
