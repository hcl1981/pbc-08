/* video.h -- PICO-8 screen (128x128, 4 bpp) to the 240x280 panel.
 *
 * Three scalings, chosen in the pause menu or the settings cart:
 *   VIDEO_1TO1     128x128, centred, one panel pixel per PICO-8 pixel
 *   VIDEO_240      240x240, nearest neighbour (15 of 16 pixels doubled)
 *   VIDEO_256CROP  256x256 (exactly 2x), 8 columns cut on each side
 *
 * The VM posts finished frames; the display core turns them into panel
 * lines. Shared by the firmware and the PC test build. */
#ifndef PBC_VIDEO_H
#define PBC_VIDEO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { VIDEO_1TO1 = 0, VIDEO_240 = 1, VIDEO_256CROP = 2, VIDEO_NSCALES = 3 };

typedef struct {
    uint8_t fb[128 * 64];       /* 2 pixels per byte, left pixel low nibble */
    uint8_t palmap[16];         /* screen palette, values 0..15 or 128..143 */
    uint8_t mode;               /* 0x5f2c screen mode */
} video_frame_t;

typedef struct { int x, y, w, h; } video_rect_t;

video_rect_t video_geometry(int scale);

/* 144-entry panel palette (16 standard, 112 unused, 16 secret colours). */
void video_set_palette(const uint8_t rgb[144][3]);

/* One panel line oy (0 .. h-1) of the picture rect of `scale`. */
void video_render_line(const video_frame_t *f, int scale, int oy, uint16_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PBC_VIDEO_H */
