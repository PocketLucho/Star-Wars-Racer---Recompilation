/* Offline Hikaru CP viewer: cpview kick.bin [slave.bin] out.png [-tex tex.bin] [-log cp.txt] [-pc 48000100] [-proj n] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hikaru_cp.h"
#include "swrender.h"
static uint8_t *load(const char *p, size_t sz) {
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    uint8_t *b = calloc(1, sz); if (fread(b, 1, sz, f)) {} fclose(f); return b;
}
int main(int argc, char **argv) {
    const char *kick = 0, *slave = 0, *out = 0, *logp = 0; uint32_t pc = 0x48000100;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-log")) logp = argv[++i];
        else if (!strcmp(argv[i], "-pc")) pc = strtoul(argv[++i], 0, 16);
        else if (!strcmp(argv[i], "-proj")) sr_proj_mode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-scale")) sr_scale_k = atof(argv[++i]);
        else if (!strcmp(argv[i], "-tex")) sr_texram = load(argv[++i], 0x800000);
        else if (!strcmp(argv[i], "-notex")) sr_no_tex = 1;
        else if (!kick) kick = argv[i]; else if (!out && strstr(argv[i], ".png")) out = argv[i]; else slave = argv[i];
    }
    static CpState s; cp_init(&s);
    s.cmdram = load(kick, 0x400000);
    if (slave) s.slave_ram = load(slave, 0x2000000);
    if (logp) s.log = fopen(logp, "w");
    cp_frame(&s, pc, 2000000);
    fprintf(stderr, "steps=%ld tris=%d unknown=%ld running=%d pc=%08x\n", s.steps, s.ntris, s.unknown_ops, s.running, s.pc);
    for (int i = 0; i < 0x200; i++) if (s.opcount[i]) fprintf(stderr, "%03x:%u ", i, s.opcount[i]);
    fprintf(stderr, "\n");
    sr_render(&s);
    sr_png(out ? out : "cp.png");
    if (s.log) fclose(s.log);
    return 0;
}
