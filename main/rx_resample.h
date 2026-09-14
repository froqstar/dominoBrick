#pragma once
#include <stdint.h>

// Uniform 8 kHz sampler from bursty ADC batches. Pure C, no IDF deps.
// Batches carry wall-clock timestamps; outputs are linear-interpolated
// onto an exact 125 us grid. Spikes (>450-count single steps, beyond
// any legit 1 kHz slew at our levels) are median-replaced on ingest.
typedef struct {
    int16_t r[24];
    int64_t t[24];
    int n;
    int64_t next_us;
    int init;
} rs_t;

void rs_reset(rs_t *s);
void rs_feed(rs_t *s, const int16_t raw[8], int64_t t0_us, int64_t t1_us);
void rs_feed1(rs_t *s, int16_t v, int64_t t_us);
int rs_pop(rs_t *s, int16_t *out);
