/*
 * PBC PICO-8 -- a PICO-8 player for the PicoBoy Color Plus
 * (RP2350A, 520 KB SRAM, 16 MB flash, ST7789 240x280, PWM speaker).
 *
 * The VM is fake-08 (MIT, github.com/jtothebell/fake-08) with z8lua.
 * Division of work:
 *   core 1   the PICO-8 VM (Lua, drawing into the 128x128 screen, cart
 *            loading, save data). Runs on its own 32 KB stack because the
 *            Lua parser recurses deeply.
 *   core 0   the panel (scaling each posted frame line by line, DMA to the
 *            ST7789) and the sound (fake-08's synth into the PWM ring).
 *
 * Boot:
 *   A or B held, or no carts on the drive  -> USB stick mode (drive "PICO8")
 *   otherwise                              -> cart menu (menu.cpp, native)
 * The menu runs at the stock clock before anything else is set up; the
 * chosen cart then starts with the whole heap. Leaving a cart restarts the
 * device into the menu, with the cart and any error passed in RAM that
 * survives the reset (boot_note).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/vreg.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/powman.h"
#include "hardware/structs/psm.h"
#include "hardware/structs/watchdog.h"

#include "ff.h"
#if PBC_SERIAL_LOG
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#define LOG(...) printf(__VA_ARGS__)
#else
#include "tusb.h"
#define LOG(...) ((void)0)
#endif

#include "board.h"
#include "color565.h"
#include "display.h"
#include "input.h"
#include "audio.h"
#include "video.h"
#include "plat.h"
#include "flash_fs.h"
#include "pbc_mem.h"
#include "pico8_run.h"
#include "menu.h"
#include "settings.h"

#include "Audio.h"

extern "C" {
#include "gfx.h"
}

#ifndef CPU_MHZ
#define CPU_MHZ 250
#endif
#ifndef CPU_VREG
#define CPU_VREG VREG_VOLTAGE_1_25
#endif
#ifndef FLASH_MAX_FREQ_MHZ
#define FLASH_MAX_FREQ_MHZ 88
#endif
#ifndef SHOW_FPS
#define SHOW_FPS 0
#endif
#ifndef LUA_RESERVE_KB
#define LUA_RESERVE_KB 48      /* heap kept free of Lua for the C++ side */
#endif

#define VM_HANG_US        8000000u
#define WATCHDOG_MS       3000u

#define CORE1_STACK_WORDS (32 * 1024 / 4)   /* the VM needs ~14 KB (measured) */
static uint32_t core1_stack[CORE1_STACK_WORDS];

extern size_t pbc_lua_used;

/* survives the watchdog reset from the VM back into the menu */
#define BOOT_NOTE_MAGIC 0x50384d4eu     /* "P8MN" */
typedef struct {
    uint32_t magic;
    char last[200];                     /* cart played last */
    char error[200];                    /* why it ended, if it did not end well */
    /* diagnosis of resets nobody asked for */
    uint32_t running;                   /* 1 while a cart runs; deliberate exits clear it */
    uint32_t frames;                    /* frames the VM posted */
    uint32_t flash_writes;              /* sectors written during the game */
    uint32_t flash_busy;                /* 1 inside an erase/program */
    uint32_t vm_hang;                   /* core 0 saw the VM stop posting frames */
    uint32_t fault;                     /* 1 + core of a hard fault */
    uint32_t fault_pc, fault_lr, fault_cfsr, fault_addr, fault_sp;
} boot_note_t;
static boot_note_t __uninitialized_ram(boot_note);

/* Hard faults (on either core) are written into the boot note, then the
 * watchdog restarts the chip: the menu and the serial log show where. */
extern "C" void __not_in_flash_func(pbc_fault_record)(uint32_t *sp)
{
    volatile uint32_t *scb = (volatile uint32_t *)0xe000ed00u;
    boot_note.fault = 1 + get_core_num();
    boot_note.fault_pc = sp[6];
    boot_note.fault_lr = sp[5];
    boot_note.fault_sp = (uint32_t)sp;
    boot_note.fault_cfsr = scb[0x28 / 4];
    boot_note.fault_addr = scb[0x38 / 4];      /* BFAR */
    boot_note.magic = BOOT_NOTE_MAGIC;
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    psm_hw->wdsel = PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS);
    hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_TRIGGER_BITS);
    for (;;) __asm volatile("nop");
}

extern "C" void __attribute__((naked, used, section(".time_critical.isr_hardfault"))) isr_hardfault(void)
{
    __asm volatile(
        "movs r0, #4        \n"
        "mov  r1, lr        \n"
        "tst  r0, r1        \n"
        "beq  1f            \n"
        "mrs  r0, psp       \n"
        "b    2f            \n"
        "1: mrs r0, msp     \n"
        "2: ldr r1, =pbc_fault_record \n"
        "bx   r1            \n");
}

/* ---- FatFs volume -------------------------------------------------------- */
static FATFS g_fs;
extern "C" volatile bool usb_msc_ejected;

static bool fs_mount(void)   { return f_mount(&g_fs, "", 1) == FR_OK; }
static void fs_unmount(void) { f_mount(NULL, "", 0); }

static bool fs_format(void)
{
    BYTE *work = (BYTE *)malloc(FF_MAX_SS);
    MKFS_PARM opt = { FM_FAT | FM_SFD, 0, 0, 0, 0 };
    FRESULT r = work ? f_mkfs("", &opt, work, FF_MAX_SS) : FR_NOT_ENOUGH_CORE;
    free(work);
    if (r != FR_OK)
        return false;
    if (fs_mount()) {
        f_setlabel("PICO8");
        fs_unmount();
    }
    return true;
}

static void fs_write_readme(void)
{
    static FILINFO fi;
    if (f_stat("/LIESMICH.TXT", &fi) == FR_OK)
        return;
    /* a FIL carries a 4 KB sector buffer: heap, not stack or static */
    FIL *fp = (FIL *)malloc(sizeof(FIL));
    if (!fp)
        return;
    FIL &f = *fp;
    if (f_open(&f, "/LIESMICH.TXT", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        free(fp);
        return;
    }
    f_puts("PBC PICO-8 - PICO-8-Spieler fuer den PicoBoy Color Plus\r\n"
           "\r\n"
           "Carts (.p8 oder .p8.png) hierher kopieren (auch in Unterordner),\r\n"
           "dann das Laufwerk AUSWERFEN. Der PicoBoy startet danach neu und\r\n"
           "zeigt die Cart-Liste.\r\n"
           "\r\n"
           "Zurueck in diesen Modus: beim Einschalten A oder B gedrueckt halten.\r\n"
           "\r\n"
           "Steuerung:\r\n"
           "  Joystick   Steuerkreuz\r\n"
           "  A          O (Taste 4)\r\n"
           "  B          X (Taste 5)\r\n"
           "  Mitte + A  Pausenmenue (Skalierung, Reset, zurueck zur Liste)\r\n"
           "\r\n"
           "Spielstaende (cartdata) liegen in /cdata, Einstellungen in pico8.cfg.\r\n", &f);
    f_close(&f);
    free(fp);
}

static bool fs_has_carts(void)
{
    static DIR d;
    static FILINFO fi;
    bool any = false;
    if (f_opendir(&d, "/") != FR_OK)
        return false;
    while (!any && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        size_t n = strlen(fi.fname);
        if (fi.fattrib & (AM_HID | AM_SYS))
            continue;
        if (fi.fattrib & AM_DIR)
            any = strcmp(fi.fname, "cdata") != 0 && strcmp(fi.fname, "System Volume Information") != 0;
        else if (n > 3 && (!strcasecmp(fi.fname + n - 3, ".p8") || (n > 4 && !strcasecmp(fi.fname + n - 4, ".png"))))
            any = true;
    }
    f_closedir(&d);
    return any;
}

/* ---- small screen helpers ------------------------------------------------ */
static int diag_y;

static void diag(const char *s, uint16_t colour)
{
    pbc_display_text(8, diag_y, s, colour, PBC_BLACK, 1);
    diag_y += 11;
}

static void screen_title(const char *title)
{
    pbc_display_fill(0, 0, PBC_LCD_W, PBC_LCD_H, PBC_BLACK);
    pbc_display_text_centered(28, title, PBC_YELLOW, PBC_BLACK, 2);
    diag_y = 60;
}

static void wait_release(void)
{
    while (input_raw())
        sleep_ms(10);
    sleep_ms(30);
}

static int count_carts_root(void)
{
    static DIR d;
    static FILINFO fi;
    int n = 0;
    if (f_opendir(&d, "/") != FR_OK)
        return 0;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        size_t len = strlen(fi.fname);
        if (!(fi.fattrib & (AM_DIR | AM_HID | AM_SYS)) && len > 3 &&
            (!strcasecmp(fi.fname + len - 3, ".p8") || (len > 4 && !strcasecmp(fi.fname + len - 4, ".png"))))
            n++;
    }
    f_closedir(&d);
    return n;
}

#if PBC_SERIAL_LOG
static void __attribute__((noreturn)) usb_stick_mode(void)
{
    LOG("[usb] stick mode is not available in the serial-log build\n");
    if (gfx_fb) usb_screen_message("Debug-Build", "kein USB-Stick im Log-Modus");
    for (;;) tight_loop_contents();
}
#else
static void __attribute__((noreturn)) usb_stick_mode(void)
{
    if (!gfx_fb && !gfx_init()) {
        /* no heap for the pretty screen: plain text */
        screen_title("USB-STICK");
        diag("Carts auf PICO8 kopieren,", PBC_WHITE);
        diag("dann auswerfen.", PBC_WHITE);
    }

    FRESULT fr = f_mount(&g_fs, "", 1);
    if (fr != FR_OK) {
        if (gfx_fb) usb_screen_message("Einrichten", "Das Laufwerk wird formatiert ...");
        if (fs_format() && fs_mount())
            fr = FR_OK;
    }
    if (fr != FR_OK) {
        if (gfx_fb) usb_screen_message("Fehler", "Das Laufwerk ist unbrauchbar.");
        for (;;) tight_loop_contents();
    }
    fs_write_readme();
    f_mkdir("/cdata");

    char info[48] = "";
    {
        DWORD free_clust = 0;
        FATFS *fs;
        int n = count_carts_root();
        if (f_getfree("", &free_clust, &fs) == FR_OK) {
            unsigned kb = (unsigned)((uint64_t)free_clust * fs->csize * FF_MAX_SS / 1024);
            snprintf(info, sizeof info, "%d %s \xfa %u,%u MB frei", n, n == 1 ? "Cart" : "Carts",
                     kb / 1024, kb % 1024 * 10 / 1024);
        }
    }
    fs_unmount();               /* the host owns the medium from now on */

    tud_init(BOARD_TUD_RHPORT);

    uint64_t start = time_us_64(), next_draw = 0;
    int shown = -1;
    for (;;) {
        tud_task();
        uint64_t now = time_us_64();
        int state = tud_mounted() ? 2 : tud_connected() ? 1 : 0;
        /* redraw a few times a second for the breathing light; the USB
         * transfers wait the ~10 ms a frame takes */
        if (gfx_fb && (state != shown || now >= next_draw)) {
            usb_screen(state, info, (unsigned)((now - start) / 1000));
            shown = state;
            next_draw = now + 120000;
        }
        if (usb_msc_ejected) {
            uint64_t t = time_us_64() + 300000;
            while (time_us_64() < t)
                tud_task();     /* let the host see the eject complete */
            if (gfx_fb) usb_screen_message("Ausgeworfen", "Neustart in die Cart-Liste ...");
            wait_release();
            boot_note.magic = 0;
            watchdog_reboot(0, 0, 10);
            for (;;) tight_loop_contents();
        }
    }
}
#endif

/* ---- clocks -------------------------------------------------------------- */

/* Keep the flash (XIP) clock under FLASH_MAX_FREQ_MHZ whatever the system
 * clock. Runs from RAM while the QMI timing changes. */
static void __no_inline_not_in_flash_func(set_flash_timings)(int cpu_mhz)
{
    const int clock_hz = cpu_mhz * 1000000;
    const int max_flash_freq = FLASH_MAX_FREQ_MHZ * 1000000;

    int divisor = (clock_hz + max_flash_freq - (max_flash_freq >> 4) - 1) / max_flash_freq;
    if (divisor == 1 && clock_hz >= 166000000)
        divisor = 2;

    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000 && clock_hz >= 166000000)
        rxdelay += 1;

    qmi_hw->m[0].timing = 0x60007000 |
                          rxdelay << QMI_M0_TIMING_RXDELAY_LSB |
                          divisor << QMI_M0_TIMING_CLKDIV_LSB;
}

static int g_cpu_mhz = 150;

extern "C" void __no_inline_not_in_flash_func(flash_fs_restore_timing)(void)
{
    if (g_cpu_mhz > 150)
        set_flash_timings(g_cpu_mhz);
}

/* SPI = clk_peri / even number. With clk_peri from the USB PLL at 150 MHz
 * the panel gets 75 MHz whatever the system clock. USB is only used in
 * stick mode, which never gets here. */
static void display_clock(void)
{
#if PBC_SERIAL_LOG
    /* USB stays on for the console: SPI from clk_sys (62.5 MHz at 250) */
    spi_set_baudrate(PBC_SPI, PBC_SPI_HZ);
    return;
#endif
    clock_stop(clk_usb);
    clock_stop(clk_adc);
    pll_init(pll_usb, 1, 1500 * MHZ, 5, 2);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    150 * MHZ, 150 * MHZ);
    spi_set_baudrate(PBC_SPI, PBC_SPI_HZ);
}

static void overclock(void)
{
    if (CPU_MHZ > 150) {
        if (CPU_VREG > VREG_VOLTAGE_1_30)
            vreg_disable_voltage_limit();
        vreg_set_voltage(CPU_VREG);
        sleep_ms(10);
        set_flash_timings(CPU_MHZ);
        if (set_sys_clock_khz(CPU_MHZ * 1000, false)) {
            g_cpu_mhz = CPU_MHZ;
        } else {
            set_flash_timings(250);
            set_sys_clock_khz(250000, true);
            g_cpu_mhz = 250;
        }
    }
    display_clock();
}

/* ---- frame hand-over (core 1 -> core 0) ----------------------------------- */
static video_frame_t frames[2];
static volatile int pending = -1;      /* frame waiting for the panel */
static volatile int rendering = 0;     /* frame the panel is drawing */
static volatile int g_scale = VIDEO_240;
static spin_lock_t *frame_lock, *audio_lock;
static volatile uint64_t last_present_us;

/* flash_fs.c reports its writes here, for the reset diagnosis */
extern "C" void flash_fs_note(int busy)
{
    /* no LOG here: core 0 is parked and may hold the stdio lock */
    boot_note.flash_busy = (uint32_t)busy;
    if (busy)
        boot_note.flash_writes++;
}

#if SHOW_FPS
static volatile uint32_t stat_frames;
#endif

extern "C" void plat_present(const uint8_t *fb, const uint8_t *palmap, uint8_t mode)
{
    uint32_t s = spin_lock_blocking(frame_lock);
    int idx = pending >= 0 ? pending : 1 - rendering;
    pending = -1;                       /* withdrawn while it is rewritten */
    spin_unlock(frame_lock, s);

    memcpy(frames[idx].fb, fb, sizeof frames[idx].fb);
    memcpy(frames[idx].palmap, palmap, 16);
    frames[idx].mode = mode;

    s = spin_lock_blocking(frame_lock);
    pending = idx;
    spin_unlock(frame_lock, s);
    boot_note.frames++;
    last_present_us = time_us_64();
#if SHOW_FPS
    stat_frames++;
#endif
}

extern "C" void plat_set_scale(int scale) { g_scale = scale; }

extern "C" uint64_t plat_time_us(void) { return time_us_64(); }

extern "C" void plat_sleep_until_us(uint64_t t)
{
    sleep_until(from_us_since_boot(t));
}

extern "C" uint32_t plat_buttons(void) { return input_raw(); }

/* ---- sound ----------------------------------------------------------------- */
static Audio *g_audio;

extern "C" void pbc_audio_lock(void)
{
    if (audio_lock) spin_lock_unsafe_blocking(audio_lock);
}

extern "C" void pbc_audio_unlock(void)
{
    if (audio_lock) spin_unlock_unsafe(audio_lock);
}

static void fill_audio(int16_t *dst, int n)
{
    g_audio->FillMonoAudioBuffer(dst, 0, (size_t)n);
}

/* Leaving the VM: core 1 only asks; core 0 brings the chip back to the
 * boot state before it restarts. A watchdog restart straight out of the
 * overclocked game (250 MHz, raised core voltage, USB PLL moved for the
 * panel) did not always come back up on the PicoBoy. */
static volatile bool exit_requested;

extern "C" void plat_exit_to_menu(const char *error)
{
    LOG("[vm] exit to menu: %s\n", error ? error : "");
    snprintf(boot_note.error, sizeof boot_note.error, "%s", error ? error : "");
    boot_note.running = 0;
    boot_note.magic = BOOT_NOTE_MAGIC;
    __dmb();
    exit_requested = true;
    for (;;) tight_loop_contents();     /* core 0 resets this core */
}

static void __attribute__((noreturn)) restart_to_menu(void)
{
    multicore_reset_core1();
    flash_fs_other_core_running = false;
    audio_stop();
    pbc_display_wait();
    while (spi_is_busy(PBC_SPI))
        tight_loop_contents();
#if PBC_SERIAL_LOG
    {   /* let the last lines reach the PC */
        uint64_t t = time_us_64() + 200000;
        while (time_us_64() < t)
            tight_loop_contents();
    }
#endif
    /* back to the boot clocks: system 150 MHz, stock voltage and flash
     * timing, USB PLL at 48 MHz, clk_peri from clk_sys */
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    set_sys_clock_khz(150000, true);
    set_flash_timings(150);
    g_cpu_mhz = 150;
    vreg_set_voltage(VREG_VOLTAGE_DEFAULT);
    sleep_ms(2);
    pll_init(pll_usb, 1, 1200 * MHZ, 5, 5);
    clock_configure(clk_usb, 0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, 48 * MHZ, 48 * MHZ);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, 150 * MHZ, 150 * MHZ);

    /* buttons still held would land in USB stick mode */
    while (input_raw())
        tight_loop_contents();
    busy_wait_ms(30);
    watchdog_reboot(0, 0, 10);
    for (;;) tight_loop_contents();
}

extern "C" void plat_set_volume(int volume)
{
    audio_set_volume(volume);
}

extern "C" void plat_audio_start(void *audio)
{
    g_audio = (Audio *)audio;
    __dmb();
    audio_start(fill_audio);
}

/* ---- out of memory ------------------------------------------------------- */

extern "C" void pbc_out_of_memory(size_t n)
{
    char m[120];
    snprintf(m, sizeof m, "Speicher voll: %u Bytes fehlten (Lua %u KB, C++ %u KB von %u KB).",
             (unsigned)n, (unsigned)(pbc_lua_used / 1024), (unsigned)(pbc_cpp_used / 1024),
             (unsigned)(pbc_heap_total / 1024));
    plat_exit_to_menu(m);
}

/* ---- core 0: panel + sound ------------------------------------------------- */
static uint16_t line_buf[2][PBC_LCD_W];

static void __not_in_flash_func(show_frame)(const video_frame_t *f, int scale)
{
    video_rect_t r = video_geometry(scale);
    pbc_display_window(r.x, r.y, r.w, r.h);
    pbc_display_begin();
    for (int y = 0; y < r.h; y++) {
        uint16_t *buf = line_buf[y & 1];
        video_render_line(f, scale, y, buf);
        pbc_display_wait();
        pbc_display_send(buf, r.w);
        if ((y & 15) == 0)
            audio_service();
    }
    pbc_display_end();
}

static void __attribute__((noreturn)) core0_loop(void)
{
    int shown_scale = -1;
#if SHOW_FPS
    uint64_t stat_t0 = time_us_64();
#endif
    for (;;) {
        audio_service();

        /* the watchdog restarts into the menu if this loop stops; a VM
         * that stops posting frames (loading takes a second or two at
         * most) is reported the same way */
        watchdog_update();
        if (exit_requested)
            restart_to_menu();
#if PBC_SERIAL_LOG
        {
            static uint64_t next_log;
            static uint32_t last_frames;
            uint64_t now = time_us_64();
            if (now >= next_log) {
                next_log = now + 1000000;
                LOG("[hb] t=%lums frames=%lu (+%lu) lua=%uK c++=%uK total=%uK flash=%lu busy=%lu\n",
                    (unsigned long)(now / 1000), (unsigned long)boot_note.frames,
                    (unsigned long)(boot_note.frames - last_frames),
                    (unsigned)(pbc_lua_used / 1024), (unsigned)(pbc_cpp_used / 1024),
                    (unsigned)(pbc_heap_total / 1024), (unsigned long)boot_note.flash_writes,
                    (unsigned long)boot_note.flash_busy);
                last_frames = boot_note.frames;
            }
        }
#endif
        if (time_us_64() - last_present_us > VM_HANG_US) {
            LOG("[hb] VM posted no frame for %us -> watchdog reboot\n", VM_HANG_US / 1000000);
            boot_note.vm_hang = 1;
            watchdog_reboot(0, 0, 10);
            for (;;) tight_loop_contents();
        }

        int idx = -1;
        uint32_t s = spin_lock_blocking(frame_lock);
        if (pending >= 0) {
            idx = pending;
            pending = -1;
            rendering = idx;
        }
        spin_unlock(frame_lock, s);
        if (idx < 0)
            continue;

        int scale = g_scale;
        if (scale != shown_scale) {
            pbc_display_fill(0, 0, PBC_LCD_W, PBC_LCD_H, PBC_BLACK);
            shown_scale = scale;
        }
        show_frame(&frames[idx], scale);

#if SHOW_FPS
        uint64_t now = time_us_64();
        if (now - stat_t0 >= 1000000) {
            char m[40];
            /* frames/s from the VM, Lua and C++ heap, heap total (KB) */
            snprintf(m, sizeof m, "%2lu fps  L%3u C%3u /%3u K", (unsigned long)stat_frames,
                     (unsigned)(pbc_lua_used / 1024), (unsigned)(pbc_cpp_used / 1024),
                     (unsigned)(pbc_heap_total / 1024));
            stat_frames = 0;
            stat_t0 = now;
            /* below the picture in every scaling, clear of the round corners */
            pbc_display_text(20, PBC_LCD_H - 11, m, PBC_GREY, PBC_BLACK, 1);
        }
#endif
    }
}

/* ---- core 1: the VM ------------------------------------------------------- */
extern "C" void plat_mark(const char *what) { (void)what; }

static char g_cart[200];

static void core1_main(void)
{
    pico8_run(g_cart);
}

/* free heap right now: what the allocator holds unused plus what sbrk can
 * still hand out (the heap ends at the end of main RAM) */
static size_t heap_free(void)
{
    extern char __StackLimit;
    struct mallinfo mi = mallinfo();
    char *top = (char *)sbrk(0);
    return (size_t)mi.fordblks + (size_t)(&__StackLimit - top);
}

/* The cart did not end by itself: say how, from what survived the reset. */
static void describe_reset(char *out, size_t n, uint32_t chip_reset, uint32_t wd_reason)
{
    const char *why;
    if (boot_note.fault) {
        snprintf(out, n, "Absturz auf Kern %lu bei %08lx (lr %08lx, cfsr %08lx, adr %08lx). Nach %lu Bildern.",
                 (unsigned long)(boot_note.fault - 1), (unsigned long)boot_note.fault_pc,
                 (unsigned long)boot_note.fault_lr, (unsigned long)boot_note.fault_cfsr,
                 (unsigned long)boot_note.fault_addr, (unsigned long)boot_note.frames);
        return;
    }
    if (boot_note.vm_hang)
        why = "die VM reagierte nicht mehr";
    else if (boot_note.flash_busy)
        why = "Absturz beim Schreiben in den Flash";
    else if (chip_reset & POWMAN_CHIP_RESET_HAD_BOR_BITS)
        why = "Unterspannung (Brownout)";
    else if (chip_reset & POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS)
        why = "Spannungsspitze (Glitch-Detektor)";
    else if (wd_reason & WATCHDOG_REASON_TIMER_BITS)
        why = "das Bild hing (Watchdog)";
    else if (chip_reset & POWMAN_CHIP_RESET_HAD_POR_BITS)
        why = "Stromausfall";
    else
        why = "unbekannt";
    snprintf(out, n, "Unerwarteter Neustart: %s. Nach %lu Bildern, %lu Flash-Schreibvorg\x84nge. (Reset %08lx/%lx)",
             why, (unsigned long)boot_note.frames, (unsigned long)boot_note.flash_writes,
             (unsigned long)chip_reset, (unsigned long)wd_reason);
}

int main(void)
{
#if PBC_SERIAL_LOG
    stdio_init_all();
    /* give the PC a moment to open the port, so the boot is in the log */
    for (int i = 0; i < 200 && !stdio_usb_connected(); i++)
        sleep_ms(10);
    LOG("\n[boot] PBC PICO-8 serial log, chip_reset=%08lx watchdog=%lx\n",
        (unsigned long)powman_hw->chip_reset, (unsigned long)watchdog_hw->reason);
#endif
    input_init();
    pbc_display_init();
    pbc_display_backlight(200);

    char error[200] = "";
    g_cart[0] = 0;
    uint32_t chip_reset = powman_hw->chip_reset;
    uint32_t wd_reason = watchdog_hw->reason;
    if (boot_note.magic == BOOT_NOTE_MAGIC) {
        boot_note.last[sizeof boot_note.last - 1] = 0;
        boot_note.error[sizeof boot_note.error - 1] = 0;
        strcpy(g_cart, boot_note.last);
        strcpy(error, boot_note.error);
        if (boot_note.running || boot_note.fault)
            describe_reset(error, sizeof error, chip_reset, wd_reason);
        LOG("[boot] note: last=%s error=%s\n", g_cart, error);
    }
    boot_note.magic = 0;

    bool want_usb = (input_raw() & (PB_A | PB_B)) != 0;
    if (!want_usb) {
        if (!fs_mount())
            want_usb = true;
        else if (!fs_has_carts())
            want_usb = true;
    }
    gfx_init();
    if (want_usb) {
        wait_release();
        usb_stick_mode();
    }

    if (menu_run(g_cart, sizeof g_cart, error) == MENU_USB) {
        fs_unmount();
        usb_stick_mode();
    }
    LOG("[menu] start %s\n", g_cart);
    menu_starting(g_cart);
    gfx_shutdown();

    /* back here after the cart, unless the device is switched off */
    snprintf(boot_note.last, sizeof boot_note.last, "%s", g_cart);
    boot_note.error[0] = 0;
    boot_note.running = 1;
    boot_note.frames = boot_note.flash_writes = boot_note.flash_busy = boot_note.vm_hang = 0;
    boot_note.fault = 0;
    boot_note.magic = BOOT_NOTE_MAGIC;

    overclock();
    frame_lock = spin_lock_init(spin_lock_claim_unused(true));
    audio_lock = spin_lock_init(spin_lock_claim_unused(true));
    audio_init();
    {
        pbc_settings_t st;
        settings_load(&st);
        audio_set_volume(st.volume);
    }

    /* the VM gets the whole remaining heap; Lua may grow while
     * LUA_RESERVE_KB stay free */
    pbc_heap_total = heap_free();
    pbc_heap_reserve = LUA_RESERVE_KB * 1024;
    LOG("[boot] clock %d MHz, heap for the VM %u KB\n", g_cpu_mhz, (unsigned)(pbc_heap_total / 1024));

    multicore_lockout_victim_init();
    flash_fs_other_core_running = true;
    last_present_us = time_us_64();
    multicore_launch_core1_with_stack(core1_main, core1_stack, sizeof core1_stack);
    watchdog_enable(WATCHDOG_MS, false);     /* also when a core has locked up */

    core0_loop();
}
