#include "cat.h"
#include "config.h"

#include <string.h>

#ifndef HOST_TEST
#include "ble_server.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
void ble_notify_freq(uint32_t hz);
#endif

#define CAT_CMD_SNOOP_FREQ 0x00
#define CAT_CMD_SNOOP_MODE 0x01
#define CAT_CMD_GET_FREQ 0x03
#define CAT_CMD_GET_MODE 0x04
#define CAT_CMD_SET_FREQ 0x05
#define CAT_CMD_SET_MODE 0x06
#define CAT_REPLY_OK 0xFB
#define CAT_REPLY_NG 0xFA
#define CAT_FRAME_MAX 16

void cat_freq_to_bcd(uint32_t hz, uint8_t out[5])
{
    out[0] = (uint8_t)(((hz / 10) % 10) << 4 | (hz % 10));
    out[1] = (uint8_t)(((hz / 1000) % 10) << 4 | ((hz / 100) % 10));
    out[2] = (uint8_t)(((hz / 100000) % 10) << 4 | ((hz / 10000) % 10));
    out[3] = (uint8_t)(((hz / 10000000) % 10) << 4 | ((hz / 1000000) % 10));
    out[4] = (uint8_t)(((hz / 1000000000) % 10) << 4 | ((hz / 100000000) % 10));
}

uint32_t cat_bcd_to_freq(const uint8_t b[5])
{
    uint32_t hz = 0;
    hz += (b[0] & 0x0F) * 1u + ((b[0] >> 4) & 0x0F) * 10u;
    hz += (b[1] & 0x0F) * 100u + ((b[1] >> 4) & 0x0F) * 1000u;
    hz += (b[2] & 0x0F) * 10000u + ((b[2] >> 4) & 0x0F) * 100000u;
    hz += (b[3] & 0x0F) * 1000000u + ((b[3] >> 4) & 0x0F) * 10000000u;
    hz += (b[4] & 0x0F) * 100000000u + ((b[4] >> 4) & 0x0F) * 1000000000u;
    return hz;
}

int cat_build_frame(uint8_t cmd, const uint8_t *data, int n, uint8_t *out)
{
    if (n < 0 || n > CAT_FRAME_MAX - 6) {
        return -1;
    }
    out[0] = 0xFE;
    out[1] = 0xFE;
    out[2] = CAT_RIG_ADDR;
    out[3] = CAT_CTRL_ADDR;
    out[4] = cmd;
    if (n > 0 && data != 0) {
        memcpy(out + 5, data, (size_t)n);
    }
    out[5 + n] = 0xFD;
    return 6 + n;
}

#ifdef HOST_TEST
void ble_notify_freq(uint32_t hz)
{
    (void)hz;
}
#else

static const char *TAG = "cat";
static portMUX_TYPE cat_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t cat_freq;
static uint8_t cat_mode = CAT_MODE_DOMINOEX;
static uint32_t cat_target;
static bool cat_pending;

static int frame_stage(uint8_t b, int stage)
{
    if (b == 0xFE) {
        return stage < 2 ? stage + 1 : stage;
    }
    return 0;
}

static int read_frame(uint8_t *out, int max, uint32_t timeout_ms)
{
    uint8_t b;
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    int stage = 0;
    int n = 0;
    while (xTaskGetTickCount() < end) {
        if (uart_read_bytes(CAT_UART_NUM, &b, 1, pdMS_TO_TICKS(20)) <= 0) {
            continue;
        }
        if (stage < 2) {
            stage = frame_stage(b, stage);
            continue;
        }
        if (stage == 2) {
            stage = (b == CAT_CTRL_ADDR || b == 0x00) ? 3 : frame_stage(b, 0);
            continue;
        }
        if (stage == 3) {
            stage = (b == CAT_RIG_ADDR) ? 4 : frame_stage(b, 0);
            n = 0;
            continue;
        }
        if (b == 0xFD) {
            return n;
        }
        if (n < max) {
            out[n++] = b;
        } else {
            stage = 0;
        }
    }
    return -1;
}

static void note_frame(const uint8_t *p, int n)
{
    if (n < 1) {
        return;
    }
    if ((p[0] == CAT_CMD_SNOOP_FREQ || p[0] == CAT_CMD_GET_FREQ) && n >= 6) {
        uint32_t hz = cat_bcd_to_freq(p + 1);
        taskENTER_CRITICAL(&cat_mux);
        int changed = hz != cat_freq;
        cat_freq = hz;
        taskEXIT_CRITICAL(&cat_mux);
        if (changed) {
            ble_notify_freq(hz);
        }
    } else if ((p[0] == CAT_CMD_SNOOP_MODE || p[0] == CAT_CMD_GET_MODE) && n >= 2) {
        taskENTER_CRITICAL(&cat_mux);
        cat_mode = p[1];
        taskEXIT_CRITICAL(&cat_mux);
    }
}

static int set_cmd(uint8_t cmd, const uint8_t *d, int n)
{
    uint8_t f[CAT_FRAME_MAX + 6];
    uint8_t p[CAT_FRAME_MAX];
    for (int t = 0; t < 3; t++) {
        uart_flush_input(CAT_UART_NUM);
        int len = cat_build_frame(cmd, d, n, f);
        uart_write_bytes(CAT_UART_NUM, (const char *)f, len);
        TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(CAT_RESP_TIMEOUT_MS);
        while (xTaskGetTickCount() < end) {
            int r = read_frame(p, sizeof(p), 100);
            if (r < 1) {
                continue;
            }
            if (p[0] == CAT_REPLY_OK || p[0] == cmd) {
                return 0;
            }
            if (p[0] == CAT_REPLY_NG) {
                break;
            }
            note_frame(p, r);
        }
    }
    return -1;
}

static int get_cmd(uint8_t cmd, uint8_t *d, int max)
{
    uint8_t f[CAT_FRAME_MAX + 6];
    uint8_t p[CAT_FRAME_MAX];
    uart_flush_input(CAT_UART_NUM);
    int len = cat_build_frame(cmd, 0, 0, f);
    uart_write_bytes(CAT_UART_NUM, (const char *)f, len);
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(CAT_RESP_TIMEOUT_MS);
    while (xTaskGetTickCount() < end) {
        int r = read_frame(p, sizeof(p), 100);
        if (r < 1) {
            continue;
        }
        if (p[0] == cmd && r >= 2) {
            int n = r - 1;
            if (n > max) {
                n = max;
            }
            memcpy(d, p + 1, (size_t)n);
            return n;
        }
        if (p[0] == CAT_REPLY_OK || p[0] == CAT_REPLY_NG) {
            return -1;
        }
        note_frame(p, r);
    }
    return -1;
}

static int set_raw(const uint8_t *pl, int pln)
{
    uint8_t f[CAT_FRAME_MAX + 6];
    uint8_t p[CAT_FRAME_MAX];
    if (pln < 1 || pln > CAT_FRAME_MAX - 5) {
        return -1;
    }
    for (int t = 0; t < 3; t++) {
        uart_flush_input(CAT_UART_NUM);
        f[0] = 0xFE;
        f[1] = 0xFE;
        f[2] = CAT_RIG_ADDR;
        f[3] = CAT_CTRL_ADDR;
        memcpy(f + 4, pl, (size_t)pln);
        f[4 + pln] = 0xFD;
        uart_write_bytes(CAT_UART_NUM, (const char *)f, (size_t)pln + 5);
        TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(CAT_RESP_TIMEOUT_MS);
        while (xTaskGetTickCount() < end) {
            int r = read_frame(p, sizeof(p), 100);
            if (r < 1) {
                continue;
            }
            if (p[0] == CAT_REPLY_OK) {
                return 0;
            }
            if (p[0] == CAT_REPLY_NG) {
                break;
            }
            note_frame(p, r);
        }
    }
    return -1;
}

static int get_sub(uint8_t cmd, uint8_t sub, uint8_t *d, int max)
{
    uint8_t f[CAT_FRAME_MAX + 6];
    uint8_t p[CAT_FRAME_MAX];
    uint8_t pl[2] = { cmd, sub };
    uart_flush_input(CAT_UART_NUM);
    f[0] = 0xFE;
    f[1] = 0xFE;
    f[2] = CAT_RIG_ADDR;
    f[3] = CAT_CTRL_ADDR;
    f[4] = cmd;
    f[5] = sub;
    f[6] = 0xFD;
    uart_write_bytes(CAT_UART_NUM, (const char *)f, 7);
    (void)pl;
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(CAT_RESP_TIMEOUT_MS);
    while (xTaskGetTickCount() < end) {
        int r = read_frame(p, sizeof(p), 100);
        if (r < 1) {
            continue;
        }
        if (p[0] == cmd && r >= 3 && p[1] == sub) {
            int n = r - 2;
            if (n > max) {
                n = max;
            }
            memcpy(d, p + 2, (size_t)n);
            return n;
        }
        if (p[0] == CAT_REPLY_OK || p[0] == CAT_REPLY_NG) {
            return -1;
        }
        note_frame(p, r);
    }
    return -1;
}

static void ensure_usb_data(void)
{
    uint8_t d[8];
    int n = get_cmd(CAT_CMD_GET_MODE, d, sizeof(d));
    uint8_t filt = 0x01;
    bool need_mode = true;
    if (n >= 2) {
        filt = d[1];
        need_mode = d[0] != CAT_MODE_DOMINOEX;
    } else if (n >= 1) {
        need_mode = d[0] != CAT_MODE_DOMINOEX;
    }
    if (need_mode) {
        uint8_t m[2] = { CAT_MODE_DOMINOEX, filt };
        if (set_cmd(CAT_CMD_SET_MODE, m, 2) != 0) {
            ESP_LOGW(TAG, "mode set 06 %02X %02X failed", m[0], m[1]);
        }
    }
    uint8_t dd[8];
    int nd = get_sub(0x1A, 0x06, dd, sizeof(dd));
    uint8_t dfilt = filt;
    bool need_data = true;
    if (nd >= 2) {
        dfilt = dd[1];
        need_data = dd[0] == 0x00;
    }
    if (need_data) {
        uint8_t pl[4] = { 0x1A, 0x06, 0x01, dfilt };
        if (set_raw(pl, sizeof(pl)) != 0) {
            ESP_LOGW(TAG, "data mode set 1A 06 01 %02X failed", dfilt);
        }
    }
}

static void do_target(uint32_t hz)
{
    ensure_usb_data();
    uint8_t bcd[5];
    cat_freq_to_bcd(hz, bcd);
    if (set_cmd(CAT_CMD_SET_FREQ, bcd, 5) != 0) {
        ESP_LOGW(TAG, "freq set failed");
        return;
    }
    uint8_t d[8];
    if (get_cmd(CAT_CMD_GET_FREQ, d, sizeof(d)) >= 5) {
        uint8_t p[7];
        p[0] = CAT_CMD_GET_FREQ;
        memcpy(p + 1, d, 5);
        note_frame(p, 6);
    }
}

static void snoop_drain(void)
{
    uint8_t p[CAT_FRAME_MAX];
    for (int i = 0; i < 4; i++) {
        int r = read_frame(p, sizeof(p), 30);
        if (r < 0) {
            return;
        }
        note_frame(p, r);
    }
}

static void cat_task(void *a)
{
    (void)a;
    uint8_t d[8];
    for (;;) {
        taskENTER_CRITICAL(&cat_mux);
        bool pend = cat_pending;
        uint32_t t = cat_target;
        cat_pending = false;
        taskEXIT_CRITICAL(&cat_mux);
        if (pend && t != 0) {
            do_target(t);
        }
        if (get_cmd(CAT_CMD_GET_FREQ, d, sizeof(d)) >= 5) {
            uint8_t p[7];
            p[0] = CAT_CMD_GET_FREQ;
            memcpy(p + 1, d, 5);
            note_frame(p, 6);
        }
        if (get_cmd(CAT_CMD_GET_MODE, d, sizeof(d)) >= 1) {
            taskENTER_CRITICAL(&cat_mux);
            cat_mode = d[0];
            taskEXIT_CRITICAL(&cat_mux);
        }
        snoop_drain();
        vTaskDelay(pdMS_TO_TICKS(CAT_POLL_INTERVAL_MS));
    }
}

int cat_request_freq(uint32_t hz)
{
    if (hz == 0) {
        return -1;
    }
    taskENTER_CRITICAL(&cat_mux);
    cat_target = hz;
    cat_pending = true;
    taskEXIT_CRITICAL(&cat_mux);
    return 0;
}

uint32_t cat_freq_get(void)
{
    taskENTER_CRITICAL(&cat_mux);
    uint32_t hz = cat_freq;
    taskEXIT_CRITICAL(&cat_mux);
    return hz;
}

void cat_init(void)
{
    uart_config_t cfg = {
        .baud_rate = CAT_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(CAT_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(CAT_UART_NUM, CAT_PIN_TX, CAT_PIN_RX, -1, -1));
    ESP_ERROR_CHECK(uart_driver_install(CAT_UART_NUM, 256, 256, 0, NULL, 0));
    xTaskCreatePinnedToCore(cat_task, "cat", 3072, NULL, 3, NULL, 0);
    ESP_LOGI(TAG, "CAT ready uart=%d tx=%d rx=%d", CAT_UART_NUM, CAT_PIN_TX, CAT_PIN_RX);
}

#endif
