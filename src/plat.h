/* plat.h -- what pbc_host.cpp needs from the machine. Implemented by
 * main_pbc.cpp on the PicoBoy and by hosttest/plat_host.cpp on the PC. */
#ifndef PBC_PLAT_H
#define PBC_PLAT_H

#include <stdint.h>
#include "video.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Button bits */
enum {
    PB_UP = 1u << 0, PB_DOWN = 1u << 1, PB_LEFT = 1u << 2, PB_RIGHT = 1u << 3,
    PB_A = 1u << 4, PB_B = 1u << 5, PB_CENTER = 1u << 6,
};

uint64_t plat_time_us(void);
void     plat_sleep_until_us(uint64_t t);
uint32_t plat_buttons(void);

/* Hand a finished frame to the display. Returns at once; the frame is
 * copied. */
void     plat_present(const uint8_t *fb, const uint8_t *palmap, uint8_t mode);
void     plat_set_scale(int scale);

/* Start sound; the samples are pulled from fake-08's Audio object. */
void     plat_audio_start(void *audio);
void     plat_set_volume(int volume);          /* 0..8, 4 = unity */

/* Leave the VM for the cart menu, showing `error` there if not empty.
 * Never returns. */
void     plat_exit_to_menu(const char *error) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* PBC_PLAT_H */
