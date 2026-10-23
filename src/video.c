/* video.c -- see video.h */
#include <string.h>
#include "video.h"
#include "color565.h"

#ifndef VIDEO_FAST
#define VIDEO_FAST
#endif

static uint16_t pal565[144];

video_rect_t video_geometry(int scale)
{
    video_rect_t r;
    switch (scale) {
    case VIDEO_240:     r.x = 0;  r.y = 20; r.w = 240; r.h = 240; break;
    case VIDEO_256CROP: r.x = 0;  r.y = 12; r.w = 240; r.h = 256; break;
    default:            r.x = 56; r.y = 76; r.w = 128; r.h = 128; break;
    }
    return r;
}

void video_set_palette(const uint8_t rgb[144][3])
{
    for (int i = 0; i < 144; i++)
        pal565[i] = PBC_RGB(rgb[i][0], rgb[i][1], rgb[i][2]);
}

/* panel row -> PICO-8 row */
static inline int src_row(int scale, int o)
{
    switch (scale) {
    case VIDEO_240:     return o * 8 / 15;
    case VIDEO_256CROP: return o >> 1;
    default:            return o;
    }
}

static inline uint8_t nib(const uint8_t *fb, int x, int y)
{
    uint8_t b = fb[y * 64 + (x >> 1)];
    return (x & 1) ? b >> 4 : b & 15;
}

/* PICO-8 screen modes (0x5f2c): stretch, mirror, flip, rotate.
 * Returns the framebuffer pixel shown at screen position (x, y). */
static inline uint8_t pixel_at(const video_frame_t *f, int x, int y)
{
    switch (f->mode) {
    case 1: x >>= 1; break;
    case 2: y >>= 1; break;
    case 3: x >>= 1; y >>= 1; break;
    case 5: if (x >= 64) x = 127 - x; break;
    case 6: if (y >= 64) y = 127 - y; break;
    case 7: if (x >= 64) x = 127 - x; if (y >= 64) y = 127 - y; break;
    case 129: x = 127 - x; break;
    case 130: y = 127 - y; break;
    case 131: x = 127 - x; y = 127 - y; break;
    case 133: { int t = x; x = y; y = 127 - t; } break;
    case 134: x = 127 - x; y = 127 - y; break;
    case 135: { int t = x; x = 127 - y; y = t; } break;
    default: break;
    }
    return nib(f->fb, x, y);
}

VIDEO_FAST void video_render_line(const video_frame_t *f, int scale, int oy, uint16_t *out)
{
    uint16_t col[16], row[128];
    for (int i = 0; i < 16; i++) {
        uint8_t v = f->palmap[i] & 0x8f;
        col[i] = pal565[v >= 128 ? 128 + (v & 15) : v & 15];
    }

    int sy = src_row(scale, oy);
    if (f->mode == 0) {
        const uint8_t *src = &f->fb[sy * 64];
        for (int x = 0; x < 128; x += 2) {
            uint8_t b = src[x >> 1];
            row[x] = col[b & 15];
            row[x + 1] = col[b >> 4];
        }
    } else {
        for (int x = 0; x < 128; x++)
            row[x] = col[pixel_at(f, x, sy)];
    }

    switch (scale) {
    case VIDEO_240:
        /* 15 panel pixels per 8 source pixels: 7 doubled, the 8th single,
         * the same pattern as src_row() gives for the rows */
        for (int sx = 0, ox = 0; sx < 128; sx += 8) {
            const uint16_t *r = &row[sx];
            out[ox++] = r[0]; out[ox++] = r[0];
            out[ox++] = r[1]; out[ox++] = r[1];
            out[ox++] = r[2]; out[ox++] = r[2];
            out[ox++] = r[3]; out[ox++] = r[3];
            out[ox++] = r[4]; out[ox++] = r[4];
            out[ox++] = r[5]; out[ox++] = r[5];
            out[ox++] = r[6]; out[ox++] = r[6];
            out[ox++] = r[7];
        }
        break;
    case VIDEO_256CROP:
        for (int ox = 0; ox < 240; ox += 2)
            out[ox] = out[ox + 1] = row[(ox >> 1) + 4];
        break;
    default:
        memcpy(out, row, sizeof row);
        break;
    }
}
