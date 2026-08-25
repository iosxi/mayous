/* rawwatch.c - 低レベルフックで握り潰された押下が Raw Input には見えるか
 *
 *  WH_MOUSE_LL でフックが 1 を返すと、そのイベントはスレッドのキューに
 *  積まれなくなり、GetAsyncKeyState も更新されない。では WM_INPUT
 *  (Raw Input) はどうか。ここが生きているなら、「他のアプリが離上を
 *  食べてしまって mayous が詰まる」を検出する地面として使える。
 *
 *      rawwatch.exe <ログ> <秒数>
 */

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static FILE      *g_log;
static ULONGLONG  g_t0;
static BOOL       g_asyncDown;   /* GetAsyncKeyState から見た右ボタン */
static BOOL       g_rawDown;     /* Raw Input から見た右ボタン       */

static void logline(const char *what)
{
    fprintf(g_log, "%6llu ms  %s\n", GetTickCount64() - g_t0, what);
    fflush(g_log);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_INPUT) {
        RAWINPUT ri;
        UINT sz = sizeof(ri);
        if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &ri, &sz,
                            sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE) {
            USHORT f = ri.data.mouse.usButtonFlags;
            char   who[80];
            /* 注入された入力を見分けられるか:
                 hDevice           … 実機なら非 NULL、SendInput 由来なら NULL のはず
                 ulExtraInformation … SendInput の dwExtraInfo がそのまま入るはず */
            wsprintfA(who, " [hDev=%p extra=%08X]",
                      ri.header.hDevice, (unsigned)ri.data.mouse.ulExtraInformation);
            if (f & (RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP |
                     RI_MOUSE_LEFT_BUTTON_DOWN  | RI_MOUSE_LEFT_BUTTON_UP  |
                     RI_MOUSE_BUTTON_4_DOWN     | RI_MOUSE_BUTTON_4_UP)) {
                char line[160];
                const char *what =
                    (f & RI_MOUSE_RIGHT_BUTTON_DOWN) ? "RAW    RIGHT DOWN" :
                    (f & RI_MOUSE_RIGHT_BUTTON_UP)   ? "RAW    RIGHT UP  " :
                    (f & RI_MOUSE_LEFT_BUTTON_DOWN)  ? "RAW    LEFT  DOWN" :
                    (f & RI_MOUSE_LEFT_BUTTON_UP)    ? "RAW    LEFT  UP  " :
                    (f & RI_MOUSE_BUTTON_4_DOWN)     ? "RAW    X1    DOWN" :
                                                       "RAW    X1    UP  ";
                if (f & RI_MOUSE_RIGHT_BUTTON_DOWN) g_rawDown = TRUE;
                if (f & RI_MOUSE_RIGHT_BUTTON_UP)   g_rawDown = FALSE;
                lstrcpyA(line, what);
                lstrcatA(line, who);
                logline(line);
            }
        }
        return 0;
    }
    if (m == WM_TIMER) {
        BOOL d = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        if (d != g_asyncDown) {
            g_asyncDown = d;
            logline(d ? "ASYNC  RIGHT DOWN" : "ASYNC  RIGHT UP");
        }
        return 0;
    }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "rawwatch.log";
    int seconds      = (argc > 2) ? atoi(argv[2]) : 15;
    WNDCLASSEXW wc;
    HWND hwnd;
    RAWINPUTDEVICE rid;
    MSG msg;
    ULONGLONG end;

    g_log = fopen(path, "w");
    if (!g_log) return 1;
    g_t0 = GetTickCount64();

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.lpszClassName = L"RawWatchWnd";
    RegisterClassExW(&wc);

    /* メッセージ専用ウィンドウでも RIDEV_INPUTSINK は効く */
    hwnd = CreateWindowExW(0, L"RawWatchWnd", L"rawwatch", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, wc.hInstance, NULL);
    if (!hwnd) return 2;

    /* INPUTSINK: 前面でなくても入力が届く */
    rid.usUsagePage = 0x01;      /* Generic Desktop */
    rid.usUsage     = 0x02;      /* Mouse           */
    rid.dwFlags     = RIDEV_INPUTSINK;
    rid.hwndTarget  = hwnd;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        logline("RegisterRawInputDevices 失敗");
        return 3;
    }

    SetTimer(hwnd, 1, 15, NULL);
    logline("ready");
    printf("READY\n");
    fflush(stdout);

    end = GetTickCount64() + (ULONGLONG)seconds * 1000;
    while (GetTickCount64() < end && GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (GetTickCount64() >= end) break;
    }
    logline("done");
    fclose(g_log);
    return 0;
}
