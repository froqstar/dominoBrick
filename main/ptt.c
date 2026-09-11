#include "ptt.h"
#include "config.h"
#include "driver/gpio.h"

void ptt_init(void)
{
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << PIN_PTT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&c);
    gpio_set_level(PIN_PTT_GPIO, 1);
}

void ptt_on(void)
{
    gpio_set_level(PIN_PTT_GPIO, 0);
}

void ptt_off(void)
{
    gpio_set_level(PIN_PTT_GPIO, 1);
}
