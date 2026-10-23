/* input.h -- joystick + A/B, active low with pull-ups. Bits as in plat.h. */
#ifndef PBC_INPUT_H
#define PBC_INPUT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void     input_init(void);
uint32_t input_raw(void);
#ifdef __cplusplus
}
#endif
#endif
