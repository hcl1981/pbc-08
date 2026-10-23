#ifndef PBC_PICO8_RUN_H
#define PBC_PICO8_RUN_H
#ifdef __cplusplus
extern "C" {
#endif
/* Never returns. autostart: cart path, or NULL for the cart menu. */
void pico8_run(const char *autostart);
#ifdef __cplusplus
}
#endif
#endif
