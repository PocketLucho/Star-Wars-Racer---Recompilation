/* Windows frontend settings (swracer.ini next to the exe) and the modal settings window (F10):
 * video (internal resolution, MSAA, 16:9, fullscreen), game (country, hidden pilots), gamepad, key bindings. */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum {
    ACT_BOTH_UP, ACT_BOTH_DOWN,          /* both levers forward / back */
    ACT_LEFT_UP, ACT_LEFT_DOWN,          /* left lever */
    ACT_RIGHT_UP, ACT_RIGHT_DOWN,        /* right lever */
    ACT_STEER_LEFT, ACT_STEER_RIGHT,     /* steer = temporary difference between the levers */
    ACT_SEL_PREV, ACT_SEL_NEXT,          /* select buttons (menus) */
    ACT_BOOST, ACT_START, ACT_COIN, ACT_TEST, ACT_SERVICE, ACT_PAUSE,
    ACT_N
};

typedef struct {
    int scale;          /* internal resolution multiplier, 1..8 */
    int wide;           /* 16:9 */
    int msaa;           /* antialiasing samples 1 (off), 2, 4, 8 */
    int fullscreen;     /* start in fullscreen (F11 toggles and remembers) */
    int lever_speed;    /* digital lever speed 1..10 */
    int pad_mode;       /* gamepad: 0 off, 1 sticks (left/right stick Y = levers), 2 triggers (LT/RT = levers) */
    int lang;           /* interface language: 0 follow Windows, 1 Spanish, 2 English */
    int region;         /* country written to the EEPROM: 0 Japan, 1 USA, 2 Export, 3 Australia, 4 Korea */
    int keys[ACT_N];    /* virtual-key codes */
} Settings;

extern Settings g_set;
extern const char *g_act_names[ACT_N];

void settings_defaults(Settings *s);
void settings_load(void);
int settings_save(void);                 /* 0 = ok */
const wchar_t *settings_dir(void);      /* folder for swracer.ini / swracer.nv / screenshots (ends in \\) */
/* modal settings window; returns 1 if the user accepted (g_set updated and saved).
 * *restart is set when an option needs the program to restart (country). */
int settings_dialog(HINSTANCE hi, HWND parent, int *restart);
extern const wchar_t *g_region_names[5];
int swr_lang_en(void);                   /* 1 = English interface */
const wchar_t *TR(const wchar_t *es);    /* Spanish UI string -> current language */
#define T2(es, en) (swr_lang_en() ? (en) : (es))
float lever_step(void);                  /* lever travel per frame for the digital keys */
