/* audio.h -- PICO-8 sound on the speaker at GP15 (PWM + RC low-pass).
 *
 * A DMA channel paced by a DMA timer copies one sample per tick (22050 Hz,
 * PICO-8's own rate) from a ring into the PWM compare register, endlessly,
 * without an interrupt per sample. audio_service() is called often from
 * the display core and keeps the ring about AUDIO_AHEAD samples ahead of
 * the DMA by asking fake-08 for more. */
#ifndef PBC_AUDIO_H
#define PBC_AUDIO_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RATE   22050

/* After the final system clock is set. */
void audio_init(void);

/* Fill function: writes n mono samples. */
typedef void (*audio_fill_fn)(int16_t *dst, int n);
void audio_start(audio_fill_fn fill);

/* Call at least every few ms. Does nothing until audio_start(). */
void audio_service(void);

/* stops the DMA and parks the pin low (before a restart) */
void audio_stop(void);

/* 0 .. 8, 4 = unity */
void audio_set_volume(int v);

#ifdef __cplusplus
}
#endif
#endif
