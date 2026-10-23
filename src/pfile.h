/* pfile.h -- the little file API fake-08 needs, backed by FatFs on the
 * device (pfile_fatfs.c) and by stdio in the PC test build (hosttest/).
 * Paths are absolute within the volume, '/' separated. */
#ifndef PBC_PFILE_H
#define PBC_PFILE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pfile pfile;

pfile *pf_open_read(const char *path);
pfile *pf_open_write(const char *path);       /* create or truncate */
int    pf_read(pfile *f, void *buf, int n);   /* bytes read, 0 at end, <0 error */
int    pf_write(pfile *f, const void *buf, int n);
long   pf_size(pfile *f);
bool   pf_seek(pfile *f, long pos);

/* size and a modification stamp; false if it does not exist */
bool   pf_stat(const char *path, long *size, unsigned long *stamp);
void   pf_close(pfile *f);

bool   pf_exists(const char *path);
bool   pf_mkdir(const char *path);            /* true if it exists afterwards */

/* Calls cb for every entry of dir (not . / ..). */
typedef void (*pf_dir_cb)(const char *name, bool is_dir, void *ctx);
bool   pf_list(const char *dir, pf_dir_cb cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PBC_PFILE_H */
