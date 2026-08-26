/* wheellat.c - 右クリック+ホイールに割り当てたキーが、実際に何 ms 遅れて
 *              出てくるかを測る。
 *
 *   ホイールを送った時刻と、キーボードフックがその押下を見た時刻の差を取る。
 *   引数: 回数(既定 8) / 1 回ごとの間隔 ms(既定 500)
 *   偶数回は上、奇数回は下 = 交互(利用者のテスト)。
 *   -same を付けると全部上(同じキーの繰り返し)。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static HHOOK  g_kb;
static LARGE_INTEGER g_freq, g_sent;
static volatile double g_lat = -1.0;
static int g_expect;

static double now_ms(void)
{
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - g_sent.QuadPart) * 1000.0 / (double)g_freq.QuadPart;
}

static LRESULT CALLBACK kb(int code, WPARAM w, LPARAM l)
{
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)l;
    if (code == HC_ACTION && (w == WM_KEYDOWN || w == WM_SYSKEYDOWN)) {
        if (g_lat < 0 && (int)k->vkCode == g_expect) g_lat = now_ms();
    }
    return CallNextHookEx(g_kb, code, w, l);
}

static void pump(int ms)
{
    DWORD end = GetTickCount() + (DWORD)ms;
    MSG m;
    for (;;) {
        DWORD left = end - GetTickCount();
        if ((int)left <= 0) break;
        if (MsgWaitForMultipleObjects(0, NULL, FALSE, left, QS_ALLINPUT) == WAIT_TIMEOUT) break;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
}

static void mouse(DWORD flags, DWORD data)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dwFlags   = flags;
    in.mi.mouseData = data;
    SendInput(1, &in, sizeof(in));
}

int main(int argc, char **argv)
{
    int n = 8, gap = 500, same = 0, npos = 0, i;
    double sum = 0; int cnt = 0;

    for (i = 1; i < argc; ++i) {
        if (!lstrcmpA(argv[i], "-same")) { same = 1; continue; }
        if (npos == 0) n = atoi(argv[i]);
        else if (npos == 1) gap = atoi(argv[i]);
        ++npos;
    }
    QueryPerformanceFrequency(&g_freq);

    g_kb = SetWindowsHookExW(WH_KEYBOARD_LL, kb, GetModuleHandleW(NULL), 0);
    if (!g_kb) { printf("hook 失敗\n"); return 1; }

    SetCursorPos(900, 500);
    pump(300);
    mouse(MOUSEEVENTF_RIGHTDOWN, 0);
    pump(300);

    for (i = 0; i < n; ++i) {
        int up = same ? 1 : ((i % 2) == 0);
        g_expect = up ? 'A' : 'B';
        g_lat = -1.0;
        QueryPerformanceCounter(&g_sent);
        mouse(MOUSEEVENTF_WHEEL, (DWORD)(up ? WHEEL_DELTA : -WHEEL_DELTA));
        pump(gap);
        if (g_lat >= 0) { printf("%2d %s -> %6.1f ms\n", i + 1, up ? "上(a)" : "下(b)", g_lat); sum += g_lat; ++cnt; }
        else            { printf("%2d %s -> 届かず\n",   i + 1, up ? "上(a)" : "下(b)"); }
    }

    mouse(MOUSEEVENTF_RIGHTUP, 0);
    pump(400);
    UnhookWindowsHookEx(g_kb);
    if (cnt) printf("届いた %d/%d  平均 %.1f ms\n", cnt, n, sum / cnt);
    else     printf("1 つも届かなかった\n");
    return 0;
}
