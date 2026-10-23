/* pfile_posix.c -- pfile.h on stdio, for the PC test build. Paths are
 * taken relative to the directory given in PF_ROOT (default "."). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "pfile.h"

struct pfile { FILE *fp; };

static const char *full(const char *path, char *buf, size_t n)
{
    const char *root = getenv("PF_ROOT");
    snprintf(buf, n, "%s/%s", root ? root : ".", path[0] == '/' ? path + 1 : path);
    return buf;
}

static pfile *open_mode(const char *path, const char *mode)
{
    char p[1024];
    FILE *fp = fopen(full(path, p, sizeof p), mode);
    if (!fp) return NULL;
    pfile *f = malloc(sizeof *f);
    f->fp = fp;
    return f;
}

pfile *pf_open_read(const char *path)  { return open_mode(path, "rb"); }
pfile *pf_open_write(const char *path) { return open_mode(path, "wb"); }
int  pf_read(pfile *f, void *b, int n)        { return (int)fread(b, 1, (size_t)n, f->fp); }
int  pf_write(pfile *f, const void *b, int n) { return (int)fwrite(b, 1, (size_t)n, f->fp); }
void pf_close(pfile *f) { fclose(f->fp); free(f); }

long pf_size(pfile *f)
{
    long cur = ftell(f->fp), end;
    fseek(f->fp, 0, SEEK_END);
    end = ftell(f->fp);
    fseek(f->fp, cur, SEEK_SET);
    return end;
}

bool pf_seek(pfile *f, long pos) { return fseek(f->fp, pos, SEEK_SET) == 0; }

bool pf_stat(const char *path, long *size, unsigned long *stamp)
{
    char p[1024];
    struct stat st;
    if (stat(full(path, p, sizeof p), &st) != 0) return false;
    *size = (long)st.st_size;
    *stamp = (unsigned long)st.st_mtime;
    return true;
}

bool pf_exists(const char *path)
{
    char p[1024];
    struct stat st;
    return stat(full(path, p, sizeof p), &st) == 0;
}

bool pf_mkdir(const char *path)
{
    char p[1024];
    struct stat st;
    full(path, p, sizeof p);
    mkdir(p, 0777);
    return stat(p, &st) == 0;
}

bool pf_list(const char *dir, pf_dir_cb cb, void *ctx)
{
    char p[1024];
    DIR *d = opendir(full(dir, p, sizeof p));
    struct dirent *e;
    if (!d) return false;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        cb(e->d_name, e->d_type == DT_DIR, ctx);
    }
    closedir(d);
    return true;
}
