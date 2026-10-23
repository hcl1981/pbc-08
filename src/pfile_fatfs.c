/* pfile_fatfs.c -- pfile.h on FatFs. One FIL is about 4.6 KB (it carries a
 * sector buffer), so files are heap objects that live only while open. */
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "pfile.h"
#include "pbc_mem.h"

struct pfile { FIL fil; };

static pfile *open_mode(const char *path, BYTE mode)
{
    pfile *f = pbc_malloc(sizeof *f);
    if (!f) return NULL;
    if (f_open(&f->fil, path, mode) != FR_OK) {
        pbc_free(f);
        return NULL;
    }
    return f;
}

pfile *pf_open_read(const char *path)  { return open_mode(path, FA_READ); }
pfile *pf_open_write(const char *path) { return open_mode(path, FA_WRITE | FA_CREATE_ALWAYS); }

int pf_read(pfile *f, void *buf, int n)
{
    UINT got = 0;
    if (f_read(&f->fil, buf, (UINT)n, &got) != FR_OK) return -1;
    return (int)got;
}

int pf_write(pfile *f, const void *buf, int n)
{
    UINT put = 0;
    if (f_write(&f->fil, buf, (UINT)n, &put) != FR_OK) return -1;
    return (int)put;
}

long pf_size(pfile *f) { return (long)f_size(&f->fil); }

bool pf_seek(pfile *f, long pos) { return f_lseek(&f->fil, (FSIZE_t)pos) == FR_OK; }

bool pf_stat(const char *path, long *size, unsigned long *stamp)
{
    FILINFO fi;
    if (f_stat(path, &fi) != FR_OK) return false;
    *size = (long)fi.fsize;
    *stamp = (unsigned long)fi.fdate << 16 | fi.ftime;
    return true;
}

void pf_close(pfile *f)
{
    f_close(&f->fil);
    pbc_free(f);
}

bool pf_exists(const char *path)
{
    FILINFO fi;
    return f_stat(path, &fi) == FR_OK;
}

bool pf_mkdir(const char *path)
{
    FRESULT r = f_mkdir(path);
    return r == FR_OK || r == FR_EXIST;
}

bool pf_list(const char *dir, pf_dir_cb cb, void *ctx)
{
    DIR *d = pbc_malloc(sizeof *d);
    FILINFO fi;
    if (!d) return false;
    if (f_opendir(d, dir[0] ? dir : "/") != FR_OK) {
        pbc_free(d);
        return false;
    }
    while (f_readdir(d, &fi) == FR_OK && fi.fname[0]) {
        if (fi.fattrib & (AM_HID | AM_SYS)) continue;
        cb(fi.fname, (fi.fattrib & AM_DIR) != 0, ctx);
    }
    f_closedir(d);
    pbc_free(d);
    return true;
}
