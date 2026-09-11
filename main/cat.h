#pragma once
#include <stdint.h>

void cat_init(void);
int cat_request_freq(uint32_t hz);
uint32_t cat_freq_get(void);

void cat_freq_to_bcd(uint32_t hz, uint8_t out[5]);
uint32_t cat_bcd_to_freq(const uint8_t b[5]);
int cat_build_frame(uint8_t cmd, const uint8_t *data, int n, uint8_t *out);
