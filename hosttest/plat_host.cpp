// plat_host.cpp -- plat.h for the PC test build.
//
// Time is virtual (sleeping just advances it), so carts run as fast as the
// PC allows. Environment:
//   FRAMES=n        stop after n presented frames (default 600)
//   SHOTS=a,b,..    write shot_<frame>.ppm (the full 240x280 panel)
//   KEYS=f:bits,..  from frame f on hold these plat.h button bits
//   SCALE=n         start with scaling n
//   HEAP_LIMIT=n    fail allocations beyond n bytes in use
// At the end it prints frames, peak heap and the time spent.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <malloc.h>
#include <unistd.h>
#include "plat.h"
#include "video.h"
#include "Audio.h"
#include "lua.h"
extern lua_State *pbc_vm_lua;

// ---- heap accounting: interposes malloc for the whole process ----------------
extern "C" {
void *__libc_malloc(size_t);
void __libc_free(void *);
void *__libc_realloc(void *, size_t);
void *__libc_calloc(size_t, size_t);
}
static size_t heap_now, heap_peak, heap_limit, lua_peak;
extern size_t pbc_lua_used, pbc_heap_total, pbc_cpp_used;

static inline size_t usz(void *p) { return p ? malloc_usable_size(p) : 0; }

extern "C" void *malloc(size_t n)
{
    if (heap_limit && heap_now + n > heap_limit) return nullptr;
    void *p = __libc_malloc(n);
    heap_now += usz(p);
    if (heap_now > heap_peak) heap_peak = heap_now;
    return p;
}
extern "C" void free(void *p)
{
    heap_now -= usz(p);
    __libc_free(p);
}
extern "C" void *calloc(size_t a, size_t b)
{
    void *p = malloc(a * b);
    if (p) memset(p, 0, a * b);
    return p;
}
extern "C" void *realloc(void *p, size_t n)
{
    size_t old = usz(p);
    if (n && heap_limit && heap_now - old + n > heap_limit) return nullptr;
    void *q = __libc_realloc(p, n);
    if (q || !n) {
        heap_now -= old;
        heap_now += usz(q);
        if (heap_now > heap_peak) heap_peak = heap_now;
    }
    return q;
}

// ---- plat.h -------------------------------------------------------------------
static uint64_t vtime;
static int frames, max_frames = 600, scale = VIDEO_240;
static std::vector<int> shots;
static Audio *audio;
static double audio_cpu, total_cpu0;
static FILE *wav;
static long wav_samples;

static void finish();

static double cpu_now()
{
    timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

extern "C" void pbc_audio_lock(void) { }
extern "C" void pbc_audio_unlock(void) { }

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
        if (frames >= f) bits = (uint32_t)atoi(c + 1);
        k = strchr(c, ',');
        if (k) k++;
    }
    return bits;
}

void plat_set_scale(int s) { scale = s; }
void plat_set_volume(int v) { (void)v; }
void plat_exit_to_menu(const char *error)
{
    printf("EXIT TO MENU: %s\n", error && error[0] ? error : "(ok)");
    finish();
    _exit(0);
}

static void write_shot(const video_frame_t *f)
{
    static uint16_t lcd[280][240];
    memset(lcd, 0, sizeof lcd);
    video_rect_t r = video_geometry(scale);
    for (int y = 0; y < r.h; y++)
        video_render_line(f, scale, y, &lcd[r.y + y][r.x]);
    char name[64];
    snprintf(name, sizeof name, "shot_%04d.ppm", frames);
    FILE *fp = fopen(name, "wb");
    fprintf(fp, "P6 240 280 255\n");
    for (int y = 0; y < 280; y++)
        for (int x = 0; x < 240; x++) {
            uint16_t v = lcd[y][x];           // BGR565
            unsigned char px[3] = {
                (unsigned char)((v & 31) << 3), (unsigned char)(((v >> 5) & 63) << 2),
                (unsigned char)(((v >> 11) & 31) << 3) };
            fwrite(px, 1, 3, fp);
        }
    fclose(fp);
    printf("wrote %s (scale %d)\n", name, scale);
}

extern "C" __attribute__((weak)) void report_stack(void) {}

static void finish()
{
    report_stack();
    static bool done;
    if (done) _exit(0);
    done = true;
    if (wav) {
        long bytes = wav_samples * 2;
        fseek(wav, 0, SEEK_SET);
        unsigned char h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
            0x22,0x56,0,0, 0x44,0xac,0,0, 2,0,16,0,'d','a','t','a'};
        long riff = bytes + 36;
        memcpy(h + 4, &riff, 4);
        memcpy(h + 40, &bytes, 4);
        fwrite(h, 1, 44, wav);
        fclose(wav);
    }
    double t = cpu_now();
    size_t before = pbc_lua_used;
    if (pbc_vm_lua) lua_gc(pbc_vm_lua, LUA_GCCOLLECT, 0);
    printf("lua now %zu peak %zu live %zu c++ %zu\n", before, lua_peak, pbc_lua_used, pbc_cpp_used);
    printf("frames %d  peak heap %zu  now %zu  cpu %.2fs (audio %.2fs) = %.2f ms/frame vm\n",
           frames, heap_peak, heap_now, t, audio_cpu, (t - audio_cpu) * 1000.0 / (frames ? frames : 1));
    fflush(stdout);
    _exit(0);
}

void plat_present(const uint8_t *fb, const uint8_t *palmap, uint8_t mode)
{
    static video_frame_t f;
    memcpy(f.fb, fb, sizeof f.fb);
    memcpy(f.palmap, palmap, 16);
    f.mode = mode;
    frames++;
    if (pbc_lua_used > lua_peak) lua_peak = pbc_lua_used;

    if (audio) {
        static int16_t buf[368];
        double t0 = cpu_now();
        audio->FillMonoAudioBuffer(buf, 0, 368);
        audio_cpu += cpu_now() - t0;
        if (wav) { fwrite(buf, 2, 368, wav); wav_samples += 368; }
    }

    for (int s : shots)
        if (s == frames) write_shot(&f);
    if (frames >= max_frames) finish();
}

void plat_audio_start(void *a)
{
    audio = (Audio *)a;
    if (getenv("WAV")) {
        wav = fopen(getenv("WAV"), "wb");
        unsigned char z[44] = {0};
        fwrite(z, 1, 44, wav);
    }
}

extern "C" void plat_mark(const char *what)
{
    if (getenv("MARK_GC") && pbc_vm_lua) lua_gc(pbc_vm_lua, LUA_GCCOLLECT, 0);
    printf("[heap] %-16s now %7zu peak %7zu lua %7zu c++ %7zu\n", what, heap_now, heap_peak, pbc_lua_used, pbc_cpp_used);
}
extern size_t pbc_lua_used, pbc_heap_total, pbc_cpp_used;

extern "C" void pbc_out_of_memory(size_t n)
{
    printf("OUT OF MEMORY (%zu bytes), heap %zu, lua %zu, c++ %zu\n", n, heap_now, pbc_lua_used, pbc_cpp_used);
    _exit(2);
}

extern "C" void plat_host_init(void)
{
    if (getenv("HEAP_TOTAL")) pbc_heap_total = (size_t)atol(getenv("HEAP_TOTAL"));
    if (getenv("FRAMES")) max_frames = atoi(getenv("FRAMES"));
    if (getenv("HEAP_LIMIT")) heap_limit = (size_t)atol(getenv("HEAP_LIMIT"));
    for (const char *s = getenv("SHOTS"); s && *s; ) {
        shots.push_back(atoi(s));
        s = strchr(s, ',');
        if (s) s++;
    }
}
