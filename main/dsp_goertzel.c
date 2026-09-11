#include "dsp_goertzel.h"
#include "config.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float goertzel_power(const float *s, int n, float target_hz, float fs)
{
    float w = 2.0f * (float)M_PI * target_hz / fs;
    float coeff = 2.0f * cosf(w);
    float s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        s0 = s[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    float p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return p < 0 ? 0 : p;
}

float goertzel_mag(const float *s, int n, float target_hz, float fs)
{
    float w = 2.0f * (float)M_PI * target_hz / fs;
    float cw = cosf(w);
    float coeff = 2.0f * cw;
    float s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        s0 = s[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    float p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return p < 0 ? 0 : sqrtf(p);
}

int detect_tone(const float *s, int n, float fs)
{
    int best = 0;
    float best_m = -1;
    float base = TONE_BASE_HZ;
    for (int i = 0; i < NUM_TONES; i++) {
        float f = base + i * TONE_SPACING_HZ;
        float m = goertzel_mag(s, n, f, fs);
        if (m > best_m) {
            best_m = m;
            best = i;
        }
    }
    return best;
}

#define AFC_MAX_HZ 250.0f
#define AFC_SLEW_HZ 4.0f
#define AFC_GAIN 0.4f

void afc_reset(float *offset)
{
    *offset = 0;
}

int tone_detect_at(const float *s, int n, float fs, float offset_hz, float *conf)
{
    float base = TONE_BASE_HZ + offset_hz;
    int best = 0;
    float best_p = -1, second_p = 0, total = 0;
    for (int i = 0; i < NUM_TONES; i++) {
        float p = goertzel_power(s, n, base + i * TONE_SPACING_HZ, fs);
        total += p;
        if (p > best_p) {
            second_p = best_p;
            best_p = p;
            best = i;
        } else if (p > second_p) {
            second_p = p;
        }
    }
    (void)total;
    *conf = best_p / (second_p + 1e-6f);
    return best;
}

int detect_tone_afc(const float *s, int n, float fs, float *offset, float *conf)
{
    float base = TONE_BASE_HZ + *offset;
    int best = tone_detect_at(s, n, fs, *offset, conf);
    float bf = base + best * TONE_SPACING_HZ;
    float half = TONE_SPACING_HZ * 0.5f;
    float hi = goertzel_power(s, n, bf + half, fs);
    float lo = goertzel_power(s, n, bf - half, fs);
    float err = (hi - lo) / (hi + lo + 1e-6f);
    float step = AFC_GAIN * err * TONE_SPACING_HZ;
    if (step > AFC_SLEW_HZ) {
        step = AFC_SLEW_HZ;
    } else if (step < -AFC_SLEW_HZ) {
        step = -AFC_SLEW_HZ;
    }
    *offset += step;
    if (*offset > AFC_MAX_HZ) {
        *offset = AFC_MAX_HZ;
    } else if (*offset < -AFC_MAX_HZ) {
        *offset = -AFC_MAX_HZ;
    }
    return best;
}

void goertzel_bins_wide(const float *s, int n, float base_hz, float fs, float *re, float *im)
{
    for (int i = 0; i < NBANK; i++) {
        float w = 2.0f * (float)M_PI * (base_hz + (i - 9) * TONE_SPACING_HZ) / fs;
        float cw = cosf(w);
        float sw = sinf(w);
        float coeff = 2.0f * cw;
        float s0 = 0, s1 = 0, s2 = 0;
        for (int k = 0; k < n; k++) {
            s0 = s[k] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        re[i] = s1 - s2 * cw;
        im[i] = s2 * sw;
    }
}

int ifk_detect_wide(const float *p_re, const float *p_im,
                    const float *c_re, const float *c_im, float *conf)
{
    float best_m = -1, second_m = 0;
    int best_k = -1;
    for (int k = 2; k <= 17; k++) {
        double sr = 0, si = 0;
        for (int i = 0; i + k < NBANK; i++) {
            int j = i + k;
            sr += (double)p_re[i] * c_re[j] + (double)p_im[i] * c_im[j];
            si += (double)p_re[i] * c_im[j] - (double)p_im[i] * c_re[j];
        }
        for (int i = 18 - k; i < NBANK; i++) {
            int j = i + k - 18;
            sr += (double)p_re[i] * c_re[j] + (double)p_im[i] * c_im[j];
            si += (double)p_re[i] * c_im[j] - (double)p_im[i] * c_re[j];
        }
        double m = sr * sr + si * si;
        if (m > best_m) {
            second_m = best_m;
            best_m = m;
            best_k = k;
        } else if (m > second_m) {
            second_m = m;
        }
    }
    *conf = best_m / (second_m + 1e-9f);
    if (best_k < 0) {
        return -1;
    }
    return best_k - 2;
}

int tone_detect_wide(const float *s, int n, float fs, float offset_hz, float *conf)
{
    float base = TONE_BASE_HZ + offset_hz;
    int best = 0;
    float best_p = -1, second_p = 0;
    for (int i = 0; i < NBANK; i++) {
        float p = goertzel_power(s, n, base + (i - 9) * TONE_SPACING_HZ, fs);
        if (p > best_p) {
            second_p = best_p;
            best_p = p;
            best = i;
        } else if (p > second_p) {
            second_p = p;
        }
    }
    *conf = best_p / (second_p + 1e-6f);
    return best;
}

int ifk_detect(const float *p_re, const float *p_im,
               const float *c_re, const float *c_im, float *conf)
{
    float best_m = -1, second_m = 0;
    int best_k = -1;
    for (int k = 2; k <= 17; k++) {
        float sr = 0, si = 0;
        for (int i = 0; i < NUM_TONES; i++) {
            int j = (i + k) % NUM_TONES;
            sr += p_re[i] * c_re[j] + p_im[i] * c_im[j];
            si += p_re[i] * c_im[j] - p_im[i] * c_re[j];
        }
        float m = sr * sr + si * si;
        if (m > best_m) {
            second_m = best_m;
            best_m = m;
            best_k = k;
        } else if (m > second_m) {
            second_m = m;
        }
    }
    *conf = best_m / (second_m + 1e-9f);
    if (best_k < 0) {
        return -1;
    }
    return best_k - 2;
}

float afc_acquire_wide(const float *s, int total_n, float fs)
{
    int n = (int)(8000 / 10.766f);
    float best_off = 0, best_e = -1;
    for (float off = -100.0f; off <= 100.0f; off += 10.0f) {
        float base = TONE_BASE_HZ + off;
        double e = 0;
        for (int st = 0; st + n <= total_n; st += n) {
            for (int i = 0; i < NUM_TONES; i++) {
                e += goertzel_power(s + st, n, base + i * TONE_SPACING_HZ, fs);
            }
        }
        if (e > best_e) {
            best_e = e;
            best_off = off;
        }
    }
    return best_off;
}

float afc_acquire_span(const float *s, int n, int stride, float fs)
{
    float best_off = 0, best_m = -1;
    for (float off = -200.0f; off <= 200.0f; off += 20.0f) {
        float base = TONE_BASE_HZ + off;
        for (int p = 0; p <= 2 * stride; p += stride) {
            float peak = 0;
            for (int i = 0; i < NUM_TONES; i++) {
                float v = goertzel_power(s + p, n, base + i * TONE_SPACING_HZ, fs);
                if (v > peak) {
                    peak = v;
                }
            }
            if (peak > best_m) {
                best_m = peak;
                best_off = off;
            }
        }
    }
    return best_off;
}

float afc_acquire(const float *s, int n, float fs)
{
    float best_off = 0, best_m = -1;
    for (float off = -200.0f; off <= 200.0f; off += 20.0f) {
        float base = TONE_BASE_HZ + off;
        float peak = 0;
        for (int i = 0; i < NUM_TONES; i++) {
            float p = goertzel_power(s, n, base + i * TONE_SPACING_HZ, fs);
            if (p > peak) {
                peak = p;
            }
        }
        if (peak > best_m) {
            best_m = peak;
            best_off = off;
        }
    }
    return best_off;
}
