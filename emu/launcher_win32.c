/* Game selection launcher (Windows): starts one of the recompiled executables that sit next to it.
 * Entries come from launcher.ini if present ("name = exe" lines), otherwise the built-in list.
 * Keyboard (arrows / Enter / Esc) and XInput gamepad (d-pad or stick, A, B). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
/* Spanish when Windows is in Spanish, English otherwise */
static int en_ui(void) { return PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_SPANISH; }
#define T2(es, en) (en_ui() ? (en) : (es))

typedef struct { wchar_t name[128], exe[MAX_PATH], info[160]; int present; } Entry;
static Entry ent[8]; static int nent, sel;
static HWND hwnd, hbtn[8], hinfo; static HFONT fbig, fsmall;
static wchar_t dir[MAX_PATH];

static void add(const wchar_t *name, const wchar_t *exe, const wchar_t *info) {
    if (nent >= 8) return;
    Entry *e = &ent[nent++];
    wcsncpy(e->name, name, 127); wcsncpy(e->exe, exe, MAX_PATH - 1); wcsncpy(e->info, info, 159);
    wchar_t p[MAX_PATH * 2]; swprintf(p, MAX_PATH * 2, L"%ls%ls", dir, exe);
    e->present = GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}
static void load_entries(void) {
    wchar_t p[MAX_PATH * 2]; swprintf(p, MAX_PATH * 2, L"%lslauncher.ini", dir);
    FILE *f = _wfopen(p, L"r, ccs=UTF-8");
    if (f) {
        wchar_t line[512];
        while (fgetws(line, 512, f)) {
            wchar_t *eq = wcschr(line, L'='); if (!eq || line[0] == L';' || line[0] == L'#') continue;
            *eq = 0; wchar_t *n = line, *x = eq + 1;
            while (*n == L' ') n++; for (wchar_t *t = eq - 1; t >= n && (*t == L' ' || *t == L'\t'); t--) *t = 0;
            while (*x == L' ') x++; x[wcscspn(x, L"\r\n")] = 0;
            if (*n && *x) add(n, x, L"");
        }
        fclose(f);
    }
    if (!nent) {
        add(L"Star Wars Racer Arcade  (Sega Hikaru, 2000)", L"StarWarsRacer-Recomp.exe", T2(L"Recreativa. Necesita podrace.zip / swracer.zip en la carpeta 'roms'.", L"Arcade. Needs podrace.zip / swracer.zip in the 'roms' folder."));
        add(L"Star Wars Episode I Racer  (Dreamcast, 2000)", L"SWRacer-DC-Recomp.exe", T2(L"Dreamcast. Necesita la imagen GD-ROM (.chd / .gdi) en la carpeta 'dreamcast'.", L"Dreamcast. Needs the GD-ROM image (.chd / .gdi) in the 'dreamcast' folder."));
    }
}
static void refresh(void) {
    for (int i = 0; i < nent; i++) {
        wchar_t t[200]; swprintf(t, 200, L"%ls%ls", i == sel ? L"▶  " : L"     ", ent[i].name);
        SetWindowTextW(hbtn[i], t); EnableWindow(hbtn[i], ent[i].present);
    }
    wchar_t t[300]; swprintf(t, 300, L"%ls%ls", ent[sel].info, ent[sel].present ? L"" : T2(L"\n(No encontrado en esta carpeta)", L"\n(Not found in this folder)"));
    SetWindowTextW(hinfo, t);
}
static void launch(int i) {
    if (!ent[i].present) return;
    wchar_t p[MAX_PATH * 2], cl[MAX_PATH * 2 + 4]; swprintf(p, MAX_PATH * 2, L"%ls%ls", dir, ent[i].exe); swprintf(cl, MAX_PATH * 2 + 4, L"\"%ls\"", p);
    STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
    if (CreateProcessW(p, cl, 0, 0, FALSE, 0, 0, dir, &si, &pi)) {
        ShowWindow(hwnd, SW_HIDE);
        WaitForSingleObject(pi.hProcess, INFINITE);       /* back to the menu when the game closes */
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd);
    } else MessageBoxW(hwnd, T2(L"No se pudo arrancar el juego.", L"The game could not be started."), L"Star Wars Racer - Recomp", MB_ICONERROR);
}
static void move(int d) { int s = sel; do { s = (s + d + nent) % nent; } while (!ent[s].present && s != sel); sel = s; refresh(); }

/* XInput (loaded at runtime) */
typedef struct { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; } XPad;
typedef struct { DWORD packet; XPad pad; } XState;
typedef DWORD (WINAPI *XGetState)(DWORD, XState *);
static XGetState xget;
static void pad_poll(void) {
    static WORD prev; static int repeat;
    if (!xget) return;
    XState s; WORD b = 0;
    for (DWORD i = 0; i < 4; i++) if (xget(i, &s) == 0) { b = s.pad.buttons; if (s.pad.ly > 16000) b |= 1; if (s.pad.ly < -16000) b |= 2; break; }
    WORD press = b & ~prev;
    if (press & 1) move(-1);
    if (press & 2) move(1);
    if (press & 0x1000) launch(sel);            /* A */
    if (press & 0x2000) PostMessageW(hwnd, WM_CLOSE, 0, 0);   /* B */
    (void)repeat; prev = b;
}

static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND: if (LOWORD(w) >= 100 && LOWORD(w) < 100 + nent) { sel = LOWORD(w) - 100; refresh(); launch(sel); } return 0;
    case WM_TIMER: pad_poll(); return 0;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)w, TRANSPARENT); SetTextColor((HDC)w, RGB(250, 200, 60)); return (LRESULT)GetStockObject(BLACK_BRUSH);
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show) {
    (void)hp; (void)cmd;
    GetModuleFileNameW(0, dir, MAX_PATH); wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) sl[1] = 0;
    load_entries();
    const char *dll[3] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    for (int i = 0; i < 3 && !xget; i++) { HMODULE mm = LoadLibraryA(dll[i]); if (mm) xget = (XGetState)(void *)GetProcAddress(mm, "XInputGetState"); }
    WNDCLASSW wc = { 0 }; wc.lpfnWndProc = wp; wc.hInstance = hi; wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); wc.lpszClassName = L"swrlauncher"; wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));
    RegisterClassW(&wc);
    int W = 720, H = 140 + nent * 64 + 90;
    RECT r = { 0, 0, W, H }; AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    hwnd = CreateWindowW(L"swrlauncher", T2(L"Star Wars Racer - Recomp: elige juego", L"Star Wars Racer - Recomp: choose a game"), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                         CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, 0, 0, hi, 0);
    fbig = CreateFontW(26, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    fsmall = CreateFontW(17, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HWND t = CreateWindowW(L"STATIC", L"STAR WARS RACER  -  RECOMP", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 24, W, 40, hwnd, 0, hi, 0);
    SendMessageW(t, WM_SETFONT, (WPARAM)fbig, TRUE);
    for (int i = 0; i < nent; i++) {
        hbtn[i] = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_LEFT, 40, 90 + i * 64, W - 80, 52, hwnd, (HMENU)(INT_PTR)(100 + i), hi, 0);
        SendMessageW(hbtn[i], WM_SETFONT, (WPARAM)fsmall, TRUE);
    }
    hinfo = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 100 + nent * 64, W - 40, 50, hwnd, 0, hi, 0);
    SendMessageW(hinfo, WM_SETFONT, (WPARAM)fsmall, TRUE);
    t = CreateWindowW(L"STATIC", T2(L"Flechas / mando: elegir    Intro / A: jugar    Esc / B: salir", L"Arrows / gamepad: select    Enter / A: play    Esc / B: quit"), WS_CHILD | WS_VISIBLE | SS_CENTER, 0, H - 30, W, 22, hwnd, 0, hi, 0);
    SendMessageW(t, WM_SETFONT, (WPARAM)fsmall, TRUE);
    sel = 0; if (!ent[0].present) move(1); refresh();
    ShowWindow(hwnd, show); SetFocus(hbtn[sel]);
    SetTimer(hwnd, 1, 50, 0);
    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_UP) { move(-1); SetFocus(hbtn[sel]); continue; }
            if (msg.wParam == VK_DOWN) { move(1); SetFocus(hbtn[sel]); continue; }
            if (msg.wParam == VK_RETURN) { launch(sel); continue; }
            if (msg.wParam == VK_ESCAPE) { DestroyWindow(hwnd); continue; }
        }
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return 0;
}
