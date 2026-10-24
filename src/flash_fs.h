/*
 * flash_fs.h
 *
 * Flash layout of PBC PICO-8 and the low-level block device that backs
 * both FatFs (diskio.c) and the USB mass-storage export (usb_msc.c).
 * Taken from PicoBoyGB / PBC Mega Drive; only the layout differs.
 *
 * 16 MB flash map (offsets from XIP_BASE / flash start):
 *
 *   0x000000 .. 0x1FFFFF   (2 MB)     firmware
 *   0x200000 .. 0xFEFFFF   (~13.9 MB) FatFs partition -> USB stick "PBC-08"
 *   0xFF0000 .. 0xFFFFFF   (64 KB)    reserved (0x10FFFF00 must stay free: the
 *                                     SDK's UF2 writes an extra block there)
 *
 * One FatFs sector == one 4096-byte flash sector, so a sector write maps
 * directly to flash_range_erase(4096) + flash_range_program(4096).
 */

#ifndef PBGB_FLASH_FS_H
#define PBGB_FLASH_FS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PBGB_FS_FLASH_OFFSET   0x200000u
#define PBGB_FS_FLASH_SIZE     (0xFF0000u - PBGB_FS_FLASH_OFFSET)
#define PBGB_FS_SECTOR_SIZE    4096u                    /* == flash erase unit */
#define PBGB_FS_SECTOR_COUNT   (PBGB_FS_FLASH_SIZE / PBGB_FS_SECTOR_SIZE)

/* Raw block access used by diskio.c and the USB MSC callbacks.
 * lba is a sector index within the FS region (0 .. PBGB_FS_SECTOR_COUNT-1).
 * Both operate on whole PBGB_FS_SECTOR_SIZE sectors. Return 0 on success. */
int  flash_fs_read (uint32_t lba, void *dst, uint32_t sector_count);
int  flash_fs_write(uint32_t lba, const void *src, uint32_t sector_count);

/* Set once the other core runs (and has called multicore_lockout_victim_init):
 * writes then park it in RAM for the duration of each erase/program. */
extern volatile bool flash_fs_other_core_running;

/* Called after every erase/program, with interrupts still off. The boot ROM
 * resets the QMI flash timing, the overclocked firmware has to restore it.
 * Must live in RAM. */
void flash_fs_restore_timing(void);

#ifdef __cplusplus
}
#endif

#endif /* PBGB_FLASH_FS_H */
