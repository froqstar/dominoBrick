#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dominoex.h"
#include "dominovar.h"
#include "dsp_goertzel.h"
#include "config.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define F_OFF_HZ 35.0f
#define PRE_SAMPLES 211
#define NOISE_AMPL 0.04f

static float tone_freq(int idx)
{
    return TONE_BASE_HZ + idx * TONE_SPACING_HZ;
}

static float *s;
static int pos, cap, tone;
static float phase;
static int N;

static void emit_sym(void)
{
    float f = tone_freq(tone) + F_OFF_HZ;
    float step = 2.0f * M_PI * f / SAMPLE_RATE_HZ;
    int ramp = N / 10;
    for (int k = 0; k < N; k++) {
        float env = 1.0f;
        if (k < ramp) {
            env = (float)k / ramp;
        } else if (k >= N - ramp) {
            env = (float)(N - 1 - k) / ramp;
        }
        if (pos == 0 && k < PRE_SAMPLES) {
            continue;
        }
        s[pos++] = sinf(phase) * env;
        phase += step;
        if (phase > 2.0f * M_PI) {
            phase -= 2.0f * M_PI;
        }
    }
}

static void send_char(unsigned char c, int secondary)
{
    uint8_t nibs[3];
    int n = dominoex_tx_nibbles(c, secondary, nibs);
    for (int i = 0; i < n; i++) {
        tone = dominoex_ifk_forward(tone, nibs[i]);
        emit_sym();
    }
}

int main(void)
{
    const char *msg = "the lazy horse jumps over the brown chicken";
    N = SAMPLES_PER_SYMBOL;
    cap = 400 * N;
    s = calloc(cap, sizeof(float));
    tone = 0;

    for (int i = 0; i < 8; i++) {
        send_char(0, 1);
    }
    send_char(0, 1);
    send_char('\r', 0);
    send_char(2, 0);
    send_char('\r', 0);
    for (const char *p = msg; *p; p++) {
        send_char(*p, 0);
    }
    send_char('\r', 0);
    send_char(4, 0);
    send_char('\r', 0);
    for (int i = 0; i < 4; i++) {
        send_char(0, 1);
    }
    srand(1);
    for (int i = 0; i < pos; i++) {
        s[i] += ((float)rand() / RAND_MAX - 0.5f) * NOISE_AMPL;
    }

    printf("synth samples=%d N=%d\n", pos, N);
    const int S = N / 8;
    float *win = calloc(N + 2 * S, sizeof(float));
    int fill = 0, need = N + 2 * S, corr = 0;
    float afc_off = 0;
    int bad = 0, si = 0, first = 1;
    static char got[1024], sec[256];
    int ngot = 0, nsec = 0;
    afc_reset(&afc_off);
    dominoex_rx_reset();
    afc_off = afc_acquire_wide(s, 8 * N, SAMPLE_RATE_HZ);

    for (int i = 0; i < pos && ngot < (int)sizeof(got) - 1;) {
        while (fill < need && i < pos) {
            win[fill++] = s[i++];
        }
        if (fill < need) {
            break;
        }
        float ce, co, cl;
        tone_detect_wide(win, N, SAMPLE_RATE_HZ, afc_off, &ce);
        tone_detect_wide(win + S, N, SAMPLE_RATE_HZ, afc_off, &co);
        tone_detect_wide(win + 2 * S, N, SAMPLE_RATE_HZ, afc_off, &cl);
        int sel = 1;
        corr = 0;
        float wmax = ce > co ? (ce > cl ? ce : cl) : (co > cl ? co : cl);
        if (wmax < 3.0f) {
            sel = 1;
        } else if (co < 3.0f) {
            if (bad > 4) {
                sel = 1;
            } else if (ce >= cl) {
                sel = 0;
                corr = -S / 2;
            } else {
                sel = 2;
                corr = S / 2;
            }
        } else if (co > 10.0f && (ce > cl ? ce - cl : cl - ce) < 0.3f * co) {
            sel = 1;
        } else if (ce > cl) {
            sel = 0;
            corr = -S / 8;
        } else if (cl > ce) {
            sel = 2;
            corr = S / 8;
        }
        const float *w = sel == 0 ? win : sel == 1 ? win + S : win + 2 * S;
        float base = TONE_BASE_HZ + afc_off;
        float c_re[NBANK], c_im[NBANK];
        goertzel_bins_wide(w, N, base, SAMPLE_RATE_HZ, c_re, c_im);
        static float p_re[NBANK], p_im[NBANK];
        float conf = 0;
        int nib = -1;
        if (first) {
            first = 0;
        } else {
            nib = ifk_detect_wide(p_re, p_im, c_re, c_im, &conf);
        }
        memcpy(p_re, c_re, sizeof(p_re));
        memcpy(p_im, c_im, sizeof(p_im));
        if (!first && conf < 4.0f && nib >= 0) {
            bad++;
        } else if (!first) {
            bad = 0;
        }
        if (bad > 8) {
            int hs = i > 8 * N ? i - 8 * N : 0;
            afc_off = afc_acquire_wide(s + hs, 8 * N, SAMPLE_RATE_HZ);
            bad = 0;
            first = 1;
        }
        if (!first && nib >= 0) {
            int v;
            if (dominoex_rx_nibble(nib, &v)) {
                if (v < 0) {
                } else if (v & 0x100) {
                    if ((v & 0xFF) && nsec < (int)sizeof(sec) - 1) {
                        sec[nsec++] = v & 0xFF;
                    }
                } else {
                    got[ngot++] = v;
                }
            }
        }
        si++;
        memmove(win, win + N + corr, (2 * S - corr) * sizeof(float));
        fill = 2 * S - corr;
        need = N + 2 * S;
    }
    got[ngot] = 0;
    sec[nsec] = 0;

    printf("sent: %s\n", msg);
    printf("got:  %s\n", got);
    printf("sec:  %s\n", sec);
    printf("afc=%.1fHz syms=%d\n", afc_off, si);
    if (strstr(got, msg)) {
        printf("LOOPBACK PASS\n");
        return 0;
    }
    printf("LOOPBACK FAIL\n");
    return 1;
}
