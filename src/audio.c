/* audio.c -- see audio.h
 *
 * GP15 is PWM channel B of its slice. The DMA writes 16 bits to the CC
 * register (both halves); only B drives the pin. The DMA's read pointer
 * inside the ring tells exactly how far playback got, so there is no
 * second clock to reconcile. */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "board.h"
#include "audio.h"

#define PWM_WRAP   1023u                  /* 10 bit; ~244 kHz carrier at 250 MHz */
#define PWM_MID    (PWM_WRAP / 2)

#define RING_N     2048u                  /* 93 ms */
#define RING_MASK  (RING_N - 1)
#define RING_BITS  12                     /* 2048 x 2 bytes, aligned to that */
#define AUDIO_AHEAD 900                   /* ~41 ms: covers a late frame */
#define CHUNK      64

static uint16_t ring[RING_N] __attribute__((aligned(RING_N * 2)));
static uint32_t head;                     /* next ring index to write */
static uint slice;
static int  dma_ch = -1, dma_timer = -1;
static audio_fill_fn fill_fn;
static int  volume = 6;                   /* PICO-8 mixes quietly; 1.5x */

void audio_init(void)
{
    for (uint32_t i = 0; i < RING_N; i++)
        ring[i] = PWM_MID;

    slice = pwm_gpio_to_slice_num(PBC_PIN_AUDIO);
    pwm_config c = pwm_get_default_config();
    pwm_config_set_wrap(&c, PWM_WRAP);
    pwm_config_set_clkdiv(&c, 1.0f);
    pwm_init(slice, &c, true);
    pwm_set_chan_level(slice, PWM_CHAN_B, PWM_MID);
    gpio_set_function(PBC_PIN_AUDIO, GPIO_FUNC_PWM);

    dma_ch = dma_claim_unused_channel(true);
    dma_timer = dma_claim_unused_timer(true);

    /* DREQ rate = clk_sys * 1 / denom: 250 MHz / 11338 = 22049.7 Hz */
    uint32_t denom = (clock_get_hz(clk_sys) + AUDIO_RATE / 2) / AUDIO_RATE;
    if (denom > 65535) denom = 65535;
    dma_timer_set_fraction((uint)dma_timer, 1, (uint16_t)denom);

    dma_channel_config dc = dma_channel_get_default_config((uint)dma_ch);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_16);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_ring(&dc, false, RING_BITS);
    channel_config_set_dreq(&dc, dma_get_timer_dreq((uint)dma_timer));
    dma_channel_configure((uint)dma_ch, &dc, &pwm_hw->slice[slice].cc, ring,
                          dma_encode_endless_transfer_count(), true);
    head = 0;
}

void audio_start(audio_fill_fn fill)
{
    fill_fn = fill;
}

void audio_set_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 8) v = 8;
    volume = v;
}

static inline uint16_t to_level(int32_t s)
{
    s = s * volume / 4;
    /* compress peaks instead of clipping them */
    const int32_t th = 24000;
    if (s > th)  s = th + (s - th) / 8;
    if (s < -th) s = -th + (s + th) / 8;
    if (s > 32767)  s = 32767;
    if (s < -32768) s = -32768;
    return (uint16_t)((s + 32768) >> 6);  /* 16 -> 10 bit */
}

void __not_in_flash_func(audio_service)(void)
{
    if (!fill_fn || dma_ch < 0)
        return;
    uint32_t tail = ((uint32_t)dma_hw->ch[dma_ch].read_addr - (uint32_t)ring) / 2u & RING_MASK;
    uint32_t fill = (head - tail) & RING_MASK;
    if (fill > RING_N - 2 * CHUNK) {
        /* the DMA overtook us (VM stalled, flash write): restart just ahead */
        head = (tail + 8) & RING_MASK;
        fill = 8;
    }
    while (fill < AUDIO_AHEAD) {
        int16_t buf[CHUNK];
        fill_fn(buf, CHUNK);
        for (int i = 0; i < CHUNK; i++)
            ring[(head + i) & RING_MASK] = to_level(buf[i]);
        head = (head + CHUNK) & RING_MASK;
        fill += CHUNK;
    }
}

void audio_stop(void)
{
    fill_fn = NULL;
    if (dma_ch >= 0)
        dma_channel_abort((uint)dma_ch);
    gpio_init(PBC_PIN_AUDIO);             /* no idle current through the speaker */
    gpio_set_dir(PBC_PIN_AUDIO, GPIO_OUT);
    gpio_put(PBC_PIN_AUDIO, 0);
}
