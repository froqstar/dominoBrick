#include "audio_io.h"
#include "config.h"
#include "rx_resample.h"
#include "driver/gpio.h"
#include "driver/dac_continuous.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "audio_io";
static dac_continuous_handle_t dac_h;
static adc_oneshot_unit_handle_t adc_h;

#define FIFO_LEN 4096
#define DESC_SRC_CAP 1024
#define SILENCE_LEVEL 128
#define PIPE_FLUSH_DESCS 4

static uint8_t fifo[FIFO_LEN];
static size_t fifo_head;
static size_t fifo_tail;
static size_t fifo_count;
static uint32_t produced;
static uint32_t consumed;
static volatile uint32_t completed_events;
static SemaphoreHandle_t fifo_mux;
static SemaphoreHandle_t space_sem;
static SemaphoreHandle_t drain_sem;
static QueueHandle_t done_q;
static uint8_t stage[DESC_SRC_CAP];
static uint8_t silence_block[DESC_SRC_CAP];

#define SAMP_Q_LEN 512
#define SAMP_PERIOD_US 125

typedef struct {
    int16_t v;
    int64_t t;
} samp_t;

static QueueHandle_t samp_q;
static esp_timer_handle_t samp_timer;
static unsigned samp_dropped;

static void samp_cb(void *arg)
{
    (void)arg;
    int v = 2048;
    adc_oneshot_read(adc_h, PIN_ADC_RX, &v);
    samp_t s = { .v = (int16_t)v, .t = esp_timer_get_time() };
    if (xQueueSend(samp_q, &s, 0) != pdTRUE) {
        samp_t old;
        xQueueReceive(samp_q, &old, 0);
        xQueueSend(samp_q, &s, 0);
        samp_dropped++;
    }
}

static bool done_cb(dac_continuous_handle_t h, const dac_event_data_t *e, void *u)
{
    (void)h;
    QueueHandle_t q = (QueueHandle_t)u;
    BaseType_t woken = pdFALSE;
    dac_event_data_t evt = *e;
    xQueueSendFromISR(q, &evt, &woken);
    return woken;
}

static void feeder_task(void *a)
{
    (void)a;
    dac_event_data_t evt;
    for (;;) {
        if (xQueueReceive(done_q, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        completed_events++;
        size_t take = 0;
        xSemaphoreTake(fifo_mux, portMAX_DELAY);
        take = fifo_count > DESC_SRC_CAP ? DESC_SRC_CAP : fifo_count;
        for (size_t i = 0; i < take; i++) {
            stage[i] = fifo[fifo_tail];
            fifo_tail = (fifo_tail + 1) % FIFO_LEN;
        }
        fifo_count -= take;
        consumed += take;
        xSemaphoreGive(fifo_mux);
        size_t loaded = 0;
        if (take > 0) {
            dac_continuous_write_asynchronously(dac_h, evt.buf, evt.buf_size,
                                                stage, take, &loaded);
        } else {
            dac_continuous_write_asynchronously(dac_h, evt.buf, evt.buf_size,
                                                silence_block, sizeof(silence_block),
                                                &loaded);
        }
        xSemaphoreGive(space_sem);
        xSemaphoreGive(drain_sem);
    }
}

void audio_io_init(void)
{
    dac_continuous_config_t dc = {
        .chan_mask = DAC_CHANNEL_MASK_ALL,
        .desc_num = 4,
        .buf_size = 2048,
        .freq_hz = SAMPLE_RATE_HZ,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_APLL,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    ESP_ERROR_CHECK(dac_continuous_new_channels(&dc, &dac_h));
    fifo_mux = xSemaphoreCreateMutex();
    space_sem = xSemaphoreCreateCounting(32, 0);
    drain_sem = xSemaphoreCreateCounting(32, 0);
    done_q = xQueueCreate(10, sizeof(dac_event_data_t));
    memset(silence_block, SILENCE_LEVEL, sizeof(silence_block));
    dac_event_callbacks_t cbs = {
        .on_convert_done = done_cb,
        .on_stop = NULL,
    };
    ESP_ERROR_CHECK(dac_continuous_register_event_callback(dac_h, &cbs, done_q));
    ESP_ERROR_CHECK(dac_continuous_enable(dac_h));
    xTaskCreatePinnedToCore(feeder_task, "dacfeed", 3072, NULL, 4, NULL, 1);
    ESP_ERROR_CHECK(dac_continuous_start_async_writing(dac_h));

    adc_oneshot_unit_init_cfg_t au = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&au, &adc_h));
    adc_oneshot_chan_cfg_t ac = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_11,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_h, PIN_ADC_RX, &ac));
    samp_q = xQueueCreate(SAMP_Q_LEN, sizeof(samp_t));
    esp_timer_create_args_t st = {
        .callback = samp_cb,
        .name = "adc_samp",
    };
    ESP_ERROR_CHECK(esp_timer_create(&st, &samp_timer));
    gpio_set_direction(PIN_PTT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_PTT_GPIO, 1);
    ESP_LOGI(TAG, "audio ready fs=%d", SAMPLE_RATE_HZ);
}

static rs_t rs_state;
static int rs_started;
static int16_t outq[8];
static int outq_n, outq_i;

void audio_rx_start(void)
{
    ESP_ERROR_CHECK(esp_timer_start_periodic(samp_timer, SAMP_PERIOD_US));
}

int audio_rx_read(void)
{
    if (outq_i >= outq_n) {
        if (!rs_started) {
            rs_reset(&rs_state);
            rs_started = 1;
        }
        for (int k = 0; k < 8; k++) {
            samp_t s;
            if (xQueueReceive(samp_q, &s, pdMS_TO_TICKS(20)) != pdTRUE) {
                break;
            }
            rs_feed1(&rs_state, s.v, s.t);
        }
        outq_n = 0;
        outq_i = 0;
        int16_t o;
        while (outq_n < 8 && rs_pop(&rs_state, &o)) {
            outq[outq_n++] = o;
        }
        if (outq_n == 0) {
            outq[0] = 2048;
            outq_n = 1;
        }
    }
    return outq[outq_i++];
}

void audio_tx_stream(const uint8_t *buf, size_t len)
{
    while (len > 0) {
        xSemaphoreTake(fifo_mux, portMAX_DELAY);
        size_t free = FIFO_LEN - fifo_count;
        size_t n = len < free ? len : free;
        for (size_t i = 0; i < n; i++) {
            fifo[fifo_head] = buf[i];
            fifo_head = (fifo_head + 1) % FIFO_LEN;
        }
        fifo_count += n;
        produced += n;
        xSemaphoreGive(fifo_mux);
        buf += n;
        len -= n;
        if (len > 0 &&
            xSemaphoreTake(space_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
            ESP_LOGW(TAG, "fifo full, waiting");
        }
    }
}

void audio_tx_drain(void)
{
    for (;;) {
        uint32_t c, p;
        xSemaphoreTake(fifo_mux, portMAX_DELAY);
        c = consumed;
        p = produced;
        xSemaphoreGive(fifo_mux);
        if (c >= p) {
            break;
        }
        xSemaphoreTake(drain_sem, pdMS_TO_TICKS(200));
    }
    uint32_t flushed = completed_events + PIPE_FLUSH_DESCS;
    while ((int32_t)(completed_events - flushed) < 0) {
        if (xSemaphoreTake(drain_sem, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGW(TAG, "drain flush timeout");
            break;
        }
    }
}

void audio_tx_idle(void)
{
    uint8_t mid[8] = {128, 128, 128, 128, 128, 128, 128, 128};
    audio_tx_stream(mid, sizeof(mid));
}
