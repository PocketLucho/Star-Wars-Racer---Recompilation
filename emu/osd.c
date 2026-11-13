/* On-screen menu drawn into the 2D layer image (640x480, ABGR1555, bit 15 = opaque), so it works with a
 * gamepad only (Steam Deck) and on any frontend. Font: font8x16.h (ASCII + Spanish letters, UTF-8 input). */
#include <string.h>
#include "osd.h"
#include "font8x16.h"

static int glyph_index(const unsigned char **p) {
    unsigned c = **p;
    if (c < 0x80) { (*p)++; return (c >= 32 && c < 127) ? (int)c - 32 : 0; }
    /* 2-byte UTF-8: look it up among the extra glyphs */
    if ((c & 0xe0) == 0xc0 && (*p)[1]) {
        const unsigned char *e = (const unsigned char *)font_extra;
        for (int i = 0; e[0]; i++, e += 2) if (e[0] == (*p)[0] && e[1] == (*p)[1]) { *p += 2; return 95 + i; }
        *p += 2; return '?' - 32;
    }
    (*p)++; while ((**p & 0xc0) == 0x80) (*p)++;
    return '?' - 32;
}

int osd_text_width(const char *s) {
    int n = 0; const unsigned char *p = (const unsigned char *)s;
    while (*p) { glyph_index(&p); n++; }
    return n * 8;
}

void osd_text(uint16_t *img, int x, int y, const char *s, uint16_t col) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int g = glyph_index(&p);
        for (int r = 0; r < 16; r++) {
            int yy = y + r; if (yy < 0 || yy >= 480) continue;
            uint8_t bits = font8x16[g][r];
            for (int b = 0; b < 8; b++) if (bits & (0x80 >> b)) {
                int xx = x + b; if (xx >= 0 && xx < 640) img[yy * 640 + xx] = col;
            }
        }
        x += 8;
    }
}

void osd_rect(uint16_t *img, int x, int y, int w, int h, uint16_t col) {
    for (int j = y; j < y + h; j++) if (j >= 0 && j < 480)
        for (int i = x; i < x + w; i++) if (i >= 0 && i < 640) img[j * 640 + i] = col;
}

#define RGB(r, g, b) (uint16_t)(0x8000 | ((b) >> 3) << 10 | ((g) >> 3) << 5 | ((r) >> 3))

void osd_menu(uint16_t *layers, uint32_t *ena, uint32_t *direct, const char *title, const OsdItem *it, int n, int sel, const char *foot) {
    /* the menu goes into the layer that is on top (layer 2 unless ena bit 2), over what the game had there */
    int top = (*ena & 4) ? 0 : 1;
    uint16_t *img = layers + top * 640 * 480;
    if (!(*ena & (1u << top))) memset(img, 0, 640 * 480 * 2);
    /* dim the picture: a 50% screen door (the layers have 1-bit alpha) */
    for (int y = 0; y < 480; y++) for (int x = (y & 1); x < 640; x += 2) img[y * 640 + x] = RGB(0, 0, 0);
    int rowh = 20, w = 520, h = 70 + n * rowh + 40, x0 = (640 - w) / 2, y0 = (480 - h) / 2;
    if (y0 < 4) y0 = 4;
    osd_rect(img, x0 - 2, y0 - 2, w + 4, h + 4, RGB(250, 200, 60));
    osd_rect(img, x0, y0, w, h, RGB(16, 22, 48));
    osd_text(img, x0 + (w - osd_text_width(title)) / 2, y0 + 12, title, RGB(250, 200, 60));
    int y = y0 + 46;
    for (int i = 0; i < n; i++, y += rowh) {
        if (i == sel) osd_rect(img, x0 + 8, y - 2, w - 16, rowh, RGB(60, 80, 150));
        uint16_t c = it[i].disabled ? RGB(120, 120, 130) : RGB(235, 235, 235);
        osd_text(img, x0 + 20, y, it[i].label, c);
        if (it[i].value[0]) {
            char v[64];
            if (it[i].kind == OSD_CHOICE) { strcpy(v, "< "); strncat(v, it[i].value, 56); strcat(v, " >"); }
            else { strncpy(v, it[i].value, 63); v[63] = 0; }
            osd_text(img, x0 + w - 20 - osd_text_width(v), y, v, i == sel ? RGB(255, 230, 120) : RGB(170, 200, 255));
        }
    }
    if (foot) osd_text(img, x0 + (w - osd_text_width(foot)) / 2, y0 + h - 26, foot, RGB(160, 160, 180));
    *ena |= 1u << top; *direct |= 1u << top;
}

void osd_banner(uint16_t *layers, uint32_t *ena, uint32_t *direct, const char *msg) {
    int top = (*ena & 4) ? 0 : 1;
    uint16_t *img = layers + top * 640 * 480;
    if (!(*ena & (1u << top))) memset(img, 0, 640 * 480 * 2);
    int w = osd_text_width(msg) + 24;
    osd_rect(img, (640 - w) / 2, 8, w, 24, RGB(16, 22, 48));
    osd_text(img, (640 - w) / 2 + 12, 12, msg, RGB(250, 200, 60));
    *ena |= 1u << top; *direct |= 1u << top;
}
