/* input.c -- see input.h */
#include "pico/stdlib.h"
#include "board.h"
#include "input.h"

/* order = bit order of plat.h: up, down, left, right, A, B, centre */
static const uint8_t pins[] = {
    PBC_PIN_UP, PBC_PIN_DOWN, PBC_PIN_LEFT, PBC_PIN_RIGHT,
    PBC_PIN_A, PBC_PIN_B, PBC_PIN_CENTER,
};

void input_init(void)
{
    for (unsigned i = 0; i < sizeof pins; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
    }
    sleep_us(50);   /* let the pull-ups settle before the first read */
}

uint32_t input_raw(void)
{
    uint32_t all = gpio_get_all();
    uint32_t r = 0;
    for (unsigned i = 0; i < sizeof pins; i++)
        if (!(all & (1u << pins[i])))
            r |= 1u << i;
    return r;
}
