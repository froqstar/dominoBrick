#pragma once
#include <stdbool.h>
#include <stdint.h>

int dominoex_tx_nibbles(unsigned char c, int secondary, uint8_t *out);
int dominoex_ifk_forward(int prev_tone, int nib);
void dominoex_rx_reset(void);
int dominoex_rx_nibble(int nib, int *out_val);
void domino_tx_set_typing(bool active);
int domino_tx_put(char c);
