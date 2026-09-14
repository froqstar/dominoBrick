#include "audio_io.h"
#include "ble_server.h"
#include "cat.h"
#include "config.h"
#include "dominoex.h"
#include "dsp_goertzel.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ptt.h"
#include "tx_nco.h"
#include <stdbool.h>
#include <string.h>

static const char *TAG = "dominoBrick";
static QueueHandle_t tx_q;

static float tone_freq(int idx)
{
    float base = TONE_BASE_HZ;
    return base + idx * TONE_SPACING_HZ;
}

static void rx_fill(float *dst, int n, TickType_t *last)
{
    int got = 0;
    while (got < n) {
        vTaskDelayUntil(last, pdMS_TO_TICKS(1));
        for (int k = 0; k < 8 && got < n; k++) {
            dst[got++] = (float)audio_rx_read() - 2048.0f;
        }
    }
}

static void rx_task(void *a)
{
    (void)a;
    const int N = SAMPLES_PER_SYMBOL;
    const int S = N / 8;
    static float win[900 + 2 * 112];
    static float acq[8 * 900];
    static float p_re[NBANK], p_im[NBANK];
    float c_re[NBANK], c_im[NBANK];
    float afc_off = 0;
    int bad = 0, syms = 0, first = 1;
    int fill = 0, need = N + 2 * S, corr = 0;
    TickType_t last = xTaskGetTickCount();
    afc_reset(&afc_off);
    dominoex_rx_reset();
    rx_fill(acq, 8 * N, &last);
    afc_off = afc_acquire_wide(acq, 8 * N, SAMPLE_RATE_HZ);
    ESP_LOGI(TAG, "acquired afc=%.1fHz", afc_off);
    while (1) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1));
        for (int k = 0; k < 8 && fill < N + 2 * S; k++) {
            win[fill++] = (float)audio_rx_read() - 2048.0f;
        }
        if (fill < need) {
            continue;
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
        goertzel_bins_wide(w, N, base, SAMPLE_RATE_HZ, c_re, c_im);
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
            rx_fill(acq, 8 * N, &last);
            afc_off = afc_acquire_wide(acq, 8 * N, SAMPLE_RATE_HZ);
            ESP_LOGI(TAG, "re-acquired afc=%.1fHz", afc_off);
            bad = 0;
            first = 1;
        }
        if (!first && nib >= 0) {
            int v;
            if (dominoex_rx_nibble(nib, &v)) {
                if (v < 0) {
                } else if (v & 0x100) {
                    if (v & 0xFF) {
                        uint8_t c = v & 0xFF;
                        ESP_LOGI(TAG, "SEC: %c", c);
                        ble_notify_rx_secondary(&c, 1);
                    }
                } else {
                    uint8_t c = v;
                    ESP_LOGI(TAG, "RX: %c", c);
                    ble_notify_rx_primary(&c, 1);
                }
            }
        }
        if (++syms % 50 == 0) {
            ESP_LOGI(TAG, "afc=%.1fHz conf=%.1f tcorr=%d", afc_off, conf, corr);
        }
            memmove(win, win + N + corr, (2 * S - corr) * sizeof(float));
            fill = 2 * S - corr;
            need = N + 2 * S;
    }
}

static int tx_tone;

static void tx_send_nib(int nib)
{
    tx_tone = dominoex_ifk_forward(tx_tone, nib);
    nco_symbol(tone_freq(tx_tone), SAMPLES_PER_SYMBOL);
}

static void tx_send_char(unsigned char c, int secondary)
{
    uint8_t nibs[3];
    int n = dominoex_tx_nibbles(c, secondary, nibs);
    for (int i = 0; i < n; i++) {
        tx_send_nib(nibs[i]);
    }
}

#define SEC_TEXT_MAX 128
#define TX_Q_LEN 1024
#define TYPING_TIMEOUT_MS 3000

static char sec_text[SEC_TEXT_MAX] = "dominoBrick ";
static unsigned sec_idx;
static bool typing_flag;
static volatile TickType_t last_keystroke;
static portMUX_TYPE sec_mux = portMUX_INITIALIZER_UNLOCKED;

static bool typing_active(void)
{
    if (!typing_flag) {
        return false;
    }
    if (xTaskGetTickCount() - last_keystroke >
        pdMS_TO_TICKS(TYPING_TIMEOUT_MS)) {
        typing_flag = false;
        return false;
    }
    return true;
}

void domino_tx_set_typing(bool active)
{
    typing_flag = active;
    if (active) {
        last_keystroke = xTaskGetTickCount();
    }
}

int domino_ble_tx_append(const uint8_t *data, uint16_t len)
{
    if (len == 0) {
        return 0;
    }
    if (uxQueueSpacesAvailable(tx_q) < len) {
        return -1;
    }
    for (uint16_t i = 0; i < len; i++) {
        char c = data[i];
        xQueueSend(tx_q, &c, 0);
    }
    typing_flag = true;
    last_keystroke = xTaskGetTickCount();
    return 0;
}

void domino_ble_sec_set(const uint8_t *data, uint16_t len)
{
    if (len > SEC_TEXT_MAX - 1) {
        len = SEC_TEXT_MAX - 1;
    }
    taskENTER_CRITICAL(&sec_mux);
    memcpy(sec_text, data, len);
    sec_text[len] = 0;
    sec_idx = 0;
    taskEXIT_CRITICAL(&sec_mux);
}

uint16_t domino_ble_sec_get(uint8_t *dst, uint16_t max)
{
    taskENTER_CRITICAL(&sec_mux);
    size_t n = strlen(sec_text);
    if (n > max) {
        n = max;
    }
    memcpy(dst, sec_text, n);
    taskEXIT_CRITICAL(&sec_mux);
    return n;
}

int domino_tx_put(char c)
{
    return xQueueSend(tx_q, &c, 0) == pdTRUE ? 0 : -1;
}

static unsigned char sec_next(void)
{
    taskENTER_CRITICAL(&sec_mux);
    size_t n = strlen(sec_text);
    if (n == 0) {
        taskEXIT_CRITICAL(&sec_mux);
        return ' ';
    }
    unsigned char c = sec_text[sec_idx++];
    if (sec_idx >= n) {
        sec_idx = 0;
    }
    taskEXIT_CRITICAL(&sec_mux);
    return c;
}

static void tx_task(void *a)
{
    (void)a;
    char c;
    bool in_session = false;
    for (;;) {
        if (!in_session) {
            bool want = typing_active() || uxQueueMessagesWaiting(tx_q) > 0;
            if (!want) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            ptt_on();
            vTaskDelay(pdMS_TO_TICKS(PTT_LEAD_MS));
            tx_tone = 0;
            tx_send_char(0, 1);
            tx_send_char('\r', 0);
            tx_send_char(2, 0);
            tx_send_char('\r', 0);
            in_session = true;
        } else if (xQueueReceive(tx_q, &c, 0) == pdTRUE) {
            tx_send_char((unsigned char)c, 0);
            ble_notify_progress((uint8_t)c);
        } else if (typing_active()) {
            tx_send_char(sec_next(), 1);
        } else {
            tx_send_char('\r', 0);
            tx_send_char(4, 0);
            tx_send_char('\r', 0);
            for (int i = 0; i < 4; i++) {
                tx_send_char(0, 1);
            }
            audio_tx_drain();
            vTaskDelay(pdMS_TO_TICKS(PTT_TAIL_MS));
            ptt_off();
            in_session = false;
        }
    }
}

#define PIN_BOOT_BUTTON 0

static void button_task(void *a)
{
    (void)a;
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    TickType_t last_press = 0;
    int low_stable = 0;
    for (;;) {
        if (gpio_get_level(PIN_BOOT_BUTTON) == 0) {
            low_stable++;
        } else {
            low_stable = 0;
        }
        if (low_stable >= 2 &&
            xTaskGetTickCount() - last_press > pdMS_TO_TICKS(500)) {
            last_press = xTaskGetTickCount();
            low_stable = 0;
            ESP_LOGI(TAG, "BOOT button: queueing test message");
            domino_ble_tx_append((const uint8_t *)"DOMINOBRICK test", 16);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot reset_reason=%d heap=%lu", esp_reset_reason(),
             (unsigned long)esp_get_free_heap_size());
    audio_io_init();
    ptt_init();
    cat_init();
    tx_q = xQueueCreate(TX_Q_LEN, 1);
    xTaskCreatePinnedToCore(rx_task, "rx", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(tx_task, "tx", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(button_task, "btn", 2048, NULL, 4, NULL, 1);
    ble_server_init();
    audio_rx_start();
    ESP_LOGI(TAG, "dominoBrick ready");
}
