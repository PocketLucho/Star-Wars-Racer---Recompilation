/* Vulkan renderer for the Hikaru GPU output (GfxFrame). Headless (offscreen + readback, for tests)
 * or windowed (Win32 surface + swapchain; the 640x480 image is scaled to the window keeping 4:3). */
#pragma once
#include <stdint.h>
#include "hikaru_gfx.h"

typedef struct {
    int headless;
    int scale;              /* internal resolution = 640*scale x 480*scale */
    void *win32_hinstance;  /* windowed (Windows) */
    void *win32_hwnd;
    int vsync;
    int wide;               /* 16:9 output: 3D widened horizontally, 2D layers centred */
    /* windowed through another frontend (SDL): its instance extensions and a callback that creates the surface
       (instance = VkInstance, surface_out = VkSurfaceKHR*) */
    const char *const *inst_ext; uint32_t n_inst_ext;
    int (*create_surface)(void *instance, void *user, void *surface_out);
    void *surface_user;
    int msaa;               /* antialiasing samples: 0/1 off, 2, 4, 8 (clamped to what the GPU supports) */
} VkrConfig;

int  vkr_init(const VkrConfig *cfg);           /* 0 = ok */
void vkr_texram_upload(const uint8_t *texram_hw);   /* 8 MB, linear 16-bit texels */
void vkr_layers_upload(const uint16_t *lay640x960, uint32_t ena, uint32_t direct);   /* 2D layers (ABGR1555), ena bits 0/1 */
void vkr_draw(const GfxFrame *f);              /* render (and present when windowed) */
int  vkr_readback(uint8_t *rgba);              /* headless: copies the last frame, w*h*4 */
void vkr_size(int *w, int *h);
void vkr_resize(int w, int h);                 /* window size changed */
int  vkr_reconfigure(int scale, int wide, int msaa);   /* change internal resolution / aspect / MSAA at runtime */
int  vkr_msaa(void);
int  vkr_is_wide(void);
void vkr_request_screenshot(void);             /* the next vkr_draw copies its image for vkr_take_screenshot */
int  vkr_screenshot_ready(void);
int  vkr_take_screenshot(uint8_t *rgba);       /* w*h*4 (vkr_size), returns 0 when an image was captured */
const char *vkr_device_name(void);
void vkr_shutdown(void);
