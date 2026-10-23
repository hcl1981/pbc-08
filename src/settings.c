/* settings.c -- see settings.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "pfile.h"
#include "video.h"

#define SETTINGS_FILE "/pico8.cfg"

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int read_file(char *buf, int n)
{
    pfile *f = pf_open_read(SETTINGS_FILE);
    if (!f) return 0;
    int got = pf_read(f, buf, n - 1);
    pf_close(f);
    if (got < 0) got = 0;
    buf[got] = 0;
    return got;
}

void settings_load(pbc_settings_t *s)
{
    char buf[256];
    s->scale = VIDEO_240;
    s->volume = 6;
    s->menustyle = 0;
    s->bgcolor = 0;
    if (!read_file(buf, sizeof buf))
        return;
    for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        int v = atoi(eq + 1);
        *eq = 0;
        if (strstr(line, "stretch"))        s->scale = clampi(v, 0, VIDEO_NSCALES - 1);
        else if (strstr(line, "volume"))    s->volume = clampi(v, 0, SETTINGS_VOLUME_MAX);
        else if (strstr(line, "menustyle")) s->menustyle = v;
        else if (strstr(line, "bgcolor"))   s->bgcolor = v;
    }
}

void settings_save(const pbc_settings_t *s)
{
    char now[256], text[160];
    int n = snprintf(text, sizeof text,
                     "stretch = %d\nvolume = %d\nmenustyle = %d\nbgcolor = %d\n",
                     s->scale, s->volume, s->menustyle, s->bgcolor);
    if (read_file(now, sizeof now) == n && memcmp(now, text, (size_t)n) == 0)
        return;
    pfile *f = pf_open_write(SETTINGS_FILE);
    if (!f) return;
    pf_write(f, text, n);
    pf_close(f);
}
