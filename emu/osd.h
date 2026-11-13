/* On-screen menu in the 2D layer image (see osd.c) */
#pragma once
#include <stdint.h>

enum { OSD_ACTION, OSD_CHOICE };
typedef struct {
    const char *label;
    char value[48];      /* shown at the right ("" = none); OSD_CHOICE draws it as < value > */
    int kind, disabled;
} OsdItem;

int  osd_text_width(const char *s);
void osd_text(uint16_t *img640x480, int x, int y, const char *utf8, uint16_t abgr1555);
void osd_rect(uint16_t *img640x480, int x, int y, int w, int h, uint16_t abgr1555);
/* layers = 640x960 (two layers), ena / direct as for vkr_layers_upload: updated to show the menu on top */
void osd_menu(uint16_t *layers, uint32_t *ena, uint32_t *direct, const char *title, const OsdItem *it, int n, int sel, const char *foot);
void osd_banner(uint16_t *layers, uint32_t *ena, uint32_t *direct, const char *msg);
