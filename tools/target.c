/* ==================================================================
 * target.c - 検証用の「アプリ役」ウィンドウ(配布物には含めない)
 *
 *  probe.c はフック鎖の中に居るため、OS がフックを呼ぶ順番次第で
 *  上流にも下流にもなり得る(実測で入れ替わることを確認した)。
 *  そこで、実際にマウスメッセージを受け取る本物のウィンドウを用意し、
 *  「アプリから見えた事実」を直接記録する。こちらは並び順に依存しない。
 *
 *  使い方:  target.exe <logfile> <実行秒数>
 * ================================================================== */

#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static FILE      *g_log;
static ULONGLONG  g_t0;
static int        g_bal[5];      /* 0=左 1=右 2=中 3=サイド1 4=サイド2 の押下収支 */

static void logline(const char *fmt, ...)
{
    va_list ap;
    /* mayous のデバッグログと突き合わせられるよう、同じ GetTickCount を使う */
    fprintf(g_log, "%8lu  [+%6lld] ", (unsigned long)GetTickCount(),
            (long long)(GetTickCount64() - g_t0));
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static void button(const char *name, int idx, int down, LPARAM lp)
{
    g_bal[idx] += down ? 1 : -1;
    logline("%-9s %-4s at(%4d,%4d)  balance L=%d R=%d M=%d X1=%d X2=%d%s",
            name, down ? "DOWN" : "UP",
            (int)(short)LOWORD(lp), (int)(short)HIWORD(lp),
            g_bal[0], g_bal[1], g_bal[2], g_bal[3], g_bal[4],
            (g_bal[idx] < 0 || g_bal[idx] > 1) ? "   <<< IMBALANCE" : "");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN:   button("LEFT",   0, 1, lp); return 0;
    case WM_LBUTTONUP:     button("LEFT",   0, 0, lp); return 0;
    case WM_RBUTTONDOWN:   button("RIGHT",  1, 1, lp); return 0;
    case WM_RBUTTONUP:     button("RIGHT",  1, 0, lp); return 0;
    case WM_MBUTTONDOWN:   button("MIDDLE", 2, 1, lp); return 0;
    case WM_MBUTTONUP:     button("MIDDLE", 2, 0, lp); return 0;

    /* 2 回目の押下は DOWN ではなく DBLCLK として届く。押下として数えないと
       収支が合わなくなるので、DOWN と同じ扱いにする。 */
    case WM_LBUTTONDBLCLK: button("LEFT-DBL",  0, 1, lp); return 0;
    case WM_RBUTTONDBLCLK: button("RIGHT-DBL", 1, 1, lp); return 0;
    case WM_MBUTTONDBLCLK: button("MID-DBL",   2, 1, lp); return 0;

    case WM_MOUSEWHEEL:    logline("WHEEL_V  %+d", GET_WHEEL_DELTA_WPARAM(wp)); return 0;
    case WM_MOUSEHWHEEL:   logline("WHEEL_H  %+d", GET_WHEEL_DELTA_WPARAM(wp)); return 0;

    /* サイドボタン。どちらかは wParam の上位ワードで分かる。 */
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
        button(HIWORD(wp) == XBUTTON2 ? "SIDE2" : "SIDE1",
               HIWORD(wp) == XBUTTON2 ? 4 : 3, 1, lp);
        return TRUE;
    case WM_XBUTTONUP:
        button(HIWORD(wp) == XBUTTON2 ? "SIDE2" : "SIDE1",
               HIWORD(wp) == XBUTTON2 ? 4 : 3, 0, lp);
        return TRUE;

    case WM_CONTEXTMENU:   logline("CONTEXTMENU");      return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect(dc, &r, (HBRUSH)(COLOR_WINDOW + 1));
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main(int argc, char **argv)
{
    WNDCLASSEXW wc;
    HWND hwnd;
    MSG msg;
    UINT seconds = 60;

    if (argc < 2) { fprintf(stderr, "usage: target <logfile> [seconds]\n"); return 1; }
    if (argc >= 3) seconds = (UINT)atoi(argv[2]);

    g_log = fopen(argv[1], "w");
    if (!g_log) return 1;
    g_t0 = GetTickCount64();

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = L"MayousTargetWnd";
    /* CS_DBLCLKS を付けないと二重クリックが押下2回に化けて収支が読みにくい */
    wc.style         = CS_DBLCLKS;
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(WS_EX_TOPMOST, L"MayousTargetWnd", L"Mayous test target",
                           WS_OVERLAPPEDWINDOW, 200, 200, 700, 500,
                           NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    logline("target ready");
    printf("READY\n");
    fflush(stdout);

    SetTimer(hwnd, 1, seconds * 1000, NULL);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_TIMER && msg.hwnd == hwnd) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    logline("target done  final balance L=%d R=%d M=%d X1=%d X2=%d",
            g_bal[0], g_bal[1], g_bal[2], g_bal[3], g_bal[4]);
    fclose(g_log);
    return 0;
}
