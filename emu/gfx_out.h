/* Hook from the GPU model to the active renderer (Vulkan when enabled; nothing otherwise). */
#pragma once
#include <stdint.h>
#include "hikaru_cp.h"
typedef void (*GfxOutFn)(const CpState *cp, const uint8_t *texram, int *texram_dirty, uint64_t frame);
extern GfxOutFn g_gfx_out;
static inline void gfx_out_frame(const CpState *cp, const uint8_t *texram, int *dirty, uint64_t frame) { if (g_gfx_out) g_gfx_out(cp, texram, dirty, frame); }
