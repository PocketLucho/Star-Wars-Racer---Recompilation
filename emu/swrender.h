#pragma once
#include <stdint.h>
#include "hikaru_cp.h"
extern uint8_t *sr_texram;     /* TEXRAM: 2 banks x 4 MB, or NULL */
extern int sr_proj_mode, sr_no_tex;
extern float sr_scale_k;
void sr_render(const CpState *s);
void sr_png(const char *path);
void sr_png_rgba(const char *path, const uint8_t *rgba, int w, int h);

#include <stdio.h>
void sr_png_rgba_fp(FILE *f, const uint8_t *rgba, int w, int h);   /* closes f */
void sr_overlay(const uint16_t *layers640x960, int ena, int direct);
