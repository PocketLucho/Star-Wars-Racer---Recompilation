/* GD-ROM CHD -> high-density area image: every MODE1 sector is placed by the address in its own header
 * (MSF - 150 = LBA), at (LBA - 45000) * 2048, so the ISO9660 extents of the GD area resolve directly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libchdr/chd.h>
static int bcd(int v) { return (v >> 4) * 10 + (v & 15); }
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: chd2gd in.chd out.img\n"); return 1; }
    chd_file *chd; if (chd_open(argv[1], CHD_OPEN_READ, 0, &chd) != CHDERR_NONE) { fprintf(stderr, "open failed\n"); return 1; }
    const chd_header *h = chd_get_header(chd);
    unsigned fb = 2448, per = h->hunkbytes / fb; char *hunk = malloc(h->hunkbytes);
    FILE *o = fopen(argv[2], "wb"); long placed = 0, maxlba = 0;
    for (unsigned hn = 0; hn < h->totalhunks; hn++) {
        if (chd_read(chd, hn, hunk) != CHDERR_NONE) continue;
        for (unsigned f = 0; f < per; f++) {
            unsigned char *s = (unsigned char *)hunk + f * fb;
            static const unsigned char sync[12] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
            if (memcmp(s, sync, 12) || s[15] != 1) continue;          /* MODE1 data sectors only */
            long lba = (bcd(s[12]) * 60 + bcd(s[13])) * 75 + bcd(s[14]) - 150;
            if (lba < 45000) continue;
            fseek(o, (lba - 45000) * 2048L, SEEK_SET); fwrite(s + 16, 1, 2048, o); placed++;
            if (lba > maxlba) maxlba = lba;
        }
    }
    fclose(o); chd_close(chd);
    fprintf(stderr, "%ld sectors placed, last LBA %ld\n", placed, maxlba);
    return 0;
}
