#pragma once

float goertzel_mag(const float *s, int n, float target_hz, float fs);
int detect_tone(const float *s, int n, float fs);
void afc_reset(float *offset);
int tone_detect_at(const float *s, int n, float fs, float offset_hz, float *conf);
int detect_tone_afc(const float *s, int n, float fs, float *offset, float *conf);
float afc_acquire(const float *s, int n, float fs);
float afc_acquire_span(const float *s, int n, int stride, float fs);
void goertzel_bins_wide(const float *s, int n, float base_hz, float fs, float *re, float *im);
float afc_acquire_wide(const float *s, int total_n, float fs);
int ifk_detect_wide(const float *p_re, const float *p_im,
                    const float *c_re, const float *c_im, float *conf);
int tone_detect_wide(const float *s, int n, float fs, float offset_hz, float *conf);
