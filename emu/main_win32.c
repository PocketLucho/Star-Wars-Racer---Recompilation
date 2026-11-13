/* Windows frontend: Win32 window + Vulkan swapchain, 60 Hz pacing, keyboard input.
 * Usage: StarWarsRacer-Recomp.exe [path\to\podrace.zip]   (default: podrace.zip / swracer.zip next to the exe)
 * Keys are configurable (F10 = settings window, saved in swracer.ini). Defaults: 5 coin, 1 start,
 *       Up/Down both levers, A/Z left lever, K/M right lever, Left/Right steer + select buttons,
 *       Space boost/decide, F2 test, F1 service, P pause; F11 fullscreen, Esc quit. XInput gamepad (sticks or triggers).
 * Env: SWR_SCALE (overrides the internal resolution), SWR_LOG (write hikaru.log), SWR_NOSOUND. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu_core.h"
#include "mie.h"
#include "gpu.h"
#include "vkrender.h"
#include "settings_win32.h"
#include "swrender.h"
#include <wchar.h>

int gfx_vk_start(const VkrConfig *cfg);

static HWND hwnd; static int running = 1, fullscreen, want_settings, want_shot, paused;
static char status_msg[256]; static double status_until;

/* ---- audio: waveOut fed from the emulator ring (44.1 kHz stereo, 1024-sample blocks) ---- */
#define AB_N 12
#define AB_LEN 1024
static HWAVEOUT wo; static WAVEHDR whdr[AB_N]; static int16_t abuf[AB_N][AB_LEN * 2]; static uint32_t aud_r; static int audio_on;
static void audio_init(void) {
    WAVEFORMATEX f = { WAVE_FORMAT_PCM, 2, 44100, 44100 * 4, 4, 16, 0 };
    if (getenv("SWR_NOSOUND") || waveOutOpen(&wo, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return;
    for (int i = 0; i < AB_N; i++) { whdr[i].lpData = (LPSTR)abuf[i]; whdr[i].dwBufferLength = AB_LEN * 4; waveOutPrepareHeader(wo, &whdr[i], sizeof whdr[i]); whdr[i].dwFlags |= WHDR_DONE; }
    aud_r = g_aud_w; audio_on = 1;
}
static void audio_pump(void) {
    if (!audio_on) return;
    int queued = 0; for (int i = 0; i < AB_N; i++) if (!(whdr[i].dwFlags & WHDR_DONE)) queued++;
    if (g_aud_w - aud_r > AUD_N / 2) aud_r = g_aud_w - AB_LEN * 4;        /* fell far behind: resync */
    while (g_aud_w - aud_r >= AB_LEN) {
        if (queued >= AB_N - 2) { aud_r += AB_LEN; continue; }           /* emulation running ahead: drop a block */
        int k = -1; for (int i = 0; i < AB_N; i++) if (whdr[i].dwFlags & WHDR_DONE) { k = i; break; }
        if (k < 0) break;
        for (int i = 0; i < AB_LEN; i++, aud_r++) { abuf[k][i * 2] = g_aud[(aud_r % AUD_N) * 2]; abuf[k][i * 2 + 1] = g_aud[(aud_r % AUD_N) * 2 + 1]; }
        whdr[k].dwFlags &= ~WHDR_DONE;
        waveOutWrite(wo, &whdr[k], sizeof whdr[k]); queued++;
    }
}
static WINDOWPLACEMENT prev_wp = { sizeof prev_wp };
static int keys[256];

static void toggle_fullscreen(void) {
    DWORD style = GetWindowLong(hwnd, GWL_STYLE);
    if (!fullscreen) {
        MONITORINFO mi = { sizeof mi };
        if (GetWindowPlacement(hwnd, &prev_wp) && GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    } else {
        SetWindowLong(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &prev_wp);
        SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    fullscreen = !fullscreen;
}
static void toggle_fullscreen_saved(void) { toggle_fullscreen(); g_set.fullscreen = fullscreen; settings_save(); }

/* ---- XInput gamepad (loaded at runtime: xinput1_4 / 1_3 / 9_1_0) ---- */
typedef struct { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; } XPad;
typedef struct { DWORD packet; XPad pad; } XState;
typedef DWORD (WINAPI *XGetState)(DWORD, XState *);
static XGetState xget; static int xtried, xconnected = -1;
static int pad_read(XPad *p) {
    if (!xtried) {
        xtried = 1;
        const char *dll[3] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
        for (int i = 0; i < 3 && !xget; i++) { HMODULE m = LoadLibraryA(dll[i]); if (m) xget = (XGetState)(void *)GetProcAddress(m, "XInputGetState"); }
    }
    if (!xget || g_set.pad_mode == 0) return 0;
    for (DWORD i = 0; i < 4; i++) { XState s; if (xget(i, &s) == 0) { *p = s.pad; return 1; } }
    return 0;
}
static float axis(SHORT v) {   /* -1..1 with a 15% dead zone */
    float f = v / 32767.0f; if (f > 1) f = 1; if (f < -1) f = -1;
    float a = f < 0 ? -f : f; if (a < 0.15f) return 0;
    a = (a - 0.15f) / 0.85f; return f < 0 ? -a : a;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CLOSE: running = 0; return 0;
    case WM_SIZE: vkr_resize(LOWORD(l), HIWORD(l)); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (w == VK_ESCAPE) running = 0;
        if (w == VK_F11 && !(l & 0x40000000)) toggle_fullscreen_saved();
        if ((int)w == g_set.keys[ACT_PAUSE] && !(l & 0x40000000)) paused = !paused;
        if (w == VK_F10 && !(l & 0x40000000)) want_settings = 1;
        if (w == VK_F12 && !(l & 0x40000000)) want_shot = 1;
        if ((int)w == g_set.keys[ACT_COIN] && !(l & 0x40000000)) g_input.coins[0]++;
        if (w < 256) keys[w] = 1;
        return 0;                                   /* also swallows F10 / Alt so the system menu does not activate */
    case WM_KEYUP: case WM_SYSKEYUP:
        if (w < 256) keys[w] = 0;
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static float lever[2], steer;   /* levers 0..1 (they stay where they are left), steer -1..1 */
#define K(a) keys[g_set.keys[a] & 255]
static void update_input(void) {
    const float d = lever_step();
    if (K(ACT_BOTH_UP))    { lever[0] += d; lever[1] += d; }
    if (K(ACT_BOTH_DOWN))  { lever[0] -= d; lever[1] -= d; }
    if (K(ACT_LEFT_UP))    lever[0] += d;
    if (K(ACT_LEFT_DOWN))  lever[0] -= d;
    if (K(ACT_RIGHT_UP))   lever[1] += d;
    if (K(ACT_RIGHT_DOWN)) lever[1] -= d;
    for (int i = 0; i < 2; i++) { if (lever[i] < 0) lever[i] = 0; if (lever[i] > 1) lever[i] = 1; }
    float target_s = (K(ACT_STEER_LEFT) ? -1.0f : 0.0f) + (K(ACT_STEER_RIGHT) ? 1.0f : 0.0f);
    /* gamepad: analog levers (absolute), left stick X steers in trigger mode, buttons */
    XPad p; int pad = pad_read(&p);
    if (pad != xconnected) { if (xconnected >= 0 || pad) snprintf(status_msg, sizeof status_msg, pad ? T2("Mando conectado", "Gamepad connected") : T2("Mando desconectado", "Gamepad disconnected")); xconnected = pad; }
    float pl = 0, pr = 0; uint16_t pb = 0;
    if (pad) {
        if (g_set.pad_mode == 1) { pl = axis(p.ly); pr = axis(p.ry); if (pl < 0) pl = 0; if (pr < 0) pr = 0; }
        else { pl = p.lt / 255.0f; pr = p.rt / 255.0f; if (pl < 0.05f) pl = 0; if (pr < 0.05f) pr = 0;
               float sx = axis(p.lx); if (sx != 0) target_s = sx; }
        if (p.buttons & 0x1000) pb |= 0x0200;                  /* A: boost / decide */
        if (p.buttons & 0x2000) pb |= 0x0200;                  /* B: boost too */
        if (p.buttons & 0x0010) pb |= 0x8000;                  /* Start */
        if (p.buttons & (0x0100 | 0x0004)) pb |= 0x0400;       /* LB / d-pad left: select previous */
        if (p.buttons & (0x0200 | 0x0008)) pb |= 0x0800;       /* RB / d-pad right: select next */
        static int back_prev; int back = (p.buttons & 0x0020) != 0;
        if (back && !back_prev) g_input.coins[0]++;            /* Back: coin */
        back_prev = back;
    }
    steer += (target_s - steer) * 0.25f;
    float L = lever[0] > pl ? lever[0] : pl, R = lever[1] > pr ? lever[1] : pr;
    float l = L - (steer < 0 ? -steer : 0) * 0.5f, r = R - (steer > 0 ? steer : 0) * 0.5f;
    if (l < 0) l = 0; if (r < 0) r = 0;
    g_input.analog[0] = (uint16_t)(l * 0xff00);
    g_input.analog[1] = (uint16_t)(r * 0xff00);
    uint16_t p1 = pb;
    if (K(ACT_START))    p1 |= 0x8000;
    if (K(ACT_BOOST))    p1 |= 0x0200;          /* boost / centre button (decide) */
    if (K(ACT_SEL_NEXT)) p1 |= 0x0800;          /* select buttons: 0x0800 = next, 0x0400 = previous (verified on SELECT COURSE) */
    if (K(ACT_SEL_PREV)) p1 |= 0x0400;
    if (K(ACT_SERVICE))  p1 |= 0x4000;          /* service: the test menu reads the JVS player-1 service bit */
    g_input.p[0] = p1;
    g_input.test = K(ACT_TEST);
    g_input.tilt = 0;                               /* TEST only on the MIE cabinet port; see gpu.c */
    g_input.service = K(ACT_SERVICE);
}

/* ---- non-volatile memory (service-menu settings, bookkeeping): swracer.nv in the settings folder ---- */
static void nv_path(wchar_t *p, wchar_t *tmp) { swprintf(p, MAX_PATH, L"%lsswracer.nv", settings_dir()); swprintf(tmp, MAX_PATH, L"%lsswracer.nv.tmp", settings_dir()); }
static void nv_load(void) {
    wchar_t p[MAX_PATH], t[MAX_PATH]; nv_path(p, t);
    FILE *f = _wfopen(p, L"rb"); if (!f) return;
    emu_nv_read(f); fclose(f);
}
static int nv_save(void) {
    wchar_t p[MAX_PATH], t[MAX_PATH]; nv_path(p, t);
    FILE *f = _wfopen(t, L"wb"); if (!f) return -1;
    int ok = emu_nv_write(f) == 0; if (fclose(f) != 0) ok = 0;
    if (!ok) { _wremove(t); return -1; }
    return MoveFileExW(t, p, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}

/* ---- screenshots (F12): PNG in <settings folder>\capturas ---- */
static void save_screenshot(void) {
    int w, h; vkr_size(&w, &h);
    uint8_t *rgba = malloc((size_t)w * h * 4); if (!rgba) return;
    if (vkr_take_screenshot(rgba) == 0) {
        wchar_t dir[MAX_PATH], p[MAX_PATH]; swprintf(dir, MAX_PATH, L"%lscapturas", settings_dir()); CreateDirectoryW(dir, 0);
        SYSTEMTIME st; GetLocalTime(&st);
        swprintf(p, MAX_PATH, L"%ls\\swracer_%04d%02d%02d_%02d%02d%02d_%03d.png", dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        FILE *f = _wfopen(p, L"wb");
        if (f) { sr_png_rgba_fp(f, rgba, w, h); snprintf(status_msg, sizeof status_msg, "%s", T2("Captura guardada en la carpeta 'capturas'", "Screenshot saved in the 'capturas' folder")); }
        else snprintf(status_msg, sizeof status_msg, "%s", T2("No se pudo guardar la captura", "The screenshot could not be saved"));
    }
    free(rgba);
}

static const char *find_rom(const char *arg) {
    static char path[MAX_PATH];
    if (arg && *arg) return arg;
    char exe[MAX_PATH]; GetModuleFileNameA(0, exe, MAX_PATH);
    char *s = strrchr(exe, '\\'); if (s) s[1] = 0;
    const char *names[] = { "podrace.zip", "swracer.zip", "roms\\podrace.zip", "roms\\swracer.zip" };
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s%s", exe, names[i]);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return path;
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    (void)hp;
    char arg[MAX_PATH] = { 0 };
    if (cmd && *cmd) { snprintf(arg, sizeof arg, "%s", cmd); size_t n = strlen(arg); if (n && arg[0] == '"') { memmove(arg, arg + 1, n); n--; if (n && arg[n - 1] == '"') arg[n - 1] = 0; } }
    const char *rom = find_rom(arg);
    if (!rom) {
        settings_load();
        MessageBoxW(0, T2(L"No se encuentra podrace.zip (o swracer.zip).\nCopia tu romset de MAME en la carpeta 'roms' (o junto al .exe), o arrastra el zip sobre el .exe.",
                          L"podrace.zip (or swracer.zip) not found.\nCopy your MAME romset to the 'roms' folder (or next to the .exe), or drag the zip onto the .exe."),
                    L"Star Wars Racer Arcade - Recomp", MB_ICONERROR);
        return 1;
    }
    /* game module: the recompiled blocks built by CONSTRUIR_JUEGO.bat / BUILD_GAME.bat from the user's ROM */
    static char modpath[MAX_PATH]; { GetModuleFileNameA(0, modpath, MAX_PATH); char *s = strrchr(modpath, '\\'); if (s) s[1] = 0; strncat(modpath, "swr_game.dll", MAX_PATH - strlen(modpath) - 1); }
    if (getenv("SWR_BUILDER_DUMP")) {       /* builder: run the game headless on the interpreter and dump the RAM of both CPUs */
        EmuConfig bc = { 0 }; bc.rom = rom; bc.hleboot = 1; bc.idle_skip = 1; bc.stop_cpu = -1; bc.prof_cpu = -1;
        if (emu_init(&bc)) return 2;
        for (int f = 0; f < 3000; f++) if (emu_run_frame() < 0) break;
        return emu_builder_dump(getenv("SWR_BUILDER_DUMP")) ? 3 : 0;
    }
    WNDCLASSA wc = { 0 }; wc.style = CS_OWNDC; wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.lpszClassName = "swracer"; wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));
    RegisterClassA(&wc);
    RECT r = { 0, 0, 1280, 960 }; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowA("swracer", "Star Wars Racer Arcade - Recomp", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                         r.right - r.left, r.bottom - r.top, 0, 0, hi, 0);
    ShowWindow(hwnd, show);

    settings_load();
    EmuConfig c = { 0 };
    c.rom = rom; c.hleboot = 1; c.aot = 1; c.idle_skip = 1; c.stop_cpu = -1; c.prof_cpu = -1;
    if (GetFileAttributesA(modpath) != INVALID_FILE_ATTRIBUTES) c.module = modpath;
    c.logpath = getenv("SWR_LOG") ? "hikaru.log" : 0;
    emu_init(&c);
    nv_load();
    VkrConfig vc = { 0 }; vc.scale = getenv("SWR_SCALE") ? atoi(getenv("SWR_SCALE")) : g_set.scale; vc.vsync = 0; vc.wide = g_set.wide; vc.msaa = g_set.msaa;
    vc.win32_hinstance = hi; vc.win32_hwnd = hwnd;
    { RECT cr; GetClientRect(hwnd, &cr); vkr_resize(cr.right, cr.bottom); }
    if (gfx_vk_start(&vc)) {
        MessageBoxW(hwnd, T2(L"No se pudo inicializar Vulkan.\nActualiza el driver de la tarjeta gráfica.", L"Vulkan could not be initialised.\nUpdate your graphics driver."), L"Star Wars Racer Arcade - Recomp", MB_ICONERROR);
        return 1;
    }
    { RECT cr; GetClientRect(hwnd, &cr); vkr_resize(cr.right, cr.bottom); }
    if (g_set.fullscreen) toggle_fullscreen();

    LARGE_INTEGER freq, t0, now; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0);
    timeBeginPeriod(1);
    audio_init();
    const double frame_s = 1.0 / 60.0; double next = 0; long frames = 0; double fps_t = 0; long fps_n = 0;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        if (want_settings) {                         /* F10: game paused while the window is open */
            want_settings = 0;
            if (audio_on) waveOutPause(wo);
            int restart = 0;
            if (settings_dialog(hi, hwnd, &restart)) {
                vkr_reconfigure(g_set.scale, g_set.wide, g_set.msaa);
                if (!!g_set.fullscreen != !!fullscreen) toggle_fullscreen();
                char d[MAX_PATH * 2]; WideCharToMultiByte(CP_UTF8, 0, settings_dir(), -1, d, sizeof d, 0, 0);
                snprintf(status_msg, sizeof status_msg, T2("Opciones guardadas en %sswracer.ini", "Options saved to %sswracer.ini"), d);
                if (restart) {   /* country changed: the BIOS reads it at boot -> save and start again */
                    nv_save();
                    wchar_t exe[MAX_PATH]; GetModuleFileNameW(0, exe, MAX_PATH);
                    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
                    if (CreateProcessW(exe, GetCommandLineW(), 0, 0, FALSE, 0, 0, 0, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); running = 0; }
                }
            }
            if (audio_on) waveOutRestart(wo);
            memset(keys, 0, sizeof keys);
            QueryPerformanceCounter(&now); next = (double)(now.QuadPart - t0.QuadPart) / freq.QuadPart;
            aud_r = g_aud_w;
            continue;
        }
        static int was_paused;
        if (paused) {                                /* P: pause (audio stopped, last image stays) */
            if (!was_paused) { was_paused = 1; if (audio_on) waveOutPause(wo); SetWindowTextW(hwnd, T2(L"Star Wars Racer Arcade - Recomp   [PAUSA]   P: continuar", L"Star Wars Racer Arcade - Recomp   [PAUSED]   P: resume")); }
            Sleep(15);
            continue;
        }
        if (was_paused) {
            was_paused = 0; if (audio_on) waveOutRestart(wo);
            QueryPerformanceCounter(&now); next = (double)(now.QuadPart - t0.QuadPart) / freq.QuadPart; aud_r = g_aud_w;
        }
        update_input();
        if (want_shot) { want_shot = 0; vkr_request_screenshot(); }
        if (emu_run_frame() < 0) break;
        if (vkr_screenshot_ready()) save_screenshot();
        audio_pump();
        if (frames % 300 == 299 && emu_nv_dirty()) nv_save();   /* service-menu settings survive a crash */
        frames++; fps_n++;
        QueryPerformanceCounter(&now);
        double t = (double)(now.QuadPart - t0.QuadPart) / freq.QuadPart;
        next += frame_s;
        if (next > t) { DWORD ms = (DWORD)((next - t) * 1000.0); if (ms) Sleep(ms); }
        else if (t - next > 0.25) next = t;          /* too slow: don't try to catch up forever */
        if (status_msg[0] && status_until == 0) status_until = t + 4.0;
        if (status_msg[0] && t > status_until) { status_msg[0] = 0; status_until = 0; }
        if (t - fps_t >= 1.0 || (status_msg[0] && status_until == t + 4.0)) {
            char title[512];
            if (status_msg[0]) snprintf(title, sizeof title, "Star Wars Racer Arcade - Recomp   [%ld fps]   %s", fps_n, status_msg);
            else if (emu_block_count() > 0) snprintf(title, sizeof title, T2("Star Wars Racer Arcade - Recomp   [%ld fps, %s, %d bloques recompilados]   F10: opciones  F12: captura",
                                                             "Star Wars Racer Arcade - Recomp   [%ld fps, %s, %d recompiled blocks]   F10: options  F12: screenshot"), fps_n, vkr_device_name(), emu_block_count());
            else snprintf(title, sizeof title, T2("Star Wars Racer Arcade - Recomp   [%ld fps, %s, intérprete: ejecuta CONSTRUIR_JUEGO.bat]   F10: opciones",
                                                  "Star Wars Racer Arcade - Recomp   [%ld fps, %s, interpreter: run BUILD_GAME.bat]   F10: options"), fps_n, vkr_device_name());
            wchar_t wt[512]; MultiByteToWideChar(CP_UTF8, 0, title, -1, wt, 512); SetWindowTextW(hwnd, wt);
            if (t - fps_t >= 1.0) { fps_t = t; fps_n = 0; }
        }
    }
    timeEndPeriod(1);
    nv_save();
    if (audio_on) { waveOutReset(wo); for (int i = 0; i < AB_N; i++) waveOutUnprepareHeader(wo, &whdr[i], sizeof whdr[i]); waveOutClose(wo); }
    vkr_shutdown();
    return 0;
}
