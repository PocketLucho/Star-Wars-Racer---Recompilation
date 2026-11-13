/* Vulkan output: builds the vertex list from the CP state and draws it every frame.
 * Headless mode (tests): VKDUMP=<every N frames> writes vkframes/v_<frame>.png. */
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "gfx_out.h"
#include "hikaru_gfx.h"
#include "vkrender.h"
#include "swrender.h"
#include "sh4.h"
#include "gpu.h"

static GfxFrame gf;
/* frontend hook: may draw over the 2D layers (menus, messages) before they are uploaded */
void (*g_vk_layer_hook)(uint16_t *lay640x960, uint32_t *ena, uint32_t *direct);
static long dump_every = -1, dump_from = 0;
static uint8_t *rgba;

static void vk_out(const CpState *cp, const uint8_t *texram, int *dirty, uint64_t frame) {
    static const char *vklist = (const char *)-1; if (vklist == (const char *)-1) vklist = getenv("VKLIST");
    { static int fblog = -1; static uint32_t l1, l7; if (fblog < 0) fblog = getenv("FBLOG") != 0;
      if (fblog && (cp->fb181 != l1 || cp->fb781 != l7)) { l1 = cp->fb181; l7 = cp->fb781; fprintf(stderr, "FB frame %llu 181=%08x 781=%08x\n", (unsigned long long)frame, l1, l7); } }
    int want = dump_every > 0 && (long)frame >= dump_from && frame % dump_every == 0;
    if (vklist) { char k[24], b[512]; snprintf(k, sizeof k, ",%llu,", (unsigned long long)frame); snprintf(b, sizeof b, ",%s,", vklist); want = strstr(b, k) != 0;
                  if (!want) return; }   /* test mode: only render the listed frames (TEXRAM stays dirty until then) */
    if (*dirty) { vkr_texram_upload(texram); *dirty = 0; }
    g_gfx_xscale = vkr_is_wide() ? 0.75f : 1.0f;
    gfx_build(&gf, cp);
    { static u16 *lay; if (!lay) lay = malloc(640 * 960 * 2); uint32_t ena = (uint32_t)gpu_layers(lay), dir = (uint32_t)gpu_layers_direct();
      if (g_vk_layer_hook) g_vk_layer_hook(lay, &ena, &dir);
      vkr_layers_upload(lay, ena, dir); }
    vkr_draw(&gf);
    if (want) {
        int w, h; vkr_size(&w, &h);
        if (!rgba) rgba = malloc((size_t)w * h * 4);
        vkr_readback(rgba);
        char fn[64]; snprintf(fn, sizeof fn, "vkframes/v_%05llu.png", (unsigned long long)frame);
        sr_png_rgba(fn, rgba, w, h);
    }
}

int gfx_vk_start(const VkrConfig *cfg) {
    if (vkr_init(cfg)) return -1;
    fprintf(stderr, "Vulkan: %s, %s, internal %dx\n", vkr_device_name(), cfg->headless ? "headless" : "windowed", cfg->scale);
    dump_every = getenv("VKDUMP") ? atol(getenv("VKDUMP")) : 0;
    dump_from = getenv("VKDUMP_FROM") ? atol(getenv("VKDUMP_FROM")) : 0;
    g_gfx_out = vk_out;
    return 0;
}

/* paused frontends (menus): draw the last 3D frame again with these layers */
void gfx_vk_redraw(const uint16_t *lay640x960, uint32_t ena, uint32_t direct) {
    vkr_layers_upload(lay640x960, ena, direct);
    vkr_draw(&gf);
}
