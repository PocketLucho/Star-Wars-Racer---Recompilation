/* ROM file access: a directory of loose files or a MAME zip (e.g. podrace.zip / swracer.zip). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniz.h"
#include "romzip.h"

static mz_zip_archive zip; static int zip_open;

int rom_open(const char *path) {
    size_t n = strlen(path);
    if (n > 4 && (!strcmp(path + n - 4, ".zip") || !strcmp(path + n - 4, ".ZIP"))) {
        memset(&zip, 0, sizeof zip);
        if (!mz_zip_reader_init_file(&zip, path, 0)) { fprintf(stderr, "cannot open %s\n", path); return -1; }
        zip_open = 1;
    }
    return 0;
}

/* reads up to sz bytes of `name` into dst; returns bytes read (0 = missing) */
size_t rom_read(const char *dir, const char *name, unsigned char *dst, size_t sz) {
    if (zip_open) {
        int idx = mz_zip_reader_locate_file(&zip, name, 0, MZ_ZIP_FLAG_IGNORE_PATH);
        if (idx < 0) { fprintf(stderr, "missing %s in zip\n", name); return 0; }
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, idx, &st)) return 0;
        size_t n = (size_t)st.m_uncomp_size;
        unsigned char *tmp = malloc(n ? n : 1);
        if (!mz_zip_reader_extract_to_mem(&zip, idx, tmp, n, 0)) { free(tmp); return 0; }
        if (n > sz) n = sz;
        memcpy(dst, tmp, n); free(tmp);
        return n;
    }
    char p[2048]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb"); if (!f) { fprintf(stderr, "missing %s\n", p); return 0; }
    size_t r = fread(dst, 1, sz, f); fclose(f); return r;
}
