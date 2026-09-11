#pragma once
#include <stdbool.h>
#include <stdint.h>

#define BLE_SVC_UUID_STR   "d0b10000-bbaa-9988-7766-554433221100"
#define BLE_TX_UUID_STR    "d0b10001-bbaa-9988-7766-554433221100"
#define BLE_PROG_UUID_STR  "d0b10002-bbaa-9988-7766-554433221100"
#define BLE_SEC_UUID_STR   "d0b10003-bbaa-9988-7766-554433221100"
#define BLE_RXPRI_UUID_STR "d0b10004-bbaa-9988-7766-554433221100"
#define BLE_RXSEC_UUID_STR "d0b10005-bbaa-9988-7766-554433221100"
#define BLE_FREQ_UUID_STR  "d0b10006-bbaa-9988-7766-554433221100"

void ble_server_init(void);
int ble_connected(void);
void ble_notify_progress(uint8_t c);
void ble_notify_rx_primary(const uint8_t *data, uint16_t len);
void ble_notify_rx_secondary(const uint8_t *data, uint16_t len);
void ble_notify_freq(uint32_t hz);

int domino_ble_tx_append(const uint8_t *data, uint16_t len);
void domino_ble_sec_set(const uint8_t *data, uint16_t len);
uint8_t domino_ble_progress_get(void);
uint16_t domino_ble_sec_get(uint8_t *dst, uint16_t max);
uint16_t domino_ble_rx_cache_get(int secondary, uint8_t *dst, uint16_t max);
