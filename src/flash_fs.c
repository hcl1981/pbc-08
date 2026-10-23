/*
 * flash_fs.c
 *
 * Low-level flash block device for the FatFs partition (see flash_fs.h).
 * Whole-sector (4096-byte) reads and erase+program writes.
 *
 * Unlike the Mega Drive port, PICO-8 carts write save data (cartdata) and
 * settings while the game runs: then the VM core writes, and the display /
 * sound core is parked in RAM by the multicore lockout, so nothing executes
 * from XIP while the flash is busy.
 */

#include "flash_fs.h"

#include <string.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <hardware/regs/addressmap.h>   /* XIP_BASE */
#include <pico/multicore.h>

volatile bool flash_fs_other_core_running;

/* the firmware keeps count for its reset diagnosis */
__attribute__((weak)) void flash_fs_note(int busy) { (void)busy; }

int flash_fs_read(uint32_t lba, void *dst, uint32_t sector_count) {
    if (lba + sector_count > PBGB_FS_SECTOR_COUNT) return -1;
    const uint8_t *src = (const uint8_t *)(XIP_BASE + PBGB_FS_FLASH_OFFSET
                                           + (size_t)lba * PBGB_FS_SECTOR_SIZE);
    memcpy(dst, src, (size_t)sector_count * PBGB_FS_SECTOR_SIZE);
    return 0;
}

int flash_fs_write(uint32_t lba, const void *src, uint32_t sector_count) {
    if (lba + sector_count > PBGB_FS_SECTOR_COUNT) return -1;
    const uint8_t *s = (const uint8_t *)src;
    bool lockout = flash_fs_other_core_running;

    for (uint32_t i = 0; i < sector_count; ++i) {
        uint32_t off = PBGB_FS_FLASH_OFFSET + (lba + i) * PBGB_FS_SECTOR_SIZE;
        if (lockout)
            multicore_lockout_start_blocking();
        flash_fs_note(1);
        uint32_t intr = save_and_disable_interrupts();
        flash_range_erase(off, PBGB_FS_SECTOR_SIZE);
        flash_range_program(off, s + (size_t)i * PBGB_FS_SECTOR_SIZE, PBGB_FS_SECTOR_SIZE);
        flash_fs_restore_timing();
        restore_interrupts(intr);
        flash_fs_note(0);
        if (lockout)
            multicore_lockout_end_blocking();
    }
    return 0;
}
