/* Settings for the Windows frontend: swracer.ini (next to the exe) + modal settings window (F10).
 * Plain Win32 controls, wide-char strings (UTF-8 source). */
#include "settings_win32.h"
#include "emu_core.h"
#include "cheats.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

Settings g_set;

/* ini keys */
const char *g_act_names[ACT_N] = {
    "both_up", "both_down", "left_up", "left_down", "right_up", "right_down",
    "steer_left", "steer_right", "select_prev", "select_next",
    "boost", "start", "coin", "test", "service", "pause"
};
const wchar_t *g_region_names[5] = { L"Japón", L"EE. UU.", L"Exportación (Europa)", L"Australia", L"Corea" };
/* labels shown in the window */
/* ---- language: Spanish strings are the keys; English comes from this table (lang 0 = follow Windows) ---- */
static const struct { const wchar_t *es, *en; } tr_tab[] = {
    { L"Japón", L"Japan" },
    { L"EE. UU.", L"USA" },
    { L"Exportación (Europa)", L"Export (Europe)" },
    { L"Australia", L"Australia" },
    { L"Corea", L"Korea" },
    { L"Ambas palancas: adelante", L"Both levers: forward" },
    { L"Ambas palancas: atrás", L"Both levers: back" },
    { L"Palanca izquierda: adelante", L"Left lever: forward" },
    { L"Palanca izquierda: atrás", L"Left lever: back" },
    { L"Palanca derecha: adelante", L"Right lever: forward" },
    { L"Palanca derecha: atrás", L"Right lever: back" },
    { L"Girar a la izquierda", L"Steer left" },
    { L"Girar a la derecha", L"Steer right" },
    { L"Botón selección: anterior", L"Select button: previous" },
    { L"Botón selección: siguiente", L"Select button: next" },
    { L"Boost / confirmar", L"Boost / decide" },
    { L"Moneda", L"Coin" },
    { L"Test (menú de servicio)", L"Test (service menu)" },
    { L"Service (mover cursor)", L"Service (move cursor)" },
    { L"Pausa", L"Pause" },
    { L"Flecha arriba", L"Up arrow" },
    { L"Flecha abajo", L"Down arrow" },
    { L"Flecha izquierda", L"Left arrow" },
    { L"Flecha derecha", L"Right arrow" },
    { L"Espacio", L"Space" },
    { L"Intro", L"Enter" },
    { L"Mayús", L"Shift" },
    { L"Retroceso", L"Backspace" },
    { L"Tecla 0x%02X", L"Key 0x%02X" },
    { L"Desbloquear Ben Quadinaros", L"Unlock Ben Quadinaros" },
    { L"Desbloquear Gasgano", L"Unlock Gasgano" },
    { L"  (ya desbloqueado)", L"  (already unlocked)" },
    { L"Desbloqueado: aparece en SELECT PODRACER (con ◀ desde Anakin).", L"Unlocked: shown on SELECT PODRACER (press ◀ from Anakin)." },
    { L"Aún no se puede: deja que el juego arranque una vez hasta el modo demo.", L"Not yet: let the game boot once up to the attract mode." },
    { L"Pulsa una tecla...  (Esc = cancelar)", L"Press a key...  (Esc = cancel)" },
    { L"Opciones - Star Wars Racer Arcade Recomp", L"Options - Star Wars Racer Arcade Recomp" },
    { L"Vídeo", L"Video" },
    { L"Resolución interna:", L"Internal resolution:" },
    { L"1x  (640 x 480, original)", L"1x  (640 x 480, original)" },
    { L"Antialiasing (MSAA):", L"Antialiasing (MSAA):" },
    { L"Desactivado", L"Off" },
    { L"Panorámico 16:9 (más campo de visión en 3D; el 2D queda en 4:3)", L"Widescreen 16:9 (wider 3D field of view; 2D stays 4:3)" },
    { L"Pantalla completa al arrancar (F11 cambia y se recuerda)", L"Start in fullscreen (F11 toggles and is remembered)" },
    { L"Juego", L"Game" },
    { L"País / región:", L"Country / region:" },
    { L"(cambiarlo reinicia el juego; cambia textos y avisos)", L"(changing it restarts the game; changes texts and notices)" },
    { L"Palancas y mando", L"Levers and gamepad" },
    { L"Velocidad palancas (teclas):", L"Lever speed (keys):" },
    { L"1 (muy lenta)", L"1 (very slow)" },
    { L"4 (por defecto)", L"4 (default)" },
    { L"8 (como antes)", L"8 (as before)" },
    { L"10 (muy rápida)", L"10 (very fast)" },
    { L"Mando (XInput):", L"Gamepad (XInput):" },
    { L"Sticks: izquierdo / derecho = palancas", L"Sticks: left / right = levers" },
    { L"Gatillos: LT / RT = palancas", L"Triggers: LT / RT = levers" },
    { L"Teclas (pulsa un botón y después la tecla)", L"Keys (click a button, then press the key)" },
    { L"Teclas por defecto", L"Default keys" },
    { L"Aceptar", L"OK" },
    { L"Cancelar", L"Cancel" },
    { L"No se pudieron guardar las opciones en:\n%lsswracer.ini", L"The options could not be saved to:\n%lsswracer.ini" },
    { L"Idioma:", L"Language:" },
    { L"Automático", L"Automatic" },
    { L"Español", L"Spanish" },
    { L"Inglés", L"English" },
};
int swr_lang_en(void) {
    if (g_set.lang == 1) return 0;
    if (g_set.lang == 2) return 1;
    return PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_SPANISH;
}
const wchar_t *TR(const wchar_t *es) {
    if (!swr_lang_en()) return es;
    for (unsigned i = 0; i < sizeof tr_tab / sizeof *tr_tab; i++) if (!wcscmp(tr_tab[i].es, es)) return tr_tab[i].en;
    return es;
}
static const wchar_t *act_labels[ACT_N] = {
    L"Ambas palancas: adelante", L"Ambas palancas: atrás",
    L"Palanca izquierda: adelante", L"Palanca izquierda: atrás",
    L"Palanca derecha: adelante", L"Palanca derecha: atrás",
    L"Girar a la izquierda", L"Girar a la derecha",
    L"Botón selección: anterior", L"Botón selección: siguiente",
    L"Boost / confirmar", L"Start", L"Moneda", L"Test (menú de servicio)", L"Service (mover cursor)", L"Pausa"
};

void settings_defaults(Settings *s) {
    s->scale = 2; s->wide = 0; s->msaa = 1; s->fullscreen = 0; s->lever_speed = 4; s->pad_mode = 1; s->region = -1; s->lang = 0;
    s->keys[ACT_BOTH_UP] = VK_UP;      s->keys[ACT_BOTH_DOWN] = VK_DOWN;
    s->keys[ACT_LEFT_UP] = 'A';        s->keys[ACT_LEFT_DOWN] = 'Z';
    s->keys[ACT_RIGHT_UP] = 'K';       s->keys[ACT_RIGHT_DOWN] = 'M';
    s->keys[ACT_STEER_LEFT] = VK_LEFT; s->keys[ACT_STEER_RIGHT] = VK_RIGHT;
    s->keys[ACT_SEL_PREV] = VK_LEFT;   s->keys[ACT_SEL_NEXT] = VK_RIGHT;
    s->keys[ACT_BOOST] = VK_SPACE;     s->keys[ACT_START] = '1'; s->keys[ACT_COIN] = '5';
    s->keys[ACT_TEST] = VK_F2;         s->keys[ACT_SERVICE] = VK_F1;
    s->keys[ACT_PAUSE] = 'P';
}
float lever_step(void) { int v = g_set.lever_speed; if (v < 1) v = 1; if (v > 10) v = 10; return 0.006f * (float)v; }

/* configuration folder: next to the exe if writable, else %APPDATA%\StarWarsRacerRecomp (wide paths: the
 * exe may live in a folder with accents) */
static wchar_t cfg_dir[MAX_PATH];
const wchar_t *settings_dir(void) {
    if (cfg_dir[0]) return cfg_dir;
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(0, exe, MAX_PATH);
    wchar_t *sl = wcsrchr(exe, L'\\'); if (sl) sl[1] = 0; else exe[0] = 0;
    wchar_t probe[MAX_PATH]; swprintf(probe, MAX_PATH, L"%lsswracer_write_test.tmp", exe);
    FILE *f = _wfopen(probe, L"wb");
    if (f) { fclose(f); _wremove(probe); wcsncpy(cfg_dir, exe, MAX_PATH); return cfg_dir; }
    wchar_t app[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH);
    if (n && n < MAX_PATH - 32) { swprintf(cfg_dir, MAX_PATH, L"%ls\\StarWarsRacerRecomp\\", app); CreateDirectoryW(cfg_dir, 0); }
    else wcsncpy(cfg_dir, exe, MAX_PATH);
    return cfg_dir;
}
static void ini_path(wchar_t *out, size_t n) { swprintf(out, n, L"%lsswracer.ini", settings_dir()); }

void settings_load(void) {
    wchar_t p[MAX_PATH]; ini_path(p, MAX_PATH);
    settings_defaults(&g_set);
    FILE *f = _wfopen(p, L"r"); if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char key[64]; int v;
        if (sscanf(line, " %63[a-z_] = %d", key, &v) != 2) continue;
        if (!strcmp(key, "scale")) g_set.scale = v;
        else if (!strcmp(key, "widescreen")) g_set.wide = v != 0;
        else if (!strcmp(key, "msaa")) g_set.msaa = v;
        else if (!strcmp(key, "fullscreen")) g_set.fullscreen = v != 0;
        else if (!strcmp(key, "lever_speed")) g_set.lever_speed = v;
        else if (!strcmp(key, "gamepad")) g_set.pad_mode = v;
        else if (!strcmp(key, "language")) g_set.lang = v;
        else for (int i = 0; i < ACT_N; i++) if (!strcmp(key, g_act_names[i]) && v > 0 && v < 256) g_set.keys[i] = v;
    }
    fclose(f);
    if (g_set.scale < 1) g_set.scale = 1; if (g_set.scale > 8) g_set.scale = 8;
    if (g_set.msaa != 2 && g_set.msaa != 4 && g_set.msaa != 8) g_set.msaa = 1;
    if (g_set.lever_speed < 1 || g_set.lever_speed > 10) g_set.lever_speed = 4;
    if (g_set.pad_mode < 0 || g_set.pad_mode > 2) g_set.pad_mode = 1;
    if (g_set.lang < 0 || g_set.lang > 2) g_set.lang = 0;
}

int settings_save(void) {
    wchar_t p[MAX_PATH]; ini_path(p, MAX_PATH);
    FILE *f = _wfopen(p, L"w"); if (!f) return -1;
    fprintf(f, "; Star Wars Racer Arcade Recomp - opciones (F10)\n[video]\nscale = %d\nwidescreen = %d\nmsaa = %d\nfullscreen = %d\n\n"
               "[controles]\n; lever speed with keys 1..10; gamepad: 0 off, 1 sticks, 2 triggers; language: 0 auto, 1 es, 2 en\nlever_speed = %d\ngamepad = %d\nlanguage = %d\n\n"
               "[keys]\n; codigos de tecla virtual de Windows\n", g_set.scale, g_set.wide, g_set.msaa, g_set.fullscreen, g_set.lever_speed, g_set.pad_mode, g_set.lang);
    for (int i = 0; i < ACT_N; i++) fprintf(f, "%s = %d\n", g_act_names[i], g_set.keys[i]);
    return fclose(f) == 0 ? 0 : -1;
}

/* ---- key names ---- */
static void key_name(int vk, wchar_t *buf, int n) {
    switch (vk) {
    case VK_UP: wcsncpy(buf, TR(L"Flecha arriba"), n); return;
    case VK_DOWN: wcsncpy(buf, TR(L"Flecha abajo"), n); return;
    case VK_LEFT: wcsncpy(buf, TR(L"Flecha izquierda"), n); return;
    case VK_RIGHT: wcsncpy(buf, TR(L"Flecha derecha"), n); return;
    case VK_SPACE: wcsncpy(buf, TR(L"Espacio"), n); return;
    case VK_RETURN: wcsncpy(buf, TR(L"Intro"), n); return;
    case VK_SHIFT: wcsncpy(buf, TR(L"Mayús"), n); return;
    case VK_CONTROL: wcsncpy(buf, L"Ctrl", n); return;
    case VK_MENU: wcsncpy(buf, L"Alt", n); return;
    case VK_TAB: wcsncpy(buf, L"Tab", n); return;
    case VK_BACK: wcsncpy(buf, TR(L"Retroceso"), n); return;
    }
    if (vk >= VK_F1 && vk <= VK_F24) { swprintf(buf, n, L"F%d", vk - VK_F1 + 1); return; }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) { swprintf(buf, n, L"%c", vk); return; }
    UINT sc = MapVirtualKeyW(vk, 0);
    if (sc && GetKeyNameTextW((LONG)(sc << 16), buf, n) > 0) return;
    swprintf(buf, n, TR(L"Tecla 0x%02X"), vk);
}

/* ---- modal window ---- */
enum { ID_SCALE = 100, ID_WIDE = 101, ID_MSAA = 102, ID_FULL = 103, ID_REGION = 104, ID_SPEED = 105, ID_PAD = 106, ID_LANG = 107,
       ID_UNLOCK0 = 110, ID_KEY0 = 200, ID_DEFAULTS = 300, ID_OK = 301, ID_CANCEL = 302 };
static Settings tmp;
static int capture = -1, done, accepted;
static HWND hkey[ACT_N], hscale, hwide, hmsaa, hfull, hregion, hspeed, hpad, hlang, hunlock[CHEAT_N], hnote;
static const int msaa_vals[4] = { 1, 2, 4, 8 };

static void refresh_keys(void) {
    wchar_t b[64];
    for (int i = 0; i < ACT_N; i++) { key_name(tmp.keys[i], b, 64); SetWindowTextW(hkey[i], b); }
}
static void refresh_unlocks(void) {
    for (int i = 0; i < CHEAT_N; i++) {
        wchar_t b[96]; int u = emu_unlocked(i);
        swprintf(b, 96, L"%ls%ls", i == CHEAT_BEN ? TR(L"Desbloquear Ben Quadinaros") : TR(L"Desbloquear Gasgano"), u ? TR(L"  (ya desbloqueado)") : L"");
        SetWindowTextW(hunlock[i], b); EnableWindow(hunlock[i], !u);
    }
}

static LRESULT CALLBACK dlg_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CLOSE: done = 1; accepted = 0; return 0;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_OK || id == IDOK) {
            int sel = (int)SendMessageW(hscale, CB_GETCURSEL, 0, 0); if (sel >= 0) tmp.scale = sel + 1;
            sel = (int)SendMessageW(hmsaa, CB_GETCURSEL, 0, 0); if (sel >= 0) tmp.msaa = msaa_vals[sel];
            sel = (int)SendMessageW(hspeed, CB_GETCURSEL, 0, 0); if (sel >= 0) tmp.lever_speed = sel + 1;
            sel = (int)SendMessageW(hpad, CB_GETCURSEL, 0, 0); if (sel >= 0) tmp.pad_mode = sel;
            sel = (int)SendMessageW(hlang, CB_GETCURSEL, 0, 0); if (sel >= 0) tmp.lang = sel;
            sel = (int)SendMessageW(hregion, CB_GETCURSEL, 0, 0); if (sel >= 0) tmp.region = sel;
            tmp.wide = SendMessageW(hwide, BM_GETCHECK, 0, 0) == BST_CHECKED;
            tmp.fullscreen = SendMessageW(hfull, BM_GETCHECK, 0, 0) == BST_CHECKED;
            done = 1; accepted = 1; return 0;
        }
        if (id == ID_CANCEL || id == IDCANCEL) { done = 1; accepted = 0; return 0; }
        if (id == ID_DEFAULTS) { Settings d; settings_defaults(&d); memcpy(tmp.keys, d.keys, sizeof tmp.keys); capture = -1; refresh_keys(); return 0; }
        if (id >= ID_UNLOCK0 && id < ID_UNLOCK0 + CHEAT_N) {
            if (emu_unlock(id - ID_UNLOCK0) == 0)
                SetWindowTextW(hnote, TR(L"Desbloqueado: aparece en SELECT PODRACER (con ◀ desde Anakin)."));
            else
                SetWindowTextW(hnote, TR(L"Aún no se puede: deja que el juego arranque una vez hasta el modo demo."));
            refresh_unlocks();
            return 0;
        }
        if (id >= ID_KEY0 && id < ID_KEY0 + ACT_N) {
            if (capture >= 0) refresh_keys();
            capture = id - ID_KEY0;
            SetWindowTextW(hkey[capture], TR(L"Pulsa una tecla...  (Esc = cancelar)"));
            return 0;
        }
        break; }
    }
    return DefWindowProcW(h, m, w, l);
}

static HWND mk(HWND parent, HINSTANCE hi, const wchar_t *cls, const wchar_t *txt, DWORD style, int x, int y, int w, int h, int id, HFONT f) {
    HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id, hi, 0);
    SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
    return c;
}
static HWND combo(HWND d, HINSTANCE hi, int x, int y, int w, int id, HFONT f, const wchar_t **items, int n, int sel) {
    HWND c = mk(d, hi, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, x, y, w, 300, id, f);
    for (int i = 0; i < n; i++) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)TR(items[i]));
    SendMessageW(c, CB_SETCURSEL, sel, 0);
    return c;
}

int settings_dialog(HINSTANCE hi, HWND parent, int *restart) {
    static int reg;
    if (!reg) {
        WNDCLASSW wc = { 0 }; wc.lpfnWndProc = dlg_proc; wc.hInstance = hi; wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = L"swrcfg";
        RegisterClassW(&wc); reg = 1;
    }
    tmp = g_set; capture = -1; done = 0; accepted = 0;
    int region_now = emu_region(); if (region_now < 0 || region_now > 4) region_now = 0;
    tmp.region = region_now;
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    int rowh = 25, colw = 450, cw = 2 * colw + 30, ch = 16 + 30 + ACT_N * rowh + 20 + 48 + 20;
    RECT wr = { 0, 0, cw, ch }; AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    RECT pr; GetWindowRect(parent, &pr);
    int W = wr.right - wr.left, H = wr.bottom - wr.top;
    HWND d = CreateWindowExW(WS_EX_DLGMODALFRAME, L"swrcfg", TR(L"Opciones - Star Wars Racer Arcade Recomp"),
                             WS_POPUP | WS_CAPTION | WS_SYSMENU, (pr.left + pr.right - W) / 2, (pr.top + pr.bottom - H) / 2, W, H, parent, 0, hi, 0);
    /* left column: video, game, gamepad */
    int x = 10, y = 10, lx = 24, cx = 190, cwid = colw - cx - 10;
    mk(d, hi, L"BUTTON", TR(L"Vídeo"), BS_GROUPBOX, x, y, colw, 150, -1, f);
    mk(d, hi, L"STATIC", TR(L"Resolución interna:"), 0, lx, y + 26, 160, 20, -1, f);
    { const wchar_t *sc[8] = { L"1x  (640 x 480, original)", L"2x  (1280 x 960)", L"3x  (1920 x 1440)", L"4x  (2560 x 1920)",
                               L"5x  (3200 x 2400)", L"6x  (3840 x 2880)", L"7x  (4480 x 3360)", L"8x  (5120 x 3840)" };
      hscale = combo(d, hi, cx, y + 22, cwid, ID_SCALE, f, sc, 8, tmp.scale - 1); }
    mk(d, hi, L"STATIC", TR(L"Antialiasing (MSAA):"), 0, lx, y + 56, 160, 20, -1, f);
    { const wchar_t *ms[4] = { L"Desactivado", L"2x", L"4x", L"8x" }; int sel = tmp.msaa == 8 ? 3 : tmp.msaa == 4 ? 2 : tmp.msaa == 2 ? 1 : 0;
      hmsaa = combo(d, hi, cx, y + 52, cwid, ID_MSAA, f, ms, 4, sel); }
    hwide = mk(d, hi, L"BUTTON", TR(L"Panorámico 16:9 (más campo de visión en 3D; el 2D queda en 4:3)"), BS_AUTOCHECKBOX | WS_TABSTOP, lx, y + 84, colw - 30, 22, ID_WIDE, f);
    SendMessageW(hwide, BM_SETCHECK, tmp.wide ? BST_CHECKED : BST_UNCHECKED, 0);
    hfull = mk(d, hi, L"BUTTON", TR(L"Pantalla completa al arrancar (F11 cambia y se recuerda)"), BS_AUTOCHECKBOX | WS_TABSTOP, lx, y + 112, colw - 30, 22, ID_FULL, f);
    SendMessageW(hfull, BM_SETCHECK, tmp.fullscreen ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 160;
    mk(d, hi, L"BUTTON", TR(L"Juego"), BS_GROUPBOX, x, y, colw, 150, -1, f);
    mk(d, hi, L"STATIC", TR(L"País / región:"), 0, lx, y + 26, 160, 20, -1, f);
    hregion = combo(d, hi, cx, y + 22, cwid, ID_REGION, f, g_region_names, 5, tmp.region);
    mk(d, hi, L"STATIC", TR(L"(cambiarlo reinicia el juego; cambia textos y avisos)"), 0, lx, y + 50, colw - 30, 18, -1, f);
    for (int i = 0; i < CHEAT_N; i++) hunlock[i] = mk(d, hi, L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, lx, y + 74 + i * 30, colw - 30, 26, ID_UNLOCK0 + i, f);
    refresh_unlocks();
    y += 160;
    mk(d, hi, L"BUTTON", TR(L"Palancas y mando"), BS_GROUPBOX, x, y, colw, 100, -1, f);
    mk(d, hi, L"STATIC", TR(L"Velocidad palancas (teclas):"), 0, lx, y + 26, 165, 20, -1, f);
    { const wchar_t *sp[10] = { L"1 (muy lenta)", L"2", L"3", L"4 (por defecto)", L"5", L"6", L"7", L"8 (como antes)", L"9", L"10 (muy rápida)" };
      hspeed = combo(d, hi, cx, y + 22, cwid, ID_SPEED, f, sp, 10, tmp.lever_speed - 1); }
    mk(d, hi, L"STATIC", TR(L"Mando (XInput):"), 0, lx, y + 58, 160, 20, -1, f);
    { const wchar_t *pm[3] = { L"Desactivado", L"Sticks: izquierdo / derecho = palancas", L"Gatillos: LT / RT = palancas" };
      hpad = combo(d, hi, cx, y + 54, cwid, ID_PAD, f, pm, 3, tmp.pad_mode); }
    y += 110;
    mk(d, hi, L"STATIC", TR(L"Idioma:"), 0, lx, y + 4, 160, 20, -1, f);
    { const wchar_t *lg[3] = { L"Automático", L"Español", L"English" };
      hlang = combo(d, hi, cx, y, cwid, ID_LANG, f, lg, 3, tmp.lang); }
    y += 30;
    hnote = mk(d, hi, L"STATIC", L"", 0, x + 4, y, colw, 36, -1, f);
    /* right column: keys */
    int rx = x + colw + 10; y = 10;
    mk(d, hi, L"BUTTON", TR(L"Teclas (pulsa un botón y después la tecla)"), BS_GROUPBOX, rx, y, colw, 30 + ACT_N * rowh, -1, f);
    y += 22;
    for (int i = 0; i < ACT_N; i++, y += rowh) {
        mk(d, hi, L"STATIC", TR(act_labels[i]), 0, rx + 14, y + 4, 200, 20, -1, f);
        hkey[i] = mk(d, hi, L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, rx + 220, y, colw - 234, 22, ID_KEY0 + i, f);
    }
    refresh_keys();
    y = ch - 40;
    mk(d, hi, L"BUTTON", TR(L"Teclas por defecto"), BS_PUSHBUTTON | WS_TABSTOP, rx, y, 140, 28, ID_DEFAULTS, f);
    mk(d, hi, L"BUTTON", TR(L"Aceptar"), BS_DEFPUSHBUTTON | WS_TABSTOP, cw - 230, y, 105, 28, ID_OK, f);
    mk(d, hi, L"BUTTON", TR(L"Cancelar"), BS_PUSHBUTTON | WS_TABSTOP, cw - 115, y, 105, 28, ID_CANCEL, f);

    EnableWindow(parent, FALSE);
    ShowWindow(d, SW_SHOW); SetForegroundWindow(d);
    MSG msg; int swallow = 0;   /* key just assigned: its release / char must not reach the focused button */
    while (!done && GetMessageW(&msg, 0, 0, 0) > 0) {
        if (capture >= 0 && (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)) {
            int vk = (int)msg.wParam;
            if (vk != VK_ESCAPE && vk > 0 && vk < 256) tmp.keys[capture] = vk;
            capture = -1; refresh_keys(); swallow = vk;
            continue;
        }
        if (msg.message == WM_CHAR || msg.message == WM_SYSCHAR) { if (swallow || capture >= 0) continue; }
        if (msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) {
            if (capture >= 0) continue;
            if (swallow && (int)msg.wParam == swallow) { swallow = 0; continue; }
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { done = 1; accepted = 0; continue; }
        if (!IsDialogMessageW(d, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(parent, TRUE);
    DestroyWindow(d);
    SetForegroundWindow(parent);
    if (restart) *restart = 0;
    if (accepted) {
        if (tmp.region != region_now) { emu_set_region(tmp.region); if (restart) *restart = 1; }
        tmp.region = -1;
        g_set = tmp;
        if (settings_save() != 0) {
            wchar_t m2[MAX_PATH + 128]; swprintf(m2, MAX_PATH + 128, TR(L"No se pudieron guardar las opciones en:\n%lsswracer.ini"), settings_dir());
            MessageBoxW(parent, m2, TR(L"Star Wars Racer Arcade Recomp"), MB_ICONWARNING);
        }
    }
    return accepted;
}
