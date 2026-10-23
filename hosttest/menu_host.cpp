// menu_host.cpp -- PC preview of the native menu and the USB screens.
//   SCREEN=menu|usb|start  KEYS=f:bits,..  SHOTS=a,b  FRAMES=n  ERR=text
//   PF_ROOT=dir  ./menu_host [last-cart-path]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <vector>
extern "C" {
#include "gfx.h"
}
#include "menu.h"
#include "plat.h"

static int frame, max_frames = 200;
static uint64_t vtime;
static std::vector<int> shots;

uint64_t plat_time_us(void) { return vtime; }
void plat_sleep_until_us(uint64_t t) { if (t > vtime) vtime = t; }
uint32_t plat_buttons(void)
{
    const char *k = getenv("KEYS");
    uint32_t bits = 0;
    while (k && *k) {
        int f = atoi(k);
        const char *c = strchr(k, ':');
        if (!c) break;
        if (frame >= f) bits = (uint32_t)atoi(c + 1);
        k = strchr(c, ',');
        if (k) k++;
    }
    return bits;
}
extern "C" void *pbc_malloc(size_t n) { return malloc(n); }
extern "C" void pbc_free(void *p) { free(p); }

extern "C" void host_frame(void)
{
    frame++;
    vtime += 1000;                      // drawing time
    for (int s : shots)
        if (s == frame) {
            char name[64];
            snprintf(name, sizeof name, "menu_%04d.ppm", frame);
            FILE *fp = fopen(name, "wb");
            fprintf(fp, "P6 %d %d 255\n", GFX_W, GFX_H);
            for (int i = 0; i < GFX_W * GFX_H; i++) {
                uint16_t v = gfx_fb[i];
                unsigned char px[3] = { (unsigned char)((v & 31) << 3),
                    (unsigned char)(((v >> 5) & 63) << 2), (unsigned char)(((v >> 11) & 31) << 3) };
                fwrite(px, 1, 3, fp);
            }
            fclose(fp);
            printf("wrote %s\n", name);
        }
    if (frame >= max_frames) { printf("frames done\n"); fflush(stdout); _exit(0); }
}

int main(int argc, char **argv)
{
    if (getenv("FRAMES")) max_frames = atoi(getenv("FRAMES"));
    for (const char *s = getenv("SHOTS"); s && *s; ) {
        shots.push_back(atoi(s));
        s = strchr(s, ',');
        if (s) s++;
    }
    gfx_init();
    const char *scr = getenv("SCREEN") ? getenv("SCREEN") : "menu";
    if (!strcmp(scr, "usb")) {
        for (unsigned t = 0;; t += 33)
            usb_screen(frame < 30 ? 0 : frame < 60 ? 1 : 2, "3 Carts \xfa 13,1 MB frei", t);
    }
    if (!strcmp(scr, "start")) {
        menu_starting(argc > 1 ? argv[1] : "/celeste.p8.png");
        return 0;
    }
    char path[256];
    snprintf(path, sizeof path, "%s", argc > 1 ? argv[1] : "");
    menu_result_t r = menu_run(path, sizeof path, getenv("ERR") ? getenv("ERR") : "");
    printf("result %d path %s\n", (int)r, path);
}
