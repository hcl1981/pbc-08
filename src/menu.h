/* menu.h -- the native cart menu and the USB stick screens.
 *
 * Drawn with gfx.c (full 240x280 frame buffers, true colour, Lato), outside
 * the PICO-8 VM: the VM starts afterwards on a heap of its own, and leaving
 * a cart restarts the device into this menu. */
#ifndef PBC_MENU_H
#define PBC_MENU_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MENU_PLAY, MENU_USB } menu_result_t;

/* path: in the last cart played ("" if none), out the cart to start.
 * error: shown first as a card, if not empty. gfx_init() must have run. */
menu_result_t menu_run(char *path, size_t n, const char *error);

/* "starting <cart>" with its label, while the VM gets going */
void menu_starting(const char *path);

/* USB stick mode. state: 0 no cable, 1 connected, 2 drive in use.
 * info: e.g. "3 Carts  -  13,1 MB frei". t_ms animates. */
void usb_screen(int state, const char *info, unsigned t_ms);
void usb_screen_message(const char *title, const char *line);

#ifdef __cplusplus
}
#endif

#endif /* PBC_MENU_H */
