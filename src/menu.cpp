// menu.cpp -- see menu.h
//
// Looks like the PBC+ launcher (pbcemu): the same gradient, Lato and
// button hints, with PICO-8's colours as the accent. Everything is redrawn
// every frame into the frame buffer, so sliding and fading come for free.
// Strings are code page 437 (the font's and FatFs' encoding): \x84 is a
// lowercase a-umlaut, \x81 u-umlaut, \x94 o-umlaut, \xe1 sharp s, \xfa a dot.
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <strings.h>

extern "C" {
#include "gfx.h"
#include "color565.h"
}
#include "menu.h"
#include "plat.h"
#include "pfile.h"
#include "settings.h"
#include "video.h"
#include "cart.h"

// ---- look --------------------------------------------------------------------

#define BG_TOP     0x161d31
#define BG_BOTTOM  0x06080e
#define CARD       0x1d2539
#define TEXT       0xf2f4f8
#define DIM        0x8d96ab
#define FAINT      0x485168
#define ACCENT     0xff004d             // PICO-8 red
#define FOLDER     0xffa300             // PICO-8 orange
#define OK_GREEN   0x00e436

static const uint32_t stripe[8] = {     // the colours of the PICO-8 logo
    0xff004d, 0xffa300, 0xffec27, 0x00e436, 0x29adff, 0x83769c, 0xff77a8, 0xffccaa };

static const uint8_t pico_rgb[32][3] = {
    {0,0,0},{29,43,83},{126,37,83},{0,135,81},{171,82,54},{95,87,79},
    {194,195,199},{255,241,232},{255,0,77},{255,163,0},{255,236,39},
    {0,228,54},{41,173,255},{131,118,156},{255,119,168},{255,204,170},
    {41,24,20},{17,29,53},{66,33,54},{18,83,89},{116,47,41},{73,51,59},
    {162,136,121},{243,239,125},{190,18,80},{255,108,36},{168,231,46},
    {0,181,67},{6,90,181},{117,70,101},{255,110,89},{255,157,129}};

#define LABEL_Y    58                   // top of the centre label
#define SLIDE      140                  // distance between labels
#define FRAME_MS   33

static void background(void)
{
    gfx_background(BG_TOP, BG_BOTTOM);
}

// A button symbol (filled disc with its letter) followed by a label.
static int hint(int x, int y, const char *key, const char *label)
{
    gfx_circle(x + 7, y + 7, 7, 0xd5dae6, 255);
    if (key[0] == '*')                  // the centre button: a dot
        gfx_circle(x + 7, y + 7, 3, 0x1a2030, 255);
    else
        gfx_text_centered(&font_small, x + 7, y + 1, key, 0x1a2030, 255);
    x += 18;
    x += gfx_text(&font_small, x, y + 1, label, DIM, 255);
    return x + 14;
}

// ease-out: t 0..256 -> 0..256
static int ease(int t)
{
    int u = 256 - t;
    return 256 - (u * u * u >> 16);
}

static void logo_stripe(int x, int y, int w, int h, int alpha)
{
    for (int i = 0; i < 8; i++)
        gfx_rect(x + w * i / 8, y, w * (i + 1) / 8 - w * i / 8, h, stripe[i], alpha);
}

static void wait_release(void)
{
    while (plat_buttons())
        plat_sleep_until_us(plat_time_us() + 10000);
}

// Newly pressed buttons, with auto-repeat for the joystick.
static uint32_t edges(void)
{
    static uint32_t prev;
    static uint64_t repeat_at;
    const uint32_t dirs = PB_UP | PB_DOWN | PB_LEFT | PB_RIGHT;
    uint32_t r = plat_buttons();
    uint32_t e = r & ~prev;
    uint64_t now = plat_time_us();
    if (e & dirs)
        repeat_at = now + 350000;
    else if ((r & dirs) && now >= repeat_at) {
        e |= r & dirs;
        repeat_at = now + 90000;
    }
    prev = r;
    return e;
}

// Word-wrapped text; returns the y below it.
static int text_wrapped(const pbc_font_t *f, int x, int y, int w, const char *s,
                        uint32_t rgb, int max_lines)
{
    std::string line, word;
    int lines = 0;
    auto flush = [&]() {
        if (lines < max_lines)
            gfx_text(f, x, y + lines * f->height, line.c_str(), rgb, 255);
        lines++;
        line.clear();
    };
    for (const char *p = s;; p++) {
        if (*p == ' ' || *p == '\n' || *p == 0) {
            std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && gfx_text_w(f, cand.c_str()) > w) {
                flush();
                cand = word;
            }
            // a single word wider than the box is cut into pieces
            while (gfx_text_w(f, cand.c_str()) > w && cand.size() > 1) {
                size_t k = cand.size();
                while (k > 1 && gfx_text_w(f, cand.substr(0, k).c_str()) > w) k--;
                line = cand.substr(0, k);
                flush();
                cand = cand.substr(k);
            }
            line = cand;
            word.clear();
            if (*p == '\n') flush();
            if (*p == 0) break;
        } else {
            word += *p;
        }
    }
    if (!line.empty()) flush();
    return y + std::min(lines, max_lines) * f->height;
}

// ---- carts -----------------------------------------------------------------------

enum { SPECIAL_NONE, SPECIAL_OPTIONS, SPECIAL_USB };

struct Entry {
    std::string name;       // file or folder name
    bool dir = false;
    bool up = false;        // ".."
    int special = 0;        // SPECIAL_OPTIONS / SPECIAL_USB: cards after the carts
    long size = 0;
};

static bool ends_with(const std::string &s, const char *e)
{
    size_t n = strlen(e);
    return s.size() >= n && strcasecmp(s.c_str() + s.size() - n, e) == 0;
}

static bool is_cart(const std::string &n)
{
    return n[0] != '.' && (ends_with(n, ".p8") || ends_with(n, ".png"));
}

// "celeste_classic.p8.png" -> "celeste classic"
static std::string pretty(const std::string &file)
{
    std::string s = file.substr(file.rfind('/') + 1);
    for (const char *e : { ".p8.png", ".png", ".p8" })
        if (ends_with(s, e)) { s.resize(s.size() - strlen(e)); break; }
    for (char &c : s)
        if (c == '_') c = ' ';
    return s;
}

static std::string kind(const std::string &file)
{
    if (ends_with(file, ".p8.png") || ends_with(file, ".png")) return "p8.png";
    return "p8";
}

struct ListCtx { std::vector<Entry> *v; const std::string *dir; };

// A game of several carts lives in a folder named like its main cart,
// e.g. /backrooms/backrooms.p8.png; load() finds the others next to it.
// Such a folder is shown as that one cart: "backrooms/backrooms.p8.png".
static std::string game_folder_cart(const std::string &dir, const std::string &n)
{
    for (const char *ext : { ".p8.png", ".p8" }) {
        std::string rel = n + "/" + n + ext;
        pfile *f = pf_open_read((dir + "/" + rel).c_str());
        if (f) { pf_close(f); return rel; }
    }
    return "";
}

static void list_cb(const char *name, bool is_dir, void *ctx)
{
    ListCtx *c = (ListCtx *)ctx;
    std::string n = name;
    if (is_dir) {
        if (n[0] != '.' && n != "cdata" && n != "System Volume Information") {
            std::string rel = game_folder_cart(*c->dir, n);
            if (!rel.empty()) {
                Entry e;
                e.name = rel;
                pfile *f = pf_open_read((*c->dir + "/" + rel).c_str());
                if (f) { e.size = pf_size(f); pf_close(f); }
                c->v->push_back(e);
                return;
            }
            Entry e;
            e.name = n;
            e.dir = true;
            c->v->push_back(e);
        }
    } else if (is_cart(n)) {
        Entry e;
        e.name = n;
        pfile *f = pf_open_read((*c->dir + "/" + n).c_str());
        if (f) { e.size = pf_size(f); pf_close(f); }
        c->v->push_back(e);
    }
}

static bool by_name(const Entry &a, const Entry &b)
{
    if (a.special != b.special) return a.special < b.special;
    if (a.up != b.up) return a.up;
    if (a.dir != b.dir) return a.dir;
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
}

static std::vector<Entry> list_dir(const std::string &dir)
{
    std::vector<Entry> v;
    ListCtx c = { &v, &dir };
    pf_list(dir.empty() ? "/" : dir.c_str(), list_cb, &c);
    if (!dir.empty()) {
        Entry up;
        up.name = "..";
        up.up = up.dir = true;
        v.push_back(up);
    } else {
        // the two cards after the carts, in the top folder only
        Entry o, u;
        o.name = "Optionen";
        o.special = SPECIAL_OPTIONS;
        u.name = "USB-Stick";
        u.special = SPECIAL_USB;
        v.push_back(o);
        v.push_back(u);
    }
    std::sort(v.begin(), v.end(), by_name);
    return v;
}

static int count_carts(const std::vector<Entry> &v)
{
    int n = 0;
    for (const Entry &e : v)
        if (!e.dir && !e.special) n++;
    return n;
}

// ---- labels --------------------------------------------------------------------------
//
// The label is loaded once the selection rests for a moment: a .p8.png has
// to be inflated down to row 152, which takes a noticeable fraction of a
// second at 150 MHz. Three are kept, for the neighbours.

struct Label { int entry = -1; bool ok = false; uint8_t *px = nullptr; };
static Label labels[3];
static uint16_t pal_full[32], pal_dim[32];

static void label_palettes(void)
{
    for (int i = 0; i < 32; i++) {
        uint32_t rgb = (uint32_t)pico_rgb[i][0] << 16 | pico_rgb[i][1] << 8 | pico_rgb[i][2];
        pal_full[i] = pbc_rgb24(rgb);
        pal_dim[i] = pbc_rgb24(gfx_mix(rgb, 0x10162a, 150));
    }
}

// The carousel goes round: the neighbour i of the selection, wrapped
// past the ends. Only when no card would show twice (cnt > 2 * reach).
static int ring(int i, int cnt, int reach)
{
    return cnt > 2 * reach ? ((i % cnt) + cnt) % cnt : i;
}

static int ring_dist(int a, int b, int cnt)
{
    int d = abs(a - b);
    return std::min(d, cnt - d);
}

static const Label *find_label(int entry)
{
    for (const Label &l : labels)
        if (l.entry == entry) return &l;
    return nullptr;
}

static void load_label(const std::string &dir, const std::vector<Entry> &v, int entry, int keep)
{
    if (entry < 0 || entry >= (int)v.size() || v[entry].dir || v[entry].special || find_label(entry))
        return;
    // reuse the slot that is furthest from the selection
    Label *slot = &labels[0];
    for (Label &l : labels) {
        if (l.entry < 0) { slot = &l; break; }
        if (ring_dist(l.entry, keep, (int)v.size()) > ring_dist(slot->entry, keep, (int)v.size())) slot = &l;
    }
    if (!slot->px) slot->px = (uint8_t *)malloc(128 * 128);
    if (!slot->px) return;
    slot->entry = entry;
    slot->ok = cart_load_label(dir + "/" + v[entry].name, slot->px);
}

static void free_labels(void)
{
    for (Label &l : labels) {
        free(l.px);
        l = Label();
    }
}

static void draw_label(const uint8_t *px, int x, int y, bool dim)
{
    const uint16_t *pal = dim ? pal_dim : pal_full;
    for (int yy = 0; yy < 128; yy++) {
        int sy = y + yy;
        if (sy < 0 || sy >= GFX_H) continue;
        uint16_t *d = &gfx_fb[sy * GFX_W];
        const uint8_t *s = &px[yy * 128];
        for (int xx = 0; xx < 128; xx++) {
            int sx = x + xx;
            if (sx >= 0 && sx < GFX_W)
                d[sx] = pal[s[xx] & 31];
        }
    }
}

static void folder_icon(int cx, int cy, bool up, int alpha)
{
    gfx_rrect(cx - 40, cy - 30, 34, 14, 5, gfx_mix(FOLDER, 0x000000, 60), alpha);
    gfx_rrect(cx - 40, cy - 22, 80, 54, 7, FOLDER, alpha);
    gfx_rect(cx - 40, cy - 12, 80, 2, 0xffffff, alpha * 60 / 255);
    if (up)
        gfx_chevron(cx, cy + 6, 12, -1, 0x1a2030, alpha);
}

// three sliders, for the options card
static void options_icon(int cx, int cy, int alpha)
{
    static const int knob[3] = { -14, 12, -2 };
    for (int i = 0; i < 3; i++) {
        int y = cy - 22 + i * 22;
        gfx_rrect(cx - 34, y - 3, 68, 6, 3, 0xc3c9d6, alpha);
        gfx_circle(cx + knob[i], y, 8, stripe[i * 2 + 1], alpha);
    }
}

extern "C" const pbc_picture_t icon_usb;

// One carousel card: picture, frame and, for the centre one, a glow.
static void card(const std::string &dir, const std::vector<Entry> &v, int i, int x, bool centre)
{
    if (i < 0 || i >= (int)v.size()) return;
    const Entry &e = v[i];
    int a = centre ? 255 : 120;
    if (centre)
        gfx_glow(x + 64, LABEL_Y + 64, 110, 100,
                 e.special == SPECIAL_USB ? 0x29adff : e.dir ? FOLDER : ACCENT, 46);
    gfx_rrect(x - 6, LABEL_Y - 2, 140, 140, 10, 0x000000, centre ? 110 : 60);   // shadow
    gfx_rrect(x - 5, LABEL_Y - 5, 138, 138, 8, centre ? 0x0b0e17 : 0x0d111c, 255);

    const Label *l = find_label(i);
    if (e.special) {
        gfx_rrect(x, LABEL_Y, 128, 128, 4, CARD, 255);
        if (e.special == SPECIAL_OPTIONS)
            options_icon(x + 64, LABEL_Y + 64, a);
        else
            gfx_picture(&icon_usb, x + 64 - icon_usb.w / 2, LABEL_Y + 64 - icon_usb.h / 2, a);
    } else if (e.dir) {
        gfx_rrect(x, LABEL_Y, 128, 128, 4, CARD, 255);
        folder_icon(x + 64, LABEL_Y + 64, e.up, a);
    } else if (l && l->ok) {
        draw_label(l->px, x, LABEL_Y, !centre);
    } else {
        // no picture (yet): a stylised cartridge
        gfx_rrect(x, LABEL_Y, 128, 128, 4, CARD, 255);
        logo_stripe(x + 14, LABEL_Y + 18, 100, 4, a);
        gfx_text_centered(&font_title, x + 64, LABEL_Y + 44, "P8", centre ? TEXT : DIM, a);
        if (l && !l->ok)
            gfx_text_centered(&font_small, x + 64, LABEL_Y + 92, "kein Label", FAINT, a);
    }
    (void)dir;
}

// ---- options ---------------------------------------------------------------------------

static const char *const scale_names[VIDEO_NSCALES] = { "1:1 (128)", "240 x 240", "256 Crop" };

// a small panel (240 x 280 at 1/10) with the picture area of `scale`
static void scale_icon(int x, int y, int scale, uint32_t accent)
{
    const int w = 24, h = 28;
    gfx_rrect(x - 1, y - 1, w + 2, h + 2, 4, 0xc3c9d6, 255);
    gfx_rrect(x, y, w, h, 3, 0x0b0e17, 255);
    switch (scale) {
    case VIDEO_1TO1:    gfx_rect(x + 6, y + 8, 13, 13, accent, 255); break;
    case VIDEO_240:     gfx_rect(x, y + 2, 24, 24, accent, 255); break;
    default:            gfx_rect(x, y + 1, 24, 26, accent, 255);
                        // cut off at the sides
                        gfx_rect(x - 3, y + 6, 2, 16, accent, 120);
                        gfx_rect(x + w + 1, y + 6, 2, 16, accent, 120);
                        break;
    }
}

struct Options { int row; pbc_settings_t s; };

static void options_card(const Options &o, int slide)
{
    int y0 = 62 + (256 - ease(slide)) * 200 / 256;
    gfx_rect(0, 0, GFX_W, GFX_H, 0x000000, ease(slide) * 140 / 256);
    gfx_rrect(10, y0, GFX_W - 20, 180, 14, CARD, 255);
    gfx_rrect(10, y0, GFX_W - 20, 180, 14, 0xffffff, 10);
    int tw0 = gfx_text(&font_head, 24, y0 + 16, "Optionen", TEXT, 255);
    logo_stripe(24, y0 + 40, tw0, 3, 255);

    static const char *const rows[] = { "Skalierung", "Lautst\x84rke" };
    for (int r = 0; r < 2; r++) {
        int y = y0 + 54 + r * 38;
        bool sel = r == o.row;
        if (sel) {
            gfx_rrect(18, y - 4, GFX_W - 36, 34, 8, ACCENT, 60);
            gfx_rrect(18, y + 4, 4, 18, 2, ACCENT, 255);
        }
        gfx_text(&font_body, 30, y + 3, rows[r], sel ? TEXT : 0xc3c9d6, 255);
        if (r == 0) {
            const char *v = scale_names[o.s.scale];
            int w = gfx_text_w(&font_small, v);
            scale_icon(GFX_W - 54, y - 1, o.s.scale, sel ? ACCENT : DIM);
            gfx_text(&font_small, GFX_W - 64 - w, y + 7, v, sel ? TEXT : DIM, 255);
        } else {
            for (int b = 0; b < SETTINGS_VOLUME_MAX; b++) {
                int bh = 6 + b * 2;
                bool on = b < o.s.volume;
                gfx_rrect(GFX_W - 112 + b * 11, y + 21 - bh, 7, bh, 2,
                          on ? (sel ? ACCENT : DIM) : FAINT, on ? 255 : 120);
            }
        }
    }
    gfx_text_centered(&font_small, GFX_W / 2, y0 + 134, "links/rechts \x84ndern \xfa A/B fertig", DIM, 255);
    gfx_text_centered(&font_small, GFX_W / 2, y0 + 156, "PBC PICO-8 \xfa fake-08", FAINT, 255);
}

// ---- the menu ------------------------------------------------------------------------------

struct Menu {
    std::string dir;
    std::vector<Entry> v;
    int sel = 0;
    int slide = 0;              // pixels the cards are still shifted
    uint64_t sel_since = 0;
    std::string error;
};

static void menu_screen(const Menu &m)
{
    background();

    // header
    int lw = gfx_text(&font_title, 18, 8, "PICO-8", TEXT, 255);
    logo_stripe(18, 42, lw, 3, 255);
    char chip[64];
    int n = count_carts(m.v);
    if (m.dir.empty())
        snprintf(chip, sizeof chip, n == 1 ? "1 Cart" : "%d Carts", n);
    else
        snprintf(chip, sizeof chip, "%s \xfa %d", m.dir.c_str() + m.dir.rfind('/') + 1, n);
    char fit[72];
    gfx_text_fit(&font_small, chip, 90, fit);
    int w = gfx_text_w(&font_small, fit) + 22;
    gfx_rrect(GFX_W - 16 - w, 16, w, 20, 10, ACCENT, 70);
    gfx_text_centered(&font_small, GFX_W - 16 - w / 2, 20, fit, TEXT, 255);

    if (m.v.empty()) {
        gfx_text_centered(&font_body, GFX_W / 2, 110, "Noch keine Carts", TEXT, 255);
        gfx_text_centered(&font_small, GFX_W / 2, 140, "Beim Einschalten A oder B halten:", DIM, 255);
        gfx_text_centered(&font_small, GFX_W / 2, 155, "dann erscheint der USB-Stick.", DIM, 255);
    } else {
        // carousel: neighbours first, the centre card on top
        int cx = (GFX_W - 128) / 2 + m.slide, cnt = (int)m.v.size();
        card(m.dir, m.v, ring(m.sel - 2, cnt, 2), cx - 2 * SLIDE, false);
        card(m.dir, m.v, ring(m.sel + 2, cnt, 2), cx + 2 * SLIDE, false);
        card(m.dir, m.v, ring(m.sel - 1, cnt, 1), cx - SLIDE, false);
        card(m.dir, m.v, ring(m.sel + 1, cnt, 1), cx + SLIDE, false);
        card(m.dir, m.v, m.sel, cx, true);

        const Entry &e = m.v[m.sel];
        std::string title = e.up ? "Zur\x81" "ck" : e.special ? e.name : pretty(e.name);
        int tw = gfx_text_w(&font_head, title.c_str()), box = GFX_W - 40;
        uint32_t ms = (uint32_t)((plat_time_us() - m.sel_since) / 1000);
        gfx_clip(20, 190, 20 + box, 220);
        int tx = tw <= box ? (GFX_W - tw) / 2 : 20 - gfx_marquee(tw, box, ms);
        gfx_text(&font_head, tx, 194, title.c_str(), TEXT, 255);
        gfx_noclip();

        char info[64];
        if (e.special == SPECIAL_OPTIONS)
            snprintf(info, sizeof info, "Skalierung \xfa Lautst\x84rke");
        else if (e.special == SPECIAL_USB)
            snprintf(info, sizeof info, "Carts vom PC aufspielen");
        else if (e.up)
            snprintf(info, sizeof info, "eine Ebene h\x94her");
        else if (e.dir)
            snprintf(info, sizeof info, "Ordner");
        else
            snprintf(info, sizeof info, "%s \xfa %ld KB", kind(e.name).c_str(), (e.size + 1023) / 1024);
        gfx_text_centered(&font_small, GFX_W / 2, 219, info, DIM, 255);

        // where we are in the list
        if (m.v.size() > 1) {
            int x0 = 50, tw2 = GFX_W - 100;
            gfx_rrect(x0, 241, tw2, 3, 1, 0xffffff, 22);
            int th = std::max(10, tw2 / (int)m.v.size());
            int tx2 = x0 + (tw2 - th) * m.sel / ((int)m.v.size() - 1);
            gfx_rrect(tx2, 240, th, 5, 2, ACCENT, 255);
        }
    }

}

static void error_card(const std::string &err, int t)
{
    int a = ease(std::min(t, 256));
    gfx_rect(0, 0, GFX_W, GFX_H, 0x000000, a * 150 / 256);
    int y0 = 70 + (256 - a) * 40 / 256;
    gfx_rrect(14, y0, GFX_W - 28, 150, 14, 0x2a1622, 255);
    gfx_rrect(14, y0, GFX_W - 28, 150, 14, ACCENT, 40);
    gfx_rect(30, y0 + 14, 40, 3, ACCENT, 255);
    gfx_text(&font_head, 28, y0 + 22, "Die Cart ist abgest\x81rzt", TEXT, 255);
    text_wrapped(&font_small, 28, y0 + 52, GFX_W - 56, err.c_str(), 0xe6c9d2, 5);
    hint(GFX_W / 2 - 25, y0 + 126, "A", "OK");
}

menu_result_t menu_run(char *path, size_t n, const char *error)
{
    label_palettes();
    Menu m;
    m.error = error ? error : "";

    // open the folder of the last cart, with it selected
    std::string last = path ? path : "";
    std::string last_name = last;
    size_t slash = last.rfind('/');
    if (slash != std::string::npos) {
        m.dir = last.substr(0, slash);
        last_name = last.substr(slash + 1);
        // a game folder: open its parent, with the folder's card selected
        size_t up = m.dir.rfind('/');
        std::string folder = up == std::string::npos ? m.dir : m.dir.substr(up + 1);
        if (!folder.empty() && (last_name == folder + ".p8.png" || last_name == folder + ".p8")) {
            last_name = folder + "/" + last_name;
            m.dir = up == std::string::npos ? "" : m.dir.substr(0, up);
        }
    }
    m.v = list_dir(m.dir);
    if (m.v.empty() && !m.dir.empty()) {        // folder gone
        m.dir.clear();
        m.v = list_dir(m.dir);
    }
    for (size_t i = 0; i < m.v.size(); i++)
        if (m.v[i].name == last_name) m.sel = (int)i;
    m.sel_since = plat_time_us();

    pbc_settings_t saved;
    settings_load(&saved);
    Options opt = { 0, saved };
    int opt_slide = 0;          // 0 closed .. 256 open
    bool opt_open = false;
    int err_t = 0;

    wait_release();
    edges();
    for (;;) {
        uint64_t frame_start = plat_time_us();

        // labels: the selection first, then its neighbours
        if (m.slide == 0 && !m.v.empty() && frame_start - m.sel_since > 150000) {
            int cnt = (int)m.v.size(), next = ring(m.sel + 1, cnt, 1), prev = ring(m.sel - 1, cnt, 1);
            if (!find_label(m.sel)) load_label(m.dir, m.v, m.sel, m.sel);
            else if (!find_label(next)) load_label(m.dir, m.v, next, m.sel);
            else if (!find_label(prev)) load_label(m.dir, m.v, prev, m.sel);
        }

        menu_screen(m);
        if (opt_slide > 0)
            options_card(opt, opt_slide);
        if (!m.error.empty())
            error_card(m.error, err_t += 40);
        gfx_present();

        // animation
        if (m.slide) {
            m.slide = m.slide * 5 / 8;
            if (abs(m.slide) < 3) m.slide = 0;
        }
        opt_slide = opt_open ? std::min(256, opt_slide + 64) : std::max(0, opt_slide - 64);

        // A and B choose; the centre is ignored (the joystick hits it)
        uint32_t e = edges();
        if (!m.error.empty()) {
            if (e & (PB_A | PB_B)) m.error.clear();
        } else if (opt_open) {
            if (e & (PB_UP | PB_DOWN)) opt.row ^= 1;
            int d = (e & PB_RIGHT) ? 1 : (e & PB_LEFT) ? -1 : 0;
            if (d && opt.row == 0)
                opt.s.scale = (opt.s.scale + d + VIDEO_NSCALES) % VIDEO_NSCALES;
            if (d && opt.row == 1)
                opt.s.volume = std::max(0, std::min(SETTINGS_VOLUME_MAX, opt.s.volume + d));
            if (e & (PB_A | PB_B)) {
                opt_open = false;
                settings_save(&opt.s);
            }
        } else {
            int old = m.sel, cnt = (int)m.v.size(), step = 0;
            if (cnt) {
                // left/right go round; the jumps of 5 stop at the ends,
                // and go round from there
                if (e & PB_RIGHT) step = 1;
                if (e & PB_LEFT)  step = -1;
                if (e & PB_DOWN)  step = m.sel == cnt - 1 ? 1 : std::min(5, cnt - 1 - m.sel);
                if (e & PB_UP)    step = m.sel == 0 ? -1 : -std::min(5, m.sel);
                m.sel = ((m.sel + step) % cnt + cnt) % cnt;
            }
            if (m.sel != old) {
                m.slide += step * SLIDE;
                m.slide = std::max(-2 * SLIDE, std::min(2 * SLIDE, m.slide));
                m.sel_since = plat_time_us();
            }
            // A and B both choose
            if ((e & (PB_A | PB_B)) && cnt && m.v[m.sel].special == SPECIAL_OPTIONS) {
                opt_open = true;
                opt.row = 0;
                e = 0;
            }
            if ((e & (PB_A | PB_B)) && cnt && m.v[m.sel].special == SPECIAL_USB) {
                free_labels();
                return MENU_USB;
            }
            if ((e & (PB_A | PB_B)) && cnt) {
                const Entry &it = m.v[m.sel];
                if (it.dir) {
                    std::string from;
                    if (it.up) {
                        size_t k = m.dir.rfind('/');
                        from = m.dir.substr(k + 1);
                        m.dir = m.dir.substr(0, k);
                    } else {
                        m.dir += "/" + it.name;
                    }
                    free_labels();
                    m.v = list_dir(m.dir);
                    m.sel = 0;
                    for (size_t i = 0; i < m.v.size(); i++)
                        if (m.v[i].name == from) m.sel = (int)i;
                    m.slide = 0;
                    m.sel_since = plat_time_us();
                } else {
                    std::string p = m.dir + "/" + it.name;
                    snprintf(path, n, "%s", p.c_str());
                    free_labels();
                    return MENU_PLAY;
                }
            }
        }

        // ~30 frames a second; the panel takes about that long anyway
        uint64_t next = frame_start + FRAME_MS * 1000;
        if (plat_time_us() < next)
            plat_sleep_until_us(next);
    }
}

void menu_starting(const char *path)
{
    label_palettes();
    std::string p = path;
    uint8_t *px = (uint8_t *)malloc(128 * 128);
    bool ok = px && cart_load_label(p, px);
    std::string name = pretty(p.substr(p.rfind('/') + 1));

    background();
    gfx_glow(GFX_W / 2, 110, 130, 110, ACCENT, 60);
    gfx_rrect(50, 42, 140, 140, 10, 0x000000, 110);
    gfx_rrect(51, 39, 138, 138, 8, 0x0b0e17, 255);
    if (ok) {
        draw_label(px, 56, 44, false);
    } else {
        gfx_rrect(56, 44, 128, 128, 4, CARD, 255);
        logo_stripe(70, 62, 100, 4, 255);
        gfx_text_centered(&font_title, GFX_W / 2, 88, "P8", TEXT, 255);
    }
    free(px);
    char fit[96];
    gfx_text_fit(&font_head, name.c_str(), GFX_W - 40, fit);
    gfx_text_centered(&font_head, GFX_W / 2, 194, fit, TEXT, 255);
    gfx_text_centered(&font_small, GFX_W / 2, 222, "Pause: Mitte + A", DIM, 255);
    logo_stripe(70, 250, 100, 3, 255);
    gfx_present();
    gfx_finish();
}

// ---- USB stick -------------------------------------------------------------------------------

extern "C" const pbc_picture_t pic_usb;

void usb_screen(int state, const char *info, unsigned t_ms)
{
    static const char *const states[] = {
        "USB-Kabel anschlie\xe1" "en", "Verbunden", "Laufwerk ist ge\x94" "ffnet" };
    static const uint32_t colours[] = { 0x5a6378, ACCENT, OK_GREEN };
    background();
    // a slow breathing glow behind the picture
    int phase = (int)(t_ms % 3000);
    int breath = phase < 1500 ? phase : 3000 - phase;       // 0..1500
    gfx_glow(GFX_W / 2, 96, 130, 80, colours[state], 50 + breath * 50 / 1500);
    gfx_picture(&pic_usb, (GFX_W - pic_usb.w) / 2, 30, 255);
    gfx_text_centered(&font_title, GFX_W / 2, 164, "USB-Stick", TEXT, 255);
    int uw = gfx_text_w(&font_title, "USB-Stick");
    logo_stripe((GFX_W - uw) / 2, 197, uw, 3, 255);
    gfx_text_centered(&font_small, GFX_W / 2, 206, "Carts (.p8 / .p8.png) auf PICO8", DIM, 255);
    gfx_text_centered(&font_small, GFX_W / 2, 220, "kopieren, dann auswerfen.", DIM, 255);
    int w = gfx_text_w(&font_small, states[state]) + 34;
    int x = (GFX_W - w) / 2;
    gfx_rrect(x, 239, w, 20, 10, colours[state], 90);
    gfx_circle(x + 12, 249, 4, colours[state], state == 1 ? 120 + breath * 135 / 1500 : 255);
    gfx_text(&font_small, x + 22, 243, states[state], TEXT, 255);
    if (info && info[0])
        gfx_text_centered(&font_small, GFX_W / 2, 262, info, FAINT, 255);
    gfx_present();
}

void usb_screen_message(const char *title, const char *line)
{
    background();
    gfx_glow(GFX_W / 2, 96, 130, 80, ACCENT, 60);
    gfx_picture(&pic_usb, (GFX_W - pic_usb.w) / 2, 30, 255);
    gfx_text_centered(&font_title, GFX_W / 2, 164, title, TEXT, 255);
    int uw = gfx_text_w(&font_title, title);
    logo_stripe((GFX_W - uw) / 2, 197, uw, 3, 255);
    gfx_text_centered(&font_small, GFX_W / 2, 210, line, DIM, 255);
    gfx_present();
    gfx_finish();
}
