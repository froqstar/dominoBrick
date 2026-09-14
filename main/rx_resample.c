#include "rx_resample.h"

#define RS_GRID_US 125
#define RS_SPIKE 450

void rs_reset(rs_t *s)
{
    s->n = 0;
    s->next_us = 0;
    s->init = 0;
}

static int16_t med3(int16_t a, int16_t b, int16_t c)
{
    if ((a <= b) != (a <= c)) {
        return a;
    }
    if ((b <= a) != (b <= c)) {
        return b;
    }
    return c;
}

void rs_feed1(rs_t *s, int16_t v, int64_t t_us)
{
    if (s->n < 24) {
        s->r[s->n] = v;
        s->t[s->n] = t_us;
        s->n++;
    } else {
        for (int k = 0; k < 23; k++) {
            s->r[k] = s->r[k + 1];
            s->t[k] = s->t[k + 1];
        }
        s->r[23] = v;
        s->t[23] = t_us;
    }
    if (!s->init) {
        s->next_us = s->t[0];
        s->init = 1;
    }
}

void rs_feed(rs_t *s, const int16_t raw[8], int64_t t0_us, int64_t t1_us)
{
    for (int i = 0; i < 8; i++) {
        int16_t v = raw[i];
        int64_t t = t0_us + (t1_us - t0_us) * i / 7;
        if (s->n < 24) {
            s->r[s->n] = v;
            s->t[s->n] = t;
            s->n++;
        } else {
            for (int k = 0; k < 23; k++) {
                s->r[k] = s->r[k + 1];
                s->t[k] = s->t[k + 1];
            }
            s->r[23] = v;
            s->t[23] = t;
        }
    }
    if (!s->init) {
        s->next_us = s->t[0];
        s->init = 1;
    }
}

int rs_pop(rs_t *s, int16_t *out)
{
    if (!s->init || s->n < 2) {
        return 0;
    }
    if (s->next_us > s->t[s->n - 1]) {
        return 0;
    }
    if (s->next_us < s->t[0]) {
        s->next_us = s->t[0];
    }
    int i = 0;
    while (i + 1 < s->n && s->t[i + 1] < s->next_us) {
        i++;
    }
    if (i + 1 >= s->n) {
        *out = s->r[s->n - 1];
    } else {
        int64_t dt = s->t[i + 1] - s->t[i];
        if (dt <= 0) {
            *out = s->r[i];
        } else {
            int64_t f = s->next_us - s->t[i];
            *out = (int16_t)(s->r[i] + (s->r[i + 1] - s->r[i]) * f / dt);
        }
    }
    s->next_us += RS_GRID_US;
    return 1;
}
