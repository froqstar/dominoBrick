#pragma once
#include <stddef.h>
#include <stdint.h>

void audio_io_init(void);
void audio_rx_start(void);
int audio_rx_read(void);
void audio_tx_stream(const uint8_t *buf, size_t len);
void audio_tx_drain(void);
void audio_tx_idle(void);
